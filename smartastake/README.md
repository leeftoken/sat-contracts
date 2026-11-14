# smartastake

Staking and bounded governance for **SAT** on WAX (Antelope).

Intended mainnet account: `smartastake1`. Contract name: `smartastake`. Token: `SAT` with 4 decimals, max `100000000.0000 SAT`, contract `sattoken1111`. Local tests point at a mock `sat.token`.

**Not a firm audit.** Source review: [AUDIT-2026-11-14.md](../AUDIT-2026-11-14.md). This tree is for local build and test only. Do not deploy it to mainnet. Do not create mainnet accounts. Do not sign mainnet transactions.

There is no admin action that withdraws user stake, cooling principal, or the reward pot. There is no `rescue` and no sweep of undistributed rewards.

## Stake, unstake, claim

Users stake only `SAT` from the configured token contract. The notify accepts memo `stake` or an empty memo. Memo `reward` always funds the reward pot. Any other memo is a reward only if `from` is on the funder allowlist (`setfunder`, admin auth); otherwise it is rejected. That lets a `smartavest` claim (memo `vesting claim`) fund stakers without mixing the transfer into principal, but only after `setfunder` adds the vest contract. Until then that claim reverts. SAT sent by the configured `trader` is always reward funding, whatever the memo, because the trader has no action that could unstake, claim, or vote a stake row.

`unstake` moves active stake into a cooling balance. A new request **resets the timer for the whole cooling balance**, not just the new amount. `withdraw` pays that cooling balance after `unstake_ready` and never touches the reward pot. `claim` pays only accrued rewards from the reward pot and does not change principal.

Stake rows created inside the token notify are billed to the contract. Antelope will not charge RAM to another account during a notify. Proposals are billed to the proposer. Ballots and merkle claims are billed to the caller.

## Reward-per-token maths

Synthetix-style, lazy settlement. Scale is `1e18`.

```text
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

Division truncates toward zero. Dust stays in the reward pot. The first stake after a zero-stake funding period receives the held amount (`flush` runs after that stake is credited, so `paid` is the pre-flush accumulator). Settlement runs on claim, unstake, adding to an existing stake, and airdrop compound. Stored `pending_rewards` stays stale until then. That is intentional: funding does not walk every staker.

`reward_rate` and `reward_period_secs` are stored only. v1 does not drip. Distribution is the instant update above. If a proposal sets a rate above 0, the period must be at least 1 day. The stored rate is capped at `1000000` (100.0000 SAT per second as a unit cap, not an emission).

The airdrop fund is compounded immediately inside a reward notify, and anyone may call `compound` for that account only.

### Invariant

```text
token_balance(contract) >= total_staked + total_cooling + reward_pot + airdrop_liability
total_votable <= total_staked
reward_pending_undistributed <= reward_pot
sum(stored pending) <= reward_pot
```

`total_staked` is active stake, including novote stake. Cooling is separate. Claimed-but-unsent airdrop amounts sit in `airdrop_liability` until `claimdrop`.

## Voting

1 active-stake unit = 1 vote. Cooling does not vote. Accounts on the `novote` list earn rewards and count in `total_staked`, but they add **0** to `total_votable`, quorum, and the proposal threshold.

Weight is the voter's active stake at the moment they vote, then frozen on that ballot. A second ballot on the same proposal is rejected. Voting sets `vote_lock_until` to the later of its current value and the proposal end. Proposing also locks the proposer until that proposal's `end_time`. Unstake is rejected until that time. Staking more during a lock is allowed.

Lifecycle:

1. `propose` — proposer active stake (and not novote) is at least `proposal_threshold`. Snapshot is `total_votable` at that moment.
2. `vote` until `end`.
3. `queue` after `end`. Pass requires both:
   - quorum against the snapshot: `(for + against) * 10000 >= snapshot_total * quorum_bps`
   - approval of votes cast: `for * 10000 >= (for + against) * approval_bps`
   Otherwise the proposal is defeated.
4. On pass, `eta = end_time + applied_timelock`. `execute` is permissionless after `eta`.
5. The guardian may `veto` while status is active or queued, before execute. An airdrop veto clears `open_airdrop_id`.

Status values: `1` active, `2` defeated, `3` queued, `4` executed, `5` vetoed.

### Queued proposals expire

A proposal must be queued and executed within 14 days after its `eta` (`EXEC_GRACE`), where `eta = end_time + applied_timelock`. A later `queue` or `execute` marks it expired (status `6`), frees the open airdrop slot if it was an airdrop, and changes nothing else. A stale `setsplit`, `setlimits`, or `setcooldown` therefore cannot override a newer decision months later, including a passed vote that nobody queued.

## Guardian veto is a v1 centralization tradeoff

v1 keeps a guardian that can kill a queued proposal before it executes. That reduces the risk of a low-liquidity governance attack while the votable stake is small. It is **not** full decentralization. The roadmap is to raise quorum, then null the guardian or replace the veto with a multisig-only emergency pause. Until that ships, a queued change is not final.

SAT contracts are **not audited yet**.

## Whitelisted kinds and hard bounds

Anything else is rejected at `propose` (`kind not allowed`). Bounds are checked again at `execute`.

| Kind | Effect | Bounds |
|---|---|---|
| `setsplit` | Inline `trader::setsplit` in order `satlp, leeflp, ops, rewards, reserve` | Sum 10000. Each bucket 0..5000. `satlp >= 1000`. `ops <= 4000`. `rewards >= 500` |
| `setlimits` | Inline `trader::setlimits(max, daily, current lock_secs)` | `max_in` 50..500, `daily` 100..2000, `max_in <= daily`. Lock length is read and passed through. Governance cannot change it |
| `setcooldown` | Local `cooldown_secs` | Inside `[min_cooldown, max_cooldown]` set at init (init itself requires 1 second .. 365 days, and the chosen cooldown inside that range) |
| `setsatlps` | Local bot knob `sat_lp_fee_share_bps` | 1000..8000 |
| `setbuyback` | Local bot knob `buyback_share_bps` | 0..5000 |
| `setrparams` | Local stored rate and period only | Rate `<= 1000000`. Period `>= 86400` if rate `> 0` |
| `delnovote` | Remove one novote account and add its active stake to `total_votable` | Cannot remove the airdrop fund |
| `airdrop` | See below | 25% of fund stake, 30-day gap, 7-day vote, 48h timelock |

Not votable in v1: `setbot`, `setfeercpt`, `setpool`, `settoken`, `setswap`, `setdest`, `setplan`, `setcode`, mint, and the Alcor lock length.

Bot strategy should read `sat_lp_fee_share_bps` and `buyback_share_bps` from this contract's config. Those knobs are not in the live `smartatrader` ABI.

## Execution model A

`execute` sends an inline action as `smartastake@eosio.code` to `setsplit` or `setlimits` on the trader account. Today those actions are admin-only, so the trader needs the patch in [docs/TRADER_PATCH.md](docs/TRADER_PATCH.md): a `gov` account, set once by trader admin to `smartastake1`, authorized **only** on those two actions.

The stake contract reads the live trader config **prefix** (through `lock_secs`) and ignores a trailing `gov` field. `setlimits` passes that `lock_secs` unchanged.

Local tests use `trader/` which already includes the patch. Direct `setsplit` from a random account fails. Inline execute from the stake contract succeeds.

## Airdrop fund

Genesis records `airdrop_alloc_bps` in **[250, 500]** (2.5%–5% of the staking-rewards allocation). The contract does not mint SAT and does not skim that percent off every reward transfer. The project seeds the fund off-contract with that slice and stakes it here. The stored bps is the recorded choice, range-checked once at `init`.

Intended mainnet account: `smartadrop11`. Local tests use `airdrop`.

The fund must be on `novote` at `init`. The list is set once. Governance may remove other entries. The fund itself cannot be removed (`airdrop fund stays novote`). Its stake counts for rewards and not for voting power.

Rewards autocompound into the fund's active stake: on each reward notify, on its own stake, on airdrop propose/execute, and via permissionless `compound`.

`unstake`, `withdraw`, and `claim` by the fund account are rejected in the contract (`airdrop fund is proposal-only`) even if a key can still authorize the action.

### Airdrop proposals

Kind `airdrop` carries `amount`, `snapshot_time` (not in the future), `memo`, and a merkle root.

Hard bounds, even when the global vote and timelock are shorter:

- at most **25%** of the fund's staked balance after compound, per proposal
- at most **one executed** airdrop per **30 days**
- voting period at least **7 days**
- timelock at least **48 hours**
- guardian veto still applies
- a second airdrop cannot be proposed while one is still open

`execute` debits the fund's active stake and credits `drops` plus `airdrop_liability`. It does not send SAT to recipients yet. Holders call `claimdrop(account, drop_id, amount, proof)`. One claim per account. A bad proof or a second claim is rejected. The transfer memo is `airdrop`.

Merkle:

```text
leaf = sha256( uint64le(account.value) || int64le(amount) )
node = sha256( min(left, right) || max(left, right) )   # bytewise on the 32-byte digests
```

Proof length is at most 32 siblings. An empty proof means the leaf is the root.

### Manual permission step (no keys in this repo)

After the fund has staked, set `smartadrop11` active to `smartastake1@eosio.code` only, so a human key cannot push active actions. Do that by hand on the account that will hold the fund. Do not commit keys.

On Antelope, `owner` still satisfies `require_auth(account)`. The in-contract novote and proposal-only checks are what stop a remaining owner key from voting, unstaking, or claiming. Treat owner as cold break-glass, not a hot key. Active with no key cannot sign at all. Funds move out of stake only through an executed airdrop proposal, which uses the code permission.

## Logs

Self-auth only, for Hyperion: `logstake`, `logunstake`, `logclaim`, `logreward`, `logpropose`, `logvote`, `logqueue`, `logexec`, `logveto`, `logairdrop`, `logclaimdrop`.

## Launch order (review 2026-11-14)

These are operational requirements. Reward transfers are now rejected until the fund is staked; `setfunder` and the trader patch are still operator-enforced.

1. Stake the airdrop fund (`novote`, cannot unstake) **before** any reward reaches the contract. If the fund account is configured, reward transfers revert with `fund not staked` until it has a positive stake, so a 0.0001 SAT sniper cannot capture parked vest claims. Once the fund is staked, `total_staked` can no longer reach zero, because airdrops take at most 25% of the fund.
2. `setfunder` the vest contract (`smartavest11`) and `smartafees11` **before** retiring the stake admin key. Otherwise vest claims and fee forwards to this contract revert forever.
3. Apply the `smartatrader` patch in [docs/TRADER_PATCH.md](docs/TRADER_PATCH.md) with `binary_extension`, then `setgov`, **before** governance queues `setsplit` or `setlimits`.
4. Reward lumps are distributed at once, so a staker who joins just before a lump gets a pro-rata share after only the cooldown. Prefer many small reward transfers over rare large ones. `reward_rate` and `reward_period_secs` are stored but not used for streaming yet.

## Build and local test

In [leeftoken/sat-contracts](https://github.com/leeftoken/sat-contracts) this tree is the `smartastake/` directory. `mocks/` is a local test token, a trader stand-in, and a tiny bios for RAM limits. It is not `sattoken1111` and not `smartatrader`. Do not copy those mocks over `sat.token/` or `leeftrader/`.

CDT **4.1.1**. Spring nodeos/cleos about **1.2.2**. `CDT_BIN` is prepended to `PATH` when set, same as the repo root `build.sh`.

```bash
cd smartastake
./build.sh
./tests/run_tests.sh
```

`./build.sh` writes production `build/smartastake.wasm` and `build/smartastake.abi` **without** `settime`. The test clock is `-DSMARTASTAKE_TEST_CLOCK` into `build/test/` only, and the script refuses a production ABI that contains `settime`, `withdrawadmin`, or `rescue`.

Production wasm sha256 (CDT 4.1.1, `-O3`, no test clock), 2026-11-14 audit: `557f65faa57a6cdfa500f3ae218fd45db6009314ba399ae55e16e7a90a9c3d79`. Checked-in ABIs are `../abi/smartastake.abi` and `../abi/smartastake_test.abi`. Wasm is not committed; rebuild to verify the hash.

`tests/run_tests.sh` starts a throwaway local chain, runs `tests/drive.py`, and writes `tests/last-run.log`. A green run ends with `ALL TESTS PASSED`.

Covered locally: stake and empty memo, cooldown reset, early withdraw, wrong token and symbol, rewards rejected until the fund is staked, tiny staker cannot take fund-era rewards, claim does not touch principal, pro-rata splits with the fund, late staker does not steal prior rewards, unstake does not steal the pot, empty row erase, threshold, novote rewards with zero votes, 25% cap, quorum failure, double vote, vote lock, proposer lock, out-of-bounds split/limits/cooldown/kind/rate, timelock, model A execute that does not change `lock_secs`, unauthorized trader call, guardian veto of active and queued airdrops clearing the slot, merkle claim once, bad proof, double claim, 7-day vote, 48h airdrop timelock, 30-day airdrop gap, unqueued proposal expiry, and the balance invariant.
