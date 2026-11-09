# smartavest spec (short)

Not audited yet. Local tests only. No mainnet deploy.

Vault for `4,SAT` from one token contract chosen in `init` (mainnet: `sattoken1111`). Contract account intended for mainnet: `smartavest11`. C++ contract name: `smartavest`.

## Rules

- Beneficiary is fixed when the stream is opened.
- `claim` is permissionless and always transfers to that beneficiary.
- No withdraw, revoke, pause, rescue, or set-beneficiary.
- Deposits that are not `open:...` or `fund:<id>` from the configured SAT contract are rejected.
- `open:...` transfers must come from `admin`. `fund:<id>` may come from anyone and only increases `total`.
- `duration_secs > 0`. `cliff_secs` may be 0. `start == 0` means now.
- `start + cliff_secs + duration_secs` must fit in `uint32`.
- `vested = floor(total * elapsed / duration_secs)` using `__int128`, or `0` before the cliff, or `total` at/after the end.
- `claimed` is updated before the inline transfer.
- Production wasm is built without `SAT_VEST_TEST_CLOCK`. `setnow` exists only in the test wasm.

## Schedules this vault is meant to hold

| Stream | Cliff | Linear after cliff |
|---|---|---|
| team | `365 * 86400` | `1095 * 86400` |
| rewards | `0` | `1461 * 86400` (4 × 365.25 days) |

The contract does not mint SAT and does not hard-code those amounts.
