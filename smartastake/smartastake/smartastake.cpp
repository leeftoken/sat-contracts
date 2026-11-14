#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/eosio.hpp>
#include <eosio/system.hpp>

#include <cstring>
#include <string>
#include <vector>

#define SMARTASTAKE_CHECK(cond, msg) eosio::check(static_cast<bool>(cond), msg)
#include "muldiv.hpp"

using namespace eosio;

// Live smartatrader config prefix (ABI 2026-11-09). `gov` may be appended later;
// leftover bytes are ignored.
struct trader_config_row {
  name admin;
  name bot;
  name fee_recipient;
  name swap;
  bool paused;
  uint16_t max_in_bps;
  uint16_t daily_cap_bps;
  uint32_t lock_secs;
  uint64_t primary_key() const { return 0; }
  EOSLIB_SERIALIZE(trader_config_row,
                   (admin)(bot)(fee_recipient)(swap)(paused)(max_in_bps)(daily_cap_bps)(lock_secs))
};
using trader_config_tbl = multi_index<"config"_n, trader_config_row>;

struct token_balance_row {
  asset balance;
  uint64_t primary_key() const { return balance.symbol.code().raw(); }
  EOSLIB_SERIALIZE(token_balance_row, (balance))
};
using token_balance_tbl = multi_index<"accounts"_n, token_balance_row>;

static constexpr uint128_t SCALE = 1000000000000000000ull; // 1e18
static constexpr int64_t MAX_I64 = 9223372036854775807ll;
static constexpr uint16_t SPLIT_EACH_MAX = 5000;
static constexpr uint16_t SPLIT_SATLP_MIN = 1000;
static constexpr uint16_t SPLIT_OPS_MAX = 4000;
static constexpr uint16_t SPLIT_REWARDS_MIN = 500;
static constexpr uint16_t LIMIT_MAX_IN_MIN = 50;
static constexpr uint16_t LIMIT_MAX_IN_MAX = 500;
static constexpr uint16_t LIMIT_DAILY_MIN = 100;
static constexpr uint16_t LIMIT_DAILY_MAX = 2000;
static constexpr uint16_t SATLP_SHARE_MIN = 1000;
static constexpr uint16_t SATLP_SHARE_MAX = 8000;
static constexpr uint16_t BUYBACK_MAX = 5000;
static constexpr uint64_t REWARD_RATE_MAX = 1000000; // 100.0000 SAT / sec, stored only
static constexpr uint32_t REWARD_PERIOD_MIN = 86400;
static constexpr uint32_t DAY = 86400;
static constexpr uint32_t AIRDROP_VOTE_MIN = 7 * DAY;
static constexpr uint32_t AIRDROP_TIMELOCK_MIN = 2 * DAY;
static constexpr uint32_t AIRDROP_GAP = 30 * DAY;
static constexpr uint16_t AIRDROP_CAP_BPS = 2500; // 25% of the fund's staked balance
static constexpr uint16_t AIRDROP_ALLOC_MIN = 250;
static constexpr uint16_t AIRDROP_ALLOC_MAX = 500;
static constexpr uint8_t ST_ACTIVE = 1;
static constexpr uint8_t ST_DEFEATED = 2;
static constexpr uint8_t ST_QUEUED = 3;
static constexpr uint8_t ST_EXECUTED = 4;
static constexpr uint8_t ST_VETOED = 5;
static constexpr uint8_t ST_EXPIRED = 6;
// A queued proposal that is not executed within this window after eta expires.
// Stops a stale proposal from overriding a newer one and frees a stuck airdrop slot.
static constexpr uint32_t EXEC_GRACE = 14 * DAY;

static uint128_t join128(uint64_t hi, uint64_t lo) {
  return (static_cast<uint128_t>(hi) << 64) | lo;
}
static void split128(uint128_t v, uint64_t& hi, uint64_t& lo) {
  lo = static_cast<uint64_t>(v);
  hi = static_cast<uint64_t>(v >> 64);
}
static void write_le64(uint8_t* p, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    p[i] = static_cast<uint8_t>(v & 0xff);
    v >>= 8;
  }
}
static bool hash_is_zero(const checksum256& h) {
  auto a = h.extract_as_byte_array();
  for (auto b : a)
    if (b) return false;
  return true;
}
static checksum256 leaf_hash(name account, int64_t amount) {
  uint8_t buf[16];
  write_le64(buf, account.value);
  write_le64(buf + 8, static_cast<uint64_t>(amount));
  return sha256(reinterpret_cast<const char*>(buf), 16);
}
static checksum256 pair_hash(const checksum256& a, const checksum256& b) {
  auto aa = a.extract_as_byte_array();
  auto bb = b.extract_as_byte_array();
  uint8_t buf[64];
  if (memcmp(aa.data(), bb.data(), 32) <= 0) {
    memcpy(buf, aa.data(), 32);
    memcpy(buf + 32, bb.data(), 32);
  } else {
    memcpy(buf, bb.data(), 32);
    memcpy(buf + 32, aa.data(), 32);
  }
  return sha256(reinterpret_cast<const char*>(buf), 64);
}
static bool merkle_ok(name account, int64_t amount, const std::vector<checksum256>& proof,
                      const checksum256& root) {
  checksum256 h = leaf_hash(account, amount);
  for (const auto& sib : proof) h = pair_hash(h, sib);
  auto hh = h.extract_as_byte_array();
  auto rr = root.extract_as_byte_array();
  return memcmp(hh.data(), rr.data(), 32) == 0;
}

class [[eosio::contract("smartastake")]] smartastake : public contract {
 public:
  using contract::contract;

  struct [[eosio::table]] config_row {
    uint64_t id;
    name admin;
    name token_contract;
    symbol token_symbol;
    name guardian;
    name trader;
    name airdrop_account;
    uint16_t airdrop_alloc_bps;
    uint32_t cooldown_secs;
    uint32_t min_cooldown;
    uint32_t max_cooldown;
    int64_t proposal_threshold;
    uint16_t quorum_bps;
    uint16_t approval_bps;
    uint32_t voting_period_secs;
    uint32_t timelock_secs;
    uint64_t rpt_hi;
    uint64_t rpt_lo;
    int64_t total_staked;
    int64_t total_cooling;
    int64_t total_votable;
    int64_t reward_pot;
    int64_t reward_pending_undistributed;
    int64_t airdrop_liability;
    uint64_t reward_rate;
    uint32_t reward_period_secs;
    uint16_t sat_lp_fee_share_bps;
    uint16_t buyback_share_bps;
    uint64_t proposal_seq;
    uint64_t open_airdrop_id;
    uint32_t last_airdrop_exec;
#ifdef SMARTASTAKE_TEST_CLOCK
    uint32_t test_now;
#endif
    uint64_t primary_key() const { return id; }
  };

  struct [[eosio::table]] stake_row {
    name owner;
    int64_t amount;
    uint64_t paid_hi;
    uint64_t paid_lo;
    int64_t pending_rewards;
    int64_t unstake_amount;
    uint32_t unstake_ready;
    uint32_t vote_lock_until;
    uint64_t primary_key() const { return owner.value; }
  };

  struct [[eosio::table]] proposal_row {
    uint64_t id;
    name proposer;
    name kind;
    uint16_t bps0;
    uint16_t bps1;
    uint16_t bps2;
    uint16_t bps3;
    uint16_t bps4;
    uint32_t p0;
    uint32_t p1;
    uint64_t rate;
    int64_t amount;
    uint32_t snapshot_time;
    std::string memo;
    checksum256 merkle_root;
    name novote_account;
    int64_t snapshot_total;
    uint32_t start_time;
    uint32_t end_time;
    uint32_t applied_timelock;
    int64_t for_votes;
    int64_t against_votes;
    uint32_t eta;
    uint8_t status;
    uint64_t primary_key() const { return id; }
  };

  struct [[eosio::table]] ballot_row {
    name voter;
    bool support;
    int64_t weight;
    uint64_t primary_key() const { return voter.value; }
  };

  struct [[eosio::table]] novote_row {
    name account;
    uint64_t primary_key() const { return account.value; }
  };

  struct [[eosio::table]] funder_row {
    name account;
    uint64_t primary_key() const { return account.value; }
  };

  struct [[eosio::table]] drop_row {
    uint64_t id;
    checksum256 merkle_root;
    int64_t amount;
    int64_t claimed;
    uint32_t snapshot_time;
    std::string memo;
    uint64_t primary_key() const { return id; }
  };

  struct [[eosio::table]] claim_row {
    name account;
    int64_t amount;
    uint64_t primary_key() const { return account.value; }
  };

  using config_tbl = multi_index<"config"_n, config_row>;
  using stakes_tbl = multi_index<"stakes"_n, stake_row>;
  using proposals_tbl = multi_index<"proposals"_n, proposal_row>;
  using ballots_tbl = multi_index<"ballots"_n, ballot_row>;
  using novote_tbl = multi_index<"novote"_n, novote_row>;
  using funders_tbl = multi_index<"funders"_n, funder_row>;
  using drops_tbl = multi_index<"drops"_n, drop_row>;
  using claims_tbl = multi_index<"claims"_n, claim_row>;

  [[eosio::action]]
  void init(name admin, name guardian, name token_contract, symbol token_symbol, name trader,
            name airdrop_account, uint16_t airdrop_alloc_bps, std::vector<name> novote,
            uint32_t cooldown_secs, uint32_t min_cooldown, uint32_t max_cooldown,
            int64_t proposal_threshold, uint16_t quorum_bps, uint16_t approval_bps,
            uint32_t voting_period_secs, uint32_t timelock_secs, uint16_t sat_lp_fee_share_bps,
            uint16_t buyback_share_bps) {
    require_auth(get_self());
    config_tbl configs(get_self(), get_self().value);
    check(configs.find(0) == configs.end(), "already inited");
    check(admin.value && guardian.value && token_contract.value && trader.value, "names");
    check(token_symbol == symbol("SAT", 4), "symbol must be 4,SAT");
    check(min_cooldown >= 1 && max_cooldown >= min_cooldown && max_cooldown <= 365 * DAY, "cooldown bounds");
    check(cooldown_secs >= min_cooldown && cooldown_secs <= max_cooldown, "cooldown bounds");
    check(proposal_threshold > 0, "threshold");
    check(quorum_bps >= 1 && quorum_bps <= 10000, "quorum");
    check(approval_bps >= 1 && approval_bps <= 10000, "approval");
    check(voting_period_secs >= 3600 && voting_period_secs <= 30 * DAY, "voting period");
    check(timelock_secs >= 60 && timelock_secs <= 30 * DAY, "timelock");
    check(sat_lp_fee_share_bps >= SATLP_SHARE_MIN && sat_lp_fee_share_bps <= SATLP_SHARE_MAX, "sat lp share");
    check(buyback_share_bps <= BUYBACK_MAX, "buyback share");
    check(novote.size() <= 8, "novote size");

    bool seen_airdrop = false;
    novote_tbl nv(get_self(), get_self().value);
    for (auto account : novote) {
      check(account.value != 0, "novote name");
      check(nv.find(account.value) == nv.end(), "novote dup");
      nv.emplace(get_self(), [&](auto& r) { r.account = account; });
      if (account == airdrop_account) seen_airdrop = true;
    }
    if (airdrop_account.value) {
      check(seen_airdrop, "airdrop must be novote");
      check(airdrop_alloc_bps >= AIRDROP_ALLOC_MIN && airdrop_alloc_bps <= AIRDROP_ALLOC_MAX, "alloc bps");
    } else {
      check(airdrop_alloc_bps == 0, "alloc bps");
    }

    config_row c{};
    c.id = 0;
    c.admin = admin;
    c.token_contract = token_contract;
    c.token_symbol = token_symbol;
    c.guardian = guardian;
    c.trader = trader;
    c.airdrop_account = airdrop_account;
    c.airdrop_alloc_bps = airdrop_alloc_bps;
    c.cooldown_secs = cooldown_secs;
    c.min_cooldown = min_cooldown;
    c.max_cooldown = max_cooldown;
    c.proposal_threshold = proposal_threshold;
    c.quorum_bps = quorum_bps;
    c.approval_bps = approval_bps;
    c.voting_period_secs = voting_period_secs;
    c.timelock_secs = timelock_secs;
    c.sat_lp_fee_share_bps = sat_lp_fee_share_bps;
    c.buyback_share_bps = buyback_share_bps;
    configs.emplace(get_self(), [&](auto& row) { row = c; });
  }

  [[eosio::action]]
  void setfunder(name admin, name account, bool add) {
    auto c = load();
    require_auth(admin);
    check(admin == c.admin, "not admin");
    check(account.value != 0, "account");
    funders_tbl funders(get_self(), get_self().value);
    auto it = funders.find(account.value);
    if (add) {
      check(it == funders.end(), "already funder");
      funders.emplace(get_self(), [&](auto& r) { r.account = account; });
    } else {
      check(it != funders.end(), "not funder");
      funders.erase(it);
    }
  }

  [[eosio::action]]
  void unstake(name owner, int64_t amount) {
    require_auth(owner);
    auto c = load();
    check(owner != c.airdrop_account, "airdrop fund is proposal-only");
    check(amount > 0, "amount");
    uint32_t now = now_of(c);
    stakes_tbl stakes(get_self(), get_self().value);
    auto it = stakes.require_find(owner.value, "no stake");
    check(now >= it->vote_lock_until, "vote locked");
    settle(c, stakes, owner.value);
    it = stakes.require_find(owner.value, "no stake");
    check(amount <= it->amount, "over unstake");
    bool novote = is_novote(owner);
    stakes.modify(it, same_payer, [&](auto& s) {
      s.amount -= amount;
      s.unstake_amount += amount;
      // New request resets the clock for the whole cooling balance.
      s.unstake_ready = now + c.cooldown_secs;
    });
    c.total_staked -= amount;
    c.total_cooling += amount;
    if (!novote) c.total_votable -= amount;
    save(c);
    assert_invariant(c);
    log_unstake(owner, amount, now + c.cooldown_secs);
  }

  [[eosio::action]]
  void withdraw(name owner) {
    require_auth(owner);
    auto c = load();
    check(owner != c.airdrop_account, "airdrop fund is proposal-only");
    uint32_t now = now_of(c);
    stakes_tbl stakes(get_self(), get_self().value);
    auto it = stakes.require_find(owner.value, "no stake");
    check(it->unstake_amount > 0, "nothing cooling");
    check(now >= it->unstake_ready, "not ready");
    int64_t pay = it->unstake_amount;
    c.total_cooling -= pay;
    stakes.modify(it, same_payer, [&](auto& s) {
      s.unstake_amount = 0;
      s.unstake_ready = 0;
    });
    save(c);
    assert_invariant(c);
    send_sat(c, owner, pay, "withdraw");
  }

  [[eosio::action]]
  void claim(name owner) {
    require_auth(owner);
    auto c = load();
    check(owner != c.airdrop_account, "airdrop fund is proposal-only");
    stakes_tbl stakes(get_self(), get_self().value);
    check(stakes.find(owner.value) != stakes.end(), "no stake");
    settle(c, stakes, owner.value);
    auto it = stakes.require_find(owner.value, "no stake");
    int64_t pay = it->pending_rewards;
    check(pay > 0, "nothing to claim");
    check(pay <= c.reward_pot, "reward pot short");
    stakes.modify(it, same_payer, [&](auto& s) { s.pending_rewards = 0; });
    c.reward_pot -= pay;
    save(c);
    assert_invariant(c);
    log_claim(owner, pay);
    send_sat(c, owner, pay, "claim");
  }

  [[eosio::action]]
  void compound(name account) {
    auto c = load();
    check(account == c.airdrop_account && account.value != 0, "not airdrop fund");
    stakes_tbl stakes(get_self(), get_self().value);
    compound_into(c, stakes, account);
    save(c);
    assert_invariant(c);
  }

  [[eosio::action]]
  void propose(name proposer, name kind, std::vector<uint16_t> bps, uint32_t p0, uint32_t p1,
               uint64_t rate, int64_t amount, uint32_t snapshot_time, std::string memo,
               checksum256 merkle_root, name novote_account) {
    require_auth(proposer);
    auto c = load();
    check(!is_novote(proposer), "no voting power");
    stakes_tbl stakes(get_self(), get_self().value);
    auto st = stakes.require_find(proposer.value, "no stake");
    check(st->amount >= c.proposal_threshold, "below threshold");
    check(c.total_votable > 0, "no votable");

    proposal_row p{};
    p.proposer = proposer;
    p.kind = kind;
    p.for_votes = 0;
    p.against_votes = 0;
    p.eta = 0;
    p.status = ST_ACTIVE;
    uint32_t now = now_of(c);
    p.start_time = now;
    p.applied_timelock = c.timelock_secs;
    uint32_t vote_secs = c.voting_period_secs;

    if (kind == "setsplit"_n) {
      check(bps.size() == 5, "split len");
      check_split(bps[0], bps[1], bps[2], bps[3], bps[4]);
      p.bps0 = bps[0];
      p.bps1 = bps[1];
      p.bps2 = bps[2];
      p.bps3 = bps[3];
      p.bps4 = bps[4];
      check(p0 == 0 && p1 == 0 && rate == 0 && amount == 0 && snapshot_time == 0 && memo.empty() &&
                hash_is_zero(merkle_root) && novote_account.value == 0,
            "extra payload");
    } else if (kind == "setlimits"_n) {
      check(bps.empty(), "unexpected bps");
      check_limits(p0, p1);
      p.p0 = p0;
      p.p1 = p1;
      check(rate == 0 && amount == 0 && snapshot_time == 0 && memo.empty() && hash_is_zero(merkle_root) &&
                novote_account.value == 0,
            "extra payload");
    } else if (kind == "setcooldown"_n) {
      check(bps.empty(), "unexpected bps");
      check(p0 >= c.min_cooldown && p0 <= c.max_cooldown, "cooldown bounds");
      p.p0 = p0;
      check(p1 == 0 && rate == 0 && amount == 0 && snapshot_time == 0 && memo.empty() &&
                hash_is_zero(merkle_root) && novote_account.value == 0,
            "extra payload");
    } else if (kind == "setsatlps"_n) {
      check(bps.empty(), "unexpected bps");
      check(p0 >= SATLP_SHARE_MIN && p0 <= SATLP_SHARE_MAX, "sat lp share");
      p.p0 = p0;
      check(p1 == 0 && rate == 0 && amount == 0 && snapshot_time == 0 && memo.empty() &&
                hash_is_zero(merkle_root) && novote_account.value == 0,
            "extra payload");
    } else if (kind == "setbuyback"_n) {
      check(bps.empty(), "unexpected bps");
      check(p0 <= BUYBACK_MAX, "buyback share");
      p.p0 = p0;
      check(p1 == 0 && rate == 0 && amount == 0 && snapshot_time == 0 && memo.empty() &&
                hash_is_zero(merkle_root) && novote_account.value == 0,
            "extra payload");
    } else if (kind == "setrparams"_n) {
      check(bps.empty(), "unexpected bps");
      check(rate <= REWARD_RATE_MAX, "rate cap");
      if (rate > 0) check(p0 >= REWARD_PERIOD_MIN, "period");
      p.p0 = p0;
      p.rate = rate;
      check(p1 == 0 && amount == 0 && snapshot_time == 0 && memo.empty() && hash_is_zero(merkle_root) &&
                novote_account.value == 0,
            "extra payload");
    } else if (kind == "delnovote"_n) {
      check(bps.empty(), "unexpected bps");
      check(novote_account.value != 0, "novote account");
      check(novote_account != c.airdrop_account, "airdrop fund stays novote");
      novote_tbl nv(get_self(), get_self().value);
      check(nv.find(novote_account.value) != nv.end(), "not novote");
      p.novote_account = novote_account;
      check(p0 == 0 && p1 == 0 && rate == 0 && amount == 0 && snapshot_time == 0 && memo.empty() &&
                hash_is_zero(merkle_root),
            "extra payload");
    } else if (kind == "airdrop"_n) {
      check(bps.empty(), "unexpected bps");
      check(c.airdrop_account.value != 0, "no airdrop fund");
      check(c.open_airdrop_id == 0, "airdrop open");
      if (c.last_airdrop_exec != 0) {
        check(now >= c.last_airdrop_exec + AIRDROP_GAP, "airdrop cooldown");
      }
      check(amount > 0, "amount");
      check(memo.size() <= 256, "memo");
      check(snapshot_time > 0 && snapshot_time <= now, "snapshot_time");
      check(!hash_is_zero(merkle_root), "empty root");
      check(novote_account.value == 0 && p0 == 0 && p1 == 0 && rate == 0, "extra payload");
      compound_into(c, stakes, c.airdrop_account);
      auto fund = stakes.require_find(c.airdrop_account.value, "fund not staked");
      check(static_cast<__int128>(amount) * 10000 <= static_cast<__int128>(fund->amount) * AIRDROP_CAP_BPS,
            "airdrop cap");
      p.amount = amount;
      p.snapshot_time = snapshot_time;
      p.memo = memo;
      p.merkle_root = merkle_root;
      if (vote_secs < AIRDROP_VOTE_MIN) vote_secs = AIRDROP_VOTE_MIN;
      if (p.applied_timelock < AIRDROP_TIMELOCK_MIN) p.applied_timelock = AIRDROP_TIMELOCK_MIN;
    } else {
      check(false, "kind not allowed");
    }

    p.end_time = now + vote_secs;
    p.snapshot_total = c.total_votable;
    p.id = ++c.proposal_seq;
    if (kind == "airdrop"_n) c.open_airdrop_id = p.id;
    proposals_tbl props(get_self(), get_self().value);
    props.emplace(proposer, [&](auto& row) { row = p; });
    save(c);
    log_propose(p.id, proposer, kind);
  }

  [[eosio::action]]
  void vote(name voter, uint64_t id, bool support) {
    require_auth(voter);
    auto c = load();
    check(!is_novote(voter), "no voting power");
    uint32_t now = now_of(c);
    proposals_tbl props(get_self(), get_self().value);
    auto pit = props.require_find(id, "no proposal");
    check(pit->status == ST_ACTIVE, "not active");
    check(now >= pit->start_time && now < pit->end_time, "vote window");
    ballots_tbl ballots(get_self(), id);
    check(ballots.find(voter.value) == ballots.end(), "already voted");
    stakes_tbl stakes(get_self(), get_self().value);
    auto st = stakes.require_find(voter.value, "no stake");
    check(st->amount > 0, "no voting power");
    int64_t weight = st->amount;
    uint32_t lock = pit->end_time;
    if (st->vote_lock_until > lock) lock = st->vote_lock_until;
    stakes.modify(st, same_payer, [&](auto& s) { s.vote_lock_until = lock; });
    props.modify(pit, same_payer, [&](auto& p) {
      if (support) {
        check(p.for_votes <= MAX_I64 - weight, "votes overflow");
        p.for_votes += weight;
      } else {
        check(p.against_votes <= MAX_I64 - weight, "votes overflow");
        p.against_votes += weight;
      }
    });
    ballots.emplace(voter, [&](auto& b) {
      b.voter = voter;
      b.support = support;
      b.weight = weight;
    });
    log_vote(id, voter, support, weight);
  }

  [[eosio::action]]
  void queue(uint64_t id) {
    auto c = load();
    uint32_t now = now_of(c);
    proposals_tbl props(get_self(), get_self().value);
    auto pit = props.require_find(id, "no proposal");
    check(pit->status == ST_ACTIVE, "not active");
    check(now >= pit->end_time, "voting open");
    __int128 cast = static_cast<__int128>(pit->for_votes) + pit->against_votes;
    bool quorum = cast * 10000 >= static_cast<__int128>(pit->snapshot_total) * c.quorum_bps;
    bool approval = cast > 0 && static_cast<__int128>(pit->for_votes) * 10000 >= cast * c.approval_bps;
    uint8_t status;
    uint32_t eta = 0;
    if (quorum && approval) {
      status = ST_QUEUED;
      eta = now + pit->applied_timelock;
    } else {
      status = ST_DEFEATED;
      if (pit->kind == "airdrop"_n && c.open_airdrop_id == pit->id) c.open_airdrop_id = 0;
    }
    props.modify(pit, same_payer, [&](auto& p) {
      p.status = status;
      p.eta = eta;
    });
    save(c);
    log_queue(id, status, eta);
  }

  [[eosio::action]]
  void execute(uint64_t id) {
    auto c = load();
    uint32_t now = now_of(c);
    proposals_tbl props(get_self(), get_self().value);
    auto pit = props.require_find(id, "no proposal");
    check(pit->status == ST_QUEUED, "not queued");
    check(pit->eta != 0 && now >= pit->eta, "timelock");
    if (static_cast<uint64_t>(now) > static_cast<uint64_t>(pit->eta) + EXEC_GRACE) {
      // Expire instead of failing so the status change sticks.
      if (pit->kind == "airdrop"_n && c.open_airdrop_id == pit->id) c.open_airdrop_id = 0;
      props.modify(pit, same_payer, [&](auto& row) { row.status = ST_EXPIRED; });
      save(c);
      log_queue(id, ST_EXPIRED, pit->eta);
      return;
    }
    proposal_row p = *pit;

    if (p.kind == "setsplit"_n) {
      check_split(p.bps0, p.bps1, p.bps2, p.bps3, p.bps4);
      std::vector<name> buckets = {"satlp"_n, "leeflp"_n, "ops"_n, "rewards"_n, "reserve"_n};
      std::vector<uint16_t> bps = {p.bps0, p.bps1, p.bps2, p.bps3, p.bps4};
      action(permission_level{get_self(), "active"_n}, c.trader, "setsplit"_n, std::make_tuple(buckets, bps))
          .send();
    } else if (p.kind == "setlimits"_n) {
      check_limits(p.p0, p.p1);
      trader_config_tbl tc(c.trader, c.trader.value);
      check(tc.begin() != tc.end(), "trader config missing");
      uint32_t lock = tc.begin()->lock_secs;
      action(permission_level{get_self(), "active"_n}, c.trader, "setlimits"_n,
             std::make_tuple(static_cast<uint16_t>(p.p0), static_cast<uint16_t>(p.p1), lock))
          .send();
    } else if (p.kind == "setcooldown"_n) {
      check(p.p0 >= c.min_cooldown && p.p0 <= c.max_cooldown, "cooldown bounds");
      c.cooldown_secs = p.p0;
    } else if (p.kind == "setsatlps"_n) {
      check(p.p0 >= SATLP_SHARE_MIN && p.p0 <= SATLP_SHARE_MAX, "sat lp share");
      c.sat_lp_fee_share_bps = static_cast<uint16_t>(p.p0);
    } else if (p.kind == "setbuyback"_n) {
      check(p.p0 <= BUYBACK_MAX, "buyback share");
      c.buyback_share_bps = static_cast<uint16_t>(p.p0);
    } else if (p.kind == "setrparams"_n) {
      check(p.rate <= REWARD_RATE_MAX, "rate cap");
      if (p.rate > 0) check(p.p0 >= REWARD_PERIOD_MIN, "period");
      c.reward_rate = p.rate;
      c.reward_period_secs = p.p0;
    } else if (p.kind == "delnovote"_n) {
      check(p.novote_account != c.airdrop_account, "airdrop fund stays novote");
      novote_tbl nv(get_self(), get_self().value);
      auto nit = nv.require_find(p.novote_account.value, "not novote");
      stakes_tbl stakes(get_self(), get_self().value);
      auto sit = stakes.find(p.novote_account.value);
      if (sit != stakes.end() && sit->amount > 0) c.total_votable += sit->amount;
      nv.erase(nit);
    } else if (p.kind == "airdrop"_n) {
      check(c.airdrop_account.value != 0, "no airdrop fund");
      if (c.last_airdrop_exec != 0) {
        check(now >= c.last_airdrop_exec + AIRDROP_GAP, "airdrop cooldown");
      }
      stakes_tbl stakes(get_self(), get_self().value);
      compound_into(c, stakes, c.airdrop_account);
      auto fund = stakes.require_find(c.airdrop_account.value, "fund not staked");
      check(p.amount > 0, "amount");
      check(static_cast<__int128>(p.amount) * 10000 <= static_cast<__int128>(fund->amount) * AIRDROP_CAP_BPS,
            "airdrop cap");
      check(fund->amount >= p.amount, "fund short");
      check(now >= fund->vote_lock_until, "vote locked");
      stakes.modify(fund, same_payer, [&](auto& s) { s.amount -= p.amount; });
      c.total_staked -= p.amount;
      c.airdrop_liability += p.amount;
      c.last_airdrop_exec = now;
      c.open_airdrop_id = 0;
      drops_tbl drops(get_self(), get_self().value);
      check(drops.find(p.id) == drops.end(), "drop exists");
      drops.emplace(get_self(), [&](auto& d) {
        d.id = p.id;
        d.merkle_root = p.merkle_root;
        d.amount = p.amount;
        d.claimed = 0;
        d.snapshot_time = p.snapshot_time;
        d.memo = p.memo;
      });
      action(permission_level{get_self(), "active"_n}, get_self(), "logairdrop"_n,
             std::make_tuple(p.id, p.amount, p.merkle_root))
          .send();
    } else {
      check(false, "kind not allowed");
    }

    props.modify(props.require_find(id, "no proposal"), same_payer, [&](auto& row) {
      row.status = ST_EXECUTED;
      row.eta = p.eta;
    });
    save(c);
    assert_invariant(c);
    log_exec(id, p.kind);
  }

  [[eosio::action]]
  void veto(name guardian, uint64_t id) {
    require_auth(guardian);
    auto c = load();
    check(guardian == c.guardian, "not guardian");
    proposals_tbl props(get_self(), get_self().value);
    auto pit = props.require_find(id, "no proposal");
    check(pit->status == ST_QUEUED, "not queued");
    if (pit->kind == "airdrop"_n && c.open_airdrop_id == pit->id) c.open_airdrop_id = 0;
    props.modify(pit, same_payer, [&](auto& p) {
      p.status = ST_VETOED;
      p.eta = 0;
    });
    save(c);
    log_veto(id, guardian);
  }

  [[eosio::action]]
  void claimdrop(name account, uint64_t drop_id, int64_t amount, std::vector<checksum256> proof) {
    require_auth(account);
    check(amount > 0, "amount");
    check(proof.size() <= 32, "proof too long");
    auto c = load();
    drops_tbl drops(get_self(), get_self().value);
    auto dit = drops.require_find(drop_id, "no drop");
    check(merkle_ok(account, amount, proof, dit->merkle_root), "bad proof");
    claims_tbl claims(get_self(), drop_id);
    check(claims.find(account.value) == claims.end(), "already claimed");
    check(dit->claimed <= MAX_I64 - amount, "claimed overflow");
    check(dit->claimed + amount <= dit->amount, "drop exhausted");
    check(c.airdrop_liability >= amount, "liability");
    drops.modify(dit, same_payer, [&](auto& d) { d.claimed += amount; });
    claims.emplace(account, [&](auto& r) {
      r.account = account;
      r.amount = amount;
    });
    c.airdrop_liability -= amount;
    save(c);
    assert_invariant(c);
    action(permission_level{get_self(), "active"_n}, get_self(), "logclaimdrop"_n,
           std::make_tuple(drop_id, account, amount))
        .send();
    send_sat(c, account, amount, "airdrop");
  }

  [[eosio::on_notify("*::transfer")]]
  void ontransfer(name from, name to, asset quantity, std::string memo) {
    if (to != get_self() || from == get_self()) return;
    auto c = load();
    check(get_first_receiver() == c.token_contract, "wrong token");
    check(quantity.symbol == c.token_symbol, "wrong symbol");
    check(quantity.amount > 0, "amount");

    // SAT from the trader contract is always reward funding. The trader has no
    // action that could ever unstake, claim, or vote, so a stake row for it
    // would lock that SAT for good.
    bool reward = memo == "reward" || from == c.trader;
    if (!reward && memo != "stake" && !memo.empty()) {
      funders_tbl funders(get_self(), get_self().value);
      check(funders.find(from.value) != funders.end(), "memo");
      reward = true;
    }
    if (reward) {
      fund_reward(c, quantity.amount);
      save(c);
      assert_invariant(c);
      log_reward(from, quantity.amount, c.reward_pot);
    } else {
      credit_stake(c, from, quantity.amount);
      save(c);
      assert_invariant(c);
      log_stake(from, quantity.amount, c.total_staked);
    }
  }

  [[eosio::action]] void logstake(name owner, int64_t amount, int64_t total_staked) { require_auth(get_self()); }
  [[eosio::action]] void logunstake(name owner, int64_t amount, uint32_t ready) { require_auth(get_self()); }
  [[eosio::action]] void logclaim(name owner, int64_t amount) { require_auth(get_self()); }
  [[eosio::action]] void logreward(name from, int64_t amount, int64_t pot) { require_auth(get_self()); }
  [[eosio::action]] void logpropose(uint64_t id, name proposer, name kind) { require_auth(get_self()); }
  [[eosio::action]] void logvote(uint64_t id, name voter, bool support, int64_t weight) { require_auth(get_self()); }
  [[eosio::action]] void logqueue(uint64_t id, uint8_t status, uint32_t eta) { require_auth(get_self()); }
  [[eosio::action]] void logexec(uint64_t id, name kind) { require_auth(get_self()); }
  [[eosio::action]] void logveto(uint64_t id, name guardian) { require_auth(get_self()); }
  [[eosio::action]] void logairdrop(uint64_t id, int64_t amount, checksum256 root) { require_auth(get_self()); }
  [[eosio::action]] void logclaimdrop(uint64_t id, name account, int64_t amount) { require_auth(get_self()); }

#ifdef SMARTASTAKE_TEST_CLOCK
  [[eosio::action]]
  void settime(uint32_t now_sec) {
    require_auth(get_self());
    auto c = load();
    c.test_now = now_sec;
    save(c);
  }
#endif

 private:
  config_row load() {
    config_tbl configs(get_self(), get_self().value);
    auto it = configs.find(0);
    check(it != configs.end(), "not inited");
    return *it;
  }
  void save(const config_row& c) {
    config_tbl configs(get_self(), get_self().value);
    auto it = configs.require_find(0, "not inited");
    configs.modify(it, same_payer, [&](auto& row) { row = c; });
  }
  uint32_t now_of(const config_row& c) const {
#ifdef SMARTASTAKE_TEST_CLOCK
    if (c.test_now) return c.test_now;
#endif
    return current_time_point().sec_since_epoch();
  }
  bool is_novote(name account) {
    novote_tbl nv(get_self(), get_self().value);
    return nv.find(account.value) != nv.end();
  }
  void settle(config_row& c, stakes_tbl& stakes, uint64_t owner_value) {
    auto it = stakes.require_find(owner_value, "no stake");
    uint128_t acc = join128(c.rpt_hi, c.rpt_lo);
    uint128_t paid = join128(it->paid_hi, it->paid_lo);
    int64_t pending = it->pending_rewards;
    if (it->amount > 0 && acc > paid) {
      uint128_t owed = muldiv(static_cast<uint128_t>(it->amount), acc - paid, SCALE);
      check(owed <= static_cast<uint128_t>(MAX_I64), "owed overflow");
      check(static_cast<uint128_t>(pending) + owed <= static_cast<uint128_t>(MAX_I64), "pending overflow");
      pending += static_cast<int64_t>(owed);
    }
    uint64_t hi = 0, lo = 0;
    split128(acc, hi, lo);
    stakes.modify(it, same_payer, [&](auto& s) {
      s.pending_rewards = pending;
      s.paid_hi = hi;
      s.paid_lo = lo;
    });
  }
  void flush(config_row& c) {
    if (c.reward_pending_undistributed <= 0 || c.total_staked <= 0) return;
    uint128_t acc = join128(c.rpt_hi, c.rpt_lo);
    acc += muldiv(static_cast<uint128_t>(c.reward_pending_undistributed), SCALE,
                  static_cast<uint128_t>(c.total_staked));
    split128(acc, c.rpt_hi, c.rpt_lo);
    c.reward_pending_undistributed = 0;
  }
  void compound_into(config_row& c, stakes_tbl& stakes, name account) {
    if (!account.value) return;
    auto it = stakes.find(account.value);
    if (it == stakes.end()) return;
    settle(c, stakes, account.value);
    it = stakes.require_find(account.value, "no stake");
    int64_t pending = it->pending_rewards;
    if (pending <= 0) return;
    check(c.reward_pot >= pending, "reward pot short");
    c.reward_pot -= pending;
    check(c.total_staked <= MAX_I64 - pending, "stake overflow");
    c.total_staked += pending;
    stakes.modify(it, same_payer, [&](auto& s) {
      s.amount += pending;
      s.pending_rewards = 0;
    });
  }
  void fund_reward(config_row& c, int64_t qty) {
    check(qty > 0, "amount");
    if (c.total_staked == 0) {
      check(c.reward_pending_undistributed <= MAX_I64 - qty, "reward overflow");
      c.reward_pending_undistributed += qty;
      check(c.reward_pot <= MAX_I64 - qty, "reward overflow");
      c.reward_pot += qty;
    } else {
      int64_t chunk = qty;
      if (c.reward_pending_undistributed > 0) {
        check(chunk <= MAX_I64 - c.reward_pending_undistributed, "reward overflow");
        chunk += c.reward_pending_undistributed;
        c.reward_pending_undistributed = 0;
      }
      uint128_t acc = join128(c.rpt_hi, c.rpt_lo);
      acc += muldiv(static_cast<uint128_t>(chunk), SCALE, static_cast<uint128_t>(c.total_staked));
      split128(acc, c.rpt_hi, c.rpt_lo);
      check(c.reward_pot <= MAX_I64 - qty, "reward overflow");
      c.reward_pot += qty;
    }
    if (c.airdrop_account.value) {
      stakes_tbl stakes(get_self(), get_self().value);
      compound_into(c, stakes, c.airdrop_account);
    }
  }
  void credit_stake(config_row& c, name owner, int64_t qty) {
    check(qty > 0, "amount");
    check(c.total_staked <= MAX_I64 - qty, "stake overflow");
    stakes_tbl stakes(get_self(), get_self().value);
    auto it = stakes.find(owner.value);
    if (it == stakes.end()) {
      uint64_t hi = c.rpt_hi, lo = c.rpt_lo;
      stakes.emplace(get_self(), [&](auto& s) {
        s.owner = owner;
        s.amount = 0;
        s.paid_hi = hi;
        s.paid_lo = lo;
        s.pending_rewards = 0;
        s.unstake_amount = 0;
        s.unstake_ready = 0;
        s.vote_lock_until = 0;
      });
    } else {
      settle(c, stakes, owner.value);
    }
    it = stakes.require_find(owner.value, "no stake");
    stakes.modify(it, same_payer, [&](auto& s) { s.amount += qty; });
    c.total_staked += qty;
    if (!is_novote(owner)) {
      check(c.total_votable <= MAX_I64 - qty, "votable overflow");
      c.total_votable += qty;
    }
    flush(c);
    if (owner == c.airdrop_account) compound_into(c, stakes, owner);
  }
  void send_sat(const config_row& c, name to, int64_t amount, const char* memo) {
    action(permission_level{get_self(), "active"_n}, c.token_contract, "transfer"_n,
           std::make_tuple(get_self(), to, asset{amount, c.token_symbol}, std::string(memo)))
        .send();
  }
  void assert_invariant(const config_row& c) {
    check(c.total_staked >= 0 && c.total_cooling >= 0 && c.total_votable >= 0, "negative pot");
    check(c.total_votable <= c.total_staked, "votable>staked");
    check(c.reward_pot >= 0 && c.airdrop_liability >= 0, "negative pot");
    check(c.reward_pending_undistributed >= 0 && c.reward_pending_undistributed <= c.reward_pot, "undistributed");
    token_balance_tbl bals(c.token_contract, get_self().value);
    auto it = bals.find(c.token_symbol.code().raw());
    int64_t bal = it == bals.end() ? 0 : it->balance.amount;
    __int128 need = static_cast<__int128>(c.total_staked) + c.total_cooling + c.reward_pot + c.airdrop_liability;
    check(bal >= need, "invariant");
  }
  void check_split(uint16_t satlp, uint16_t leeflp, uint16_t ops, uint16_t rewards, uint16_t reserve) {
    uint32_t sum = uint32_t(satlp) + leeflp + ops + rewards + reserve;
    check(sum == 10000, "split sum");
    check(satlp <= SPLIT_EACH_MAX && leeflp <= SPLIT_EACH_MAX && ops <= SPLIT_EACH_MAX &&
              rewards <= SPLIT_EACH_MAX && reserve <= SPLIT_EACH_MAX,
          "split each");
    check(satlp >= SPLIT_SATLP_MIN, "satlp min");
    check(ops <= SPLIT_OPS_MAX, "ops max");
    check(rewards >= SPLIT_REWARDS_MIN, "rewards min");
  }
  void check_limits(uint32_t max_in, uint32_t daily) {
    check(max_in >= LIMIT_MAX_IN_MIN && max_in <= LIMIT_MAX_IN_MAX, "max_in bounds");
    check(daily >= LIMIT_DAILY_MIN && daily <= LIMIT_DAILY_MAX, "daily bounds");
    check(max_in <= daily, "max_in>daily");
  }
  void log_stake(name owner, int64_t amount, int64_t total_staked) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logstake"_n,
           std::make_tuple(owner, amount, total_staked))
        .send();
  }
  void log_unstake(name owner, int64_t amount, uint32_t ready) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logunstake"_n,
           std::make_tuple(owner, amount, ready))
        .send();
  }
  void log_claim(name owner, int64_t amount) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logclaim"_n, std::make_tuple(owner, amount))
        .send();
  }
  void log_reward(name from, int64_t amount, int64_t pot) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logreward"_n,
           std::make_tuple(from, amount, pot))
        .send();
  }
  void log_propose(uint64_t id, name proposer, name kind) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logpropose"_n,
           std::make_tuple(id, proposer, kind))
        .send();
  }
  void log_vote(uint64_t id, name voter, bool support, int64_t weight) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logvote"_n,
           std::make_tuple(id, voter, support, weight))
        .send();
  }
  void log_queue(uint64_t id, uint8_t status, uint32_t eta) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logqueue"_n, std::make_tuple(id, status, eta))
        .send();
  }
  void log_exec(uint64_t id, name kind) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logexec"_n, std::make_tuple(id, kind)).send();
  }
  void log_veto(uint64_t id, name guardian) {
    action(permission_level{get_self(), "active"_n}, get_self(), "logveto"_n, std::make_tuple(id, guardian))
        .send();
  }
};
