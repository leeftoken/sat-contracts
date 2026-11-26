# SAT mainnet state reconciliation required

**Status: unresolved — do not use this document as proof of live state.**

The checked-in documentation contains conflicting assertions. This is a documentation safety correction, not a chain query or contract audit.

## Conflicts found on 2026-11-26

| Item | README.md | AUDIT-2026-11-14.md | Required source of truth |
|---|---|---|---|
| SAT issuance | Says all 100M SAT is issued | Says 80M SAT remains unissued / mintable | WAX mainnet `get_currency_stats` for `sattoken1111`, plus issuer/max-supply and permission state |
| `smartatrader` code hash | `0a48f389ed1a921b93393278102cc63ac83ba77e87e21352e63ee158581fabb4` | `4ed7d02a7f7107dc03d9c9df61b77fd4927368ea95b669e0d0ba228f1ee729c4` | WAX mainnet `get_code_hash` response captured with timestamp and RPC endpoint |
| `smartavest11` / `smartastake1` status | Says live since 14 Nov | Labels them planned in the audit scope/hash table | Current WAX account existence, code hashes, ABIs, tables and permissions |
| `sattoken1111` hash | `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` | Same hash | Re-query anyway and preserve raw response |

## Required verification sequence

Run against a trusted WAX mainnet RPC and retain the raw JSON plus UTC timestamp:

1. `POST /v1/chain/get_currency_stats` with `{"code":"sattoken1111","symbol":"SAT"}`.
2. `POST /v1/chain/get_code_hash` for `sattoken1111`, `smartatrader`, `smartavest11`, and `smartastake1`.
3. `POST /v1/chain/get_account` for those accounts and the issuer/admin/guardian/funder accounts; inspect permission thresholds and linked authorities.
4. Fetch deployed ABIs and compare them with the checked-in ABI files.
5. Rebuild the exact production WASM with the documented CDT version and compare its SHA-256 with the freshly queried live code hashes.
6. Reconcile token supply, maximum supply, remaining mint authority, and all account deployment claims in README, audit reports, launch guides, and the LEEF Trader Telegram documentation.

## Safety rule

Until all checks above agree, treat the issuance amount, deployment status, code hashes, and permissions as **unverified**. Do not mint, upgrade, rotate authorities, execute a launch batch, or sign a contract transaction based on the conflicting documentation.

## Scope and non-claims

- No WAX mainnet query was performed as part of this documentation change.
- No contract source, ABI, WASM, account permission, token supply, or live chain state was changed.
- This does not supersede the specialist audit requirement.
