# smartatrader / leeftrader patch (model A)

`smartastake` executes passed `setsplit` and `setlimits` proposals as an inline action from `smartastake1@eosio.code`.

Today those two actions are `require_auth(c.admin)` only (`smartaadmin1`). Without this patch, governance can only store local bot knobs and a guardian would have to push the trader action by hand (model B). This repo implements **model A** and tests it against `trader/trader.cpp`.

Do not extend the new auth path to `setbot`, `setfeercpt`, or the whitelist setters (`setpool`, `settoken`, `setswap`, `setdest`, `setplan`).

## Live config prefix (ABI snapshot 2026-11-09)

Flat `config` row, not a singleton wrapper. Field order:

| Field | Type | Live value at snapshot |
|---|---|---|
| `admin` | `name` | `smartaadmin1` |
| `bot` | `name` | `smartabot111` |
| `fee_recipient` | `name` | `smartafees11` |
| `swap` | `name` | `swap.alcor` |
| `paused` | `bool` | `0` |
| `max_in_bps` | `uint16` | `100` |
| `daily_cap_bps` | `uint16` | `500` |
| `lock_secs` | `uint32` | `1325419200` |

Append `gov` **at the end**, and declare it as `eosio::binary_extension<name>`, **not** a plain `name`. The live row on `smartatrader` was written without `gov`. `cfg_t` is a singleton, and a plain trailing `name` makes every `cfg()` read on the upgraded contract fail with `datastream attempted to read past the end`. That would brick `setlimits`, `setsplit`, `pause`, `payout`, `buyback`, `addlp`, and `collectfees` until the code is rolled back. `integration/run.sh` shows this on a local chain with the repo's real `leeftrader` wasm (same hash as mainnet `smartatrader`). With `binary_extension` the old row reads with `gov` absent, and the first `set()` after `setgov` writes it. `smartastake` reads only this prefix (it does not define `gov` on its reader), so a trailing field is ignored. `execute` of `setlimits` copies `lock_secs` from that read and passes it through unchanged. Governance must not change the Alcor lock length.

Live split order, which `setsplit` proposals must keep: `satlp`, `leeflp`, `ops`, `rewards`, `reserve`.

## Code change

```cpp
#include <eosio/binary_extension.hpp>

// config table, after lock_secs. binary_extension is required: the live row has no gov bytes.
eosio::binary_extension<name> gov;

[[eosio::action]] void setgov(name gov) {
   auto c = cfg(); require_auth(c.admin);
   check(gov.value == 0 || is_account(gov), "gov account missing");
   c.gov = gov; cfg_t(get_self(), get_self().value).set(c, get_self());
}

// in setsplit and setlimits only, in place of require_auth(c.admin):
check(has_auth(c.admin) ||
      (c.gov.has_value() && c.gov.value().value != 0 && has_auth(c.gov.value())),
      "missing authority of admin or gov");
```

`integration/make_trader_patches.py` generates exactly this patch (and the broken plain-`name` variant, for the test) from `leeftrader/leeftrader.cpp` without touching that file.

Use that check at the start of `setsplit` and `setlimits` **only**.

One-time, from the trader admin, after `smartastake1` is deployed and its `eosio.code` permission is on its active authority:

```text
cleos push action smartatrader setgov '{"gov":"smartastake1"}' -p smartaadmin1
```

That is an operational step. This repo does not contain keys and does not send it.

## Inline action signatures the stake contract sends

These must match the live ABI:

```cpp
void setsplit(std::vector<name> buckets, std::vector<uint16_t> bps);
void setlimits(uint16_t max_in_bps, uint16_t daily_cap_bps, uint32_t lock_secs);
```

`smartastake` sends:

```text
setsplit(
  ["satlp", "leeflp", "ops", "rewards", "reserve"],
  [satlp_bps, leeflp_bps, ops_bps, rewards_bps, reserve_bps]
)

setlimits(max_in_bps, daily_cap_bps, lock_secs_read_from_trader_config)
```

The stake contract also enforces its own hard bounds before the inline call (each bucket 0..5000, `satlp >= 1000`, `ops <= 4000`, `rewards >= 500`, sum 10000; `max_in` 50..500, `daily` 100..2000, `max_in <= daily`). The trader should keep its own sum check (`10000`) so a direct admin call cannot brick the split.

## What this patch does not do

- It does not let `gov` call `setbot` or `setfeercpt`.
- It does not let governance rotate admin, code, pools, tokens, the swap contract, destinations, or plans.
- It does not change `lock_secs` unless admin calls `setlimits` themselves and passes a new value. The stake contract never supplies a new lock.
- Bot fee-split knobs (`sat_lp_fee_share_bps`, `buyback_share_bps`) stay on `smartastake` config. Bots read them there.

The local mock in `mocks/trader/trader.cpp` is deployed fresh, so it never exercises the upgrade of an existing row. Use `integration/run.sh` for the upgrade path.

Until this patch is live, a passed `setsplit` or `setlimits` proposal cannot execute on `smartatrader` (`missing authority of smartaadmin1`). With the review fix, a queued proposal expires 14 days after its `eta`, so apply the patch and `setgov` before governance starts queueing trader changes.
