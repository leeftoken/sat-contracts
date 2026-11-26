# sat-contracts — Smart Ass Trading (SAT)

Source for the SAT smart contracts deployed on WAX mainnet. **Not a firm audit.** Source+mainnet review: [AUDIT-REPORT.md](AUDIT-REPORT.md). Scope sheet: [AUDIT-SCOPE.md](AUDIT-SCOPE.md).

Full contract guides (token, trader, vesting, staking, airdrop fund, launch order, and governance): [docs/README.md](docs/README.md).


| Account | Source | Deployed code hash (sha256) |
|---|---|---|
| `sattoken1111` | `sat.token/sat.token.cpp` | `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` |
| `smartatrader` | `leeftrader/leeftrader.cpp` + `setgov` patch | `0a48f389ed1a921b93393278102cc63ac83ba77e87e21352e63ee158581fabb4` |
| `smartavest11` | `smartavest/smartavest/smartavest.cpp` | `b6166eae6560e8bc43ac2bafed01ec58e7476fa30e297ddbe4c85074a54ae532` |
| `smartastake1` | `smartastake/smartastake/smartastake.cpp` | `557f65faa57a6cdfa500f3ae218fd45db6009314ba399ae55e16e7a90a9c3d79` |

**Live-chain status requires reconciliation before any operational decision.** The checked-in documents currently disagree: this README records 100M SAT issued and lists `smartatrader` hash `0a48f389…`, while [AUDIT-2026-11-14.md](AUDIT-2026-11-14.md) records 80M SAT mint headroom and lists `smartatrader` hash `4ed7d02a…`. The 14 Nov audit also labels the new vesting/staking deployments as planned, while this README labels them live. These claims must not be treated as verified until the current WAX mainnet token stats, account code hashes, account permissions, and deployed ABIs are fetched and recorded together. See [CHAIN-STATE-RECONCILIATION.md](CHAIN-STATE-RECONCILIATION.md). PR #8 (exact-permission checks for `sat.token` and `leeftrader`) is merged in `main` but **not deployed**; do not infer that `main` rebuilds to the live `sattoken1111` / `smartatrader` hashes.

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


## smartavest (live as `smartavest11`)

Vesting vault in [`smartavest/`](smartavest/). **Live since 14 Nov 2026, not audited.** Source-reviewed in [AUDIT-2026-11-14.md](AUDIT-2026-11-14.md) with no WASM change. `claim(id)` is permissionless; `claim(1)` moves vested user rewards into `smartastake1`.

`smartavest/sat.token/` is a local test token for `smartavest/tests/run_tests.sh` only. It is not `sattoken1111`. Do not replace `sat.token/` or `leeftrader/` with anything from `smartavest/`.

Local Spring 1.2.2 suite, 2026-11-09: 83 passed, 0 failed. Build with `cd smartavest && ./build.sh` (CDT 4.1.1). Production wasm has no `setnow`.

Design notes: [DESIGN.md](DESIGN.md). Proposed parameters: `params.proposal.json`.

## smartastake (live as `smartastake1`)

Staking and bounded governance in [`smartastake/`](smartastake/). **Live since 14 Nov 2026, not audited.** Source-reviewed in [AUDIT-2026-11-14.md](AUDIT-2026-11-14.md); the deployed wasm includes the High/Medium fixes.

`smartastake/mocks/` is a local test token, a trader stand-in, and a tiny bios for RAM. It is not `sattoken1111` or `smartatrader`. Do not replace `sat.token/` or `leeftrader/` with anything from `smartastake/`.

Local Spring 1.2.2 suite, 2026-11-14: 27 checks passed. Build with `cd smartastake && ./build.sh` (CDT 4.1.1). Production wasm has no `settime`. Checked-in ABIs: `abi/smartastake.abi`, `abi/smartastake_test.abi`. The trader still needs the `setgov` patch in [smartastake/docs/TRADER_PATCH.md](smartastake/docs/TRADER_PATCH.md) before a passed split or limit change can execute on `smartatrader`. That patch is applied on mainnet (`smartatrader` hash `0a48f389…`) but is not merged into `leeftrader/` in this tree.
