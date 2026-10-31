#!/usr/bin/env python3
"""Hand the script of each page test_wa_page wrote to node.

Each <folder>/page_<lang>.html holds the page's <script> elements, and a
page with none fails.  Each body is written to a temporary file and checked
with `node --check`, which parses it without running it: a text that ends
a string early, or a "</script>" inside a text, fails here as it fails in a
browser.

The file manager's data element (id "fm-data") is read as JSON, and every
text the file manager's scripts (shared/webaccess/web/*.js) read by their
key is in it: each literal key, a "k." key per kind word and an "r." key
per place word the file API writes (shared/webaccess/wa_api.c).  These
checks run without node.

Exit code 77 (skipped) when node is not on the PATH and the data checks
pass.

Usage: check_wa_page.py <folder>
"""

import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
WEB = ROOT / "shared/webaccess/web"
WA_API = ROOT / "shared/webaccess/wa_api.c"


def data_of(html):
    """The file manager's data element as JSON; None when there is none."""
    m = re.search(r'<script type="application/json" id="fm-data">(.*?)'
                  r"</script>", html, re.DOTALL)
    return None if m is None else json.loads(m.group(1))


def keys_read(js, api):
    """Every text key the scripts read.

    The literal ones, and the kind and place words the API writes behind
    "k." and "r.".
    """
    keys = set(re.findall(r"\bTX\.([A-Za-z_]\w*)", js))
    keys |= set(re.findall(r"\bTX\['([^']+)'\]", js))
    keys |= {"k." + w for w in re.findall(r'\[FM_KIND_\w+\] = "(\w+)"',
                                          api)}
    keys |= {"r." + w for w in re.findall(r'\[FM_PLACE_\w+\] = "(\w+)"',
                                          api)}
    return keys


def check_data(page, js, api):
    """The failures of one page's data: [] when it holds every key."""
    try:
        data = data_of(page.read_text(encoding="utf-8"))
    except ValueError as e:
        return [f"{page.name}: the data is no JSON: {e}"]
    if data is None:
        return [f"{page.name}: no data element"]
    missing = sorted(keys_read(js, api) - set(data["tx"]))
    return [f"{page.name}: no text {k!r}" for k in missing]


def scripts_of(html):
    """The body of every script element, each ended as a browser ends it."""
    return re.findall(r"<script>(.*?)</script>", html, re.DOTALL)


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    pages = sorted(pathlib.Path(argv[1]).glob("page_*.html"))
    if len(pages) != 2:
        print(f"{argv[1]}: {len(pages)} pages, want 2", file=sys.stderr)
        return 1
    js = "\n".join(f.read_text(encoding="utf-8")
                   for f in sorted(WEB.glob("*.js")))
    api = WA_API.read_text(encoding="utf-8")
    bad = [f for page in pages for f in check_data(page, js, api)]
    if bad:
        print("\n".join(bad), file=sys.stderr)
        return 1
    print(f"{len(keys_read(js, api))} texts the app reads, in both pages")
    node = shutil.which("node")
    if node is None:
        print("node not found; script check skipped")
        return 77
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
