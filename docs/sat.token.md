# sat.token (`sattoken1111`)

## Purpose

Standard eosio.token-compatible SAT token on WAX. Wallets, Alcor, and explorers treat SAT like any other WAX token. The contract is the Antelope reference `create` / `issue` / `retire` / `transfer` / `open` / `close` surface, with no custom hooks beyond ordinary `require_recipient` notifications.

**Live** on WAX mainnet as `sattoken1111`. Source: `sat.token/sat.token.cpp`. Code hash `e1702842a5cc049c592abc92aabfbd2e02325c709b401629f5b3634d2dcf7dbc` matches a CDT 4.1.1 rebuild of this repo. **Not audited yet** (see [AUDIT-REPORT.md](../AUDIT-REPORT.md)).

| Property | Value |
|---|---|
| Symbol | `SAT`, precision 4 |
| Max supply | `100000000.0000 SAT` (fixed, no inflation) |
| Issued today | `20000000.0000 SAT` |
| Still mintable today | `80000000.0000 SAT` |
| Issuer | `smartaadmin1` |
| After the approved lock batches | Supply equals max supply; nothing left to mint |

The remaining 80M is the lock plan in [README.md](README.md#approved-allocation-2026-11-14). Issuing it waits for an external audit and owner approval of each batch.

## Accounts and permissions

| Actor | Authority needed |
|---|---|
| Contract account `sattoken1111` | `create` (once per symbol) is `require_auth(get_self())`. |
| Issuer `smartaadmin1` | `issue` and `retire`. Tokens can only be issued **to the issuer**, who then transfers them onward. |
| Token holder | `transfer` (from), `close` (owner). |
| RAM payer | `open` is paid and authorized by `ram_payer`. |
| Recipients | `transfer` notifies both `from` and `to` (`require_recipient`), so contracts such as `smartavest11` and `smartastake1` can handle deposits in their `on_notify` handlers. |

`active` on `sattoken1111` does not need `eosio.code` for the token itself (it does not send inline transfers). Downstream contracts that send SAT out of their own accounts need `eosio.code` on **their** active permission.

## Tables

### `stat` (scope = symbol code, i.e. `SAT`)

| Field | Type | Meaning |
|---|---|---|
| `supply` | `asset` | Currently issued SAT, including the symbol. |
| `max_supply` | `asset` | Ceiling, `100000000.0000 SAT`. |
| `issuer` | `name` | `smartaadmin1`. |

Primary key: symbol code.

```bash
cleos -u https://wax.greymass.com get currency stats sattoken1111 SAT
cleos -u https://wax.greymass.com get table sattoken1111 SAT stat
```

### `accounts` (scope = owner)

| Field | Type | Meaning |
|---|---|---|
| `balance` | `asset` | That account's SAT balance. |

Primary key: symbol code. A missing row means a zero balance that was never `open`ed or was `close`d.

```bash
cleos -u https://wax.greymass.com get currency balance sattoken1111 ACCOUNT SAT
cleos -u https://wax.greymass.com get table sattoken1111 ACCOUNT accounts
```

```js
const rows = await fetch('https://wax.greymass.com/v1/chain/get_table_rows', {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({
    json: true, code: 'sattoken1111', scope: 'ACCOUNT', table: 'accounts', limit: 10
  })
}).then((r) => r.json());
```

## Actions

### `create(issuer, maximum_supply)`

- **Auth:** `sattoken1111@active` (the contract itself).
- **Params:** `issuer` (`name`), `maximum_supply` (`asset`, must be positive, valid symbol).
- **Effects:** Writes the one `stat` row for that symbol. Supply starts at zero with the same symbol. Cannot be called again for `SAT`.
- **Errors:** `invalid symbol name`, `invalid supply`, `max-supply must be positive`, `token with symbol already exists`.
- **Status:** Already executed on mainnet. Do not call it again.

```bash
# Already done on mainnet. Shown only as the historical shape.
cleos push action sattoken1111 create '{"issuer":"smartaadmin1","maximum_supply":"100000000.0000 SAT"}' -p sattoken1111@active
```

```js
{ account: 'sattoken1111', name: 'create',
  authorization: [{ actor: 'sattoken1111', permission: 'active' }],
  data: { issuer: 'smartaadmin1', maximum_supply: '100000000.0000 SAT' } }
```

### `issue(to, quantity, memo)`

- **Auth:** the issuer (`smartaadmin1`).
- **Params:** `to` must equal the issuer; `quantity` must be a positive SAT amount that fits under `max_supply - supply`; `memo` at most 256 bytes.
- **Effects:** Increases `stat.supply` and credits the issuer's `accounts` row.
- **Errors:** `invalid symbol name`, `memo has more than 256 bytes`, `token with symbol does not exist, create token before issue`, `tokens can only be issued to issuer account`, `must issue positive quantity`, `symbol precision mismatch`, `quantity exceeds available supply`.

```bash
# Example only. Do not sign issuance until the audit and the approved lock batches.
cleos push action sattoken1111 issue '{"to":"smartaadmin1","quantity":"1250000.0000 SAT","memo":"SAT lock: airdrop fund seed (5% of rewards)"}' -p smartaadmin1@active
```

```js
{ account: 'sattoken1111', name: 'issue',
  authorization: [{ actor: 'smartaadmin1', permission: 'active' }],
  data: { to: 'smartaadmin1', quantity: '1250000.0000 SAT', memo: 'SAT lock: airdrop fund seed (5% of rewards)' } }
```

A mainnet `compute_transaction` on 2026-11-14 (no signatures) accepted a combined issue of `80000000.0000 SAT` and rejected `80000000.0001 SAT` with `quantity exceeds available supply`.

### `retire(quantity, memo)`

- **Auth:** the issuer.
- **Params:** positive SAT amount the issuer currently holds; memo ≤ 256 bytes.
- **Effects:** Decreases `stat.supply` and debits the issuer's balance. Used to burn issuer-held SAT, not user balances (users would first transfer SAT to the issuer).
- **Errors:** `memo has more than 256 bytes`, `token with symbol does not exist`, `must retire positive quantity`, `symbol precision mismatch`, plus ordinary `overdrawn balance` / `no balance object found`.

```bash
cleos push action sattoken1111 retire '{"quantity":"1.0000 SAT","memo":"example retire"}' -p smartaadmin1@active
```

The lock plan does not retire SAT. The allocation is issued into contracts and streams instead.

### `transfer(from, to, quantity, memo)`

- **Auth:** `from`.
- **Params:** `from != to`; `to` must exist; positive SAT amount; memo ≤ 256 bytes.
- **Effects:** Debits `from`, credits `to` (opens the destination row if needed). RAM for a new destination row is paid by `to` if `to` also signed, otherwise by `from`. Notifies both accounts, which is how vesting and staking deposits are recognized.
- **Errors:** `cannot transfer to self`, `to account does not exist`, `token does not exist`, `must transfer positive quantity`, `symbol precision mismatch`, `memo has more than 256 bytes`, `no balance object found`, `overdrawn balance`.

```bash
cleos push action sattoken1111 transfer '{"from":"USER","to":"smartastake1","quantity":"100.0000 SAT","memo":"stake"}' -p USER@active
```

```js
{ account: 'sattoken1111', name: 'transfer',
  authorization: [{ actor: 'USER', permission: 'active' }],
  data: { from: 'USER', to: 'smartastake1', quantity: '100.0000 SAT', memo: 'stake' } }
```

Memos that other SAT contracts understand are listed in [smartavest.md](smartavest.md#notify-handlers-and-memo-formats) and [smartastake.md](smartastake.md#notify-handlers-and-memo-formats). This token contract itself does not interpret the memo.

### `open(owner, symbol, ram_payer)`

- **Auth:** `ram_payer`.
- **Params:** `owner` must exist; `symbol` must be the live SAT symbol (`4,SAT`).
- **Effects:** Creates a zero `accounts` row for `owner` if it does not already exist, billed to `ram_payer`. Idempotent if the row is already there.
- **Errors:** `owner account does not exist`, `symbol does not exist`, `symbol precision mismatch`.

```bash
cleos push action sattoken1111 open '{"owner":"USER","symbol":"4,SAT","ram_payer":"USER"}' -p USER@active
```

### `close(owner, symbol)`

- **Auth:** `owner`.
- **Params:** the owner's SAT `accounts` row must exist and be exactly zero.
- **Effects:** Deletes the row and frees that RAM.
- **Errors:** `Balance row already deleted or never existed. Action won't have any effect.`, `Cannot close because the balance is not zero.`

```bash
cleos push action sattoken1111 close '{"owner":"USER","symbol":"4,SAT"}' -p USER@active
```

## Notify handlers and memo formats

This contract has no `on_notify` handler. It **emits** transfer notifications to `from` and `to`. Downstream memo formats:

| Destination | Memo | Result |
|---|---|---|
| `smartavest11` | `open:<beneficiary>:<category>:<cliff_secs>:<duration_secs>[:start]` | Admin opens and funds a stream. |
| `smartavest11` | `fund:<stream_id>` | Anyone tops up that stream. |
| `smartastake1` | `stake` or empty | Credits the sender's stake. |
| `smartastake1` | `reward` | Funds the reward pot. SAT from `smartatrader` is always reward, whatever the memo. |
| `smartastake1` | any other memo (e.g. `vesting claim`) | Reward only if `from` is on the funder list; otherwise the transfer reverts. |
| `smartatrader` | `sub:<plan_id>[:<user>]` | Pays a subscription, then the amount is split into buckets. |
| `smartatrader` | any other memo | Treated as revenue and split, if the token is whitelisted. |

## Parameters and bounds

- Precision is 4. Amounts such as `10000.0000 SAT` are 100,000,000 smallest units.
- Memo length ≤ 256 bytes on `issue`, `retire`, and `transfer`.
- `max_supply` is immutable after `create`. There is no inflate action.

## Fees and splits

None in this contract. Fee splits live on `smartatrader` and, for bot knobs, on `smartastake1`.

## Security model and known limits

- **Not audited yet.** Behaviour matches the eosio.token reference, and the live wasm hash matches this source, but that is not a substitute for an external audit.
- Only the issuer can mint, and only to itself, up to `max_supply`. Until the lock batches are signed, 80M SAT remains mintable.
- There is no pause, blacklist, or fee-on-transfer.
- Transfer notifications are the integration surface. A destination contract that rejects an unexpected memo or symbol will revert the whole transfer, which is intended for `smartavest11` (it must not hold SAT outside a stream) and for `smartastake1` (unknown memos from non-funders are rejected).
- Do not send WAX or other tokens to a SAT contract account and expect this contract to handle them.

## User guide

Hold SAT in any WAX wallet that supports eosio.token transfers. To stake, transfer SAT to `smartastake1` with memo `stake` once that contract is live (see [smartastake.md](smartastake.md#user-guide)). To check a balance:

```bash
cleos -u https://wax.greymass.com get currency balance sattoken1111 YOURACCOUNT SAT
```

## Operator guide

- Issue only to `smartaadmin1`, then transfer to the destination with the memo that contract expects.
- Follow the launch order in [README.md](README.md#deploy-and-launch-order): seed and stake the airdrop fund (B2) before opening the rewards stream (B4), so no early staker can take held rewards.
- Keep RAM and CPU on `sattoken1111` healthy so transfers do not fail under load. CPU for this account was topped up on 2026-11-14; re-check if transfers start billing high CPU.
- Retiring SAT is possible but is not part of the approved allocation.

## Deploy and launch order

`sattoken1111` is already live (`create` and the first 20M issued, including the 10% community airdrop and the SAT that went into locked LP). The remaining issuance is B2 then B4, after the audit, as listed in the [system overview](README.md#deploy-and-launch-order).
