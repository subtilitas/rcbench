#!/usr/bin/env python3
"""Hold every translated format string to the call that formats it.

A format from the string tables reaches snprintf() through a lookup --
`TR(ID)` on the screens, `S(ID)` in the servo report -- so the compiler's
-Wformat never sees it.  `test_text` holds each translation to its English;
this holds the English to the call site.

It compiles every C file of `shared/` once more with the lookups replaced by
the English literals, generated here from `shared/ui/include/ui_text.def`
and the servo report's table, under -Wformat=2 -Wformat-nonliteral, and
fails on any warning.  A format that is still not a literal after that is a
warning too, so a new lookup that bypasses the tables cannot slip past.

    tools/check_formats.py

Not covered: firmware/panel/main/main.c, which needs ESP-IDF to compile.
Its one translated format is ALERT_SUPPLY_TRIP.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHARED = ROOT / "shared"
DEF = SHARED / "ui" / "include" / "ui_text.def"
UI_TEXT_H = SHARED / "ui" / "include" / "ui_text.h"
REPORT = SHARED / "servo" / "servo_report.c"

FLAGS = ["-std=c11", "-fsyntax-only", "-Wformat=2", "-Wformat-nonliteral",
         "-Wformat-signedness"]

STRING = r'"(?:[^"\\]|\\.)*"'


def literals_from_def() -> str:
    """`#define TXS_<ID> "<English>"` for every table string."""
    out = ["#pragma once"]
    pat = re.compile(r'^UI_TEXT\((\w+),\s*\d+,\s*(%s)\)' % STRING, re.M)
    for ident, en in pat.findall(DEF.read_text(encoding="utf-8")):
        out.append("#define TXS_%s %s" % (ident, en))
    if len(out) < 2:
        sys.exit("no UI_TEXT entries in %s" % DEF)
    return "\n".join(out) + "\n"


def literals_from_report(text: str) -> str:
    """`#define SVS_<ID> <English>` for every entry of the report's k_str,
    its adjacent literals kept as they are."""
    m = re.search(r"k_str\[SERVO_STR_COUNT\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if m is None:
        sys.exit("no k_str in %s" % REPORT)
    out = []
    entry = re.compile(r"\[SERVO_STR_(\w+)\]\s*=\s*((?:\s*%s)+)\s*," % STRING)
    for ident, lits in entry.findall(m.group(1)):
        out.append("#define SVS_%s %s" % (ident, " ".join(lits.split("\n"))))
    if not out:
        sys.exit("no k_str entries in %s" % REPORT)
    return "\n".join(out) + "\n"


def main() -> int:
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc is None:
        sys.exit("no C compiler on PATH")
    with tempfile.TemporaryDirectory() as td:
        # A copy of shared/ with the two lookups swapped for literals: a
        # header is found next to the file that includes it first, so the
        # swap has to be in place rather than ahead on the include path.
        tree = Path(td) / "shared"
        shutil.copytree(SHARED, tree)
        header = tree / UI_TEXT_H.relative_to(SHARED)
        text = header.read_text(encoding="utf-8")
        tr = "#define TR(id) ui_tr(TX_##id)"
        if tr not in text:
            sys.exit("TR() is not defined as %r in %s" % (tr, UI_TEXT_H))
        (header.parent / "tx_lit.h").write_text(literals_from_def(),
                                                encoding="utf-8")
        header.write_text(
            text.replace(tr, '#include "tx_lit.h"\n#define TR(id) (TXS_##id)'),
            encoding="utf-8")

        report = tree / REPORT.relative_to(SHARED)
        text = report.read_text(encoding="utf-8")
        s_macro = "#define S(id) servo_str_in(c->text, SERVO_STR_##id)"
        if s_macro not in text:
            sys.exit("S() is not defined as %r in %s" % (s_macro, REPORT))
        report.write_text(text.replace(
            s_macro, literals_from_report(text) + "#define S(id) (SVS_##id)"),
            encoding="utf-8")

        includes = []
        for d in sorted(tree.glob("*/include")):
            includes += ["-I", str(d)]
        sources = sorted(p for p in tree.rglob("*.c")
                         if not p.name.startswith("gfx_font"))
        failed = False
        for src in sources:
            run = subprocess.run([cc, *FLAGS, *includes, str(src)],
                                 capture_output=True, text=True)
            out = (run.stdout + run.stderr).strip()
            if run.returncode != 0 or "warning:" in out:
                failed = True
                print(out.replace(str(tree), "shared"))
        if failed:
            print("\na format string does not match its call", file=sys.stderr)
            return 1
        print("%d files: every format matches its arguments" % len(sources))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
