# Build brief: SAT vesting contract (`smartavest11`)

Paste this whole file into regular Grok. Goal: write, build, and locally test a smallest-safe vesting/timelock contract for SAT on WAX. **Do not deploy to mainnet. Do not create mainnet accounts. Do not sign mainnet transactions.**

SAT contracts are **not audited yet**. Keep that wording if you mention safety.

---

## 1. What SAT is

- **Chain:** WAX (Antelope).
- **Token contract:** `sattoken1111`
- **Symbol:** `SAT`, **4 decimals** (`asset` like `1000000.0000 SAT`, symbol `"4,SAT"`).
- **Max supply:** `100000000.0000 SAT` (fixed; no inflation).
- **Issuer (mainnet):** `smartaadmin1` (do not hardcode issuer logic into the vest contract).
- **Related live contracts (context only):** `smartatrader` (treasury/automation), Alcor `swap.alcor` for LP locks. This vest contract is separate and only holds SAT for timed release.

### Whitepaper allocation (of 100M max) — context for the streams this contract will hold

| Bucket | % | Amount | How it locks |
|---|---|---|---|
| Locked LP | 35% | 35,000,000 | Alcor `lockpos` (not this contract) |
| User rewards | 25% | 25,000,000 | **This contract**, 4-year linear vest, cliff 0 |
| Treasury | 15% | 15,000,000 | Multisig wallet (not this contract) |
| Team | 15% | 15,000,000 | **This contract**, 12-month cliff + 36-month linear vest |
| LEEF community | 10% | 10,000,000 | Airdrop (already sent; not this contract) |

**Vest terms this contract must support:**

| Stream | Cliff | Linear duration after cliff | Notes |
|---|---|---|---|
| **team** | 12 months = `365 * 86400` seconds | 36 months = `1095 * 86400` seconds | Claimable = 0 until `start + cliff`; then linear to full over duration |
| **rewards** | 0 | 4 years = `1461 * 86400` seconds (use 1461 days = 4×365.25, or document `4 * 365 * 86400` if you prefer exact days — pick one, use it consistently, document it) | Cliff 0 means vesting starts immediately at `start` |

Issued so far on mainnet: 20M (≈10M LP + ≈10M airdrop). Remaining mintable under the proposal for this plan: 80M. You are only writing the vest contract + tests; issuance batches are handled elsewhere.

---

## 2. Contract account name

- Intended mainnet account: **`smartavest11`**
- Local/test account may be shorter (e.g. `smartavest`) if the local chain uses short names; keep the C++ contract name `smartavest` / `[[eosio::contract("smartavest")]]`.

---

## 3. Exact contract specification

### 3.1 Design goals

- Smallest safe vesting vault for **SAT@sattoken1111 only**.
- Each stream has a **fixed beneficiary** set at creation; it never changes.
- **`claim` is permissionless:** anyone may call it, but tokens **always** transfer to that stream’s beneficiary (never to the caller, never redirectable).
- **No admin withdraw**, no revoke, no early unlock, no pause-that-steals, no change-beneficiary.
- Prefer **no upgrade path after funding** (document permission recommendation in README; do not bake private key material into the repo).
- Guard overflow, double-claims, and rounding so total claimed never exceeds `total`.

### 3.2 Accepted token (hard rule)

On every deposit path:

- `get_first_receiver()` / notify contract **must** be the configured SAT token contract.
- Quantity symbol **must** be `4,SAT` (precision 4, code SAT).
- Reject any other contract, symbol, or precision.
- Reject zero and negative amounts (`quantity.amount > 0`).
- Reject transfers to self / empty memos that would create ambiguous streams.

Mainnet config: token contract = `sattoken1111`, symbol = `4,SAT`.  
Local tests: allow `init` to set the token contract once to a local `sat.token` (or similar) mock so tests don’t need the real mainnet name.

### 3.3 Time

- Production clock: `eosio::current_time_point().sec_since_epoch()` as `uint32_t` / `uint64_t` carefully.
- Optional **test-only clock:** compile flag e.g. `SAT_VEST_TEST_CLOCK` that adds `setnow(uint32_t t)` (self/auth as contract or admin) and makes `now()` return the stored test time. **Must be compiled out of the production wasm** (default build = no `setnow` in ABI).

### 3.4 Tables

**`config` (singleton, scope = self)**

| Field | Type | Meaning |
|---|---|---|
| `admin` | `name` | Only authority that may `init` (once) and open streams if open is an action |
| `token_contract` | `name` | Accepted SAT contract (`sattoken1111` on mainnet) |
| `token_symbol` | `symbol` | Must be `4,SAT` |
| `inited` | `bool` / implied by row existence | `init` runs once |

**`streams` (multi_index, scope = self)**

| Field | Type | Meaning |
|---|---|---|
| `id` | `uint64_t` | Primary key, auto-increment |
| `beneficiary` | `name` | Fixed payout account |
| `category` | `name` | e.g. `team`, `rewards` (informative; not used for auth) |
| `total` | `int64_t` or `asset` | Total SAT locked in this stream (amount in smallest units or full asset with symbol check) |
| `claimed` | `int64_t` or `asset` | Already released |
| `start` | `uint32_t` | Vest start (unix seconds) |
| `cliff_secs` | `uint32_t` | Seconds after `start` before any unlock |
| `duration_secs` | `uint32_t` | Linear vest length **after** the cliff |
| `created_at` | `uint32_t` | Optional audit field |

Secondary indexes optional: by beneficiary (`uint64_t` = beneficiary.value).

### 3.5 Vesting maths (must match tests)

Let `now` be current unix seconds.

```
cliff_end = start + cliff_secs
end       = cliff_end + duration_secs

if now < cliff_end:  vested = 0
else if now >= end:  vested = total
else:                vested = total * (now - cliff_end) / duration_secs   // integer division
claimable = vested - claimed
```

Requirements:

- Use **wider intermediate type** (e.g. `__int128` / `int128_t`) for `total * elapsed` so `total * duration` cannot overflow `int64`.
- Reject creating a stream where `start + cliff_secs + duration_secs` overflows `uint32_t` (cap before year 2106), or document uint64 times if you prefer — be consistent and test overflow rejection.
- `duration_secs` must be `> 0`.
- `cliff_secs` may be `0`.
- `total` must be `> 0`.
- After a successful claim: `claimed += paid`, and `paid == claimable` at that moment (or allow partial claim of `amount` ≤ claimable if you implement optional amount — default = claim all claimable).
- Never pay more than `total - claimed`.
- Dust from integer division stays in the contract until a later claim (or until `now >= end`, when `vested = total` clears it).

### 3.6 Actions

| Action | Auth | Behavior |
|---|---|---|
| `init(admin, token_contract)` | `get_self()` once | Sets admin + accepted token contract; symbol fixed `4,SAT`. Fails if already inited. Rejects null admin / null token. |
| `openstream(beneficiary, category, start, cliff_secs, duration_secs)` | `admin` | Creates a stream with `total = 0`, `claimed = 0`. `start == 0` means “use now”. Validates beneficiary is an account, duration > 0, no time overflow. Prefer funding via transfer after open **or** allow open+fund in one notify (see below). |
| `claim(id)` | **none required** (permissionless) | Computes claimable; if `claimable <= 0` fail with clear message (`nothing to claim` / `still in cliff`). Inline `eosio.token`-style `transfer` from self → **beneficiary only** of `claimable` SAT via `token_contract`. Updates `claimed`. |
| `setnow(t)` | test build only | Sets test clock. **Not in production ABI.** |

**Do not implement:** `withdraw`, `revoke`, `setbeneficiary`, `rescue`, `pause` that blocks claims permanently without a documented recovery that isn’t a rug, or any path that sends tokens to admin.

### 3.7 `on_notify` transfer handling

Listen for `token_contract::transfer` (use `[[eosio::on_notify("*::transfer")]]` then check `get_first_receiver() == config.token_contract`, or notify specifically once contract is known).

Rules:

1. Ignore transfers where `to != get_self()` (outbound claims).
2. Ignore / no-op when `from == get_self()`.
3. Require `quantity.symbol == config.token_symbol` and `quantity.amount > 0`.
4. **Memo formats** (choose one scheme and test it; recommended):

**Scheme A — open and fund in one transfer**

```
open:<beneficiary>:<category>:<cliff_secs>:<duration_secs>
```

Optional 6th field `:start` (unix); if omitted, `start = now`.

- Creates a new stream with `total = quantity.amount`.
- Only `admin` may be `from` **or** allow any funder but still fix beneficiary from memo (pick one; **recommend admin-only open** so random people cannot create team streams — enforce `require_auth`-equivalent by checking `from == config.admin` on open memos).

**Scheme B — fund an existing stream**

```
fund:<stream_id>
```

- Adds `quantity` to `streams[id].total`.
- Reject if stream missing.
- Recommend: only allow funding until first claim, **or** allow top-ups anytime but never reduce `total` below `claimed`. Simplest safe rule: **allow top-up anytime** with `total += qty` and `total >= claimed` invariant.

Implement **both** `open:...` and `fund:<id>` for flexibility.

5. Reject unknown memos (don’t silently accept free money into an untracked balance without a stream — either reject or require memo). **Prefer reject** untracked deposits so SAT can’t get stuck unaccounted.

### 3.8 Inline transfer on claim

```text
action(permission_level{get_self(), "active"_n},
       token_contract, "transfer"_n,
       std::make_tuple(get_self(), beneficiary, asset{paid, sym}, memo)).send();
```

Contract account must have `eosio.code` on `active` after deploy (document this; tests should `set account permission … --add-code`).

### 3.9 Post-deploy permissions (document in README only — no keys in repo)

State clearly in the README:

| Phase | Recommendation | Tradeoff |
|---|---|---|
| Setup | `active` includes admin + `eosio.code` so admin can `init` / open / fund | Admin can still `set code` if they control owner |
| After all planned streams are funded | Set **owner → `eosio.null`** (or equivalent immutable), **active → `eosio.code` only** | **Cannot open new streams or upgrade**; claims still work via code. Strongest “no rug / no upgrade” posture |
| Alternative | Leave admin multisig on `active` forever, but code has no withdraw | Admin cannot steal via ABI, but **could upgrade code** if owner isn’t nulled |

Do **not** put private keys, multisig signer lists, or permission JSON secrets in the repo.

---

## 4. Required tests (local nodeos only — no mainnet, no WAX testnet unless throwaway and never mainnet)

Mirror the style of a throwaway local `nodeos` + `keosd` + `cleos` suite (temp datadir, fresh key per run). Toolchain: **Antelope CDT 4.1.1** (`cdt-cpp`), Spring `nodeos`/`cleos`/`keosd` ~1.2.x.

Build production wasm **without** `SAT_VEST_TEST_CLOCK`. Build test wasm **with** `-DSAT_VEST_TEST_CLOCK` (or equivalent).

### Pass/fail cases to implement

1. **init** once succeeds; second init fails.
2. **Deposit/open** via transfer memo creates a stream with correct `total`, beneficiary, cliff, duration.
3. **Claim before cliff** fails; beneficiary balance unchanged.
4. **Partial claim mid-vest** (test clock → 50% through duration after cliff): pays `floor(total/2)` (or exact integer formula); `claimed` updates; second immediate claim fails or pays 0.
5. **Full amount at end**: after `start+cliff+duration`, claim pays remainder; total received by beneficiary == `total`; further claim fails.
6. **Never more than total**: multiple claims across time sum to exactly `total`.
7. **Wrong token rejected**: transfer of same symbol from a different contract, or wrong symbol/precision, does not create/fund a stream (and should error).
8. **Wrong precision / zero amount rejected.**
9. **Non-beneficiary cannot redirect**: caller ≠ beneficiary still sends tokens only to beneficiary.
10. **fund:\<id\>** top-up increases `total`; claimable uses new total; cannot claim unvested.
11. **Unknown memo rejected** (no silent untracked balance).
12. **Non-admin cannot open** a stream (if admin-only open).
13. **Overflow / absurd schedule rejected** (e.g. cliff+duration overflowing uint32).
14. **Production build ABI has no `setnow`.**
15. **Double-claim guard**: two claims in one tx or back-to-back at same timestamp don’t overpay.

Save the full test log to e.g. `tests/last-run.log` and print `RESULT: N passed, M failed`.

Include a local copy or build of a minimal eosio.token-compatible SAT token for the suite (create/issue/transfer). A reference copy may already exist under `/workspace/sat-vest/sat.token/sat.token.cpp` on the machine that prepared this brief.

---

## 5. Expected deliverables

Return / write these files (suggested layout):

```text
sat-vest/
  README.md                 # usage, maths, permission tradeoff, "not audited yet"
  smartavest/smartavest.cpp # single translation unit preferred; .hpp only if needed
  smartavest/smartavest.hpp # optional
  sat.token/sat.token.cpp   # test token (eosio.token-compatible)
  build.sh                  # cdt-cpp commands for prod + test builds
  tests/run_tests.sh        # local nodeos suite
  docs/SPEC.md              # optional short restatement of this brief
  build/smartavest.wasm     # prod
  build/smartavest.abi
  build/smartavest_test.wasm  # optional separate test artifact
  build/smartavest_test.abi
  tests/last-run.log
```

### Build commands (CDT 4.1.1)

```bash
# Production (no test clock)
cdt-cpp -abigen -O3 -o build/smartavest.wasm smartavest/smartavest.cpp

# Test build with clock
cdt-cpp -abigen -O3 -DSAT_VEST_TEST_CLOCK -o build/smartavest_test.wasm smartavest/smartavest.cpp
```

Same for `sat.token` if needed:

```bash
cdt-cpp -abigen -O3 -o build/sat.token.wasm sat.token/sat.token.cpp
```

### Out of scope for this Grok task

- Creating `smartavest11` on mainnet
- Deploying wasm to mainnet
- Issuing or transferring real SAT
- Alcor LP lock batches
- Any secrets, keys, `.env`, or real permission JSON with live keys

---

## 6. CDT / node version

| Tool | Version used in this project |
|---|---|
| CDT | **4.1.1** (`cdt-cpp version 4.1.1`) |
| nodeos / cleos / keosd | Antelope Spring **v1.2.2** (or compatible Spring 1.2.x) |

---

## 7. Sanity checklist before you finish

- [ ] Only SAT from the configured token contract is accepted
- [ ] Beneficiary fixed at open; claim always pays beneficiary
- [ ] Claim permissionless
- [ ] No admin withdraw / revoke / early unlock in ABI
- [ ] `__int128` (or equivalent) in vest multiply
- [ ] Test clock compiled out of prod ABI
- [ ] Local tests all PASS; log saved
- [ ] README says **not audited yet**
- [ ] No keys or secrets in the tree

---

## 8. Scratch note for the human paste

A scratch directory `/workspace/sat-vest` may already exist with empty `smartavest/`, `tests/`, `build/`, `docs/` folders and a copied `sat.token/sat.token.cpp`. You may continue there or create a clean tree; do not assume any vesting `.cpp` was finished.
