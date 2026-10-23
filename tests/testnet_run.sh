#!/usr/bin/env bash
# WAX TESTNET run of the leeftrader suite (never mainnet). Accounts are fresh throwaway testnet accounts
# with a key that lives only in a local keosd wallet outside the repo (WALLET_URL). Prefix P names them.
# mockswap stands in for swap.alcor because testnet swap.alcor has no lockpos action (checked via get_abi).
# Usage: P=<prefix> WALLET_URL=http://127.0.0.1:18901 tests/testnet_run.sh   -> prints PASS/FAIL + tx ids
set -uo pipefail
cd "$(dirname "$0")/.."
[ -n "${SPRING_BIN:-}" ] && export PATH="$SPRING_BIN:$PATH"
RPC=https://testnet.waxsweden.org
case "$RPC" in *testnet*) ;; *) echo "refusing: not a testnet RPC"; exit 1;; esac
C="cleos -u $RPC --wallet-url $WALLET_URL"; T=$(mktemp -d); trap 'rm -rf $T' EXIT
F=${FUNDER:-${P}fundr1}; TR=${P}trader; SAT=${P}sattkn; SW=${P}mswap1; USDT=${P}usdtmk; LEEF=${P}leefmk
BOT=${P}botact; ADM=${P}admins; FEE=${P}feemsg; AL=${P}alice1; OPS=$F; BOB=${P}sign3c
S1=${P}sign1a; S2=${P}sign2b; S3=${P}sign3c
NEW=(trader sattkn mswap1 usdtmk leefmk botact admins feemsg alice1)
PASS=0; FAIL=0; TXLOG=${TXLOG:-/dev/null}
tx(){ grep -oE "executed transaction: [0-9a-f]+" $T/last | awk '{print $3}'; }
ok(){ if "$@" >$T/last 2>&1; then PASS=$((PASS+1)); echo "PASS: $DESC $(tx)"; echo "$DESC|$(tx)" >>$TXLOG; else FAIL=$((FAIL+1)); echo "FAIL: $DESC"; grep -m2 -E "assertion|Error" $T/last; fi; sleep 0.6; }
no(){ if "$@" >$T/last 2>&1; then FAIL=$((FAIL+1)); echo "FAIL (should reject): $DESC"; else
  if [ -n "${EXPECT:-}" ] && ! grep -q "$EXPECT" $T/last; then FAIL=$((FAIL+1)); echo "FAIL (wrong error): $DESC"; tail -2 $T/last; else PASS=$((PASS+1)); echo "PASS: $DESC (rejected)"; fi; fi; EXPECT=""; }
q(){ $C get table "$@"; }
bal(){ $C get currency balance $1 $2 $3; }
if [ "${SETUP:-1}" = 1 ]; then
 K=${KEY:?public key of the throwaway testnet wallet key}
 for s in ${NEW[@]/#/$P}; do $C system newaccount $F $s $K $K --buy-ram-kbytes 8 --stake-net "1.00000000 WAX" --stake-cpu "5.00000000 WAX" --transfer >/dev/null 2>&1; done
 for s in $S1 $S2 $S3; do $C system newaccount $F $s $K $K --buy-ram-kbytes 4 --stake-net "1.00000000 WAX" --stake-cpu "2.00000000 WAX" --transfer >/dev/null 2>&1; done
 $C system buyram $F $TR "60.00000000 WAX" >/dev/null; $C system buyram $F $SW "20.00000000 WAX" >/dev/null
 for a in $SAT $USDT $LEEF; do $C system buyram $F $a "15.00000000 WAX" >/dev/null; done
 for a in $TR $SW $BOT; do $C system delegatebw $F $a "5.00000000 WAX" "40.00000000 WAX" >/dev/null; done
 for t in $SAT $USDT $LEEF; do $C set contract $t build sat.token.wasm sat.token.abi -p $t >/dev/null; done
 DESC="deploy leeftrader"; ok $C set contract $TR build leeftrader.wasm leeftrader.abi -p $TR
 DESC="deploy mockswap (stand-in for swap.alcor)"; ok $C set contract $SW build mockswap.wasm mockswap.abi -p $SW
 for a in $TR $SW; do $C set account permission $a active --add-code -p $a@owner >/dev/null; sleep 0.5; done
 # 2-of-3 multisig (weight 1 each, signer@active) on admin and fee recipient — same shape as mainnet
 for a in $ADM $FEE; do
  DESC="$a active = 2-of-3 over $S1,$S2,$S3"; ok $C set account permission $a active "{\"threshold\":2,\"keys\":[],\"accounts\":[{\"permission\":{\"actor\":\"$S1\",\"permission\":\"active\"},\"weight\":1},{\"permission\":{\"actor\":\"$S2\",\"permission\":\"active\"},\"weight\":1},{\"permission\":{\"actor\":\"$S3\",\"permission\":\"active\"},\"weight\":1}]}" owner -p $a@owner
 done
 $C push action $SAT create "[\"$SAT\",\"100000000.0000 SAT\"]" -p $SAT >/dev/null
 $C push action $USDT create "[\"$USDT\",\"1000000000.0000 USDT\"]" -p $USDT >/dev/null
 $C push action $LEEF create "[\"$LEEF\",\"420000000000.0000 LEEF\"]" -p $LEEF >/dev/null; sleep 1
 $C push action $SAT issue "[\"$SAT\",\"100000000.0000 SAT\",\"all\"]" -p $SAT >/dev/null
 $C push action $USDT issue "[\"$USDT\",\"100000.0000 USDT\",\"\"]" -p $USDT >/dev/null
 $C push action $LEEF issue "[\"$LEEF\",\"1000000000.0000 LEEF\",\"\"]" -p $LEEF >/dev/null; sleep 1
 $C transfer $F $AL "1100.00000000 WAX" "" >/dev/null
 $C push action $USDT transfer "[\"$USDT\",\"$AL\",\"1000.0000 USDT\",\"\"]" -p $USDT >/dev/null
 $C push action $SAT transfer "[\"$SAT\",\"$SW\",\"1000000.0000 SAT\",\"fund\"]" -p $SAT >/dev/null
 $C transfer $F $SW "200.00000000 WAX" "fund" >/dev/null; sleep 1
fi
# ---- suite (same cases as tests/run_tests.sh, account names mapped; WAX is real testnet eosio.token)
sed -n '/^echo "== sat.token"/,/^echo; echo "RESULT/p' tests/run_tests.sh | sed '$d' \
 | sed -e "s/\bleeftrader\b/$TR/g" -e "s/sat\.token/$SAT/g" -e "s/swap\.mock/$SW/g" -e "s/usdt\.mock/$USDT/g" \
       -e "s/leef\.mock/$LEEF/g" -e "s/wax\.mock/eosio.token/g" -e "s/adminmsig/$ADM/g" -e "s/botacct/$BOT/g" \
       -e "s/teamfees/$FEE/g" -e "s/opswallet/$OPS/g" -e "s/\balice\b/$AL/g" -e "s/\bbob\b/$BOB/g" \
       -e "s/-p $ADM\b/-p $ADM@active/g" > $T/suite.sh
source $T/suite.sh
echo "== multisig"
DESC="one signer alone cannot act as admin"; no $C push action $TR pause "[\"$ADM\",true]" -p $S3@active
# real eosio.msig flow: propose -> 2 approvals -> exec (mainnet admin changes go this way)
PR=pz$(date +%s | tail -c 6 | tr 0-9 a-j)
$C multisig propose $PR "[{\"actor\":\"$S1\",\"permission\":\"active\"},{\"actor\":\"$S2\",\"permission\":\"active\"},{\"actor\":\"$S3\",\"permission\":\"active\"}]" "[{\"actor\":\"$ADM\",\"permission\":\"active\"}]" $TR setlimits '{"max_in_bps":400,"daily_cap_bps":1500,"lock_secs":1325419200}' $S1 72 -p $S1 >$T/last 2>&1; echo "msig propose $PR|$(tx)" >>$TXLOG; sleep 1
DESC="msig: exec with only 1 approval fails"; $C multisig approve $S1 $PR "{\"actor\":\"$S1\",\"permission\":\"active\"}" -p $S1 >$T/last 2>&1; echo "msig approve 1|$(tx)" >>$TXLOG; sleep 1
no $C multisig exec $S1 $PR -p $S1
DESC="msig: 2nd approval"; ok $C multisig approve $S1 $PR "{\"actor\":\"$S2\",\"permission\":\"active\"}" -p $S2
DESC="msig: exec 2-of-3 -> setlimits 4%/15% as admin"; ok $C multisig exec $S1 $PR -p $S1
echo; echo "RESULT: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
