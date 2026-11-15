# SAT / LEEF Trader source remediation status

Date: 2026-11-15
Branch: `fix/exact-contract-authorities-and-same-token-swap-20261114`

## Scope

This is a source-level remediation and regression-test change. It is not a mainnet deployment, authority rotation, multisig operation, or independent security audit.

## Changes

### Exact contract permissions
- LEEF Trader configuration actions require `admin@active` rather than accepting any permission belonging to the configured admin account.
- Bot trading operations require `bot@bot`, including the shared bot guard, bot pause, and bot-authorized fee collection.
- The outbound `payout` action now requires `admin@active`; it is deliberately not a bot permission.
- Admin fee collection requires `admin@active`; bot fee collection requires `bot@bot`.
- SAT `issue` and `retire` require the configured issuer's exact `@active` permission.

### Same-token buyback
The trader now snapshots the output-token balance against the post-input baseline for same-token routes. `checkout` therefore compares the gross swap output with the gross `min_out`, while the bucket accounting still records the net balance change after the input spend. Different-token routes retain their existing baseline behavior.

### Regression infrastructure
- CI pins Antelope CDT 4.1.1 and Spring 1.2.2.
- Production SAT token and trader WASM are built.
- Existing staking and vesting production/test-clock WASM variants are built and run through their isolated local-chain suites.
- The token/trader local-chain harness forces unique transactions so independent test actions cannot collide on identical TAPOS data.
- Tests explicitly reject owner/active substitutions for bot/admin-only actions, reject bot-authorized treasury payouts, and exercise the profitable same-token route.

## CI evidence

GitHub Actions run: https://github.com/leeftoken/sat-contracts/actions/runs/43673261732

- Production SAT token/trader build: passed
- Staking contract production and test-clock build: passed
- Vesting contract production and test-clock build: passed
- Token/trader local-chain suite: 65 passed, 0 failed
- Staking local-chain suite: 83 passed, 0 failed
- Vesting local-chain suite: 7 passed, 0 failed
- Trader client serialization/safety tests: passed

The local-chain staking tests use a disposable local node and test clock. They do not modify or attest to any live staking deployment.

## Required post-rotation release checks

Before deploying the new trader/token WASM:
1. Confirm the rotated configured admin `@active` authority has the intended 2-of-3 threshold and current signer set.
2. Confirm the owner permission cannot be used as a substitute for the required admin active authorization.
3. Confirm the bot account has a dedicated `@bot` permission and that its authority uses the intended isolated bot key.
4. Link only the intended bot actions to `@bot`; keep `payout` excluded.
5. Confirm `smartatrader` and `sattoken1111` account/issuer configuration matches the intended admin and permission names.
6. Build the release WASM from the reviewed commit, verify hashes, simulate on testnet/local chain, and independently review the upgrade transaction before mainnet deployment.
7. Re-read the live account permissions and contract code hashes after deployment; verify the runtime actions against the source/ABI.

## Explicit non-claims

- No live account authority, key, multisig, token balance, or treasury setting was changed.
- No mainnet contract code was deployed.
- The live staking contract source was not modified.
- A successful local CI run is not an independent audit and does not certify mainnet state.
