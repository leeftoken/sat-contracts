#!/usr/bin/env python3
"""Local smartastake tests against Spring nodeos. Not mainnet."""
import hashlib
import json
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
URL = "http://127.0.0.1:8888"
PUB = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
DAY = 86400
T = 1_700_000_000
SCALE = 10**18

passed = 0


def say(msg):
    global passed
    passed += 1
    print(f"PASS {passed}: {msg}", flush=True)


def run(args, ok=True):
    p = subprocess.run(args, capture_output=True, text=True)
    blob = (p.stdout or "") + (p.stderr or "")
    if ok and p.returncode != 0:
        raise SystemExit(f"command failed: {' '.join(args)}\n{blob}")
    return p.returncode, blob


def push(contract, action, data, auth, ok=True):
    return run(
        ["cleos", "-u", URL, "push", "action", "-f", contract, action, json.dumps(data), "-p", auth],
        ok=ok,
    )


def must_fail(contract, action, data, auth, needle):
    code, blob = push(contract, action, data, auth, ok=False)
    if code == 0 or needle not in blob:
        raise SystemExit(f"expected failure containing {needle!r}\ncmd {action} {data} -p {auth}\n{blob}")


def cleos_ok(args):
    run(["cleos", "-u", URL, *args], ok=True)


def table(code, scope, name, limit=100):
    _, blob = run(
        ["cleos", "-u", URL, "get", "table", str(code), str(scope), name, "--limit", str(limit)],
        ok=True,
    )
    start = blob.find("{")
    if start < 0:
        raise SystemExit(f"no json for table {code} {scope} {name}\n{blob}")
    return json.loads(blob[start:])["rows"]


def cfg():
    rows = table("smartastake", "smartastake", "config")
    if len(rows) != 1:
        raise SystemExit(f"config rows {rows}")
    return rows[0]


def I(v):
    return int(v)


def parse_asset(text):
    num, _sym = text.split()
    whole, frac = num.split(".")
    return int(whole) * (10 ** len(frac)) + int(frac)


def token_bal(account, contract="sat.token", sym="SAT"):
    rows = table(contract, account, "accounts")
    total = 0
    for row in rows:
        if sym and not row["balance"].endswith(" " + sym):
            continue
        total += parse_asset(row["balance"])
    return total


def stakes():
    return {row["owner"]: row for row in table("smartastake", "smartastake", "stakes")}


def books(tag):
    c = cfg()
    bal = token_bal("smartastake")
    rows = list(stakes().values())
    staked = sum(I(r["amount"]) for r in rows)
    cooling = sum(I(r["unstake_amount"]) for r in rows)
    pending = sum(I(r["pending_rewards"]) for r in rows)
    novote = {r["account"] for r in table("smartastake", "smartastake", "novote")}
    votable = sum(I(r["amount"]) for r in rows if r["owner"] not in novote)
    need = I(c["total_staked"]) + I(c["total_cooling"]) + I(c["reward_pot"]) + I(c["airdrop_liability"])
    if bal != need:
        raise SystemExit(f"invariant {tag}: bal {bal} need {need} cfg {c}")
    if staked != I(c["total_staked"]) or cooling != I(c["total_cooling"]):
        raise SystemExit(f"stake sum {tag}: {staked}/{cooling} vs {c}")
    if votable != I(c["total_votable"]):
        raise SystemExit(f"votable {tag}: {votable} vs {c['total_votable']}")
    if pending > I(c["reward_pot"]):
        raise SystemExit(f"pending {tag}: {pending} > pot {c['reward_pot']}")
    if I(c["reward_pending_undistributed"]) > I(c["reward_pot"]):
        raise SystemExit(f"undistributed {tag}")
    return c


def warp(ts):
    global T
    if ts < T:
        raise SystemExit(f"time went backwards {ts} < {T}")
    T = ts
    push("smartastake", "settime", {"now_sec": T}, "smartastake")


def sat(units):
    return f"{units // 10000}.{units % 10000:04d} SAT"


def xfer(frm, to, units, memo, contract="sat.token", ok=True):
    return push(
        contract,
        "transfer",
        {"from": frm, "to": to, "quantity": sat(units), "memo": memo},
        frm,
        ok=ok,
    )


def encode_name(s):
    def sym(c):
        if "a" <= c <= "z":
            return ord(c) - ord("a") + 6
        if "1" <= c <= "5":
            return ord(c) - ord("1") + 1
        if c == ".":
            return 0
        raise ValueError(c)

    v = 0
    for i, c in enumerate(s):
        v |= (sym(c) & 0x1F) << (64 - 5 * (i + 1))
    return v


def leaf(account, amount):
    return hashlib.sha256(struct.pack("<Qq", encode_name(account), amount)).digest()


def pair(a, b):
    return hashlib.sha256(a + b).digest() if a <= b else hashlib.sha256(b + a).digest()


def hx(b):
    return b.hex()


ZERO = "00" * 32


def propose(who, kind, **kw):
    data = {
        "proposer": who,
        "kind": kind,
        "bps": kw.get("bps", []),
        "p0": kw.get("p0", 0),
        "p1": kw.get("p1", 0),
        "rate": kw.get("rate", 0),
        "amount": kw.get("amount", 0),
        "snapshot_time": kw.get("snapshot_time", 0),
        "memo": kw.get("memo", ""),
        "merkle_root": kw.get("merkle_root", ZERO),
        "novote_account": kw.get("novote_account", ""),
    }
    auth = kw.get("auth", who)
    ok = kw.get("ok", True)
    code, blob = push("smartastake", "propose", data, auth, ok=ok)
    if not ok:
        needle = kw["needle"]
        if code == 0 or needle not in blob:
            raise SystemExit(f"expected propose fail {needle!r}\n{blob}")
        return None
    return I(cfg()["proposal_seq"])


def vote(who, pid, support, ok=True, needle=None):
    code, blob = push("smartastake", "vote", {"voter": who, "id": pid, "support": support}, who, ok=ok)
    if not ok and (code == 0 or needle not in blob):
        raise SystemExit(f"expected vote fail {needle!r}\n{blob}")


def queue(pid, ok=True, needle=None):
    code, blob = push("smartastake", "queue", {"id": pid}, "alice", ok=ok)
    if not ok and (code == 0 or needle not in blob):
        raise SystemExit(f"expected queue fail {needle!r}\n{blob}")


def execute(pid, who="bob", ok=True, needle=None):
    code, blob = push("smartastake", "execute", {"id": pid}, who, ok=ok)
    if not ok and (code == 0 or needle not in blob):
        raise SystemExit(f"expected execute fail {needle!r}\n{blob}")


def prop(pid):
    rows = [r for r in table("smartastake", "smartastake", "proposals") if I(r["id"]) == pid]
    if len(rows) != 1:
        raise SystemExit(f"proposal {pid} missing {rows}")
    return rows[0]


def set_active(account, keys, code_actor=None):
    auth = {
        "threshold": 1,
        "keys": [{"key": k, "weight": 1} for k in keys],
        "accounts": [],
        "waits": [],
    }
    if code_actor:
        auth["accounts"].append(
            {"permission": {"actor": code_actor, "permission": "eosio.code"}, "weight": 1}
        )
    cleos_ok(
        ["set", "account", "permission", account, "active", json.dumps(auth), "owner", "-p", account]
    )


def main():
    prod = json.load(open("build/smartastake.abi"))
    test = json.load(open("build/test/smartastake.abi"))
    prod_names = {a["name"] for a in prod["actions"]}
    test_names = {a["name"] for a in test["actions"]}
    if "settime" in prod_names or "withdrawadmin" in prod_names or "rescue" in prod_names:
        raise SystemExit(f"prod abi has a forbidden action: {prod_names}")
    if "settime" not in test_names:
        raise SystemExit("test abi missing settime")
    say("production abi has no test clock and no admin rescue")

    accounts = [
        "sat.token",
        "bad.token",
        "trader",
        "smartastake",
        "alice",
        "bob",
        "carol",
        "dave",
        "airdrop",
        "guardian",
        "funder",
    ]
    cleos_ok(["set", "contract", "eosio", "build", "bios.wasm", "bios.abi", "-p", "eosio"])
    for acct in accounts:
        cleos_ok(["create", "account", "eosio", acct, PUB])
        push(
            "eosio",
            "setalimits",
            {"account": acct, "ram_bytes": 8000000, "net_weight": -1, "cpu_weight": -1},
            "eosio",
        )
    cleos_ok(["set", "contract", "sat.token", "build", "sat.token.wasm", "sat.token.abi", "-p", "sat.token"])
    cleos_ok(["set", "contract", "bad.token", "build", "sat.token.wasm", "sat.token.abi", "-p", "bad.token"])
    cleos_ok(["set", "contract", "trader", "build", "trader.wasm", "trader.abi", "-p", "trader"])
    cleos_ok(
        [
            "set",
            "contract",
            "smartastake",
            "build/test",
            "smartastake.wasm",
            "smartastake.abi",
            "-p",
            "smartastake",
        ]
    )
    set_active("smartastake", [PUB], code_actor="smartastake")
    say("contracts deployed; stake active includes eosio.code")

    push(
        "trader",
        "init",
        {"admin": "guardian", "lock_secs": 1325419200},
        "trader",
    )
    push("trader", "setgov", {"gov": "smartastake"}, "guardian")
    push(
        "smartastake",
        "init",
        {
            "admin": "guardian",
            "guardian": "guardian",
            "token_contract": "sat.token",
            "token_symbol": "4,SAT",
            "trader": "trader",
            "airdrop_account": "airdrop",
            "airdrop_alloc_bps": 300,
            "novote": ["airdrop"],
            "cooldown_secs": 3 * DAY,
            "min_cooldown": DAY,
            "max_cooldown": 30 * DAY,
            "proposal_threshold": 500000,
            "quorum_bps": 5500,
            "approval_bps": 5000,
            "voting_period_secs": 3 * DAY,
            "timelock_secs": DAY,
            "sat_lp_fee_share_bps": 2500,
            "buyback_share_bps": 0,
        },
        "smartastake",
    )
    must_fail(
        "smartastake",
        "init",
        {
            "admin": "guardian",
            "guardian": "guardian",
            "token_contract": "sat.token",
            "token_symbol": "4,SAT",
            "trader": "trader",
            "airdrop_account": "airdrop",
            "airdrop_alloc_bps": 300,
            "novote": ["airdrop"],
            "cooldown_secs": 3 * DAY,
            "min_cooldown": DAY,
            "max_cooldown": 30 * DAY,
            "proposal_threshold": 1,
            "quorum_bps": 5500,
            "approval_bps": 5000,
            "voting_period_secs": 3 * DAY,
            "timelock_secs": DAY,
            "sat_lp_fee_share_bps": 2500,
            "buyback_share_bps": 0,
        },
        "smartastake",
        "already inited",
    )
    warp(T)
    say("init once, test clock set, trader gov is the stake contract")

    push("sat.token", "create", {"issuer": "sat.token", "maximum_supply": "100000000.0000 SAT"}, "sat.token")
    push("sat.token", "issue", {"to": "sat.token", "quantity": "1000000.0000 SAT", "memo": "test"}, "sat.token")
    for acct, units in (
        ("alice", 20_000_0000),
        ("bob", 5_000_0000),
        ("carol", 10_000_0000),
        ("dave", 1_000_0000),
        ("airdrop", 30_000_0000),
        ("funder", 2_000_0000),
    ):
        push(
            "sat.token",
            "transfer",
            {"from": "sat.token", "to": acct, "quantity": sat(units), "memo": "fund"},
            "sat.token",
        )
    push("sat.token", "create", {"issuer": "sat.token", "maximum_supply": "1000000.0000 BAD"}, "sat.token")
    push("sat.token", "issue", {"to": "alice", "quantity": "10.0000 BAD", "memo": "bad"}, "sat.token")
    push("bad.token", "create", {"issuer": "bad.token", "maximum_supply": "1000000.0000 SAT"}, "bad.token")
    push("bad.token", "issue", {"to": "alice", "quantity": "10.0000 SAT", "memo": "bad"}, "bad.token")

    code, blob = xfer("alice", "smartastake", 1_0000, "stake", contract="bad.token", ok=False)
    if code == 0 or "wrong token" not in blob:
        raise SystemExit(blob)
    code, blob = push(
        "sat.token",
        "transfer",
        {"from": "alice", "to": "smartastake", "quantity": "1.0000 BAD", "memo": "stake"},
        "alice",
        ok=False,
    )
    if code == 0 or "wrong symbol" not in blob:
        raise SystemExit(blob)
    say("wrong token contract and wrong symbol rejected")

    push("smartastake", "setfunder", {"admin": "guardian", "account": "funder", "add": True}, "guardian")
    code, blob = xfer("bob", "smartastake", 1_0000, "vest", ok=False)
    if code == 0 or "memo" not in blob:
        raise SystemExit(blob)
    xfer("funder", "smartastake", 1_0000, "vest")
    xfer("funder", "smartastake", 50_0000, "reward")
    c = books("held reward")
    if I(c["total_staked"]) != 0 or I(c["reward_pot"]) != 51_0000:
        raise SystemExit(c)
    if I(c["reward_pending_undistributed"]) != 51_0000:
        raise SystemExit(c)
    say("reward memo and allowlisted funder memo sit undistributed when nobody is staked")

    xfer("alice", "smartastake", 100_0000, "stake")
    c = books("alice staked")
    if I(c["total_staked"]) != 100_0000 or I(stakes()["alice"]["amount"]) != 100_0000:
        raise SystemExit(c)
    push("smartastake", "claim", {"owner": "alice"}, "alice")
    if I(stakes()["alice"]["amount"]) != 100_0000:
        raise SystemExit("claim touched principal")
    if I(stakes()["alice"]["pending_rewards"]) != 0 or I(cfg()["reward_pot"]) != 0:
        raise SystemExit(cfg())
    if token_bal("alice") != 20_000_0000 - 100_0000 + 51_0000:
        raise SystemExit(f"alice liquid {token_bal('alice')}")
    books("after first claim")
    say("first staker receives held rewards; claim does not touch principal")

    push("smartastake", "unstake", {"owner": "alice", "amount": 40_0000}, "alice")
    row = stakes()["alice"]
    if I(row["amount"]) != 60_0000 or I(row["unstake_amount"]) != 40_0000:
        raise SystemExit(row)
    if I(row["unstake_ready"]) != T + 3 * DAY:
        raise SystemExit(row)
    must_fail("smartastake", "withdraw", {"owner": "alice"}, "alice", "not ready")
    must_fail("smartastake", "unstake", {"owner": "alice", "amount": 1}, "bob", "missing authority")
    warp(T + DAY)
    push("smartastake", "unstake", {"owner": "alice", "amount": 10_0000}, "alice")
    row = stakes()["alice"]
    if I(row["amount"]) != 50_0000 or I(row["unstake_amount"]) != 50_0000:
        raise SystemExit(row)
    if I(row["unstake_ready"]) != T + 3 * DAY:
        raise SystemExit(f"cooldown did not reset: {row['unstake_ready']} vs {T + 3 * DAY}")
    must_fail("smartastake", "withdraw", {"owner": "alice"}, "alice", "not ready")
    warp(T + 3 * DAY)
    pot_before = I(cfg()["reward_pot"])
    push("smartastake", "withdraw", {"owner": "alice"}, "alice")
    row = stakes()["alice"]
    if I(row["unstake_amount"]) != 0 or I(row["amount"]) != 50_0000:
        raise SystemExit(row)
    if I(cfg()["reward_pot"]) != pot_before:
        raise SystemExit("withdraw touched reward pot")
    books("after partial withdraw")
    say("unstake cooldown resets for the whole cooling balance; early withdraw fails")

    push("smartastake", "unstake", {"owner": "alice", "amount": 50_0000}, "alice")
    warp(T + 3 * DAY)
    push("smartastake", "withdraw", {"owner": "alice"}, "alice")
    if I(cfg()["total_staked"]) != 0 or I(cfg()["total_cooling"]) != 0:
        raise SystemExit(cfg())
    say("full unstake and withdraw returns principal")

    xfer("alice", "smartastake", 3000_0000, "stake")
    xfer("bob", "smartastake", 1000_0000, "")
    if I(cfg()["total_staked"]) != 4000_0000 or I(cfg()["total_votable"]) != 4000_0000:
        raise SystemExit(cfg())
    say("stake via memo stake and empty memo; totals match")

    xfer("funder", "smartastake", 100_0000, "reward")
    if I(cfg()["reward_pot"]) != 100_0000:
        raise SystemExit(cfg())
    alice_liq = token_bal("alice")
    bob_liq = token_bal("bob")
    alice_amt = I(stakes()["alice"]["amount"])
    bob_amt = I(stakes()["bob"]["amount"])
    push("smartastake", "claim", {"owner": "alice"}, "alice")
    push("smartastake", "claim", {"owner": "bob"}, "bob")
    if I(stakes()["alice"]["amount"]) != alice_amt or I(stakes()["bob"]["amount"]) != bob_amt:
        raise SystemExit("claim changed principal")
    if token_bal("alice") != alice_liq + 75_0000 or token_bal("bob") != bob_liq + 25_0000:
        raise SystemExit(
            f"claim split alice {token_bal('alice') - alice_liq} bob {token_bal('bob') - bob_liq}"
        )
    if I(cfg()["reward_pot"]) != 0:
        raise SystemExit(cfg())
    say("two stakers claim 75/25 of 100 SAT")

    xfer("carol", "smartastake", 4000_0000, "stake")
    if I(stakes()["carol"]["pending_rewards"]) != 0:
        raise SystemExit(stakes()["carol"])
    must_fail("smartastake", "claim", {"owner": "carol"}, "carol", "nothing to claim")
    say("stake after reward does not steal prior rewards")

    xfer("funder", "smartastake", 80_0000, "reward")
    pot = I(cfg()["reward_pot"])
    if pot != 80_0000:
        raise SystemExit(cfg())
    push("smartastake", "unstake", {"owner": "carol", "amount": 3900_0000}, "carol")
    if I(cfg()["reward_pot"]) != pot:
        raise SystemExit("unstake stole reward pot")
    if I(stakes()["carol"]["amount"]) != 100_0000 or I(stakes()["carol"]["pending_rewards"]) != 40_0000:
        raise SystemExit(stakes()["carol"])
    books("unstake kept pot")
    say("unstake does not steal the reward pot")

    alice_liq = token_bal("alice")
    bob_liq = token_bal("bob")
    carol_liq = token_bal("carol")
    push("smartastake", "claim", {"owner": "alice"}, "alice")
    push("smartastake", "claim", {"owner": "bob"}, "bob")
    push("smartastake", "claim", {"owner": "carol"}, "carol")
    if (
        token_bal("alice") != alice_liq + 30_0000
        or token_bal("bob") != bob_liq + 10_0000
        or token_bal("carol") != carol_liq + 40_0000
    ):
        raise SystemExit(
            (
                token_bal("alice") - alice_liq,
                token_bal("bob") - bob_liq,
                token_bal("carol") - carol_liq,
            )
        )
    if I(cfg()["reward_pot"]) != 0:
        raise SystemExit(cfg())
    warp(T + 3 * DAY)
    push("smartastake", "withdraw", {"owner": "carol"}, "carol")
    books("carol withdrawn")
    say("claims pay only rewards; cooling principal withdraws after the timer")

    xfer("dave", "smartastake", 10_0000, "")
    propose("dave", "setcooldown", p0=7 * DAY, ok=False, needle="below threshold")
    say("propose below threshold fails")

    xfer("airdrop", "smartastake", 20000_0000, "stake")
    if I(cfg()["total_votable"]) != 3000_0000 + 1000_0000 + 100_0000 + 10_0000:
        raise SystemExit(f"airdrop stake changed votable {cfg()}")
    if I(stakes()["airdrop"]["amount"]) != 20000_0000:
        raise SystemExit(stakes()["airdrop"])
    must_fail("smartastake", "unstake", {"owner": "airdrop", "amount": 1}, "airdrop", "airdrop fund is proposal-only")
    must_fail("smartastake", "claim", {"owner": "airdrop"}, "airdrop", "airdrop fund is proposal-only")
    must_fail("smartastake", "vote", {"voter": "airdrop", "id": 1, "support": True}, "airdrop", "no voting power")
    propose("airdrop", "setcooldown", p0=7 * DAY, ok=False, needle="no voting power")
    set_active("airdrop", [], code_actor="smartastake")
    must_fail("smartastake", "unstake", {"owner": "airdrop", "amount": 1}, "airdrop", "signatures")
    propose(
        "alice",
        "airdrop",
        amount=5000_0001,
        snapshot_time=T,
        memo="too big",
        merkle_root="11" * 32,
        ok=False,
        needle="airdrop cap",
    )
    say("novote fund is staked, cannot unstake, and a 25%+ airdrop is rejected")

    before_votable = I(cfg()["total_votable"])
    alice_liq = token_bal("alice")
    bob_liq = token_bal("bob")
    carol_liq = token_bal("carol")
    xfer("funder", "smartastake", 241_1000, "reward")
    push("smartastake", "compound", {"account": "airdrop"}, "bob")
    row = stakes()["airdrop"]
    if I(row["pending_rewards"]) != 0 or I(row["amount"]) != 20200_0000:
        raise SystemExit(row)
    if I(cfg()["total_votable"]) != before_votable:
        raise SystemExit("compound granted voting power")
    must_fail("smartastake", "vote", {"voter": "airdrop", "id": 1, "support": True}, "airdrop@owner", "no voting power")
    propose("airdrop", "setcooldown", p0=7 * DAY, auth="airdrop@owner", ok=False, needle="no voting power")
    push("smartastake", "claim", {"owner": "alice"}, "alice")
    push("smartastake", "claim", {"owner": "bob"}, "bob")
    push("smartastake", "claim", {"owner": "carol"}, "carol")
    if (
        token_bal("alice") != alice_liq + 30_0000
        or token_bal("bob") != bob_liq + 10_0000
        or token_bal("carol") != carol_liq + 1_0000
    ):
        raise SystemExit(
            (
                token_bal("alice") - alice_liq,
                token_bal("bob") - bob_liq,
                token_bal("carol") - carol_liq,
            )
        )
    # dave's 0.1000 SAT stays in the pot until he settles; novote fund took its share as stake
    if I(cfg()["reward_pot"]) != 1000 or I(cfg()["total_staked"]) != before_votable + 20200_0000:
        raise SystemExit(cfg())
    books("after novote reward")
    say("airdrop stake earns rewards, auto-compounds, and has zero voting power")

    cd = I(cfg()["cooldown_secs"])
    fail_start = T
    fail_id = propose("bob", "setcooldown", p0=7 * DAY)
    vote("bob", fail_id, True)
    warp(fail_start + 3 * DAY - 1)
    queue(fail_id, ok=False, needle="voting open")
    warp(fail_start + 3 * DAY)
    queue(fail_id)
    if I(prop(fail_id)["status"]) != 2:
        raise SystemExit(prop(fail_id))
    if I(cfg()["cooldown_secs"]) != cd:
        raise SystemExit("defeated proposal changed cooldown")
    execute(fail_id, ok=False, needle="not queued")
    say("quorum miss defeats the proposal; execute of a non-queued id fails")

    good_split = [3000, 2000, 2000, 2000, 1000]
    pid = propose("alice", "setsplit", bps=good_split)
    if I(prop(pid)["snapshot_total"]) != I(cfg()["total_votable"]):
        raise SystemExit("snapshot is not votable stake")
    if I(prop(pid)["end_time"]) - I(prop(pid)["start_time"]) != 3 * DAY:
        raise SystemExit(prop(pid))
    vote("alice", pid, True)
    vote("alice", pid, True, ok=False, needle="already voted")
    must_fail("smartastake", "unstake", {"owner": "alice", "amount": 1}, "alice", "vote locked")
    vote("bob", pid, True)
    propose("alice", "setsplit", bps=[500, 2000, 3000, 2000, 2500], ok=False, needle="satlp min")
    propose("alice", "setlimits", p0=10, p1=500, ok=False, needle="max_in bounds")
    propose("alice", "setlimits", p0=400, p1=300, ok=False, needle="max_in>daily")
    propose("alice", "setcooldown", p0=10, ok=False, needle="cooldown bounds")
    propose("alice", "setbot", ok=False, needle="kind not allowed")
    propose("alice", "setrparams", rate=1000001, p0=DAY, ok=False, needle="rate cap")
    warp(T + 3 * DAY)
    queue(pid)
    queued = prop(pid)
    if I(queued["status"]) != 3 or I(queued["eta"]) != T + DAY:
        raise SystemExit(queued)
    execute(pid, ok=False, needle="timelock")
    warp(T + DAY)
    lock_before = I(table("trader", "trader", "config")[0]["lock_secs"])
    execute(pid)
    if I(prop(pid)["status"]) != 4:
        raise SystemExit(prop(pid))
    splits = {r["bucket"]: I(r["bps"]) for r in table("trader", "trader", "splits")}
    if splits != {"satlp": 3000, "leeflp": 2000, "ops": 2000, "rewards": 2000, "reserve": 1000}:
        raise SystemExit(splits)
    if I(table("trader", "trader", "config")[0]["lock_secs"]) != lock_before:
        raise SystemExit("setsplit touched lock")
    code, blob = push(
        "trader",
        "setsplit",
        {"buckets": ["satlp", "leeflp", "ops", "rewards", "reserve"], "bps": good_split},
        "alice",
        ok=False,
    )
    if code == 0 or "missing auth" not in blob:
        raise SystemExit(blob)
    books("after setsplit")
    say("quorum plus approval queues; timelock gates execute; split lands on the trader")

    lim = propose("alice", "setlimits", p0=200, p1=800)
    vote("alice", lim, True)
    vote("bob", lim, True)
    warp(T + 3 * DAY)
    queue(lim)
    if I(prop(lim)["status"]) != 3:
        raise SystemExit(prop(lim))
    must_fail("smartastake", "veto", {"guardian": "bob", "id": lim}, "bob", "not guardian")
    push("smartastake", "veto", {"guardian": "guardian", "id": lim}, "guardian")
    if I(prop(lim)["status"]) != 5:
        raise SystemExit(prop(lim))
    execute(lim, ok=False, needle="not queued")
    tcfg = table("trader", "trader", "config")[0]
    if I(tcfg["max_in_bps"]) != 100 or I(tcfg["daily_cap_bps"]) != 500:
        raise SystemExit(tcfg)
    say("guardian veto stops execute; trader limits stay unchanged")

    la = leaf("alice", 60_0000)
    lb = leaf("bob", 40_0000)
    root = pair(la, lb)
    drop_start = T
    drop = propose(
        "alice",
        "airdrop",
        amount=100_0000,
        snapshot_time=T,
        memo="holders",
        merkle_root=hx(root),
    )
    meta = prop(drop)
    if I(meta["end_time"]) - I(meta["start_time"]) != 7 * DAY:
        raise SystemExit(meta)
    if I(meta["applied_timelock"]) != 2 * DAY:
        raise SystemExit(meta)
    vote("alice", drop, True)
    vote("bob", drop, True)
    warp(drop_start + 7 * DAY - 1)
    queue(drop, ok=False, needle="voting open")
    warp(drop_start + 7 * DAY)
    queue(drop)
    if I(prop(drop)["status"]) != 3 or I(prop(drop)["eta"]) != drop_start + 7 * DAY + 2 * DAY:
        raise SystemExit(prop(drop))
    execute(drop, ok=False, needle="timelock")
    warp(drop_start + 7 * DAY + 2 * DAY)
    fund_before = I(stakes()["airdrop"]["amount"])
    execute(drop)
    if I(stakes()["airdrop"]["amount"]) != fund_before - 100_0000:
        raise SystemExit(stakes()["airdrop"])
    drow = table("smartastake", "smartastake", "drops")
    drow = [r for r in drow if I(r["id"]) == drop][0]
    if I(drow["amount"]) != 100_0000 or I(cfg()["airdrop_liability"]) != 100_0000:
        raise SystemExit((drow, cfg()))
    books("drop open")
    say("airdrop uses a 7-day vote and 48h timelock, then escrows the fund")

    must_fail(
        "smartastake",
        "claimdrop",
        {"account": "carol", "drop_id": drop, "amount": 60_0000, "proof": [hx(lb)]},
        "carol",
        "bad proof",
    )
    push(
        "smartastake",
        "claimdrop",
        {"account": "alice", "drop_id": drop, "amount": 60_0000, "proof": [hx(lb)]},
        "alice",
    )
    must_fail(
        "smartastake",
        "claimdrop",
        {"account": "alice", "drop_id": drop, "amount": 60_0000, "proof": [hx(lb)]},
        "alice",
        "already claimed",
    )
    push(
        "smartastake",
        "claimdrop",
        {"account": "bob", "drop_id": drop, "amount": 40_0000, "proof": [hx(la)]},
        "bob",
    )
    if I(cfg()["airdrop_liability"]) != 0:
        raise SystemExit(cfg())
    claimed = {r["account"]: I(r["amount"]) for r in table("smartastake", str(drop), "claims")}
    if claimed != {"alice": 60_0000, "bob": 40_0000}:
        raise SystemExit(claimed)
    books("claims done")
    say("merkle claims pay once and reject a bad proof or a second claim")

    one = leaf("dave", 1_0000)
    propose(
        "alice",
        "airdrop",
        amount=1_0000,
        snapshot_time=T,
        memo="too soon",
        merkle_root=hx(one),
        ok=False,
        needle="airdrop cooldown",
    )
    last = I(cfg()["last_airdrop_exec"])
    warp(last + 30 * DAY)
    second = propose(
        "alice",
        "airdrop",
        amount=1_0000,
        snapshot_time=T,
        memo="later",
        merkle_root=hx(one),
    )
    if I(prop(second)["status"]) != 1:
        raise SystemExit(prop(second))
    execute(second, ok=False, needle="not queued")
    books("final")
    say("a second airdrop inside 30 days is rejected; after the gap a new one can be proposed")

    print(f"\n{passed} checks passed")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        print("\n--- nodeos log tail ---", file=sys.stderr)
        try:
            print(open("/tmp/sat-stake-nodeos.log").read()[-4000:], file=sys.stderr)
        except OSError:
            pass
        raise
