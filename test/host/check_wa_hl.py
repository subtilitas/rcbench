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

node runs both scripts in a context of their own, with no document, and
compares.  Every difference is a failure; the first 20 are printed.

Exit code 77 (skipped) when node is not on the PATH.

Usage: check_wa_hl.py <folder>
"""

import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HARNESS = r"""
'use strict';
const fs = require('fs');
const vm = require('vm');
const [page, fixtures] = process.argv.slice(2);
const html = fs.readFileSync(page, 'utf8');
const data = JSON.parse(/<script type="application\/json" id="fm-data">([\s\S]*?)<\/script>/.exec(html)[1]);
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const hlSrc = scripts.find(s => s.startsWith('var RcbHl'));
const appSrc = scripts.find(s => s.startsWith('var RcbFm'));
if (!hlSrc || !appSrc) {
  console.error('the page lacks hl.js or app.js');
  process.exit(1);
}
const ctx = vm.createContext({ TextEncoder, TextDecoder });
vm.runInContext(hlSrc + '\n;globalThis.RcbHl = RcbHl;', ctx);
vm.runInContext(appSrc + '\n;globalThis.RcbFm = RcbFm;', ctx);
const { RcbHl, RcbFm } = ctx;
const fx = JSON.parse(fs.readFileSync(fixtures, 'utf8'));
const bytes = h => Uint8Array.from(Buffer.from(h, 'hex'));
const text = h => Buffer.from(h, 'hex').toString('utf8');
const fails = [];
const t = RcbHl.tables(data.hl);
const cap = t.cap;
for (const [kind, no, room, hex, want] of fx.lines) {
  t.cap = room;
  const got = RcbHl.line(t, bytes(hex), no, kind);
  if (JSON.stringify(got) !== JSON.stringify(want)) {
    fails.push(`line ${JSON.stringify(text(hex))} kind ${kind} no ${no} ` +
               `room ${room}: ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
  }
}
t.cap = cap;
for (const [hex, want] of fx.kinds) {
  const got = RcbHl.kindOf(text(hex));
  if (got !== want) {
    fails.push(`kind of ${JSON.stringify(text(hex))}: ${got}, want ${want}`);
  }
}
for (const [hex, want] of fx.files) {
  const L = RcbHl.lines(bytes(hex));
  const got = L.s.map((s, i) => [s, L.e[i]]);
  if (JSON.stringify(got) !== JSON.stringify(want)) {
    fails.push(`lines of ${hex}: ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
  }
}
for (const [n, want] of fx.sizes) {
  const got = RcbFm.sizeText(n);
  if (got !== want) {
    fails.push(`size ${n}: ${got}, want ${want}`);
  }
}
for (const hex of fx.paths) {
  const p = text(hex);
  const enc = RcbFm.encPath(p);
  const back = enc.split('/').map(decodeURIComponent).join('/');
  if (back !== p || /%2F/i.test(enc) || /[^A-Za-z0-9%/._~!'()*-]/.test(enc)) {
    fails.push(`path ${JSON.stringify(p)} encodes as ${enc}`);
  }
}
const n = fx.lines.length + fx.kinds.length + fx.files.length +
  fx.sizes.length + fx.paths.length;
if (fails.length > 0) {
  console.error(fails.slice(0, 20).join('\n'));
  console.error(`${fails.length} of ${n} cases differ`);
  process.exit(1);
}
console.log(`${n} cases: hl.js and app.js agree with the C side`);
"""


def main(argv):
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
    with tempfile.TemporaryDirectory() as tmp:
        harness = pathlib.Path(tmp) / "harness.js"
        harness.write_text(HARNESS, encoding="utf-8")
        r = subprocess.run([node, str(harness), str(page), str(fixtures)],
                           capture_output=True, text=True, check=False)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
