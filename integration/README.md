# Review integration suite (2026-11-14)

Local-only checks of `smartavest` and `smartastake` against the repo's **real** `sat.token` (deployed as `sattoken1111`) and **real** `leeftrader` (deployed as `smartatrader`). Both wasm files build with CDT 4.1.1 to the same code hashes as mainnet:

| Account | Code hash (mainnet = local build) |
|---|---|
| `sattoken1111` | `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` |
| `smartatrader` | `4ed7d02a7f7107dc03d9c9df61b77fd4927368ea95b669e0d0ba228f1ee729c4` |

The suite starts a throwaway Spring 1.2.2 `nodeos` on `127.0.0.1:8891` with a key generated for the run. It does not stop any other `nodeos` and never contacts mainnet or a testnet. Nothing here is a deploy script.

```bash
CDT_BIN=/path/to/cdt/bin SPRING_BIN=/path/to/spring/bin ./integration/run.sh
# pre-fix behaviour, for comparison:
git show <base>:smartastake/smartastake/smartastake.cpp > /tmp/base/smartastake.cpp
MODE=base STAKE_SRC=/tmp/base/smartastake.cpp ./integration/run.sh
```

Output lines are `PASS`, `FINDING` (confirmed and documented, not changed), `FIXED` (changed on this branch), or `BUG` (pre-fix behaviour, `MODE=base`).

What it covers: the live `smartatrader` limits and split, a team vest stream, a mid-stream top-up, a beneficiary that rejects SAT, WAX sent to the vault, a rewards stream into `smartastake1`, first-staker capture, just-in-time staking, the airdrop-fund seed, a trader payout with an empty memo, `smartafees11` forwards, governance against the unpatched trader, the trader upgrade with a plain `name gov` and with `binary_extension<name> gov`, stale queued proposals, and the balance invariants at the end.

`make_trader_patches.py` writes the patched trader sources to `integration/build/` and does not change `leeftrader/`.
