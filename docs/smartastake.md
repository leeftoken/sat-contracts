# smartastake (`smartastake1`)

## Purpose

Staking and bounded governance for SAT on WAX. Users stake SAT, earn a pro-rata share of the reward pot, and vote on a fixed list of proposal kinds. An airdrop fund (`smartadrop11`) is a special no-vote stake that cannot unstake; stakers vote on merkle airdrops paid from that fund.

**Not audited. Not deployed.** Do not create `smartastake1` or send mainnet SAT to it until an external audit is complete and the owner has approved the lock batches. Source: `smartastake/smartastake.cpp`. Planned account: `smartastake1`. Planned production wasm sha256 (CDT 4.1.1, `-O3`, no test clock, after [PR #5](https://github.com/leeftoken/sat-contracts/pull/5)): `dff935d9797bac8a16bea24a42b4061cf066a42cd401dd9b541320fc6f42a2ad`. Rebuild to verify. Production ABI: `abi/smartastake.abi` (no `settime`). The test clock build is `abi/smartastake_test.abi` only.

There is no admin action that withdraws user stake, cooling principal, or the reward pot. There is no `rescue` and no sweep of undistributed rewards.

Passing `setsplit` or `setlimits` also requires the trader `setgov` patch. Until then those kinds can be voted on but cannot execute on `smartatrader`. Details: [leeftrader.md](leeftrader.md#governance-patch-setgov) and [smartastake/docs/TRADER_PATCH.md](../smartastake/docs/TRADER_PATCH.md). The airdrop fund is documented end-to-end in [airdrop-fund.md](airdrop-fund.md).

## Accounts and permissions

| Account | Role |
|---|---|
| `smartastake1` | Contract. `init` requires self. `active` must include `smartastake1@eosio.code` for outbound SAT, trader inlines, and logs. The contract pays RAM for stake rows created in the token notify (Antelope will not bill another account during a notify). Proposals are billed to the proposer; ballots and merkle claims are billed to the caller. |
| `smartaadmin1` | `admin` (only `setfunder`) and `guardian` (may `veto` a queued proposal). Also listed `novote` at the approved init, so it has no voting power if it stakes. |
| `sattoken1111` | Only accepted token, symbol `4,SAT`. |
| `smartatrader` | `trader`. SAT it sends is always reward funding. After `setgov`, this contract may call `setsplit` / `setlimits` on it. |
| `smartadrop11` | `airdrop_account`. Must be in `novote` at init. Cannot unstake, withdraw, or claim. |
| `smartavest11` | Must be added with `setfunder` so claims of the rewards stream (memo `vesting claim`) credit the pot. |
| `smartafees11` | Also `setfunder`, and listed `novote` at the approved init, so fee-forwarded SAT is reward and the fee account has no votes. |
| Ordinary stakers | Stake (transfer), `unstake`, `withdraw`, `claim`, `propose`, `vote`. `queue` and `execute` are permissionless. |

## Tables

Scope is `smartastake1` unless noted.

### `config` (row id 0)

| Field | Type | Approved / meaning |
|---|---|---|
| `admin` | `name` | `smartaadmin1` |
| `token_contract` | `name` | `sattoken1111` |
| `token_symbol` | `symbol` | `4,SAT` |
| `guardian` | `name` | `smartaadmin1` |
| `trader` | `name` | `smartatrader` |
| `airdrop_account` | `name` | `smartadrop11` |
| `airdrop_alloc_bps` | `uint16` | `500` (5% of the rewards bucket, recorded only) |
| `cooldown_secs` | `uint32` | `604800` (7 days). Votable in `[min_cooldown, max_cooldown]`. |
| `min_cooldown` / `max_cooldown` | `uint32` | `259200` / `2592000` (3–30 days). Fixed at init (init itself allows 1 s .. 365 days). |
| `proposal_threshold` | `int64` | `100000000` (10,000.0000 SAT). Fixed at init. |
| `quorum_bps` | `uint16` | `1000` (10%). Fixed at init. |
| `approval_bps` | `uint16` | `5000` (50% of votes cast). Fixed at init. |
| `voting_period_secs` | `uint32` | `259200` (3 days). Fixed at init. Airdrops use at least 7 days. |
| `timelock_secs` | `uint32` | `172800` (2 days). Fixed at init. Airdrops use at least 48 hours. |
| `rpt_hi` / `rpt_lo` | `uint64` | Reward-per-token accumulator, 1e18 scale, packed as uint128. |
| `total_staked` | `int64` | Active stake, including novote. |
| `total_cooling` | `int64` | Principal in cooldown. |
| `total_votable` | `int64` | Active stake of non-novote accounts. |
| `reward_pot` | `int64` | SAT reserved for unclaimed rewards (includes undistributed). |
| `reward_pending_undistributed` | `int64` | Held while `total_staked == 0`, then flushed into the accumulator. |
| `airdrop_liability` | `int64` | Executed but unclaimed airdrop SAT. |
| `reward_rate` / `reward_period_secs` | `uint64` / `uint32` | Stored only. v1 does not drip. |
| `sat_lp_fee_share_bps` | `uint16` | `5000` (50%). Bot knob, votable 1000..8000. |
| `buyback_share_bps` | `uint16` | `0`. Bot knob, votable 0..5000. |
| `proposal_seq` | `uint64` | Last proposal id. |
| `open_airdrop_id` | `uint64` | Non-zero while an airdrop proposal is in flight. |
| `last_airdrop_exec` | `uint32` | Time of last executed airdrop; 30-day gap is from this. |

`test_now` exists only in the test-clock build.

```bash
cleos get table smartastake1 smartastake1 config
```

### `stakes`

| Field | Type | Meaning |
|---|---|---|
| `owner` | `name` | Primary key. |
| `amount` | `int64` | Active stake (smallest units). |
| `paid_hi` / `paid_lo` | `uint64` | User's settled reward-per-token snapshot. |
| `pending_rewards` | `int64` | Settled but unclaimed rewards. Stale until the next settle. |
| `unstake_amount` | `int64` | Cooling principal. |
| `unstake_ready` | `uint32` | When `withdraw` is allowed. A new `unstake` **resets this for the whole cooling balance**. |
| `vote_lock_until` | `uint32` | `unstake` is rejected until this time. |

```bash
cleos get table smartastake1 smartastake1 stakes --lower YOURACCOUNT --upper YOURACCOUNT
```

### `proposals`

| Field | Type | Meaning |
|---|---|---|
| `id` | `uint64` | Primary key, from `proposal_seq`. |
| `proposer` | `name` | |
| `kind` | `name` | One of the whitelisted kinds. |
| `bps0`..`bps4` | `uint16` | For `setsplit`, in order `satlp, leeflp, ops, rewards, reserve`. |
| `p0` / `p1` | `uint32` | Kind-specific (limits, cooldown, shares, period). |
| `rate` | `uint64` | For `setrparams`. |
| `amount` / `snapshot_time` / `memo` / `merkle_root` | airdrop payload | |
| `novote_account` | `name` | For `delnovote`. |
| `snapshot_total` | `int64` | `total_votable` at propose time, used for quorum. |
| `start_time` / `end_time` | `uint32` | Vote window. |
| `applied_timelock` | `uint32` | Used at queue to set `eta`. |
| `for_votes` / `against_votes` | `int64` | |
| `eta` | `uint32` | Execute-after time if queued. |
| `status` | `uint8` | `1` active, `2` defeated, `3` queued, `4` executed, `5` vetoed, `6` expired. |

### `ballots` (scope = proposal id)

`voter`, `support`, `weight`. One row per voter. RAM paid by the voter.

### `novote`

Set at init (approved: `smartadrop11`, `smartaadmin1`, `smartafees11`). At most 8 entries at init. The fund cannot be removed.

### `funders`

Accounts whose non-`stake` / non-empty memos still count as reward funding. Approved launch adds `smartavest11` and `smartafees11`.

### `drops`

Executed airdrops. `id` equals the proposal id. Fields: `merkle_root`, `amount`, `claimed`, `snapshot_time`, `memo`.

### `claims` (scope = drop id)

`account`, `amount` claimed. One claim per account per drop.

## Reward maths

Synthetix-style, lazy settlement. Scale is `1e18`.

```
if total_staked == 0:
    reward_pending_undistributed += incoming
    reward_pot += incoming
else:
    acc += (incoming + held) * SCALE / total_staked
    reward_pot += incoming
    held = 0

on settle(user):
    pending += user.amount * (acc - user.paid) / SCALE
    user.paid = acc
```

Division truncates toward zero. Dust stays in the reward pot. The first stake after a zero-stake funding period receives the held amount (`flush` runs after that stake is credited, so `paid` is the pre-flush accumulator). Settlement runs on claim, unstake, adding to an existing stake, and airdrop compound. Stored `pending_rewards` stays stale until then on purpose: funding does not walk every staker.

`reward_rate` and `reward_period_secs` are stored only. v1 does not drip. If a proposal sets a rate above 0, the period must be at least 1 day. The stored rate is capped at `1000000` (100.0000 SAT per second as a unit cap, not an emission).

The airdrop fund is compounded immediately inside a reward notify, and anyone may call `compound` for that account only.

### Invariant

```
token_balance(contract) >= total_staked + total_cooling + reward_pot + airdrop_liability
total_votable <= total_staked
reward_pending_undistributed <= reward_pot
sum(stored pending) <= reward_pot
```

The local test suite checks this after every run.

## Voting

1 active-stake unit = 1 vote. Cooling does not vote. Novote accounts earn rewards and count in `total_staked`, but add **0** to `total_votable`, quorum, and the proposal threshold.

Weight is the voter's active stake at the moment they vote, then frozen on that ballot. A second ballot on the same proposal is rejected. Voting sets `vote_lock_until` to the later of its current value and the proposal end. Unstake is rejected until that time. Staking more during a lock is allowed. Proposer stake is not locked just for proposing.

Lifecycle:

1. `propose` — proposer is not novote, and their active stake is at least `proposal_threshold`. Snapshot is `total_votable` at that moment.
2. `vote` until `end_time`.
3. `queue` after `end_time`. Pass requires both:
   - quorum against the snapshot: `(for + against) * 10000 >= snapshot_total * quorum_bps`
   - approval of votes cast: `for * 10000 >= (for + against) * approval_bps`
   Otherwise the proposal is defeated (and an airdrop frees `open_airdrop_id`).
4. On pass, `eta = now + applied_timelock`. `execute` is permissionless after `eta`.
5. The guardian may `veto` while status is queued, before execute.
6. A queued proposal must be executed within 14 days after `eta` (`EXEC_GRACE`). A later `execute` marks it expired (status 6), frees the airdrop slot if needed, and changes nothing else. This stops a stale `setsplit` / `setlimits` / `setcooldown` from overriding a newer decision months later.

Guardian veto is a v1 centralization tradeoff. It reduces the risk of a low-liquidity governance attack while votable stake is small. It is **not** full decentralization. A queued change is not final until the timelock ends without a veto (and is then executed). Stakers cannot vote the guardian out; signer changes on `smartaadmin1` are a multisig `updateauth` ([README.md](README.md#multisig-signer-management)).

## Whitelisted proposal kinds

Anything else is rejected at `propose` (`kind not allowed`). Bounds are checked again at `execute`.

| Kind | Effect | Bounds |
|---|---|---|
| `setsplit` | Inline `smartatrader::setsplit` in order `satlp, leeflp, ops, rewards, reserve` | Sum 10000. Each bucket 0..5000. `satlp >= 1000`. `ops <= 4000`. `rewards >= 500`. Needs the `setgov` patch. |
| `setlimits` | Inline `smartatrader::setlimits(max, daily, current lock_secs)` | `max_in` 50..500, `daily` 100..2000, `max_in <= daily`. Lock length is read and passed through. Governance cannot change it. Needs the patch. |
| `setcooldown` | Local `cooldown_secs` | Inside `[min_cooldown, max_cooldown]` (approved 3–30 days). |
| `setsatlps` | Local bot knob `sat_lp_fee_share_bps` | 1000..8000 |
| `setbuyback` | Local bot knob `buyback_share_bps` | 0..5000 |
| `setrparams` | Local stored rate and period only | Rate `<= 1000000`. Period `>= 86400` if rate `> 0`. |
| `delnovote` | Remove one novote account and add its active stake to `total_votable` | Cannot remove the airdrop fund. |
| `airdrop` | See [airdrop-fund.md](airdrop-fund.md) | 25% of fund stake, 30-day gap, 7-day vote, 48h timelock. |

Not votable in v1: `setbot`, `setfeercpt`, `setpool`, `settoken`, `setswap`, `setdest`, `setplan`, `setcode`, mint, quorum / threshold / voting period / timelock / guardian, and the Alcor lock length.

## Actions

Amounts in actions are SAT **smallest units** (4 decimals): `100000000` = `10000.0000 SAT`.

### `init(...)`

```
init(admin, guardian, token_contract, token_symbol, trader,
     airdrop_account, airdrop_alloc_bps, novote,
     cooldown_secs, min_cooldown, max_cooldown,
     proposal_threshold, quorum_bps, approval_bps,
     voting_period_secs, timelock_secs,
     sat_lp_fee_share_bps, buyback_share_bps)
```

- **Auth:** `smartastake1`.
- **Effects:** Writes `config` id 0 and the `novote` list. Once only. If `airdrop_account` is set, it must appear in `novote` and `airdrop_alloc_bps` must be in 250..500. If no airdrop account, `airdrop_alloc_bps` must be 0. At most 8 novote names, no duplicates.
- **Errors:** `already inited`, `names`, `symbol must be 4,SAT`, `cooldown bounds`, `threshold`, `quorum`, `approval`, `voting period`, `timelock`, `sat lp share`, `buyback share`, `novote size`, `novote name`, `novote dup`, `airdrop must be novote`, `alloc bps`.

Init bounds (independent of the approved values): `min_cooldown >= 1`, `max_cooldown >= min` and `<= 365 days`, cooldown inside that range; `proposal_threshold > 0`; quorum and approval in 1..10000; voting period 3600..30 days; timelock 60 s..30 days; sat LP share 1000..8000; buyback ≤ 5000.

```bash
cleos push action smartastake1 init '{
  "admin":"smartaadmin1","guardian":"smartaadmin1",
  "token_contract":"sattoken1111","token_symbol":"4,SAT",
  "trader":"smartatrader","airdrop_account":"smartadrop11",
  "airdrop_alloc_bps":500,
  "novote":["smartadrop11","smartaadmin1","smartafees11"],
  "cooldown_secs":604800,"min_cooldown":259200,"max_cooldown":2592000,
  "proposal_threshold":100000000,"quorum_bps":1000,"approval_bps":5000,
  "voting_period_secs":259200,"timelock_secs":172800,
  "sat_lp_fee_share_bps":5000,"buyback_share_bps":0
}' -p smartastake1@active
```

### `setfunder(admin, account, add)`

- **Auth:** `admin`, and `admin == cfg.admin`.
- **Effects:** Insert or erase a `funders` row.
- **Errors:** `not admin`, `account`, `already funder`, `not funder`.

```bash
cleos push action smartastake1 setfunder '{"admin":"smartaadmin1","account":"smartavest11","add":true}' -p smartaadmin1@active
cleos push action smartastake1 setfunder '{"admin":"smartaadmin1","account":"smartafees11","add":true}' -p smartaadmin1@active
```

Must run (B3) before any vest claim or fee forward, and before the admin is retired. There is no other way to add a funder later if admin can no longer sign.

### `unstake(owner, amount)`

- **Auth:** `owner`.
- **Params:** `amount > 0` and `<=` active stake. Not the airdrop fund. Not while `now < vote_lock_until`.
- **Effects:** Settles rewards first (pending stays in the row; this does not pay them). Moves `amount` from active to cooling, resets `unstake_ready = now + cooldown_secs` for the **whole** cooling balance, updates totals.
- **Errors:** `airdrop fund is proposal-only`, `amount`, `no stake`, `vote locked`, `over unstake`, plus invariant / overflow checks.

```bash
# Unstake 50 SAT (500000 smallest units)
cleos push action smartastake1 unstake '{"owner":"YOURACCOUNT","amount":500000}' -p YOURACCOUNT@active
```

```js
{ account: 'smartastake1', name: 'unstake',
  authorization: [{ actor: 'YOURACCOUNT', permission: 'active' }],
  data: { owner: 'YOURACCOUNT', amount: '500000' } }
```

### `withdraw(owner)`

- **Auth:** `owner`.
- **Effects:** Pays the entire cooling balance to `owner` with memo `withdraw`, once `now >= unstake_ready`. Does not touch the reward pot. Does not pay pending rewards.
- **Errors:** `airdrop fund is proposal-only`, `no stake`, `nothing cooling`, `not ready`.

```bash
cleos push action smartastake1 withdraw '{"owner":"YOURACCOUNT"}' -p YOURACCOUNT@active
```

### `claim(owner)`

- **Auth:** `owner`.
- **Effects:** Settles, then pays `pending_rewards` from `reward_pot` with memo `claim`. Principal is unchanged.
- **Errors:** `airdrop fund is proposal-only`, `no stake`, `nothing to claim`, `reward pot short`.

```bash
cleos push action smartastake1 claim '{"owner":"YOURACCOUNT"}' -p YOURACCOUNT@active
```

### `compound(account)`

- **Auth:** none. `account` must be the airdrop fund.
- **Effects:** Settles the fund and folds `pending_rewards` into its active `amount` (and into `total_staked`; votable is unchanged because the fund is novote).
- **Errors:** `not airdrop fund`.

```bash
cleos push action smartastake1 compound '{"account":"smartadrop11"}' -p ANYACCOUNT@active
```

### `propose(proposer, kind, bps, p0, p1, rate, amount, snapshot_time, memo, merkle_root, novote_account)`

- **Auth:** `proposer`.
- **Requirements:** not novote; active stake ≥ threshold; `total_votable > 0`. Unused payload fields for that kind must be zero / empty (`extra payload`).
- **Effects:** Allocates the next id (RAM paid by the proposer). For `airdrop`, also compounds the fund, checks the 25% cap and 30-day gap, requires a non-zero root and a non-future `snapshot_time`, forces vote ≥ 7 days and timelock ≥ 48 hours, and sets `open_airdrop_id`.
- **Errors:** `no voting power`, `no stake`, `below threshold`, `no votable`, `kind not allowed`, plus the per-kind messages (`split len`, `split sum`, `satlp min`, `ops max`, `rewards min`, `max_in bounds`, `daily bounds`, `max_in>daily`, `cooldown bounds`, `sat lp share`, `buyback share`, `rate cap`, `period`, `unexpected bps`, `novote account`, `airdrop fund stays novote`, `not novote`, `no airdrop fund`, `airdrop open`, `airdrop cooldown`, `amount`, `memo`, `snapshot_time`, `empty root`, `fund not staked`, `airdrop cap`).

```bash
# Raise unstake cooldown to 14 days (still inside 3–30).
cleos push action smartastake1 propose '{
  "proposer":"YOURACCOUNT","kind":"setcooldown",
  "bps":[],"p0":1209600,"p1":0,"rate":0,"amount":0,
  "snapshot_time":0,"memo":"","merkle_root":"0000000000000000000000000000000000000000000000000000000000000000",
  "novote_account":""
}' -p YOURACCOUNT@active

# setsplit example: keep live order, move rewards to 15% from reserve.
cleos push action smartastake1 propose '{
  "proposer":"YOURACCOUNT","kind":"setsplit",
  "bps":[3000,2000,3000,1500,500],"p0":0,"p1":0,"rate":0,"amount":0,
  "snapshot_time":0,"memo":"","merkle_root":"0000000000000000000000000000000000000000000000000000000000000000",
  "novote_account":""
}' -p YOURACCOUNT@active
```

```js
{ account: 'smartastake1', name: 'propose',
  authorization: [{ actor: 'YOURACCOUNT', permission: 'active' }],
  data: {
    proposer: 'YOURACCOUNT', kind: 'setcooldown',
    bps: [], p0: 1209600, p1: 0, rate: 0, amount: 0,
    snapshot_time: 0, memo: '',
    merkle_root: '0000000000000000000000000000000000000000000000000000000000000000',
    novote_account: ''
  } }
```

An airdrop propose sets `amount` (units), `snapshot_time` (unix, ≤ now), `memo`, and `merkle_root`. See [airdrop-fund.md](airdrop-fund.md).

### `vote(voter, id, support)`

- **Auth:** `voter`.
- **Effects:** Adds the voter's current active stake as weight, writes a ballot (RAM paid by the voter), and extends `vote_lock_until` to at least `end_time`.
- **Errors:** `no voting power` (novote, or zero active stake), `no proposal`, `not active`, `vote window`, `already voted`, `no stake`, `votes overflow`.

```bash
cleos push action smartastake1 vote '{"voter":"YOURACCOUNT","id":1,"support":true}' -p YOURACCOUNT@active
```

### `queue(id)`

- **Auth:** none.
- **Effects:** After `end_time`, sets status to queued (`eta = now + applied_timelock`) or defeated, using the snapshot quorum and approval rules above.
- **Errors:** `no proposal`, `not active`, `voting open`.

```bash
cleos push action smartastake1 queue '{"id":1}' -p ANYACCOUNT@active
```

### `execute(id)`

- **Auth:** none.
- **Effects:** After `eta`, applies the kind (inline trader calls, local config writes, `delnovote`, or airdrop debit + `drops` row). Marks executed. If `now > eta + 14 days`, marks expired instead and returns (status change sticks).
- **Errors:** `not queued`, `timelock`, plus the same kind bounds as propose, `trader config missing`, `fund short`, `vote locked` (fund), `drop exists`. A `setsplit` / `setlimits` execute fails with a trader auth error until `setgov` is live.

```bash
cleos push action smartastake1 execute '{"id":1}' -p ANYACCOUNT@active
```

### `veto(guardian, id)`

- **Auth:** `guardian`, who must be `cfg.guardian`.
- **Effects:** While the proposal is queued, sets status to vetoed (`5`) and clears `eta`. The proposal is not applied.
- **Errors:** `not guardian`, `no proposal`, `not queued`.

```bash
cleos push action smartastake1 veto '{"guardian":"smartaadmin1","id":1}' -p smartaadmin1@active
```

`veto` does not clear `open_airdrop_id`. A defeated `queue` or an `execute` that expires a still-queued airdrop does. If the guardian vetoes a queued airdrop, `open_airdrop_id` can stay set, and a later `airdrop` propose fails with `airdrop open`. Treat that as a known v1 limit: prefer letting a bad airdrop expire (call `execute` after `eta + 14 days`) rather than vetoing it, unless you have a follow-up plan to clear the slot.

### `claimdrop(account, drop_id, amount, proof)`

- **Auth:** `account`.
- **Params:** `amount > 0` (smallest units); `proof` has at most 32 siblings; drop must exist; proof must match `merkle_root`; the account must not have claimed this drop; `claimed + amount <= drop.amount`; `airdrop_liability >= amount`.
- **Effects:** Writes a `claims` row (RAM paid by the caller), increases `drops.claimed`, decreases `airdrop_liability`, transfers SAT with memo `airdrop`.
- **Errors:** `amount`, `proof too long`, `no drop`, `bad proof`, `already claimed`, `claimed overflow`, `drop exhausted`, `liability`.

Merkle format:

```
leaf = sha256( uint64le(account.value) || int64le(amount) )
node = sha256( min(left, right) || max(left, right) )   # bytewise on the 32-byte digests
```

An empty proof means the leaf is the root.

```bash
cleos push action smartastake1 claimdrop '{
  "account":"YOURACCOUNT",
  "drop_id":42,
  "amount":1000000,
  "proof":["CHECKSUM256_SIBLING_0"]
}' -p YOURACCOUNT@active
```

```js
{ account: 'smartastake1', name: 'claimdrop',
  authorization: [{ actor: 'YOURACCOUNT', permission: 'active' }],
  data: { account: 'YOURACCOUNT', drop_id: 42, amount: '1000000', proof: ['…'] } }
```

### Log actions

Self-auth only, for Hyperion: `logstake`, `logunstake`, `logclaim`, `logreward`, `logpropose`, `logvote`, `logqueue`, `logexec`, `logveto`, `logairdrop`, `logclaimdrop`.

### `settime` (test builds only)

Present only when compiled with `-DSMARTASTAKE_TEST_CLOCK`. Auth: the contract itself. Production ABI must not contain `settime`, `withdrawadmin`, or `rescue`. Do not deploy the test wasm.

## Notify handlers and memo formats

`[[eosio::on_notify("*::transfer")]] ontransfer`:

1. Ignore unless `to` is this contract and `from` is not this contract.
2. First receiver must be `cfg.token_contract`, symbol must be `4,SAT`, amount > 0. Otherwise: `wrong token`, `wrong symbol`, `amount`.
3. Classify the transfer:
   - Memo `reward`, **or** `from == cfg.trader` (whatever the memo): always reward funding. This stops a `smartatrader` payout with an empty or `stake` memo from creating a trader stake row that can never unstake or claim.
   - Memo `stake`, or an empty memo, and not from the trader: credit the sender's stake.
   - Any other memo: reward only if `from` is in `funders`; otherwise the transfer reverts with `memo`. That is how a `smartavest11` claim (memo `vesting claim`) funds stakers — and why `setfunder` must run first, or the vest claim reverts forever.

| Memo | From | Result |
|---|---|---|
| `stake` or empty | anyone but the trader | Stake principal. |
| `reward` | anyone | Reward pot. |
| any memo | `smartatrader` | Reward pot. |
| `vesting claim` or other | a funder (`smartavest11`, `smartafees11`, …) | Reward pot. |
| anything else | not a funder | Reverts (`memo`). |

Outbound memos this contract sends: `withdraw`, `claim`, `airdrop`.

## Parameters and bounds

Approved init values are in [README.md](README.md#approved-governance-numbers). Contract limits:

| Parameter | Bounds |
|---|---|
| Cooldown at init | 1 second .. 365 days; `min <= cooldown <= max` |
| Approved cooldown range | 3–30 days (`259200`..`2592000`), start 7 days |
| Proposal threshold | `> 0`; approved 10,000 SAT |
| Quorum / approval | 1..10000 bps; approved 1000 / 5000 |
| Voting period | 3600 s .. 30 days; approved 3 days; airdrops at least 7 days |
| Timelock | 60 s .. 30 days; approved 2 days; airdrops at least 48 hours |
| `sat_lp_fee_share_bps` | 1000..8000; approved 5000 |
| `buyback_share_bps` | 0..5000; approved 0 |
| `airdrop_alloc_bps` | 250..500 if a fund exists; approved 500 |
| Novote list at init | at most 8, must include the fund |
| Airdrop size | ≤ 25% of the fund's staked balance after compound |
| Airdrop gap | 30 days between executes |
| Execute grace | 14 days after `eta`, then expire |
| `reward_rate` | ≤ 1,000,000; stored only |
| Reward period | ≥ 1 day if rate > 0; stored only |
| Merkle proof | ≤ 32 siblings |
| Memo | ≤ 256 bytes on airdrop proposals |

## Fees and splits

This contract does not take a fee on stake, unstake, or claim. Rewards come from SAT transferred into the pot (vest stream claims, fee forwards, trader payouts, and any other funder or `reward` memo).

Bot knobs that strategy code should read from this `config` (they are not in the live `smartatrader` ABI):

- `sat_lp_fee_share_bps` — approved starting 50% of the relevant fee share toward SAT LP.
- `buyback_share_bps` — approved starting 0%.

Trader revenue splits (`satlp` / `leeflp` / `ops` / `rewards` / `reserve`) stay on `smartatrader` and change only through a passed `setsplit` after the `setgov` patch.

## Security model and known limits

**Not audited yet.** A 2026-11-14 source review plus local tests (22 stake-suite checks, and an integration suite against the real `sat.token` and `leeftrader` wasm) found that balances and the invariant held, and that muldiv and merkle hashing were consistent. Governance and launch order still have limits. That work is not an external audit. Honest list:

- **First staker takes held rewards.** While `total_staked == 0`, incoming rewards are held, and the first staker of even 0.0001 SAT receives all of them. Vest claims are permissionless, so anyone could park rewards and then snipe them if the fund is not staked first. **Operational fix:** stake the airdrop fund in B2 before any reward arrives. After that, `total_staked` cannot return to zero (airdrops take at most 25% of the fund).
- **`setfunder` before retiring admin.** A rewards-stream claim uses memo `vesting claim`. Without `setfunder` for `smartavest11` (and `smartafees11` for fee forwards), those transfers revert. If the stake admin is retired first, the 23.75M stream can never be claimed.
- **Trader payouts are always reward.** SAT from `cfg.trader` cannot create a stake row (review fix in PR #5). Prefer memo `reward` anyway, and list `smartastake1` in the trader `dests` table.
- **Queued proposals expire.** A queued proposal that is not executed within 14 days after `eta` expires (status 6) and does not apply. This was added in PR #5 so a stale vote cannot override a newer one later.
- **Veto of an airdrop does not clear `open_airdrop_id`.** A defeated vote or an expired execute does. If the guardian vetoes a queued airdrop, a new airdrop propose can fail with `airdrop open`. Prefer expiry over veto for airdrops unless you have another way to clear the slot.
- **`setsplit` / `setlimits` need the trader patch.** Against today's `smartatrader`, execute fails (`missing authority of smartaadmin1`) until the `binary_extension<name> gov` patch and `setgov` (B5). A plain `name gov` bricks the live trader config singleton — never use that form.
- **Just-in-time staking.** Reward lumps are distributed at once. A staker who joins immediately before a lump takes a pro-rata share and can leave after the cooldown (3–30 days, start 7). `reward_rate` / `reward_period_secs` are unused. Prefer many small reward transfers. Streaming is a v2 item.
- **Vote weight is live stake at cast, not a snapshot** (quorum uses `snapshot_total`). The vote lock prevents unstaking that weight until the vote ends. Proposer stake is not locked. Staking more mid-vote does not raise an already-cast weight.
- **Stake rows are paid by the contract and never erased.** Many tiny stakes can consume RAM. Unclaimed airdrop liability has no expiry or sweep.
- **Guardian veto is a centralization tradeoff**, documented above. It is not full decentralization.

## User guide

Once the contract is live (it is not, today):

**Stake.** Transfer SAT to `smartastake1` with memo `stake` (or an empty memo):

```bash
cleos push action sattoken1111 transfer '{"from":"YOURACCOUNT","to":"smartastake1","quantity":"100.0000 SAT","memo":"stake"}' -p YOURACCOUNT@active
```

```js
{ account: 'sattoken1111', name: 'transfer',
  authorization: [{ actor: 'YOURACCOUNT', permission: 'active' }],
  data: { from: 'YOURACCOUNT', to: 'smartastake1', quantity: '100.0000 SAT', memo: 'stake' } }
```

**Check your position and global config:**

```bash
cleos get table smartastake1 smartastake1 stakes --lower YOURACCOUNT --upper YOURACCOUNT
cleos get table smartastake1 smartastake1 config
```

`pending_rewards` may be stale until you `claim` or `unstake` (settlement is lazy). The `claim` action settles first, then pays.

**Claim rewards** (does not change principal):

```bash
cleos push action smartastake1 claim '{"owner":"YOURACCOUNT"}' -p YOURACCOUNT@active
```

**Unstake, then withdraw after the cooldown** (7 days at launch; a new unstake resets the timer for everything already cooling). You cannot unstake while a vote lock is active.

```bash
cleos push action smartastake1 unstake '{"owner":"YOURACCOUNT","amount":500000}' -p YOURACCOUNT@active
# wait until unstake_ready
cleos push action smartastake1 withdraw '{"owner":"YOURACCOUNT"}' -p YOURACCOUNT@active
```

**Propose** if you have at least 10,000 SAT of votable (non-novote) active stake. See the `propose` examples above.

**Vote, then anyone may queue and execute:**

```bash
cleos push action smartastake1 vote '{"voter":"YOURACCOUNT","id":1,"support":true}' -p YOURACCOUNT@active
# after end_time:
cleos push action smartastake1 queue '{"id":1}' -p YOURACCOUNT@active
# after eta, and only if not vetoed or expired:
cleos push action smartastake1 execute '{"id":1}' -p YOURACCOUNT@active
```

**Airdrops:** follow [airdrop-fund.md](airdrop-fund.md). After execute, call `claimdrop` with your amount and merkle proof.

Admin, fee, and airdrop-fund accounts are novote at the approved init: they earn rewards if they stake, but they cannot propose or vote.

## Operator guide

1. B0: create `smartastake1` with `eosio.code` on `active`, buy enough RAM (B0 drafts use 921,600 bytes — stake rows are billed to the contract), set the production wasm and ABI. Never deploy the test-clock wasm. Confirm the hash `dff935d9…a2ad`.
2. B1: `init` with the approved numbers.
3. B2: stake the airdrop fund **before** any reward. Confirm `total_staked` and the `smartadrop11` row.
4. B3: `setfunder` for `smartavest11` and `smartafees11` before any vest claim or fee forward, and before retiring admin.
5. B4: rewards stream opens only after B2 and B3. Anyone may periodically `claim` that vest stream; prefer frequent small claims.
6. B5: apply the safe trader patch and `setgov` before queueing `setsplit` or `setlimits`.
7. B6: lock `smartadrop11@active` to `smartastake1@eosio.code` only.
8. List `smartastake1` in `smartatrader` `dests` before trader payouts.
9. Bots should read `sat_lp_fee_share_bps` and `buyback_share_bps` from this config.
10. Guardian: review queued proposals during the timelock, especially airdrop merkle roots. For airdrops you want to stop, prefer expiry (`execute` after the 14-day grace) over veto, because of `open_airdrop_id`.
11. Keep contract RAM topped up. Optional later freezes (no further `setfunder`, no `setcode`) are separate, partly irreversible decisions, only after B3 and the audit.

## Deploy and launch order

Follow [README.md](README.md#deploy-and-launch-order). Signing waits for an external audit. This document is not a signing approval.

Build and test locally:

```bash
cd smartastake
./build.sh
./tests/run_tests.sh
```

`smartastake/mocks/` is a local test token, a trader stand-in, and a tiny bios. It is not `sattoken1111` or `smartatrader`. Do not copy those mocks over the root contracts. The real-trader upgrade path is `integration/run.sh`.
