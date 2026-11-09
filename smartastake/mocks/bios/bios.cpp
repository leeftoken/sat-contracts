#include <eosio/eosio.hpp>
#include <eosio/privileged.hpp>

using namespace eosio;

// Tiny privileged helper so local nodeos accounts can hold table rows.
// Deployed to `eosio` only inside tests. Not a mainnet artifact.
class [[eosio::contract("bios")]] bios : public contract {
 public:
  using contract::contract;

  [[eosio::action]]
  void setalimits(name account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight) {
    require_auth(get_self());
    set_resource_limits(account, ram_bytes, net_weight, cpu_weight);
  }
};
