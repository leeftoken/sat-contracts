#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>

#include <string>

using namespace eosio;

// Test-only beneficiary that refuses incoming SAT, to show what happens to a
// vesting stream whose fixed beneficiary is a contract that rejects transfers.
class [[eosio::contract("rejector")]] rejector : public contract {
 public:
  using contract::contract;

  [[eosio::on_notify("sattoken1111::transfer")]]
  void on_sat(name from, name to, asset quantity, std::string memo) {
    if (to == get_self()) check(false, "rejector refuses SAT");
  }

  [[eosio::action]]
  void noop() { require_auth(get_self()); }
};
