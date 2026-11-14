// leeftrader — on-chain treasury + automation guard for the LEEF / SAT system.
//
// Holds revenue (subscriptions, deposits), splits it into buckets by configurable basis points,
// and lets ONE dedicated bot account run a narrow set of guarded actions:
//   buyback  : swap via a whitelisted DEX; contract builds the memo itself, recipient is always
//              this contract, and an inline `checkout` reverts the whole tx if output < min_out.
//   addlp    : deposit into Alcor CLMM, mint a position owned by this contract, then lock it with
//              Alcor's NATIVE `lockpos` (default 42 years). This contract has no subliquid
//              action at all, so liquidity can never be removed through it.
//   collectfees : Alcor `collect` on a locked position; fees go only to cfg.fee_recipient.
//   payout   : move bucket funds only to whitelisted destinations.
// Funds can never leave to a non-whitelisted account. Admin (multisig) sets config; bot can only pause.
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
using namespace eosio;
using std::string;

class [[eosio::contract("leeftrader")]] leeftrader : public contract {
public:
   using contract::contract;

   static constexpr uint32_t DEFAULT_LOCK_SECS = 1325419200; // 42 * 365.25 days
   static constexpr uint32_t MIN_LOCK_SECS     = 365u * 86400u; // never lock shorter than 1 year

   // ---------------- admin ----------------
   [[eosio::action]] void init(name admin, name bot, name fee_recipient, name swap_contract) {
      require_auth(get_self());
      cfg_t c(get_self(), get_self().value);
      check(!c.exists(), "already initialised");
      // admin + fee_recipient are 2-of-3 multisig accounts (owner override, Oct 23 2026: signers may be
      // trading accounts). The bot can never be a trading account.
      check(is_account(admin) && is_account(fee_recipient), "account does not exist");
      check_actor(bot); check(bot != fee_recipient, "bot and fee recipient must differ");
      check(admin != bot, "admin and bot must differ");
      check(is_account(swap_contract), "swap contract missing");
      config x;
      x.admin = admin; x.bot = bot; x.fee_recipient = fee_recipient; x.swap = swap_contract;
      c.set(x, get_self());
      dests_t d(get_self(), get_self().value);
      d.emplace(get_self(), [&](auto& r) { r.account = fee_recipient; });
      swaps_t s(get_self(), get_self().value);
      s.emplace(get_self(), [&](auto& r) { r.contract = swap_contract; });
   }

   [[eosio::action]] void setlimits(uint16_t max_in_bps, uint16_t daily_cap_bps, uint32_t lock_secs) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      check(max_in_bps > 0 && max_in_bps <= 10000, "max_in_bps 1..10000");
      check(daily_cap_bps > 0 && daily_cap_bps <= 10000, "daily_cap_bps 1..10000");
      check(lock_secs >= MIN_LOCK_SECS, "lock too short");
      check((uint64_t)current_time_point().sec_since_epoch() + lock_secs <= 0xFFFFFFFFull, "lock past uint32 max (2106)");
      c.max_in_bps = max_in_bps; c.daily_cap_bps = daily_cap_bps; c.lock_secs = lock_secs;
      cfg_t(get_self(), get_self().value).set(c, get_self());
   }

   [[eosio::action]] void setbot(name bot) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      check_actor(bot); check(bot != c.admin && bot != c.fee_recipient, "bot must differ from admin and fee recipient");
      c.bot = bot; cfg_t(get_self(), get_self().value).set(c, get_self());
   }

   [[eosio::action]] void setfeercpt(name fee_recipient) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      check(is_account(fee_recipient), "account does not exist"); check(fee_recipient != c.bot, "fee recipient must differ from bot");
      dests_t old(get_self(), get_self().value); auto o = old.find(c.fee_recipient.value);
      if (o != old.end()) old.erase(o); // the old fee multisig stops being a payout target
      c.fee_recipient = fee_recipient; cfg_t(get_self(), get_self().value).set(c, get_self());
      dests_t d(get_self(), get_self().value);
      if (d.find(fee_recipient.value) == d.end()) d.emplace(get_self(), [&](auto& r) { r.account = fee_recipient; });
   }

   // splits: buckets and basis points, must total 10000. Last entry absorbs rounding dust.
   [[eosio::action]] void setsplit(std::vector<name> buckets, std::vector<uint16_t> bps) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      check(buckets.size() == bps.size() && !buckets.empty() && buckets.size() <= 10, "bad split");
      uint32_t sum = 0; for (auto b : bps) sum += b;
      check(sum == 10000, "split must total 10000 bps");
      splits_t s(get_self(), get_self().value);
      for (auto it = s.begin(); it != s.end();) it = s.erase(it);
      for (size_t i = 0; i < buckets.size(); ++i) {
         check(s.find(buckets[i].value) == s.end(), "duplicate bucket");
         s.emplace(get_self(), [&](auto& r) { r.bucket = buckets[i]; r.bps = bps[i]; r.ord = i; });
      }
   }

   [[eosio::action]] void settoken(extended_symbol token, bool allowed) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      tokens_t t(get_self(), get_self().value);
      auto it = t.find(token.get_symbol().code().raw());
      if (allowed) {
         check(it == t.end(), "symbol already listed");
         t.emplace(get_self(), [&](auto& r) { r.token = token; });
      } else { check(it != t.end(), "not listed"); t.erase(it); }
   }

   [[eosio::action]] void setswap(name contract, bool allowed) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      swaps_t s(get_self(), get_self().value); auto it = s.find(contract.value);
      if (allowed) { check(is_account(contract), "no account"); if (it == s.end()) s.emplace(get_self(), [&](auto& r) { r.contract = contract; }); }
      else if (it != s.end()) s.erase(it);
   }

   [[eosio::action]] void setdest(name account, bool allowed) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      check_actor(account);
      dests_t d(get_self(), get_self().value); auto it = d.find(account.value);
      if (allowed) { if (it == d.end()) d.emplace(get_self(), [&](auto& r) { r.account = account; }); }
      else if (it != d.end()) d.erase(it);
   }

   // Alcor pool ids the bot may route through or add liquidity to (blocks attacker-made pools).
   [[eosio::action]] void setpool(uint64_t pool_id, bool allowed) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      pools_t p(get_self(), get_self().value); auto it = p.find(pool_id);
      if (allowed) { if (it == p.end()) p.emplace(get_self(), [&](auto& r) { r.id = pool_id; }); }
      else if (it != p.end()) p.erase(it);
   }

   [[eosio::action]] void setplan(uint64_t id, extended_asset price, uint32_t days, bool active) {
      auto c = cfg(); require_auth(permission_level{c.admin, "active"_n});
      check(price.quantity.amount > 0 && days > 0 && days <= 366, "bad plan");
      plans_t p(get_self(), get_self().value); auto it = p.find(id);
      if (it == p.end()) p.emplace(get_self(), [&](auto& r) { r.id = id; r.price = price; r.days = days; r.active = active; });
      else p.modify(it, same_payer, [&](auto& r) { r.price = price; r.days = days; r.active = active; });
   }

   // admin can pause/unpause; bot may only pause (emergency stop, e.g. 10% drawdown).
   [[eosio::action]] void pause(name actor, bool paused) {
      auto c = cfg();
      if (actor == c.admin) require_auth(permission_level{actor, "active"_n});
      else if (actor == c.bot && paused) require_auth(permission_level{actor, "bot"_n});
      else check(false, "not allowed");
      c.paused = paused; cfg_t(get_self(), get_self().value).set(c, get_self());
   }

   // ---------------- incoming funds ----------------
   [[eosio::on_notify("*::transfer")]] void on_transfer(name from, name to, asset quantity, string memo) {
      if (to != get_self() || from == get_self()) return;
      name tc = get_first_receiver();
      // proceeds from swaps / Alcor withdrawals are accounted by checkout / lpwithdraw, not as revenue
      swaps_t s(get_self(), get_self().value);
      if (s.find(from.value) != s.end()) return;
      tokens_t t(get_self(), get_self().value);
      auto tk = t.find(quantity.symbol.code().raw());
      check(tk != t.end() && tk->token.get_contract() == tc && tk->token.get_symbol() == quantity.symbol, "token not accepted");
      check(quantity.amount > 0, "zero");
      extended_asset ea(quantity, tc);
      if (memo.rfind("sub:", 0) == 0) handle_sub(from, ea, memo);
      split_in(ea);
      action(permission_level{get_self(), "active"_n}, get_self(), "logrevenue"_n, std::make_tuple(from, ea, memo)).send();
   }

   // ---------------- bot actions ----------------
   [[eosio::action]] void buyback(name bucket, extended_asset in, name swap, string pool_path, extended_asset min_out) {
      auto c = guard_bot();
      swaps_t s(get_self(), get_self().value);
      check(s.find(swap.value) != s.end(), "swap contract not whitelisted");
      check(in.quantity.amount > 0 && min_out.quantity.amount > 0, "amounts must be positive");
      check(valid_path(pool_path), "pool_path must be digits and commas");
      if (in.get_extended_symbol() == min_out.get_extended_symbol())
         check(min_out.quantity.amount >= in.quantity.amount + 1, "same-token loop needs min_out >= in + 1 unit");
      tokens_t t(get_self(), get_self().value);
      auto ot = t.find(min_out.quantity.symbol.code().raw());
      check(ot != t.end() && ot->token == min_out.get_extended_symbol(), "output token not whitelisted");
      check_path_pools(pool_path);
      spend(c, bucket, in);
      pend_t p(get_self(), get_self().value);
      check(!p.exists(), "swap already pending");
      // The input is already deducted by spend() and will leave this contract.
      // For same-token routes, snapshot the post-input baseline so checkout
      // measures gross output. For different tokens the output balance is unaffected.
      int64_t output_baseline = balance_of(min_out.contract, min_out.quantity.symbol);
      if (in.get_extended_symbol() == min_out.get_extended_symbol()) {
         check(output_baseline >= in.quantity.amount, "same-token input exceeds contract token balance");
         output_baseline -= in.quantity.amount;
      }
      pending x{bucket, min_out, output_baseline};
      p.set(x, get_self());
      string memo = "swapexactin#" + pool_path + "#" + get_self().to_string() + "#" +
                    min_out.quantity.to_string() + "@" + min_out.contract.to_string() + "#" +
                    std::to_string(current_time_point().sec_since_epoch() + 300);
      send_transfer(in, swap, memo);
      action(permission_level{get_self(), "active"_n}, get_self(), "checkout"_n, std::make_tuple()).send();
   }

   [[eosio::action]] void checkout() {
      require_auth(get_self());
      pend_t p(get_self(), get_self().value);
      check(p.exists(), "nothing pending");
      auto x = p.get();
      int64_t now = balance_of(x.min_out.contract, x.min_out.quantity.symbol);
      int64_t got = now - x.before;
      check(got >= x.min_out.quantity.amount, "output below min_out, reverting");
      extended_asset out(asset(got, x.min_out.quantity.symbol), x.min_out.contract);
      credit(x.bucket, out);
      p.remove();
      action(permission_level{get_self(), "active"_n}, get_self(), "logswap"_n, std::make_tuple(x.bucket, x.min_out, out)).send();
   }

   [[eosio::action]] void addlp(name bucket, uint64_t pool_id, extended_asset a, extended_asset b,
                                int32_t tick_lower, int32_t tick_upper, asset min_a, asset min_b) {
      auto c = guard_bot();
      check(a.quantity.amount > 0 && b.quantity.amount > 0, "amounts must be positive");
      check(tick_lower < tick_upper, "bad ticks");
      check(pools_t(get_self(), get_self().value).find(pool_id) != pools_t(get_self(), get_self().value).end(), "pool not whitelisted");
      check(min_a.amount > 0 && min_b.amount > 0, "min amounts must be positive");
      spend(c, bucket, a); spend(c, bucket, b);
      send_transfer(a, c.swap, "deposit");
      send_transfer(b, c.swap, "deposit");
      uint32_t now = current_time_point().sec_since_epoch();
      action(permission_level{get_self(), "active"_n}, c.swap, "addliquid"_n,
             std::make_tuple(pool_id, get_self(), a.quantity, b.quantity, tick_lower, tick_upper, min_a, min_b, now + 300)).send();
      check((uint64_t)now + c.lock_secs <= 0xFFFFFFFFull, "lock past uint32 max");
      uint32_t unlock = now + c.lock_secs;
      // Alcor native lock: blocks subliquid + transferpos until unlock; fees stay collectable.
      action(permission_level{get_self(), "active"_n}, c.swap, "lockpos"_n,
             std::make_tuple(pool_id, get_self(), tick_lower, tick_upper, unlock)).send();
      lps_t l(get_self(), get_self().value);
      auto it = std::find_if(l.begin(), l.end(), [&](const lprow& r) { return r.pool_id == pool_id && r.tick_lower == tick_lower && r.tick_upper == tick_upper; });
      if (it == l.end()) l.emplace(get_self(), [&](auto& r) { r.id = l.available_primary_key(); r.pool_id = pool_id; r.tick_lower = tick_lower; r.tick_upper = tick_upper; r.unlock = unlock; r.bucket = bucket; });
      else l.modify(it, same_payer, [&](auto& r) { r.unlock = std::max(r.unlock, unlock); });
      action(permission_level{get_self(), "active"_n}, get_self(), "loglp"_n, std::make_tuple(pool_id, a, b, unlock)).send();
   }

   // leftovers that Alcor kept as deposit after addliquid go back to the same bucket
   [[eosio::action]] void lpwithdraw(name bucket, extended_asset amount) {
      auto c = guard_bot();
      check(amount.quantity.amount > 0, "positive");
      action(permission_level{get_self(), "active"_n}, c.swap, "withdraw"_n, std::make_tuple(get_self(), amount)).send();
      credit(bucket, amount); // if Alcor's withdraw fails the whole tx reverts
   }

   [[eosio::action]] void collectfees(name actor, uint64_t lp_id, asset max_a, asset max_b) {
      auto c = cfg();
      if (actor == c.admin) require_auth(permission_level{actor, "active"_n});
      else if (actor == c.bot) require_auth(permission_level{actor, "bot"_n});
      else check(false, "not allowed");
      lps_t l(get_self(), get_self().value);
      const auto& r = l.get(lp_id, "unknown lp");
      action(permission_level{get_self(), "active"_n}, c.swap, "collect"_n,
             std::make_tuple(r.pool_id, get_self(), c.fee_recipient, r.tick_lower, r.tick_upper, max_a, max_b)).send();
   }

   [[eosio::action]] void payout(name bucket, name to, extended_asset quantity, string memo) {
      // Outbound treasury payouts require the admin's exact active permission.
      auto c = cfg();
      require_auth(permission_level{c.admin, "active"_n});
      check(!c.paused, "paused");
      dests_t d(get_self(), get_self().value);
      check(d.find(to.value) != d.end(), "destination not whitelisted");
      if (to != c.fee_recipient) check_actor(to); // trading accounts only ever receive as the fee multisig
      spend(c, bucket, quantity);
      send_transfer(quantity, to, memo);
   }

   // ---------------- logs (readable by bot + leef-trader platform via Hyperion) ----------------
   [[eosio::action]] void logrevenue(name from, extended_asset amount, string memo) { require_auth(get_self()); }
   [[eosio::action]] void logsub(name user, uint64_t plan, time_point_sec expires) { require_auth(get_self()); }
   [[eosio::action]] void logswap(name bucket, extended_asset min_out, extended_asset got) { require_auth(get_self()); }
   [[eosio::action]] void loglp(uint64_t pool_id, extended_asset a, extended_asset b, uint32_t unlock) { require_auth(get_self()); }

   // ---------------- tables ----------------
   struct [[eosio::table("config")]] config {
      name admin, bot, fee_recipient, swap;
      bool paused = false;
      uint16_t max_in_bps = 500;      // one action may use at most 5% of a bucket balance
      uint16_t daily_cap_bps = 2000;  // at most 20% of a bucket per UTC day
      uint32_t lock_secs = DEFAULT_LOCK_SECS;
   };
   struct [[eosio::table]] split { name bucket; uint16_t bps; uint64_t ord; uint64_t primary_key() const { return bucket.value; } };
   struct [[eosio::table]] token { extended_symbol token; uint64_t primary_key() const { return token.get_symbol().code().raw(); } };
   struct [[eosio::table]] swapc { name contract; uint64_t primary_key() const { return contract.value; } };
   struct [[eosio::table]] dest { name account; uint64_t primary_key() const { return account.value; } };
   struct [[eosio::table]] plan { uint64_t id; extended_asset price; uint32_t days; bool active; uint64_t primary_key() const { return id; } };
   struct [[eosio::table]] sub { name user; uint64_t plan; time_point_sec expires; uint64_t primary_key() const { return user.value; } };
   // scope = bucket
   struct [[eosio::table]] bucketbal { extended_asset balance; uint32_t day = 0; int64_t spent_today = 0;
      uint64_t primary_key() const { return balance.quantity.symbol.code().raw(); } };
   struct [[eosio::table("pending")]] pending { name bucket; extended_asset min_out; int64_t before; };
   struct [[eosio::table]] poolok { uint64_t id; uint64_t primary_key() const { return id; } };
   struct [[eosio::table]] lprow { uint64_t id; uint64_t pool_id; int32_t tick_lower, tick_upper; uint32_t unlock; name bucket;
      uint64_t primary_key() const { return id; } };

   typedef singleton<"config"_n, config> cfg_t;
   typedef singleton<"pending"_n, pending> pend_t;
   typedef multi_index<"splits"_n, split> splits_t;
   typedef multi_index<"tokens"_n, token> tokens_t;
   typedef multi_index<"swaps"_n, swapc> swaps_t;
   typedef multi_index<"dests"_n, dest> dests_t;
   typedef multi_index<"plans"_n, plan> plans_t;
   typedef multi_index<"subs"_n, sub> subs_t;
   typedef multi_index<"buckets"_n, bucketbal> buckets_t;
   typedef multi_index<"lps"_n, lprow> lps_t;
   typedef multi_index<"pools"_n, poolok> pools_t;

private:

   config cfg() { cfg_t c(get_self(), get_self().value); check(c.exists(), "not initialised"); return c.get(); }

   // Hard rule: live trading accounts may never be the bot or a payout destination (except as the fee multisig).
   static void check_actor(name n) {
      static const name banned[] = {"mywaxgallery"_n, "danishcrypto"_n, "qu.ui.wam"_n, "skgaw.wam"_n,
                                    "dustwizard42"_n, "niftywarlord"_n, "smart.ass"_n};
      for (auto b : banned) check(n != b, "trading accounts may not hold contract roles");
      check(is_account(n), "account does not exist");
   }

   config guard_bot() { auto c = cfg(); require_auth(permission_level{c.bot, "bot"_n}); check(!c.paused, "paused"); return c; }

   static bool valid_path(const string& p) {
      if (p.empty() || p.size() > 64) return false;
      for (char ch : p) if (!((ch >= '0' && ch <= '9') || ch == ',')) return false;
      return true;
   }

   void check_path_pools(const string& p) {
      pools_t pl(get_self(), get_self().value);
      size_t i = 0;
      while (i < p.size()) {
         size_t j = p.find(',', i); if (j == string::npos) j = p.size();
         check(j > i && j - i <= 18, "bad pool id");
         uint64_t id = 0; for (size_t k = i; k < j; ++k) id = id * 10 + (p[k] - '0');
         check(pl.find(id) != pl.end(), "pool not whitelisted");
         i = j + 1;
      }
      check(p.back() != ',', "bad pool path");
   }

   int64_t balance_of(name tc, symbol sym) {
      // read the standard eosio.token `accounts` row of this contract (no ABI entry needed)
      struct acct { asset balance; uint64_t primary_key() const { return balance.symbol.code().raw(); } };
      multi_index<"accounts"_n, acct> a(tc, get_self().value); auto it = a.find(sym.code().raw());
      return it == a.end() ? 0 : it->balance.amount;
   }

   void credit(name bucket, const extended_asset& v) {
      buckets_t b(get_self(), bucket.value); auto it = b.find(v.quantity.symbol.code().raw());
      if (it == b.end()) b.emplace(get_self(), [&](auto& r) { r.balance = v; });
      else { check(it->balance.contract == v.contract, "symbol clash across contracts");
             b.modify(it, same_payer, [&](auto& r) { r.balance.quantity += v.quantity; }); }
   }

   void spend(const config& c, name bucket, const extended_asset& v) {
      buckets_t b(get_self(), bucket.value);
      const auto& r = b.get(v.quantity.symbol.code().raw(), "bucket has no such token");
      check(r.balance.contract == v.contract, "wrong token contract");
      check(r.balance.quantity.amount >= v.quantity.amount, "bucket balance too low");
      check((__int128)v.quantity.amount * 10000 <= (__int128)r.balance.quantity.amount * c.max_in_bps, "exceeds per-action cap");
      uint32_t day = current_time_point().sec_since_epoch() / 86400;
      int64_t spent = (r.day == day ? r.spent_today : 0) + v.quantity.amount;
      int64_t base = r.balance.quantity.amount + (r.day == day ? r.spent_today : 0);
      check((__int128)spent * 10000 <= (__int128)base * c.daily_cap_bps, "exceeds daily cap");
      b.modify(r, same_payer, [&](auto& x) { x.balance.quantity -= v.quantity; x.day = day; x.spent_today = spent; });
   }

   void split_in(const extended_asset& v) {
      splits_t s(get_self(), get_self().value);
      check(s.begin() != s.end(), "split not set");
      std::vector<split> rows(s.begin(), s.end());
      std::sort(rows.begin(), rows.end(), [](const split& a, const split& b) { return a.ord < b.ord; });
      int64_t left = v.quantity.amount;
      for (size_t i = 0; i < rows.size(); ++i) {
         int64_t part = (i + 1 == rows.size()) ? left : (int64_t)((__int128)v.quantity.amount * rows[i].bps / 10000);
         left -= part;
         if (part > 0) credit(rows[i].bucket, extended_asset(asset(part, v.quantity.symbol), v.contract));
      }
   }

   void handle_sub(name from, const extended_asset& paid, const string& memo) {
      // memo: sub:<plan_id>[:<user>]  (user defaults to payer)
      auto p2 = memo.find(':', 4);
      string ids = memo.substr(4, p2 == string::npos ? string::npos : p2 - 4);
      check(!ids.empty() && ids.size() <= 18, "bad plan id");
      uint64_t id = 0; for (char ch : ids) { check(ch >= '0' && ch <= '9', "bad plan id"); id = id * 10 + (ch - '0'); }
      name user = p2 == string::npos ? from : name(memo.substr(p2 + 1));
      check(is_account(user), "user account missing");
      plans_t p(get_self(), get_self().value);
      const auto& pl = p.get(id, "unknown plan");
      check(pl.active, "plan inactive");
      check(paid.get_extended_symbol() == pl.price.get_extended_symbol(), "wrong token for plan");
      check(paid.quantity.amount >= pl.price.quantity.amount, "underpaid");
      subs_t s(get_self(), get_self().value);
      auto now = current_time_point().sec_since_epoch();
      auto it = s.find(user.value);
      uint32_t start = (it != s.end() && it->expires.sec_since_epoch() > now) ? it->expires.sec_since_epoch() : now;
      time_point_sec exp(start + pl.days * 86400);
      if (it == s.end()) s.emplace(get_self(), [&](auto& r) { r.user = user; r.plan = id; r.expires = exp; });
      else s.modify(it, same_payer, [&](auto& r) { r.plan = id; r.expires = exp; });
      action(permission_level{get_self(), "active"_n}, get_self(), "logsub"_n, std::make_tuple(user, id, exp)).send();
   }

   void send_transfer(const extended_asset& v, name to, const string& memo) {
      action(permission_level{get_self(), "active"_n}, v.contract, "transfer"_n,
             std::make_tuple(get_self(), to, v.quantity, memo)).send();
   }
};
