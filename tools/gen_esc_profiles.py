#!/usr/bin/env python3
"""Check the ESC programming profiles and compile them into C.

    python3 tools/gen_esc_profiles.py          # check, then write the C
    python3 tools/gen_esc_profiles.py --check  # and the checked-in C agrees

The JSON files in shared/esc/profiles/ are the profiles of record;
docs/EscProfiles.md describes them.  The C file is generated from them so the
panel carries every profile without a card and the firmware build needs no
Python.  CI (continuous integration) runs --check so the two cannot drift
apart.

The rules here are the rules esc_profile_parse() applies to a file on the
card: a profile this accepts, the panel accepts, and the host suite parses
every file of record and compares the result with the generated table field
by field.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "shared" / "esc" / "profiles"
OUT = REPO / "shared" / "esc" / "esc_profiles_gen.c"

SCHEME = {"count": "ESC_SCHEME_COUNT", "short_long": "ESC_SCHEME_SHORT_LONG",
          "melody_groups": "ESC_SCHEME_MELODY_GROUPS",
          "yes_no": "ESC_SCHEME_YES_NO",
          "stick_position": "ESC_SCHEME_STICK_POSITION",
          "other": "ESC_SCHEME_OTHER"}
ENCODING = {"count": "ESC_ENC_COUNT", "short_long": "ESC_ENC_SHORT_LONG",
            "melody": "ESC_ENC_MELODY", "yes_no": "ESC_ENC_YES_NO"}
ANNOUNCE = {"item": "ESC_ANNOUNCE_ITEM", "value": "ESC_ANNOUNCE_VALUE",
            "item_then_value": "ESC_ANNOUNCE_ITEM_THEN_VALUE"}
AUTO = {"full": "ESC_AUTO_FULL", "assisted": "ESC_AUTO_ASSISTED",
        "none": "ESC_AUTO_NONE"}
THROTTLE = {"min": "ESC_THR_MIN", "mid": "ESC_THR_MID", "max": "ESC_THR_MAX",
            "none": "ESC_THR_NONE"}
WHEN = {"before_power_on": "false", "after_power_on": "true"}


class Bad(Exception):
    pass


def want(cond: bool, where: str, what: str) -> None:
    if not cond:
        raise Bad(f"{where}: {what}")


def text(d: dict, key: str, where: str, empty_ok: bool = False) -> str:
    v = d.get(key)
    want(isinstance(v, str), f"{where}.{key}", "not a string")
    want(empty_ok or v != "", f"{where}.{key}", "empty")
    return v


def num(d: dict, key: str, where: str, hi: int, null_ok: bool = True,
        lo: int = 0) -> int | None:
    v = d.get(key)
    if v is None:
        want(null_ok, f"{where}.{key}", "missing")
        return None
    want(isinstance(v, int) and not isinstance(v, bool),
         f"{where}.{key}", "not a whole number")
    want(lo <= v <= hi, f"{where}.{key}", f"outside {lo}..{hi}")
    return v


def pick(d: dict, key: str, where: str, table: dict) -> str:
    v = d.get(key)
    want(v in table, f"{where}.{key}", f"not one of {', '.join(table)}")
    return table[v]


def check(path: pathlib.Path) -> dict:
    """The profile, reduced to what the firmware carries; raises Bad."""
    d = json.loads(path.read_text(encoding="utf-8"))
    w = path.name
    want(isinstance(d, dict), w, "not an object")
    want(d.get("schema") == 1, f"{w}.schema", "not 1")
    pid = text(d, "id", w)
    want(re.fullmatch(r"[a-z0-9-]{1,48}", pid) is not None, f"{w}.id",
         "not 1-48 of a-z 0-9 -")
    want(pid == path.stem, f"{w}.id", "differs from the file name")
    want(d.get("verified") in (True, False), f"{w}.verified", "not a boolean")
    auto = pick(d, "automatable", w, AUTO)
    note = d.get("automatable_note") or ""
    want(isinstance(note, str), f"{w}.automatable_note", "not a string")
    want(auto == "ESC_AUTO_FULL" or note != "", f"{w}.automatable_note",
         "needed when not full")

    s = d.get("scheme")
    want(isinstance(s, dict), f"{w}.scheme", "not an object")
    e, a = s.get("entry"), s.get("announce")
    sel, skip = s.get("select"), s.get("skip")
    for k, v in (("entry", e), ("announce", a), ("select", sel),
                 ("skip", skip)):
        want(isinstance(v, dict), f"{w}.scheme.{k}", "not an object")
    steps = e.get("steps")
    want(isinstance(steps, list) and 0 < len(steps) <= 255
         and all(isinstance(x, str) and x for x in steps),
         f"{w}.scheme.entry.steps", "not 1-255 strings")
    enc = pick(a, "encoding", f"{w}.scheme.announce", ENCODING)
    les = num(a, "long_equals_short", f"{w}.scheme.announce", 255)
    want(enc != "ESC_ENC_SHORT_LONG" or bool(les),
         f"{w}.scheme.announce.long_equals_short", "needed for short_long")
    cpe = s.get("changes_per_entry")
    want(cpe in ("one", "many"), f"{w}.scheme.changes_per_entry",
         "not one or many")
    rep = num(a, "repeat", f"{w}.scheme.announce", 255)

    p = {
        "id": pid, "brand": text(d, "brand", w),
        "family": text(d, "family", w),
        "scheme": pick(s, "type", f"{w}.scheme", SCHEME),
        "encoding": enc,
        "announce": pick(a, "what", f"{w}.scheme.announce", ANNOUNCE),
        "auto": auto, "note": note,
        "entry_thr": pick(e, "throttle", f"{w}.scheme.entry",
                          {k: v for k, v in THROTTLE.items() if k != "none"}),
        "entry_after": pick(e, "when", f"{w}.scheme.entry", WHEN),
        "hold": num(e, "hold_ms", f"{w}.scheme.entry", 600000) or 0,
        "select_thr": pick(sel, "throttle", f"{w}.scheme.select", THROTTLE),
        "skip_thr": pick(skip, "throttle", f"{w}.scheme.skip", THROTTLE),
        "les": les or 0,
        "beep": num(a, "beep_ms", f"{w}.scheme.announce", 60000) or 0,
        "gap": num(a, "gap_ms", f"{w}.scheme.announce", 60000) or 0,
        "ggap": num(a, "group_gap_ms", f"{w}.scheme.announce", 60000) or 0,
        "repeat": -1 if rep is None else rep,
        "one": "true" if cpe == "one" else "false",
        "verified": "true" if d["verified"] else "false",
        "steps": steps, "models": [], "items": [],
    }

    models = d.get("models")
    want(isinstance(models, list) and 0 < len(models) <= 1000,
         f"{w}.models", "not 1-1000 entries")
    names = set()
    for i, m in enumerate(models):
        mw = f"{w}.models[{i}]"
        want(isinstance(m, dict), mw, "not an object")
        name = text(m, "name", mw)
        want(name not in names, f"{mw}.name", "duplicate")
        names.add(name)
        ct = m.get("cell_type") or "lipo"
        want(ct in ("lipo", "nimh"), f"{mw}.cell_type", "not lipo or nimh")
        p["models"].append({
            "name": name,
            "cmin": num(m, "cells_min", mw, 255) or 0,
            "cmax": num(m, "cells_max", mw, 255) or 0,
            "nimh": "true" if ct == "nimh" else "false",
            "v": num(m, "v_max_mv", mw, 1000000) or 0,
            "a": num(m, "current_a", mw, 65535) or 0,
        })

    items = d.get("items")
    want(isinstance(items, list) and len(items) <= 255, f"{w}.items",
         "not a list of at most 255")
    want(auto == "ESC_AUTO_NONE" or len(items) > 0, f"{w}.items",
         "empty for a profile the bench may run")
    seen: dict[int, list[bool]] = {}
    for i, it in enumerate(items):
        iw = f"{w}.items[{i}]"
        want(isinstance(it, dict), iw, "not an object")
        number = num(it, "number", iw, 255, null_ok=False, lo=1)
        key = text(it, "key", iw)
        want(re.fullmatch(r"[a-z0-9_]{1,32}", key) is not None, f"{iw}.key",
             "not 1-32 of a-z 0-9 _")
        applies = it.get("applies_to") or []
        want(isinstance(applies, list) and len(applies) <= 255
             and all(x in names for x in applies), f"{iw}.applies_to",
             "not a list of this profile's model names")
        when = it.get("applies_when") or ""
        want(isinstance(when, str), f"{iw}.applies_when", "not a string")
        seen.setdefault(number, []).append(bool(applies) or when != "")
        vals = it.get("values")
        want(isinstance(vals, list) and 0 < len(vals) <= 255,
             f"{iw}.values", "not 1-255 entries")
        vnums, out = set(), []
        defaults = 0
        for j, v in enumerate(vals):
            vw = f"{iw}.values[{j}]"
            want(isinstance(v, dict), vw, "not an object")
            vn = num(v, "number", vw, 255, null_ok=False)
            want(vn not in vnums, f"{vw}.number", "duplicate")
            vnums.add(vn)
            dflt = v.get("default", False)
            want(dflt in (True, False), f"{vw}.default", "not a boolean")
            defaults += dflt
            out.append({"name": text(v, "name", vw), "n": vn,
                        "d": "true" if dflt else "false"})
        want(defaults <= 1, f"{iw}.values", "more than one default")
        p["items"].append({"name": text(it, "name", iw), "key": key,
                           "n": number, "values": out, "applies": applies,
                           "when": when})
    for number, conditional in seen.items():
        want(len(conditional) == 1 or all(conditional), f"{w}.items",
             f"number {number} twice without applies_to or applies_when")
    return p


def c_str(s: str) -> str:
    """A C string literal; anything outside printable ASCII as octal."""
    out = []
    for b in s.encode("utf-8"):
        c = chr(b)
        if c in "\\\"":
            out.append("\\" + c)
        elif 0x20 <= b < 0x7F:
            out.append(c)
        else:
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def ident(pid: str) -> str:
    return "p_" + pid.replace("-", "_")


def emit(profiles: list[dict]) -> str:
    o = ["/*\n"
         " * Generated by tools/gen_esc_profiles.py from\n"
         " * shared/esc/profiles/.\n"
         " * Do not edit: change the JSON and run the generator.\n"
         " *\n"
         " * SPDX-License-Identifier: MIT\n"
         " */\n\n"
         "#include \"esc_profile.h\"\n"]
    for p in profiles:
        n = ident(p["id"])
        o.append(f"\n/* {p['id']} */\n")
        o.append(f"static const char *const {n}_steps[] = {{\n")
        o += [f"    {c_str(s)},\n" for s in p["steps"]]
        o.append("};\n")
        o.append(f"static const esc_model_t {n}_models[] = {{\n")
        for m in p["models"]:
            o.append(f"    {{ {c_str(m['name'])}, {m['cmin']}u, {m['cmax']}u, "
                     f"{m['nimh']}, {m['v']}u, {m['a']}u }},\n")
        o.append("};\n")
        for k, it in enumerate(p["items"]):
            o.append(f"static const esc_value_t {n}_v{k}[] = {{\n")
            for v in it["values"]:
                o.append(f"    {{ {c_str(v['name'])}, {v['n']}u, "
                         f"{v['d']} }},\n")
            o.append("};\n")
            if it["applies"]:
                o.append(f"static const char *const {n}_a{k}[] = {{\n")
                o += [f"    {c_str(x)},\n" for x in it["applies"]]
                o.append("};\n")
        if p["items"]:
            o.append(f"static const esc_item_t {n}_items[] = {{\n")
            for k, it in enumerate(p["items"]):
                ap = f"{n}_a{k}" if it["applies"] else "NULL"
                o.append(f"    {{ {c_str(it['name'])}, {c_str(it['key'])}, "
                         f"{it['n']}u, {len(it['values'])}u, {n}_v{k},\n"
                         f"      {len(it['applies'])}u, {ap}, "
                         f"{c_str(it['when'])} }},\n")
            o.append("};\n")
    o.append("\nconst esc_profile_t esc_profiles_builtin[] = {\n")
    for p in profiles:
        n = ident(p["id"])
        items = f"{n}_items" if p["items"] else "NULL"
        o.append(
            f"    {{ {c_str(p['id'])}, {c_str(p['brand'])},\n"
            f"      {c_str(p['family'])},\n"
            f"      {p['scheme']}, {p['encoding']}, {p['announce']},"
            f" {p['auto']},\n"
            f"      {c_str(p['note'])},\n"
            f"      {p['entry_thr']}, {p['entry_after']}, {p['hold']}u,"
            f" {p['select_thr']}, {p['skip_thr']},\n"
            f"      {p['les']}u, {p['beep']}u, {p['gap']}u, {p['ggap']}u,"
            f" {p['repeat']}, {p['one']}, {p['verified']},\n"
            f"      {len(p['steps'])}u, {n}_steps,"
            f" {len(p['models'])}u, {n}_models,"
            f" {len(p['items'])}u, {items} }},\n")
    o.append("};\n\n")
    o.append("const size_t esc_profiles_builtin_count =\n"
             "    sizeof(esc_profiles_builtin)\n"
             "    / sizeof(esc_profiles_builtin[0]);\n")
    return "".join(o)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the checked-in C file differs from the JSON")
    args = ap.parse_args()
    profiles, bad = [], []
    for path in sorted(SRC.glob("*.json")):
        try:
            profiles.append(check(path))
        except (Bad, ValueError) as e:
            bad.append(str(e))
    if bad:
        print("\n".join(bad), file=sys.stderr)
        return 1
    profiles.sort(key=lambda p: p["id"].encode())   # strcmp order
    text_ = emit(profiles)
    if args.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text_:
            print(f"{OUT.relative_to(REPO)} is out of date: run "
                  "tools/gen_esc_profiles.py", file=sys.stderr)
            return 1
        print(f"{len(profiles)} profiles agree")
        return 0
    OUT.write_text(text_, encoding="utf-8")
    print(f"wrote {OUT.relative_to(REPO)}: {len(profiles)} profiles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
