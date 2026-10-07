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
import tempfile

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
# When an operator's step at the ESC is due, in the order of a run.
MANUAL = {"before_power": "ESC_MANUAL_BEFORE_POWER",
          "at_power_up": "ESC_MANUAL_AT_POWER_UP",
          "before_menu": "ESC_MANUAL_BEFORE_MENU",
          "during_menu": "ESC_MANUAL_DURING_MENU",
          "after_programming": "ESC_MANUAL_AFTER_PROGRAMMING"}
MANUAL_MAX = 4          # steps in one profile: ESC_MANUAL_MAX
AFTER_MAX = 4           # moves after a value's selection: ESC_AFTER_MAX
ACTION_MAX = 120        # bytes of one step's text: ESC_MANUAL_ACTION_MAX


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
    # A string first: a list or an object is not hashable, and the card
    # reader refuses it as it refuses any other unknown value.
    want(isinstance(v, str) and v in table, f"{where}.{key}",
         f"not one of {', '.join(table)}")
    return table[v]


# The card reader's limits, so a file one takes the other takes.
MAX_BYTES = 65536
MAX_DEPTH = 16          # nesting below the top-level object
MAX_MEMBERS = 64        # members of one object


def no_twice(pairs: list[tuple[str, object]]) -> dict:
    """An object, refused if a key comes twice: json.loads() would keep the
    last and esc_profile_parse() refuses the file, so neither may pass it.
    Nor one with more than MAX_MEMBERS, where the duplicate check would
    cost the panel time quadratic in the count."""
    if len(pairs) > MAX_MEMBERS:
        raise Bad(f"an object with more than {MAX_MEMBERS} members")
    out: dict = {}
    for k, v in pairs:
        if k in out:
            raise Bad(f"key {k!r} twice")
        out[k] = v
    return out


def no_constant(name: str) -> float:
    raise Bad(f"{name} is not a JSON number")


def no_deeper(v: object, where: str, depth: int = 0) -> None:
    """No value nested deeper than the card reader follows."""
    want(depth <= MAX_DEPTH, where, f"nested deeper than {MAX_DEPTH}")
    if isinstance(v, dict):
        for k, x in v.items():
            no_deeper(x, f"{where}.{k}", depth + 1)
    elif isinstance(v, list):
        for i, x in enumerate(v):
            no_deeper(x, f"{where}[{i}]", depth + 1)


def optional(d: dict, key: str, where: str, kind: type, default: object):
    """A field that may be absent or null, and is of @kind when present:
    `false` or `0` is not an empty value here, as it is not to the card
    reader."""
    v = d.get(key)
    if v is None:
        return default
    want(isinstance(v, kind), f"{where}.{key}", f"not a {kind.__name__}")
    return v


def no_nul(v: object, where: str) -> None:
    """No string holds U+0000, which in C would end the string early, nor
    half a surrogate pair, which json.loads() keeps and UTF-8 cannot carry.
    Keys too: esc_profile_parse() checks every string in the file."""
    if isinstance(v, str):
        want("\0" not in v, where, "a string holds \\u0000")
        want(not any(0xD800 <= ord(c) <= 0xDFFF for c in v), where,
             "half a surrogate pair")
    elif isinstance(v, dict):
        for k, x in v.items():
            no_nul(k, where)
            no_nul(x, f"{where}.{k}")
    elif isinstance(v, list):
        for i, x in enumerate(v):
            no_nul(x, f"{where}[{i}]")


def after_select(v: dict, w: str) -> list[str]:
    """The moves a value asks for after its select move: absent or null,
    none; else 1 to AFTER_MAX of min, mid, max."""
    a = v.get("after_select")
    if a is None:
        return []
    want(isinstance(a, list) and 0 < len(a) <= AFTER_MAX,
         f"{w}.after_select", f"not 1-{AFTER_MAX} moves")
    moves = {k: x for k, x in THROTTLE.items() if k != "none"}
    for i, m in enumerate(a):
        want(isinstance(m, str) and m in moves, f"{w}.after_select[{i}]",
             f"not one of {', '.join(moves)}")
    return [moves[m] for m in a]


def check_manual(d: dict, w: str, auto: str) -> list[dict]:
    """The operator's steps at the ESC: absent or null, none.  Only an
    assisted profile has them, at most MANUAL_MAX, in the order a run
    meets them."""
    man = d.get("manual")
    if man is None:
        return []
    want(isinstance(man, list) and 0 < len(man) <= MANUAL_MAX,
         f"{w}.manual", f"not 1-{MANUAL_MAX} steps")
    want(auto == "ESC_AUTO_ASSISTED", f"{w}.manual",
         "only on an assisted profile")
    out, last, starts = [], 0, None
    for i, m in enumerate(man):
        mw = f"{w}.manual[{i}]"
        want(isinstance(m, dict), mw, "not an object")
        when = pick(m, "when", mw, MANUAL)
        at = list(MANUAL.values()).index(when)
        want(at >= last, f"{mw}.when", "before the step above it")
        last = at
        action = text(m, "action", mw)
        want(len(action.encode("utf-8")) <= ACTION_MAX, f"{mw}.action",
             f"longer than {ACTION_MAX} bytes")
        hold = num(m, "hold_ms", mw, 60000)
        want(hold is None or when == "ESC_MANUAL_AT_POWER_UP",
             f"{mw}.hold_ms", "only for at_power_up")
        # The German beside it: absent or null, none; else as the action.
        de = "" if m.get("action_de") is None else text(m, "action_de", mw)
        want(len(de.encode("utf-8")) <= ACTION_MAX, f"{mw}.action_de",
             f"longer than {ACTION_MAX} bytes")
        # Whether the step starts the menu: absent or null, false.
        sm = m.get("starts_menu")
        want(sm is None or isinstance(sm, bool), f"{mw}.starts_menu",
             "not a boolean")
        sm = bool(sm)
        want(not sm or when == "ESC_MANUAL_BEFORE_MENU",
             f"{mw}.starts_menu", "only for before_menu")
        want(not sm or starts is None, f"{mw}.starts_menu",
             "a second step that starts the menu")
        want(starts is None or when != "ESC_MANUAL_BEFORE_MENU",
             f"{mw}.when", "before_menu after the step that starts the menu")
        if sm:
            starts = i
        out.append({"when": when, "action": action, "hold": hold or 0,
                    "de": de, "sm": "true" if sm else "false"})
    return out


def check(path: pathlib.Path) -> dict:
    """The profile, reduced to what the firmware carries; raises Bad."""
    w = path.name
    raw = path.read_bytes()
    want(len(raw) <= MAX_BYTES, w, f"larger than {MAX_BYTES} bytes")
    try:
        d = json.loads(raw.decode("utf-8"),
                       object_pairs_hook=no_twice,
                       parse_constant=no_constant)
    except Bad as e:
        raise Bad(f"{w}: {e}") from None
    no_nul(d, w)
    no_deeper(d, w)
    want(isinstance(d, dict), w, "not an object")
    num(d, "schema", w, 1, null_ok=False, lo=1)
    pid = text(d, "id", w)
    want(re.fullmatch(r"[a-z0-9-]{1,48}", pid) is not None, f"{w}.id",
         "not 1-48 of a-z 0-9 -")
    want(pid == path.stem, f"{w}.id", "differs from the file name")
    # isinstance, not membership: 0 == False and 1 == True in Python.
    want(isinstance(d.get("verified"), bool), f"{w}.verified",
         "not a boolean")
    auto = pick(d, "automatable", w, AUTO)
    note = optional(d, "automatable_note", w, str, "")
    want(auto == "ESC_AUTO_FULL" or note != "", f"{w}.automatable_note",
         "needed when not full")
    manual = check_manual(d, w, auto)

    s = d.get("scheme")
    want(isinstance(s, dict), f"{w}.scheme", "not an object")
    e, a = s.get("entry"), s.get("announce")
    sel, skip = s.get("select"), s.get("skip")
    for k, v in (("entry", e), ("announce", a), ("select", sel),
                 ("skip", skip)):
        want(isinstance(v, dict), f"{w}.scheme.{k}", "not an object")
    # Only a two-stage menu has one: absent or null, the select move stores
    # the value too.
    vsel = s.get("value_select")
    want(vsel is None or isinstance(vsel, dict), f"{w}.scheme.value_select",
         "not an object")
    # Where the stick rests while the menu sounds: absent or null, where the
    # entry left it.
    lis = s.get("listen")
    want(lis is None or isinstance(lis, dict), f"{w}.scheme.listen",
         "not an object")
    # The move that stores a selection once the ESC has answered it: absent
    # or null, the selection stores.
    sto = s.get("store")
    want(sto is None or isinstance(sto, dict), f"{w}.scheme.store",
         "not an object")
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
        "select_ms": num(sel, "within_ms", f"{w}.scheme.select", 60000) or 0,
        "vsel_thr": "ESC_THR_NONE" if vsel is None else
        pick(vsel, "throttle", f"{w}.scheme.value_select", THROTTLE),
        "vsel_ms": 0 if vsel is None else
        num(vsel, "within_ms", f"{w}.scheme.value_select", 60000) or 0,
        "skip_thr": pick(skip, "throttle", f"{w}.scheme.skip", THROTTLE),
        "listen_thr": "ESC_THR_NONE" if lis is None else
        pick(lis, "throttle", f"{w}.scheme.listen",
             {k: v for k, v in THROTTLE.items() if k != "none"}),
        "store_thr": "ESC_THR_NONE" if sto is None else
        pick(sto, "throttle", f"{w}.scheme.store",
             {k: v for k, v in THROTTLE.items() if k != "none"}),
        "les": les or 0,
        "beep": num(a, "beep_ms", f"{w}.scheme.announce", 60000) or 0,
        "gap": num(a, "gap_ms", f"{w}.scheme.announce", 60000) or 0,
        "ggap": num(a, "group_gap_ms", f"{w}.scheme.announce", 60000) or 0,
        "repeat": -1 if rep is None else rep,
        "one": "true" if cpe == "one" else "false",
        "verified": "true" if d["verified"] else "false",
        "steps": steps, "models": [], "items": [], "manual": manual,
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
        ct = m.get("cell_type")
        ct = "lipo" if ct is None else ct
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
        applies = optional(it, "applies_to", iw, list, [])
        want(len(applies) <= 255
             and all(x in names for x in applies), f"{iw}.applies_to",
             "not a list of this profile's model names")
        when = optional(it, "applies_when", iw, str, "")
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
            want(isinstance(dflt, bool), f"{vw}.default", "not a boolean")
            defaults += dflt
            # The stick position this value is programmed from, where the
            # manual gives one other than the profile's entry: absent or
            # null, the entry's.
            et = "ESC_THR_NONE" if v.get("entry_throttle") is None else \
                pick(v, "entry_throttle", vw,
                     {k: x for k, x in THROTTLE.items() if k != "none"})
            out.append({"name": text(v, "name", vw), "n": vn,
                        "d": "true" if dflt else "false", "et": et,
                        # Power-on to the menu for this value, where the
                        # manual gives one other than the entry's.
                        "eh": num(v, "entry_hold_ms", vw, 600000) or 0,
                        "after": after_select(v, vw)})
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
                         f"{v['d']}, {v['et']}, {v['eh']}u, "
                         f"{len(v['after'])}u, "
                         f"{{ {', '.join(v['after']) or '0'} }} }},\n")
            o.append("};\n")
            if it["applies"]:
                o.append(f"static const char *const {n}_a{k}[] = {{\n")
                o += [f"    {c_str(x)},\n" for x in it["applies"]]
                o.append("};\n")
        if p["manual"]:
            o.append(f"static const esc_manual_t {n}_manual[] = {{\n")
            for m in p["manual"]:
                o.append(f"    {{ {m['when']}, {c_str(m['action'])}, "
                         f"{m['hold']}u, {c_str(m['de'])}, {m['sm']} }},\n")
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
        manual = f"{n}_manual" if p["manual"] else "NULL"
        o.append(
            f"    {{ {c_str(p['id'])}, {c_str(p['brand'])},\n"
            f"      {c_str(p['family'])},\n"
            f"      {p['scheme']}, {p['encoding']}, {p['announce']},"
            f" {p['auto']},\n"
            f"      {c_str(p['note'])},\n"
            f"      {p['entry_thr']}, {p['entry_after']}, {p['hold']}u,\n"
            f"      {p['select_thr']}, {p['select_ms']}u,"
            f" {p['vsel_thr']}, {p['vsel_ms']}u, {p['skip_thr']},"
            f" {p['listen_thr']}, {p['store_thr']},\n"
            f"      {p['les']}u, {p['beep']}u, {p['gap']}u, {p['ggap']}u,"
            f" {p['repeat']}, {p['one']}, {p['verified']},\n"
            f"      {len(p['steps'])}u, {n}_steps,"
            f" {len(p['models'])}u, {n}_models,"
            f" {len(p['items'])}u, {items},\n"
            f"      {len(p['manual'])}u, {manual} }},\n")
    o.append("};\n\n")
    o.append("const size_t esc_profiles_builtin_count =\n"
             "    sizeof(esc_profiles_builtin)\n"
             "    / sizeof(esc_profiles_builtin[0]);\n")
    return "".join(o)


def self_test() -> list[str]:
    """The inputs test_esc_profiles.c holds the card reader to, held to the
    generator: each mutation of a profile of record must be refused, and
    each control accepted.  Run by --check, so a rule dropped here fails CI
    as a rule dropped in C does."""
    base = (SRC / "align-rce-bl15x.json").read_text(encoding="utf-8")

    def at(old: str, new: str) -> bytes:
        assert old in base, old
        return base.replace(old, new, 1).encode("utf-8")

    head = '"schema": 1,'
    sel = '"select": {'
    auto = '"automatable": "none",'
    val = '"name": "disabled'

    def man(steps: str, kind: str = "assisted") -> bytes:
        return at(auto, f'"automatable": "{kind}", "manual": {steps},')

    jumper = '{"when": "before_power", "action": "Fit the jumper."}'
    pull = '{"when": "before_menu", "action": "Pull the jumper."}'
    refuse = {
        "schema true": at(head, '"schema": true,'),
        "key twice": at(head, head + ' "schema": 1,'),
        "escaped key twice": at(head, head + ' "sch\\u0065ma": 1,'),
        "NUL": at(head, head + ' "n": "a\\u0000b",'),
        "lone surrogate": at(head, head + ' "n": "\\ud800",'),
        "NaN": at(head, head + ' "n": NaN,'),
        "applies_to false": at('"applies_to": null', '"applies_to": false'),
        "applies_when 0": at('"applies_to": null',
                             '"applies_to": null, "applies_when": 0'),
        "cell_type false": at('"cell_type": "lipo"', '"cell_type": false'),
        "nested 17": at(head, head + ' "n": ' + "[" * 16 + "1" + "]" * 16
                        + ","),
        "verified 0": at('"verified": false', '"verified": 0'),
        "default 1": at('"number": 1,\n          "name": "disabled',
                        '"number": 1, "default": 1,\n'
                        '          "name": "disabled'),
        "raw and escaped key twice": at(head, head
                                        + ' "\u00e9": 1, "\\u00e9": 1,'),
        "65 members": at(head, head + ' "x": {' + ", ".join(
            f'"k{i}": 1' for i in range(65)) + "},"),
        "too large": at(head, head + ' "pad": "' + "x" * MAX_BYTES + '",'),
        "not UTF-8": base.encode("utf-8").replace(b'"Align"', b'"Al\xffign"',
                                                  1),
        "value_select a string": at(sel, '"value_select": "max", ' + sel),
        "within_ms 60001": at('"within_ms": null', '"within_ms": 60001'),
        "listen a string": at(sel, '"listen": "min", ' + sel),
        "listen none": at(sel, '"listen": {"throttle": "none"}, ' + sel),
        "listen no throttle": at(sel, '"listen": {}, ' + sel),
        "listen throttle null": at(sel, '"listen": {"throttle": null}, '
                                   + sel),
        "listen throttle a list": at(sel, '"listen": {"throttle": []}, '
                                     + sel),
        "listen throttle an object": at(sel, '"listen": {"throttle": {}}, '
                                        + sel),
        "listen throttle 0": at(sel, '"listen": {"throttle": 0}, ' + sel),
        "listen throttle MIN": at(sel, '"listen": {"throttle": "MIN"}, '
                                  + sel),
        "listen throttle twice": at(sel, '"listen": {"throttle": "min", '
                                    '"throttle": "max"}, ' + sel),
        "store a string": at(sel, '"store": "min", ' + sel),
        "store none": at(sel, '"store": {"throttle": "none"}, ' + sel),
        "store throttle a list": at(sel, '"store": {"throttle": []}, '
                                    + sel),
        "value entry none": at(val, '"entry_throttle": "none", ' + val),
        "value entry MID": at(val, '"entry_throttle": "MID", ' + val),
        "value entry an object": at(val, '"entry_throttle": {"throttle": '
                                    '"mid"}, ' + val),
        "value entry false": at(val, '"entry_throttle": false, ' + val),
        "value hold 600001": at(val, '"entry_hold_ms": 600001, ' + val),
        "value hold -1": at(val, '"entry_hold_ms": -1, ' + val),
        "value hold 2.5": at(val, '"entry_hold_ms": 2.5, ' + val),
        "value hold a string": at(val, '"entry_hold_ms": "5000", ' + val),
        "after a string": at(val, '"after_select": "min", ' + val),
        "after empty": at(val, '"after_select": [], ' + val),
        "after 5 moves": at(val, '"after_select": ["min", "max", "min", '
                            '"max", "min"], ' + val),
        "after none": at(val, '"after_select": ["none"], ' + val),
        "after MIN": at(val, '"after_select": ["MIN"], ' + val),
        "after null inside": at(val, '"after_select": [null], ' + val),
        "after an object": at(val, '"after_select": [{"throttle": "min"}], '
                              + val),
        "manual on a full profile": man(f"[{jumper}]", "full"),
        "manual on a profile nobody runs": man(f"[{jumper}]", "none"),
        "manual empty": man("[]"),
        "manual an object": man(jumper),
        "manual 5 steps": man("[" + ", ".join([jumper] * 5) + "]"),
        "manual step a string": man('["Fit the jumper."]'),
        "manual when unknown": man('[{"when": "later", "action": "x"}]'),
        "manual when missing": man('[{"action": "x"}]'),
        "manual out of order": man(f"[{pull}, {jumper}]"),
        "manual action empty": man('[{"when": "before_menu", '
                                   '"action": ""}]'),
        "manual action missing": man('[{"when": "before_menu"}]'),
        "manual action 121 bytes": man('[{"when": "before_menu", '
                                       '"action": "' + "x" * 121 + '"}]'),
        "manual action 61 umlauts": man('[{"when": "before_menu", '
                                        '"action": "' + "ü" * 61
                                        + '"}]'),
        "manual hold before the menu": man(
            '[{"when": "before_menu", "action": "x", "hold_ms": 0}]'),
        "manual hold 60001": man(
            '[{"when": "at_power_up", "action": "x", "hold_ms": 60001}]'),
        "manual hold a string": man(
            '[{"when": "at_power_up", "action": "x", "hold_ms": "2"}]'),
        "manual de empty": man('[{"when": "before_menu", "action": "x", '
                               '"action_de": ""}]'),
        "manual de a number": man('[{"when": "before_menu", "action": "x", '
                                  '"action_de": 1}]'),
        "manual de 61 umlauts": man('[{"when": "before_menu", "action": "x", '
                                    '"action_de": "' + "\u00fc" * 61
                                    + '"}]'),
        "manual starts 1": man('[{"when": "before_menu", "action": "x", '
                               '"starts_menu": 1}]'),
        "manual starts a string": man('[{"when": "before_menu", '
                                      '"action": "x", "starts_menu": "y"}]'),
        "manual starts before power": man(
            '[{"when": "before_power", "action": "x", "starts_menu": true}]'),
        "manual starts after": man(
            '[{"when": "after_programming", "action": "x", '
            '"starts_menu": true}]'),
        "manual starts twice": man(
            '[{"when": "before_menu", "action": "x", "starts_menu": true}, '
            '{"when": "before_menu", "action": "y", "starts_menu": true}]'),
        "manual before_menu after the start": man(
            '[{"when": "before_menu", "action": "x", "starts_menu": true}, '
            '{"when": "before_menu", "action": "y"}]'),
        "manual de 121 bytes": man('[{"when": "before_menu", "action": "x", '
                                   '"action_de": "' + "x" * 121 + '"}]'),
    }
    accept = {
        "plain": base.encode("utf-8"),
        "surrogate pair": at(head, head + ' "n": "\\ud83d\\ude00",'),
        "nested 16": at(head, head + ' "n": ' + "[" * 15 + "1" + "]" * 15
                        + ","),
        "64 members": at(head, head + ' "x": {' + ", ".join(
            f'"k{i}": 1' for i in range(64)) + "},"),
        # Past Python's default of 4,300 digits for int(); the card reader
        # skips any length of number it does not read.
        "4301 digits": at(head, head + ' "n": ' + "1" * 4301 + ","),
        "value_select null": at(sel, '"value_select": null, ' + sel),
        "listen null": at(sel, '"listen": null, ' + sel),
        "listen min": at(sel, '"listen": {"throttle": "min"}, ' + sel),
        "listen escaped min": at(sel, '"listen": {"throttle": "m\\u0069n"}, '
                                 + sel),
        "listen with more": at(sel, '"listen": {"throttle": "mid", '
                               '"x": [1, 2]}, ' + sel),
        "store null": at(sel, '"store": null, ' + sel),
        "store min": at(sel, '"store": {"throttle": "min"}, ' + sel),
        "value entry null": at(val, '"entry_throttle": null, ' + val),
        "value entry mid": at(val, '"entry_throttle": "mid", ' + val),
        "value hold null": at(val, '"entry_hold_ms": null, ' + val),
        "value hold 600000": at(val, '"entry_hold_ms": 600000, ' + val),
        "after null": at(val, '"after_select": null, ' + val),
        "after min": at(val, '"after_select": ["min"], ' + val),
        "after 4 moves": at(val, '"after_select": ["min", "mid", "max", '
                            '"m\\u0069n"], ' + val),
        "manual null": at(auto, auto + ' "manual": null,'),
        "manual two steps": man(f"[{jumper}, {pull}]"),
        "manual four steps of one kind": man("[" + ", ".join([pull] * 4)
                                             + "]"),
        "manual action 120 bytes": man('[{"when": "before_menu", '
                                       '"action": "' + "x" * 120 + '"}]'),
        "manual action 60 umlauts": man('[{"when": "before_menu", '
                                        '"action": "' + "ü" * 60
                                        + '"}]'),
        "manual hold at power-up": man(
            '[{"when": "at_power_up", "action": "x", "hold_ms": 60000}]'),
        "manual hold null": man(
            '[{"when": "after_programming", "action": "x", '
            '"hold_ms": null}]'),
        "manual with more": man('[{"when": "during_menu", "action": "x", '
                                '"source": "p. 5"}]'),
        "manual starts": man('[{"when": "before_menu", "action": "x"}, '
                             '{"when": "before_menu", "action": "y", '
                             '"starts_menu": true}, '
                             '{"when": "after_programming", "action": "z"}]'),
        "manual starts false": man('[{"when": "before_menu", "action": "x", '
                                   '"starts_menu": false}]'),
        "manual starts null": man('[{"when": "before_menu", "action": "x", '
                                  '"starts_menu": null}]'),
        "manual de null": man('[{"when": "before_menu", "action": "x", '
                              '"action_de": null}]'),
        "manual de 60 umlauts": man('[{"when": "before_menu", "action": "x", '
                                    '"action_de": "' + "\u00fc" * 60
                                    + '"}]'),
        "manual de escaped": man('[{"when": "before_menu", "action": "x", '
                                 '"action_de": "Br\\u00fccke ab"}]'),
    }
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "align-rce-bl15x.json"
        for name, data in [*refuse.items(), *accept.items()]:
            path.write_bytes(data)
            try:
                check(path)
                took = True
            except (Bad, ValueError):
                took = False
            if took != (name in accept):
                bad.append(f"self-test: {name} was "
                           f"{'accepted' if took else 'refused'}")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the checked-in C file differs from the JSON")
    args = ap.parse_args()
    # json.loads() reads a whole number with int(), which from Python 3.11
    # refuses more than 4,300 digits; the card reader takes any length in a
    # field it skips, and so does this.
    if hasattr(sys, "set_int_max_str_digits"):
        sys.set_int_max_str_digits(0)
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
        failed = self_test()
        if failed:
            print("\n".join(failed), file=sys.stderr)
            return 1
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
