#!/usr/bin/env bash
# CDT 4.1.1 production build plus a test-clock build used only by tests/.
# Mocks are local only. They are not sattoken1111 or smartatrader.
set -euo pipefail
cd "$(dirname "$0")"
[ -n "${CDT_BIN:-}" ] && export PATH="$CDT_BIN:$PATH"

if ! cdt-cpp --version | grep -q "4.1.1"; then
  echo "cdt-cpp 4.1.1 required" >&2
  cdt-cpp --version >&2 || true
  exit 1
fi

mkdir -p build build/test
cdt-cpp -abigen -O3 -o build/smartastake.wasm smartastake/smartastake.cpp
cdt-cpp -abigen -O3 -DSMARTASTAKE_TEST_CLOCK -o build/test/smartastake.wasm smartastake/smartastake.cpp
cdt-cpp -abigen -O3 -o build/sat.token.wasm mocks/sat.token/sat.token.cpp
cdt-cpp -abigen -O3 -o build/trader.wasm mocks/trader/trader.cpp
cdt-cpp -abigen -O3 -o build/bios.wasm mocks/bios/bios.cpp

python3 - << 'PY'
import json
prod = json.load(open("build/smartastake.abi"))
test = json.load(open("build/test/smartastake.abi"))
prod_actions = {a["name"] for a in prod["actions"]}
test_actions = {a["name"] for a in test["actions"]}
assert "settime" not in prod_actions, prod_actions
assert "settime" in test_actions
assert "withdrawadmin" not in prod_actions
assert "rescue" not in prod_actions
print("abi ok; prod actions:", ", ".join(sorted(prod_actions)))
PY
