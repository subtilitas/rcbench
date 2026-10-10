#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The profile generator and the card reader, held to one verdict.

A profile is read in two places: tools/gen_esc_profiles.py turns the files
of record into the built-in table, and esc_profile_parse() reads a file from
the SD card.  A file one takes and the other refuses, or one both take with
a field read differently, is a profile that behaves one way built in and
another way from the card.

Each case is a profile of record, changed in one of two ways or in both:

- its structure, 1 to 3 times: a value replaced from a pool of boundary
  values, a key or an element removed, an element doubled, an optional key
  added, or a count, a length or a number put at its limit or 1 past it;
- its text, once: a number or a string spelled another way JSON has or has
  not, bytes that are not UTF-8, white space and other bytes between the
  tokens, a member added whose value is at a limit of the reader (nesting,
  members of one object, digits), the file cut short or padded to the
  size limit.

check() and esc_profile_parse() (through esc_parse_dump) read it; both
refuse it, or both take it and every parsed field is equal.  The reasons of
two refusals are not compared.  The generator refuses with Bad or
ValueError, as its main() takes them; any other exception is a failure.
A file keeps the name of the profile it is made from, and esc_parse_dump
holds the id to that name with esc_profile_file_is(), as the panel does
for a card file.

The cases come from random.Random(seed) and are the same on every run of
one Python version.  A failure states the seed and the case number, and
prints the file, so it is run again with

    fuzz_parity.py <esc_parse_dump> --seed <seed> --count <count>

The default run is 2 seeds of 1500 cases each.  Measured on one x86-64
host: 6.2 s, and 8.1 s with esc_parse_dump built under the sanitizers.
ctest stops it after 120 s.

usage: fuzz_parity.py <esc_parse_dump> [--seed N ...] [--count N]
"""
import argparse
import copy
import json
import pathlib
import random
import re
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_esc_profiles as g  # noqa: E402

SEEDS = (1, 20261009)
COUNT = 1500            # cases for each seed

SCHEME = list(g.SCHEME.values())
ENC = list(g.ENCODING.values())
ANN = list(g.ANNOUNCE.values())
AUTO = list(g.AUTO.values())
THR = list(g.THROTTLE.values())
MAN = list(g.MANUAL.values())


def hx(s):
    return "'" + s.encode("utf-8").hex() + "'"


def b(x):
    return 1 if x == "true" else 0


def dump(p):
    """check()'s result as esc_parse_dump prints the parser's."""
    o = [hx(p["id"]), hx(p["brand"]), hx(p["family"])]
    o.append("|%d %d %d %d|" % (SCHEME.index(p["scheme"]),
                                ENC.index(p["encoding"]),
                                ANN.index(p["announce"]),
                                AUTO.index(p["auto"])))
    o.append(hx(p["note"]))
    o.append("|%d %d %u|%d %u|%d %u|%d %d %d|%u %u %u %u %d %d %d|" % (
        THR.index(p["entry_thr"]), b(p["entry_after"]), p["hold"],
        THR.index(p["select_thr"]), p["select_ms"],
        THR.index(p["vsel_thr"]), p["vsel_ms"],
        THR.index(p["skip_thr"]), THR.index(p["listen_thr"]),
        THR.index(p["store_thr"]),
        p["les"], p["beep"], p["gap"], p["ggap"], p["repeat"], b(p["one"]),
        b(p["verified"])))
    o.append("S%u" % len(p["steps"]))
    o += [hx(s) for s in p["steps"]]
    o.append("|M%u" % len(p["models"]))
    for m in p["models"]:
        o.append(hx(m["name"]))
        o.append("(%u %u %d %u %u %u)" % (m["cmin"], m["cmax"], b(m["nimh"]),
                                          m["v"], m["a"], m["vmin"]))
    o.append("|I%u" % len(p["items"]))
    for it in p["items"]:
        o.append(hx(it["name"]) + hx(it["key"]))
        o.append("(%u)V%u" % (it["n"], len(it["values"])))
        for v in it["values"]:
            o.append(hx(v["name"]))
            o.append("(%u %d %d %u A%u" % (v["n"], b(v["d"]),
                                           THR.index(v["et"]), v["eh"],
                                           len(v["after"])))
            for a in v["after"]:
                o.append(" %d" % THR.index(a))
            o.append(")")
        o.append("P%u" % len(it["applies"]))
        o += [hx(a) for a in it["applies"]]
        o.append(hx(it["when"]))
    o.append("|H%u" % len(p["manual"]))
    for m in p["manual"]:
        o.append("(%d " % MAN.index(m["when"]) + hx(m["action"]))
        o.append(" %u " % m["hold"] + hx(m["de"]))
        o.append(" %d %d)" % (b(m["sm"]), b(m["lk"])))
    return "".join(o)


POOL = [None, True, False, 0, 1, -1, 2, 3, 4, 5, 254, 255, 256, 1000, 59999,
        60000, 60001, 65535, 65536, 600000, 600001, 999999, 1000000, 1000001,
        100000000, 100000001, 1000000000, 99999999999, 10**19, 10**30,
        1.0, 0.0, 2.5, 1e2, "", "min", "mid", "max", "none", "x", "one",
        "many", "lipo", "nimh", "full", "assisted", "count", "short_long",
        "item", "value", "item_then_value", "before_power", "at_power_up",
        "before_menu", "during_menu", "before_power_off", "after_programming",
        "before_power_on", "after_power_on",
        [], {}, ["min"], ["max"], ["mid", "min"], ["min", "min"],
        ["min", "mid", "max", "mid"], ["min", "mid", "max", "mid", "min"],
        [""], [1], [None], [[]], [{}], "a" * 120, "a" * 121, "ä" * 60,
        "ä" * 61, "A", "a_b", "a-b", "a" * 32, "a" * 33, "a" * 48,
        "a" * 49, " ", "\t", "ü", "\U0001f600", "\x7f", "\x01"]

ADDED = ["after_select", "entry_throttle", "entry_hold_ms", "hold_ms",
         "starts_menu", "locks", "action_de", "default", "applies_to",
         "applies_when", "cell_type", "v_min_mv", "manual", "listen", "store",
         "value_select", "repeat", "long_equals_short", "within_ms",
         "throttle", "when", "number"]


def paths(v, at=()):
    yield at
    if isinstance(v, dict):
        for k, x in v.items():
            yield from paths(x, at + (k,))
    elif isinstance(v, list):
        for i, x in enumerate(v[:6]):
            yield from paths(x, at + (i,))


def get(v, at):
    for k in at:
        v = v[k]
    return v


def dicts(v):
    return [x for x in v if isinstance(x, dict)] \
        if isinstance(v, list) else []


def home(rng, d, key):
    """An object of @d that @key is read in, or None."""
    items = dicts(d.get("items"))
    if key == "manual":
        found = [d]
    elif key in ("listen", "store", "value_select"):
        found = [d.get("scheme")]
    elif key in ("after_select", "entry_throttle", "entry_hold_ms",
                 "default"):
        found = [v for it in items for v in dicts(it.get("values"))]
    elif key in ("applies_to", "applies_when"):
        found = items
    elif key in ("cell_type", "v_min_mv"):
        found = dicts(d.get("models"))
    elif key in ("hold_ms", "starts_menu", "locks", "action_de"):
        found = dicts(d.get("manual"))
    else:
        found = []
    found = [x for x in found if isinstance(x, dict)]
    return rng.choice(found) if found else None


# The most each whole number may be, by its key; hold_ms by where it is.
MOST = {"within_ms": 60000, "beep_ms": 60000, "gap_ms": 60000,
        "group_gap_ms": 60000, "repeat": 255, "long_equals_short": 255,
        "cells_min": 255, "cells_max": 255, "v_max_mv": 1000000,
        "v_min_mv": 1000000, "current_a": 65535, "entry_hold_ms": 600000,
        "number": 255, "schema": 1}


def assisted(d):
    d["automatable"] = "assisted"
    d.setdefault("automatable_note", "x")


def at_a_limit(rng, d, model_names):
    """One count, length or number of @d at its limit or 1 past it."""
    over = rng.randint(0, 1)
    items = dicts(d.get("items"))
    values = [v for it in items for v in dicts(it.get("values"))]
    scheme = d.get("scheme") if isinstance(d.get("scheme"), dict) else {}
    kind = rng.randrange(10)
    if kind == 0 and values:
        # The first move leaves where the stick is: the store move, else
        # the move that stores the value.
        last = "none"
        for key in ("store", "value_select", "select"):
            move = scheme.get(key)
            move = move.get("throttle") if isinstance(move, dict) else None
            if move in ("min", "mid", "max"):
                last = move
                break
        moves = []
        while len(moves) < g.AFTER_MAX + over:
            last = rng.choice([m for m in ("min", "mid", "max") if m != last])
            moves.append(last)
        rng.choice(values)["after_select"] = moves
    elif kind == 1:
        when = rng.choice(list(g.MANUAL))
        d["manual"] = [{"when": when, "action": "do it"}
                       for _ in range(g.MANUAL_MAX + over)]
        assisted(d)
    elif kind == 2:
        letter = rng.choice(["a", "\u00fc"])
        size = len(letter.encode("utf-8"))
        d["manual"] = [{"when": "before_menu", "action": "do it",
                        rng.choice(["action", "action_de"]):
                        letter * ((g.ACTION_MAX + size * over) // size)}]
        assisted(d)
    elif kind == 3 and isinstance(scheme.get("entry"), dict):
        scheme["entry"]["steps"] = ["s"] * (255 + over)
    elif kind == 4 and dicts(d.get("models")):
        first = dicts(d["models"])[0]
        d["models"] += [dict(first, name="m%d" % i)
                        for i in range(1000 + over - len(d["models"]))]
    elif kind == 5 and items:
        first = dict(items[0], values=items[0].get("values", [])[:1])
        d["items"] = [dict(first, number=i + 1) for i in range(255 + over)]
    elif kind == 6 and items and values:
        # 0 to 255 are 256 numbers, so one value past the count has them.
        rng.choice(items)["values"] = [
            dict(values[0], number=i, default=False)
            for i in range(255 + over)]
    elif kind == 7 and items:
        rng.choice(items)["applies_to"] = \
            [rng.choice(model_names)] * (255 + over)
    elif kind == 8 and items:
        rng.choice(items)["key"] = "k" * (32 + over)
    else:
        ps = [p for p in paths(d)
              if p and (p[-1] in MOST or p[-1] == "hold_ms")]
        if ps:
            at = rng.choice(ps)
            most = MOST.get(at[-1], 600000 if "entry" in at else 60000)
            least = 1 if at[-1] == "number" and "values" not in at else 0
            get(d, at[:-1])[at[-1]] = rng.choice(
                [most, most + 1, least, least - 1])


def mutate(rng, d, model_names):
    ps = [p for p in paths(d) if p and p[0] not in ("sources", "notes")]
    at = rng.choice(ps)
    parent = get(d, at[:-1])
    op = rng.random()
    if op < 0.15:
        if isinstance(parent, dict):
            del parent[at[-1]]
        else:
            parent.pop(at[-1])
    elif op < 0.25 and isinstance(parent, list):
        parent.insert(at[-1], copy.deepcopy(parent[at[-1]]))
    elif op < 0.40 and isinstance(get(d, at), dict):
        tgt = get(d, at)
        key = rng.choice(ADDED)
        # A copy: a later change to the case leaves the pool as it is.
        val = copy.deepcopy(rng.choice(POOL))
        # Mostly where the key is read, and mostly a value near what is
        # taken, so the limits are met from both sides.
        if rng.random() < 0.8:
            tgt = home(rng, d, key) or tgt
        if key == "applies_to" and rng.random() < 0.6:
            val = rng.sample(model_names, k=min(len(model_names),
                                                rng.randint(1, 2)))
        if key in ("listen", "store", "value_select") and rng.random() < 0.7:
            val = {"throttle": rng.choice(["min", "mid", "max", "none"]),
                   "within_ms": rng.choice([None, 0, 3000])}
        if key == "after_select" and rng.random() < 0.8:
            val = [rng.choice(["min", "mid", "max"])]
            for _ in range(rng.randint(0, g.AFTER_MAX)):
                val.append(rng.choice([m for m in ("min", "mid", "max")
                                       if m != val[-1]]))
        if key == "manual" and rng.random() < 0.8:
            whens = [rng.choice(list(g.MANUAL))
                     for _ in range(rng.randint(1, g.MANUAL_MAX + 1))]
            if rng.random() < 0.8:
                whens.sort(key=list(g.MANUAL).index)
            val = [{"when": when,
                    "action": "do it",
                    **({"hold_ms": rng.choice([0, 1, 60000, 60001])}
                       if rng.random() < 0.3 else {}),
                    **({"starts_menu": rng.choice([True, False, None])}
                       if rng.random() < 0.3 else {}),
                    **({"locks": rng.choice([True, False, None])}
                       if rng.random() < 0.3 else {})}
                   for when in whens]
            assisted(d)
        tgt[key] = val
    else:
        parent[at[-1]] = copy.deepcopy(rng.choice(POOL))


def nest(n):
    return b"[" * n + b"1" + b"]" * n


def members(n):
    return b"{" + b", ".join(b'"k%d": 1' % i for i in range(n)) + b"}"


# A whole number spelled as JSON allows and as it does not, and at the
# widths a reader may count in.
NUMBERS = [b"-0", b"0.0", b"1.0", b"1e0", b"1E0", b"1e+0", b"1e-0", b"10e-1",
           b"01", b"00", b"1.", b".5", b"+1", b"1e", b"0x1", b"--1", b"-",
           b"1e400", b"-1", b"1 2", b"1_0", b"\xd9\xa1", b"1" * 30,
           b"255", b"256", b"65535", b"65536", b"4294967295", b"4294967296",
           b"4294967297", b"2147483648", b"-2147483649",
           b"18446744073709551615", b"18446744073709551616",
           b"18446744073709551617", b"true", b"null", b'"1"']

# Put inside a string: escapes JSON has and has not, characters it wants
# escaped, and bytes that are not UTF-8.
IN_STRING = [b"\\u0000", b"\\ud800", b"\\udc00", b"\\ud83d\\ude00",
             b"\\uD83D\\uDE00", b"\\ude00\\ud83d", b"\\u00e9", b"\\u00E9",
             b"\\/", b"\\x41", b"\\a", b"\\u12", b"\\u12g4", b"\\",
             b"\\b\\f\\n\\r\\t\\\"\\\\", b"\x01", b"\t", b"\n", b"\x1f",
             b"\x7f", b"\xff", b"\xc0\x80", b"\xc1\xbf", b"\xed\xa0\x80",
             b"\xf4\x90\x80\x80", b"\xe2\x82", b"\x80", b"\xef\xbf\xbe",
             b"\xf0\x9f\x98\x80", b"\xc3\xa9"]

# The value of a member the reader skips.
SKIPPED = [b"NaN", b"Infinity", b"-Infinity", b"True", b"nul", b"'a'",
           nest(15), nest(16), nest(17), nest(3000), members(64),
           members(65), b"[1,]", b"[1 2]", b"[,1]", b"{,}", b'{"a" 1}',
           b'{"a":1,"a":2}', b'{"a":1,"\\u0061":2}', b'{"":1}', b"[]",
           b"{}", b"1" * 4301, b"-", b'"\\ud83d"', b"1e5",
           b"[-0, 0.5, 1e3, 2E-2, 10]", b'{"a":{"a":1}}', b"1.5e", b"/**/1"]

BEFORE = [b"\xef\xbb\xbf", b" ", b"\n", b"\x00", b"//c\n", b"\r\n\t "]
AFTER = [b" ", b"\n", b"\x00", b"x", b",", b"}", b"{}", b"\r\n\t ", b"\x0c",
         b"\xc2\xa0", b"\xef\xbb\xbf"]
BETWEEN = [b"\t", b"\r", b"\n", b"\x0c", b"\x0b", b"\xc2\xa0",
           b"\xe2\x80\xa8", b"/**/", b",", b"\x00", b"\xef\xbb\xbf"]

NUMBER = re.compile(rb'(?<=[:\[,] )-?\d+(?=[,\]}])')
STRING = re.compile(rb'"[^"\\]*"')
SIZES = (g.MAX_BYTES - 1, g.MAX_BYTES, g.MAX_BYTES + 1)


def edit_text(rng, raw):
    """@raw with one change to its text."""
    op = rng.randrange(10)
    if op < 2:                          # a number, spelled another way
        found = list(NUMBER.finditer(raw))
        if found:
            m = rng.choice(found)
            return raw[:m.start()] + rng.choice(NUMBERS) + raw[m.end():]
    elif op < 4:                        # a string or a key
        found = list(STRING.finditer(raw))
        if found:
            m = rng.choice(found)
            at = rng.randint(m.start() + 1, m.end() - 1)
            if rng.random() < 0.3 and at < m.end() - 1 and raw[at] < 0x80:
                # One character as its escape: the same string.
                return raw[:at] + b"\\u%04x" % raw[at] + raw[at + 1:]
            return raw[:at] + rng.choice(IN_STRING) + raw[at:]
    elif op < 6:                        # a member nothing reads
        key = rng.choice([b"zz", b"zz", b"zz", b"schema", b"sch\\u0065ma",
                          b"notes", b"\xc3\xa9"])
        # Half of them at the two limits a skipped value has, from both
        # sides: the file's object is 1 level itself.
        value = rng.choice(SKIPPED if rng.random() < 0.5 else
                           [nest(15), nest(16), members(64), members(65)])
        return b'{"' + key + b'": ' + value + b", " + raw[1:]
    elif op == 6:                       # between two tokens
        found = [m.start() for m in re.finditer(rb'[,:\[\]{}]', raw)]
        at = rng.choice(found) + rng.randint(0, 1)
        return raw[:at] + rng.choice(BETWEEN) + raw[at:]
    elif op == 7:                       # before the text, or after it
        if rng.random() < 0.5:
            return rng.choice(BEFORE) + raw
        return raw + rng.choice(AFTER)
    elif op == 8:                       # at the size limit, or cut short
        if rng.random() < 0.5:
            size = rng.choice(SIZES)
            return raw[:-1] + b" " * max(0, size - len(raw)) + raw[-1:]
        return raw[:rng.randrange(len(raw))]
    at = rng.randrange(len(raw))        # one byte, any value
    return raw[:at] + bytes([rng.randrange(256)]) + raw[at + 1:]


def make(rng, bases, out, count):
    """The cases of one seed, written under @p out: their paths, in order,
    None for a case whose text no file can hold."""
    made = []
    for n in range(count):
        src = rng.choice(bases)
        d = json.loads(src.read_text(encoding="utf-8"))
        names = [m.get("name") for m in d.get("models", [])
                 if isinstance(m, dict) and isinstance(m.get("name"), str)]
        if rng.random() < 0.3:
            assisted(d)
        how = rng.random()      # structure, text, or both
        for _ in range(rng.choice([1, 1, 1, 2, 3]) if how < 0.7 else 0):
            try:
                if rng.random() < 0.25:
                    at_a_limit(rng, d, names or ["x"])
                else:
                    mutate(rng, d, names or ["x"])
            except (KeyError, IndexError, TypeError):
                pass
        sub = out / ("c%05d" % n)
        sub.mkdir()
        path = sub / src.name       # the id is the file's name
        ascii_only = rng.random() < 0.5
        try:
            raw = json.dumps(d, ensure_ascii=ascii_only).encode("utf-8")
        except (UnicodeEncodeError, ValueError):
            made.append(None)
            continue
        if how >= 0.55:
            raw = edit_text(rng, raw)
        path.write_bytes(raw)
        made.append(path)
    return made


def run_seed(parse_dump, seed, count):
    """The differences of one seed, as text, and how many cases both ends
    took and both refused."""
    rng = random.Random(seed)
    bases = sorted((REPO / "shared" / "esc" / "profiles").glob("*.json"))
    problems = []
    both_ok = both_bad = 0
    with tempfile.TemporaryDirectory(prefix="rcbench-parity-") as tmp:
        made = make(rng, bases, pathlib.Path(tmp), count)
        files = [p for p in made if p is not None]
        res = subprocess.run([parse_dump],
                             input="\n".join(str(p) for p in files) + "\n",
                             capture_output=True, text=True, check=False)
        if res.returncode != 0:
            return ([f"seed {seed}: esc_parse_dump ended with "
                     f"{res.returncode}:\n{res.stderr[-3000:]}"], 0, 0)
        c = {}
        for line in res.stdout.splitlines():
            f = line.split("\t")
            c[f[0]] = (f[1], f[2] if len(f) > 2 else "")
        for n, path in enumerate(made):
            if path is None:
                continue
            try:
                py = ("OK", dump(g.check(path)))
            except (g.Bad, ValueError) as e:    # as main() takes them
                py = ("BAD", str(e))
            except Exception as e:  # noqa: BLE001 -- a crash is a finding
                py = ("CRASH", "%s: %s" % (type(e).__name__, e))
            cv = c.get(str(path), ("MISSING", ""))
            if py[0] == "OK" and cv[0] == "OK" and py[1] == cv[1]:
                both_ok += 1
                continue
            if py[0] == "BAD" and cv[0] == "BAD":
                both_bad += 1
                continue
            if py[0] == "OK" and cv[0] == "OK":
                kind = "both take it and read it differently"
            elif py[0] == "CRASH":
                kind = "the generator ends with an exception"
            else:
                kind = "generator %s, card reader %s" % (py[0], cv[0])
            problems.append(
                "seed %d case %d of %d (%s): %s\n  generator:   %s\n"
                "  card reader: %s\n  file: %s"
                % (seed, n, count, path.name, kind, py[1][:400],
                   cv[1][:400],
                   path.read_bytes()[:1500].decode("utf-8",
                                                   "backslashreplace")))
    return problems, both_ok, both_bad


def least(count):
    """The fewest of @count cases that reach each verdict in a run that
    compares something: 1 in 20, and 1 at least."""
    return max(1, count // 20)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("parse_dump", help="the esc_parse_dump binary")
    ap.add_argument("--seed", type=int, action="append",
                    help="a seed; may be given more than once "
                         "(default: %s)" % ", ".join(map(str, SEEDS)))
    ap.add_argument("--count", type=int, default=COUNT,
                    help="cases for each seed (default: %d)" % COUNT)
    args = ap.parse_args(argv)
    if args.count < 1:
        ap.error("--count: not 1 or more")
    # As the generator's main() does: a whole number of any length.
    if hasattr(sys, "set_int_max_str_digits"):
        sys.set_int_max_str_digits(0)
    failed = False
    for seed in args.seed or SEEDS:
        problems, both_ok, both_bad = run_seed(args.parse_dump, seed,
                                               args.count)
        print("seed %d: %d cases, both take %d, both refuse %d, "
              "differences %d" % (seed, args.count, both_ok, both_bad,
                                  len(problems)))
        for p in problems[:5]:
            print(p)
        if len(problems) > 5:
            print("... and %d more" % (len(problems) - 5))
        failed = failed or bool(problems)
        # A run in which nothing is taken, or nothing refused, compares
        # nothing: the cases have stopped reaching both verdicts.  So a
        # run of under 20 cases fails unless it has one of each.
        if both_ok < least(args.count) or both_bad < least(args.count):
            print("seed %d: too few cases reach one of the verdicts"
                  % seed)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
