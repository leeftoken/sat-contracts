#!/usr/bin/env bash
# Build all contracts with AntelopeIO CDT (tested with cdt 4.1.1).
# Set CDT_BIN if cdt-cpp is not on PATH, e.g. CDT_BIN=/workspace/.antelope/cdt/usr/bin
set -euo pipefail
cd "$(dirname "$0")"
[ -n "${CDT_BIN:-}" ] && export PATH="$CDT_BIN:$PATH"
command -v cdt-cpp >/dev/null || { echo "cdt-cpp not found (install AntelopeIO CDT)"; exit 1; }
mkdir -p build
for c in sat.token leeftrader mocks/mockswap; do
  n=$(basename "$c")
  echo "building $n"
  cdt-cpp -abigen -O3 -o "build/$n.wasm" "$c/$n.cpp" 2>&1 | grep -v -i ricardian || true
  test -s "build/$n.wasm" && test -s "build/$n.abi"
done
ls -la build
