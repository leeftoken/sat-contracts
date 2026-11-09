#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace eosio;

// Local mock of SAT@sattoken1111. Not the mainnet token.
class [[eosio::contract("sat.token")]] token : public contract {
 public:
  using contract::contract;

  [[eosio::action]]
  void create(name issuer, asset maximum_supply) {
    require_auth(get_self());
    auto sym = maximum_supply.symbol;
    check(sym.is_valid(), "invalid symbol");
    check(maximum_supply.is_valid(), "invalid supply");
    check(maximum_supply.amount > 0, "max supply must be positive");
    stats statstable(get_self(), sym.code().raw());
    check(statstable.find(sym.code().raw()) == statstable.end(), "token exists");
    statstable.emplace(get_self(), [&](auto& s) {
      s.supply.symbol = sym;
      s.supply.amount = 0;
      s.max_supply = maximum_supply;
      s.issuer = issuer;
    });
  }

  [[eosio::action]]
  void issue(name to, asset quantity, std::string memo) {
    check(memo.size() <= 256, "memo");
    auto sym = quantity.symbol;
    stats statstable(get_self(), sym.code().raw());
    auto st = statstable.require_find(sym.code().raw(), "token does not exist");
    require_auth(st->issuer);
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must issue positive quantity");
    check(quantity.symbol == st->supply.symbol, "symbol mismatch");
    check(quantity.amount <= st->max_supply.amount - st->supply.amount, "over max");
    statstable.modify(st, same_payer, [&](auto& s) { s.supply += quantity; });
    add_balance(to, quantity, st->issuer);
  }

  [[eosio::action]]
  void transfer(name from, name to, asset quantity, std::string memo) {
    require_auth(from);
    check(from != to, "cannot transfer to self");
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must transfer positive quantity");
    check(memo.size() <= 256, "memo");
    sub_balance(from, quantity);
    add_balance(to, quantity, from);
    require_recipient(from);
    require_recipient(to);
  }

 private:
  struct [[eosio::table]] account {
    asset balance;
    uint64_t primary_key() const { return balance.symbol.code().raw(); }
  };
  struct [[eosio::table]] currency_stats {
    asset supply;
    asset max_supply;
    name issuer;
    uint64_t primary_key() const { return supply.symbol.code().raw(); }
  };
  using stats = multi_index<"stat"_n, currency_stats>;
  using accounts = multi_index<"accounts"_n, account>;

  void add_balance(name owner, asset value, name ram_payer) {
    accounts accts(get_self(), owner.value);
    auto it = accts.find(value.symbol.code().raw());
    if (it == accts.end()) {
      accts.emplace(ram_payer, [&](auto& a) { a.balance = value; });
    } else {
      accts.modify(it, same_payer, [&](auto& a) { a.balance += value; });
    }
  }

  void sub_balance(name owner, asset value) {
    accounts accts(get_self(), owner.value);
    auto it = accts.require_find(value.symbol.code().raw(), "no balance");
    check(it->balance.amount >= value.amount, "overdrawn balance");
    check(it->balance.symbol == value.symbol, "symbol mismatch");
    if (it->balance.amount == value.amount) {
      accts.erase(it);
    } else {
      accts.modify(it, same_payer, [&](auto& a) { a.balance -= value; });
    }
  }
};
