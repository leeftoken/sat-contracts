#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// SAT vesting vault. Not audited. No withdraw, revoke, pause, or beneficiary change.
// Amounts are SAT smallest units (precision 4). Vesting math uses __int128.

using namespace eosio;

namespace {

constexpr symbol SAT_SYMBOL = symbol("SAT", 4);

std::vector<std::string> split_colon(const std::string& memo) {
  std::vector<std::string> parts;
  std::string cur;
  parts.reserve(6);
  for (char c : memo) {
    if (c == ':') {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  parts.push_back(cur);
  return parts;
}

uint32_t parse_u32(const std::string& s) {
  check(!s.empty() && s.size() <= 10, "bad integer");
  uint64_t v = 0;
  for (unsigned char c : s) {
    check(c >= '0' && c <= '9', "bad integer");
    v = v * 10ull + static_cast<uint64_t>(c - '0');
    check(v <= 0xffffffffull, "integer overflow");
  }
  return static_cast<uint32_t>(v);
}

uint64_t parse_u64(const std::string& s) {
  check(!s.empty() && s.size() <= 20, "bad integer");
  uint64_t v = 0;
  for (unsigned char c : s) {
    check(c >= '0' && c <= '9', "bad integer");
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    check(v <= (std::numeric_limits<uint64_t>::max() - digit) / 10ull, "integer overflow");
    v = v * 10ull + digit;
  }
  return v;
}

name parse_name(const std::string& s) {
  check(!s.empty() && s.size() <= 13, "bad name");
  for (unsigned char c : s) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '1' && c <= '5') || c == '.';
    check(ok, "bad name");
  }
  const name n(s);
  check(n.to_string() == s, "bad name");
  return n;
}

void validate_schedule(uint32_t start, uint32_t cliff_secs, uint32_t duration_secs) {
  check(start != 0, "start must be non-zero");
  check(duration_secs > 0, "duration must be positive");
  const uint64_t sum = uint64_t(start) + uint64_t(cliff_secs) + uint64_t(duration_secs);
  check(sum <= 0xffffffffull, "schedule overflows uint32");
}

// vested = 0 before cliff_end; total at/after end; else floor(total * elapsed / duration).
int64_t compute_vested(int64_t total, uint32_t start, uint32_t cliff_secs, uint32_t duration_secs, uint32_t now) {
  check(total >= 0, "negative total");
  check(duration_secs > 0, "duration must be positive");
  const uint64_t cliff_end = uint64_t(start) + uint64_t(cliff_secs);
  const uint64_t end = cliff_end + uint64_t(duration_secs);
  if (uint64_t(now) < cliff_end) return 0;
  if (uint64_t(now) >= end) return total;
  const uint64_t elapsed = uint64_t(now) - cliff_end;
  const __int128 numer = static_cast<__int128>(total) * static_cast<__int128>(elapsed);
  return static_cast<int64_t>(numer / static_cast<__int128>(duration_secs));
}

}  // namespace

class [[eosio::contract("smartavest")]] smartavest : public contract {
 public:
  using contract::contract;

  [[eosio::action]]
  void init(name admin, name token_contract) {
    require_auth(get_self());
    config_singleton cfg(get_self(), get_self().value);
    check(!cfg.exists(), "already initialized");
    check(admin.value != 0, "invalid admin");
    check(token_contract.value != 0, "invalid token contract");
    check(is_account(admin), "admin does not exist");
    check(is_account(token_contract), "token contract does not exist");
    cfg.set(config_row{admin, token_contract, SAT_SYMBOL, true}, get_self());
  }

  [[eosio::action]]
  void openstream(name beneficiary, name category, uint32_t start, uint32_t cliff_secs, uint32_t duration_secs) {
    const auto cfg = get_config();
    require_auth(cfg.admin);
    if (start == 0) start = current_now();
    create_stream(beneficiary, category, /*total=*/0, start, cliff_secs, duration_secs);
  }

  // Permissionless. Always pays the stream beneficiary, never the caller.
  [[eosio::action]]
  void claim(uint64_t id) {
    const auto cfg = get_config();
    streams_t streams(get_self(), get_self().value);
    auto it = streams.find(id);
    check(it != streams.end(), "stream not found");

    const uint32_t now = current_now();
    const uint64_t cliff_end = uint64_t(it->start) + uint64_t(it->cliff_secs);
    const int64_t vested = compute_vested(it->total, it->start, it->cliff_secs, it->duration_secs, now);
    check(vested >= it->claimed, "claimed exceeds vested");
    const int64_t claimable = vested - it->claimed;
    if (claimable <= 0) {
      check(uint64_t(now) >= cliff_end, "still in cliff");
      check(false, "nothing to claim");
    }
    check(it->claimed <= std::numeric_limits<int64_t>::max() - claimable, "claimed overflow");
    check(it->claimed + claimable <= it->total, "claim exceeds total");

    const name beneficiary = it->beneficiary;
    const int64_t paid = claimable;
    // Update before the inline transfer so a reentrant claim cannot double-pay.
    streams.modify(it, same_payer, [&](auto& row) { row.claimed += paid; });

    action(permission_level{get_self(), "active"_n},
           cfg.token_contract,
           "transfer"_n,
           std::make_tuple(get_self(), beneficiary, asset{paid, cfg.token_symbol}, std::string("vesting claim")))
        .send();
  }

#ifdef SAT_VEST_TEST_CLOCK
  // Test builds only. Production ABI must not contain this action.
  [[eosio::action]]
  void setnow(uint32_t t) {
    config_singleton cfg(get_self(), get_self().value);
    if (cfg.exists()) {
      const auto row = cfg.get();
      check(has_auth(get_self()) || has_auth(row.admin), "missing authority");
    } else {
      require_auth(get_self());
    }
    clock_singleton clk(get_self(), get_self().value);
    clk.set(clock_row{t, true}, get_self());
  }
#endif

  [[eosio::on_notify("*::transfer")]]
  void ontransfer(name from, name to, asset quantity, std::string memo) {
    // Outbound claim transfers and anyone else's traffic are ignored.
    if (to != get_self()) return;
    if (from == get_self()) return;

    const auto cfg = get_config();
    check(get_first_receiver() == cfg.token_contract, "wrong token contract");
    check(quantity.symbol == cfg.token_symbol, "wrong symbol");
    check(quantity.amount > 0, "must transfer positive quantity");
    check(!memo.empty(), "unknown memo");

    const auto parts = split_colon(memo);
    check(!parts.empty(), "unknown memo");

    if (parts[0] == "open") {
      // open:<beneficiary>:<category>:<cliff_secs>:<duration_secs>[:start]
      check(parts.size() == 5 || parts.size() == 6, "unknown memo");
      check(from == cfg.admin, "only admin may open");
      const name beneficiary = parse_name(parts[1]);
      const name category = parse_name(parts[2]);
      const uint32_t cliff_secs = parse_u32(parts[3]);
      const uint32_t duration_secs = parse_u32(parts[4]);
      uint32_t start = parts.size() == 6 ? parse_u32(parts[5]) : 0;
      if (start == 0) start = current_now();
      create_stream(beneficiary, category, quantity.amount, start, cliff_secs, duration_secs);
      return;
    }

    if (parts[0] == "fund") {
      // fund:<stream_id> — anyone may top up; beneficiary stays fixed.
      check(parts.size() == 2, "unknown memo");
      const uint64_t id = parse_u64(parts[1]);
      streams_t streams(get_self(), get_self().value);
      auto it = streams.find(id);
      check(it != streams.end(), "stream not found");
      check(quantity.amount <= std::numeric_limits<int64_t>::max() - it->total, "total overflow");
      const int64_t new_total = it->total + quantity.amount;
      check(new_total >= it->claimed, "total below claimed");
      streams.modify(it, same_payer, [&](auto& row) { row.total = new_total; });
      return;
    }

    check(false, "unknown memo");
  }

 private:
  struct [[eosio::table("config"), eosio::contract("smartavest")]] config_row {
    name admin;
    name token_contract;
    symbol token_symbol;
    bool inited = false;
  };
  using config_singleton = eosio::singleton<"config"_n, config_row>;

  struct [[eosio::table("streams"), eosio::contract("smartavest")]] stream_row {
    uint64_t id;
    name beneficiary;
    name category;
    int64_t total;
    int64_t claimed;
    uint32_t start;
    uint32_t cliff_secs;
    uint32_t duration_secs;
    uint32_t created_at;

    uint64_t primary_key() const { return id; }
    uint64_t by_beneficiary() const { return beneficiary.value; }
  };
  using streams_t = eosio::multi_index<
      "streams"_n,
      stream_row,
      indexed_by<"byben"_n, const_mem_fun<stream_row, uint64_t, &stream_row::by_beneficiary>>>;

#ifdef SAT_VEST_TEST_CLOCK
  struct [[eosio::table("testclock"), eosio::contract("smartavest")]] clock_row {
    uint32_t now_sec = 0;
    bool active = false;
  };
  using clock_singleton = eosio::singleton<"testclock"_n, clock_row>;
#endif

  config_row get_config() const {
    config_singleton cfg(get_self(), get_self().value);
    check(cfg.exists(), "not initialized");
    const auto row = cfg.get();
    check(row.inited, "not initialized");
    check(row.token_symbol == SAT_SYMBOL, "wrong symbol");
    return row;
  }

  uint32_t current_now() const {
#ifdef SAT_VEST_TEST_CLOCK
    clock_singleton clk(get_self(), get_self().value);
    if (clk.exists()) {
      const auto row = clk.get();
      if (row.active) return row.now_sec;
    }
#endif
    return current_time_point().sec_since_epoch();
  }

  uint64_t create_stream(name beneficiary,
                         name category,
                         int64_t total,
                         uint32_t start,
                         uint32_t cliff_secs,
                         uint32_t duration_secs) {
    check(beneficiary.value != 0, "invalid beneficiary");
    check(category.value != 0, "invalid category");
    check(beneficiary != get_self(), "beneficiary cannot be contract");
    check(is_account(beneficiary), "beneficiary does not exist");
    check(total >= 0, "negative total");
    validate_schedule(start, cliff_secs, duration_secs);

    streams_t streams(get_self(), get_self().value);
    const uint64_t id = streams.available_primary_key();
    const uint32_t created = current_now();
    streams.emplace(get_self(), [&](auto& row) {
      row.id = id;
      row.beneficiary = beneficiary;
      row.category = category;
      row.total = total;
      row.claimed = 0;
      row.start = start;
      row.cliff_secs = cliff_secs;
      row.duration_secs = duration_secs;
      row.created_at = created;
    });
    return id;
  }
};
