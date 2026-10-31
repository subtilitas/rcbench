#!/usr/bin/env python3
"""Run the Wi-Fi page's editor on the fixtures test_wa_edit wrote.

<folder>/page_en.html is the page with its scripts as the browser gets
them, edit.js (var RcbEd) among them.  <folder>/edit_fixtures.json holds
what the C side gives:

- limits: the bytes and the line ends a file may have (text_buf.h);
- files: [bytes as hex, the X-Rcbench-Eol word, the BOM, the line ends],
  each as the panel's text buffer reads the bytes;
- sum: [line as hex, kst_file_checksum_line()'s verdict];
- kst: [a KST file edited, as hex; the text a SAVE writes, as hex], the
  second read by kst_file_parse() as EDITED; a last pair without the KST
  header, which keeps every line.

node runs the page's edit.js (check_wa_edit.js) in a context of its own,
with no document, on those and on its own cases of the save: If-Match on
every PUT, the 412 that keeps the text and the ETag, the limits at their
edge, the KST and ESC notes, the question on CLOSE, and OVERWRITE: its 2s
hold, its ETag and the refusals at a third change, a file deleted and a
failed write.  Every difference
is a failure; the first 20 are printed.

Exit code 77 (skipped) when node is not on the PATH.

Usage: check_wa_edit.py <folder>
"""

import json
import pathlib
import re
import shutil
import subprocess
import sys

HARNESS = pathlib.Path(__file__).resolve().parent / "check_wa_edit.js"


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
        print("node not found; editor check skipped")
        return 77
    folder = pathlib.Path(argv[1])
    page = folder / "page_en.html"
    fixtures = folder / "edit_fixtures.json"
    for f in (page, fixtures):
        if not f.is_file():
            print(f"{f}: missing", file=sys.stderr)
            return 1
    json.loads(fixtures.read_text(encoding="utf-8"))
    if not re.search(r"<script>var RcbEd", page.read_text(encoding="utf-8")):
        print(f"{page}: no edit.js", file=sys.stderr)
        return 1
    r = subprocess.run([node, str(HARNESS), str(page), str(fixtures)],
                       capture_output=True, text=True, check=False)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
