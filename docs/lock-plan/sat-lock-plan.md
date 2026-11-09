# SAT token lock plan (PLAN ONLY — nothing signed)

Date: 2026-11-09 22:18 Europe/Copenhagen  
Status: **plan / dry-run only**. No issue, transfer, lockpos or deploy was signed.  
Contracts: **not audited yet**.

## Sources

- Whitepaper / tokenomics (proposal): `contracts/params.proposal.json`, sat-site “Planned allocation”, skill `sat-token`.
- Live chain (wax.greymass.com / Alcor API, 2026-11-09 ~22:15 CET).
- Deployed contracts: `sattoken1111` (eosio.token-compatible), `smartatrader` (leeftrader), Alcor `swap.alcor::lockpos`.

## Live snapshot

| Item | Value |
|---|---|
| Max supply | 100,000,000.0000 SAT |
| Issued | 20,000,000.0000 SAT (20%) |
| Still mintable | 80,000,000.0000 SAT |
| Issuer | `smartaadmin1` |
| Locked LP pos 153128 (pool 12905) | unlockTime 3118144759 = **2068-10-22**; owner `smartafees11`; Alcor `isLocked=true` |
| Pos 153128 amounts (Alcor) | ~1.36B LEEF + **~7.80M SAT** (CLMM full-range; originally ~10M SAT deposited) |
| Unclaimed fees on 153128 | ~944,300 LEEF + ~262 SAT |
| `smartatrader` SAT bucket | 91.2780 SAT in `satlp` |
| `smartaadmin1` SAT | 0.0365 (airdrop budget essentially sent) |

### Notable liquid SAT (trading / ops wallets, not locked)

| Account | SAT (approx) |
|---|---|
| danishcrypto | 505,022 |
| qu.ui.wam | 495,824 |
| skgaw.wam | 480,595 |
| dustwizard42 | 365,565 |
| smart.ass | 155,710 |
| mywaxgallery | 12,006 |
| niftywarlord | 2,861 |
| smartatrader | 91 |
| **Sum of listed** | **~2.02M** |

These came largely from the LEEF-community airdrop. Building them into LP is a **trading** action (already approved separately as “build SAT LP hard”), not the allocation lock below.

## Whitepaper / proposed allocation (of 100M max)

| Bucket | % | Amount | Schedule (as published) | Issued so far |
|---|---|---|---|---|
| Locked LP | 35% | 35,000,000 | Alcor `lockpos` (~42y); deepen over time | ~10M deposited to pos 153128 (now ~7.8M in-range SAT) |
| User rewards | 25% | 25,000,000 | Released over **4 years** to bot users | 0 |
| Treasury (msig) | 15% | 15,000,000 | Held by 2-of-3 admin for ops / new pools | 0 |
| Team | 15% | 15,000,000 | **12-month cliff, then 36-month vest** | 0 |
| LEEF community | 10% | 10,000,000 | Airdrop (sent Nov 9 2026) | ~10M issued & distributed |

**Remaining to mint under the proposal:** 80,000,000 SAT  
(= +25M LP to reach 35M target + 25M rewards + 15M treasury + 15M team).

## Contract readiness

### What already works (verified)

1. **`sattoken1111::issue` / `transfer` / `retire`** — standard token; issuer `smartaadmin1`.  
   - Cleos packed unsigned txs successfully (`-d -j --skip-sign`).  
   - Cleos `--dry-run` simulated `issue` of 1.0000 SAT (local execution trace, **not broadcast**; supply stayed 20,000,000.0000; tx id not on history).
2. **Alcor `swap.alcor::lockpos`** — live on mainnet for pos **153128** and test pos **153124** (`smartatrader` WAX/USDT). Fees remain collectable while locked; `subliquid` / `transferpos` blocked until unlock. Max ~2106.
3. **`smartatrader` buckets + `addlp` + `collectfees` + `payout`** — revenue split live (satlp 30 / leeflp 20 / ops 30 / rewards 10 / reserve 10). `addlp` locks new LP with `lock_secs` (~42y). Caps 1%/action, 5%/day. **Not a token-allocation vesting vault.**

### What does **not** exist

- **No vesting / timelock contract** for team (12m cliff / 36m linear) or for user-rewards drip.  
  Accounts checked: `smartavest11`, `smartavesting`, `sattokenvest` — none.  
  `sattoken1111` ABI has only create/issue/retire/transfer/open/close.  
  `smartatrader` ABI has no vest/claim/cliff actions.

**Conclusion:** we can lock **more LP** today with existing Alcor + (optionally) `smartatrader::addlp`. We **cannot** honestly “lock team + rewards on a schedule in contract” until a small vesting contract is deployed (or vesting is added to `smartatrader` — larger change).

## Recommended lock design (smallest safe vesting contract)

**Do not deploy until the owner approves after audit / review.** Outline only:

| Field | Proposal |
|---|---|
| Account | new `smartavest11` (or similar), owner+active = same admin msig as `smartaadmin1` |
| Code | single-purpose vesting: categories `team` and `rewards` (and optional `treasury_timelock`) |
| Actions | `openstream(beneficiary, category, total, start, cliff_secs, duration_secs)`, `claim(id)`, `revoke(id)` (admin only, optional — prefer **no revoke** for team once funded), `pause` |
| Auth | `openstream` / fund via token notify or `transfer` memo; `claim` = beneficiary; admin msig for open |
| Maths | claimable = 0 before `start+cliff`; then linear `total * (now-start-cliff)/duration` capped at total |
| Team params | start = issue time; cliff = 365d; duration = 1095d (36m) after cliff; beneficiary = msig or named team account |
| Rewards params | cliff = 0; duration = 1461d (4y); beneficiary = `smartatrader` or a rewards distributor the bot can call |
| Withdraw | only `claim` to the stream’s beneficiary; no general admin drain after funding (or drain only of unallocated residual with msig + delay) |
| Audit | **not audited yet** — treat like `smartatrader` |

Alternative (weaker): keep treasury on `smartaadmin1` msig without a contract lock (matches site text “held by multisig”), and only contract-lock team + rewards.

## Proposed issuance / destination plan (unsigned)

All amounts in SAT, 4 decimals. **Nothing below is signed.**

### A. Additional locked LP — toward 35% (25,000,000)

Goal: raise locked LP toward 35M. Prefer gradual adds (price impact), not one dump.

1. `sattoken1111::issue` → `smartaadmin1` — `25000000.0000 SAT` memo `SAT allocation: additional locked LP toward 35pct`  
2. Transfer + LEEF (or WAX) into Alcor pool(s), mint position(s), then `swap.alcor::lockpos` to ~2068 (same pattern as 153128), owner preferably `smartafees11` or `smartatrader` satlp bucket via `addlp`.  
3. Prefer healthy pools only (12905, 12925, 13230, 13231, 13235, 13239). Never 13238 / 13234.

Unsigned pack saved: `sat-lock/unsigned-issue-lp-25m.json`.

### B. User rewards — 25,000,000 over 4 years

1. Issue `25000000.0000 SAT` to `smartaadmin1`.  
2. **After** vesting contract exists: fund a `rewards` stream (cliff 0, 4y linear) with beneficiary = rewards payer.  
3. **Until then (interim, weaker):** transfer into `smartatrader` with memo `fund` and keep labelled off-chain as rewards — **not** a time lock; admin `payout` can move it anytime. Not recommended as the final state.

Unsigned pack: `sat-lock/unsigned-issue-rewards-25m.json`.

### C. Treasury — 15,000,000

1. Issue `15000000.0000 SAT` to `smartaadmin1` (or a dedicated treasury account controlled by the same msig).  
2. Matches whitepaper “held by multisig”. Optional later: soft-lock in vesting with long cliff if desired.

Unsigned pack: `sat-lock/unsigned-issue-treasury-15m.json`.

### D. Team — 15,000,000 (12m cliff, 36m vest)

1. Issue `15000000.0000 SAT` to `smartaadmin1`.  
2. **Requires vesting contract** before this is “locked right in contract”.  
3. Open stream: cliff 12m, duration 36m after cliff, beneficiary = team payout account (msig).

Unsigned pack: `sat-lock/unsigned-issue-team-15m.json`.

### E. Community airdrop — done

~10M issued and sent (Nov 9). `smartaadmin1` dust 0.0365 left. No further lock action.

## Exact unsigned actions (templates)

Files under `/workspace/status-20261109/sat-lock/` (also summarized here). Auth: `smartaadmin1@active` (msig). **DO NOT SIGN** until owner sign-off.

```json
{
  "account": "sattoken1111",
  "name": "issue",
  "authorization": [{"actor": "smartaadmin1", "permission": "active"}],
  "data": {
    "to": "smartaadmin1",
    "quantity": "25000000.0000 SAT",
    "memo": "SAT allocation: user rewards 4y (PLAN — DO NOT SIGN)"
  }
}
```

Same shape for treasury (`15000000.0000 SAT`), team (`15000000.0000 SAT`), additional LP (`25000000.0000 SAT`) with the memos in the saved JSON files.

Example later transfer into treasury contract / vest (only after deploy):

```json
{
  "account": "sattoken1111",
  "name": "transfer",
  "authorization": [{"actor": "smartaadmin1", "permission": "active"}],
  "data": {
    "from": "smartaadmin1",
    "to": "smartavest11",
    "quantity": "15000000.0000 SAT",
    "memo": "vest:team"
  }
}
```

Example `smartatrader::payout` pack (admin; only for interim bucket moves — **not** a vest):

```json
{
  "account": "smartatrader",
  "name": "payout",
  "authorization": [{"actor": "smartaadmin1", "permission": "active"}],
  "data": {
    "bucket": "rewards",
    "to": "danishcrypto",
    "quantity": {"quantity": "1.0000 SAT", "contract": "sattoken1111"},
    "memo": "dry-run"
  }
}
```

## Simulation results

| Test | Result |
|---|---|
| Pack `issue` 1 / 15M / 25M SAT (`cleos -d -j --skip-sign`) | OK — valid unsigned trx JSON, empty signatures |
| Pack `transfer` fund 1 SAT to `smartatrader` | OK |
| Pack `smartatrader::payout` 1 SAT | OK |
| `cleos --dry-run` issue 1.0000 SAT | Simulated execute locally; **not on chain**; supply remained 20,000,000.0000; history lookup of returned id → not found |
| Alcor lock 153128 | On-chain `locks` row present; unlock 2068-10-22; fees claimable |
| Vesting claim simulation | **N/A** — no vesting contract deployed |

## Risks

- **Not audited yet** — issuing large SAT into anything other than Alcor lockpos / known msig without a reviewed vest increases dump risk if keys or msig are compromised.
- Issuing the full 80M at once creates headline supply shock even if locked; prefer tranche issues matching roadmap milestones.
- CLMM LP “amount” moves with price; locked **liquidity** stays, SAT quantity in-range is not fixed at 10M.
- `smartatrader` buckets are **not** time-locked; calling them a vest would mislead the site / whitepaper.
- Multisig / permission details for admin signing stay out of public docs; use existing private runbooks.
- Issuer remains `smartaadmin1` until deliberately renounced; mintable 80M is a standing power — locking allocation in vest + optional issuer→null/msig-only policy is a separate owner decision.
- LP adds must respect pool whitelist, 1% impact, simulate-first, and no wash trading rules.

## What needs owner sign-off

1. Approve this allocation still (params.proposal.json was never formally approved).  
2. Choose: (a) deploy smallest `smartavest11` vest first, then issue+fund; or (b) issue treasury+LP only now, wait on vest for team/rewards.  
3. Team beneficiary account name and whether revoke is allowed.  
4. LP tranche size / which pools / owner of new locked positions (`smartafees11` vs `smartatrader`).  
5. Whether to leave issuer mint authority as-is after allocation.  
6. Human audit before large issues.  
7. Explicit “sign these actions” per batch — **this document is not that approval.**

## Safety note

SAT stays **not audited yet**. This file is internal status. Do not paste multisig weakness detail into public/audit marketing copy.
