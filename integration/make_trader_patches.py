#!/usr/bin/env python3
"""Write two patched copies of ../leeftrader/leeftrader.cpp into build/patch/.

naive: `name gov;` appended to the config struct, as smartastake/docs/TRADER_PATCH.md
       described it before this review.
safe:  `eosio::binary_extension<name> gov;` so the live config row (which has no gov
       bytes) still deserializes after a code upgrade.

The repo file leeftrader/leeftrader.cpp is only read, never changed.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, "leeftrader", "leeftrader.cpp")).read()


def must_replace(s, old, new, count=1):
    if s.count(old) != count:
        sys.exit(f"patch anchor found {s.count(old)} times, wanted {count}: {old!r}")
    return s.replace(old, new)


def patch(s, safe):
    field = "eosio::binary_extension<name> gov;" if safe else "name gov;"
    s = must_replace(
        s,
        "      uint32_t lock_secs = DEFAULT_LOCK_SECS;\n   };",
        f"      uint32_t lock_secs = DEFAULT_LOCK_SECS;\n      {field}\n   }};",
    )
    if safe:
        s = must_replace(s, "#include <eosio/system.hpp>\n", "#include <eosio/system.hpp>\n#include <eosio/binary_extension.hpp>\n")
        gov_ok = "(c.gov.has_value() && c.gov.value().value != 0 && has_auth(c.gov.value()))"
    else:
        gov_ok = "(c.gov.value != 0 && has_auth(c.gov))"
    for sig in (
        "[[eosio::action]] void setlimits(uint16_t max_in_bps, uint16_t daily_cap_bps, uint32_t lock_secs) {\n      auto c = cfg(); require_auth(c.admin);",
        "[[eosio::action]] void setsplit(std::vector<name> buckets, std::vector<uint16_t> bps) {\n      auto c = cfg(); require_auth(c.admin);",
    ):
        s = must_replace(
            s,
            sig,
            sig.replace(
                "require_auth(c.admin);",
                f'check(has_auth(c.admin) || {gov_ok}, "missing authority of admin or gov");',
            ),
        )
    s = must_replace(
        s,
        "   [[eosio::action]] void setbot(name bot) {",
        "   [[eosio::action]] void setgov(name gov) {\n"
        "      auto c = cfg(); require_auth(c.admin);\n"
        '      check(gov.value == 0 || is_account(gov), "gov account missing");\n'
        "      c.gov = gov; cfg_t(get_self(), get_self().value).set(c, get_self());\n"
        "   }\n\n"
        "   [[eosio::action]] void setbot(name bot) {",
    )
    return s


out = os.path.join(ROOT, "integration", "build", "patch")
for kind, safe in (("naive", False), ("safe", True)):
    d = os.path.join(out, kind)
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "leeftrader.cpp"), "w").write(patch(src, safe))
print("patched trader sources written to", out)
