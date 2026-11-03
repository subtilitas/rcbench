// The node side of check_wa_hl.py: runs the page's hl.js and app.js on
// the fixtures test_wa_hl wrote and prints every difference.
//
// Usage: node check_wa_hl.js <page_en.html> <hl_fixtures.json>
//
// SPDX-License-Identifier: MIT
'use strict';
const fs = require('fs');
const vm = require('vm');
const [page, fixtures] = process.argv.slice(2);
const html = fs.readFileSync(page, 'utf8');
const DATA = /<script type="application\/json" id="fm-data">([\s\S]*?)<\/script>/;
const data = JSON.parse(DATA.exec(html)[1]);
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)]
  .map(m => m[1]);
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
