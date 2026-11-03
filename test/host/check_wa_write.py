#!/usr/bin/env python3
"""Run the Wi-Fi page's write.js over a document and a panel of node's own.

<folder>/page_en.html is the page test_wa_edit wrote, with its scripts as
the browser gets them: write.js (var RcbWr) and edit.js (var RcbEd) among
them.  check_wa_write.js runs both, with the buttons, the box and the requests
as scripted stand-ins: the write resent after the code question and only
once, an answer after CANCEL that sends nothing, a wrong code, no answer at
all, and UPLOAD's REPLACE, which reads /api/info and not the file.

Exit code 77 (skipped) when node is not on the PATH.

Usage: check_wa_write.py <folder>
"""

import pathlib
import shutil
import subprocess
import sys

HARNESS = pathlib.Path(__file__).resolve().parent / "check_wa_write.js"


def main(argv):
    """Run node on the page.

    Args:
        argv: The program's name and the folder.

    Returns:
        0 when every case holds, 1 on a difference, 2 for a wrong call,
        77 without node.
    """
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    node = shutil.which("node")
    if node is None:
        print("node not found; write check skipped")
        return 77
    page = pathlib.Path(argv[1]) / "page_en.html"
    if not page.is_file():
        print(f"{page}: missing", file=sys.stderr)
        return 1
    r = subprocess.run([node, str(HARNESS), str(page)],
                       capture_output=True, text=True, check=False)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
