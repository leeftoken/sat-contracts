# sat-contracts — Smart Ass Trading (SAT)

Source for the SAT smart contracts deployed on WAX mainnet. See [AUDIT-SCOPE.md](AUDIT-SCOPE.md) for what is in scope, and [AUDIT.md](AUDIT.md) for the independent review of 23 October 2026 (WASM unchanged; hashes still match mainnet).

| Account | Source | Deployed code hash (sha256) |
|---|---|---|
| `sattoken1111` | `sat.token/sat.token.cpp` | `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` |
| `smartatrader` | `leeftrader/leeftrader.cpp` | `4ed7d02a7f7107dc03d9c9df61b77fd4927368ea95b669e0d0ba228f1ee729c4` |

`mocks/mockswap` is a test stand-in for `swap.alcor` and is not deployed on mainnet.

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
- `client/`: JS client helpers + `node --test client/leeftrader-client.test.mjs`.

Design notes: [DESIGN.md](DESIGN.md). Proposed parameters: `params.proposal.json`.
