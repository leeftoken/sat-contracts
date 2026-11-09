#!/usr/bin/env bash
# Build production wasm (no test clock) and the test wasm (setnow included).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build build/obj

echo "production smartavest (no SAT_VEST_TEST_CLOCK)"
cdt-cpp -abigen -O3 -o build/smartavest.wasm smartavest/smartavest.cpp

echo "test smartavest (-DSAT_VEST_TEST_CLOCK)"
# -o basename must match the contract name or abigen rejects the wasm.
rm -rf build/obj/test
mkdir -p build/obj/test
cdt-cpp -abigen -O3 -DSAT_VEST_TEST_CLOCK -o build/obj/test/smartavest.wasm smartavest/smartavest.cpp
mv -f build/obj/test/smartavest.wasm build/smartavest_test.wasm
mv -f build/obj/test/smartavest.abi build/smartavest_test.abi

echo "sat.token"
cdt-cpp -abigen -O3 -o build/sat.token.wasm sat.token/sat.token.cpp

python3 - << 'PY'
import json
prod = json.load(open("build/smartavest.abi"))
test = json.load(open("build/smartavest_test.abi"))
prod_actions = {a["name"] for a in prod.get("actions", [])}
test_actions = {a["name"] for a in test.get("actions", [])}
prod_tables = {t["name"] for t in prod.get("tables", [])}
test_tables = {t["name"] for t in test.get("tables", [])}
assert prod_actions == {"init", "openstream", "claim"}, prod_actions
assert "setnow" not in prod_actions
assert "testclock" not in prod_tables
assert test_actions == {"init", "openstream", "claim", "setnow"}, test_actions
assert "streams" in prod_tables and "config" in prod_tables
assert "testclock" in test_tables
print("abi check ok")
print("prod actions:", sorted(prod_actions))
print("test actions:", sorted(test_actions))
print("prod tables:", sorted(prod_tables))
print("test tables:", sorted(test_tables))
PY

rm -rf build/obj
echo "built build/smartavest.wasm and build/smartavest_test.wasm"
