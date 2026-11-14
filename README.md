# sat-contracts — Smart Ass Trading (SAT)

Source for the SAT smart contracts deployed on WAX mainnet. **Not a firm audit.** Source+mainnet review: [AUDIT-REPORT.md](AUDIT-REPORT.md). Scope sheet: [AUDIT-SCOPE.md](AUDIT-SCOPE.md).

Full contract guides (token, trader, vesting, staking, airdrop fund, launch order, and governance): [docs/README.md](docs/README.md).


| Account | Source | Deployed code hash (sha256) |
|---|---|---|
| `sattoken1111` | `sat.token/sat.token.cpp` | `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` |
| `smartatrader` | `leeftrader/leeftrader.cpp` | `4ed7d02a7f7107dc03d9c9df61b77fd4927368ea95b669e0d0ba228f1ee729c4` |

`mocks/mockswap` is a test stand-in for `swap.alcor` and is not deployed on mainnet.

**Audit (2026-11-09):** [AUDIT-REPORT.md](AUDIT-REPORT.md). Source matches live WASM; top risks are live key/permission centralization, 80M unissued SAT, bot `@active` bypassing `linkauth`, and a same-token `buyback`/`checkout` logic bug. Contract logic is unchanged.

**Audit (2026-11-14):** [AUDIT-2026-11-14.md](AUDIT-2026-11-14.md). Source review of `smartavest` / `smartastake` plus their interaction with `sat.token` and the trader `setgov` patch. High/Medium stake-contract fixes are in this tree; vest wasm is unchanged; live `sattoken1111` / `smartatrader` were not modified.

## Reproducible build
Requires AntelopeIO CDT **4.1.1**.
```bash
CDT_BIN=/path/to/cdt/usr/bin ./build.sh
sha256sum build/sat.token.wasm build/leeftrader.wasm
```
Verified on 2026-10-23: both hashes match the on-chain code exactly, and the generated ABIs match `abi/`.

Check on-chain:
```bash
curl -s -d '{"account_name":"sattoken1111","code_as_wasm":1}' https://wax.greymass.com/v1/chain/get_code_hash
curl -s -d '{"account_name":"smartatrader","code_as_wasm":1}' https://wax.greymass.com/v1/chain/get_code_hash
```

## Tests
- `tests/run_tests.sh`: local throwaway nodeos chain (needs AntelopeIO Spring).
- `tests/testnet_run.sh`: WAX testnet only, refuses other RPCs.
- `client/`: JS client helpers + `node client/leeftrader-client.test.mjs`.


## smartavest (not deployed)

Vesting vault in [`smartavest/`](smartavest/). **Not deployed.** Source-reviewed in [AUDIT-2026-11-14.md](AUDIT-2026-11-14.md) with no WASM change. Do not create a mainnet account for it until an external firm audit and owner-approved lock batches.

`smartavest/sat.token/` is a local test token for `smartavest/tests/run_tests.sh` only. It is not `sattoken1111`. Do not replace `sat.token/` or `leeftrader/` with anything from `smartavest/`.

Local Spring 1.2.2 suite, 2026-11-09: 83 passed, 0 failed. Build with `cd smartavest && ./build.sh` (CDT 4.1.1). Production wasm has no `setnow`.

Design notes: [DESIGN.md](DESIGN.md). Proposed parameters: `params.proposal.json`.

## smartastake (not deployed)

Staking and bounded governance in [`smartastake/`](smartastake/). **Not deployed.** Source-reviewed in [AUDIT-2026-11-14.md](AUDIT-2026-11-14.md); High/Medium WASM fixes are in this tree. Do not create a mainnet account for it until an external firm audit and owner-approved lock batches.

`smartastake/mocks/` is a local test token, a trader stand-in, and a tiny bios for RAM. It is not `sattoken1111` or `smartatrader`. Do not replace `sat.token/` or `leeftrader/` with anything from `smartastake/`.

Local Spring 1.2.2 suite, 2026-11-14: 27 checks passed. Build with `cd smartastake && ./build.sh` (CDT 4.1.1). Production wasm has no `settime`. Checked-in ABIs: `abi/smartastake.abi`, `abi/smartastake_test.abi`. The trader still needs the `setgov` patch in [smartastake/docs/TRADER_PATCH.md](smartastake/docs/TRADER_PATCH.md) before a passed split or limit change can execute on `smartatrader`. That patch is not applied to `leeftrader/` in this commit.
