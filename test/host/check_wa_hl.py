#!/usr/bin/env python3
"""Run the Wi-Fi page's highlighter on the fixtures test_wa_hl wrote.

<folder>/page_en.html is the page with the panel's highlighter: its data
element and its two scripts, hl.js (var RcbHl) and app.js (var RcbFm), as
the browser gets them.  <folder>/hl_fixtures.json holds what the C side
gives:

- lines: [kind, line number, span room, bytes as hex, text_hl_line()'s
  spans as [start, length, class]];
- kinds: [name as hex, text_hl_kind_of()];
- files: [bytes as hex, the viewer's lines as [first byte, end]];
- sizes: [bytes, files_size_text()];
- paths: names as hex, each of which RcbFm.encPath() encodes and
  decodeURIComponent() reads back.

node runs both scripts (check_wa_hl.js) in a context of their own, with
no document, and compares.  Every difference is a failure; the first 20
are printed.

Exit code 77 (skipped) when node is not on the PATH.

Usage: check_wa_hl.py <folder>
"""

import json
import pathlib
import re
import shutil
import subprocess
import sys

HARNESS = pathlib.Path(__file__).resolve().parent / "check_wa_hl.js"


def main(argv):
    """Run node on the page and the fixtures.

    Args:
        argv: The program's name and the folder.

    Returns:
        0 when every case agrees, 1 on a difference, 2 for a wrong call,
        77 without node.
    """
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    node = shutil.which("node")
    if node is None:
        print("node not found; highlighter check skipped")
        return 77
    folder = pathlib.Path(argv[1])
    page = folder / "page_en.html"
    fixtures = folder / "hl_fixtures.json"
    for f in (page, fixtures):
        if not f.is_file():
            print(f"{f}: missing", file=sys.stderr)
            return 1
    # The fixture file is JSON before node reads it.
    json.loads(fixtures.read_text(encoding="utf-8"))
    if not re.search(r"<script>var RcbHl", page.read_text(encoding="utf-8")):
        print(f"{page}: no hl.js", file=sys.stderr)
        return 1
    r = subprocess.run([node, str(HARNESS), str(page), str(fixtures)],
                       capture_output=True, text=True, check=False)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
