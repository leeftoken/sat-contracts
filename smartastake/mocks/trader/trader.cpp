#include <eosio/eosio.hpp>
#include <vector>

using namespace eosio;

// Local stand-in for smartatrader / leeftrader.
// Config field order matches the live ABI as of 2026-11-09, with `gov`
// appended (the model A patch). Stake reads only the prefix.
class [[eosio::contract("trader")]] trader : public contract {
 public:
  using contract::contract;

  [[eosio::action]]
  void init(name admin, uint32_t lock_secs) {
    require_auth(get_self());
    config_tbl configs(get_self(), get_self().value);
    check(configs.find(0) == configs.end(), "already inited");
    check(admin.value != 0, "admin");
    configs.emplace(get_self(), [&](auto& c) {
      c.admin = admin;
      c.bot = "smartabot111"_n;
      c.fee_recipient = "smartafees11"_n;
      c.swap = "swap.alcor"_n;
      c.paused = false;
      c.max_in_bps = 100;
      c.daily_cap_bps = 500;
      c.lock_secs = lock_secs;
      c.gov = name{};
    });
  }

  [[eosio::action]]
  void setgov(name gov) {
    auto c = load();
    require_auth(c.admin);
    config_tbl configs(get_self(), get_self().value);
    auto it = configs.find(0);
    configs.modify(it, same_payer, [&](auto& row) { row.gov = gov; });
  }

  [[eosio::action]]
  void setsplit(std::vector<name> buckets, std::vector<uint16_t> bps) {
    require_admin_or_gov();
    check(buckets.size() == bps.size() && !buckets.empty(), "split size");
    check(buckets.size() <= 16, "split size");
    uint32_t sum = 0;
    for (auto b : bps) sum += b;
    check(sum == 10000, "split sum");
    split_tbl splits(get_self(), get_self().value);
    auto it = splits.begin();
    while (it != splits.end()) it = splits.erase(it);
    for (uint32_t i = 0; i < buckets.size(); ++i) {
      check(buckets[i].value != 0, "bucket");
      splits.emplace(get_self(), [&](auto& s) {
        s.bucket = buckets[i];
        s.bps = bps[i];
        s.ord = i;
      });
    }
  }

  [[eosio::action]]
  void setlimits(uint16_t max_in_bps, uint16_t daily_cap_bps, uint32_t lock_secs) {
    require_admin_or_gov();
    config_tbl configs(get_self(), get_self().value);
    auto it = configs.find(0);
    configs.modify(it, same_payer, [&](auto& c) {
      c.max_in_bps = max_in_bps;
      c.daily_cap_bps = daily_cap_bps;
      c.lock_secs = lock_secs;
    });
  }

 private:
  struct [[eosio::table]] config_row {
    name admin;
    name bot;
    name fee_recipient;
    name swap;
    bool paused;
    uint16_t max_in_bps;
    uint16_t daily_cap_bps;
    uint32_t lock_secs;
    name gov;
    uint64_t primary_key() const { return 0; }
  };
  struct [[eosio::table]] split_row {
    name bucket;
    uint16_t bps;
    uint64_t ord;
    uint64_t primary_key() const { return bucket.value; }
  };
  using config_tbl = multi_index<"config"_n, config_row>;
  using split_tbl = multi_index<"splits"_n, split_row>;

  config_row load() {
    config_tbl configs(get_self(), get_self().value);
    auto it = configs.find(0);
    check(it != configs.end(), "trader not inited");
    return *it;
  }

  void require_admin_or_gov() {
    auto c = load();
    check(has_auth(c.admin) || (c.gov.value && has_auth(c.gov)), "missing auth");
  }
};
