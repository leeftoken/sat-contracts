# leeftrader (`smartatrader`)

## Purpose

On-chain treasury and automation guard for the LEEF / SAT system. Source: `leeftrader/leeftrader.cpp`. Live account: `smartatrader`.

The contract holds revenue (subscriptions and other accepted deposits), splits it into named buckets by configurable basis points, and lets **one** dedicated bot account run a narrow set of guarded actions:

- `buyback` — swap via a whitelisted DEX. The contract builds the memo itself, the recipient is always this contract, and an inline `checkout` reverts the whole transaction if output is below `min_out`.
- `addlp` — deposit into Alcor CLMM, mint a position owned by this contract, then lock it with Alcor's native `lockpos` (default 42 years). This contract has **no** `subliquid` action, so liquidity can never be removed through it.
- `collectfees` — Alcor `collect` on a locked position; fees go only to `cfg.fee_recipient`.
- `payout` — move bucket funds only to whitelisted destinations.
- `lpwithdraw` — pull leftover Alcor deposit credit back into a bucket (funds stay in the contract).
- `pause` — the bot may pause (for example a 10% drawdown stop); only admin may unpause.

Funds can never leave to a non-whitelisted account. Admin (the 2-of-3 multisig) sets configuration. **Not audited yet.** Live code hash `4ed7d02a7f7107dc03d9c9df61b77fd4927368ea95b669e0d0ba228f1ee729c4` matches a CDT 4.1.1 `-O3` rebuild of this repo.

A later, additive `setgov` patch is required before `smartastake1` can execute passed `setsplit` / `setlimits` votes. That patch is **not** in `leeftrader/` on `main` today. See [Governance patch](#governance-patch-setgov). Until it is applied, those two actions remain admin-only.

## Accounts and permissions

| Account | Role |
|---|---|
| `smartatrader` | Contract. `init`, `checkout`, and all `log*` actions require `self`. `active` must include `eosio.code` for inline transfers, Alcor calls, and self-logs. |
| `smartaadmin1` | `cfg.admin`. All `set*` configuration, `setplan`, unpause, and (with the bot) `collectfees`. 2-of-3: `danishcrypto`, `smart.ass`, `leef`. Signer rotation is described in [README.md](README.md#multisig-signer-management). |
| `smartabot111` | `cfg.bot`. `buyback`, `addlp`, `lpwithdraw`, `payout`, `collectfees`, and pause-only `pause`. Cannot be a trading account, cannot be the admin or fee recipient. |
| `smartafees11` | `cfg.fee_recipient`. Receives `collectfees`. Always a payout destination. Same 2-of-3 pattern as admin. |
| `swap.alcor` | Default `cfg.swap`, also listed in the `swaps` whitelist. |
| `smartastake1` | Planned payout destination for the `rewards` bucket (memo `reward`) and, after the patch, `gov` on `setsplit` / `setlimits`. |
| Trading accounts | Denied as bot or payout destination: `mywaxgallery`, `danishcrypto`, `qu.ui.wam`, `skgaw.wam`, `dustwizard42`, `niftywarlord`, `smart.ass`. They may be **signers** of the admin / fee multisigs. The fee multisig is the only payout target that can involve those names, indirectly. |

Recommended operational tightening (account permissions, not wasm): limit `smartabot111@active` with `linkauth` so it can only call the bot actions on this contract.

## Tables

All except `buckets` are scoped to `smartatrader`.

### `config` (singleton)

| Field | Type | Live / default |
|---|---|---|
| `admin` | `name` | `smartaadmin1` |
| `bot` | `name` | `smartabot111` |
| `fee_recipient` | `name` | `smartafees11` |
| `swap` | `name` | `swap.alcor` |
| `paused` | `bool` | `false` at last documented snapshot |
| `max_in_bps` | `uint16` | Live **100** (1% of a bucket per action). Defaults in source: 500. |
| `daily_cap_bps` | `uint16` | Live **500** (5% of a bucket per UTC day). Defaults in source: 2000. |
| `lock_secs` | `uint32` | `1325419200` (42 × 365.25 days). Minimum allowed: 1 year. |

After the `setgov` patch a trailing `gov` field (`binary_extension<name>`) may be appended. See below.

```bash
cleos -u https://wax.greymass.com get table smartatrader smartatrader config
```

### `splits`

| Field | Type | Meaning |
|---|---|---|
| `bucket` | `name` | Primary key. Live order by `ord`: `satlp`, `leeflp`, `ops`, `rewards`, `reserve`. |
| `bps` | `uint16` | Basis points. Live: 3000 / 2000 / 3000 / 1000 / 1000. Must sum to 10,000. |
| `ord` | `uint64` | Insertion order. The last bucket absorbs rounding dust. |

### `tokens`

Whitelisted `extended_symbol` (`symbol` + contract). Incoming transfers of any other token revert. Primary key is the symbol code (one row per symbol; a symbol cannot be listed under two contracts). Live as of 2026-11-14: `4,SAT@sattoken1111`, `8,WAX@eosio.token`, `4,LEEF@leefmaincorp`, `6,USDT@wrap.alcor`, `6,WAXUSDC@eth.token`, `8,WAXWBTC@eth.token`.

### `swaps`

Whitelisted swap contracts. Incoming transfers **from** a listed swap are ignored (not counted as revenue); proceeds are accounted by `checkout` / `lpwithdraw` instead.

### `dests`

Whitelisted payout destinations. `fee_recipient` is always inserted at `init` and again by `setfeercpt`. Live as of 2026-11-14: `smartafees11` only. `smartastake1` is not listed yet (and that account does not exist).

### `pools`

Alcor pool ids the bot may route through or add liquidity to. Operators should treat the live table as the source of truth. The project's published SAT pool set has included `12405`, `12905`, `12925`, `13230`, `13231`, `13235`, `13239`. Pools `13238` and `13234` must not be added.

### `plans` and `subs`

Subscription plans (`id`, `price` as `extended_asset`, `days` 1..366, `active`) and per-user subscriptions (`user`, `plan`, `expires`). Paying `sub:<id>[:<user>]` extends from `max(now, current expiry)`.

### `buckets` (scope = bucket name)

| Field | Type | Meaning |
|---|---|---|
| `balance` | `extended_asset` | Token + contract held in that bucket. |
| `day` | `uint32` | UTC day number (`now / 86400`) of `spent_today`. |
| `spent_today` | `int64` | Amount already spent this UTC day, used with `daily_cap_bps`. |

```bash
cleos get table smartatrader satlp buckets
cleos get table smartatrader rewards buckets
```

### `pending` (singleton)

Set during `buyback`, cleared by `checkout`. Fields: `bucket`, `min_out`, `before` (output-token balance snapshot). Blocks overlapping buybacks.

### `lps`

Locked positions this contract recorded: `id`, `pool_id`, `tick_lower`, `tick_upper`, `unlock`, `bucket`. As of 2026-11-14 this table has one row (pool 12405, bucket `satlp`, unlock unix `3118144086`). Locked SAT/LEEF position **153128** is owned by `smartafees11` and lives in `swap.alcor` (`positions` scope `12905`, `locks` scope `swap.alcor`). It is not a `smartatrader` `lps` row, because it was not opened through `addlp`.

## Actions

In the examples, admin actions are signed by `smartaadmin1@active` (2-of-3). Bot actions are signed by `smartabot111`. Replace RPC and amounts as needed. JS payloads follow the same shape as `client/leeftrader-client.mjs`.

### `init(admin, bot, fee_recipient, swap_contract)`

- **Auth:** `smartatrader`.
- **Params:** existing `admin` and `fee_recipient`; `bot` passes `check_actor` and must differ from admin and fee recipient; `swap_contract` must exist.
- **Effects:** Writes `config` (paused false, default caps 500 / 2000 unless later changed, default lock 42 years). Inserts `fee_recipient` into `dests` and `swap_contract` into `swaps`. Once only.
- **Errors:** `already initialised`, `account does not exist`, `trading accounts may not hold contract roles`, `bot and fee recipient must differ`, `admin and bot must differ`, `swap contract missing`.
- **Status:** Already executed on mainnet.

### `setlimits(max_in_bps, daily_cap_bps, lock_secs)`

- **Auth:** admin (and, after the patch, `gov` as well).
- **Params:** `max_in_bps` and `daily_cap_bps` in 1..10000; `lock_secs >= 365 * 86400`; `now + lock_secs` must fit in `uint32`.
- **Effects:** Updates the three config fields. Governance, when enabled, passes the **current** `lock_secs` through and must not shorten the Alcor lock. Stake-side bounds are tighter than these (see [smartastake.md](smartastake.md#whitelisted-proposal-kinds)).
- **Errors:** `max_in_bps 1..10000`, `daily_cap_bps 1..10000`, `lock too short`, `lock past uint32 max (2106)`, `not initialised`.

```bash
# Live values as of the last documented snapshot (1% / 5% / 42y). Admin only until setgov.
cleos push action smartatrader setlimits '{"max_in_bps":100,"daily_cap_bps":500,"lock_secs":1325419200}' -p smartaadmin1@active
```

### `setbot(bot)`

- **Auth:** admin.
- **Params:** `bot` passes `check_actor` and differs from admin and fee recipient.
- **Effects:** Replaces `cfg.bot`.
- **Errors:** `trading accounts may not hold contract roles`, `account does not exist`, `bot must differ from admin and fee recipient`.

```bash
cleos push action smartatrader setbot '{"bot":"smartabot111"}' -p smartaadmin1@active
```

### `setfeercpt(fee_recipient)`

- **Auth:** admin.
- **Params:** existing account, not the bot.
- **Effects:** Updates `cfg.fee_recipient`, removes the previous fee recipient from `dests` if present, and ensures the new one is listed.
- **Errors:** `account does not exist`, `fee recipient must differ from bot`.

### `setsplit(buckets, bps)`

- **Auth:** admin (and `gov` after the patch).
- **Params:** 1..10 buckets; parallel `bps` vectors of equal length; no duplicates; sum of `bps` is exactly 10000. Last entry absorbs dust on later splits of incoming funds.
- **Effects:** Erases previous split rows and writes the new ones with `ord` = index.
- **Errors:** `bad split`, `split must total 10000 bps`, `duplicate bucket`.

```bash
cleos push action smartatrader setsplit '{"buckets":["satlp","leeflp","ops","rewards","reserve"],"bps":[3000,2000,3000,1000,1000]}' -p smartaadmin1@active
```

```js
{ account: 'smartatrader', name: 'setsplit',
  authorization: [{ actor: 'smartaadmin1', permission: 'active' }],
  data: { buckets: ['satlp','leeflp','ops','rewards','reserve'], bps: [3000,2000,3000,1000,1000] } }
```

### `settoken(token, allowed)` / `setswap(contract, allowed)` / `setdest(account, allowed)` / `setpool(pool_id, allowed)`

- **Auth:** admin.
- **Params:** `allowed` true inserts, false erases. `settoken` takes an `extended_symbol`. `setswap` requires the contract account to exist when adding. `setdest` runs `check_actor`. `setpool` takes a `uint64` Alcor pool id.
- **Effects:** Maintain the corresponding whitelist. `settoken` error `symbol already listed` / `not listed`. `setswap`: `no account` when adding a missing contract.
- **Errors (dest):** `trading accounts may not hold contract roles`, `account does not exist`.

```bash
cleos push action smartatrader setpool '{"pool_id":12905,"allowed":true}' -p smartaadmin1@active
cleos push action smartatrader setdest '{"account":"smartastake1","allowed":true}' -p smartaadmin1@active
```

```js
{ account: 'smartatrader', name: 'settoken',
  authorization: [{ actor: 'smartaadmin1', permission: 'active' }],
  data: { token: { contract: 'sattoken1111', symbol: '4,SAT' }, allowed: true } }
```

The exact `extended_symbol` JSON shape can vary by serializer (some stacks want `"4,SAT"` plus `contract`). Prefer ABI-driven serialization.

### `setplan(id, price, days, active)`

- **Auth:** admin.
- **Params:** `price.quantity.amount > 0`; `days` in 1..366. Creates or updates that plan id.
- **Errors:** `bad plan`.

```bash
# Example shape. Live USDT plans are listed in params.proposal.json (wrap.alcor, 6 decimals).
cleos push action smartatrader setplan '{"id":1,"price":{"quantity":"0.975000 WAX","contract":"eosio.token"},"days":30,"active":true}' -p smartaadmin1@active
```

### `pause(actor, paused)`

- **Auth:** `actor`, who must be the admin, or the bot **and** `paused == true`.
- **Effects:** Sets `cfg.paused`. While paused, `guard_bot()` rejects `buyback`, `addlp`, `lpwithdraw`, and `payout` with `paused`. `collectfees` does not use `guard_bot` and still works for admin or bot.
- **Errors:** `not allowed`.

```bash
cleos push action smartatrader pause '{"actor":"smartabot111","paused":true}' -p smartabot111@active
cleos push action smartatrader pause '{"actor":"smartaadmin1","paused":false}' -p smartaadmin1@active
```

```js
import { pauseAction, drawdownGuard } from './client/leeftrader-client.mjs';
pauseAction({ actor: 'smartabot111' });
drawdownGuard({ actor: 'smartabot111', peak, equity, limit: 0.10 });
```

### `buyback(bucket, in, swap, pool_path, min_out)`

- **Auth:** bot, and not paused.
- **Params:** `swap` is in `swaps`; `in` and `min_out` amounts > 0; `pool_path` is digits and commas, length 1..64, no trailing comma, every id in `pools`; output token is in `tokens`. If input and output are the same extended symbol, `min_out` must be at least `in + 1` unit.
- **Effects:** Spends `in` from `bucket` under the per-action and daily caps, records `pending`, transfers `in` to `swap` with an Alcor `swapexactin#…` memo that the **contract** builds (recipient is always `smartatrader`, deadline `now+300`), then inline `checkout`.
- **Errors:** `swap contract not whitelisted`, `amounts must be positive`, `pool_path must be digits and commas`, `same-token loop needs min_out >= in + 1 unit`, `output token not whitelisted`, `bad pool id`, `pool not whitelisted`, `bad pool path`, `swap already pending`, plus spend errors `bucket has no such token`, `wrong token contract`, `bucket balance too low`, `exceeds per-action cap`, `exceeds daily cap`.

```bash
cleos push action smartatrader buyback '{
  "bucket":"satlp",
  "in":{"quantity":"10.00000000 WAX","contract":"eosio.token"},
  "swap":"swap.alcor",
  "pool_path":"12905",
  "min_out":{"quantity":"1.0000 SAT","contract":"sattoken1111"}
}' -p smartabot111@active
```

```js
import { buybackAction } from './client/leeftrader-client.mjs';
buybackAction({
  actor: 'smartabot111', bucket: 'satlp',
  inAsset: { quantity: '10.00000000 WAX', contract: 'eosio.token' },
  swap: 'swap.alcor', poolPath: '12905',
  minOut: { quantity: '1.0000 SAT', contract: 'sattoken1111' }
});
```

### `checkout()`

- **Auth:** `smartatrader` only (inline from `buyback`).
- **Effects:** Reads the output-token balance, requires `got = now - before >= min_out`, credits that `got` amount to the same bucket, clears `pending`, logs `logswap`.
- **Errors:** `nothing pending`, `output below min_out, reverting` (the whole buyback, including the swap, rolls back).

Do not call this from a wallet.

### `addlp(bucket, pool_id, a, b, tick_lower, tick_upper, min_a, min_b)`

- **Auth:** bot, not paused.
- **Params:** both input amounts > 0; `tick_lower < tick_upper`; `pool_id` whitelisted; `min_a` and `min_b` > 0. Deadline on Alcor `addliquid` is `now+300`.
- **Effects:** Spends both assets from `bucket`, transfers them to `cfg.swap` with memo `deposit`, calls `addliquid`, then `lockpos` until `now + cfg.lock_secs` (must fit in `uint32`). Records or extends the `lps` row. Logs `loglp`.
- **Errors:** `amounts must be positive`, `bad ticks`, `pool not whitelisted`, `min amounts must be positive`, `lock past uint32 max`, plus spend errors.

```bash
cleos push action smartatrader addlp '{
  "bucket":"satlp","pool_id":12905,
  "a":{"quantity":"100.0000 SAT","contract":"sattoken1111"},
  "b":{"quantity":"1000.0000 LEEF","contract":"leefmaincorp"},
  "tick_lower":-887220,"tick_upper":887220,
  "min_a":"99.0000 SAT","min_b":"990.0000 LEEF"
}' -p smartabot111@active
```

ABI field names are `tick_lower` / `tick_upper`. Wrong names serialize as 0,0 and the action reverts. See `client/leeftrader-client.mjs`.

### `lpwithdraw(bucket, amount)`

- **Auth:** bot, not paused.
- **Params:** positive `extended_asset`.
- **Effects:** Calls `cfg.swap::withdraw` to this contract, then credits `bucket`. If Alcor's withdraw fails, the whole transaction reverts. The bot chooses the bucket label; funds never leave the contract.
- **Errors:** `positive`, plus Alcor errors.

### `collectfees(actor, lp_id, max_a, max_b)`

- **Auth:** `actor`, who must be the bot or the admin. Does **not** check `paused`.
- **Params:** existing `lps` id; `max_a` / `max_b` passed through to Alcor `collect`. Recipient is always `cfg.fee_recipient`.
- **Errors:** `not allowed`, `unknown lp`.

```bash
cleos push action smartatrader collectfees '{"actor":"smartabot111","lp_id":0,"max_a":"1000000.0000 SAT","max_b":"1000000.0000 LEEF"}' -p smartabot111@active
```

### `payout(bucket, to, quantity, memo)`

- **Auth:** bot, not paused.
- **Params:** `to` is in `dests`. If `to` is not the fee recipient, `check_actor(to)` also applies. Amount spent from `bucket` under caps.
- **Effects:** Transfers `quantity` to `to` with the given memo.
- **Errors:** `destination not whitelisted`, `trading accounts may not hold contract roles`, plus spend errors.

To send staking rewards once `smartastake1` is live and listed in `dests`:

```bash
cleos push action smartatrader payout '{
  "bucket":"rewards",
  "to":"smartastake1",
  "quantity":{"quantity":"10.0000 SAT","contract":"sattoken1111"},
  "memo":"reward"
}' -p smartabot111@active
```

Use memo `reward` (or any memo: after the staking review fix, SAT from the configured trader is always treated as reward funding). List `smartastake1` in `dests` first (`setdest`).

### Log actions

`logrevenue`, `logsub`, `logswap`, `loglp` — `require_auth(get_self())` only, for Hyperion. The JS helper `logs()` in `client/leeftrader-client.mjs` reads them.

## Notify handlers and memo formats

`[[eosio::on_notify("*::transfer")]] on_transfer`:

1. Ignore unless `to` is this contract and `from` is not this contract.
2. Ignore if `from` is in `swaps` (swap / LP proceeds).
3. Otherwise the token must be in `tokens` with matching contract and symbol, amount > 0.
4. If memo starts with `sub:`, handle a subscription (`sub:<plan_id>[:<user>]`, user defaults to payer). Plan must be active, token must match, payment must be at least the price. Expiry extends from now or from the current future expiry. Emits `logsub`.
5. Every accepted inbound amount (including subscription payments) is then `split_in` across the configured buckets. Emits `logrevenue`.

| Memo | Effect |
|---|---|
| `sub:<plan_id>` | Subscribe the payer. |
| `sub:<plan_id>:<user>` | Subscribe `user` (must exist). `plan_id` is digits, length 1..18. |
| anything else (or empty) | Revenue only; still split. |
| transfers from a whitelisted swap | Ignored, not split. |

**Errors:** `token not accepted`, `zero`, `bad plan id`, `user account missing`, `unknown plan`, `plan inactive`, `wrong token for plan`, `underpaid`, `split not set`, `symbol clash across contracts`.

Alcor memos this contract **sends** (not user-supplied):

- Buyback: `swapexactin#<pool_path>#smartatrader#<min_out>@<contract>#<deadline>`.
- LP deposit: `deposit`.

## Parameters and bounds

| Parameter | Source bounds | Live / planned |
|---|---|---|
| `max_in_bps` | 1..10000 | Live 100. Staking governance, once patched, will only allow 50..500 and `max_in <= daily`. |
| `daily_cap_bps` | 1..10000 | Live 500. Governance 100..2000. |
| `lock_secs` | ≥ 1 year, `now + lock` ≤ uint32 max | 1,325,419,200 s (~42 years, unlock around 2068). Governance cannot change this. |
| Split | 1..10 buckets, sum 10000 | Live 30/20/30/10/10. Governance (once patched) also enforces each bucket 0..5000, `satlp >= 1000`, `ops <= 4000`, `rewards >= 500`. |
| Plan duration | 1..366 days | See `params.proposal.json`. |
| Pool path | digits and commas, ≤ 64 chars | Every id must be whitelisted. |
| Swap deadline | `now + 300` seconds | Built by the contract. |
| Same-token buyback | `min_out >= in + 1` unit | Required by wasm and by the JS client. |

## Fees and splits

Incoming accepted tokens are split by `setsplit`. Live:

| Bucket | bps | Typical use |
|---|---|---|
| `satlp` | 3000 | Buy SAT or add SAT LP, then lock. |
| `leeflp` | 2000 | LEEF-side LP. |
| `ops` | 3000 | Operations payouts to whitelisted dests. |
| `rewards` | 1000 | Payout to `smartastake1` once listed. |
| `reserve` | 1000 | Reserve. |

Dust from integer division goes to the last bucket in `ord`. Caps apply per bucket, per token, per action and per UTC day, using 128-bit math:

```
amount * 10000 <= balance * max_in_bps
spent_today_including_this * 10000 <= (balance + already_spent_today) * daily_cap_bps
```

Alcor trading fees on locked positions are collected to `smartafees11`, not split again by this contract. After B3, `smartafees11` can forward SAT to `smartastake1` as a funder.

Bot strategy knobs `sat_lp_fee_share_bps` (approved start 50%) and `buyback_share_bps` (approved start 0%) live on `smartastake1` config, not in this ABI. Bots should read them there once staking is deployed.

## Governance patch (`setgov`)

Today `setsplit` and `setlimits` are `require_auth(c.admin)` only. `smartastake1` executes passed votes as `smartastake1@eosio.code`. Without a patch those inlines fail with `missing authority of smartaadmin1`.

The safe patch (see [smartastake/docs/TRADER_PATCH.md](../smartastake/docs/TRADER_PATCH.md) and `integration/make_trader_patches.py`):

- Append `eosio::binary_extension<name> gov` at the **end** of the config struct. A plain `name gov` makes every `cfg()` read fail with `datastream attempted to read past the end` and bricks `setlimits`, `setsplit`, `pause`, `payout`, `buyback`, `addlp`, and `collectfees` until the code is rolled back. This was reproduced against the live wasm on a local chain.
- Add `setgov(gov)` (admin only).
- On `setsplit` and `setlimits` **only**, accept admin **or** the configured `gov`. Do not extend this to `setbot`, `setfeercpt`, or the whitelist setters.

One-time, after `smartastake1` is deployed and its `eosio.code` is on its active authority:

```bash
cleos push action smartatrader setgov '{"gov":"smartastake1"}' -p smartaadmin1@active
```

Apply this (batch B5, planned patched wasm sha256 `0a48f389ed1a921b93393278102cc63ac83ba77e87e21352e63ee158581fabb4`) before governance queues trader changes. Queued proposals expire 14 days after `eta`, so a forgotten patch will not leave a stale split hanging forever, but the vote will still have been wasted.

The patch is generated; it is not committed in `leeftrader/` on this branch. Review the generated source before any `setcode`.

## Security model and known limits

**Not audited yet.** The 2026-11-09 source + mainnet review (`AUDIT-REPORT.md`) confirmed this source matches the live wasm. Honest limits and findings that are about contract behaviour:

- The bot is powerful within the whitelist and caps, but it cannot send funds to a non-destination or remove locked LP through this contract. Admin still controls whitelists, the bot identity, and unpause.
- `lpwithdraw` trusts the bot's bucket label. Funds stay inside the contract; only the internal label moves. A mistaken label can over/under-state a bucket until it is corrected by later credits.
- Transfers of unexpected tokens from a whitelisted swap are left unaccounted (they are ignored by `on_transfer`).
- Same-token buybacks are a known review item: `checkout` measures the output as a balance delta against a snapshot taken **before** the input leaves the contract. The wasm requires `min_out >= in + 1` on same-token loops; operators should avoid same-token routes unless they fully understand that accounting, and should keep `min_out` conservative.
- Overlapping buybacks are blocked by the `pending` singleton.
- Subscription rows are paid by the contract; spam costs at least the plan price.
- Apply the `binary_extension` form of the gov patch only. Never set code that adds a plain `name gov`.
- SAT from this contract that lands on `smartastake1` is always reward funding, so an empty-memo payout cannot create a stuck trader stake row (staking review fix, PR #5). Still prefer memo `reward` and a positive, simulated `min_out` / payout size.

Operational rules that the wasm does not itself encode, but that the bot is expected to obey: simulate first, keep price impact low, no wash trading, no trades between own wallets, 10% drawdown pause, no trading actions triggered from Telegram.

## User guide

Users do not call admin or bot actions.

- **Pay a subscription:** transfer the plan token to `smartatrader` with memo `sub:<plan_id>` (or `sub:<plan_id>:<user>`). Confirm the plan in the `plans` table first (id, price, token, days, `active`).
- **Read buckets, splits, pause, and caps:**

```bash
cleos -u https://wax.greymass.com get table smartatrader smartatrader config
cleos -u https://wax.greymass.com get table smartatrader smartatrader splits
cleos -u https://wax.greymass.com get table smartatrader satlp buckets
```

- **Stake, vote, or claim staking rewards** once `smartastake1` is live: see [smartastake.md](smartastake.md#user-guide). This contract only forwards SAT there via `payout`.

## Operator guide

1. Keep `dests`, `tokens`, `swaps`, and `pools` tight. Add `smartastake1` as a destination before any rewards payout. Never whitelist pools 13238 or 13234.
2. After staking is initialized, prefer payouts to `smartastake1` with memo `reward`, in frequent small transfers rather than rare lumps.
3. `addlp` always locks for `lock_secs`. There is no unlock path through this contract. Confirm ticks, mins, and pool id on a simulation first.
4. Collect fees periodically; they go to `smartafees11`. After B3, forward SAT from that account to `smartastake1`.
5. If the 10% drawdown guard fires, the bot pauses itself. Only `smartaadmin1` can unpause.
6. Do not `setcode` the gov patch until it has been reviewed and the audit of the staking system is done. Then B5: patched wasm + `setgov`.
7. Signer leaks on the admin or fee multisig are handled by a 2-of-3 `updateauth`, as in the [system overview](README.md#multisig-signer-management).

## Deploy and launch order

`smartatrader` is already live (deployed 2026-10-23) with the caps and split above. Remaining work that touches this contract is B5 (gov patch + `setgov`) and listing `smartastake1` as a payout destination, after the external audit and after `smartastake1` itself is deployed (B0–B3). Signing those steps is not approved by this document.

Build the current (unpatched) contract:

```bash
CDT_BIN=/path/to/cdt/usr/bin ./build.sh
sha256sum build/leeftrader.wasm   # must match 4ed7d02a…29c4
```

Local tests: `tests/run_tests.sh`. Client unit tests: `node client/leeftrader-client.test.mjs`. Integration of the safe patch against this wasm: `integration/run.sh`.
