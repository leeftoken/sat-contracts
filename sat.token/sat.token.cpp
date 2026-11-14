// sat.token — standard eosio.token-compatible contract for SAT (Smart Ass Trader).
// Behaviour mirrors AntelopeIO reference eosio.token (create/issue/retire/transfer/open/close),
// so wallets, Alcor and explorers treat SAT like any WAX token.
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
using namespace eosio;

class [[eosio::contract("sat.token")]] sattoken : public contract {
public:
   using contract::contract;

   [[eosio::action]] void create(const name& issuer, const asset& maximum_supply) {
      require_auth(get_self());
      auto sym = maximum_supply.symbol;
      check(sym.is_valid(), "invalid symbol name");
      check(maximum_supply.is_valid(), "invalid supply");
      check(maximum_supply.amount > 0, "max-supply must be positive");
      stats st(get_self(), sym.code().raw());
      check(st.find(sym.code().raw()) == st.end(), "token with symbol already exists");
      st.emplace(get_self(), [&](auto& s) { s.supply.symbol = sym; s.max_supply = maximum_supply; s.issuer = issuer; });
   }

   [[eosio::action]] void issue(const name& to, const asset& quantity, const std::string& memo) {
      auto sym = quantity.symbol;
      check(sym.is_valid(), "invalid symbol name");
      check(memo.size() <= 256, "memo has more than 256 bytes");
      stats st(get_self(), sym.code().raw());
      const auto& s = st.get(sym.code().raw(), "token with symbol does not exist, create token before issue");
      check(to == s.issuer, "tokens can only be issued to issuer account");
      require_auth2(s.issuer.value, "active"_n.value);
      check(quantity.is_valid() && quantity.amount > 0, "must issue positive quantity");
      check(sym == s.supply.symbol, "symbol precision mismatch");
      check(quantity.amount <= s.max_supply.amount - s.supply.amount, "quantity exceeds available supply");
      st.modify(s, same_payer, [&](auto& x) { x.supply += quantity; });
      add_balance(s.issuer, quantity, s.issuer);
   }

   [[eosio::action]] void retire(const asset& quantity, const std::string& memo) {
      auto sym = quantity.symbol;
      check(memo.size() <= 256, "memo has more than 256 bytes");
      stats st(get_self(), sym.code().raw());
      const auto& s = st.get(sym.code().raw(), "token with symbol does not exist");
      require_auth2(s.issuer.value, "active"_n.value);
      check(quantity.is_valid() && quantity.amount > 0, "must retire positive quantity");
      check(sym == s.supply.symbol, "symbol precision mismatch");
      st.modify(s, same_payer, [&](auto& x) { x.supply -= quantity; });
      sub_balance(s.issuer, quantity);
   }

   [[eosio::action]] void transfer(const name& from, const name& to, const asset& quantity, const std::string& memo) {
      check(from != to, "cannot transfer to self");
      require_auth(from);
      check(is_account(to), "to account does not exist");
      stats st(get_self(), quantity.symbol.code().raw());
      const auto& s = st.get(quantity.symbol.code().raw(), "token does not exist");
      require_recipient(from);
      require_recipient(to);
      check(quantity.is_valid() && quantity.amount > 0, "must transfer positive quantity");
      check(quantity.symbol == s.supply.symbol, "symbol precision mismatch");
      check(memo.size() <= 256, "memo has more than 256 bytes");
      auto payer = has_auth(to) ? to : from;
      sub_balance(from, quantity);
      add_balance(to, quantity, payer);
   }

   [[eosio::action]] void open(const name& owner, const symbol& symbol, const name& ram_payer) {
      require_auth(ram_payer);
      check(is_account(owner), "owner account does not exist");
      stats st(get_self(), symbol.code().raw());
      const auto& s = st.get(symbol.code().raw(), "symbol does not exist");
      check(s.supply.symbol == symbol, "symbol precision mismatch");
      accounts acnts(get_self(), owner.value);
      if (acnts.find(symbol.code().raw()) == acnts.end())
         acnts.emplace(ram_payer, [&](auto& a) { a.balance = asset{0, symbol}; });
   }

   [[eosio::action]] void close(const name& owner, const symbol& symbol) {
      require_auth(owner);
      accounts acnts(get_self(), owner.value);
      auto it = acnts.find(symbol.code().raw());
      check(it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
      check(it->balance.amount == 0, "Cannot close because the balance is not zero.");
      acnts.erase(it);
   }

private:
   struct [[eosio::table]] account { asset balance; uint64_t primary_key() const { return balance.symbol.code().raw(); } };
   struct [[eosio::table]] currency_stats { asset supply; asset max_supply; name issuer; uint64_t primary_key() const { return supply.symbol.code().raw(); } };
   typedef multi_index<"accounts"_n, account> accounts;
   typedef multi_index<"stat"_n, currency_stats> stats;

   void sub_balance(const name& owner, const asset& value) {
      accounts from_acnts(get_self(), owner.value);
      const auto& from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
      check(from.balance.amount >= value.amount, "overdrawn balance");
      from_acnts.modify(from, owner, [&](auto& a) { a.balance -= value; });
   }
   void add_balance(const name& owner, const asset& value, const name& ram_payer) {
      accounts to_acnts(get_self(), owner.value);
      auto to = to_acnts.find(value.symbol.code().raw());
      if (to == to_acnts.end()) to_acnts.emplace(ram_payer, [&](auto& a) { a.balance = value; });
      else to_acnts.modify(to, same_payer, [&](auto& a) { a.balance += value; });
   }
};
