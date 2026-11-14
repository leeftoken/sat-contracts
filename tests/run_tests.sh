#!/usr/bin/env bash
# Local-chain tests for sat.token + leeftrader (with mockswap standing in for swap.alcor).
# Uses a throwaway nodeos + keosd in a temp dir and the well-known local dev key only.
# Never points at mainnet or testnet. Needs nodeos/cleos/keosd (AntelopeIO Spring) on PATH or SPRING_BIN.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -n "${SPRING_BIN:-}" ] && export PATH="$SPRING_BIN:$PATH"
T=$(mktemp -d); PORT=${PORT:-18888}; WPORT=$((PORT+1))
KEYS=$(cleos create key --to-console)   # fresh throwaway key per run, never stored
DEVPUB=$(echo "$KEYS" | awk '/Public/{print $3}'); DEVPRIV=$(echo "$KEYS" | awk '/Private/{print $3}')
printf '{"initial_timestamp":"2026-01-01T00:00:00.000","initial_key":"%s"}\n' "$DEVPUB" > $T/genesis.json
nodeos -e -p eosio --plugin eosio::chain_api_plugin --plugin eosio::producer_api_plugin --plugin eosio::http_plugin \
  --http-server-address 127.0.0.1:$PORT --data-dir $T/data --config-dir $T/cfg --genesis-json $T/genesis.json --signature-provider $DEVPUB=KEY:$DEVPRIV \
  --contracts-console --access-control-allow-origin '*' >$T/nodeos.log 2>&1 &
NPID=$!
keosd --http-server-address 127.0.0.1:$WPORT --wallet-dir $T/w --data-dir $T/w >$T/keosd.log 2>&1 &
KPID=$!
cleanup(){ kill $NPID $KPID 2>/dev/null; wait 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT
C="cleos -u http://127.0.0.1:$PORT --wallet-url http://127.0.0.1:$WPORT"
for i in $(seq 1 30); do $C get info >/dev/null 2>&1 && break; sleep 0.5; done
$C wallet create -n t --file $T/pw >/dev/null; $C wallet import -n t --private-key $DEVPRIV >/dev/null
# activate PREACTIVATE_FEATURE so later features (needed by CDT 4 wasm) can be enabled
curl -s -X POST http://127.0.0.1:$PORT/v1/producer/schedule_protocol_feature_activations \
  -d '{"protocol_features_to_activate":["0ec7e080177b2c02b278d5088611686b49d739925a92d9bfcacd7fc6b74053bd"]}' >/dev/null
sleep 1.5
PASS=0; FAIL=0
ok(){ if "$@" >$T/last 2>&1; then PASS=$((PASS+1)); echo "PASS: $DESC"; else FAIL=$((FAIL+1)); echo "FAIL: $DESC"; grep -m2 -E "assertion|Error [0-9]" $T/last; fi; }
no(){ if "$@" >$T/last 2>&1; then FAIL=$((FAIL+1)); echo "FAIL (should reject): $DESC"; else
  if [ -n "${EXPECT:-}" ] && ! grep -q "$EXPECT" $T/last; then FAIL=$((FAIL+1)); echo "FAIL (wrong error): $DESC"; tail -2 $T/last; else PASS=$((PASS+1)); echo "PASS: $DESC (rejected)"; fi; fi; EXPECT=""; }
q(){ $C get table "$@"; }
bal(){ $C get currency balance $1 $2 $3; }

# boot contract enables the remaining protocol features
if [ -n "${BOOT_WASM_DIR:-}" ]; then $C set contract eosio "$BOOT_WASM_DIR" -p eosio >/dev/null; fi
for f in $( curl -s -X POST http://127.0.0.1:$PORT/v1/producer/get_supported_protocol_features | python3 -c "import json,sys;[print(x['feature_digest']) for x in json.load(sys.stdin) if x['specification'][0]['value']!='PREACTIVATE_FEATURE']" ); do
  $C push action -f eosio activate "[\"$f\"]" -p eosio >/dev/null 2>&1 || true
done
sleep 1

for a in sat.token leeftrader swap.mock wax.mock usdt.mock leef.mock adminmsig botacct teamfees opswallet alice bob; do
  $C create account eosio $a $DEVPUB $DEVPUB >/dev/null
done
$C create account eosio danishcrypto $DEVPUB $DEVPUB >/dev/null  # only to prove the deny-list rejects it
for t in sat.token wax.mock usdt.mock leef.mock; do $C set code $t build/sat.token.wasm >/dev/null && $C set abi $t build/sat.token.abi >/dev/null; done
$C set code leeftrader build/leeftrader.wasm >/dev/null && $C set abi leeftrader build/leeftrader.abi >/dev/null
$C set code swap.mock build/mockswap.wasm >/dev/null && $C set abi swap.mock build/mockswap.abi >/dev/null
for a in leeftrader swap.mock; do $C set account permission $a active --add-code >/dev/null; done

# tokens
$C push action -f sat.token create '["sat.token","100000000.0000 SAT"]' -p sat.token >/dev/null
$C push action -f wax.mock create '["wax.mock","1000000000.00000000 WAX"]' -p wax.mock >/dev/null
$C push action -f usdt.mock create '["usdt.mock","1000000000.0000 USDT"]' -p usdt.mock >/dev/null
$C push action -f leef.mock create '["leef.mock","420000000000.0000 LEEF"]' -p leef.mock >/dev/null
$C push action -f sat.token issue '["sat.token","100000000.0000 SAT","all"]' -p sat.token >/dev/null
$C push action -f wax.mock issue '["wax.mock","1000000.00000000 WAX",""]' -p wax.mock >/dev/null
$C push action -f usdt.mock issue '["usdt.mock","100000.0000 USDT",""]' -p usdt.mock >/dev/null
$C push action -f leef.mock issue '["leef.mock","1000000000.0000 LEEF",""]' -p leef.mock >/dev/null
$C push action -f wax.mock transfer '["wax.mock","alice","10000.00000000 WAX",""]' -p wax.mock >/dev/null
$C push action -f usdt.mock transfer '["usdt.mock","alice","1000.0000 USDT",""]' -p usdt.mock >/dev/null
$C push action -f sat.token transfer '["sat.token","swap.mock","1000000.0000 SAT","fund"]' -p sat.token >/dev/null
$C push action -f leef.mock transfer '["leef.mock","swap.mock","10000000.0000 LEEF","fund"]' -p leef.mock >/dev/null
$C push action -f wax.mock transfer '["wax.mock","swap.mock","1000.00000000 WAX","fund"]' -p wax.mock >/dev/null

echo "== sat.token"
DESC="SAT supply is 100,000,000.0000 with issuer sat.token"; ok bash -c "$C get currency stats sat.token SAT | grep -q '100000000.0000 SAT'"
DESC="cannot issue beyond max supply"; EXPECT="exceeds available supply"; no $C push action -f sat.token issue '["sat.token","1.0000 SAT",""]' -p sat.token
DESC="transfer without owner auth rejected"; no $C push action -f sat.token transfer '["swap.mock","bob","1.0000 SAT",""]' -p bob

echo "== leeftrader config"
DESC="trading account rejected as bot (deny-list)"; EXPECT="trading accounts may not hold"; no $C push action -f leeftrader init '["adminmsig","danishcrypto","teamfees","swap.mock"]' -p leeftrader
DESC="init with fresh placeholder accounts"; ok $C push action -f leeftrader init '["adminmsig","botacct","teamfees","swap.mock"]' -p leeftrader
DESC="split must total 10000 bps"; EXPECT="10000"; no $C push action -f leeftrader setsplit '[["satlp","leeflp"],[5000,4000]]' -p adminmsig
DESC="bot cannot change split"; no $C push action -f leeftrader setsplit '[["satlp","leeflp","ops","rewards","reserve"],[3000,2500,2500,1000,1000]]' -p botacct
DESC="admin sets 30/25/25/10/10 split"; ok $C push action -f leeftrader setsplit '[["satlp","leeflp","ops","rewards","reserve"],[3000,2500,2500,1000,1000]]' -p adminmsig
DESC="whitelist WAX, USDT, SAT, LEEF"; ok $C push action -f leeftrader settoken '[{"sym":"8,WAX","contract":"wax.mock"},true]' -p adminmsig
ok $C push action -f leeftrader settoken '[{"sym":"4,USDT","contract":"usdt.mock"},true]' -p adminmsig
ok $C push action -f leeftrader settoken '[{"sym":"4,SAT","contract":"sat.token"},true]' -p adminmsig
ok $C push action -f leeftrader settoken '[{"sym":"4,LEEF","contract":"leef.mock"},true]' -p adminmsig
DESC="admin sets plan 1: 5 USDT / 30 days"; ok $C push action -f leeftrader setplan '[1,{"quantity":"5.0000 USDT","contract":"usdt.mock"},30,true]' -p adminmsig
DESC="raise caps for test (100% per action/day, 42y lock)"; ok $C push action -f leeftrader setlimits '[10000,10000,1325419200]' -p adminmsig
DESC="lock shorter than 1 year rejected"; EXPECT="lock too short"; no $C push action -f leeftrader setlimits '[10000,10000,86400]' -p adminmsig
DESC="lock past 2106 (uint32 overflow) rejected"; EXPECT="uint32 max"; no $C push action -f leeftrader setlimits '[10000,10000,4000000000]' -p adminmsig
DESC="admin whitelists Alcor pools 1 and 7"; ok $C push action -f leeftrader setpool '[1,true]' -p adminmsig
ok $C push action -f leeftrader setpool '[7,true]' -p adminmsig
DESC="non-admin cannot whitelist pools"; no $C push action -f leeftrader setpool '[99,true]' -p botacct
DESC="bot cannot become fee recipient"; EXPECT="differ from bot"; no $C push action -f leeftrader setfeercpt '["botacct"]' -p adminmsig

echo "== revenue"
DESC="subscription payment records sub and splits revenue"; ok $C push action -f usdt.mock transfer '["alice","leeftrader","5.0000 USDT","sub:1"]' -p alice
DESC="subs row for alice exists"; ok bash -c "$C get table leeftrader leeftrader subs | grep -q alice"
DESC="satlp bucket got 30% (1.5000 USDT)"; ok bash -c "$C get table leeftrader satlp buckets | grep -q '1.5000 USDT'"
DESC="reserve bucket got 10% (0.5000 USDT)"; ok bash -c "$C get table leeftrader reserve buckets | grep -q '0.5000 USDT'"
DESC="underpaid subscription rejected"; EXPECT="underpaid"; no $C push action -f usdt.mock transfer '["alice","leeftrader","1.0000 USDT","sub:1"]' -p alice
DESC="delist LEEF"; ok $C push action -f leeftrader settoken '[{"sym":"4,LEEF","contract":"leef.mock"},false]' -p adminmsig
DESC="non-whitelisted token rejected"; EXPECT="token not accepted"; no $C push action -f leef.mock transfer '["leef.mock","leeftrader","1.0000 LEEF",""]' -p leef.mock
DESC="WAX deposit splits into buckets"; ok $C push action -f wax.mock transfer '["alice","leeftrader","1000.00000000 WAX","fund"]' -p alice
sleep 0.6; DESC="relist LEEF"; ok $C push action -f leeftrader settoken '[{"sym":"4,LEEF","contract":"leef.mock"},true]' -p adminmsig

echo "== buyback guards"
$C push action -f swap.mock setout '[{"quantity":"5000.0000 SAT","contract":"sat.token"},{"quantity":"1.0000 SAT","contract":"sat.token"}]' -p swap.mock >/dev/null
DESC="buyback by non-bot rejected"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"4000.0000 SAT","contract":"sat.token"}]' -p alice
DESC="buyback via non-whitelisted swap rejected"; EXPECT="not whitelisted"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"bob","1",{"quantity":"4000.0000 SAT","contract":"sat.token"}]' -p botacct
DESC="buyback with injected memo path rejected"; EXPECT="digits and commas"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1#bob",{"quantity":"4000.0000 SAT","contract":"sat.token"}]' -p botacct
DESC="buyback above bucket balance rejected"; EXPECT="too low"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"999.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"1.0000 SAT","contract":"sat.token"}]' -p botacct
DESC="buyback reverts when output below min_out"; EXPECT="below min_out"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"6000.0000 SAT","contract":"sat.token"}]' -p botacct
DESC="same-token loop needs min_out >= in + 1 unit"; EXPECT="in + 1"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"100.00000000 WAX","contract":"wax.mock"}]' -p botacct
# H-02: checkout records the token balance BEFORE sending `in`, so a same-token
# swap that returns in+1 is measured as net +1 and fails min_out. Restore SAT
# setout afterwards so later cases keep the original mock output.
$C push action -f swap.mock setout '[{"quantity":"101.00000000 WAX","contract":"wax.mock"},{"quantity":"1.0000 SAT","contract":"sat.token"}]' -p swap.mock >/dev/null
DESC="same-token min_out=in+1 still reverts (checkout net-delta vs gross out)"; EXPECT="below min_out"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"101.00000000 WAX","contract":"wax.mock"}]' -p botacct
$C push action -f swap.mock setout '[{"quantity":"5000.0000 SAT","contract":"sat.token"},{"quantity":"1.0000 SAT","contract":"sat.token"}]' -p swap.mock >/dev/null
DESC="buyback through non-whitelisted pool rejected"; EXPECT="pool not whitelisted"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1,99",{"quantity":"4000.0000 SAT","contract":"sat.token"}]' -p botacct
DESC="min_out on a fake contract with the same symbol rejected"; EXPECT="output token not whitelisted"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"4000.0000 SAT","contract":"leef.mock"}]' -p botacct
DESC="valid buyback: 100 WAX -> 5000 SAT credited to satlp"; ok $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"4000.0000 SAT","contract":"sat.token"}]' -p botacct
DESC="satlp now holds 5000.0000 SAT"; ok bash -c "$C get table leeftrader satlp buckets | grep -q '5000.0000 SAT'"

echo "== LP + native lock"
DESC="addlp into non-whitelisted pool rejected"; EXPECT="pool not whitelisted"; no $C push action -f leeftrader addlp '["satlp",99,{"quantity":"50.00000000 WAX","contract":"wax.mock"},{"quantity":"2500.0000 SAT","contract":"sat.token"},-1000,1000,"1.00000000 WAX","1.0000 SAT"]' -p botacct
DESC="addlp with zero min amounts rejected"; EXPECT="min amounts"; no $C push action -f leeftrader addlp '["satlp",7,{"quantity":"50.00000000 WAX","contract":"wax.mock"},{"quantity":"2500.0000 SAT","contract":"sat.token"},-1000,1000,"0.00000000 WAX","0.0000 SAT"]' -p botacct
DESC="addlp mints position owned by leeftrader and locks it ~42y"; ok $C push action -f leeftrader addlp '["satlp",7,{"quantity":"50.00000000 WAX","contract":"wax.mock"},{"quantity":"2500.0000 SAT","contract":"sat.token"},-1000,1000,"1.00000000 WAX","1.0000 SAT"]' -p botacct
DESC="mock position unlock is >= 41 years ahead"; ok bash -c "u=\$($C get table swap.mock swap.mock positions | python3 -c 'import json,sys;print(json.load(sys.stdin)[\"rows\"][0][\"unlock\"])'); [ \$((u - \$(date +%s))) -gt 1293000000 ]"
DESC="locked position cannot be removed (subliquid)"; EXPECT="locked"; no $C push action -f swap.mock subliquid '[7,"leeftrader",1,-1000,1000,"0.00000000 WAX","0.0000 SAT",0]' -p leeftrader
DESC="leeftrader ABI has no subliquid / transferpos action"; ok bash -c "! $C get abi leeftrader | grep -qE '\"(subliquid|transferpos|withdrawlp)\"'"
B0=$(bal sat.token teamfees SAT)
DESC="collectfees by bot sends fees to fee recipient only"; ok $C push action -f leeftrader collectfees '["botacct",0,"1000.00000000 WAX","1000.0000 SAT"]' -p botacct
DESC="teamfees received 1.0000 SAT"; ok bash -c "[ \"\$($C get currency balance sat.token teamfees SAT)\" = '1.0000 SAT' ]"
DESC="collectfees by outsider rejected"; no $C push action -f leeftrader collectfees '["alice",0,"1.00000000 WAX","1.0000 SAT"]' -p alice

echo "== payout + pause"
DESC="payout to non-whitelisted account rejected"; EXPECT="not whitelisted"; no $C push action -f leeftrader payout '["ops","bob",{"quantity":"1.00000000 WAX","contract":"wax.mock"},"x"]' -p botacct
DESC="trading account cannot be whitelisted as destination"; EXPECT="trading accounts"; no $C push action -f leeftrader setdest '["danishcrypto",true]' -p adminmsig
DESC="admin whitelists opswallet"; ok $C push action -f leeftrader setdest '["opswallet",true]' -p adminmsig
DESC="payout ops -> opswallet"; ok $C push action -f leeftrader payout '["ops","opswallet",{"quantity":"1.00000000 WAX","contract":"wax.mock"},"ops"]' -p botacct
DESC="bot can pause"; ok $C push action -f leeftrader pause '["botacct",true]' -p botacct
DESC="paused contract rejects buyback"; EXPECT="paused"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"1.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"1.0000 SAT","contract":"sat.token"}]' -p botacct
DESC="bot cannot unpause"; EXPECT="not allowed"; no $C push action -f leeftrader pause '["botacct",false]' -p botacct
DESC="admin unpauses"; ok $C push action -f leeftrader pause '["adminmsig",false]' -p adminmsig
DESC="admin sets 5% per-action / 20% daily caps"; ok $C push action -f leeftrader setlimits '[500,2000,1325419200]' -p adminmsig
DESC="buyback over 5% of bucket rejected"; EXPECT="per-action cap"; no $C push action -f leeftrader buyback '["satlp",{"quantity":"100.00000000 WAX","contract":"wax.mock"},"swap.mock","1",{"quantity":"1.0000 SAT","contract":"sat.token"}]' -p botacct

echo; echo "RESULT: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
