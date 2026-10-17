#!/usr/bin/env python3
"""Hold every translated format string to the call that formats it.

A format from the string tables reaches snprintf() through a lookup --
`TR(ID)` on the screens, `S(ID)` in the servo report and `LS(ID)` in the
limit run's report -- so the compiler's -Wformat never sees it.
`test_text` holds each translation to its English; this holds the English
to the call site.

It compiles every C file of `shared/` once more with the lookups replaced by
the English literals, generated here from `shared/ui/include/ui_text.def`
and the servo test's table in `shared/servo/servo_str.c`, under -Wformat=2
-Wformat-nonliteral, and fails on any warning.  A format that is still not
a literal after that is a warning too, so a new lookup that bypasses the
tables cannot slip past.

    tools/check_formats.py

Not covered: firmware/panel/main/, which needs ESP-IDF to compile.  Its
translated formats are ALERT_SUPPLY_TRIP and ALERT_PDMINI_SAG in main.c and
ALERT_SENSE_SAME_ADDR, ALERT_SENSE_PINS, ALERT_SENSE_SILENT,
ALERT_SENSE_SILENT_FOUND, ALERT_SENSE_WRONG and ALERT_SENSE_I3221_CLIPPED in
panel_services.c.

SPDX-License-Identifier: MIT
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
PROTOCOLS = ROOT / "protocols"
DEF = SHARED / "ui" / "include" / "ui_text.def"
UI_TEXT_H = SHARED / "ui" / "include" / "ui_text.h"
# The servo test's table of words, k_str, and the files that look its
# strings up, each with its macro: the automatic test's report and the limit
# run's.
SERVO_STR = SHARED / "servo" / "servo_str.c"
LOOKUPS = ((SHARED / "servo" / "servo_report.c", "S"),
           (SHARED / "servo" / "servo_limrun_report.c", "LS"))

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
        sys.exit("no k_str in %s" % SERVO_STR)
    out = []
    entry = re.compile(r"\[SERVO_STR_(\w+)\]\s*=\s*((?:\s*%s)+)\s*," % STRING)
    for ident, lits in entry.findall(m.group(1)):
        out.append("#define SVS_%s %s" % (ident, " ".join(lits.split("\n"))))
    if not out:
        sys.exit("no k_str entries in %s" % SERVO_STR)
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

        svs = literals_from_report(SERVO_STR.read_text(encoding="utf-8"))
        for path, name in LOOKUPS:
            copy = tree / path.relative_to(SHARED)
            body = copy.read_text(encoding="utf-8")
            macro = ("#define %s(id) servo_str_in(c->text, SERVO_STR_##id)"
                     % name)
            if macro not in body:
                sys.exit("%s() is not defined as %r in %s"
                         % (name, macro, path))
            copy.write_text(body.replace(
                macro, svs + "#define %s(id) (SVS_##id)" % name),
                encoding="utf-8")

        includes = []
        for d in sorted(tree.glob("*/include")):
            includes += ["-I", str(d)]
        # The protocol modules' headers, which shared/ includes: read where
        # they are, since no lookup is swapped in them.
        for d in sorted(PROTOCOLS.glob("*/include")):
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
