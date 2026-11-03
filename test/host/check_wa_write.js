// The node side of check_wa_write.py: runs the page's write.js and edit.js
// with the box, the buttons and the requests as scripted stand-ins, and
// prints every difference.
//
// Usage: node check_wa_write.js <page_en.html>
//
// SPDX-License-Identifier: MIT
'use strict';
const fs = require('fs');
const vm = require('vm');
const [page] = process.argv.slice(2);
const html = fs.readFileSync(page, 'utf8');
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)]
  .map(m => m[1]);
const wrSrc = scripts.find(s => s.startsWith('var RcbWr'));
const edSrc = scripts.find(s => s.startsWith('var RcbEd'));
if (!wrSrc || !edSrc) {
  console.error('the page lacks write.js or edit.js');
  process.exit(1);
}
const fails = [];
let n = 0;
function check(ok, what) {
  n++;
  if (!ok) {
    fails.push(what);
  }
}

// ---------------------------------------------------------- the stand-ins

class El {
  constructor(tag, cls, text) {
    this.tag = tag;
    this.className = cls || '';
    this.kids = [];
    this.on = {};
    this.hidden = false;
    this.disabled = false;
    this.value = '';
    this._text = text || '';
    this.parentNode = null;
  }
  get textContent() {
    return this._text + this.kids.map(k => k.textContent).join(' ');
  }
  set textContent(t) {
    this._text = t;
    this.kids = [];
  }
  appendChild(k) {
    k.parentNode = this;
    this.kids.push(k);
    return k;
  }
  insertBefore(k) {
    return this.appendChild(k);
  }
  addEventListener(k, f) {
    this.on[k] = f;
  }
  setAttribute() {}
  focus() {}
  select() {}
  click() {
    if (this.on.click) {
      this.on.click({});
    }
  }
}

// One panel for one case: answers each request in order from @p api, which
// a case fills, and keeps every request.
function rig(mode) {
  const r = { xhr: [], api: [], code: [], msgs: [], rescans: 0, hold: null };
  r.answer = () => r.api.shift() ||
    { status: 500, body: '{"error":"IO","text":"no answer scripted"}' };
  class Xhr {
    constructor() {
      this.h = {};
      this.upload = {};
    }
    open(m, u) {
      this.m = m;
      this.u = u;
    }
    setRequestHeader(k, v) {
      this.h[k] = v;
    }
    getResponseHeader() {
      return null;
    }
    send(b) {
      r.xhr.push({ m: this.m, u: this.u, h: this.h, b });
      const a = r.answer();
      this.status = a.status;
      this.responseText = a.body;
      Promise.resolve().then(() => this.onload());
    }
  }
  const fetchFn = (u, o) => {
    r.code.push({ u, body: o.body, redirect: o.redirect });
    return new Promise((done, bad) => {
      r.hold = { done, bad };
    });
  };
  const ctx = vm.createContext({ Promise, TextEncoder, TextDecoder,
    XMLHttpRequest: Xhr, fetch: fetchFn, setTimeout, clearTimeout,
    encodeURIComponent, JSON, Object,
    document: { addEventListener() {} },
    window: { addEventListener() {} } });
  vm.runInContext(`${edSrc}\n${wrSrc}\n` +
    ';globalThis.RcbEd = RcbEd; globalThis.RcbWr = RcbWr;', ctx);
  const TX = new Proxy({}, { get: (t, k) => String(k) });
  const entry = { n: 'A.TXT', d: false, p: 'FREE', k: 'TEXT' };
  const panes = [
    { path: 'USER', list: { mount: 3, entries: [entry] } },
    { path: 'OTHER', list: { mount: 3, entries: [] } }];
  const foot = new El('div');
  const parent = new El('div');
  parent.appendChild(foot);
  const c = {
    F: { join: (a, b) => (a ? `${a}/${b}` : b), encPath: s => s,
      fmt: (f, ...a) => [f, ...a].join(' ') },
    TX,
    st: { active: 0, panes, mount: 3,
      status: { writes: 'open', file_max: 1048576, mode } },
    selected: () => (r.selection === null ? null : { e: entry, p: panes[0] }),
    queue: f => f(),
    msg: t => r.msgs.push(t),
    rescan: () => {
      r.rescans++;
      return Promise.resolve();
    },
    button: (t, k, fn) => {
      const b = new El('button', k, t);
      b.on.click = fn;
      return b;
    },
    el: (t, k, x) => new El(t, k, x),
    root: new El('div'),
    ui: { foot },
  };
  r.c = c;
  r.wr = ctx.RcbWr.attach(c);
  // The box is the root's last child; its parts by what they are.
  const box = c.root.kids[c.root.kids.length - 1];
  const card = box.kids[0];
  r.box = box;
  r.title = card.kids[0];
  r.body = card.kids[1];
  r.input = card.kids[2];
  r.form = card.kids[3];
  r.codeIn = r.form.kids[0];
  r.codeGo = r.form.kids[1];
  r.err = card.kids[5];
  const btns = card.kids[6];
  r.go = btns.kids[0];
  r.cancel = btns.kids[1];
  const row = parent.kids[parent.kids.length - 1];
  r.button = i => row.kids[i];
  r.file = row.kids[6];
  r.sendCode = v => {
    r.codeIn.value = v;
    r.form.on.submit({ preventDefault() {} });
  };
  return r;
}
const tick = async () => {
  for (let i = 0; i < 30; ++i) {
    await new Promise(setImmediate);
  }
};
const js = (status, o) => ({ status, body: JSON.stringify(o) });
const CODE = js(403, { error: 'WRITES_CODE', text: 'needs the code' });
const DONE = js(201, { path: 'X', created: true, note: '' });
const TAKEN = { type: 'opaqueredirect', status: 0 };
const WRONG = { type: 'basic', status: 200 };
const BTN = { upload: 0, newdir: 1, newfile: 2, rename: 3, move: 4, del: 5 };
const FILE = { name: 'A.TXT', size: 2048 };

// Press a write's button and bring it to the code question.
async function toCode(r, what) {
  r.api.push(CODE);
  if (what === 'upload') {
    r.file.files = [FILE];
    r.file.on.change();
  } else {
    r.button(BTN[what]).click();
    if (what === 'newdir' || what === 'newfile') {
      r.input.value = 'NEW1';
      r.go.click();
    } else if (what === 'rename') {
      r.input.value = 'B.TXT';
      r.go.click();
    } else if (what === 'del') {
      r.go.click();
    }
  }
  await tick();
  check(!r.box.hidden && !r.form.hidden && r.go.hidden,
    `${what}: the code form does not show`);
  check(r.xhr.length === 1, `${what}: ${r.xhr.length} writes before the code`);
}

// ----------------------------------------------------------------- cases

async function eachWriteGoesOutAgainOnce() {
  for (const what of Object.keys(BTN)) {
    const r = rig('CODE');
    await toCode(r, what);
    const first = r.xhr[0];
    r.api.push(DONE);
    r.sendCode('abcd2345');
    check(r.code.length === 1 && r.code[0].body === 'code=abcd2345' &&
      r.code[0].redirect === 'manual' && r.code[0].u === '/code',
    `${what}: code sent as ${JSON.stringify(r.code)}`);
    check(r.codeGo.disabled, `${what}: SEND CODE takes a second tap`);
    await tick();
    check(r.xhr.length === 1, `${what}: a write before the answer`);
    r.hold.done(TAKEN);
    await tick();
    check(r.xhr.length === 2, `${what}: ${r.xhr.length} writes after the code`);
    const a = r.xhr[1] || {};
    check(a.m === first.m && a.u === first.u && a.b === first.b &&
      JSON.stringify(a.h) === JSON.stringify(first.h),
    `${what}: sent again as ${JSON.stringify(a)}, was ${JSON.stringify(first)}`);
    check(r.box.hidden, `${what}: the box stays after the write`);
    check(r.rescans === 1, `${what}: ${r.rescans} rescans`);
    check(!r.codeGo.disabled, `${what}: SEND CODE stays dimmed`);
  }
}

async function aLateAnswerAfterCancelDoesNothing() {
  for (const what of Object.keys(BTN)) {
    for (const answer of [TAKEN, WRONG, null]) {
      const r = rig('CODE');
      await toCode(r, what);
      r.sendCode('abcd2345');
      r.cancel.click();
      check(r.box.hidden, `${what}: CANCEL leaves the box`);
      const msgs = r.msgs.length;
      if (answer) {
        r.hold.done(answer);
      } else {
        r.hold.bad(new Error('gone'));
      }
      await tick();
      check(r.xhr.length === 1, `${what}: late answer wrote`);
      check(r.box.hidden, `${what}: late answer opened the box`);
      check(r.err.textContent === '', `${what}: late answer said something`);
      check(r.msgs.length === msgs, `${what}: late answer left a message`);
      check(!r.codeGo.disabled, `${what}: SEND CODE stays dimmed`);
    }
  }
  // Another write's box opened in the meantime: it is not answered either,
  // and its own code goes on.
  const r = rig('CODE');
  await toCode(r, 'newdir');
  r.sendCode('abcd2345');
  const late = r.hold;
  r.cancel.click();
  r.button(BTN.newfile).click();
  r.input.value = 'NEW2';
  r.api.push(CODE);
  r.go.click();
  await tick();
  r.sendCode('abcd2345');
  const own = r.hold;
  r.api.push(DONE);
  late.done(TAKEN);
  await tick();
  check(r.xhr.length === 2 && !r.form.hidden,
    `an older code's answer sent ${r.xhr.length} writes`);
  own.done(TAKEN);
  await tick();
  check(r.xhr.length === 3 && r.xhr[2].m === 'PUT' &&
    r.xhr[2].u.includes('NEW2'), `its own: ${JSON.stringify(r.xhr[2])}`);
}

async function aWrongCodeOrNoAnswerAsksAgainAndWritesNothing() {
  const r = rig('CODE');
  await toCode(r, 'newdir');
  r.sendCode('');
  check(r.code.length === 0, 'an empty code went out');
  r.sendCode('wrong000');
  r.sendCode('wrong000');
  check(r.code.length === 1, 'a second tap sent the code twice');
  r.hold.done(WRONG);
  await tick();
  check(r.xhr.length === 1, 'a wrong code wrote');
  check(r.err.textContent === 'codewrong' && !r.form.hidden,
    `a wrong code says "${r.err.textContent}"`);
  r.sendCode('wrong000');
  r.hold.done({ type: 'basic', status: 503 });
  await tick();
  check(r.xhr.length === 1 && r.err.textContent === 'noans',
    `a closed panel: ${r.xhr.length} writes, "${r.err.textContent}"`);
  r.sendCode('wrong000');
  r.hold.bad(new Error('gone'));
  await tick();
  check(r.xhr.length === 1 && r.err.textContent === 'noans',
    `no answer: ${r.xhr.length} writes, "${r.err.textContent}"`);
  // The right one after all of those writes the name asked.
  r.api.push(DONE);
  r.sendCode('abcd2345');
  r.hold.done(TAKEN);
  await tick();
  check(r.xhr.length === 2 && r.xhr[1].b.includes('NEW1'),
    `the right code: ${JSON.stringify(r.xhr[1])}`);
}

async function aRefusalAfterTheCodeKeepsTheBox() {
  // The name refused after the code: the box with the name, to change it.
  const r = rig('CODE');
  await toCode(r, 'newdir');
  r.api.push(js(409, { error: 'EXISTS', text: 'taken' }));
  r.sendCode('abcd2345');
  r.hold.done(TAKEN);
  await tick();
  check(!r.box.hidden && r.err.textContent === 'taken' && !r.input.hidden &&
    r.input.value === 'NEW1' && r.form.hidden && !r.go.disabled,
  `after the code: "${r.err.textContent}" input ${r.input.value}`);
  // The code refused once more: asked again, for the same write.
  const q = rig('CODE');
  await toCode(q, 'del');
  q.api.push(CODE);
  q.sendCode('abcd2345');
  q.hold.done(TAKEN);
  await tick();
  check(q.xhr.length === 2 && !q.form.hidden, 'the second question');
  q.api.push(DONE);
  q.sendCode('abcd2345');
  q.hold.done(TAKEN);
  await tick();
  check(q.xhr.length === 3 && q.xhr[2].u === '/api/delete' && q.box.hidden,
    `after the second: ${JSON.stringify(q.xhr[2])}`);
}

async function replaceReadsTheEtagFromInfo() {
  const taken = js(409, { error: 'EXISTS', text: 'taken' });
  const start = async (r, info) => {
    r.api.push(taken, info);
    r.file.files = [FILE];
    r.file.on.change();
    await tick();
  };
  // The question after /api/info alone; REPLACE sends If-Match on its ETag.
  {
    const r = rig('OPEN');
    await start(r, js(200, { path: 'USER/A.TXT', s: 2048, etag: '0a1b2c3d' }));
    check(r.xhr[0].h['If-None-Match'] === '*', 'first try without If-None-Match');
    const g = r.xhr[1] || {};
    check(g.m === 'GET' && g.u.startsWith('/api/info?') &&
      g.u.includes('path=USER/A.TXT') && g.u.includes('etag=1') &&
      g.u.includes('dir=0') && g.u.includes('mount=3'),
    `the ETag read: ${g.m} ${g.u}`);
    check(!r.xhr.some(x => x.u.startsWith('/api/file') && x.m === 'GET'),
      'REPLACE read the whole file');
    check(!r.go.hidden && r.go.textContent === 'replace', 'no question');
    r.api.push(DONE);
    r.go.click();
    await tick();
    const p = r.xhr[2] || { h: {} };
    check(p.m === 'PUT' && p.h['If-Match'] === '"0a1b2c3d"' &&
      p.h['If-None-Match'] === undefined, `REPLACE: ${JSON.stringify(p.h)}`);
    check(r.box.hidden, 'REPLACE leaves the box');
  }
  // A file over 1MiB has no ETag: replaced without the compare.
  {
    const r = rig('OPEN');
    await start(r, js(200, { path: 'USER/A.TXT', s: 2097152, etag: '' }));
    r.api.push(DONE);
    r.go.click();
    await tick();
    const p = r.xhr[2] || { h: {} };
    check(p.m === 'PUT' && p.h['If-Match'] === undefined,
      `over 1MiB: ${JSON.stringify(p.h)}`);
  }
  // INFO refused: its text, no question and no write.
  {
    const r = rig('OPEN');
    await start(r, js(403, { error: 'LOCKED', text: 'locked' }));
    check(r.xhr.length === 2 && r.err.textContent === 'locked',
      `INFO refused: ${r.xhr.length} requests, "${r.err.textContent}"`);
  }
  // A change while the question is open: 412 shown, the file as it was.
  {
    const r = rig('OPEN');
    await start(r, js(200, { path: 'USER/A.TXT', s: 2048, etag: '0a1b2c3d' }));
    r.api.push(js(412, { error: 'CHANGED', text: 'changed', etag: 'ffffffff' }));
    r.go.click();
    await tick();
    check(r.err.textContent === 'changed' && r.xhr.length === 3,
      `412: "${r.err.textContent}"`);
  }
  // The code asked on REPLACE: the same If-Match goes out again.
  {
    const r = rig('CODE');
    await start(r, js(200, { path: 'USER/A.TXT', s: 2048, etag: '0a1b2c3d' }));
    r.api.push(CODE);
    r.go.click();
    await tick();
    r.api.push(DONE);
    r.sendCode('abcd2345');
    r.hold.done(TAKEN);
    await tick();
    const p = r.xhr[3] || { h: {} };
    check(p.m === 'PUT' && p.h['If-Match'] === '"0a1b2c3d"',
      `REPLACE after the code: ${JSON.stringify(p.h)}`);
  }
}

async function aFixedFolderTakesDeleteAndNoRenameOrMove() {
  const r = rig('OPEN');
  const e = r.c.selected().e;
  e.d = true;
  e.p = 'FIXED';
  check(r.wr.whyNot('delete') === '', 'DELETE of a fixed folder is dimmed');
  check(r.wr.whyNot('rename') === 'e.FIXED', 'RENAME of a fixed folder is not');
  check(r.wr.whyNot('move') === 'e.FIXED', 'MOVE of a fixed folder is not');
  e.p = 'FAULT';
  check(r.wr.whyNot('delete') === '', 'DELETE of the fault log is dimmed');
  check(r.wr.whyNot('rename') === 'e.FIXED', 'RENAME of the fault log is not');
  e.p = 'LOCKED';
  check(r.wr.whyNot('delete') === 'e.LOCKED', 'DELETE of the record is not');
}

(async () => {
  await eachWriteGoesOutAgainOnce();
  await aLateAnswerAfterCancelDoesNothing();
  await aWrongCodeOrNoAnswerAsksAgainAndWritesNothing();
  await aRefusalAfterTheCodeKeepsTheBox();
  await replaceReadsTheEtagFromInfo();
  await aFixedFolderTakesDeleteAndNoRenameOrMove();
  if (fails.length > 0) {
    console.error(fails.slice(0, 20).join('\n'));
    console.error(`${fails.length} of ${n} cases differ`);
    process.exit(1);
  }
  console.log(`${n} cases: write.js holds`);
})().catch(e => {
  console.error(e.stack || String(e));
  process.exit(1);
});
