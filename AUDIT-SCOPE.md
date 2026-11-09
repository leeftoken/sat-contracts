# Audit scope — SAT contracts (WAX mainnet)

Status: deployed with low caps. Source in this repo builds byte-identical to the on-chain wasm (CDT 4.1.1). **Human source+mainnet review:** [AUDIT-REPORT.md](AUDIT-REPORT.md) (2026-11-09). This file is the pre-audit scope sheet; several live tables (tokens, pools, SAT inventory, owner keys) have drifted — the report’s “Live snapshot” is authoritative.

## In scope
| Contract | Account | Source | Code sha256 |
|---|---|---|---|
| SAT token (eosio.token derivative) | `sattoken1111` | `sat.token/sat.token.cpp` (104 lines) | `e1702842…7dbc` |
| Treasury / buyback / LP manager ("leeftrader") | `smartatrader` | `leeftrader/leeftrader.cpp` (382 lines) | `4ed7d02a…29c4` |

Out of scope: Alcor `swap.alcor` (external), `mocks/mockswap` (tests only), the off-chain bot, the website.

## Accounts and permissions (public)
- `smartaadmin1`: admin; 2-of-3 multisig over `danishcrypto@active`, `smart.ass@active`, `leef@active` (weight 1 each).
- `smartafees11`: fee recipient; same 2-of-3.
- `smartatrader` (owner + active) and `sattoken1111` (owner + active) are controlled by `smartaadmin1@active`; `smartatrader@active` also has `smartatrader@eosio.code` for inline actions.
- `smartabot111`: bot account; a separate `bot` permission is linkauth'd to `smartatrader::{buyback, addlp, collectfees, lpwithdraw, pause}` only.
- Trading accounts (`mywaxgallery`, `danishcrypto`, `qu.ui.wam`, `skgaw.wam`, `dustwizard42`, `niftywarlord`, `smart.ass`) are hard-coded as banned from holding bot/contract/destination roles (`check_actor`).

## sat.token actions
`create` (self), `issue` (issuer, to issuer only, ≤ max supply), `retire` (issuer), `transfer`, `open`, `close`.
SAT: precision 4, max supply 100,000,000.0000, issuer `smartaadmin1`. Issued 20,000,000 (10M in the SAT/LEEF LP pool 12905, 10M held by `smartaadmin1` for the airdrop). The remaining 80M can be minted by the multisig at any time.

## leeftrader actions
| Action | Auth | Purpose |
|---|---|---|
| `init` | self | one-time config (admin, bot, fee recipient, swap) |
| `setlimits` | admin | `max_in_bps`, `daily_cap_bps`, `lock_secs` (≥ MIN_LOCK_SECS, ≤ uint32 time) |
| `setbot`, `setfeercpt` | admin | rotate roles |
| `setsplit` | admin | revenue split across buckets (sum 10000 bps, ≤10 buckets) |
| `settoken`, `setswap`, `setdest`, `setpool`, `setplan` | admin | whitelists and subscription plans |
| `pause` | admin (both ways) / bot (pause only) | circuit breaker |
| `on_notify *::transfer` | — | incoming payments: whitelisted tokens only; plan memo → subscription; split into buckets |
| `buyback` | bot | swap from a bucket via whitelisted swap + pools, `min_out` required; same-token loop needs `min_out ≥ in + 1` |
| `checkout` | self (inline) | verifies swap output ≥ `min_out`, reverts otherwise |
| `addlp` | bot | add Alcor CLMM liquidity on whitelisted pool and lock it (`lockpos`) for `lock_secs` |
| `lpwithdraw` | bot | return leftover deposit to bucket |
| `collectfees` | bot or admin | collect LP fees to fee recipient |
| `payout` | admin | pay from bucket to whitelisted destination |
| `logrevenue`, `logsub`, `logswap`, `loglp` | self | inline logs |

## Caps (current on-chain)
- `max_in_bps 100` (1% of bucket per action), `daily_cap_bps 500` (5% of bucket per day per bucket).
- `lock_secs 1325419200` (~42 years).
- Split: satlp 30 / leeflp 20 / ops 30 / rewards 10 / reserve 10 %.
- Whitelists: tokens WAX, USDT (wrap.alcor), SAT; swap `swap.alcor`; pool 12405 only.

## Fee / value flows
1. User pays a plan → contract splits into buckets.
2. Bot `buyback` (capped) → `addlp` → LP position locked ~42y at Alcor.
3. LP fees → `collectfees` → `smartafees11` (multisig).
4. SAT/LEEF launch LP (pool 12905, position 153128) is owned by `smartafees11`, locked until 2068-10-22; fees collected by the multisig directly via `swap.alcor::collect`.

## Known risks / auditor focus
- **Unlimited mint headroom:** 80M SAT mintable by the multisig.
- **No absolute cap:** limits are bps of a bucket only.
- **Admin power:** the multisig can change limits, whitelists, bot, fee recipient, and pay out buckets; it also controls contract code (upgradeable).
- **Bot key compromise:** bounded by caps, whitelists and `min_out`, but can grief with bad (in-cap) trades; `pause` is available.
- **Pending-swap pattern:** `buyback` → inline `checkout`; review re-entrancy/notify ordering and balance-delta measurement.
- **`on_notify("*::transfer")`:** token contract spoofing; check the contract + symbol whitelist (`token not accepted`).
- **Lock dependence on Alcor:** relies on `swap.alcor` `lockpos` semantics and its upgradeability.
- **Integer math:** `__int128` cap checks, plan id parsing, pool path parsing.
- Test coverage: local chain + testnet scripts only; no fuzzing.
