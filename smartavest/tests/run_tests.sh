#!/usr/bin/env bash
# Local nodeos suite for smartavest. Never talks to mainnet or a public testnet.
# Keys are generated per run and stay in /tmp. Do not commit wallet files.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p tests

PASS=0
FAIL=0
API=http://127.0.0.1:8888
WALLET_URL=http://127.0.0.1:8900
DATA=/tmp/sat-vest-data
WALLET_DIR=/tmp/sat-vest-wallet
NODE_LOG=/tmp/sat-vest-nodeos.log
KEOSD_LOG=/tmp/sat-vest-keosd.log
NODE_PID=""
KEOSD_PID=""

pass() { echo "PASS: $1"; PASS=$((PASS + 1)); }
fail() {
  echo "FAIL: $1"
  printf '%s\n' "$2" | tail -n 40
  FAIL=$((FAIL + 1))
}
result_line() { echo "RESULT: $PASS passed, $FAIL failed"; }

cleanup() {
  if [[ -n "${NODE_PID}" ]]; then kill "$NODE_PID" 2>/dev/null || true; fi
  if [[ -n "${KEOSD_PID}" ]]; then kill "$KEOSD_PID" 2>/dev/null || true; fi
  sleep 0.2
  if [[ -n "${NODE_PID}" ]]; then kill -9 "$NODE_PID" 2>/dev/null || true; fi
  if [[ -n "${KEOSD_PID}" ]]; then kill -9 "$KEOSD_PID" 2>/dev/null || true; fi
}
trap cleanup EXIT

c() { cleos --url "$API" --wallet-url "$WALLET_URL" "$@"; }
# Identical action bodies inside one 30s expiration window hash to the same tx id.
cpush() { c push action -f "$@"; }

expect_ok() {
  local name="$1"
  shift
  local out
  if out=$("$@" 2>&1); then
    pass "$name"
  else
    fail "$name" "$out"
  fi
}

expect_fail() {
  local name="$1"
  local needle="$2"
  shift 2
  local out
  if out=$("$@" 2>&1); then
    fail "$name" "expected failure but command succeeded: ${out}"
  elif [[ -n "$needle" && "$out" != *"$needle"* ]]; then
    fail "$name" "missing [$needle] in: ${out}"
  else
    pass "$name"
  fi
}

stop_old() {
  local p cmd
  for p in /proc/[0-9]*; do
    cmd=$(tr '\0' ' ' < "$p/cmdline" 2>/dev/null || true)
    case "$cmd" in
      *nodeos*|*keosd*) kill "$(basename "$p")" 2>/dev/null || true ;;
    esac
  done
  sleep 0.4
}

echo "=== smartavest local tests (no mainnet) ==="
"$ROOT/build.sh"

# 14 — production ABI has no setnow; test ABI has it. Also the prod wasm blob.
python3 - << 'PY'
import json,sys
prod=json.load(open("build/smartavest.abi"))
test=json.load(open("build/smartavest_test.abi"))
pa={a["name"] for a in prod["actions"]}
ta={a["name"] for a in test["actions"]}
pt={t["name"] for t in prod["tables"]}
tt={t["name"] for t in test["tables"]}
ok=True
def check(cond, msg):
    global ok
    print(("PASS: " if cond else "FAIL: ")+msg)
    if not cond:
        ok=False
        print(f"  prod_actions={sorted(pa)} test_actions={sorted(ta)}")
        print(f"  prod_tables={sorted(pt)} test_tables={sorted(tt)}")
check(pa=={"init","openstream","claim"}, "14 production ABI actions are init/openstream/claim only")
check("setnow" not in pa and "testclock" not in pt, "14 production ABI has no setnow and no testclock")
check(ta=={"init","openstream","claim","setnow"}, "test ABI contains setnow")
check("withdraw" not in pa and "revoke" not in pa and "setbeneficiary" not in pa, "ABI has no withdraw/revoke/setbeneficiary")
sys.exit(0 if ok else 1)
PY
# Count the python checks into PASS/FAIL by re-reading is awkward because python printed them.
# The python exit status gates the run; recount from its four PASS lines is done above only on success.
# Fold those four into the shell counters (python already printed PASS lines).
PASS=$((PASS + 4))

if strings build/smartavest.wasm | grep -q setnow; then
  fail "14b production wasm contains setnow" "strings found setnow"
else
  pass "14b production wasm has no setnow string"
fi

stop_old
rm -rf "$DATA" "$WALLET_DIR"
mkdir -p "$DATA" "$WALLET_DIR"

KEY_OUT=$(cleos create key --to-console)
PUB=$(printf '%s\n' "$KEY_OUT" | sed -n 's/^Public key: //p')
PRIV=$(printf '%s\n' "$KEY_OUT" | sed -n 's/^Private key: //p')
[[ -n "$PUB" && -n "$PRIV" ]] || { echo "key generation failed"; exit 1; }

GEN_TS=$(date -u -d '15 seconds ago' +%Y-%m-%dT%H:%M:%S.000)
cat > "$DATA/genesis.json" << EOF
{
  "initial_timestamp": "${GEN_TS}",
  "initial_key": "${PUB}",
  "initial_configuration": {
    "max_block_net_usage": 1048576,
    "target_block_net_usage_pct": 1000,
    "max_transaction_net_usage": 524288,
    "base_per_transaction_net_usage": 12,
    "net_usage_leeway": 500,
    "context_free_discount_net_usage_num": 20,
    "context_free_discount_net_usage_den": 100,
    "max_block_cpu_usage": 200000,
    "target_block_cpu_usage_pct": 10,
    "max_transaction_cpu_usage": 150000,
    "min_transaction_cpu_usage": 100,
    "max_transaction_lifetime": 3600,
    "deferred_trx_expiration_window": 600,
    "max_transaction_delay": 3888000,
    "max_inline_action_size": 524288,
    "max_inline_action_depth": 4,
    "max_authority_depth": 6
  }
}
EOF

nodeos \
  --data-dir "$DATA/data" \
  --config-dir "$DATA" \
  --genesis-json "$DATA/genesis.json" \
  --plugin eosio::chain_api_plugin \
  --plugin eosio::http_plugin \
  --plugin eosio::producer_plugin \
  --http-server-address 127.0.0.1:8888 \
  --p2p-listen-endpoint 127.0.0.1:9876 \
  --access-control-allow-origin '*' \
  --contracts-console \
  --http-validate-host=false \
  --verbose-http-errors \
  --http-max-response-time-ms 10000 \
  --producer-name eosio \
  --enable-stale-production \
  --signature-provider "${PUB}=KEY:${PRIV}" \
  --max-transaction-time 1000 \
  --abi-serializer-max-time-ms 2000 \
  --chain-state-db-size-mb 256 \
  > "$NODE_LOG" 2>&1 &
NODE_PID=$!

keosd --wallet-dir "$WALLET_DIR" --http-server-address 127.0.0.1:8900 --unlock-timeout 99999999 > "$KEOSD_LOG" 2>&1 &
KEOSD_PID=$!

ready=0
for _ in $(seq 1 50); do
  if cleos --url "$API" get info >/tmp/sat-vest-info.json 2>>/tmp/sat-vest-setup.log; then
    if python3 - << 'PY'
import json, datetime
info = json.load(open("/tmp/sat-vest-info.json"))
ht = datetime.datetime.strptime(info["head_block_time"], "%Y-%m-%dT%H:%M:%S.%f")
delta = abs((datetime.datetime.utcnow() - ht).total_seconds())
raise SystemExit(0 if info["head_block_num"] >= 2 and delta < 30 else 1)
PY
    then
      ready=1
      break
    fi
  fi
  sleep 0.2
done
[[ "$ready" == 1 ]] || { echo "nodeos failed to start"; tail -40 "$NODE_LOG"; exit 1; }

for _ in $(seq 1 30); do
  cleos --wallet-url "$WALLET_URL" wallet list >>/tmp/sat-vest-setup.log 2>&1 && break
  sleep 0.1
done

# Wallet password stays in the keosd process, not the repo.
cleos --wallet-url "$WALLET_URL" wallet create --name test --to-console >/tmp/sat-vest-wallet-create.txt
cleos --wallet-url "$WALLET_URL" wallet import --name test --private-key "$PRIV" >/tmp/sat-vest-wallet-import.txt

add_code() {
  local acct="$1"
  c set account permission "$acct" active \
    "{\"threshold\":1,\"keys\":[{\"key\":\"${PUB}\",\"weight\":1}],\"accounts\":[{\"permission\":{\"actor\":\"${acct}\",\"permission\":\"eosio.code\"},\"weight\":1}]}" \
    owner -p "${acct}@owner" >>/tmp/sat-vest-setup.log 2>&1
}

for acct in sat.token bad.token smartavest doubleclaim admin teambenef rewardsben fundbenef attacker calleracct; do
  c create account eosio "$acct" "$PUB" "$PUB" >>/tmp/sat-vest-setup.log 2>&1
done

mkdir -p /tmp/doubleclaim
cdt-cpp -abigen -O3 -o /tmp/doubleclaim/doubleclaim.wasm "$ROOT/tests/doubleclaim.cpp" >/tmp/doubleclaim-build.log 2>&1

c set contract sat.token "$ROOT/build" sat.token.wasm sat.token.abi >>/tmp/sat-vest-setup.log 2>&1
c set contract bad.token "$ROOT/build" sat.token.wasm sat.token.abi >>/tmp/sat-vest-setup.log 2>&1
c set contract smartavest "$ROOT/build" smartavest_test.wasm smartavest_test.abi >>/tmp/sat-vest-setup.log 2>&1
c set contract doubleclaim /tmp/doubleclaim >>/tmp/sat-vest-setup.log 2>&1
add_code smartavest
add_code doubleclaim

cpush sat.token create '{"issuer":"admin","maximum_supply":"100000000.0000 SAT"}' -p sat.token >>/tmp/sat-vest-setup.log 2>&1
cpush sat.token issue '{"to":"admin","quantity":"1000.0000 SAT","memo":"issue"}' -p admin >>/tmp/sat-vest-setup.log 2>&1
cpush sat.token issue '{"to":"attacker","quantity":"5.0000 SAT","memo":"issue"}' -p admin >>/tmp/sat-vest-setup.log 2>&1
cpush bad.token create '{"issuer":"attacker","maximum_supply":"100000000.0000 SAT"}' -p bad.token >>/tmp/sat-vest-setup.log 2>&1
cpush bad.token issue '{"to":"attacker","quantity":"5.0000 SAT","memo":"issue"}' -p attacker >>/tmp/sat-vest-setup.log 2>&1

# --- 1 init ---
expect_fail "1a non-self cannot init" "missing authority" \
  cpush smartavest init '{"admin":"admin","token_contract":"sat.token"}' -p attacker

expect_fail "1b empty admin rejected" "" \
  cpush smartavest init '{"admin":"","token_contract":"sat.token"}' -p smartavest

expect_fail "1c empty token contract rejected" "" \
  cpush smartavest init '{"admin":"admin","token_contract":""}' -p smartavest

expect_ok "1 init once" \
  cpush smartavest init '{"admin":"admin","token_contract":"sat.token"}' -p smartavest

expect_fail "1 second init fails" "already initialized" \
  cpush smartavest init '{"admin":"admin","token_contract":"sat.token"}' -p smartavest

CFG=$(c get table smartavest smartavest config)
if python3 -c 'import json,sys
row=json.loads(sys.argv[1])["rows"][0]
assert row["admin"]=="admin"
assert row["token_contract"]=="sat.token"
assert row["token_symbol"]=="4,SAT"
assert row["inited"] in (1, True, "true")
' "$CFG"; then
  pass "1 config singleton is admin + sat.token + 4,SAT"
else
  fail "1 config singleton is admin + sat.token + 4,SAT" "$CFG"
fi

balance_units() {
  local contract="$1" acct="$2" out
  out=$(c get currency balance "$contract" "$acct" SAT 2>/dev/null || true)
  python3 -c 'import sys
s=sys.argv[1].strip()
if not s:
    print(0); raise SystemExit
whole, frac = s.split()[0].split(".")
frac=(frac+"0000")[:4]
print(int(whole)*10000+int(frac))
' "$out"
}

stream_count() {
  c get table smartavest smartavest streams --limit 100 | python3 -c 'import json,sys; print(len(json.load(sys.stdin)["rows"]))'
}

latest_id() {
  c get table smartavest smartavest streams --limit 100 | python3 -c 'import json,sys
rows=json.load(sys.stdin)["rows"]
print(max(int(r["id"]) for r in rows))'
}

row_json() {
  local id="$1"
  c get table smartavest smartavest streams --limit 100 | python3 -c 'import json,sys
want=int(sys.argv[1])
for r in json.load(sys.stdin)["rows"]:
    if int(r["id"])==want:
        json.dump(r, sys.stdout)
        raise SystemExit
raise SystemExit(1)
' "$id"
}

assert_eq() {
  local name="$1" got="$2" want="$3"
  if [[ "$got" == "$want" ]]; then
    pass "$name"
  else
    fail "$name" "got [$got] want [$want]"
  fi
}

# --- 2 open via memo ---
S0=1700000000
expect_ok "setnow stream0 start" cpush smartavest setnow "{\"t\":${S0}}" -p smartavest
BEFORE=$(stream_count)
expect_ok "2 open memo deposit" \
  cpush sat.token transfer "{\"from\":\"admin\",\"to\":\"smartavest\",\"quantity\":\"10.0000 SAT\",\"memo\":\"open:teambenef:team:1000:1000:${S0}\"}" -p admin
AFTER=$(stream_count)
assert_eq "2 stream count increased by 1" "$AFTER" "$((BEFORE + 1))"
ID0=$(latest_id)
ROW=$(row_json "$ID0")
if python3 -c 'import json,sys
r=json.loads(sys.argv[1])
assert r["beneficiary"]=="teambenef"
assert r["category"]=="team"
assert int(r["total"])==100000
assert int(r["claimed"])==0
assert int(r["start"])==1700000000
assert int(r["cliff_secs"])==1000
assert int(r["duration_secs"])==1000
' "$ROW"; then
  pass "2 stream fields match memo (total 10.0000 SAT, cliff 1000, duration 1000)"
else
  fail "2 stream fields match memo" "$ROW"
fi

# --- 3 claim before cliff ---
BEN_BEFORE=$(balance_units sat.token teambenef)
expect_ok "setnow inside cliff" cpush smartavest setnow '{"t":1700000999}' -p smartavest
expect_fail "3 claim before cliff" "still in cliff" \
  cpush smartavest claim "{\"id\":${ID0}}" -p calleracct
BEN_AFTER=$(balance_units sat.token teambenef)
assert_eq "3 beneficiary balance unchanged" "$BEN_AFTER" "$BEN_BEFORE"

# --- 15 same-tx double claim, then 4 partial, then back-to-back ---
expect_ok "setnow 50% after cliff" cpush smartavest setnow '{"t":1700001500}' -p smartavest
expect_fail "15 two claims in one transaction do not pay" "nothing to claim" \
  cpush doubleclaim go "{\"id\":${ID0}}" -p doubleclaim
CLAIMED_NOW=$(python3 -c 'import json,sys; print(int(json.loads(sys.argv[1])["claimed"]))' "$(row_json "$ID0")")
assert_eq "15 claimed still 0 after rolled-back double claim" "$CLAIMED_NOW" "0"
assert_eq "15 beneficiary still unpaid" "$(balance_units sat.token teambenef)" "0"

expect_ok "4/9 permissionless claim by non-beneficiary" \
  cpush smartavest claim "{\"id\":${ID0}}" -p calleracct
assert_eq "4 paid floor(total/2) = 5.0000 SAT" "$(balance_units sat.token teambenef)" "50000"
assert_eq "9 caller received nothing" "$(balance_units sat.token calleracct)" "0"
CLAIMED_NOW=$(python3 -c 'import json,sys; print(int(json.loads(sys.argv[1])["claimed"]))' "$(row_json "$ID0")")
assert_eq "4 claimed updated to 50000" "$CLAIMED_NOW" "50000"

expect_fail "4/15 second claim at same timestamp pays nothing" "nothing to claim" \
  cpush smartavest claim "{\"id\":${ID0}}" -p attacker
assert_eq "4/15 balance unchanged after second claim" "$(balance_units sat.token teambenef)" "50000"

# --- 5 full amount at end ---
expect_ok "setnow at vest end" cpush smartavest setnow '{"t":1700002000}' -p smartavest
expect_ok "5 claim remainder at end" cpush smartavest claim "{\"id\":${ID0}}" -p calleracct
assert_eq "5 beneficiary received full total" "$(balance_units sat.token teambenef)" "100000"
CLAIMED_NOW=$(python3 -c 'import json,sys; print(int(json.loads(sys.argv[1])["claimed"]))' "$(row_json "$ID0")")
assert_eq "5 claimed equals total" "$CLAIMED_NOW" "100000"
expect_fail "5 further claim fails" "nothing to claim" \
  cpush smartavest claim "{\"id\":${ID0}}" -p calleracct
assert_eq "5 no overpay after end" "$(balance_units sat.token teambenef)" "100000"

# --- 6 awkward division sums to exactly total ---
S1=1800000000
expect_ok "6 open rounding stream" \
  cpush sat.token transfer "{\"from\":\"admin\",\"to\":\"smartavest\",\"quantity\":\"1.0000 SAT\",\"memo\":\"open:rewardsben:rewards:0:3:${S1}\"}" -p admin
ID1=$(latest_id)
expect_ok "6a setnow elapsed 1" cpush smartavest setnow '{"t":1800000001}' -p smartavest
expect_ok "6a claim" cpush smartavest claim "{\"id\":${ID1}}" -p calleracct
assert_eq "6a floor(10000*1/3)=3333" "$(balance_units sat.token rewardsben)" "3333"
expect_ok "6b setnow elapsed 2" cpush smartavest setnow '{"t":1800000002}' -p smartavest
expect_ok "6b claim" cpush smartavest claim "{\"id\":${ID1}}" -p calleracct
assert_eq "6b cumulative 6666" "$(balance_units sat.token rewardsben)" "6666"
expect_ok "6c setnow at end" cpush smartavest setnow '{"t":1800000003}' -p smartavest
expect_ok "6c claim remainder" cpush smartavest claim "{\"id\":${ID1}}" -p calleracct
assert_eq "6 claims sum to exactly total" "$(balance_units sat.token rewardsben)" "10000"
expect_fail "6 further claim fails" "nothing to claim" \
  cpush smartavest claim "{\"id\":${ID1}}" -p calleracct
assert_eq "6 still exactly total" "$(balance_units sat.token rewardsben)" "10000"

# --- 10 fund top-up ---
S2=1900000000
expect_ok "10 openstream unfunded" \
  cpush smartavest openstream "{\"beneficiary\":\"fundbenef\",\"category\":\"topup\",\"start\":${S2},\"cliff_secs\":0,\"duration_secs\":1000}" -p admin
ID2=$(latest_id)
ROW=$(row_json "$ID2")
assert_eq "10 new stream total is 0" "$(python3 -c 'import json,sys; print(int(json.loads(sys.argv[1])["total"]))' "$ROW")" "0"
expect_ok "10 fund first 10 SAT" \
  cpush sat.token transfer "{\"from\":\"admin\",\"to\":\"smartavest\",\"quantity\":\"10.0000 SAT\",\"memo\":\"fund:${ID2}\"}" -p admin
expect_ok "10 fund top-up 10 SAT" \
  cpush sat.token transfer "{\"from\":\"admin\",\"to\":\"smartavest\",\"quantity\":\"10.0000 SAT\",\"memo\":\"fund:${ID2}\"}" -p admin
ROW=$(row_json "$ID2")
assert_eq "10 total is 20.0000 SAT after top-up" "$(python3 -c 'import json,sys; print(int(json.loads(sys.argv[1])["total"]))' "$ROW")" "200000"

expect_ok "10 setnow at start (nothing vested)" cpush smartavest setnow "{\"t\":${S2}}" -p smartavest
expect_fail "10 cannot claim unvested at elapsed 0" "nothing to claim" \
  cpush smartavest claim "{\"id\":${ID2}}" -p calleracct
assert_eq "10 fundbenef unpaid at start" "$(balance_units sat.token fundbenef)" "0"

expect_ok "10 setnow 50% of duration" cpush smartavest setnow '{"t":1900000500}' -p smartavest
expect_ok "10 claim half of new total" cpush smartavest claim "{\"id\":${ID2}}" -p calleracct
assert_eq "10 claimable uses new total (10.0000 of 20.0000)" "$(balance_units sat.token fundbenef)" "100000"
expect_fail "10 cannot claim the unvested half" "nothing to claim" \
  cpush smartavest claim "{\"id\":${ID2}}" -p calleracct
assert_eq "10 no extra pay" "$(balance_units sat.token fundbenef)" "100000"
expect_fail "10 missing stream rejected" "stream not found" \
  cpush sat.token transfer '{"from":"admin","to":"smartavest","quantity":"1.0000 SAT","memo":"fund:9999"}' -p admin

# --- 7 wrong token ---
COUNT_BEFORE=$(stream_count)
ATK_BAD_BEFORE=$(balance_units bad.token attacker)
expect_fail "7 wrong token contract rejected" "wrong token contract" \
  cpush bad.token transfer '{"from":"attacker","to":"smartavest","quantity":"1.0000 SAT","memo":"open:teambenef:team:0:1000:1700000000"}' -p attacker
assert_eq "7 stream count unchanged" "$(stream_count)" "$COUNT_BEFORE"
assert_eq "7 bad.token balance unchanged" "$(balance_units bad.token attacker)" "$ATK_BAD_BEFORE"

# --- 8 wrong precision / zero ---
ADMIN_BEFORE=$(balance_units sat.token admin)
expect_fail "8 wrong precision rejected" "symbol precision mismatch" \
  cpush sat.token transfer '{"from":"admin","to":"smartavest","quantity":"1.00 SAT","memo":"open:teambenef:team:0:1000:1700000000"}' -p admin
expect_fail "8 zero amount rejected" "must transfer positive quantity" \
  cpush sat.token transfer '{"from":"admin","to":"smartavest","quantity":"0.0000 SAT","memo":"fund:0"}' -p admin
assert_eq "8 admin balance unchanged" "$(balance_units sat.token admin)" "$ADMIN_BEFORE"
assert_eq "8 stream count still unchanged" "$(stream_count)" "$COUNT_BEFORE"

# --- 11 unknown memo ---
expect_fail "11 unknown memo rejected" "unknown memo" \
  cpush sat.token transfer '{"from":"admin","to":"smartavest","quantity":"1.0000 SAT","memo":"hello"}' -p admin
expect_fail "11 empty memo rejected" "unknown memo" \
  cpush sat.token transfer '{"from":"admin","to":"smartavest","quantity":"1.0000 SAT","memo":""}' -p admin
assert_eq "11 stream count unchanged" "$(stream_count)" "$COUNT_BEFORE"
assert_eq "11 admin balance unchanged" "$(balance_units sat.token admin)" "$ADMIN_BEFORE"

# --- 12 non-admin open ---
ATK_BEFORE=$(balance_units sat.token attacker)
expect_fail "12 non-admin memo open rejected" "only admin may open" \
  cpush sat.token transfer '{"from":"attacker","to":"smartavest","quantity":"1.0000 SAT","memo":"open:attacker:team:0:1000:1700000000"}' -p attacker
expect_fail "12 non-admin openstream rejected" "missing authority" \
  cpush smartavest openstream '{"beneficiary":"attacker","category":"team","start":1700000000,"cliff_secs":0,"duration_secs":1000}' -p attacker
assert_eq "12 attacker balance unchanged" "$(balance_units sat.token attacker)" "$ATK_BEFORE"
assert_eq "12 stream count unchanged" "$(stream_count)" "$COUNT_BEFORE"

# --- 13 overflow ---
expect_fail "13 openstream overflow rejected" "schedule overflows uint32" \
  cpush smartavest openstream '{"beneficiary":"teambenef","category":"team","start":4000000000,"cliff_secs":4000000000,"duration_secs":10}' -p admin
expect_fail "13 duration 0 rejected" "duration must be positive" \
  cpush smartavest openstream '{"beneficiary":"teambenef","category":"team","start":1700000000,"cliff_secs":0,"duration_secs":0}' -p admin
expect_fail "13 memo schedule overflow rejected" "schedule overflows uint32" \
  cpush sat.token transfer '{"from":"admin","to":"smartavest","quantity":"1.0000 SAT","memo":"open:teambenef:team:4000000000:4000000000:4000000000"}' -p admin
assert_eq "13 stream count unchanged" "$(stream_count)" "$COUNT_BEFORE"

# Documented team / rewards constants must be accepted (365d cliff, 1095d linear, 1461d rewards).
TEAM_CLIFF=$((365 * 86400))
TEAM_DUR=$((1095 * 86400))
REW_DUR=$((1461 * 86400))
expect_ok "team constants accepted (365d cliff + 1095d linear)" \
  cpush smartavest openstream "{\"beneficiary\":\"teambenef\",\"category\":\"team\",\"start\":1700000000,\"cliff_secs\":${TEAM_CLIFF},\"duration_secs\":${TEAM_DUR}}" -p admin
expect_ok "rewards constants accepted (cliff 0 + 1461d linear)" \
  cpush smartavest openstream "{\"beneficiary\":\"rewardsben\",\"category\":\"rewards\",\"start\":1700000000,\"cliff_secs\":0,\"duration_secs\":${REW_DUR}}" -p admin

# Contract still holds exactly the unvested half of the top-up stream (10.0000 SAT).
assert_eq "vault balance equals unvested remainder" "$(balance_units sat.token smartavest)" "100000"
assert_eq "admin spent exactly 31.0000 SAT" "$(balance_units sat.token admin)" "9690000"

result_line
if [[ "$FAIL" -ne 0 ]]; then
  echo "nodeos log tail:"
  tail -n 30 "$NODE_LOG" || true
  exit 1
fi
