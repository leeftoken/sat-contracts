# SAT / leeftrader security and logic audit

**Date:** 2026-11-09  
**Auditor:** Cursor cloud agent (source review + live WAX mainnet reads; no exploit development, no signing, no deployment)  
**Code under review:** this repo, claimed byte-identical to mainnet  
**Live accounts:** `sattoken1111` (SAT), `smartatrader` (leeftrader)

This is a human-style source-and-config review, not a substitute for a specialist Antelope firm with formal verification or fuzzing. No private keys or secrets were used or added.

---

## Verdict

The **on-chain bytecode matches** the source in this repo, and several important guards work as designed (fake-token `get_first_receiver` check, swap-memo construction, per-action/daily bps caps, Alcor `lockpos` on the live LP, bucket balances currently reconciling to token balances).

The **dominant risk is not a hidden drain in `on_notify`**. It is that the live permission layout and `require_auth(account)` (any permission of that account) collapse the advertised 2-of-3 into a **single-signer super-admin**, who can upgrade both contracts, mint the remaining 80M SAT, rewrite whitelists, and (after unlock, or via `setcode` now) move funds. Caps and the 42-year Alcor lock only constrain the **bot permission**, not that signer.

Contract logic bugs that should be fixed in a **future, separately reviewed upgrade** (not in this PR): same-token `buyback`/`checkout` net-delta mismatch; `require_auth` vs `require_auth2`; `cfg.swap` frozen at `init`; `lpwithdraw` bucket labelling.

**Do not treat current TVL as the loss bound.** Caps are a percentage of each bucket. SAT mint headroom is 80M. Upgrade can remove every guard.

---

## Scope and method

| In scope | Out of scope |
|---|---|
| `sat.token/sat.token.cpp` + ABI | Off-chain bot, Telegram, website |
| `leeftrader/leeftrader.cpp` + ABI | Alcor `swap.alcor` internals (trust boundary; behaviour checked against public ABI/docs) |
| Auth, asset math, `on_notify`, inline actions, RAM, tables, pause/caps, admin, upgrade, doc mismatch | Writing or deploying replacement WASM |

**Method:** line-level review of both contracts; ABI vs client vs tests; `DESIGN.md` / `README.md` / `AUDIT-SCOPE.md` / `params.proposal.json` vs code; live `get_code_hash`, tables, accounts, Alcor pool/lock/position, Hyperion. Mainnet reads used `https://wax.greymass.com` and `https://wax.eosusa.io` on 2026-11-09.

**Reproducible hashes (match live `get_code_hash`):**

| Account | sha256 |
|---|---|
| `sattoken1111` | `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` |
| `smartatrader` | `4ed7d02a7f7107dc03d9c9df61b77fd4927368ea95b669e0d0ba228f1ee729c4` |

---

## Live snapshot (2026-11-09)

**SAT:** 20,000,000.0000 / 100,000,000.0000 issued; issuer `smartaadmin1`. Remaining **80M mintable**. `smartaadmin1` holds 0.0365 SAT (not the 10M airdrop described in `AUDIT-SCOPE.md`). `danishcrypto` holds ~505,021 SAT. Alcor pool 12905 (LEEF/SAT) holds ~7.82M SAT.

**`smartatrader` config:** admin `smartaadmin1`, bot `smartabot111`, fee `smartafees11`, swap `swap.alcor`, **not paused**, `max_in_bps=100`, `daily_cap_bps=500`, `lock_secs=1325419200`.

**Split (ord 0–4):** satlp 30 / leeflp 20 / ops 30 / rewards 10 / reserve 10. Dust → last row (`reserve`).

**Whitelisted tokens (live, broader than `AUDIT-SCOPE.md`):**

- `4,SAT` @ `sattoken1111`
- `8,WAX` @ `eosio.token`
- `6,USDT` @ `wrap.alcor`
- `4,LEEF` @ `leefmaincorp`
- `6,WAXUSDC` @ `eth.token`
- `8,WAXWBTC` @ `eth.token`

**Pools (live, not “12405 only”):** 12405 (WAX/USDT), 12905 (LEEF/SAT), 12925 (WAX/SAT), 13230 (LEEF/SAT), 13231 (WAXUSDC/SAT), 13235 (WAX/SAT), 13239 (WAXWBTC/SAT).

**Plans:** ids 1–3 WAX (`213` / `427` / `4269`, 30/30/365d); ids 11–13 `wrap.alcor` USDT 6dp (`0.975000` / `1.950000` / `19.500000`). **No SAT-priced plans** (20% SAT discount is not on-chain).

**Destinations:** only `smartafees11`. **Subs:** one row (`qu.ui.wam`, plan 1, expires 2026-11-24).

**LP:** `smartatrader` owns Alcor position **153124** on pool 12405, ticks `-105960`/`-93960`, liquidity `3640906`. `swap.alcor::locks` has `pos_id=153124`, `unlockTime=3118144086` (~2068-10-22). `lockpos` tx `10fab4ce…` on 2026-10-23. Alcor deposit leftover: `0.00000018 WAX`.

**Bucket vs wallet (accounting currently consistent):**

| Asset | Contract wallet | Sum of buckets |
|---|---|---|
| WAX | 219.46640000 | 63.3664 + 44.6 + 66.9 + 22.3 + 22.3 = 219.4664 |
| SAT | 91.2780 | 91.2780 (satlp) |
| USDT | 0.000452 | 0.000452 (satlp) |

Hyperion shows a same-day buyback output `91.2780 SAT` from `swap.alcor` (pool 12925) credited to satlp — the **different-token** `checkout` path works on mainnet.

---

## Finding index

| ID | Severity | Title |
|---|---|---|
| [C-01](#c-01-live-2-of-3-is-a-single-signer) | **Critical** | Live “2-of-3” collapses to `danishcrypto` |
| [C-02](#c-02-require_auth-any-permission-bypasses-intended-roles) | **Critical** | `require_auth(account)` accepts owner/active, bypassing 2-of-3 and bot `linkauth` |
| [H-01](#h-01-80m-sat-mint-headroom--upgradeable-code) | **High** | 80M SAT mint + upgradeable token/trader code |
| [H-02](#h-02-same-token-buyback-always-reverts) | **High** | Same-token `buyback`/`checkout` net-delta vs Alcor gross `minOut` |
| [H-03](#h-03-bot-active-key-can-payout-and-rewrite-bot-perm) | **High** | Bot `@active` key is unrestricted; can `payout` |
| [H-04](#h-04-admin-can-remove-caps-and-redirect-all-flows) | **High** | Admin (or C-01 signer) can set 100% caps, dests, tokens, fee recipient |
| [M-01](#m-01-cfgswap-immutable-setswap-does-not-move-lp) | **Medium** | `cfg.swap` fixed at `init`; `setswap` only affects `buyback` |
| [M-02](#m-02-lpwithdraw-trusts-bot-bucket--swap-from-skip) | **Medium** | `lpwithdraw` bucket label + unaccounted swap.alcor transfers |
| [M-03](#m-03-unknown-token-notify-asserts-instead-of-return) | **Medium** | `on_notify` `check("token not accepted")` aborts foreign transfers |
| [M-04](#m-04-docs-and-tests-diverge-from-code-and-mainnet) | **Medium** | README/DESIGN/AUDIT-SCOPE/params vs code vs live |
| [M-05](#m-05-client-addlp-abi-field-mismatch) | **Medium** | JS client sends `tl`/`tu` instead of `tick_lower`/`tick_upper` |
| [M-06](#m-06-subscription-semantics-and-ram) | **Medium** | Overpay, plan overwrite, gift `sub:<id>:<user>`, cheap-plan RAM |
| [L-01](#l-01-split-dust-and-tiny-payments) | **Low** | Rounding dust all to last bucket |
| [L-02](#l-02-pause-does-not-cover-inbound-or-collectfees) | **Low** | Pause only blocks `guard_bot` paths |
| [L-03](#l-03-sat-token-compatibility-nits) | **Low** | Issue-to-issuer-only; extra symbols possible; `sub_balance` RAM payer |
| [L-04](#l-04-deny-list-is-not-a-security-boundary) | **Low** | `setfeercpt` bypasses `check_actor`; trading accounts subscribe and trade |
| [I-01](#i-01-controls-that-work) | **Info** | Guards that held up under review |

Contract logic fixes are **recommendations only**. This PR does not change WASM, ABI, or tables.

---

## Critical

### C-01 Live 2-of-3 is a single signer

**Location:** WAX account permissions (not WASM). Documented as 2-of-3 in `DESIGN.md` (Multisig setup), `AUDIT-SCOPE.md` (Accounts and permissions), `params.proposal.json`.

**Live `smartaadmin1` / `smartafees11`:**

- `@active`: threshold 2; `danishcrypto@active`, `leef@active`, `smart.ass@active` weight 1 each. This part matches the docs.
- `@owner`: threshold 2; **`danishcrypto@active` weight 2**, `leef` and `smart.ass` weight 1.

`danishcrypto@owner`/`@active` are **single keys**. `smart.ass@active` and `@owner` are threshold 1 and include `danishcrypto@active` as an account authority. Therefore `danishcrypto`:

1. Alone satisfies `smartaadmin1@owner` and `smartafees11@owner` (weight 2 ≥ 2).
2. Can `updateauth` those accounts’ `@active` 2-of-3 away.
3. Can also authorize as `smart.ass@active`, i.e. supply **two of the three active signatures** without `leef`.

**Live `smartatrader` / `sattoken1111` `@owner`:** threshold **1**, accounts `danishcrypto@active` **or** `smartaadmin1@active`. Either actor can `setcode` / `setabi` / `updateauth`.

On **2026-11-09** (this audit day) Hyperion/`get_actions` shows `smartatrader@owner` changed from “only `smartaadmin1`” (set 2026-10-23) to “`danishcrypto` + `smartaadmin1`, threshold 1”. The same day, `settoken` / `setpool` ran as **`smartaadmin1@owner`**, adding LEEF, WAXUSDC, WAXWBTC and pools 12905/12925/13230/13231/13235/13239 — not via the advertised 2-of-3 `@active`.

`DESIGN.md` required `smartatrader`/`sattoken1111` owner+active to be the same 2-of-3 JSON, with `eosio.code` weight 2 on trader `@active`. Live trader `@active` is threshold **1**: `smartaadmin1@active` **or** `eosio.code`.

**Impact:** One key (`danishcrypto@active`) can upgrade both live contracts, replace admin/fee authorities, and then mint or drain. The 42-year Alcor lock still blocks `subliquid`/`transferpos` on position 153124 until 2068 **unless Alcor’s lock implementation changes**. Everything in buckets (and future LP) is unbounded by that lock.

**Recommended fix (ops, immediate, no WASM):** Restore equal-weight 2-of-3 on **owner and active** of `smartaadmin1`, `smartafees11`, `smartatrader`, `sattoken1111`. Remove `danishcrypto` from `smart.ass` owner/active (or stop using `smart.ass` as a distinct msig signer). Put `eosio.code` on trader `@active` with threshold ≥ 2 so a single admin signature cannot act as the contract. Consider `eosio.null` on token/trader **owner** after a real upgrade freeze. Rotate keys that have been used as single-signer owner.

---

### C-02 `require_auth` (any permission) bypasses intended roles

**Location:** `leeftrader/leeftrader.cpp` — every `require_auth(c.admin)`, `require_auth(c.bot)`, `require_auth(actor)` on `pause`/`collectfees`; `sat.token/sat.token.cpp` `require_auth(s.issuer)` on `issue`/`retire`.

Antelope `require_auth(name)` succeeds if **any** permission of that account is in the authorization list. It does **not** require `@active`.

Consequences, confirmed live:

| Intended control | Actual |
|---|---|
| Admin actions = `smartaadmin1@active` 2-of-3 | `smartaadmin1@owner` works. Owner is danishcrypto-only (C-01). Used on 2026-11-09 for `settoken`/`setpool`. |
| Bot actions = `smartabot111@bot` + `linkauth` | `smartabot111@active` also satisfies `require_auth(c.bot)` and is **not** linkauth-limited (see H-03). |
| SAT `issue` = admin 2-of-3 | `smartaadmin1@owner` can issue 80M after C-01, without `leef`. |

`pause`/`collectfees` take an `actor` argument and `require_auth(actor)`. That is the right shape for custom perms, but admin/bot **config actions** do not use `require_auth2`.

**Recommended fix (future WASM, ABI-stable):**

```cpp
require_auth(permission_level{c.admin, "active"_n}); // or require_auth2
require_auth(permission_level{c.bot, "bot"_n});
```

Pair with linkauth of `bot` to the bot action set (include or exclude `payout` deliberately — see H-03 / M-04). Document that issuer `issue`/`retire` must be `@active` 2-of-3, or move issuer to `eosio.null` (H-01).

---

## High

### H-01 80M SAT mint headroom + upgradeable code

**Location:** `sat.token/sat.token.cpp` `issue` (lines 23–36); live `stat` issuer `smartaadmin1`; live owner of `sattoken1111` (C-01).

`issue` correctly caps at `max_supply - supply` and only credits the issuer. That does **not** make supply “fixed”. `DESIGN.md` says “no inflation” and “After issue, set the issuer/owner permission to the multisig (or `eosio.null` for owner) so supply is fixed.” `params.proposal.json` says `"inflation": "none (max supply fixed in sat.token)"`. Live: issuer still `smartaadmin1`, 80M unissued, owner not `eosio.null`.

Two `issue` actions ran 2026-10-23 (10M “launch LP”, 10M “community airdrop”). Further `issue` is a 2-of-3 **or** (via C-01/C-02) a single owner signature.

`smartatrader` and `sattoken1111` remain `setcode`-able. A replacement trader WASM can add `subliquid` wrappers, ignore dest/caps, or `transfer` any balance. Alcor lock is the only constraint on **existing** position 153124.

**Recommended fix:** `issue` remaining 0 by retiring mint rights: set issuer to an account with `eosio.null` active, or `setcode` to a token without `issue` (ABI/state-breaking — separate upgrade). Until then, treat 80M as **admin liability** and disclose it on every SAT surface. Freeze trader/token owner to high-threshold msig or `eosio.null` **after** any logic patch.

---

### H-02 Same-token `buyback` always reverts

**Location:** `leeftrader/leeftrader.cpp` `buyback` (153–175), `checkout` (177–189).

`buyback` records `before = balance_of(min_out)` **before** the input transfer, then requires `now - before >= min_out.amount`. For **different** tokens this is the swap output (confirmed live: 91.2780 SAT). For **same** token:

1. Input leaves the contract (`before - in`).
2. Alcor sends gross `out` from `swap.alcor` (docs: output transfer `from: swap.alcor`).
3. `got = out - in`.
4. Same-token rule requires `min_out >= in + 1`, and the Alcor memo uses that same `min_out` as **gross** minimum.

So a profitable loop `in=100`, Alcor `out=101` yields `got=1`, then `1 >= 101` fails. Raising memo `min_out` makes Alcor stricter; lowering it is forbidden by the +1 check. **The path cannot succeed.**

Tests only assert `min_out == in` is rejected (`in + 1`). They never assert a successful same-token loop. The JS client still encodes the +1 rule as if it were usable.

This is not a theft bug; it bricks the documented wash-trade/arb guardrail. A naive “fix” of comparing `got` to `min_out - in` without changing the memo would desync Alcor’s gross `minOut` and is **easy to get wrong** — design it in a dedicated upgrade.

**Recommended fix (future WASM):** Snapshot `before` **after** sending input, or require `got >= min_out` only for distinct extended symbols and `got >= 1` (net) plus memo gross `in+1` for loops. Add a test that a mock swap returning `in+1` **succeeds**. This PR adds a local test that today’s behaviour reverts (`below min_out`).

---

### H-03 Bot `@active` key can `payout` and rewrite `bot` perm

**Location:** `leeftrader.cpp` `payout` (233–240) uses `guard_bot()` → `require_auth(c.bot)`; live `smartabot111`.

Live `smartabot111@bot` is linkauth’d to `buyback`, `addlp`, `collectfees`, `lpwithdraw`, `pause` only — **not** `payout`. That matches `AUDIT-SCOPE.md` and contradicts `DESIGN.md` (which lists `payout` on the bot perm and as a bot action; `AUDIT-SCOPE.md`’s action table says `payout` is **admin**).

Code: **bot only**, not admin. Live `@active` of the bot is a **different key** (`EOS6jqGJ…`) with **no** `linkauth`. `require_auth(c.bot)` accepts `@active` (C-02). Therefore the active key can:

- `payout` up to caps to any dest (`dests` is currently only `smartafees11`).
- `updateauth` on the `bot` child permission (parent is `active`).
- `buyback` / `addlp` with in-cap grief (bad ticks → 42y lock of treasury into a worthless range; bad `min_out` still bounded by whitelist + checkout).

Bot `@owner` is threshold 1 with that same active-class key **and** `danishcrypto@active`.

**Recommended fix:** Remove keys from `smartabot111@active`; keep a single `bot` key + linkauth. Decide product intent:

- If payout is treasury ops: `require_auth(c.admin)` (or admin **or** bot) and document it; currently admin **cannot** payout without `setbot` or the bot active key.
- If payout is bot: add `payout` to linkauth **and** drop the active key.

Caps mean a compromised bot can move **5%/day/bucket** to `smartafees11` today (then 2-of-3 / C-01 controls that account). Grief via `addlp` is worse: funds become Alcor-locked ~42 years in a range the bot chooses.

---

### H-04 Admin can remove caps and redirect all flows

**Location:** `setlimits` (47–55) allows `max_in_bps`/`daily_cap_bps` ∈ `[1, 10000]`; `setdest`, `settoken`, `setpool`, `setsplit`, `setfeercpt`, `setbot`, `setplan`.

There is **no absolute** WAX/USDT cap. At 10000 bps the bot (or a bot the admin just set) can empty a bucket in one action. `setfeercpt` does not `check_actor`, then `payout` skips `check_actor` when `to == fee_recipient`. Combined with C-01 this is full centralization, not a bot-compromise scenario.

Default struct caps (5%/20%) differ from live (1%/5%) and from `params.proposal.json` (5%/20%). Live values are a **policy choice**, not an invariant.

**Recommended fix:** Hard-code ceilings (e.g. max 500/2000 bps) in WASM if the product wants un-adminable brakes; otherwise disclose “caps are advisory until owner is nulled.” Keep live 100/500 until TVL and a firm audit justify more.

---

## Medium

### M-01 `cfg.swap` immutable; `setswap` does not move LP

**Location:** `init` writes `config.swap` and `swaps` (28–45). `setswap` (97–102) only touches `swaps`. `buyback` uses the action’s `swap` argument against `swaps`. `addlp` / `lpwithdraw` / `collectfees` always use `c.swap`.

There is **no action** to rotate Alcor. A migration of `swap.alcor` requires `setcode`. A second whitelisted swap can be used for `buyback` but LP still hits the `init` account.

**Recommended fix:** `setswap` should update `cfg.swap` when `allowed=true` (or add `setswapcfg`). Until then, only whitelist DEX accounts you intend as `from` on skipped notifies (M-02).

---

### M-02 `lpwithdraw` trusts bot bucket + swap-from skip

**Location:** `on_transfer` (136–141) returns without accounting if `from` is in `swaps`; `lpwithdraw` (217–222) `credit(bucket, amount)` with **bot-chosen** `bucket`; `checkout` credits the full balance **delta** of `min_out` to the buyback bucket.

Documented residual in `DESIGN.md` audit table; still true.

- Transfers **from** `swap.alcor` (output, refunds, mistakes) are not split. `checkout` only books the measured delta of one symbol into one bucket. Extra symbols or leftover input sit in the wallet **unlabelled** (or remain in Alcor `balances` — live dust `0.00000018 WAX`).
- Bot can `lpwithdraw` leftovers into `ops` even if `satlp` paid `addlp`. Tokens stay in the contract; labels and **caps** follow the label (`spend` uses the bucket table). That is how a compromised bot stretches a small bucket’s 5% cap: credit the fat Alcor leftover into a thin bucket, then `payout`/`buyback` against the new label.

Honest Alcor `withdraw` should revert if the deposit is missing; tests use `mockswap`, which sends whatever is asked.

**Recommended fix:** Credit `lpwithdraw` to the `lps.bucket` of the related position, or to a dedicated `refunds` bucket. Optionally, on swap-from transfers of **unexpected** symbols, `credit` a `unassigned` bucket instead of ignoring. Do not credit more than Alcor’s `balances` row (read it the same way `balance_of` reads `accounts`).

---

### M-03 Unknown-token notify asserts instead of return

**Location:** `on_transfer` line 144: `check(..., "token not accepted")` after the swap-from skip.

Inbound **non-whitelisted** tokens (or wrong precision) **fail the sender’s transfer**. That prevents silent stuck funds, but:

- Any protocol that `require_recipient(smartatrader)` on a `transfer(name,name,asset,string)` with another token will revert.
- Alcor/bridges sending an unexpected token **to** the trader (not `from` swap) abort the surrounding tx.
- Standard pattern for “not our deposit” is `return`, not `check`.

Wildcard `on_notify("*::transfer")` only runs if the payload matches this signature; a non-matching `transfer` action that notifies the trader will fail deserialization (classic Antelope footgun). Relevant if an NFT or odd DEX notifies `to`/`from` = `smartatrader`.

**Recommended fix:** `return` on whitelist miss. Keep `check` only inside `handle_sub` after the user opted into `sub:`.

---

### M-04 Docs and tests diverge from code and mainnet

| Claim | Reality |
|---|---|
| `AUDIT-SCOPE.md`: tokens WAX/USDT/SAT; pool 12405 only | Live also LEEF, WAXUSDC, WAXWBTC; 7 pools (whitelisted 2026-11-09) |
| `AUDIT-SCOPE.md`: 10M SAT still on `smartaadmin1` for airdrop | Admin balance 0.0365 SAT |
| `AUDIT-SCOPE.md`: `payout` = admin; bot linkauth has no payout | Code = bot; live `@bot` has no payout; `@active` can payout |
| `DESIGN.md`: `payout` = bot; bot linkauth **includes** payout | Linkauth does not |
| `DESIGN.md` paths `contracts/…`, `MAINNET.md`, Hermes skill | Files live at repo root; `MAINNET.md` missing |
| `DESIGN.md`: SAT “no inflation”; issuer → `eosio.null` | 80M unissued; issuer `smartaadmin1` |
| `DESIGN.md` default caps 5%/20%; live 1%/5% | `config` defaults 500/2000; live 100/500 |
| `params.proposal.json` USDT `"0.9750 USDT"` (4dp) vs comment 6dp | Live plans 6dp `wrap.alcor`; ids 11–13 exist |
| `sat_discount_bps: 2000` | No SAT plans, no discount code |
| Tests split 30/25/25/10/10 | Production 30/20/30/10/10 |
| Tests issue 100% SAT to the token account | Mainnet 20% issued to admin |
| Client / DESIGN: `leeftrader.bot` permission name | Live permission name is `bot` |

Stale docs are an ops hazard (wrong linkauth, wrong threat model, wrong token list).

**Recommended fix:** Treat `AUDIT-SCOPE.md` as a living mainnet sheet (or generate it from `get_table_rows`). Keep `DESIGN.md` as a dated design memo and point to this report for “as deployed.”

---

### M-05 Client `addlp` ABI field mismatch

**Location:** `client/leeftrader-client.mjs` `addlpAction` sets `tl` / `tu`. ABI / C++ fields are `tick_lower` / `tick_upper`.

eosjs will zero the real fields. `tick_lower < tick_upper` then fails (`0 < 0`). Safer than a bad lock, but **addlp via this helper cannot work**.

This PR fixes the payload names and adds a unit test. No contract change.

---

### M-06 Subscription semantics and RAM

**Location:** `handle_sub` (355–376), `on_transfer` (147–148).

- Memo `sub:<plan_id>[:<user>]`. `name(substr)` gifts a sub to any account. Overwrite sets `plan` to the **new** id but extends `expires` from the remaining time — attacker can pay Basic and relabel a Pro row (costs them the Basic price).
- `paid >= price`, no refund of overpay; full amount is split.
- Rows paid by `get_self()`. Current WAX/USDT prices make RAM-grief unprofitable. `setplan` allows `amount > 0` (one unit). A 0.00000001 WAX plan would let anyone create contract-paid `subs` rows (~0.3 KB each).
- No `closesub`; expired rows never free RAM.
- `days * 86400 + start` is uint32; stacking toward year 2106 can wrap (same class as the existing lock-overflow check).
- `sub:` memos that fail parse **revert the payment** (user keeps funds). A donation memo starting with `sub:` is not a silent fund.

**Recommended fix:** Optional `check(paid == price)`; do not overwrite `plan` with a lower tier without resetting expiry; `closesub` after expiry; reject `sub:` unless the rest is a known plan (already mostly true); floor plan price at a RAM-covering minimum; use `uint64_t` for expiry math.

---

## Low

### L-01 Split dust and tiny payments

**Location:** `split_in` (342–353). Non-last parts floor `__int128` mul/div 10000; last `ord` gets `left`. For amount `1`, 100% goes to `reserve`. Fine at WAX 8dp / USDT 6dp plan sizes; misleading for 1-unit donations.

Zero-bps last bucket would still absorb dust. `setsplit` allows that if sums to 10000.

---

### L-02 Pause does not cover inbound or `collectfees`

**Location:** `guard_bot` (294); `on_transfer` has no pause check; `collectfees` (224–231) only `require_auth(actor)` + role check.

While paused: subscriptions and generic deposits still split; LP fees can still be collected to `fee_recipient`. Bot cannot `payout`/`buyback`/`addlp`/`lpwithdraw`. Admin config still works. Probably intended; document it. Drawdown pause does **not** freeze fee extraction.

---

### L-03 SAT token compatibility nits

**Location:** `sat.token/sat.token.cpp`.

- `issue` requires `to == issuer` (stricter than reference `eosio.token`, which inline-transfers to `to`). README “eosio.token-compatible” is slightly wrong; safer for mint.
- `create` allows any symbol; `sattoken1111@active` could list a second token on the same code.
- `sub_balance` `modify(..., owner)` can fail if the row’s RAM payer is not `owner` and `owner` has no RAM (classic `eosio.token`).
- `open` is paid by `ram_payer` (standard).
- `require_recipient` before balance updates matches historical `eosio.token` (notifies run after the action body).

No fake-token issue **in this contract**; consumers must still check `sattoken1111` + `4,SAT`.

---

### L-04 Deny-list is not a security boundary

**Location:** `check_actor` (287–292); `setfeercpt` (63–71); `payout` (237).

Banned names cannot be bot or `setdest` targets. They **can** be `fee_recipient`, msig signers (by design), subscribers (`qu.ui.wam` is live on `subs`), and SAT traders (`skgaw.wam`, `smart.ass`, `niftywarlord`, `dustwizard42` appear on SAT/Alcor transfers). `setfeercpt(danishcrypto)` then `payout` to that account skips `check_actor`.

Do not market the deny-list as preventing team wallets from receiving treasury.

---

## Info

### I-01 Controls that work

- **Fake token / fake transfer:** `get_first_receiver()` must equal the whitelisted contract and full `extended_symbol` (code **and** precision). Table PK is symbol code, so two contracts cannot share `USDT`. `min_out` is checked as extended symbol, not symbol-only (the old issue in `DESIGN.md`’s automated checklist is fixed).
- **Memo injection:** `pool_path` only `[0-9,]+`, length ≤ 64, pool ids ≤ 18 digits (fit `uint64`), each id must be in `pools`. Recipient in the Alcor memo is always `get_self()`. Extra `#market` referral field is not appended ([Alcor swap memo](https://docs.alcor.exchange/developers-api/amm-swap-contract-api)).
- **Self-notify:** `from == get_self()` returns so `payout`/`buyback` do not re-split outgoing transfers.
- **Atomic swap:** `pending` singleton + inline `checkout`; failed `min_out` reverts the whole tx (Alcor included). `checkout` is `require_auth(self)`. Different-token path verified live.
- **Caps math:** `__int128` for bps; daily base = `balance + spent_today` so intra-day spends share one notional. Live 1%/5% currently bind the bot.
- **Lock overflow:** `setlimits` / `addlp` reject `now + lock_secs > 0xFFFFFFFF`. Live lock row matches `lps.unlock`.
- **No `subliquid` in trader ABI.** Liquidity exit is Alcor’s action, which is lock-blocked until 2068 for pos 153124 ([Alcor lock docs](https://docs.alcor.exchange/alcor-swap/locking-liquidity): lock cannot be shortened; fees still collectable).
- **`addlp` mins > 0**, deadline `now+300`, ticks ordered, pool whitelist.
- **Split** must sum 10000; ≤ 10 buckets; last absorbs dust; uses `ord` not table iteration order.
- **Wallet vs buckets** currently match for WAX/SAT/USDT.

Trust remains on: Alcor upgradeability, `eth.token` / `wrap.alcor` / `leefmaincorp` issuers, and C-01 keys.

---

## Auth matrix (code)

| Action | Auth in WASM | Pause | Caps | Notes |
|---|---|---|---|---|
| `init` | `self` | n/a | n/a | Once |
| `setlimits` `setbot` `setfeercpt` `setsplit` `settoken` `setswap` `setdest` `setpool` `setplan` | `c.admin` (any perm) | no | n/a | C-02 |
| `pause` | `actor` == admin, or bot with `paused=true` | n/a | n/a | |
| `on_notify *::transfer` | token contract notify | no | n/a | Whitelist or abort |
| `buyback` `addlp` `lpwithdraw` `payout` | `c.bot` (any perm) | yes | yes except `lpwithdraw` | |
| `collectfees` | bot or admin | **no** | n/a | Fees → `fee_recipient` only |
| `checkout` logs | `self` | n/a | n/a | |

---

## Overflow / precision notes

- Asset `+=` / `-=` use CDT checks; cap compares use `__int128`.
- Plan id and pool id parsers: max 18 digits, no `uint64` overflow.
- `issue`: `max_supply.amount - supply.amount` is the usual `eosio.token` pattern; amounts fit SAT’s 4dp × 1e8.
- Split uses floor division; last bucket can exceed its bps (L-01).
- `time_point_sec` / `uint32` lock and sub expiry: year-2106 class; lock path is guarded, sub path is not.

---

## Reentrancy / inline actions

Antelope has no EVM reentrancy, but nested notifies are real.

`buyback` order: `spend` → set `pending` → inline token `transfer` to swap (Alcor notify, possible output notify back) → inline `checkout`. Swap `from` is skipped. A **malicious** extra `swaps` row (admin `setswap`) could skip accounting and still increment `checkout`’s delta (M-01/M-02). Token contracts on the whitelist (`eth.token`, `wrap.alcor`) run during `send_transfer`; compromise there is out of scope but is a nested-call surface.

`pending` prevents a second overlapping buyback in the same execution if it were somehow re-entered; bot auth is still required. No deferred tx.

Compromised dest contract on `payout` could reenter `payout` (bot auth still required). Nested payouts each hit caps.

---

## RAM payer

All trader tables use `get_self()` / `same_payer`. Attack surface is **subscription rows** (M-06) and **many `addlp` tick ranges** (bot, in-cap). `lps` never deletes rows. Token `open`/`transfer` RAM follows `eosio.token`. Live RAM: trader quota ~1.12 MB / used ~784 KB; token ~181 KB / ~158 KB.

---

## Table / scope design

| Table | Scope | PK | Comment |
|---|---|---|---|
| `config` `pending` | self | singleton | `pending` empty on live (good) |
| `tokens` | self | **symbol code only** | One contract per ticker; clash check on `credit` |
| `buckets` | **bucket name** | symbol code | `extended_asset` + `day`/`spent_today`; delisting does not migrate rows |
| `subs` | self | user | One plan per user |
| `lps` | self | auto id | Deduped by pool+ticks |
| `pools` `swaps` `dests` `splits` `plans` | self | id/name | |

`tokens` PK means you cannot whitelist two `USDT` contracts at once (good against fakes; painful if migrating wrappers).

---

## Suggested WASM patches (do **not** merge here)

ABI/table-compatible unless noted:

1. `require_auth2` admin `@active` and bot `@bot`.
2. Same-token `checkout` semantics (H-02) + tests.
3. `on_transfer`: `return` on unknown token; book unexpected swap-from assets.
4. `lpwithdraw`: bind bucket to LP origin or `unassigned`.
5. `setswap` updates `cfg.swap`; optional `payout` auth = admin.
6. `setlimits` hard ceiling; `setplan` min price; uint64 expiry.
7. Freeze mint / owner (H-01) — political as much as technical.

Any WASM change is an upgrade: **C-01 must be fixed first** or the same signer ships a drain instead.

---

## This PR’s non-breaking changes

- This report.
- JS `addlpAction` ABI field names + client test.
- Local `run_tests.sh` case documenting H-02 (expects revert).
- README pointer here.

No contract source, ABI, or table layout changes. Hashes above remain the mainnet hashes.

---

## Top risks (executive)

1. **`danishcrypto` is a super-admin** (owner weights, `smart.ass` authority, trader/token owner threshold 1, `require_auth` any perm). Upgrade + mint + whitelist are one key.
2. **80M SAT unissued** to that same control plane; supply is not frozen.
3. **Bot `@active` key bypasses `linkauth`** and can `payout` / grief-`addlp` within 1%/5% (and lock sizeable bucket slices for 42 years).
4. **Same-token buyback is dead code**; different-token buyback and Alcor lock **work** on mainnet.
5. **Docs describe a tighter system than is deployed** (tokens, pools, payout auth, 2-of-3, SAT discount, airdrop inventory).

Until owner permissions are actually 2-of-3 with no single weight-2 signer and contract owners are frozen, treat SAT and `smartatrader` as **custodial admin-key risk**, with Alcor lock as the only strong delay on the existing WAX/USDT LP slice.
