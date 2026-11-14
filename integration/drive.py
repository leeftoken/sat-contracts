#!/usr/bin/env python3
"""Review integration checks. Local chain only; see run.sh.

Each check prints one of:
  PASS      expected safe behaviour
  FINDING   a confirmed risk that this branch documents but does not change
  FIXED     a confirmed risk that this branch fixes (MODE=fixed)
  BUG       the pre-fix behaviour, reproduced (MODE=base)
"""
import json
import os
import subprocess
import sys

URL = os.environ["URL"]
WALLET = os.environ["WALLET"]
CLEOS = os.environ["CLEOS"]
PUB = os.environ["PUB"]
MODE = os.environ.get("MODE", "fixed")
B = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
DAY = 86400
T0 = 1_800_000_000
TOK = "sattoken1111"
STK = "smartastake1"
VST = "smartavest11"
TRD = "smartatrader"
ADM = "smartaadmin1"
FEES = "smartafees11"
BOT = "smartabot111"
GRD = "guardian1"
DROP = "airdrop1"
LOCK = 1325419200
counts = {"PASS": 0, "FINDING": 0, "FIXED": 0, "BUG": 0}


def say(kind, msg):
    counts[kind] += 1
    print(f"{kind}: {msg}", flush=True)


def run(args, ok=True):
    p = subprocess.run([CLEOS, "--no-auto-keosd", "--wallet-url", WALLET, "-u", URL, *args],
                       capture_output=True, text=True)
    blob = (p.stdout or "") + (p.stderr or "")
    if ok and p.returncode != 0:
        sys.exit(f"command failed: {' '.join(args)}\n{blob}")
    return p.returncode, blob


def push(contract, action, data, auth, ok=True):
    return run(["push", "action", "-f", contract, action, json.dumps(data), "-p", auth], ok=ok)


def fails(contract, action, data, auth, needle):
    code, blob = push(contract, action, data, auth, ok=False)
    if code == 0:
        sys.exit(f"expected failure {needle!r}: {action} {data}")
    if needle not in blob:
        sys.exit(f"expected {needle!r} in failure of {action}:\n{blob[-1500:]}")
    return blob


def table(code, scope, name, limit=200):
    _, blob = run(["get", "table", code, str(scope), name, "--limit", str(limit)])
    return json.loads(blob[blob.find("{"):])["rows"]


def units(text):
    num, _ = text.split()
    whole, frac = num.split(".")
    return int(whole) * 10 ** len(frac) + int(frac)


def sat(u):
    return f"{u // 10000}.{u % 10000:04d} SAT"


def bal(acct, contract=TOK, sym="SAT"):
    return sum(units(r["balance"]) for r in table(contract, acct, "accounts") if r["balance"].endswith(" " + sym))


def xfer(frm, to, u, memo, ok=True, contract=TOK, sym=None):
    q = sat(u) if sym is None else f"{u // 10000}.{u % 10000:04d} {sym}"
    return push(contract, "transfer", {"from": frm, "to": to, "quantity": q, "memo": memo}, frm, ok=ok)


def scfg():
    return table(STK, STK, "config")[0]


def stakes():
    return {r["owner"]: r for r in table(STK, STK, "stakes")}


def stream(i):
    return [r for r in table(VST, VST, "streams") if int(r["id"]) == i][0]


def prop(i):
    return [r for r in table(STK, STK, "proposals") if int(r["id"]) == i][0]


T = T0


def warp(t):
    global T
    assert t >= T, (t, T)
    T = t
    push(STK, "settime", {"now_sec": T}, STK)
    push(VST, "setnow", {"t": T}, VST)


def set_code_perm(acct):
    auth = {"threshold": 1, "keys": [{"key": PUB, "weight": 1}],
            "accounts": [{"permission": {"actor": acct, "permission": "eosio.code"}, "weight": 1}], "waits": []}
    run(["set", "account", "permission", acct, "active", json.dumps(auth), "owner", "-p", acct])


def deploy(acct, sub, name):
    run(["set", "contract", acct, os.path.join(B, sub), f"{name}.wasm", f"{name}.abi", "-p", acct])


def propose(who, kind, **kw):
    d = {"proposer": who, "kind": kind, "bps": kw.get("bps", []), "p0": kw.get("p0", 0), "p1": 0, "rate": 0,
         "amount": 0, "snapshot_time": 0, "memo": "", "merkle_root": "00" * 32, "novote_account": ""}
    push(STK, "propose", d, who)
    return int(scfg()["proposal_seq"])


def main():
    # ---------- setup ----------
    accts = [TOK, STK, VST, TRD, ADM, FEES, BOT, GRD, DROP, "swap.alcor", "eosio.token",
             "alice", "bob", "mallory", "rejector"]
    deploy("eosio", "bios", "bios")
    for a in accts:
        run(["create", "account", "eosio", a, PUB])
        push("eosio", "setalimits", {"account": a, "ram_bytes": 8_000_000, "net_weight": -1, "cpu_weight": -1}, "eosio")
    deploy(TOK, "token", "sat.token")
    deploy("eosio.token", "token", "sat.token")
    deploy(TRD, "trader", "leeftrader")
    deploy(VST, "vest", "smartavest")
    deploy(STK, "stake", "smartastake")
    deploy("rejector", "rejector", "rejector")
    for a in (TRD, VST, STK):
        set_code_perm(a)

    push(TOK, "create", {"issuer": ADM, "maximum_supply": "100000000.0000 SAT"}, TOK)
    push(TOK, "issue", {"to": ADM, "quantity": "2000000.0000 SAT", "memo": "local"}, ADM)
    for a, u in (("alice", 200_000_0000), ("bob", 100_000_0000), ("mallory", 20_000_0000),
                 (FEES, 100_0000), (DROP, 50_000_0000)):
        xfer(ADM, a, u, "seed")
    push("eosio.token", "create", {"issuer": "eosio.token", "maximum_supply": "1000000.0000 WAX"}, "eosio.token")
    push("eosio.token", "issue", {"to": "eosio.token", "quantity": "100.0000 WAX", "memo": ""}, "eosio.token")

    push(TRD, "init", {"admin": ADM, "bot": BOT, "fee_recipient": FEES, "swap_contract": "swap.alcor"}, TRD)
    push(TRD, "setlimits", {"max_in_bps": 100, "daily_cap_bps": 500, "lock_secs": LOCK}, ADM)
    live_split = {"satlp": 3000, "leeflp": 2000, "ops": 3000, "rewards": 1000, "reserve": 1000}
    push(TRD, "setsplit", {"buckets": list(live_split), "bps": list(live_split.values())}, ADM)
    push(TRD, "settoken", {"token": {"sym": "4,SAT", "contract": TOK}, "allowed": True}, ADM)

    push(VST, "init", {"admin": ADM, "token_contract": TOK}, VST)
    push(STK, "init", {
        "admin": GRD, "guardian": GRD, "token_contract": TOK, "token_symbol": "4,SAT", "trader": TRD,
        "airdrop_account": DROP, "airdrop_alloc_bps": 300, "novote": [DROP],
        "cooldown_secs": 3 * DAY, "min_cooldown": DAY, "max_cooldown": 30 * DAY,
        "proposal_threshold": 50_0000, "quorum_bps": 1000, "approval_bps": 5000,
        "voting_period_secs": 3 * DAY, "timelock_secs": DAY,
        "sat_lp_fee_share_bps": 2500, "buyback_share_bps": 0}, STK)
    warp(T0)
    say("PASS", "local chain: real sat.token as sattoken1111, real leeftrader as smartatrader (live limits + split), "
        "smartavest11 + smartastake1 test-clock builds")

    # ---------- vesting ----------
    team_cliff, team_lin = 365 * DAY, 1095 * DAY
    xfer(ADM, VST, 1000_0000, f"open:alice:team:{team_cliff}:{team_lin}:{T0}")
    fails(VST, "claim", {"id": 0}, "mallory", "still in cliff")
    warp(T0 + team_cliff + team_lin // 2)
    a0 = bal("alice")
    push(VST, "claim", {"id": 0}, "mallory")
    if bal("alice") - a0 != 500_0000:
        sys.exit(f"half-way claim paid {bal('alice') - a0}")
    say("PASS", "vest: team stream on real sat.token pays 50% at mid-schedule to the beneficiary, called by a stranger")

    xfer(ADM, VST, 1000_0000, "fund:0")
    a0 = bal("alice")
    push(VST, "claim", {"id": 0}, "mallory")
    got = bal("alice") - a0
    if got != 500_0000:
        sys.exit(f"top-up claim {got}")
    say("FINDING", "vest V-3: a mid-stream fund:<id> top-up is instantly vested pro-rata "
        "(1000 SAT top-up at 50% elapsed -> 500 SAT claimable at once)")

    xfer(ADM, VST, 10_0000, f"open:rejector:team:0:100:{T0}")
    rid = len(table(VST, VST, "streams")) - 1
    fails(VST, "claim", {"id": rid}, "mallory", "rejector refuses SAT")
    say("FINDING", "vest V-2: a beneficiary contract that rejects SAT bricks its stream for good "
        "(no withdraw/re-point by design); 10 SAT stuck in the local test")

    code, blob = xfer("eosio.token", VST, 1_0000, "ram", ok=False, contract="eosio.token", sym="WAX")
    if code == 0 or "wrong token contract" not in blob:
        sys.exit(blob)
    say("PASS", "vest: WAX (eosio.token) and any other token sent to the vault are rejected")

    # rewards stream -> smartastake1, as the staking brief recommends
    xfer(ADM, VST, 10_000_0000, f"open:{STK}:rewards:0:{1461 * DAY}:{T}")
    vid = len(table(VST, VST, "streams")) - 1
    warp(T + 10 * DAY)
    fails(VST, "claim", {"id": vid}, "mallory", "memo")
    say("FINDING", "integration I-1: vest claim memo 'vesting claim' to smartastake1 reverts until "
        "smartavest11 is setfunder'ed; if the stake admin is retired first, the 25M rewards stream is stuck")
    push(STK, "setfunder", {"admin": GRD, "account": VST, "add": True}, GRD)
    fails(VST, "claim", {"id": vid}, "mallory", "fund not staked")
    say("FIXED", "stake S-1: vest/reward transfers revert until the airdrop fund is staked, so a "
        "0.0001 SAT sniper cannot capture parked rewards")
    xfer(DROP, STK, 30_000_0000, "stake")
    pot0 = int(scfg()["reward_pot"])
    st0 = int(scfg()["total_staked"])
    push(VST, "claim", {"id": vid}, "mallory")
    c1 = scfg()
    if int(c1["total_staked"]) <= st0:
        sys.exit(c1)
    say("PASS", "integration: after setfunder and B2 fund stake, vest claim compounds into the fund")

    # ---------- staking: sniper after the fund is in ----------
    m0 = bal("mallory")
    xfer("mallory", STK, 1, "stake")
    fails(STK, "claim", {"owner": "mallory"}, "mallory", "nothing to claim")
    if bal("mallory") != m0 - 1:
        sys.exit(f"mallory liquid {bal('mallory')} after 1-unit stake")
    say("PASS", "tiny staker after the fund is in cannot claim the vest lump")

    # ---------- JIT staking ----------
    xfer("bob", STK, 1000_0000, "stake")
    xfer("mallory", STK, 9000_0000, "stake")
    xfer(ADM, STK, 100_0000, "reward")
    m0 = bal("mallory")
    push(STK, "claim", {"owner": "mallory"}, "mallory")
    jit = bal("mallory") - m0
    push(STK, "unstake", {"owner": "mallory", "amount": int(stakes()["mallory"]["amount"])}, "mallory")
    warp(T + 3 * DAY)
    push(STK, "withdraw", {"owner": "mallory"}, "mallory")
    say("FINDING", f"stake S-2 (MEDIUM): staking just before a reward lump captures it pro-rata "
        f"({sat(jit)} of 100 SAT) and exits after the 3-day cooldown; reward_rate is stored but unused")

    say("PASS", "airdrop fund was staked before rewards (B2); it cannot unstake, so total_staked stays > 0")

    # ---------- trader integration ----------
    xfer(ADM, TRD, 100_000_0000, "revenue")
    rb = [r for r in table(TRD, "rewards", "buckets")]
    if not rb or units(rb[0]["balance"]["quantity"]) != 10_000_0000:
        sys.exit(f"rewards bucket {rb}")
    push(TRD, "setdest", {"account": STK, "allowed": True}, ADM)
    c0 = scfg()
    pot0 = int(c0["reward_pot"]) + int(c0["total_staked"])
    push(TRD, "payout", {"bucket": "rewards", "to": STK,
                         "quantity": {"quantity": "10.0000 SAT", "contract": TOK}, "memo": ""}, BOT)
    st = stakes()
    if MODE == "base":
        if TRD not in st or int(st[TRD]["amount"]) != 10_0000:
            sys.exit(f"expected trader stake row: {st.get(TRD)}")
        say("BUG", "stake S-3: trader payout with empty memo makes smartatrader a staker with voting power; "
            "leeftrader has no action to unstake/claim, so that SAT is locked forever")
    else:
        c1 = scfg()
        grew = int(c1["reward_pot"]) + int(c1["total_staked"]) - pot0
        if TRD in st or grew != 10_0000:
            sys.exit(f"trader payout not treated as reward: {st.get(TRD)} grew {grew}")
        say("FIXED", "stake S-3: any SAT from cfg.trader is reward funding, never a stake row")
    push(TRD, "payout", {"bucket": "rewards", "to": STK,
                         "quantity": {"quantity": "10.0000 SAT", "contract": TOK}, "memo": "reward"}, BOT)
    say("PASS", "integration: smartatrader payout(rewards -> smartastake1, memo reward) funds the pot "
        "once smartaadmin1 setdest's smartastake1")

    code, blob = xfer(FEES, STK, 5_0000, "alcor lp fees", ok=False)
    if code == 0 or "memo" not in blob:
        sys.exit(blob)
    push(STK, "setfunder", {"admin": GRD, "account": FEES, "add": True}, GRD)
    xfer(FEES, STK, 5_0000, "alcor lp fees")
    say("PASS", "integration: smartafees11 forwards with any memo only after setfunder; before that it reverts")

    # ---------- governance vs the live trader ----------
    xfer("alice", STK, 100_000_0000, "stake")
    new_split = [3000, 2000, 2000, 2000, 1000]
    p1 = propose("alice", "setsplit", bps=new_split)
    push(STK, "vote", {"voter": "alice", "id": p1, "support": True}, "alice")
    warp(T + 3 * DAY)
    push(STK, "queue", {"id": p1}, "bob")
    warp(T + DAY)
    fails(STK, "execute", {"id": p1}, "bob", f"missing authority of {ADM}")
    if int(prop(p1)["status"]) != 3:
        sys.exit(prop(p1))
    say("FINDING", "integration I-2: against today's smartatrader a passed setsplit cannot execute "
        "(missing authority of smartaadmin1); TRADER_PATCH is required first")

    # trader upgrade path: the patch exactly as TRADER_PATCH.md described it
    deploy(TRD, "naive", "leeftrader")
    blob = fails(TRD, "setlimits", {"max_in_bps": 100, "daily_cap_bps": 500, "lock_secs": LOCK}, ADM, "datastream")
    fails(TRD, "pause", {"actor": ADM, "paused": True}, ADM, "datastream")
    say("FIXED" if MODE == "fixed" else "BUG", "trader T-1 (HIGH, doc): TRADER_PATCH's plain `name gov;` appended to "
        "the singleton config breaks every action on the upgraded live contract (datastream read past end); "
        "doc now prescribes binary_extension")
    deploy(TRD, "safe", "leeftrader")
    push(TRD, "setlimits", {"max_in_bps": 100, "daily_cap_bps": 500, "lock_secs": LOCK}, ADM)
    push(TRD, "setgov", {"gov": STK}, ADM)
    push(STK, "execute", {"id": p1}, "bob")
    got = {r["bucket"]: int(r["bps"]) for r in table(TRD, TRD, "splits")}
    want = dict(zip(["satlp", "leeflp", "ops", "rewards", "reserve"], new_split))
    if got != want:
        sys.exit(f"split {got}")
    cfgt = table(TRD, TRD, "config")[0]
    if int(cfgt["lock_secs"]) != LOCK:
        sys.exit(cfgt)
    say("PASS", "trader upgraded with binary_extension<name> gov keeps the old row, setgov works, the queued "
        "setsplit then executes inside its grace window; lock_secs untouched")

    # ---------- stale queued proposal ----------
    p2 = propose("alice", "setcooldown", p0=5 * DAY)
    p3 = propose("alice", "setcooldown", p0=2 * DAY)
    for pid in (p2, p3):
        push(STK, "vote", {"voter": "alice", "id": pid, "support": True}, "alice")
    warp(T + 3 * DAY)
    for pid in (p2, p3):
        push(STK, "queue", {"id": pid}, "bob")
    warp(T + DAY)
    push(STK, "execute", {"id": p3}, "bob")
    warp(T + 20 * DAY)
    push(STK, "execute", {"id": p2}, "bob")
    cd = int(scfg()["cooldown_secs"])
    if MODE == "base":
        if cd != 5 * DAY:
            sys.exit(f"cooldown {cd}")
        say("BUG", "stake S-4: a queued proposal never expires; 20 days later the older setcooldown(5d) "
            "overrode the newer executed setcooldown(2d) (same for setsplit/setlimits and a stuck airdrop slot)")
    else:
        if cd != 2 * DAY or int(prop(p2)["status"]) != 6:
            sys.exit(f"cooldown {cd} status {prop(p2)['status']}")
        say("FIXED", "stake S-4: queued proposals expire 14 days after eta (status 6); the stale setcooldown "
            "did not override the newer one")

    p4 = propose("alice", "setcooldown", p0=4 * DAY)
    push(STK, "vote", {"voter": "alice", "id": p4, "support": True}, "alice")
    warp(T + 3 * DAY)
    warp(T + 20 * DAY)
    push(STK, "queue", {"id": p4}, "bob")
    if MODE == "base":
        if int(prop(p4)["status"]) != 3:
            sys.exit(prop(p4))
        say("BUG", "stake S-5: an unqueued passed vote can be queued years later with a fresh timelock")
    else:
        if int(prop(p4)["status"]) != 6:
            sys.exit(prop(p4))
        say("FIXED", "stake S-5: unqueued proposals expire 14 days after end+timelock; delayed queue cannot revive them")

    # ---------- books ----------
    c = scfg()
    need = int(c["total_staked"]) + int(c["total_cooling"]) + int(c["reward_pot"]) + int(c["airdrop_liability"])
    have = bal(STK)
    if have < need:
        sys.exit(f"stake invariant {have} < {need}")
    vest_left = sum(int(r["total"]) - int(r["claimed"]) for r in table(VST, VST, "streams"))
    if bal(VST) != vest_left:
        sys.exit(f"vest balance {bal(VST)} != unclaimed {vest_left}")
    say("PASS", f"books: smartastake1 holds {sat(have)} >= liabilities {sat(need)} (dust {have - need}); "
        f"smartavest11 holds exactly its unclaimed streams {sat(vest_left)}")
    print("SUMMARY " + " ".join(f"{k}={v}" for k, v in counts.items()), flush=True)


if __name__ == "__main__":
    main()
