#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

// Minimal eosio.token-compatible SAT ledger for local tests only.
// Not the mainnet sattoken1111 contract.

using namespace eosio;

class [[eosio::contract("sat.token")]] token : public contract {
 public:
  using contract::contract;

  [[eosio::action]]
  void create(name issuer, asset maximum_supply) {
    require_auth(get_self());
    const auto sym = maximum_supply.symbol;
    check(sym.is_valid(), "invalid symbol name");
    check(maximum_supply.is_valid(), "invalid supply");
    check(maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable(get_self(), sym.code().raw());
    check(statstable.find(sym.code().raw()) == statstable.end(), "token with symbol already exists");
    statstable.emplace(get_self(), [&](auto& s) {
      s.supply = asset{0, maximum_supply.symbol};
      s.max_supply = maximum_supply;
      s.issuer = issuer;
    });
  }

  [[eosio::action]]
  void issue(name to, asset quantity, std::string memo) {
    const auto sym = quantity.symbol;
    check(sym.is_valid(), "invalid symbol name");
    check(memo.size() <= 256, "memo has more than 256 bytes");

    stats statstable(get_self(), sym.code().raw());
    auto existing = statstable.find(sym.code().raw());
    check(existing != statstable.end(), "token with symbol does not exist");
    const auto& st = *existing;
    require_auth(st.issuer);
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must issue positive quantity");
    check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
    check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify(st, same_payer, [&](auto& s) { s.supply += quantity; });
    add_balance(st.issuer, quantity, st.issuer);
    if (to != st.issuer) {
      transfer(st.issuer, to, quantity, memo);
    }
  }

  [[eosio::action]]
  void transfer(name from, name to, asset quantity, std::string memo) {
    check(from != to, "cannot transfer to self");
    require_auth(from);
    check(is_account(to), "to account does not exist");
    check(memo.size() <= 256, "memo has more than 256 bytes");

    const auto sym_code = quantity.symbol.code();
    stats statstable(get_self(), sym_code.raw());
    const auto& st = statstable.get(sym_code.raw(), "token with symbol does not exist");
    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must transfer positive quantity");
    check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");

    require_recipient(from);
    require_recipient(to);
    sub_balance(from, quantity);
    add_balance(to, quantity, from);
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

  using accounts = multi_index<"accounts"_n, account>;
  using stats = multi_index<"stat"_n, currency_stats>;

  void sub_balance(name owner, asset value) {
    accounts from_acnts(get_self(), owner.value);
    const auto& from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
    check(from.balance.amount >= value.amount, "overdrawn balance");
    check(from.balance.symbol == value.symbol, "symbol precision mismatch");
    from_acnts.modify(from, owner, [&](auto& a) { a.balance -= value; });
  }

  void add_balance(name owner, asset value, name ram_payer) {
    accounts to_acnts(get_self(), owner.value);
    auto to = to_acnts.find(value.symbol.code().raw());
    if (to == to_acnts.end()) {
      to_acnts.emplace(ram_payer, [&](auto& a) { a.balance = value; });
    } else {
      to_acnts.modify(to, same_payer, [&](auto& a) { a.balance += value; });
    }
  }
};
