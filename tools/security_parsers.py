#!/usr/bin/env python3
"""Hold the parser table in SECURITY.md to the code tree.

A module under shared/ or protocols/ parses input somebody else produced when
its public header (include/*.h) declares a function whose name ends in
`parse`, `decode`, `unpack`, `_rx`, `_feed`, `_write`, `_read`, `_done` or
`_utf8_next` (also with a suffix after `parse`, `decode` or `unpack`).  The
`_write`, `_read` and `_done` names find the page handlers that take link
frames, flash records and card results; `_utf8_next` finds the UTF-8 decoder.
FORCED below adds the modules that take outside input through functions
the name rule cannot see, each with the reason.  The table in SECURITY.md,
"Input parsing", holds the name-rule candidates plus the forced ones: one
module per row, or several joined with "and", in the first column.

The tool fails when

- a candidate is neither in the table nor in NOT_UNTRUSTED below, which
  gives the reason;
- a forced module is not in the table;
- the table names a module that is neither a candidate nor forced;
- NOT_UNTRUSTED names a module that is no longer a candidate or is also in
  the table;
- FORCED names a module that does not exist or that the name rule finds
  already.

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
    r"^[A-Za-z_][\w \t\*]*?\b(\w*_(?:parse\w*|decode\w*|unpack\w*"
    r"|rx|feed|write|read|done|utf8_next))\(",
    re.M)

# Modules with a matching function whose input is not somebody else's file
# or frame.  Each reason is checked only for the module being listed.  Empty:
# every candidate module has a row in the SECURITY.md table.
NOT_UNTRUSTED: dict[str, str] = {}

# Modules with outside input whose public functions the name rule does not
# match.  Each reason names the input.
FORCED: dict[str, str] = {
    "shared/settings": "settings values and texts loaded from NVS "
                       "(settings_store_t.load and load_text)",
    "shared/touch": "the GT911 point list turned into touch events "
                    "(touch_tracker_update)",
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
    named = candidates(root)
    forced = set(FORCED)
    found = named | forced
    listed = table((root / "SECURITY.md").read_text())
    out = []
    for mod in sorted(named - listed - set(NOT_UNTRUSTED)):
        out.append(f"{mod} declares a parse function and is neither in the "
                   f"SECURITY.md table nor in NOT_UNTRUSTED")
    for mod in sorted(forced - named - listed):
        out.append(f"{mod} is in FORCED and not in the SECURITY.md table")
    for mod in sorted(listed - found):
        out.append(f"SECURITY.md names {mod}, which declares no parse "
                   f"function and is not in FORCED")
    for mod in sorted(set(NOT_UNTRUSTED) - named):
        out.append(f"NOT_UNTRUSTED names {mod}, which is no candidate")
    for mod in sorted(set(NOT_UNTRUSTED) & listed):
        out.append(f"{mod} is in the SECURITY.md table and in NOT_UNTRUSTED")
    for mod in sorted(forced):
        if not (root / mod).is_dir():
            out.append(f"FORCED names {mod}, which does not exist")
        elif mod in named:
            out.append(f"FORCED names {mod}, which the name rule finds")
    return out


def main(argv: list[str]) -> int:
    if "--check" not in argv:
        modules = (candidates() | set(FORCED)) - set(NOT_UNTRUSTED)
        for mod in sorted(modules):
            print(mod)
        return 0
    found = problems()
    for line in found:
        print(f"security_parsers: {line}", file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
