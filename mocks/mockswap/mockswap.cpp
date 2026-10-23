// TEST ONLY. Minimal stand-in for swap.alcor's ABI surface used by leeftrader
// (swapexactin memo, deposit memo, addliquid, lockpos, subliquid, collect, withdraw).
// Lock semantics follow Alcor docs: lock blocks subliquid/transferpos, can only be extended,
// fees stay collectable. Never deploy anywhere but a local test chain.
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>
using namespace eosio; using std::string;

class [[eosio::contract("mockswap")]] mockswap : public contract {
public:
   using contract::contract;
   struct [[eosio::table("out")]] outcfg { extended_asset out; extended_asset fee; };
   struct [[eosio::table]] pos { uint64_t id; uint64_t pool; name owner; int32_t tl, tu; uint64_t liq; uint32_t unlock;
      uint64_t primary_key() const { return id; } };
   typedef singleton<"out"_n, outcfg> out_t;
   typedef multi_index<"positions"_n, pos> pos_t;

   [[eosio::action]] void setout(extended_asset out, extended_asset fee) { require_auth(get_self()); out_t(get_self(), get_self().value).set(outcfg{out, fee}, get_self()); }

   [[eosio::on_notify("*::transfer")]] void on_transfer(name from, name to, asset q, string memo) {
      if (to != get_self() || from == get_self()) return;
      if (memo == "deposit" || memo == "fund") return;
      check(memo.rfind("swapexactin#", 0) == 0, "bad memo");
      auto p1 = memo.find('#', 12); auto p2 = memo.find('#', p1 + 1);
      name rcpt(memo.substr(p1 + 1, p2 - p1 - 1));
      auto c = out_t(get_self(), get_self().value).get();
      send(c.out, rcpt, "mock swap out");
   }
   [[eosio::action]] void addliquid(uint64_t poolId, name owner, asset a, asset b, int32_t tl, int32_t tu, asset amin, asset bmin, uint32_t deadline) {
      require_auth(owner);
      pos_t p(get_self(), get_self().value);
      for (auto& r : p) if (r.pool == poolId && r.owner == owner && r.tl == tl && r.tu == tu) { p.modify(r, same_payer, [&](auto& x) { x.liq += a.amount; }); return; }
      p.emplace(owner, [&](auto& r) { r.id = p.available_primary_key(); r.pool = poolId; r.owner = owner; r.tl = tl; r.tu = tu; r.liq = a.amount; r.unlock = 0; });
   }
   [[eosio::action]] void lockpos(uint64_t poolId, name owner, int32_t tl, int32_t tu, uint32_t unlockTime) {
      require_auth(owner);
      pos_t p(get_self(), get_self().value);
      for (auto it = p.begin(); it != p.end(); ++it) if (it->pool == poolId && it->owner == owner && it->tl == tl && it->tu == tu) {
         check(unlockTime >= it->unlock, "lock can only be extended");
         p.modify(it, same_payer, [&](auto& x) { x.unlock = unlockTime; }); return; }
      check(false, "position not found");
   }
   [[eosio::action]] void subliquid(uint64_t poolId, name owner, uint64_t liquidity, int32_t tl, int32_t tu, asset amin, asset bmin, uint32_t deadline) {
      require_auth(owner); uint32_t u = find(poolId, owner, tl, tu).unlock;
      check(current_time_point().sec_since_epoch() >= u, "position is locked");
   }
   [[eosio::action]] void collect(uint64_t poolId, name owner, name recipient, int32_t tl, int32_t tu, asset amax, asset bmax) {
      require_auth(owner); find(poolId, owner, tl, tu);
      send(out_t(get_self(), get_self().value).get().fee, recipient, "mock fees");
   }
   [[eosio::action]] void withdraw(name owner, extended_asset assetInput) { require_auth(owner); send(assetInput, owner, "withdraw"); }
private:
   pos find(uint64_t poolId, name owner, int32_t tl, int32_t tu) {
      pos_t p(get_self(), get_self().value);
      for (auto it = p.begin(); it != p.end(); ++it) if (it->pool == poolId && it->owner == owner && it->tl == tl && it->tu == tu) return *it;
      check(false, "position not found"); return *p.begin();
   }
   void send(const extended_asset& v, name to, const string& memo) {
      action(permission_level{get_self(), "active"_n}, v.contract, "transfer"_n, std::make_tuple(get_self(), to, v.quantity, memo)).send();
   }
};
