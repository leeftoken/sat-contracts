# Independent security review — SAT contracts (WAX mainnet)

**Date:** 23 October 2026 (Europe/Copenhagen)  
**Reviewer:** Cursor coding agent (source + live-chain review; not a bonded firm audit)  
**Code reviewed:** `sat.token/sat.token.cpp` (104 lines), `leeftrader/leeftrader.cpp` (382 lines), ABIs, tests, client helpers  
**Deployed accounts:** `sattoken1111`, `smartatrader`  
**Commit reviewed:** `3eaf730` (`main`)

This is a technical review of the deployed WASM-equivalent source. It is **not** a substitute for an Antelope-specialist firm audit before material TVL. Contract source was **not** patched: it currently builds byte-identical to mainnet, and changing it would break that claim.

## Verdict

The WASM is small, readable, and has real bot-side guards (token+contract whitelist, on-chain swap memos, `min_out` checkout, pool whitelist, deny-list, per-action/daily caps, Alcor `lockpos`). Those guards **do not bind the admin account**. Live permissions are weaker than the documented 2-of-3 story.

Do not treat this deployment as an immutable treasury until:

1. `danishcrypto` is removed from `smart.ass@active` (or `smart.ass@active` is raised so one nested signature cannot satisfy it).
2. `smartatrader@active` is reduced to `eosio.code` only (owner stays the msig for upgrades).
3. `payout` is gated on the admin msig, not the bot.
4. Remaining 80M SAT mint authority is locked or vested.

Current on-chain balances are small (about 7.1 WAX + dust USDT in the trader, plus a tiny locked WAX/USDT position). The SAT/LEEF launch LP (10M SAT) is locked on Alcor until 22 October 2068 (Europe/Copenhagen). Economic risk is dominated by **mint + upgrade + signer-key** power, not by a bot drain of the current buckets.

| Severity | Count |
|---|---|
| Critical | 1 |
| High | 4 |
| Medium | 5 |
| Low | 5 |
| Informational | 4 |

---

## Scope and method

**In scope (matches `AUDIT-SCOPE.md`):** `sattoken1111` / `sat.token`, `smartatrader` / `leeftrader`, public account permissions.

**Out of scope:** `swap.alcor` internals, `mocks/mockswap`, the off-chain bot, the website. Alcor was reviewed only at the ABI/docs/table boundary that leeftrader calls.

**Method:**

- Line-by-line review of both contracts against Antelope notify/inline semantics.
- ABI vs C++ action/table layout.
- Tests and client helpers (read; local nodeos suite not re-run here — environment has no CDT/Spring).
- Live WAX reads on 23 October 2026: code hashes, `get_account` permissions, leeftrader tables, Alcor `pools` / `positions` / `locks`, SAT `stat`.

**Reproducible-build check (live):**

| Account | Live `code_hash` | README claim |
|---|---|---|
| `sattoken1111` | `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` | match |
| `smartatrader` | `4ed7d02a7f7107dc03d9c9df61b77fd4927368ea95b669e0d0ba228f1ee729c4` | match |

---

## Live topology (23 Oct 2026, Europe/Copenhagen)

| Item | On-chain |
|---|---|
| Config | admin `smartaadmin1`, bot `smartabot111`, fee `smartafees11`, swap `swap.alcor`, unpaused, `max_in_bps=100`, `daily_cap_bps=500`, `lock_secs=1325419200` |
| Split | satlp 30 / leeflp 20 / ops 30 / rewards 10 / reserve 10 |
| Tokens | `4,SAT@sattoken1111`, `8,WAX@eosio.token`, `6,USDT@wrap.alcor` |
| Destinations | `smartafees11` only |
| Pools | `12405` only (WAX/USDT 0.3% on Alcor) |
| SAT | max 100,000,000.0000, supply 20,000,000.0000, issuer `smartaadmin1`; issuer balance 10,000,000.0000 SAT |
| Trader wallet vs buckets | **in sync**: 7.10000000 WAX and 0.000452 USDT both match bucket sums |
| leeftrader LP | Alcor pos **153124**, pool 12405, ticks `-105960..-93960`, liquidity 3640906, lock `unlockTime=3118144086` → **22 Oct 2068 17:08 CEST** |
| Launch LP (out of leeftrader, owned by fee msig) | pos **153128**, pool 12905 SAT/LEEF, full range, ~10M SAT, lock → **22 Oct 2068 17:19 CEST** |
| Alcor leftover deposit | 0.00000018 WAX on `swap.alcor/balances` for `smartatrader` |
| Bot `bot` permission | linkauth: `buyback`, `addlp`, `collectfees`, `lpwithdraw`, `pause` (**not** `payout`) |
| Bot `active`/`owner` | **different** key (`EOS6jqG…`) from the `bot` key (`EOS52jC…`) |

Plans 1–3 are WAX-priced; 11–13 are `wrap.alcor` USDT. There is no SAT-priced plan on-chain (the 20% SAT discount in `params.proposal.json` is not implemented in the contract).

---

## Trust model (read this first)

Antelope authorization is account-based. `leeftrader` can only constrain callers of **its own actions**. Anyone who can satisfy `smartatrader@active` can push `eosio.token::transfer`, `wrap.alcor::transfer`, `swap.alcor::subliquid`, `swap.alcor::withdraw`, etc. **without** going through caps, dest whitelist, or `pause`.

Today `smartatrader@active` is threshold 1 with:

- `smartaadmin1@active` weight 1
- `smartatrader@eosio.code` weight 1

So the admin msig (and, after C-01, a single signer) can empty the liquid treasury in one transfer. The 42-year story that *does* bind that principal is **Alcor `lockpos` on positions 153124 and 153128**, which blocks `subliquid` / `transferpos` even for the owner. Locked LP ≠ liquid buckets.

`require_auth(c.bot)` on `payout` / `buyback` is a bot-key check. It is not an admin-msig check.

---

## Findings

### C-01 — Documented 2-of-3 is 1-of-1 for `danishcrypto`

**Severity:** Critical  
**Component:** live permissions (`smartaadmin1`, `smartafees11`, `smart.ass`) — not WASM  
**Status:** confirmed on-chain

`smartaadmin1@active` / `owner` and `smartafees11@active` / `owner` are threshold 2 over:

- `danishcrypto@active` (weight 1)
- `leef@active` (weight 1)
- `smart.ass@active` (weight 1)

`smart.ass@active` (and `owner`) is **threshold 1** with two satisfiers: its own key **or** `danishcrypto@active` weight 1.

EOSIO evaluates nested permissions recursively. A transaction signed by `danishcrypto@active` therefore satisfies both `danishcrypto@active` and `smart.ass@active`, which is already 2 of 3.

**Impact:** `danishcrypto` alone can `setcode` both contracts, `issue` the remaining 80M SAT, `updateauth` the bot, whitelist destinations, and transfer every un-locked token on `smartatrader`. The deny-list that forbids trading accounts as bot/dest does not apply to this path.

**Fix (ops, no WASM change):**

1. Remove `danishcrypto@active` from `smart.ass@active` and `smart.ass@owner`, **or** set those to threshold 2 so the nested account cannot satisfy them alone.
2. Prefer dedicated signer accounts that never appear in other authorities.
3. After the topology is actually 2-of-3, print `get_account` for all three signers in the runbook and re-check after every `updateauth`.

---

### H-01 — Admin can bypass every leeftrader treasury guard

**Severity:** High (Critical if C-01 is unfixed, because it is then a single key)  
**Component:** `smartatrader@active`  
**Status:** confirmed on-chain

`smartaadmin1@active` is on `smartatrader@active`. That permission can authorize a raw token `transfer` from `smartatrader` to any account, ignoring `dests`, caps, pause, and buckets. It can also call `swap.alcor` as the position owner. Alcor `lockpos` still blocks removal of **locked** liquidity until 2068; liquid balances are unprotected.

**Fix:** set `smartatrader@active` to **only** `{"permission":{"actor":"smartatrader","permission":"eosio.code"},"weight":1}` with threshold 1. Leave `owner` on the (real) 2-of-3 for `setcode` / `updateauth`. Then funds can leave only through WASM. Residual: owner can still upgrade the code.

---

### H-02 — `payout` is bot-authorized; docs say admin

**Severity:** High  
**Component:** `leeftrader::payout`  
**Status:** confirmed in WASM + tests + live linkauth

```233:240:leeftrader/leeftrader.cpp
   [[eosio::action]] void payout(name bucket, name to, extended_asset quantity, string memo) {
      auto c = guard_bot();
      dests_t d(get_self(), get_self().value);
      check(d.find(to.value) != d.end(), "destination not whitelisted");
      if (to != c.fee_recipient) check_actor(to); // trading accounts only ever receive as the fee multisig
      spend(c, bucket, quantity);
      send_transfer(quantity, to, memo);
   }
```

`AUDIT-SCOPE.md` lists `payout` as **admin**. `DESIGN.md` lists it as **bot**. Tests push it as `-p botacct`. Live `smartabot111@bot` is **not** linkauth'd to `payout`, but `smartabot111@active` uses a **separate hot key** and `require_auth(c.bot)` accepts `@active`.

**Impact:**

- The 2-of-3 cannot pay out through the contract (wrong actor).
- The bot **active** key can pay out to any dest, with caps, and with an **arbitrary memo** (dangerous if a contract is ever `setdest`'d — e.g. `swap.alcor` would bypass buyback's memo builder).
- If that active key is lost, payout liveness dies until `setbot` / `updateauth`.

**Fix:** `require_auth(c.admin)` for `payout` (and keep dest whitelist + caps). Do not put `payout` on the bot permission. Store the bot `active`/`owner` keys in cold storage; only the linkauth'd `bot` key should be hot.

---

### H-03 — Same-token buyback measures net delta against a gross `min_out`

**Severity:** High (correctness / accounting; not a theft of current funds)  
**Component:** `buyback` + `checkout`  
**Status:** source; no successful same-token test exists

`buyback` snapshots `balance_of(min_out)` **before** the outgoing transfer, then `checkout` requires `now - before >= min_out`.

For A→B that is the gross output. For A→A:

- wallet delta = `out - in`
- the +1 rule requires `min_out >= in + 1` (documented as **gross** output)
- Alcor's memo also enforces gross `min_out`

So a documented same-token loop (`min_out = in + 1`) makes Alcor succeed and `checkout` fail (`out - in >= in + 1` ⇒ `out >= 2in + 1`). The whole tx reverts.

If a route ever did pay `out >= 2in + 1` and the bot used that as `min_out`, `checkout` would **credit `out - in` after already debiting `in`**, leaving `in` tokens in the wallet with no bucket row (desync). Tests only assert the `in + 1` rejection; they never complete a same-token swap.

**Fix:** snapshot after the outbound transfer, **or** for same-symbol compare `got + in >= min_out` and credit `got + in`. Add a test that actually round-trips WAX→WAX.

---

### H-04 — 80M SAT still mintable by the issuer

**Severity:** High (economic / rug surface)  
**Component:** `sat.token::issue`, issuer `smartaadmin1`  
**Status:** confirmed on-chain (`max_supply - supply = 80_000_000.0000 SAT`)

`issue` is standard (issuer-only, cannot exceed max). Combined with C-01, one signer can mint and dump. Allocation in `params.proposal.json` (team vest, community, etc.) is **not on-chain**.

**Fix:** mint the remaining allocation into vesting / lock contracts, then set issuer to `eosio.null` (or a contract that cannot `issue`). Until then, treat SAT as a msig-governed supply.

---

### M-01 — Anyone can overwrite another user's plan id

**Severity:** Medium  
**Component:** `handle_sub` (`sub:<plan>:<user>`)

A payer may buy a plan for any `user`. On an existing unexpired sub the contract **replaces** `plan` and extends `expires`. A cheap Basic payment can downgrade a Pro row. Off-chain gating that trusts `subs.plan` is griefable for the price of plan 11 (0.975000 USDT).

Expiry stacking can also walk `uint32` (see L-03).

**Fix:** if `from != user`, only extend expiry and do not lower `plan`; or restrict the `:user` form to admin.

---

### M-02 — `lpwithdraw` has no cap and trusts the bot's bucket

**Severity:** Medium  
**Component:** `lpwithdraw`  
**Status:** documented as open in `DESIGN.md`; confirmed

```217:222:leeftrader/leeftrader.cpp
   [[eosio::action]] void lpwithdraw(name bucket, extended_asset amount) {
      auto c = guard_bot();
      check(amount.quantity.amount > 0, "positive");
      action(permission_level{get_self(), "active"_n}, c.swap, "withdraw"_n, std::make_tuple(get_self(), amount)).send();
      credit(bucket, amount); // if Alcor's withdraw fails the whole tx reverts
   }
```

`spend` is not called. The bot chooses the bucket. Leftovers from a `satlp` `addlp` can be credited to `ops` and then `payout`'d (if dests allow) or `buyback`'d. Live leftover is 0.00000018 WAX.

**Fix:** credit the LP row's `bucket`; cap optional. Reject a bucket that is not the LP's.

---

### M-03 — Tokens arriving `from` a whitelisted swap are never booked

**Severity:** Medium (stuck funds / donation)  
**Component:** `on_transfer`

```140:141:leeftrader/leeftrader.cpp
      swaps_t s(get_self(), get_self().value);
      if (s.find(from.value) != s.end()) return;
```

Any transfer whose `from` is `swap.alcor` is ignored: swap outputs (booked later by `checkout`), Alcor refunds during `addlp`, and **third-party swaps that set recipient=`smartatrader`**. Those last two credit the wallet and not a bucket. There is no sweep action. Recovery today is H-01 (raw admin transfer) or a `setcode`.

`checkout` only books the `min_out` asset. Extra output tokens in the same swap are stuck.

**Fix:** a permissioned `creditfound` / `sweep` for unbooked balances; or treat unexpected swap inflows as `split_in` when no `pending` row exists.

---

### M-04 — No `setadmin`; `config.swap` is frozen at `init`

**Severity:** Medium (liveness / ops)  
**Component:** `init`, `setswap`, `addlp` / `collectfees` / `lpwithdraw`

`setswap` only edits the **whitelist** used by `buyback`. `addlp`, `lockpos`, `collect`, and `withdraw` always use `cfg.swap` from init (`swap.alcor`). Admin cannot be rotated without `setcode`. Lost admin account = permanent freeze of admin actions (bot can still pause and, today, payout).

**Fix:** `setadmin` (maybe with a delay); `setswapcontract` that updates `cfg.swap` after whitelist.

---

### M-05 — Client `addlp` payload does not match the ABI

**Severity:** Medium (bot integration; out of contract scope)  
**Component:** `client/leeftrader-client.mjs`

ABI / C++ fields are `tick_lower`, `tick_upper`. The client sends `tl`, `tu`. A bot using this helper will fail deserialize (or worse, if a custom serializer ignores unknown fields, ticks default to 0).

**Fix:** emit `tick_lower` / `tick_upper`. Add a client test against the ABI struct names.

---

## Low

### L-01 — Docs disagree with WASM and with each other

`AUDIT-SCOPE.md`: payout = admin; bot linkauth excludes payout.  
`DESIGN.md`: payout = bot; linkauth includes payout; `CONTRACT_ACCOUNT` paths still say `contracts/`.  
Default caps in the struct (5% / 20%) differ from mainnet (1% / 5%).

### L-02 — Incoming transfers are not pause-gated

`on_transfer` does not read `cfg.paused`. Unlisting tokens is the only halt. That is acceptable if pause means “bot stop”, not “refuse revenue”.

### L-03 — Subscription expiry can wrap `uint32`

`start + days * 86400` is uint32. About 80 stacked 366-day payments from now wrap expiry into 1970. Costly grief (~80 × cheapest plan), enabled by M-01's `:user` form. Saturate at `0xFFFFFFFF - 1`.

### L-04 — `setfeercpt` silently drops the old fee account from `dests`

If that account was also a general dest, it is erased. Re-`setdest` after rotation.

### L-05 — `settoken` primary key is symbol code only

One contract per symbol. Relisting a symbol under a new contract strands old-contract bucket rows (`symbol clash` / `wrong token contract`). Operational, not an attacker path unless admin does it.

---

## Informational (accepted / already documented)

### I-01 — Upgradeability

Both contract accounts are owned by `smartaadmin1`. `setcode` replaces every invariant. Alcor can also upgrade `lockpos`. Making `owner` = `eosio.null` after a real 2-of-3 and `active` = `eosio.code` only is the immutability end-state; it is a product decision.

### I-02 — Caps are bps of the bucket, not an absolute ceiling

As documented. At 1% / 5% a compromised bot (or H-01 admin) moving size *N* needs many txs / ~20 days via the **WASM** path. The raw-transfer path is immediate.

### I-03 — Bot grief on the one whitelisted pool

Pool 12405 is WAX/USDT. The bot cannot buy SAT on-chain today. It can still `buyback` with `min_out` of 1 unit and eat 1%/tx 5%/day of a bucket on that pool, or `addlp` with a worthless tick range (then Alcor-lock it). `pause` is the breaker; only admin unpauses.

### I-04 — `sat.token` matches modern `eosio.token`

`create` / `issue` (to issuer only) / `retire` / `transfer` / `open` / `close`, memo ≤ 256, no freeze. Wallets and Alcor will treat SAT as a normal token. No extra SAT-specific controls (and no on-chain vest).

---

## What is working

These were explicitly checked and look correct for the intended bot path:

- `on_notify("*::transfer")` uses `get_first_receiver()` and requires the **whitelisted contract + exact symbol** (`token not accepted`).
- Buyback memo is built on-chain: `swapexactin#<digits/commas>#<self>#<min_out@contract>#<now+300>`. Path injection (`#`, letters) is rejected. Recipient cannot be redirected. Matches [Alcor swap memo](https://docs.alcor.exchange/developers-api/amm-swap-contract-api).
- Output token must be the whitelisted **extended** symbol (fake SAT contract rejected in tests).
- `pending` singleton + inline `checkout` is atomic; overlapping buybacks abort; failed `min_out` reverts the spend.
- Different-token buyback accounting matches the wallet (live buckets == balances).
- Pool whitelist on `buyback` path and `addlp`.
- `addlp` requires `min_a, min_b > 0`, ticks ordered, lock ≤ uint32 max, then Alcor `lockpos`. Live pos 153124 **is** locked until 2068.
- Contract ABI has no `subliquid` / `transferpos`.
- Deny-list on bot and dest (not on admin/fee, by design).
- Split must total 10000 bps; last bucket gets dust; `__int128` cap math.
- Pause: bot can pause only; admin both ways.
- `collectfees` always uses `cfg.fee_recipient`.
- RAM for new `subs` rows is paid by the contract but requires a paid plan (~0.975 USDT or 213 WAX), so spam is not cheap relative to RAM.
- SAT cannot be issued above max; cannot issue to a non-issuer; retire burns issuer balance only.
- Admin and fee accounts are 2-of-3 **in their own JSON**, and `eosio.msig` 2-of-3 was tested on testnet — the break is the **nested** `smart.ass` authority (C-01), not the threshold field on `smartaadmin1`.

---

## Suggested WASM patches (for a later upgrade)

Do not apply these in this repo until you are ready to `setcode` (hashes will change).

1. **`payout`:** `require_auth(c.admin)` instead of `guard_bot()`.
2. **`checkout`:** if `in` and `min_out` share an extended symbol, require `got + in_amount >= min_out` and credit `got + in_amount`; snapshot after the outbound transfer is cleaner.
3. **`lpwithdraw`:** look up the latest LP for that asset / require `bucket` to match `lprow.bucket`.
4. **`handle_sub`:** do not write a lower `plan` for `user != from`; saturate expiry.
5. **`on_transfer`:** if `from` is a swap and `pending` is empty, `split_in` (or `credit` to `reserve`) instead of silent ignore.
6. **`setadmin` / `setswap` updating `cfg.swap`.**
7. Tests: successful same-token loop; nested-permission check is **off-chain** (`get_account` on signers).

---

## Immediate ops checklist (no `setcode` required)

1. **C-01:** `updateauth` on `smart.ass` so `danishcrypto` cannot satisfy it alone. Re-read all three signer accounts.
2. **H-01:** `smartatrader@active` → `eosio.code` only. Confirm inline `buyback` still works after.
3. **H-02:** move `smartabot111` owner/active keys to cold storage; keep only the linkauth'd `bot` key on the host. Do not `setdest` any contract that parses memos.
4. **H-04:** do not `issue` SAT until a written mint schedule exists; consider issuer → `eosio.null` after the airdrop mint.
5. Keep caps at 1%/5% until the items above are done.
6. Do not whitelist pool 12905 on leeftrader unless you intend the bot to trade the launch SAT/LEEF book.

---

## Test-suite gaps

`tests/run_tests.sh` covers deny-list, split, whitelist, memo injection, `min_out` revert, +1 rejection, fake-contract SAT, pool whitelist, lock via **mockswap**, fee recipient, dest whitelist, pause, per-action cap.

Missing: successful same-token buyback; `lpwithdraw` to the wrong bucket; `sub:<plan>:<other>`; nested `get_account` topology; raw `eosio.token::transfer` from the contract account as admin; Alcor `lockpos` against **real** `swap.alcor` (testnet Alcor has no `lockpos` — mainnet pos 153124 is the only live proof).
)
