#include <eosio/eosio.hpp>

// Test helper: fires two claim actions in one transaction. The second must fail
// and roll the whole transaction back so nothing is overpaid.

using namespace eosio;

class [[eosio::contract("doubleclaim")]] doubleclaim : public contract {
 public:
  using contract::contract;

  [[eosio::action]]
  void go(uint64_t id) {
    require_auth(get_self());
    send_claim(id);
    send_claim(id);
  }

 private:
  void send_claim(uint64_t id) {
    action(permission_level{get_self(), "active"_n}, "smartavest"_n, "claim"_n, std::make_tuple(id)).send();
  }
};
