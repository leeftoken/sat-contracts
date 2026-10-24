# SAT + leeftrader smart contracts (WAX / Antelope)

Status: **local tests (58/58) + WAX testnet run (64/64 + eosio.msig flow) done. Deployed on WAX mainnet Oct 23 2026 with low caps, not audited — see `contracts/MAINNET.md`.** See "WAX testnet run" and "Audit".
Code: `contracts/` — `sat.token/`, `leeftrader/`, `mocks/mockswap/` (test only), `build.sh`, `tests/run_tests.sh`.

## Hard rules (owner, Oct 22 2026)
- **Trading accounts** (`mywaxgallery`, `danishcrypto`, `qu.ui.wam`, `skgaw.wam`, `dustwizard42`, `niftywarlord`,
  `smart.ass`) may never be the contract account, the bot actor or a payout destination. Their keys are never used
  in contracts, tests or configs. On-chain deny-list in `init`/`setbot` (bot), `setdest` and `payout`.
- **Owner override (Oct 23 2026):** the only exception is as **signers of the 2-of-3 admin / fee multisig**:
  `danishcrypto@active`, `smart.ass@active`, `leef@active`, weight 1 each, threshold 2. So `admin` and
  `fee_recipient` are not deny-listed; they must be dedicated multisig accounts (placeholders `smartaadmin1`,
  `smartafees11`). The fee multisig is the only non-fresh payout target. The bot stays a fresh account (`smartabot111`).
- Mainnet accounts (live): `smartatrader`, `sattoken1111`, `smartaadmin1`, `smartafees11`, `smartabot111` (see `contracts/MAINNET.md`).
- Tests use a throwaway local `nodeos` with a fresh key generated per run (custom genesis), never stored or committed. Never mainnet.
- smart.ass stays claims-only. Bot still obeys: simulate first, min-out >= input + 1 unit for same-token loops,
  no wash trading, no trades between own wallets, 10% drawdown stop, no action triggered from Telegram.

## Existing LEEF contracts (researched on-chain, read-only)
| Account | What it is | Actions |
|---|---|---|
| `leefmaincorp` | LEEF token, standard eosio.token ABI. Max 420,000,000,000.0000 LEEF, supply 132,000,000,000.0000 (`get_currency_stats`, 24 Oct 2026, wax.greymass.com) | create, issue, retire, transfer, open, close |
| `leefstakerio` | LEEF staking. Tables `stakerlist`, `stakeralist`, `roomlist`. leeftoken.io calls this the staking contract. | regstaker, roomstake, roomunstake, unstake, unstakea, transfer, plus admin/ban actions |
| `leefrewarder` | Reward calculator. Tables `accounts`, `assets`, `config`, `rewards`, `stakerlist`. leeftoken.io calls this the passive reward contract. | init, calcrewards, claim |
| `leefhopperio` | leeftoken.io calls this the active reward contract. `get_abi` returned no ABI on 24 Oct 2026. Do not invent actions. | none published |
No public source repo for leeftoken.io contracts was found; only the on-chain ABIs above. SAT and leeftrader are
designed as a sub-system next to these: they only *use* LEEF via standard transfers and never need permissions on
`leefmaincorp`, `leefstakerio`, `leefrewarder`, or `leefhopperio`.

## Alcor native LP lock (verified Oct 22 2026)
Source: https://docs.alcor.exchange/alcor-swap/locking-liquidity and the live `swap.alcor` ABI/tables.
- Action: `swap.alcor::lockpos(poolId uint64, owner name, tickLower int32, tickUpper int32, unlockTime uint32)`. Owner auth.
- Table `swap.alcor/locks`: `{pos_id, unlockTime}`; event `loglock(poolId, posId, owner, unlockTime)`.
- While locked: `subliquid` and `transferpos` are blocked; **fees can still be collected** (`collect`) and the position
  can be staked in farms and claim farm rewards. A lock can be extended, never shortened.
- **Max duration**: `unlockTime` is a uint32 unix time, so the ceiling is 2106-02-07. The live `locks` table already
  contains a lock at 4294967295 (2106), so a 42-year lock (~2068) is accepted on-chain.
- Who claims fees: only the position **owner** can call `collect`; the owner chooses the `recipient`. If `leeftrader` owns
  the position, only its `collectfees` action can claim, and it always sends to `cfg.fee_recipient` (team/LEEF-team multisig).
- Permanent alternative (not used): transfer to `eosio.null`. That kills fee claims forever, so we do **not** use it.
- Custom vault: dropped. Only needed if Alcor ever removed fee claims from timed locks.

Other `swap.alcor` actions used: `addliquid(poolId, owner, tokenADesired, tokenBDesired, tickLower, tickUpper, tokenAMin, tokenBMin, deadline)`,
`collect(poolId, owner, recipient, tickLower, tickUpper, tokenAMax, tokenBMax)`, `withdraw(owner, extended_asset)`,
deposit = token transfer with memo `deposit`, swap memo `swapexactin#<poolIds>#<recipient>#<minOut>@<contract>#<deadline>`.

## Contracts
### `sat.token`
eosio.token-compatible (create/issue/retire/transfer/open/close). Example: max 100,000,000.0000 SAT, no inflation.
After issue, set the issuer/owner permission to the multisig (or `eosio.null` for owner) so supply is fixed.

## Contract account (CONTRACT_ACCOUNT)
Configurable everywhere (`CONTRACT_ACCOUNT` for the bot client, `VITE_SAT_CONTRACT` in leef-trader). Local tests deploy to `leeftrader` on a throwaway chain only.
- **Default: `smartatrader`** (owner's final choice, Oct 22 2026): 12 chars, free, so it can be created directly with `newaccount` (no auction).
- Considered and dropped: `smarttrader` (11 chars, would need a premium-name auction) and `smarttrader1`.
- Rejected: `smart.ass.tr`. Only the owner of the `tr` premium suffix can create `*.tr`, and making it from `smart.ass` would need smart.ass's key, which is banned.
- The SAT token contract needs its own fresh account (`sattoken1111` on mainnet).

### `leeftrader` contract (deployed to CONTRACT_ACCOUNT)
Roles (config singleton): `admin` (multisig), `bot` (fresh dedicated account), `fee_recipient` (team multisig), `swap` (`swap.alcor`).
| Action | Auth | Guards |
|---|---|---|
| `init, setsplit, settoken, setswap, setdest, setplan, setlimits, setbot, setfeercpt` | self / admin | deny-list, split = 10000 bps, lock >= 1 year |
| `on_notify *::transfer` | any payer | token+contract whitelist; memo `sub:<plan>[:<user>]` records a subscription; revenue split into buckets |
| `buyback(bucket, in, swap, pool_path, min_out)` | bot | not paused, swap whitelisted, path digits/commas only, contract builds memo with itself as recipient, inline `checkout` reverts if received < min_out, same-token loop needs min_out >= in+1, per-action and daily caps |
| `addlp(bucket, pool_id, a, b, tick_lower, tick_upper, min_a, min_b)` | bot | deposit, `addliquid` owner = leeftrader, then Alcor `lockpos` until now + lock_secs (default 42y) |
| `collectfees(actor, lp_id, max_a, max_b)` | bot or admin | Alcor `collect` → fee_recipient only |
| `lpwithdraw(bucket, amount)` | bot | returns Alcor deposit leftovers to the bucket |
| `payout(bucket, to, quantity, memo)` | bot | `to` must be whitelisted; caps |
| `pause(actor, paused)` | admin; bot may only pause | bot uses it for the 10% drawdown stop |
| `logrevenue, logsub, logswap, loglp` | self (inline) | events for indexers |
There is **no** `subliquid`, `transferpos` or free-form transfer action: liquidity can never be removed through this contract.
Tables: `config, splits, tokens, swaps, dests, plans, subs, buckets (scope = bucket), pending, lps`.
Default caps: 5% of a bucket per action, 20% per UTC day (`setlimits`).

## Build and test
```
CDT_BIN=/path/to/cdt/usr/bin contracts/build.sh            # AntelopeIO CDT 4.1.1
SPRING_BIN=/path/to/spring/usr/bin contracts/tests/run_tests.sh   # AntelopeIO Spring 1.2.2 nodeos/cleos/keosd
```
The `.deb` packages can be unpacked without root: `dpkg-deb -x cdt_4.1.1-1_amd64.deb DIR`.
Last run (Oct 23 2026): **58 passed, 0 failed** — token supply/limits, deny-list, split maths, subscriptions,
whitelists, memo-injection, min_out revert, +1 unit rule, caps, native-lock simulation (42y, subliquid blocked),
fee collection to fee_recipient only, payout whitelist, pause rules. `mockswap` mimics `swap.alcor`'s action
signatures and lock rules; it is not Alcor's code, so a WAX testnet run against real Alcor testnet pools is still needed.

## On-chain vs off-chain
On-chain (enforced by the contract): token whitelist, revenue split, subscription records and expiry, swap/destination whitelists,
memo construction, min-out check after the swap, the +1 unit loop rule, per-action and daily caps, LP lock duration, fee recipient, pause, and the deny-list.
Off-chain (bot): route search/simulation, choosing when to act, the 10% drawdown valuation (it triggers the on-chain `pause`), Telegram display.
Bot client: `contracts/client/leeftrader-client.mjs` (unsigned payloads, table/Hyperion reads; 6/6 tests via `node contracts/client/leeftrader-client.test.mjs`).
Hermes setup skill: `skills/leef-system-setup/SKILL.md`.

## Integration notes
### Bot (Grok Bot now, Hermes later)
- Runs as the dedicated `bot` account with a key that only has a custom permission `leeftrader.bot`, linked
  (`linkauth`) to `leeftrader::buyback, addlp, lpwithdraw, collectfees, payout, pause` only.
- Off-chain loop: read `buckets`, simulate the route (existing Alcor sim), compute min_out (sim minus slippage, and >= in+1
  for loops), push `buyback`; on success optionally `addlp` the bucket into SAT/WAX, SAT/LEEF, SAT/USDC or SAT/USDT pools.
- Drawdown: bot values the treasury each run; at -10% it pushes `pause(bot, true)`; only the admin multisig can unpause.
- Telegram stays read-only: it may display tables/logs but never triggers actions.
- Stars revenue never reaches the chain directly: Fragment → TON → manual conversion → transfer to leeftrader with memo `fund`.
### leef-trader platform (https://github.com/leeftoken/leef-trader, https://leef-trader.vercel.app/)
- Read `get_table_rows` on `leeftrader`: `subs` (subscription status per user), `buckets`, `lps`, `plans`, `config`.
- Read history (Hyperion `get_actions` filter `leeftrader:logrevenue,logsub,logswap,loglp`) for activity feeds.
- Subscriptions from the web: user signs `transfer(user → leeftrader, price, "sub:<plan_id>")` in their own wallet.
- LP lock proof: show `swap.alcor/locks` rows for the `lps` positions.

## Multisig setup (2-of-3)
Same JSON for `smartaadmin1` and `smartafees11` (`active` and `owner`), and for `smartatrader` / `sattoken1111`
`owner` + `active` once deployed (for `smartatrader@active` add `{"permission":{"actor":"smartatrader","permission":"eosio.code"},"weight":2}`
so inline transfers keep working):
```json
{"threshold":2,"keys":[],"waits":[],"accounts":[
 {"permission":{"actor":"danishcrypto","permission":"active"},"weight":1},
 {"permission":{"actor":"leef","permission":"active"},"weight":1},
 {"permission":{"actor":"smart.ass","permission":"active"},"weight":1}]}
```
(accounts sorted alphabetically, as `updateauth` requires). Admin changes: `cleos multisig propose` →
two `approve` → `exec` (tested on testnet: exec with one approval fails, with two it runs).

## Proposed parameters (PROPOSAL, not approved)
`contracts/params.proposal.json`. Supply 100,000,000.0000 SAT fixed; allocation 35% locked LP / 25% user rewards
(4y) / 15% treasury msig / 15% team (12m cliff, 36m vest) / 10% LEEF-holder community; split 30% SAT LP / 20% LEEF LP /
30% ops / 10% rewards / 10% reserve; plans (public bot 50-100 Stars/month, default 75): Basic 30d 75 Stars = $0.975 = 0.9750 USDT ≈ 213 WAX,
Pro 30d 150 Stars = $1.95 = 1.9500 USDT ≈ 427 WAX, Pro 365d 1,500 Stars = $19.50 = 19.5000 USDT ≈ 4,269 WAX
($0.013 per Star; WAX $0.004568, CoinGecko Oct 23 2026); 20% discount when paid in SAT; caps 5%/action, 20%/day; lock 42y.
WAX prices drift; re-set plans via msig or price plans in USDT only. Reasons are on the SAT Page.

## WAX testnet run (Oct 23 2026)
`contracts/tests/testnet_run.sh` (refuses any non-testnet RPC). Fresh throwaway accounts, prefix `ukbejz` (funder
`aejtwdfundr1` from the waxsweden faucet; faucet keys rotated straight away to a key that only lives in a local keosd
outside the repo). Result: **64 passed, 0 failed** + eosio.msig propose/approve/exec. Explorer: `https://wax-test.bloks.io/transaction/<id>`.
- **Alcor on testnet:** `swap.alcor` exists on testnet but its ABI has **no `lockpos`** (older build with `lockpool`),
  so LP lock + fee collect were run against `mockswap` only. The lock path still needs one tiny check on mainnet or
  on an Alcor testnet with lockpos.
- RAM used on testnet: leeftrader 783 KB, sat.token 159 KB, mockswap 204 KB.

## Audit (automated checklist, Oct 23 2026) — not a replacement for a human audit
| Check | Finding | Fix |
|---|---|---|
| Overflow | `now + lock_secs` could wrap uint32 → lock in the past | `setlimits`/`addlp` reject anything past 2106 (test) |
| Notify spoofing | transfer handler checks `get_first_receiver()` against the whitelisted contract ✔. But `buyback` `min_out` was checked by symbol code only | contract must match too (test: fake-contract SAT rejected) |
| Malicious pool | bot could route buybacks / add LP through any pool id (attacker-made pool drains up to caps) | admin pool whitelist `setpool`; checked in `buyback` path and `addlp` (tests) |
| Slippage / deadline | `addlp` accepted min 0; swap memo deadline 0 | min amounts > 0; deadline now+300 s |
| Auth | all admin actions `require_auth(admin)`, bot actions `require_auth(bot)`, checkout/logs self only ✔; bot can no longer be fee recipient | added checks |
| RAM payer | all rows paid by contract; subscription rows need a paid plan, so spam costs the plan price ✔ | — |
| Reentrancy-like inline | `pending` singleton blocks overlapping buybacks; checkout measures balance delta ✔ | — |
| Rounding | split dust goes to the last bucket; caps use 128-bit maths ✔ | — |
| Open (documented) | `lpwithdraw` trusts the bot's bucket choice (funds stay in contract, only bucket labels move); transfers from whitelisted swaps with other tokens are left unaccounted | human audit to decide |

## Before mainnet (owner decisions)
RAM: ~0.45 WAX/KB on mainnet (Oct 23 2026) → leeftrader ~1 MB ≈ 460 WAX, sat.token ~200 KB ≈ 90 WAX, plus ~0.3 KB per
subscriber row; ≈ 600 WAX (≈ $3) with headroom. A **human audit** (an Antelope-specialist firm) is recommended
before any real funds, since the automated pass can't prove absence of bugs.

Account names, multisig signers/threshold for admin + fee_recipient, fresh bot account, independent audit,
WAX testnet run against Alcor, SAT supply/allocation, split percentages, lock length (42y default), whether the
`leeftrader` code itself is made immutable (owner → multisig with high threshold, or eosio.null) so the lock and
guards cannot be changed by upgrade.
