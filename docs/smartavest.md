# smartavest (`smartavest11`)

## Purpose

Smallest-safe SAT vesting vault. It holds SAT only and releases it on a fixed cliff plus linear schedule to a beneficiary that is set at open and can never change.

**Not audited. Not deployed.** Do not create `smartavest11` or send mainnet SAT to it until an external audit is done and the owner has approved the lock batches. Source: `smartavest/smartavest/smartavest.cpp`. Planned account: `smartavest11`. Planned production wasm sha256 (CDT 4.1.1, `-O3`, no test clock): `b6166eae6560e8bc43ac2bafed01ec58e7476fa30e297ddbe4c85074a54ae532`. Rebuild to verify. The production ABI is `abi/smartavest.abi` (actions: `init`, `openstream`, `claim` only — no `setnow`).

There is no `withdraw`, `revoke`, `setbeneficiary`, `rescue`, or `pause`. `claim` is permissionless and always pays the stream beneficiary, never the caller.

Approved streams (opened in this order in batch B4):

| Stream `category` | Amount | Beneficiary | Cliff | Linear duration |
|---|---|---|---|---|
| `lpreserve` | 25,000,000 SAT | `smartaadmin1` | 0 | 4 years (`126230400` s = `1461 * 86400`) |
| `rewards` | 23,750,000 SAT | `smartastake1` | 0 | 4 years (`126230400` s) |
| `treasury` | 15,000,000 SAT | `smartaadmin1` | 0 | 4 years (`126230400` s) |
| `team` | 15,000,000 SAT | `smartaadmin1` | 12 months (`31536000` s = `365 * 86400`) | 36 months (`94608000` s = `1095 * 86400`) |

`1461` days is `4 * 365.25`, not `4 * 365`. The contract does not special-case these numbers; any positive `duration_secs` that does not overflow `uint32` time is valid. Sizes are not hard-coded: the vault locks whatever SAT is actually transferred into a stream.

The 1,250,000 SAT airdrop fund is **not** a vest stream. It is staked directly; see [airdrop-fund.md](airdrop-fund.md).

## Accounts and permissions

| Account | Role |
|---|---|
| `smartavest11` | Contract. `init` requires its own authority. `active` must include `smartavest11@eosio.code` so `claim` can send inline transfers. The contract pays RAM for stream rows. |
| `smartaadmin1` | Vest admin (set at `init`). Only the admin may `openstream` or open-and-fund with the `open:…` memo. Also the beneficiary of `lpreserve`, `treasury`, and `team`. |
| `sattoken1111` | The only accepted token contract, symbol `4,SAT`. |
| `smartastake1` | Beneficiary of the `rewards` stream. Must already be a staking **funder** before anyone claims this stream, otherwise the vesting claim memo is rejected by the stake contract. |
| Caller of `claim` | Anyone. Payment still goes to the beneficiary. |

Nulling the contract keys does **not** by itself stop a still-live admin from opening another stream. There is still no action that can redirect or withdraw an existing stream. See [Operator guide](#operator-guide).

## Tables

### `config` (singleton, scope = `smartavest11`)

| Field | Type | Meaning |
|---|---|---|
| `admin` | `name` | `smartaadmin1`. |
| `token_contract` | `name` | `sattoken1111`. |
| `token_symbol` | `symbol` | `4,SAT`. |
| `inited` | `bool` | Set true by `init`. |

```bash
cleos get table smartavest11 smartavest11 config
```

### `streams` (scope = `smartavest11`)

| Field | Type | Meaning |
|---|---|---|
| `id` | `uint64` | Primary key, assigned with `available_primary_key()` (0, 1, 2, …). |
| `beneficiary` | `name` | Fixed payout target. |
| `category` | `name` | Label only (`team`, `rewards`, `treasury`, `lpreserve`, …). Not an authority. |
| `total` | `int64` | SAT smallest units locked in this stream (can grow via `fund:<id>`). |
| `claimed` | `int64` | SAT already paid out. |
| `start` | `uint32` | Unix start. |
| `cliff_secs` | `uint32` | Seconds after `start` before any SAT vests. |
| `duration_secs` | `uint32` | Linear period after the cliff. |
| `created_at` | `uint32` | Time the row was created. |

Secondary index `byben` on `beneficiary`.

```bash
cleos get table smartavest11 smartavest11 streams
# One stream:
cleos get table smartavest11 smartavest11 streams --lower 0 --upper 0 --limit 1
```

```js
const { rows } = await fetch(rpc + '/v1/chain/get_table_rows', {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({
    json: true, code: 'smartavest11', scope: 'smartavest11', table: 'streams', limit: 20
  })
}).then((r) => r.json());
```

Production wasm has no `testclock` table. That singleton exists only in the `-DSAT_VEST_TEST_CLOCK` test build.

## Vesting math

```
cliff_end = start + cliff_secs
end       = cliff_end + duration_secs

if now < cliff_end:  vested = 0
else if now >= end:  vested = total
else:                vested = floor(total * (now - cliff_end) / duration_secs)
claimable = vested - claimed
```

`total * elapsed` is computed in `__int128`, so a full-supply stream cannot overflow `int64`. Integer-division dust stays in the vault until `now >= end`, when `vested` snaps to `total` and a later claim pays the remainder. The sum of claims cannot exceed `total`.

`now` is `eosio::current_time_point()` in the production build.

A `fund:<id>` top-up mid-stream increases `total` immediately, so the newly added SAT vests pro-rata for time already elapsed (half of a top-up is immediately vested if the stream is 50% through). Fund streams fully at open when that is the intent.

## Actions

### `init(admin, token_contract)`

- **Auth:** `smartavest11` (the contract account). In the launch batches this is satisfied because `smartaadmin1@active` is on `smartavest11@active` at create time, but the action itself checks `get_self()`.
- **Params:** `admin` and `token_contract` must be existing, non-zero accounts.
- **Effects:** Writes `config` once. Symbol is fixed at `4,SAT`. Cannot be called again.
- **Errors:** `already initialized`, `invalid admin`, `invalid token contract`, `admin does not exist`, `token contract does not exist`. Later reads fail with `not initialized` or `wrong symbol` if config is missing or not SAT.

```bash
cleos push action smartavest11 init '{"admin":"smartaadmin1","token_contract":"sattoken1111"}' -p smartavest11@active
```

```js
{ account: 'smartavest11', name: 'init',
  authorization: [{ actor: 'smartavest11', permission: 'active' }],
  data: { admin: 'smartaadmin1', token_contract: 'sattoken1111' } }
```

### `openstream(beneficiary, category, start, cliff_secs, duration_secs)`

- **Auth:** `config.admin` (`smartaadmin1`).
- **Params:** `beneficiary` and `category` must be valid names; beneficiary must exist and must not be the contract itself. `start = 0` means "now". `duration_secs > 0`. `start + cliff_secs + duration_secs` must fit in `uint32`.
- **Effects:** Creates a stream with `total = 0`, `claimed = 0`. Fund it afterwards with `fund:<id>`.
- **Errors:** `not initialized`, plus `invalid beneficiary`, `invalid category`, `beneficiary cannot be contract`, `beneficiary does not exist`, `negative total`, `start must be non-zero` (after 0 is replaced with now), `duration must be positive`, `schedule overflows uint32`.

```bash
# Empty team stream, then fund it separately. start=0 means now.
cleos push action smartavest11 openstream '{"beneficiary":"smartaadmin1","category":"team","start":0,"cliff_secs":31536000,"duration_secs":94608000}' -p smartaadmin1@active
```

The approved launch uses the transfer memo form below (open and fund in one transaction) rather than an empty `openstream`.

### `claim(id)`

- **Auth:** none (permissionless).
- **Params:** existing stream `id`.
- **Effects:** Computes claimable SAT, adds it to `claimed` **before** the inline transfer (so a re-entrant claim from the beneficiary's transfer notification cannot be paid twice), then transfers SAT from `smartavest11` to the beneficiary with memo `vesting claim`.
- **Errors:** `stream not found`, `still in cliff` (nothing vested yet), `nothing to claim` (after the cliff, but no new SAT has vested), `claimed exceeds vested`, `claimed overflow`, `claim exceeds total`. A beneficiary contract that rejects the SAT transfer reverts the whole transaction (it can brick its own claims; it cannot drain the vault).

```bash
cleos push action smartavest11 claim '{"id":1}' -p ANYACCOUNT@active
```

```js
{ account: 'smartavest11', name: 'claim',
  authorization: [{ actor: 'ANYACCOUNT', permission: 'active' }],
  data: { id: 1 } }
```

To see how much is claimable, read the `streams` row and apply the math above with current UTC seconds.

## Notify handlers and memo formats

`[[eosio::on_notify("*::transfer")]]`. Only inbound SAT from `config.token_contract` is accepted. Outbound claim transfers, transfers not to this contract, and the contract sending to itself are ignored. Anything else (wrong contract, wrong symbol or precision, zero amount, empty or unknown memo, WAX, other tokens) **reverts**, so SAT cannot sit on the account outside a stream. Buy RAM for `smartavest11` with `buyram` / `buyrambytes` from another account; do not transfer WAX in with a memo.

| Memo | Who | Effect |
|---|---|---|
| `open:<beneficiary>:<category>:<cliff_secs>:<duration_secs>` | Admin only (`from == admin`) | Creates a stream and sets `total` to the transfer amount. |
| `open:<beneficiary>:<category>:<cliff_secs>:<duration_secs>:<start>` | Admin only | Same, with an explicit unix `start`. `0` means now. |
| `fund:<stream_id>` | Anyone | Adds the transfer to that stream's `total`. Allowed before and after claims. `total` never shrinks and must stay `>= claimed`. |
| anything else | — | `unknown memo` (or `only admin may open`, `stream not found`, `total overflow`, `total below claimed`, `bad integer`, `bad name`, `integer overflow`). |

Names in memos must be canonical Antelope names (a–z, 1–5, dots, at most 13 characters, and `name(s).to_string() == s`). Integers are unsigned decimal, no signs or spaces.

Approved B4 memos (after issuing 78,750,000 SAT to `smartaadmin1`):

```bash
# Stream 0 — locked LP reserve, 4y, beneficiary smartaadmin1
cleos push action sattoken1111 transfer '{"from":"smartaadmin1","to":"smartavest11","quantity":"25000000.0000 SAT","memo":"open:smartaadmin1:lpreserve:0:126230400"}' -p smartaadmin1@active

# Stream 1 — staking rewards, 4y, beneficiary smartastake1
cleos push action sattoken1111 transfer '{"from":"smartaadmin1","to":"smartavest11","quantity":"23750000.0000 SAT","memo":"open:smartastake1:rewards:0:126230400"}' -p smartaadmin1@active

# Stream 2 — treasury, 4y, beneficiary smartaadmin1
cleos push action sattoken1111 transfer '{"from":"smartaadmin1","to":"smartavest11","quantity":"15000000.0000 SAT","memo":"open:smartaadmin1:treasury:0:126230400"}' -p smartaadmin1@active

# Stream 3 — team, 12m cliff + 36m linear, beneficiary smartaadmin1
cleos push action sattoken1111 transfer '{"from":"smartaadmin1","to":"smartavest11","quantity":"15000000.0000 SAT","memo":"open:smartaadmin1:team:31536000:94608000"}' -p smartaadmin1@active
```

```js
{
  account: 'sattoken1111', name: 'transfer',
  authorization: [{ actor: 'smartaadmin1', permission: 'active' }],
  data: {
    from: 'smartaadmin1', to: 'smartavest11',
    quantity: '15000000.0000 SAT',
    memo: 'open:smartaadmin1:team:31536000:94608000'
  }
}
```

Ids above assume these four are the first streams opened. Always read the `streams` table after the fact and use the actual `id`.

## Parameters and bounds

- Token: only `4,SAT` from the configured token contract.
- `start != 0` after defaulting; `duration_secs > 0`; schedule sum fits in `uint32` (year 2106 ceiling).
- `total` and `claimed` are `int64` SAT units; top-ups must not overflow `int64`.
- Claim memo sent to the beneficiary is exactly `vesting claim`.

## Fees and splits

None. The vault does not take a fee. What happens after a claim is up to the beneficiary: `smartastake1` treats `vesting claim` as reward funding once `smartavest11` is a funder; `smartaadmin1` receives team, treasury, and LP-reserve releases to use according to the lock plan (LP reserve is meant for further Alcor `lockpos` adds).

## Security model and known limits

**Not audited yet.** A 2026-11-14 source review found no fund-loss bug in this contract: math, auth, first-receiver checks, re-entrancy ordering, and books all checked out, and the local suite was 83 passed / 0 failed. That is not an external audit. Known limits, stated honestly:

- A beneficiary contract that rejects SAT bricks that stream. The beneficiary cannot be changed and there is no withdraw. Only use beneficiaries that accept SAT. For the rewards stream, that means `smartastake1` must already have `setfunder` for `smartavest11` (see launch order). If the stake admin is retired before `setfunder`, the 23.75M rewards stream can never be claimed.
- Mid-stream `fund:<id>` vests pro-rata immediately. Fund fully at open unless that effect is intended.
- Any non-SAT transfer is rejected, including WAX. Buy RAM from another account.
- `openstream` / `open:` remain available for as long as the admin account can still authorize. Code freeze (no `setcode`) is a separate step from retiring the admin (no new streams). Anyone can still `fund:<id>` an existing stream after that, which only increases what the fixed beneficiary will receive.
- Production wasm must be the build **without** `setnow`. The test clock is only for `smartavest/tests/run_tests.sh`.

## User guide — check a vesting stream

Streams are public tables. Anyone can read them; anyone can `claim` (the SAT still goes to the beneficiary).

```bash
# List streams
cleos -u https://wax.greymass.com get table smartavest11 smartavest11 streams

# Claim available SAT for stream id 3 (example: team) — pays smartaadmin1
cleos push action smartavest11 claim '{"id":3}' -p YOURACCOUNT@active
```

Worked check (do this off-chain; the contract does the same math):

1. Read `start`, `cliff_secs`, `duration_secs`, `total`, `claimed`.
2. `cliff_end = start + cliff_secs`. If `now < cliff_end`, claimable is 0 (`still in cliff`).
3. If `now >= cliff_end + duration_secs`, vested = `total`.
4. Otherwise vested = `floor(total * (now - cliff_end) / duration_secs)`.
5. Claimable = `vested - claimed`. If 0, `claim` fails with `nothing to claim`.

For the team stream, nothing vests for the first 12 months. After that it unlocks linearly over 36 months. Treasury, rewards, and LP reserve start vesting immediately over 4 years.

Users do **not** claim the rewards stream to their own wallet. That stream pays `smartastake1`, which credits the staking reward pot. Stakers then use `smartastake1::claim` (see [smartastake.md](smartastake.md#user-guide)).

## Operator guide

1. Create the account with `smartavest11@eosio.code` on `active`, buy RAM (B0 drafts use 266,240 bytes), and set the production wasm and ABI. Never deploy `smartavest_test.wasm`.
2. `init` once (B1).
3. `setfunder` on `smartastake1` for `smartavest11` (B3) **before** any rewards-stream claim and **before** retiring the stake admin.
4. Open and fund the four streams in the approved order (B4), fully funded at open.
5. After streams exist, claims are permissionless. No keeper is required, but a periodic claim of the rewards stream (by anyone) is what actually moves SAT into the staking pot. Prefer frequent small claims over rare large ones, because `smartastake` distributes a lump in one shot (see staking known limits).
6. LP-reserve claims arrive at `smartaadmin1`. Pair released SAT with WAX or LEEF and add locked Alcor LP on approved pools only, simulate first, and keep price impact low. That is a trading action, not a vest action.
7. Optional later freeze: retire the vest admin so no new streams can be opened. Existing streams keep working. Do this only after B4, and only as a deliberate, partly irreversible decision.

Required `eosio.code` on active (shape only; do not commit keys):

```bash
cleos set account permission smartavest11 active \
  '{"threshold":1,"keys":[],"accounts":[{"permission":{"actor":"smartavest11","permission":"eosio.code"},"weight":1}]}' \
  owner -p smartavest11@owner
```

At launch, `smartaadmin1@active` remains on this authority so init and setup can run; the snippet above is the later, tighter form.

## Deploy and launch order

Follow [README.md](README.md#deploy-and-launch-order). For this contract specifically: B0 (account + code) → B1 (`init`) → B3 (`setfunder` on the stake side) → B4 (open the four streams) → optional admin/code freeze. Signing waits for the external audit.

Build and test locally:

```bash
cd smartavest
./build.sh
./tests/run_tests.sh   # test wasm + setnow, throwaway local chain
```

`smartavest/sat.token/` is a local test token only. It is not `sattoken1111`.
