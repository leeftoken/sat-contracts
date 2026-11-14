#!/usr/bin/env bash
# Review integration suite: smartavest + smartastake against the repo's real
# sat.token (sattoken1111) and leeftrader (smartatrader) wasm on a throwaway
# local Spring chain. Never talks to mainnet or a public testnet.
#
#   CDT_BIN=/path/to/cdt/bin SPRING_BIN=/path/to/spring/bin ./integration/run.sh
#
# MODE=fixed (default) expects the fixes on this branch. MODE=base expects the
# pre-review smartastake behaviour (use with STAKE_SRC pointing at that source).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/integration"
[ -n "${CDT_BIN:-}" ] && export PATH="$CDT_BIN:$PATH"
[ -n "${SPRING_BIN:-}" ] && export PATH="$SPRING_BIN:$PATH"
cdt-cpp --version | grep -q "4.1.1" || { echo "cdt-cpp 4.1.1 required" >&2; exit 1; }
STAKE_SRC="${STAKE_SRC:-$ROOT/smartastake/smartastake/smartastake.cpp}"
export MODE="${MODE:-fixed}"
PORT="${PORT:-8891}"
P2P="${P2P:-9881}"
WORK="$(mktemp -d /tmp/sat-review-XXXXXX)"
LOG="$ROOT/integration/last-run.log"
: > "$LOG"

echo "== build (CDT $(cdt-cpp --version | awk '{print $NF}')) ==" | tee -a "$LOG"
B="$ROOT/integration/build"
rm -rf "$B"; mkdir -p "$B"/{token,trader,naive,safe,vest,stake,rejector,bios}
python3 make_trader_patches.py >>"$LOG"
q() { cdt-cpp -abigen -O3 "$@" 2>&1 | grep -v -i ricardian >>"$LOG" || true; }
q -o "$B/token/sat.token.wasm" "$ROOT/sat.token/sat.token.cpp"
q -o "$B/trader/leeftrader.wasm" "$ROOT/leeftrader/leeftrader.cpp"
q -o "$B/naive/leeftrader.wasm" "$B/patch/naive/leeftrader.cpp"
q -o "$B/safe/leeftrader.wasm" "$B/patch/safe/leeftrader.cpp"
q -DSAT_VEST_TEST_CLOCK -o "$B/vest/smartavest.wasm" "$ROOT/smartavest/smartavest/smartavest.cpp"
q -DSMARTASTAKE_TEST_CLOCK -I "$ROOT/smartastake/smartastake" -o "$B/stake/smartastake.wasm" "$STAKE_SRC"
q -o "$B/rejector/rejector.wasm" rejector/rejector.cpp
q -o "$B/bios/bios.wasm" "$ROOT/smartastake/mocks/bios/bios.cpp"
for f in token/sat.token trader/leeftrader naive/leeftrader safe/leeftrader vest/smartavest stake/smartastake rejector/rejector bios/bios; do
  test -s "$B/$f.wasm" && test -s "$B/$f.abi" || { echo "build failed: $f" | tee -a "$LOG"; exit 1; }
done
sha256sum "$B"/token/sat.token.wasm "$B"/trader/leeftrader.wasm | tee -a "$LOG"

cleanup() {
  [[ -f "$WORK/nodeos.pid" ]] && kill "$(cat "$WORK/nodeos.pid")" 2>/dev/null || true
  [[ -f "$WORK/keosd.pid" ]] && kill "$(cat "$WORK/keosd.pid")" 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

export HOME="$WORK/home"; mkdir -p "$HOME/wallet" "$WORK/data" "$WORK/config"
setsid keosd --wallet-dir "$HOME/wallet" --unix-socket-path "$HOME/keosd.sock" </dev/null >"$WORK/keosd.log" 2>&1 &
echo $! >"$WORK/keosd.pid"
for _ in $(seq 1 50); do [[ -S "$HOME/keosd.sock" ]] && break; sleep 0.1; done
export CLEOS="$(command -v cleos)"
export URL="http://127.0.0.1:$PORT" WALLET="unix://$HOME/keosd.sock"
c() { "$CLEOS" --no-auto-keosd --wallet-url "$WALLET" "$@"; }
c wallet create --file "$HOME/wpass" >/dev/null
# One throwaway key per run, generated here and used for every local account.
KEYS="$(c create key --to-console)"
c wallet import --private-key "$(echo "$KEYS" | awk '/Private key/{print $3}')" >/dev/null
export PUB="$(echo "$KEYS" | awk '/Public key/{print $3}')"
PRIV="$(echo "$KEYS" | awk '/Private key/{print $3}')"
unset KEYS
python3 - "$ROOT/smartastake/tests/genesis.json" "$WORK/genesis.json" "$PUB" <<'PY2'
import json, sys
g = json.load(open(sys.argv[1])); g["initial_key"] = sys.argv[3]
json.dump(g, open(sys.argv[2], "w"))
PY2

setsid nodeos --data-dir "$WORK/data" --config-dir "$WORK/config" \
  --genesis-json "$WORK/genesis.json" \
  --plugin eosio::producer_plugin --plugin eosio::producer_api_plugin \
  --plugin eosio::chain_api_plugin --plugin eosio::http_plugin \
  -p eosio --enable-stale-production --signature-provider "$PUB=KEY:$PRIV" \
  --http-server-address "127.0.0.1:$PORT" --p2p-listen-endpoint "127.0.0.1:$P2P" \
  --http-validate-host false --contracts-console --verbose-http-errors \
  --max-transaction-time 2000 --abi-serializer-max-time-ms 1000 \
  --wasm-runtime eos-vm --chain-state-db-size-mb 512 \
  </dev/null >"$WORK/nodeos.log" 2>&1 &
echo $! >"$WORK/nodeos.pid"
for _ in $(seq 1 75); do curl -sf "$URL/v1/chain/get_info" >/dev/null && break; sleep 0.2; done
curl -sf "$URL/v1/chain/get_info" >/dev/null || { tail -30 "$WORK/nodeos.log"; exit 1; }
unset PRIV
# Wait for a few blocks so head time is "now", not the genesis timestamp.
for _ in $(seq 1 50); do
  n="$(curl -sf "$URL/v1/chain/get_info" | python3 -c 'import json,sys; print(json.load(sys.stdin)["head_block_num"])' 2>/dev/null || echo 0)"
  [[ "$n" -ge 3 ]] && break; sleep 0.2
done

echo "== drive (MODE=$MODE) ==" | tee -a "$LOG"
set +e
python3 drive.py 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}
set -e
[[ "$rc" == 0 ]] || { echo "review suite failed" | tee -a "$LOG"; exit "$rc"; }
echo "== REVIEW SUITE PASSED ==" | tee -a "$LOG"
