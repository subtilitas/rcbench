// The node side of check_wa_edit.py: runs the page's edit.js on the
// fixtures test_wa_edit wrote and on its own cases of the save, and prints
// every difference.
//
// Usage: node check_wa_edit.js <page_en.html> <edit_fixtures.json>
//
// SPDX-License-Identifier: MIT
'use strict';
const fs = require('fs');
const vm = require('vm');
const [page, fixtures] = process.argv.slice(2);
const html = fs.readFileSync(page, 'utf8');
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)]
  .map(m => m[1]);
const edSrc = scripts.find(s => s.startsWith('var RcbEd'));
if (!edSrc) {
  console.error('the page lacks edit.js');
  process.exit(1);
}
const ctx = vm.createContext({ TextEncoder, TextDecoder, Promise });
vm.runInContext(edSrc + '\n;globalThis.RcbEd = RcbEd;', ctx);
const { RcbEd } = ctx;
const fx = JSON.parse(fs.readFileSync(fixtures, 'utf8'));
const bytes = h => Uint8Array.from(Buffer.from(h, 'hex'));
const str = h => Buffer.from(h, 'hex').toString('utf8');
const hexOf = b => Buffer.from(b).toString('hex');
const fails = [];
let n = 0;
function check(ok, what) {
  n++;
  if (!ok) {
    fails.push(what);
  }
}
const MAX = fx.limits.bytes;
const ENDS = fx.limits.ends;

// ------------------------------------------------- line ends and the BOM

for (const [hex, eol, bom, ends] of fx.files) {
  const b = bytes(hex);
  check(RcbEd.eolOf(b) === eol, `eol of ${hex}: ${RcbEd.eolOf(b)}, want ${eol}`);
  check(RcbEd.endsOf(b) === ends,
    `ends of ${hex}: ${RcbEd.endsOf(b)}, want ${ends}`);
  const d = RcbEd.open(b);
  check(d !== null, `${hex} does not open`);
  if (d === null) {
    continue;
  }
  check(d.bom === bom, `BOM of ${hex}: ${d.bom}, want ${bom}`);
  check(!/\r/.test(d.text), `${hex}: a CR left in the textarea's text`);
  // Saved as it opened: the same bytes; a mixed file with LF ends.
  const back = RcbEd.encode(d.text, d.eol, d.bom);
  const want = eol === 'mixed'
    ? hexOf(Buffer.from(str(hex).replace(/\r\n?/g, '\n'), 'utf8'))
    : hex;
  check(hexOf(back) === want, `${hex} saves as ${hexOf(back)}, want ${want}`);
  // A line typed in the textarea takes the file's own line end.
  if (eol !== 'mixed') {
    const typed = RcbEd.encode(d.text + 'z\n', d.eol, d.bom);
    const end = { lf: '0a', crlf: '0d0a', cr: '0d' }[eol];
    check(hexOf(typed) === hex + '7a' + end,
      `${hex} + "z" and Enter saves as ${hexOf(typed)}`);
  }
}
// Bytes the browser would not give back: the editor does not open them.
for (const hex of ['e40a', 'c3', '61ff62', 'efbbbfe40a']) {
  check(RcbEd.open(bytes(hex)) === null, `${hex} (not UTF-8) opens`);
}

// ---------------------------------------------------------- the limits

{
  const fit = (t, eol, bom) => RcbEd.fits(RcbEd.encode(t, eol, bom), MAX,
    ENDS);
  const x = k => 'x'.repeat(k);
  check(fit(x(MAX), 'lf', false) === '', `${MAX}B LF refused`);
  check(fit(x(MAX + 1), 'lf', false) === 'FULL', `${MAX + 1}B LF taken`);
  // A BOM and CR LF ends count: 3 + (MAX - 5) + 2 bytes.
  check(fit(x(MAX - 5) + '\n', 'crlf', true) === '',
    `${MAX}B with a BOM and CR LF refused`);
  check(fit(x(MAX - 4) + '\n', 'crlf', true) === 'FULL',
    `${MAX + 1}B with a BOM and CR LF taken`);
  check(fit('\n'.repeat(ENDS), 'crlf', false) === '',
    `${ENDS} CR LF ends refused`);
  check(fit('\n'.repeat(ENDS + 1), 'lf', false) === 'LINES',
    `${ENDS + 1} line ends taken`);
}

// ------------------------------------------------------ the KST checksum

for (const [hex, verdict] of fx.sum) {
  const got = RcbEd.sumLine(str(hex));
  check(got === verdict, `checksum line ${JSON.stringify(str(hex))}: ${got}`);
}
for (const [hex, want] of fx.kst) {
  const c = RcbEd.cutSum(str(hex));
  const got = hexOf(Buffer.from(c.text, 'utf8'));
  check(got === want, `KST cut of ${JSON.stringify(str(hex).slice(0, 40))}` +
    `: ${JSON.stringify(c.text)}`);
  check(c.cut === (want !== hex), `KST cut flag ${c.cut} for ${hex.slice(0, 20)}`);
}

// ------------------------------------------------------------- the save

// A PUT that answers @p answers in turn and keeps every call.
function putter(answers) {
  const calls = [];
  const put = (b, hdr) => {
    calls.push({ b: hexOf(b), hdr: Object.assign({}, hdr) });
    return Promise.resolve(answers[Math.min(calls.length, answers.length) - 1]);
  };
  return { put, calls };
}
const ok = (etag, note) => ({ ok: true, status: 200,
  j: { path: 'X', created: false, etag, note: note || '' } });
const k412 = { ok: false, status: 412,
  j: { error: 'CHANGED', text: 'Changed on the card since it was read.' } };
const session = over => Object.assign({ tag: '"0a1b2c3d"', eol: 'crlf',
  bom: true, base: 'one\ntwo\n', max: MAX, endsMax: ENDS }, over);

async function saves() {
  // 412 on a stale ETag: nothing taken, the text and the ETag kept, and
  // the PUT carried If-Match.
  {
    const ed = session();
    const p = putter([k412]);
    const r = await RcbEd.save(ed, 'one\ntwo\nthree', p.put);
    check(r.r === 'CHANGED', `412: ${r.r}`);
    check(p.calls.length === 1 && p.calls[0].hdr['If-Match'] === '"0a1b2c3d"',
      `412: PUT ${JSON.stringify(p.calls)}`);
    check(p.calls[0].b === 'efbbbf6f6e650d0a74776f0d0a7468726565',
      `412: body ${p.calls[0].b}`);
    check(ed.tag === '"0a1b2c3d"' && ed.base === 'one\ntwo\n',
      `412: the session moved: ${JSON.stringify(ed)}`);
    check(RcbEd.dirty(ed, 'one\ntwo\nthree'), '412: the text counts as saved');
  }
  // A save that writes takes the answer's ETag; the next sends it.
  {
    const ed = session();
    const p = putter([ok('deadbeef'), ok('01234567')]);
    const r = await RcbEd.save(ed, 'one\ntwo\n3', p.put);
    check(r.r === 'SAVED' && !r.kst && !r.esc && r.size === 14,
      `save: ${JSON.stringify(r)}`);
    check(ed.tag === '"deadbeef"' && ed.base === 'one\ntwo\n3',
      `save: session ${JSON.stringify(ed)}`);
    check(!RcbEd.dirty(ed, 'one\ntwo\n3'), 'save: still dirty');
    await RcbEd.save(ed, 'one\ntwo\n34', p.put);
    check(p.calls.length === 2 && p.calls[1].hdr['If-Match'] === '"deadbeef"',
      `save: second If-Match ${JSON.stringify(p.calls[1])}`);
  }
  // No ETag: nothing sent.  No change: nothing sent.
  {
    const p = putter([ok('deadbeef')]);
    const r1 = await RcbEd.save(session({ tag: '' }), 'new', p.put);
    const r2 = await RcbEd.save(session(), 'one\ntwo\n', p.put);
    check(r1.r === 'CHANGED' && r2.r === 'NONE' && p.calls.length === 0,
      `no tag ${r1.r}, no change ${r2.r}, ${p.calls.length} PUTs`);
  }
  // At the limit and one byte over: the first is sent, the second not.
  {
    const p = putter([ok('11111111')]);
    const ed = session({ eol: 'lf', bom: false, base: '' });
    const r = await RcbEd.save(ed, 'y'.repeat(MAX), p.put);
    check(r.r === 'SAVED' && p.calls.length === 1 &&
      p.calls[0].b.length === 2 * MAX, `limit: ${r.r}`);
    const r2 = await RcbEd.save(ed, 'y'.repeat(MAX + 1), p.put);
    check(r2.r === 'FULL' && p.calls.length === 1, `limit + 1: ${r2.r}`);
    const r3 = await RcbEd.save(ed, '\n'.repeat(ENDS + 1), p.put);
    check(r3.r === 'LINES' && p.calls.length === 1, `lines + 1: ${r3.r}`);
  }
  // A mixed file is saved with LF ends, and then counts as LF.
  {
    const p = putter([ok('22222222')]);
    const ed = session({ eol: 'mixed', bom: false });
    const r = await RcbEd.save(ed, 'one\ntwo\nx\n', p.put);
    check(r.r === 'SAVED' && r.mixed && ed.eol === 'lf' &&
      p.calls[0].b === '6f6e650a74776f0a780a', `mixed: ${JSON.stringify(r)}`);
  }
  // A KST file loses its checksum line, and says so; an ESC profile says
  // it is read at the next start.
  {
    const head = 'KST servo configuration, format 2\n';
    const p = putter([ok('33333333'), ok('44444444', 'ESC_NEXT_START')]);
    const ed = session({ eol: 'lf', bom: false,
      base: head + 'Type: V6\nchecksum 0a0b0c0d\n' });
    const r = await RcbEd.save(ed, head + '# edit\nType: V6\nchecksum 0a0b0c0d\n',
      p.put);
    check(r.r === 'SAVED' && r.kst && r.text === head + '# edit\nType: V6\n' &&
      ed.base === r.text && str(p.calls[0].b) === r.text,
      `KST: ${JSON.stringify(r)}`);
    const esc = session({ eol: 'lf', bom: false, base: '{}' });
    const r2 = await RcbEd.save(esc, '{"a":1}', p.put);
    check(r2.r === 'SAVED' && r2.esc && !r2.kst, `ESC: ${JSON.stringify(r2)}`);
  }
  // Another refusal keeps the session and hands the answer on.
  {
    const p = putter([{ ok: false, status: 423,
      j: { error: 'BUSY', text: 'busy' } }]);
    const ed = session();
    const r = await RcbEd.save(ed, 'z', p.put);
    check(r.r === 'REFUSED' && r.a.j.error === 'BUSY' &&
      ed.tag === '"0a1b2c3d"', `refused: ${JSON.stringify(r)}`);
  }
}

// ------------------------------------------------- the access code's answer

{
  const a = r => RcbEd.codeAnswer(r);
  check(a({ type: 'opaqueredirect', status: 0 }) === 'OK', 'code: 303 not OK');
  check(a({ type: 'basic', status: 303 }) === 'OK', 'code: 303 status not OK');
  check(a({ type: 'basic', status: 200, redirected: true }) === 'OK',
    'code: followed redirect not OK');
  check(a({ type: 'basic', status: 200 }) === 'WRONG', 'code: page not WRONG');
  check(a({ type: 'basic', status: 503 }) === 'CLOSED', 'code: 503 not CLOSED');
}

// ---------------------------------------------------- the unsaved question

{
  const ed = session();
  const q = (e, t, saving, asking) => RcbEd.onClose(e, t, saving, asking);
  check(q(null, '', false, false) === 'close', 'no editor: not closed');
  check(q(ed, 'one\ntwo\n', false, false) === 'close', 'no changes: asked');
  check(q(ed, 'one\ntwo\nx', false, false) === 'ask', 'changes: not asked');
  check(q(ed, 'one\ntwo\nx', true, false) === 'wait', 'saving: not waited');
  check(q(ed, 'one\ntwo\nx', false, true) === 'stay',
    'question open: not closed as STAY');
}

saves().then(() => {
  if (fails.length > 0) {
    console.error(fails.slice(0, 20).join('\n'));
    console.error(`${fails.length} of ${n} cases differ`);
    process.exit(1);
  }
  console.log(`${n} cases: edit.js agrees with the panel`);
}, e => {
  console.error(e.stack || String(e));
  process.exit(1);
});
