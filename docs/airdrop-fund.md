# Airdrop fund (`smartadrop11`)

## Purpose

`smartadrop11` is not a smart contract. It is a dedicated WAX account that holds the future-airdrop slice of SAT **as a stake** inside `smartastake1`.

The slice is **1,250,000.0000 SAT**: 5% of the 25% rewards bucket (`airdrop_alloc_bps = 500`, the top of the allowed 250–500 range). It is taken from rewards, not from a sixth whitepaper bucket. The other 23,750,000 SAT of that bucket streams to `smartastake1` over 4 years for ordinary staker rewards; see [smartavest.md](smartavest.md) and [README.md](README.md#approved-allocation-2026-11-14).

Once staked, the fund:

- Earns a pro-rata share of every reward that reaches `smartastake1` (the rewards stream, `smartafees11` forwards, and `smartatrader` payouts) and autocompounds those rewards into its stake.
- Has **no voting power** (`novote`). It cannot be removed from that list.
- **Cannot** `unstake`, `withdraw`, or `claim`. Those actions revert with `airdrop fund is proposal-only`.
- Pays future airdrops only through a passed governance proposal of kind `airdrop`, after which recipients call `claimdrop` with a merkle proof.

**Account not created.** The stake contract that enforces this is not deployed. A 2026-11-14 source review is in [AUDIT-2026-11-14.md](../AUDIT-2026-11-14.md); signing the seed-and-stake batch still waits for an external firm audit.

## Accounts and permissions

| Account | Role |
|---|---|
| `smartadrop11` | Fund holder. No wasm. Created in B0. Owner starts as `smartaadmin1@active`. After the fund has staked (B2), B6 sets `active` to `smartastake1@eosio.code` only, so a human key cannot push active actions. |
| `smartastake1` | Staking contract that holds the SAT, compounds it, and pays merkle claims from an executed airdrop. |
| `smartaadmin1` | Creates the account, issues and forwards the 1.25M seed, and is the governance guardian (can veto a queued airdrop). Listed `novote` at init, so the admin also cannot vote even if it later stakes. |
| Stakers | Propose, vote, queue, and execute airdrop proposals. Recipients claim with `claimdrop`. |

On Antelope, `owner` still satisfies `require_auth(account)`. The in-contract `novote` and proposal-only checks are what stop a remaining owner authority from voting, unstaking, or claiming the fund. Treat owner as cold break-glass, not a hot key. Funds leave the stake only through an executed airdrop proposal, which uses the code permission to debit the fund and later to transfer SAT on `claimdrop`.

Do not commit keys or a filled-in permission JSON with private material.

## How the money is sourced

1. **Principal.** B2 issues `1250000.0000 SAT` to `smartaadmin1`, transfers it to `smartadrop11`, then `smartadrop11` transfers it to `smartastake1` with memo `stake`. Reward / vest-claim transfers revert with `fund not staked` until that stake exists, so a first staker cannot capture a parked pot. After the fund is staked, `total_staked` cannot return to zero, because an airdrop can take at most 25% of the fund.
2. **Compounds.** The fund's stake earns its share of everything that funds the reward pot. `compound` is permissionless and also runs automatically on reward notifies and on airdrop propose/execute.

A rejected alternative (`B2-alt`: 625k staked plus 625k streamed to `smartadrop11`) is not used. Each vest claim would land as a plain balance that needed a human `active` key to restake, which conflicts with B6.

The contract does not mint SAT and does not skim 5% off every reward transfer. `airdrop_alloc_bps` is a recorded choice, range-checked once at `init`.

## Tables (on `smartastake1`)

There is no table on `smartadrop11`. Look at the stake contract:

| Table | Scope | What to read |
|---|---|---|
| `config` | `smartastake1` | `airdrop_account`, `airdrop_alloc_bps`, `open_airdrop_id`, `last_airdrop_exec`, `airdrop_liability`. |
| `stakes` | `smartastake1` | Row `smartadrop11`: `amount` (active fund principal), `pending_rewards`. |
| `novote` | `smartastake1` | Must include `smartadrop11`. |
| `proposals` | `smartastake1` | Kind `airdrop` rows and their status. |
| `drops` | `smartastake1` | Executed drops: merkle root, amount, claimed, snapshot_time, memo. Drop id equals the proposal id. |
| `claims` | scope = `drop_id` | One row per account that has already claimed that drop. |

```bash
cleos get table smartastake1 smartastake1 stakes --lower smartadrop11 --upper smartadrop11
cleos get table smartastake1 smartastake1 drops
cleos get table smartastake1 DROPID claims
```

## Actions (all on `smartastake1`)

The fund has no actions of its own. Relevant stake actions, in full in [smartastake.md](smartastake.md#actions):

| Action | Auth | Role for the fund |
|---|---|---|
| Transfer SAT to `smartastake1` with memo `stake` | `smartadrop11` (B2 only, while it still has an active authority that can transfer) | Creates the fund's stake row. |
| `compound(account)` | Permissionless; `account` must be the fund | Settles pending rewards into the fund's `amount`. |
| `propose(..., kind=airdrop, amount, snapshot_time, memo, merkle_root)` | A staker with at least 10,000 SAT votable stake | Opens an airdrop vote. |
| `vote` / `queue` / `execute` / `veto` | As in the staking guide | Ordinary governance. Guardian may veto while queued. |
| `claimdrop(account, drop_id, amount, proof)` | `account` | Pays that account SAT with memo `airdrop`. One claim per account per drop. |
| `unstake` / `withdraw` / `claim` as `smartadrop11` | Would need the fund's auth | Always rejected. |

### Airdrop proposal bounds (enforced at propose and again at execute)

Even when the global vote period or timelock is shorter:

- At most **25%** of the fund's staked balance after compound, per proposal (`AIRDROP_CAP_BPS = 2500`).
- At most **one executed** airdrop per **30 days** (`AIRDROP_GAP`).
- Voting period at least **7 days**.
- Timelock at least **48 hours**.
- A second airdrop cannot be proposed while one is still `open_airdrop_id`.
- Guardian veto still applies.
- `snapshot_time` must be in the past or now, not the future.
- `memo` ≤ 256 bytes; merkle root must be non-zero.
- A queued airdrop that is not executed within 14 days after `eta` expires (status 6) and frees the open slot.
- A guardian `veto` of an active or queued airdrop clears `open_airdrop_id` (a defeated `queue` or an expired `queue`/`execute` does too). See [smartastake.md](smartastake.md).

`execute` debits the fund's active stake and credits `drops` plus `airdrop_liability`. It does not send SAT to recipients yet.

### Merkle claim

```
leaf = sha256( uint64le(account.value) || int64le(amount) )
node = sha256( min(left, right) || max(left, right) )   # bytewise on the 32-byte digests
```

Proof length is at most 32 siblings. An empty proof means the leaf is the root. `claimdrop` checks the proof, rejects a second claim, and rejects an amount that would exceed the drop or the remaining liability. The transfer memo is `airdrop`.

```bash
# After a drop is executed:
cleos push action smartastake1 claimdrop '{
  "account":"YOURACCOUNT",
  "drop_id":42,
  "amount":1000000,
  "proof":["CHECKSUM256_SIBLING_0"]
}' -p YOURACCOUNT@active
```

`amount` is in SAT smallest units (4 decimals): `1000000` = `100.0000 SAT`.

```js
{ account: 'smartastake1', name: 'claimdrop',
  authorization: [{ actor: 'YOURACCOUNT', permission: 'active' }],
  data: { account: 'YOURACCOUNT', drop_id: 42, amount: '1000000', proof: ['…'] } }
```

## Notify handlers and memo formats

On the fund account itself: none. The only transfer it must make is the B2 `stake` transfer into `smartastake1`.

Outbound airdrop payments from `smartastake1` use memo `airdrop`.

## Parameters and bounds

See the table above and [README.md](README.md#approved-governance-numbers). Hard caps 25% / 30 days / 7-day vote / 48-hour timelock cannot be relaxed by a parameter vote.

## Fees and splits

The fund pays no fee to be staked. It receives a pro-rata share of the staking reward pot like any other stake, then compounds that share back into the fund so later airdrops can be larger than 1.25M in total over time, while each single airdrop stays ≤ 25% of the then-current fund stake.

## Security model and known limits

- **Not a firm audit.** The fund's safety is the stake contract's safety. See [AUDIT-2026-11-14.md](../AUDIT-2026-11-14.md).
- Unclaimed airdrop amounts sit in `airdrop_liability` until `claimdrop`. There is no expiry or sweep. Recipients who lose their proof or miss the drop leave SAT in the contract indefinitely.
- A stale queued airdrop used to block the airdrop slot forever unless the guardian vetoed it. After PR #5 it expires 14 days after `eta`.
- Just-in-time staking can still let someone pick up a share of a reward lump after a short cooldown; that is a staking-rewards issue, not an airdrop-cap bypass.
- Building a dishonest merkle root would require a passed vote plus a guardian that does not veto. Recipients should verify published trees against the on-chain root before claiming, and the guardian should check the tree during the 48-hour timelock.
- After B6, `smartadrop11@active` cannot transfer leftover SAT that somehow sits on the account (it should not: the seed is fully staked). Do not send stray tokens to `smartadrop11`.

## User guide

1. Watch `proposals` for kind `airdrop`, or follow a published announcement that includes the merkle root, snapshot time, and a tree file.
2. Vote if you are a staker and you agree or disagree (see [smartastake.md](smartastake.md#user-guide)).
3. After `execute`, find your row in the published tree, then call `claimdrop` with your amount and proof. You pay the RAM for your `claims` row.
4. You cannot unstake or vote with the fund, and you cannot claim the fund's staking rewards to a personal wallet.

## Operator guide

1. B0: create `smartadrop11` (no code). Keep `smartaadmin1` on owner/active until B2 and B6.
2. B1: `init` lists `smartadrop11` in `novote` and as `airdrop_account`. If you set an airdrop account, it **must** be in the novote list or `init` fails (`airdrop must be novote`).
3. B2: issue, transfer, stake, **before any reward**. Confirm the `stakes` row and `total_staked >= 12500000000` units (1,250,000.0000 SAT).
4. B3–B5 can then proceed (funders, vest streams, trader patch).
5. B6: set `smartadrop11@active` to `smartastake1@eosio.code` only.
6. For each airdrop: pick a past snapshot, build the canonical merkle tree (sorted pair hashing as above), publish the tree, propose, wait out the 7-day vote and 48-hour timelock, review as guardian, then execute (or veto). Keep at least 30 days between executes. Prefer amounts well under the 25% cap so a small price-of-time compound movement does not fail the execute-time cap check.
7. Call `compound('smartadrop11')` occasionally if you want the public number to include pending rewards before a proposal (propose and execute already compound).

B2 shape (unsigned, do not sign before the audit):

```bash
cleos push action sattoken1111 issue '{"to":"smartaadmin1","quantity":"1250000.0000 SAT","memo":"SAT lock: airdrop fund seed (5% of rewards)"}' -p smartaadmin1@active
cleos push action sattoken1111 transfer '{"from":"smartaadmin1","to":"smartadrop11","quantity":"1250000.0000 SAT","memo":"airdrop fund seed"}' -p smartaadmin1@active
cleos push action sattoken1111 transfer '{"from":"smartadrop11","to":"smartastake1","quantity":"1250000.0000 SAT","memo":"stake"}' -p smartadrop11@active
```

## Deploy and launch order

See [README.md](README.md#deploy-and-launch-order). The fund exists so that step B2 can close the "first staker takes the pot" gap before B4 starts dripping rewards.
