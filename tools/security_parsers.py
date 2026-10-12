#!/usr/bin/env python3
"""Hold the parser table in SECURITY.md to the code tree.

A module under shared/ or protocols/ parses input somebody else produced when
its public header (include/*.h) declares a function whose name ends in
`parse`, `decode`, `unpack`, `_rx` or `_feed` (also with a suffix after
`parse`, `decode` or `unpack`).  The table in SECURITY.md, "Input parsing",
names one module per row, or several joined with "and", in the first column.

The tool fails when

- a module the headers show as a parser is neither in the table nor in
  NOT_UNTRUSTED below, which gives the reason;
- the table names a module that is not a parser by the headers;
- NOT_UNTRUSTED names a module that is no longer a candidate or is also in
  the table.

    python3 tools/security_parsers.py           # print the derived modules
    python3 tools/security_parsers.py --check   # fail on drift

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
ROOTS = ("shared", "protocols")
SECURITY = REPO / "SECURITY.md"

# A declaration at the start of a line, then the name and its opening
# parenthesis.  Comment lines start with `*` or `/` and do not match.
NAME = re.compile(
    r"^[A-Za-z_][\w \t\*]*?\b(\w*_(?:parse\w*|decode\w*|unpack\w*|rx|feed))\(",
    re.M)

# Modules with a matching function whose input is not somebody else's file
# or frame.  Each reason is checked only for the module being listed.
NOT_UNTRUSTED = {
    "shared/bench": "knob words from the bench's own sensor and the KST "
                    "programmer's session; card text goes through "
                    "protocols/kst",
    "shared/gfx": "unpacks a colour value the firmware built itself",
    "shared/outputs": "reads the page the board's own link code stored",
    "shared/sense": "samples from the bench's own scheduler",
    "shared/ui": "calls into shared/bench and protocols/kst with text "
                 "those modules check",
}


def candidates(root: pathlib.Path = REPO) -> set[str]:
    """Modules whose public headers declare a parse-like function."""
    found = set()
    for top in ROOTS:
        for header in sorted((root / top).glob("*/include/*.h")):
            if NAME.search(header.read_text(errors="replace")):
                found.add(f"{top}/{header.parent.parent.name}")
    return found


def table(text: str) -> set[str]:
    """Module paths in the first column of the 'Input parsing' table."""
    m = re.search(r"^## Input parsing\n(.*?)(?=^## )", text, re.M | re.S)
    if not m:
        return set()
    rows = [ln for ln in m.group(1).splitlines() if ln.startswith("|")]
    names = set()
    for row in rows[2:]:
        first = row.split("|")[1]
        names.update(re.findall(r"`((?:shared|protocols)/[a-z0-9_]+)`", first))
    return names


def problems(root: pathlib.Path = REPO) -> list[str]:
    found = candidates(root)
    listed = table((root / "SECURITY.md").read_text())
    out = []
    for mod in sorted(found - listed - set(NOT_UNTRUSTED)):
        out.append(f"{mod} declares a parse function and is neither in the "
                   f"SECURITY.md table nor in NOT_UNTRUSTED")
    for mod in sorted(listed - found):
        out.append(f"SECURITY.md names {mod}, which declares no parse "
                   f"function")
    for mod in sorted(set(NOT_UNTRUSTED) - found):
        out.append(f"NOT_UNTRUSTED names {mod}, which is no candidate")
    for mod in sorted(set(NOT_UNTRUSTED) & listed):
        out.append(f"{mod} is in the SECURITY.md table and in NOT_UNTRUSTED")
    return out


def main(argv: list[str]) -> int:
    if "--check" not in argv:
        for mod in sorted(candidates() - set(NOT_UNTRUSTED)):
            print(mod)
        return 0
    found = problems()
    for line in found:
        print(f"security_parsers: {line}", file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
