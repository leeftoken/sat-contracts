> Lives in [leeftoken/sat-contracts](https://github.com/leeftoken/sat-contracts) under `smartavest/`.
> **Not audited. Not deployed.** `sat.token/` in this directory is a local test token only.
> The deployed SAT token is the repo-root `sat.token/` (`sattoken1111`). Do not mix them.

# smartavest — SAT vesting vault

**Not audited yet.** This is a smallest-safe vesting contract for SAT on WAX (Antelope). Do not deploy it to mainnet, do not create the mainnet account, and do not send real SAT to it until it has been reviewed by someone who does this for a living. Nothing in this repo was broadcast to mainnet.

The contract holds **SAT only** and releases it on a fixed cliff + linear schedule. It cannot pay the caller, change the beneficiary, pause, revoke, or send tokens to an admin.

## What it is for

| Stream | Intended size | Cliff | Linear release after the cliff |
|---|---|---|---|
| team | 15,000,000 SAT | 365 days | 1095 days |
| rewards | 25,000,000 SAT | none | 1461 days |

Those sizes are **not** hard-coded. The contract will lock whatever SAT an admin actually transfers into a stream. Locked LP, treasury, and the LEEF community airdrop are out of scope.

Day length used everywhere below (and accepted by the local tests):

| Name | Formula | Seconds |
|---|---|---|
| Team cliff (12 × 365-day months) | `365 * 86400` | 31536000 |
| Team linear (36 × 365-day months) | `1095 * 86400` | 94608000 |
| Rewards linear (4 × 365.25 days) | `1461 * 86400` | 126230400 |

`1461` days is `4 * 365.25`, not `4 * 365`. The contract does not special-case these numbers; any `duration_secs > 0` that does not overflow `uint32` time is valid.

## Vesting math

```
cliff_end = start + cliff_secs
end       = cliff_end + duration_secs

if now < cliff_end:  vested = 0
else if now >= end:  vested = total
else:                vested = floor(total * (now - cliff_end) / duration_secs)
claimable = vested - claimed
```

`total * elapsed` is computed in `__int128` so a full-supply stream cannot overflow `int64`. Integer-division dust stays in the vault until `now >= end`, when `vested` snaps to `total` and a later claim pays the remainder. The sum of claims cannot exceed `total`.

`now` is `eosio::current_time_point()` in the production build. A test build (`-DSAT_VEST_TEST_CLOCK`) adds `setnow` and is **not** the wasm you deploy. `build/smartavest.abi` has no `setnow` action.

## Actions

| Action | Who | What |
|---|---|---|
| `init(admin, token_contract)` | contract account, once | Records the admin and the only token contract that will be accepted. Symbol is fixed at `4,SAT`. |
| `openstream(beneficiary, category, start, cliff_secs, duration_secs)` | `admin` | Opens a stream with `total = 0`. `start = 0` means "now". |
| `claim(id)` | anyone | Pays the **beneficiary**, never the caller. Fails with `still in cliff` or `nothing to claim`. |

There is no `withdraw`, `revoke`, `setbeneficiary`, `rescue`, or `pause`.

`claim` adds to `claimed` **before** the inline transfer, so a beneficiary contract that calls `claim` again from its transfer notification cannot be paid twice. If that re-entrant call fails, the whole transaction rolls back (a hostile or buggy beneficiary can brick its own claims; it cannot drain the vault).

## Deposits

Only a transfer notification from the configured token contract is accepted. The symbol must be `4,SAT` and the amount must be positive. Any other token, symbol, precision, or memo is rejected, so SAT cannot sit on the account outside a stream.

| Memo | Who | Effect |
|---|---|---|
| `open:<beneficiary>:<category>:<cliff_secs>:<duration_secs>` | `admin` only | Creates a stream and sets `total` to the transfer amount. Optional 6th field `:start` (unix seconds). Omitted or `0` means now. |
| `fund:<stream_id>` | anyone | Adds the transfer to that stream's `total`. Top-ups are allowed before and after claims. `total` never shrinks and must stay `>= claimed`. |

`category` is a label (`team`, `rewards`, …). It is not an authority.

The contract account pays the RAM for each stream row. Buy RAM for the contract before opening streams.

`claim` sends an inline `transfer` from the contract to the beneficiary, so `active` must include `eosio.code`:

```bash
cleos set account permission smartavest11 active \
  '{"threshold":1,"keys":[{"key":"ACTIVE_PUBLIC_KEY","weight":1}],"accounts":[{"permission":{"actor":"smartavest11","permission":"eosio.code"},"weight":1}]}' \
  owner -p smartavest11@owner
```

## Permissions after funding

`openstream` is authorized by the **admin account**, not by the contract. Nulling the contract keys does **not** by itself stop that admin from opening another stream. There is still no action that can redirect or withdraw an existing stream.

| Phase | Do this | What you get |
|---|---|---|
| Setup | Contract `active` holds the deployer key **and** `eosio.code`. Admin key is a separate account that holds the SAT and calls `init` / `openstream` / the open-and-fund transfer. | Can init, open, fund, and claim. Whoever still controls `owner` can also `set code`. |
| Code freeze | `active` → `eosio.code` only. `owner` → `eosio.null`. | Nobody can upgrade the wasm or call `init` again. Claims still work. Admin can still open or fund if their key is live. They still cannot steal. |
| Stream freeze | After the planned streams are funded, retire the **admin account** the same way (`owner` / `active` → `eosio.null`). | No new `open` memos and no `openstream`. Anyone can still `fund:<id>` an existing stream (that only increases what the fixed beneficiary will receive). |

Do both freezes if the goal is "no upgrade and no new streams". Do not put keys, signer lists, or a filled-in permission JSON in this repo.

Intended mainnet names, when you eventually do this for real: contract `smartavest11`, token `sattoken1111`, symbol `4,SAT`. `init` takes the admin and the token contract as arguments so neither name is hard-coded.

## Build

CDT **4.1.1** (`cdt-cpp`). The `-o` file name has to match the contract name or abigen rejects it, which is why the test wasm is compiled to a temp `smartavest.wasm` and then renamed.

```bash
./build.sh
```

Equivalent commands:

```bash
cdt-cpp -abigen -O3 -o build/smartavest.wasm smartavest/smartavest.cpp

mkdir -p /tmp/smartavest-test-build
cdt-cpp -abigen -O3 -DSAT_VEST_TEST_CLOCK -o /tmp/smartavest-test-build/smartavest.wasm smartavest/smartavest.cpp
cp /tmp/smartavest-test-build/smartavest.wasm build/smartavest_test.wasm
cp /tmp/smartavest-test-build/smartavest.abi build/smartavest_test.abi

cdt-cpp -abigen -O3 -o build/sat.token.wasm sat.token/sat.token.cpp
```

## Tests

`tests/run_tests.sh` starts a throwaway Spring **1.2.2** `nodeos` + `keosd`, deploys the **test** wasm (so it can `setnow`), and runs the spec cases: init-once, memo open, cliff, 50% claim, exact end, division dust that sums to `total`, wrong contract, wrong precision, zero amount, permissionless claim that still pays the beneficiary, `fund:<id>`, unknown memo, non-admin open, `uint32` schedule overflow, production ABI without `setnow`, and two claims in one transaction.

Keys are generated in `/tmp` for that run. The well-known public Antelope dev key is not used. No mainnet endpoint is contacted.

```bash
./tests/run_tests.sh
```

A passing run ends with `RESULT: N passed, 0 failed` and writes `tests/last-run.log`.

`sat.token` is a local eosio.token-compatible ledger so the suite does not need mainnet `sattoken1111`. `tests/doubleclaim.cpp` is a test-only helper.

## Layout

```text
smartavest/smartavest.cpp   contract
sat.token/sat.token.cpp     local test token
build.sh
tests/run_tests.sh
tests/last-run.log
build/smartavest.wasm       production, no setnow
build/smartavest.abi
build/smartavest_test.wasm  test clock, do not deploy
build/smartavest_test.abi
```

MIT license. Not audited yet.
