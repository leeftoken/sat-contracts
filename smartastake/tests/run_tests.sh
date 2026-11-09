#!/usr/bin/env bash
# Local Spring 1.2.2 chain. Does not touch mainnet.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p tests
: > tests/last-run.log

echo "== build ==" | tee -a tests/last-run.log
./build.sh >> tests/last-run.log 2>&1

for p in /proc/[0-9]*; do
  cmd="$(tr '\0' ' ' < "$p/cmdline" 2>/dev/null || true)"
  case "$cmd" in
    *nodeos*|*keosd*) kill "$(basename "$p")" 2>/dev/null || true ;;
  esac
done
sleep 0.5

rm -rf /tmp/sat-stake-chain /tmp/sat-stake-home
mkdir -p /tmp/sat-stake-chain/data /tmp/sat-stake-chain/config /tmp/sat-stake-home
export HOME=/tmp/sat-stake-home

cleanup() {
  if [[ -f /tmp/sat-stake-nodeos.pid ]]; then
    kill "$(cat /tmp/sat-stake-nodeos.pid)" 2>/dev/null || true
  fi
  if [[ -f /tmp/sat-stake-keosd.pid ]]; then
    kill "$(cat /tmp/sat-stake-keosd.pid)" 2>/dev/null || true
  fi
}
trap cleanup EXIT

setsid nodeos \
  --data-dir /tmp/sat-stake-chain/data \
  --config-dir /tmp/sat-stake-chain/config \
  --genesis-json "$ROOT/tests/genesis.json" \
  --plugin eosio::producer_plugin \
  --plugin eosio::producer_api_plugin \
  --plugin eosio::chain_api_plugin \
  --plugin eosio::http_plugin \
  -p eosio \
  --enable-stale-production \
  --http-server-address 127.0.0.1:8888 \
  --p2p-listen-endpoint 127.0.0.1:9876 \
  --http-validate-host false \
  --access-control-allow-origin '*' \
  --contracts-console \
  --verbose-http-errors \
  --max-transaction-time 2000 \
  --abi-serializer-max-time-ms 1000 \
  --wasm-runtime eos-vm \
  --chain-state-db-size-mb 512 \
  </dev/null >/tmp/sat-stake-nodeos.log 2>&1 &
echo $! >/tmp/sat-stake-nodeos.pid

echo "== wait for nodeos ==" | tee -a tests/last-run.log
ok=0
for _ in $(seq 1 50); do
  if curl -sf http://127.0.0.1:8888/v1/chain/get_info >/dev/null; then
    ok=1
    break
  fi
  sleep 0.2
done
if [[ "$ok" != 1 ]]; then
  echo "nodeos did not start" | tee -a tests/last-run.log >&2
  tail -40 /tmp/sat-stake-nodeos.log | tee -a tests/last-run.log >&2 || true
  exit 1
fi

PUB=EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV
PK=5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3
mkdir -p "$HOME/eosio-wallet"
setsid keosd --wallet-dir "$HOME/eosio-wallet" --unix-socket-path "$HOME/keosd.sock" \
  </dev/null >/tmp/sat-stake-keosd.log 2>&1 &
echo $! >/tmp/sat-stake-keosd.pid
export PATH="$ROOT/tests/bin:$PATH"
for _ in $(seq 1 50); do
  [[ -S "$HOME/keosd.sock" ]] && break
  sleep 0.1
done
cleos wallet create --file "$HOME/wpass" >/dev/null
cleos wallet import --private-key "$PK" >/dev/null

echo "== drive ==" | tee -a tests/last-run.log
set +e
python3 tests/drive.py 2>&1 | tee -a tests/last-run.log
rc=${PIPESTATUS[0]}
set -e
if [[ "$rc" != 0 ]]; then
  echo "tests failed" | tee -a tests/last-run.log >&2
  exit "$rc"
fi
echo "== ALL TESTS PASSED ==" | tee -a tests/last-run.log
