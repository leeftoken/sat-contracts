# Build brief: SAT staking + governance (`smartastake1`)

Paste this whole file into regular Grok. Goal: write, build, and **locally test** a staking contract for SAT on WAX that also supports bounded governance over fee/split parameters. **Do not deploy to mainnet. Do not create mainnet accounts. Do not sign mainnet transactions.**

SAT contracts are **not audited yet**. Keep that wording in the README.

Companion brief (vesting): if available, see `grok-vesting-brief.md` for `smartavest11`. This staking contract is the natural **beneficiary / sink** for the whitepaper **rewards** stream (25% of max supply, 4-year linear vest).

---

## 1. What SAT is (context)

- **Chain:** WAX (Antelope).
- **Token:** `SAT@sattoken1111`, **4 decimals**, max `100000000.0000 SAT`.
- **Treasury / automation contract (live):** `smartatrader` (code name `leeftrader`).
- **Fee recipient (live):** `smartafees11` — receives Alcor `collect` fees from locked LP positions owned by that account / contract path. That is **LP trading-fee cashflow**, separate from `smartatrader` revenue buckets.
- **Vesting (planned):** `smartavest11` — holds team + rewards allocation streams; rewards stream should ultimately fund stakers via this contract (or via `smartatrader` `rewards` bucket → here).

### Whitepaper buckets (of 100M max)

| Bucket | % | Amount | Relation to this contract |
|---|---|---|---|
| Locked LP | 35% | 35M | Alcor locks — not here |
| **User rewards** | **25%** | **25M** | Vest over 4y → **stake rewards** |
| Treasury | 15% | 15M | Multisig ops — not here |
| Team | 15% | 15M | Vest — not here |
| Community airdrop | 10% | 10M | Already distributed |

### Live `smartatrader` knobs governance must map to

On-chain today (admin-gated; bot cannot change them):

| Action | What it sets | Live / proposal values |
|---|---|---|
| `setsplit(buckets[], bps[])` | Revenue split; **must total 10000 bps**; last bucket absorbs dust | Live: `satlp 3000`, `leeflp 2000`, `ops 3000`, `rewards 1000`, `reserve 1000` |
| `setlimits(max_in_bps, daily_cap_bps, lock_secs)` | Per-action / per-day spend caps + Alcor LP lock length | Live: `max_in_bps=100` (1%), `daily_cap_bps=500` (5%), `lock_secs≈42y` |
| `setpool` / `settoken` / `setswap` / `setdest` / `setplan` | Whitelists & subscription prices | **Out of scope for v1 governance** (too sharp / easy to brick) |
| `setbot` / `setfeercpt` | Bot & fee recipient | **Never** governance-votable in v1 |

**Off-chain / bot strategy (not in `smartatrader` ABI today):** trading bots decide how much of reinvested fees go into SAT LP vs LEEF farms (recently aimed at ~25–50% into SAT LP, with a WAX trigger). Governance v1 should expose a **bounded config row** the bots read (see §5), *or* require a tiny `smartatrader` extension (see §8).

`smartafees11` flow: Alcor position fees → `collect` → `smartafees11`. That account is **not** the stake vault. Do not mix user stake with fee-recipient balances.

---

## 2. Proposed account name

- Mainnet intent: **`smartastake1`**
- Contract C++ name: `smartastake` / `[[eosio::contract("smartastake")]]`
- Local tests may use a short name if needed.

---

## 3. Staking specification

### 3.1 Goals

- Users stake **SAT@sattoken1111 only** via transfer notify.
- Unstake with a **cooldown** (default 3–7 days; governance may change only inside hard min/max).
- Rewards use classic **reward-per-token** accounting (Synthetix-style) so late stakers don’t dilute past rewards.
- Reward funding: transfers of SAT into the contract with memo `reward` (from vest claims, `smartatrader` `payout` of the `rewards` bucket, or fee-share buybacks) — **not** from user stake balances.
- `claim` pays rewards to the staker; unstake returns principal after cooldown.
- **No admin withdraw of user stake.** No redirect of another user’s stake. Overflow / rounding guards.

### 3.2 Accepted token

- Notify path: `get_first_receiver() == config.token_contract` (mainnet `sattoken1111`).
- Symbol must be `4,SAT`. Reject other contracts/symbols/zero/negative.
- `init` sets `token_contract` once (local tests point at a mock `sat.token`).

### 3.3 Tables

**`config` (singleton)**

| Field | Type | Notes |
|---|---|---|
| `admin` | `name` | Bootstrap + guardian in v1 |
| `token_contract` | `name` | `sattoken1111` on mainnet |
| `token_symbol` | `symbol` | `4,SAT` |
| `guardian` | `name` | Can veto / cancel execution in v1 (tradeoff — see §4.6) |
| `trader` | `name` | `smartatrader` — target for executed param changes |
| `cooldown_secs` | `uint32_t` | Current unstake cooldown |
| `min_cooldown` / `max_cooldown` | `uint32_t` | Hard bounds (e.g. 1d .. 30d) |
| `proposal_threshold` | `int64_t` | Min staked SAT (smallest units) to open a proposal |
| `quorum_bps` | `uint16_t` | e.g. 1000 = 10% of total staked at snapshot |
| `approval_bps` | `uint16_t` | e.g. 5000 = 50% of votes cast, or of snapshot — **pick and document** (recommend: of votes cast, with separate quorum on snapshot total) |
| `voting_period_secs` | `uint32_t` | e.g. 3–7 days |
| `timelock_secs` | `uint32_t` | Delay after success before `execute` |
| `reward_per_token_stored` | `uint128_t` or split hi/lo | Accumulator (scale 1e18 or 1e12) |
| `total_staked` | `int64_t` | Active stake (not in cooldown) |
| `reward_rate` | `uint64_t` | Optional SAT/sec emission if using continuous drip; else 0 and fund via `reward` transfers |
| `sat_lp_fee_share_bps` | `uint16_t` | Bot-facing: share of fee reinvest to SAT LP (e.g. 2500–5000); hard-bounded |
| `buyback_share_bps` | `uint16_t` | Optional bot-facing knob; hard-bounded |
| `inited` | implied | |

**`stakes` (scope = self, PK = owner)**

| Field | Type |
|---|---|
| `owner` | `name` |
| `amount` | `int64_t` | Active staked |
| `reward_debt` / `user_reward_per_token_paid` | matching accumulator type |
| `pending_rewards` | `int64_t` | Accrued but unclaimed |
| `unstake_amount` | `int64_t` | Amount cooling down |
| `unstake_ready` | `uint32_t` | Unix time when withdraw allowed |
| `vote_lock_until` | `uint32_t` | Optional: block unstake while vote open if using lock model |

**`proposals` (PK = id)**

| Field | Type |
|---|---|
| `id` | `uint64_t` |
| `proposer` | `name` |
| `kind` | `uint8_t` / `name` | Whitelisted change type |
| `payload` | packed bytes or fixed fields | See §4.3 |
| `snapshot_total` | `int64_t` | `total_staked` at propose time |
| `start` / `end` | `uint32_t` | Voting window |
| `for_votes` / `against_votes` | `int64_t` |
| `eta` | `uint32_t` | `end + timelock` once succeeded; 0 otherwise |
| `status` | `uint8_t` | active / succeeded / defeated / queued / executed / cancelled / vetoed |

**`ballots` (scope = proposal id, PK = voter)** — one vote per account per proposal; weight frozen at first vote from that account’s stake at vote time (or from snapshot — pick one; recommend **weight = active stake at cast**, and **forbid unstake that would reduce below voted weight until voting ends** OR snapshot all voters — simplest v1: **lock active stake from unstake while any vote is active for that user** via `vote_lock_until = max(vote_lock_until, proposal.end)`).

### 3.4 Stake / unstake / claim flow

**Stake (notify)** — memo `stake` or empty when `to == self`:

1. Update global accumulator; settle user’s pending rewards.
2. `amount += qty`; `total_staked += qty`.
3. Reject if still in a way that breaks vote locks (staking more is OK).

**Unstake request** — action `unstake(amount)` (auth = owner):

1. Settle rewards.
2. Require `amount > 0`, `amount <= stakes.amount`, and not violating vote lock.
3. Move from `amount` → `unstake_amount` (or replace prior cooldown: document whether new request resets timer — recommend **reset timer for the new total cooling amount**).
4. `unstake_ready = now + cooldown_secs`; `total_staked -= amount`.

**Withdraw** — action `withdraw()` after `now >= unstake_ready`:

1. Transfer `unstake_amount` SAT to owner; clear cooldown fields.
2. Never touch other users’ balances.

**Claim** — action `claim()` (auth = owner):

1. Settle; transfer `pending_rewards` SAT to owner; zero pending.
2. Rewards balance must come from the contract’s **reward pot** (track `reward_balance` separately from `total_staked + sum(unstake_amount)` so principal can’t be paid as rewards).

**Fund rewards** — notify memo `reward`:

1. Only increase reward pot + update `reward_per_token_stored` (if instantaneous distribute) **or** bump `reward_rate` / period (if streaming).
2. Recommend v1: **instant reward-per-token update** on each `reward` transfer:  
   `reward_per_token += amount * 1e18 / total_staked` (if `total_staked == 0`, hold in `reward_pending_undistributed` until first stake, or reject — **prefer hold**).

### 3.5 Reward-per-token maths

```
acc' = acc + (incoming_reward * SCALE) / total_staked     // if total_staked > 0
user_pending += stake * (acc' - user_paid) / SCALE
user_paid = acc'
```

- Use `uint128` / `__int128` intermediates.
- Never let `claim` reduce balances below `total_staked + cooling` (invariant: `sat_balance >= total_staked + sum_cooling + reward_pot` accounting — keep explicit pots).
- Rounding: dust stays in reward pot; invariant `sum(user pending+paid effects) <= funded rewards`.

### 3.6 No admin rug

- No `withdrawadmin`, `rescue` of SAT that is user stake or cooling.
- Optional: after N days, admin/guardian may sweep **only** `reward_pending_undistributed` when `total_staked == 0` — document; default **omit** for stricter safety.

---

## 4. Governance specification

### 4.1 Voting power

- **1 staked SAT (smallest unit) = 1 vote**, using **active** stake only (not cooling).
- On `vote(id, support)`: record ballot; add weight to for/against; set `vote_lock_until`.
- Double voting: second ballot from same voter rejected.
- Stake/unstake games: vote lock prevents reducing active stake used for an open vote; transfers of SAT don’t move stake (stake is account-bound).

### 4.2 Proposal lifecycle

1. `propose(kind, payload…)` — proposer active stake ≥ `proposal_threshold`; snapshot `total_staked`; status=active; `end = now + voting_period`.
2. `vote` until `end`.
3. `queue(id)` after `end`: if `for+against >= snapshot_total * quorum_bps / 10000` AND `for * 10000 >= (for+against) * approval_bps` → status=queued, `eta = now + timelock_secs`; else defeated.
4. `execute(id)` after `now >= eta` — permissionless call, but effects only via whitelisted executors (below).
5. Guardian may `veto(id)` while queued (before execute) — **v1 safety tradeoff** (centralization).

### 4.3 Whitelisted proposal kinds (v1) + hard bounds

Only these `kind` values; anything else rejected at propose time.

| kind | Payload | Executes as | Hard bounds (enforce in stake contract **and** document for trader) |
|---|---|---|---|
| `setsplit` | five bps: `satlp, leeflp, ops, rewards, reserve` (must sum 10000; fixed bucket names/order matching live) | Inline `smartatrader::setsplit` | Each bucket 0..5000; `satlp >= 1000`; `ops <= 4000`; `rewards >= 500` (tune numbers but **encode constants**) |
| `setlimits` | `max_in_bps, daily_cap_bps` (do **not** allow changing `lock_secs` via gov in v1 — keep 42y LP lock sacred) | Inline `smartatrader::setlimits(max, daily, current_lock_secs)` reading current lock from trader config **or** pass through unchanged lock | `max_in_bps` ∈ [50, 500]; `daily_cap_bps` ∈ [100, 2000]; `max_in_bps <= daily_cap_bps` |
| `setcooldown` | `cooldown_secs` | Local config only | ∈ `[min_cooldown, max_cooldown]` |
| `set_sat_lp_share` | `sat_lp_fee_share_bps` | Local config (bots read) | ∈ [1000, 8000] |
| `set_buyback_share` | `buyback_share_bps` | Local config (bots read) | ∈ [0, 5000] |
| `set_reward_params` | optional rate/period | Local only | Rate capped; period ≥ 1 day |

**Not votable in v1:** `setbot`, `setfeercpt`, `setpool`, `settoken`, `setswap`, `setdest`, `setplan`, upgrade/setcode, mint, vest params.

### 4.4 Execution model (v1)

**Preferred:** `execute` does an **inline action** as `smartastake1@eosio.code` that calls `smartatrader::setsplit` / `setlimits`, but **today those actions `require_auth(admin)`** (`smartaadmin1`). So either:

**A (recommended small trader change):** add `smartatrader` action or auth path: allow `config.gov` (or `config.admin` OR `config.gov`) to call `setsplit` / `setlimits` only, with the same internal checks. Set `gov = smartastake1` via a one-time admin `setgov` action.

**B (guardian-ratified v1):** `execute` only marks proposal `executed` and emits `logexec`; guardian/admin must manually push the matching `smartatrader` action within a deadline. Honest but slower; document clearly.

Ship **A** if you can patch `smartatrader`; otherwise implement **B** and still store bot-facing bps locally via kinds that don’t need trader.

### 4.5 Logging / events

Self-auth log actions (or `print`/on-chain actions): `logstake`, `logunstake`, `logclaim`, `logreward`, `logpropose`, `logvote`, `logqueue`, `logexec`, `logveto`. Enough for Hyperion indexers.

### 4.6 Guardian / veto tradeoff (say this plainly in README)

v1 keeps a **guardian** that can veto a queued proposal before execute. That reduces risk of a low-liquidity governance attack while stake is small, but it is **not fully decentralized**. Roadmap: raise quorum, then null guardian or replace with multisig-only emergency pause. SAT remains **not audited yet**.

---

## 5. Fit with `smartavest11` and fee flows

```text
smartavest11 (rewards stream, 4y)
    --claim--> beneficiary (recommend: smartastake1 or a thin rewards payer)
                    --transfer memo "reward"--> smartastake1  (reward pot)

smartatrader rewards bucket (10% of revenue split)
    --payout (admin/bot to whitelisted dest)--> smartastake1 memo "reward"

smartafees11 (Alcor LP fees)
    --separate cashflow; optional future buyback of SAT into reward pot (off-chain/bot)-->
```

Recommend vest **rewards** beneficiary = `smartastake1` so claimed SAT lands directly with memo `reward` (vest claim = transfer from vest → stake; stake notify treats it as reward **only if** memo says `reward` — so vest claim memo must be `reward`, or stake offers `notify` from vest account allowlist). Simplest: allowlist `config.reward_funders[]` including `smartavest11` and `smartatrader`.

---

## 6. Actions (summary)

| Action | Auth | Purpose |
|---|---|---|
| `init(...)` | self once | admin, guardian, token, trader, bounds, initial cooldown |
| `setgov` on trader (separate PR) | trader admin | register stake contract |
| `unstake` / `withdraw` / `claim` | staker | as above |
| `propose` / `vote` / `queue` / `execute` / `veto` | as above | governance |
| `on_notify(*::transfer)` | token | `stake` / `reward` memos |

---

## 7. Required local tests (nodeos; CDT 4.1.1)

1. Stake via transfer; total_staked & balances update.  
2. Unstake → cooldown; withdraw before ready fails; after ready succeeds.  
3. Wrong token / wrong symbol rejected.  
4. Reward fund + two stakers: claim amounts match proportional math (within 1 unit).  
5. Stake after reward doesn’t steal prior rewards.  
6. Claim doesn’t touch principal; unstake doesn’t steal reward pot.  
7. Propose below threshold fails.  
8. Double vote rejected.  
9. Unstake while vote-locked fails.  
10. Quorum fail → defeated; quorum+approval → queue.  
11. Execute before timelock fails; after succeeds (or marks executed in model B).  
12. Out-of-bounds split / limits / cooldown rejected at propose or execute.  
13. Unauthorized execute of non-queued id fails.  
14. Veto prevents execute.  
15. Non-whitelisted kind rejected.  
16. Invariant: contract SAT ≥ staked + cooling + reward_pot.  

Save `tests/last-run.log`. Production build has no test clock unless you add one compiled out by default (same pattern as vesting brief).

---

## 8. Change required on `smartatrader` (call out)

**Today:** `setsplit` and `setlimits` are `require_auth(c.admin)` only.

**Needed for on-chain auto-execution (model A):**

- Add `name gov` to `config` (default empty).
- Add admin action `setgov(name gov)` (once/rare).
- In `setsplit` and `setlimits` only: `check(has_auth(c.admin) || (c.gov.value && has_auth(c.gov)), "missing auth");`
- Do **not** extend this to `setbot` / `setfeercpt` / whitelist setters in v1.
- Optionally read-only table export of current split for UIs.

**If that patch is deferred:** governance still ships with local bot knobs (`sat_lp_fee_share_bps`, cooldown) + guardian-ratified manual `setsplit`/`setlimits` (model B).

Bot strategy should read `sat_lp_fee_share_bps` from `smartastake1` config (replacing hard-coded 25%/50% fee→SAT LP share).

---

## 9. Deliverables

```text
sat-stake/
  README.md                 # maths, guardian tradeoff, "not audited yet", bounds table
  smartastake/smartastake.cpp
  smartastake/smartastake.hpp   # optional
  sat.token/sat.token.cpp       # test token
  build.sh                      # cdt-cpp 4.1.1 prod (+ optional test clock)
  tests/run_tests.sh
  tests/last-run.log
  build/smartastake.wasm
  build/smartastake.abi
  docs/TRADER_PATCH.md          # exact setgov auth patch for leeftrader.cpp
```

Build:

```bash
cdt-cpp -abigen -O3 -o build/smartastake.wasm smartastake/smartastake.cpp
```

CDT **4.1.1**; Spring nodeos/cleos ~**1.2.2**.

---

## 10. Out of scope

Mainnet account creation/deploy, real SAT mint/transfer, changing Alcor lock length via gov, upgrading `sattoken1111`, secrets/keys/signer lists, and any public discussion of multisig edge cases.

---

## 11. Sanity checklist

- [ ] SAT@configured contract only  
- [ ] User stake never admin-withdrawable  
- [ ] Reward pot ≠ principal  
- [ ] Vote lock / single ballot prevents double voting  
- [ ] Quorum + approval + timelock + bounds  
- [ ] Guardian veto documented as v1 tradeoff  
- [ ] Trader patch or model-B path documented  
- [ ] Local tests PASS; **not audited yet** in README  

## Addendum: Airdrop fund account (added 2026-11-09)

- A project airdrop account (proposed name `smartadrop11`) receives 2.5-5% of the staking-rewards allocation (exact % set at genesis, within [250, 500] bps) and stakes it in `smartastake1`. Its rewards auto-compound into its stake so the airdrop fund grows.
- **No voting power:** the stake contract keeps a `novote` list (set once at init, removable only by governance) and the airdrop account is on it. Its stake counts for rewards but NOT for voting power, quorum or proposal thresholds.
- **Governance-triggered airdrops:** new proposal type `airdrop {amount, snapshot_time, memo}`. If it passes quorum and threshold and its timelock expires, `execute` unstakes or claims up to `amount` from the airdrop account and moves it to a distribution holding table (or a merkle-claim table) for eligible holders. Recipient lists are snapshotted off-chain and committed as a merkle root in the proposal. Holders claim with `claimdrop {account, amount, proof}`.
- **Hard bounds:** at most 25% of the airdrop account's balance per airdrop proposal, at most 1 executed airdrop per 30 days, and a minimum 7-day voting period plus a 48h timelock. The guardian veto still applies in v1.
- **Required permission:** the airdrop account's active permission is `smartastake1@eosio.code` only (no human key) so funds leave only through passed proposals. Document the setup as a manual step; do not include keys.
- **Tests to add:** novote stake gets rewards but adds 0 voting power; airdrop proposal over the 25% bound is rejected; a second airdrop within 30 days is rejected; merkle claim works once per account and rejects bad proofs or double claims; the airdrop executes only after the timelock.
