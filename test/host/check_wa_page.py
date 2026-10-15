#!/usr/bin/env python3
"""Hand the script of each page test_wa_page wrote to node.

Each <folder>/page_<lang>.html holds the page's <script> elements, and a
page with none fails.  Each body is written to a temporary file and checked
with `node --check`, which parses it without running it: a text that ends
a string early, or a "</script>" inside a text, fails here as it fails in a
browser.

Exit code 77 (skipped) when node is not on the PATH.

Usage: check_wa_page.py <folder>
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


def scripts_of(html):
    """The body of every script element, each ended as a browser ends it."""
    return re.findall(r"<script>(.*?)</script>", html, re.DOTALL)


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    node = shutil.which("node")
    if node is None:
        print("node not found; script check skipped")
        return 77
    pages = sorted(pathlib.Path(argv[1]).glob("page_*.html"))
    if len(pages) != 2:
        print(f"{argv[1]}: {len(pages)} pages, want 2", file=sys.stderr)
        return 1
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        for page in pages:
            bodies = scripts_of(page.read_text(encoding="utf-8"))
            if not bodies:
                print(f"{page.name}: no script element", file=sys.stderr)
                failed = True
                continue
            for i, js in enumerate(bodies):
                out = pathlib.Path(tmp) / f"{page.stem}_{i}.js"
                out.write_text(js, encoding="utf-8")
                r = subprocess.run([node, "--check", str(out)],
                                   capture_output=True, text=True,
                                   check=False)
                if r.returncode != 0:
                    print(f"{page.name}: script {i} fails node --check\n"
                          f"{r.stderr}", file=sys.stderr)
                    failed = True
                else:
                    print(f"{page.name}: script {i} parses")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
