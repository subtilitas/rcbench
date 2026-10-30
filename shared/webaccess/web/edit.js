// The editor of the Wi-Fi page's file manager (app.js): EDIT in the
// viewer turns the text into a textarea over the coloured lines, and SAVE
// writes it back through PUT /api/file (wa_api.h).
//
// The textarea holds the text with LF line ends, as every browser keeps
// them.  Its own letters are transparent; under it a <pre> of the same
// font and line height draws the lines in view and 200 either side,
// coloured as the viewer colours them (hl.js), moved with the textarea's
// scroll.  The browser keeps the caret, the selection, undo and a phone's
// keyboard.  The lines are split and coloured again one frame after each
// change or scroll.
//
// Line ends and BOM (byte order mark): SAVE writes the file's own ends
// back (LF, CR LF or CR) and its BOM in front.  A file with mixed ends is
// saved with LF ends, and the editor says so when it opens.  A file the
// browser cannot give back byte for byte (not UTF-8, or a byte the
// textarea drops) opens in the viewer only.
//
// SAVE writes only over the file the viewer read: it always sends
// If-Match with the ETag of that read or of its own last save.  A file
// changed on the card since, by FILES' editor on the panel, another
// browser or an upload, is answered 412 CHANGED, and the editor asks STAY,
// which keeps the text, or READ AGAIN, which drops it and reads the file
// anew.  No SAVE goes out without If-Match.
//
// A KST file (first line "KST servo configuration, format ...") loses
// every checksum line on SAVE (kst_file_checksum_line()), so the servo
// programmer reads it as EDITED, as FILES' editor does; the note says so.
// An ESC profile the panel reads at its start says it is read at the next
// start (the answer's note ESC_NEXT_START).
//
// The limits are the viewer's, of the bytes as saved: the status's
// file_max (1MiB) and lines_max (65,536 line ends, CR LF one); a text over
// either is not sent and the note says which.
//
// CLOSE and Escape ask first while the text has changes (UNSAVED CHANGES:
// STAY or DISCARD), and leaving or reloading the page asks through the
// browser's own question.
//
// A SAVE refused WRITES_CODE (Browser writes ACCESS CODE, no code given
// yet) asks for the code in the editor's box and sends it to /code itself,
// without leaving the page: the panel's answer sets the cookie, and SAVE
// goes out again with the text as it stands.
//
// SPDX-License-Identifier: MIT

var RcbEd = (function () {
  'use strict';

  var KST_HEAD = 'KST servo configuration, format ';
  var BOM = [0xEF, 0xBB, 0xBF];

  function hasBom(b) {
    return b.length >= 3 && b[0] === BOM[0] && b[1] === BOM[1] &&
      b[2] === BOM[2];
  }

  // The line ends of bytes @p b: {lf, crlf, cr}, CR LF counted once.
  function kinds(b) {
    var k = { lf: 0, crlf: 0, cr: 0 };
    for (var i = 0; i < b.length; ++i) {
      if (b[i] === 13) {
        if (i + 1 < b.length && b[i + 1] === 10) {
          k.crlf++;
          i++;
        } else {
          k.cr++;
        }
      } else if (b[i] === 10) {
        k.lf++;
      }
    }
    return k;
  }

  // The X-Rcbench-Eol word of bytes @p b: lf, crlf, cr or mixed; lf for
  // a file without a line end.
  function eolOf(b) {
    var k = kinds(b);
    var n = (k.lf > 0 ? 1 : 0) + (k.crlf > 0 ? 1 : 0) + (k.cr > 0 ? 1 : 0);
    if (n > 1) {
      return 'mixed';
    }
    return k.crlf > 0 ? 'crlf' : (k.cr > 0 ? 'cr' : 'lf');
  }

  // The line ends of bytes @p b as the panel counts them.
  function endsOf(b) {
    var k = kinds(b);
    return k.lf + k.crlf + k.cr;
  }

  // A file's bytes as the textarea takes them: {text, eol, bom}, the text
  // with LF ends and without the BOM; null when they are not UTF-8.
  function decode(b) {
    var bom = hasBom(b);
    var text;
    try {
      text = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true })
        .decode(b.subarray(bom ? 3 : 0));
    } catch (e) {
      return null;
    }
    return { text: text.replace(/\r\n?/g, '\n'), eol: eolOf(b), bom: bom };
  }

  // The textarea's text as the file's bytes: its LF ends turned into
  // @p eol's (LF for mixed), the BOM in front with @p bom.
  function encode(text, eol, bom) {
    var t = text.replace(/\r\n?/g, '\n');
    if (eol === 'crlf') {
      t = t.replace(/\n/g, '\r\n');
    } else if (eol === 'cr') {
      t = t.replace(/\n/g, '\r');
    }
    var body = new TextEncoder().encode(t);
    if (!bom) {
      return body;
    }
    var out = new Uint8Array(body.length + 3);
    out.set(BOM, 0);
    out.set(body, 3);
    return out;
  }

  function same(a, b) {
    if (a.length !== b.length) {
      return false;
    }
    for (var i = 0; i < a.length; ++i) {
      if (a[i] !== b[i]) {
        return false;
      }
    }
    return true;
  }

  // Whether the editor gives file @p b back as it is: decode() of it, or
  // null.  A mixed file comes back with LF ends, its other bytes kept.
  function open(b) {
    var d = decode(b);
    if (!d || (d.eol !== 'mixed' && !same(encode(d.text, d.eol, d.bom), b))) {
      return null;
    }
    return d;
  }

  // Why the panel would refuse bytes @p b: 'FULL' over @p max bytes,
  // 'LINES' over @p endsMax line ends; '' when they fit.
  function fits(b, max, endsMax) {
    if (b.length > max) {
      return 'FULL';
    }
    return endsOf(b) > endsMax ? 'LINES' : '';
  }

  function blank(c) {
    return c === ' ' || c === '\t';
  }

  // Whether line @p s is a checksum line, kst_file_checksum_line()'s
  // test: its first word, after spaces and tabs, is "checksum" in any
  // case, ended by a space, a tab, '=', ':' or the line's end.
  function sumLine(s) {
    var i = 0;
    while (i < s.length && blank(s.charAt(i))) {
      i++;
    }
    var w = i;
    while (w < s.length && !blank(s.charAt(w)) && s.charAt(w) !== '=' &&
           s.charAt(w) !== ':') {
      w++;
    }
    if (w - i !== 8) {
      return false;
    }
    for (var k = 0; k < 8; ++k) {
      var c = s.charCodeAt(i + k);
      if ((c >= 65 && c <= 90 ? c + 32 : c) !== 'checksum'.charCodeAt(k)) {
        return false;
      }
    }
    return true;
  }

  // A KST file's text without its checksum lines, each taken with its
  // line end, as FILES' editor takes them: {text, cut}.  Any other text
  // comes back as it is.
  function cutSum(text) {
    if (text.slice(0, KST_HEAD.length) !== KST_HEAD) {
      return { text: text, cut: false };
    }
    var out = [];
    var cut = false;
    var at = 0;
    // Line 0 starts with the header: never a checksum line.
    for (;;) {
      var nl = text.indexOf('\n', at);
      var end = nl < 0 ? text.length : nl;
      if (sumLine(text.slice(at, end))) {
        cut = true;
      } else {
        out.push(text.slice(at, nl < 0 ? end : nl + 1));
      }
      if (nl < 0) {
        break;
      }
      at = nl + 1;
    }
    return { text: out.join(''), cut: cut };
  }

  // The answer of POST /code with the redirect not followed: 'OK' for the
  // panel's 303 to the page (the cookie set), 'WRONG' for the page again
  // with its wrong-code line, else 'CLOSED'.
  function codeAnswer(r) {
    if (r.type === 'opaqueredirect' || r.redirected ||
        (r.status >= 300 && r.status < 400)) {
      return 'OK';
    }
    return r.status === 200 ? 'WRONG' : 'CLOSED';
  }

  function dirty(ed, text) {
    return text !== ed.base;
  }

  // What CLOSE or Escape does with session @p ed (null for none) and
  // @p text: 'close'; 'wait' while a save runs; 'stay' with the question
  // open, which it closes; 'ask' with changes.
  function onClose(ed, text, saving, asking) {
    if (!ed) {
      return 'close';
    }
    if (saving) {
      return 'wait';
    }
    if (asking) {
      return 'stay';
    }
    return dirty(ed, text) ? 'ask' : 'close';
  }

  // SAVE of @p text over the file of session @p ed ({tag, eol, bom, base,
  // max, endsMax}); @p put(bytes, headers) sends it and answers a promise
  // of {ok, status, j}.  Answers a promise of {r, text, kst, esc, mixed,
  // a}: r is SAVED, NONE (no changes), FULL, LINES, CHANGED (412, or no
  // ETag to send), or REFUSED with the answer a; text is the text as sent
  // (without its checksum lines when kst).  A save that wrote takes the
  // answer's ETag and the text as the session's new state.
  function save(ed, text, put) {
    if (!dirty(ed, text)) {
      return Promise.resolve({ r: 'NONE', text: text, kst: false });
    }
    var c = cutSum(text);
    var b = encode(c.text, ed.eol, ed.bom);
    var why = fits(b, ed.max, ed.endsMax);
    if (why) {
      return Promise.resolve({ r: why, text: c.text, kst: c.cut });
    }
    if (!ed.tag) {
      return Promise.resolve({ r: 'CHANGED', text: c.text, kst: c.cut });
    }
    return put(b, { 'If-Match': ed.tag }).then(function (a) {
      if (a.ok) {
        var mixed = ed.eol === 'mixed';
        ed.tag = (a.j && a.j.etag) ? '"' + a.j.etag + '"' : '';
        ed.base = c.text;
        if (mixed) {
          ed.eol = 'lf';
        }
        return { r: 'SAVED', text: c.text, kst: c.cut, mixed: mixed,
          esc: !!(a.j && a.j.note === 'ESC_NEXT_START'), size: b.length };
      }
      var changed = a.status === 412 || (a.j && a.j.error === 'CHANGED');
      return { r: changed ? 'CHANGED' : 'REFUSED', text: c.text, kst: c.cut,
        a: a };
    });
  }

  // ------------------------------------------------------------ the page

  function attach(c) {
    var F = c.F;
    var TX = c.TX;
    var ui = c.ui;
    var LH = 20;          // px of a line, as the viewer's
    var AROUND = 200;     // lines drawn above and below the window
    var enc = new TextEncoder();
    var dec = new TextDecoder('utf-8');
    var ed = null;        // the session while the editor is on
    var lines = null;     // the textarea's lines, split after a change
    var queued = false;
    var saving = false;
    var x = {};

    function note(t, warn) {
      x.note.textContent = t || '';
      x.note.className = 'vn' + (warn ? ' w' : '');
    }

    function lineHtml(s, k) {
      var b = enc.encode(s);
      var spans = RcbHl.line(c.hl, b, k, ed.kind);
      var out = '';
      for (var j = 0; j < spans.length; ++j) {
        var sp = spans[j];
        out += '<span class="h-' + c.cls[sp[2]] + '">' +
          F.esc(F.clean(dec.decode(b.subarray(sp[0], sp[0] + sp[1])))) +
          '</span>';
      }
      return out;
    }

    function head() {
      var on = !!ed;
      ui.vro.textContent = !on ? TX.ro
        : (dirty(ed, x.ta.value) ? TX.modified : TX.editing);
      ui.vro.className = (on && dirty(ed, x.ta.value)) ? 'w' : 'ro';
      x.edit.hidden = on;
      x.save.hidden = !on;
      x.save.disabled = saving;
    }

    function draw() {
      queued = false;
      if (!ed) {
        return;
      }
      if (!lines) {
        lines = x.ta.value.split('\n');
        ed.from = ed.to = -1;
      }
      var n = lines.length;
      var top = Math.floor(x.ta.scrollTop / LH);
      var rows = Math.ceil(x.ta.clientHeight / LH) + 1;
      var from = Math.max(0, top - AROUND);
      var to = Math.min(n, top + rows + AROUND);
      if (top < ed.from || Math.min(top + rows, n) > ed.to ||
          ed.from < 0) {
        var html = '';
        for (var k = from; k < to; ++k) {
          html += lineHtml(lines[k], k) + '\n';
        }
        x.pre.innerHTML = html;
        ed.from = from;
        ed.to = to;
      }
      x.pre.style.transform = 'translate(' + (-x.ta.scrollLeft) + 'px,' +
        (ed.from * LH - x.ta.scrollTop) + 'px)';
      ui.vline.textContent = F.fmt(TX.line, Math.min(top + 1, n), n);
      head();
    }

    function later(changed) {
      if (changed) {
        lines = null;
      }
      if (!queued) {
        queued = true;
        requestAnimationFrame(draw);
      }
    }

    function setText(t) {
      var top = x.ta.scrollTop;
      var at = Math.min(x.ta.selectionStart, t.length);
      x.ta.value = t;
      x.ta.scrollTop = top;
      x.ta.setSelectionRange(at, at);
      later(true);
    }

    // Why EDIT is refused for the file the viewer shows; '' when not.
    function whyNot() {
      var v = c.view();
      if (!v || !v.b) {
        return TX.vreading;
      }
      var closed = c.wr ? c.wr.closed() : TX.noans;
      if (closed) {
        return closed;
      }
      var e = v.e;
      if (e.b) {
        return TX['e.BUSY'];
      }
      if (e.p === 'LOCKED') {
        return TX['e.LOCKED'];
      }
      return e.p === 'FREE' ? '' : TX['e.ARG'];
    }

    function stop() {
      ed = null;
      lines = null;
      saving = false;
      x.box.hidden = true;
      x.wrap.hidden = true;
      x.ta.value = '';
      x.pre.textContent = '';
      ui.scroll.hidden = false;
      note('');
      head();
    }

    function start() {
      var why = whyNot();
      if (why) {
        note(why, true);
        return;
      }
      var v = c.view();
      var d = open(v.b);
      if (d) {
        x.ta.value = d.text;
        if (x.ta.value !== d.text) {
          d = null;           // the textarea would not keep every byte
        }
      }
      if (!d) {
        x.ta.value = '';
        note(TX.notutf8, true);
        return;
      }
      var s = c.st.status || {};
      ed = { tag: v.tag || '', eol: d.eol, bom: d.bom, base: d.text,
        max: s.file_max || 1048576, endsMax: s.lines_max || 65536,
        kind: RcbHl.kindOf(v.e.n), name: v.e.n, path: v.path,
        mount: v.mount, from: -1, to: -1 };
      var top = ui.scroll.scrollTop;
      ui.scroll.hidden = true;
      x.wrap.hidden = false;
      x.ta.scrollTop = top;
      x.ta.setSelectionRange(0, 0);
      x.ta.focus();
      note(d.eol === 'mixed' ? TX.mixedlf : '', d.eol === 'mixed');
      later(true);
    }

    function ask(changed) {
      x.qtitle.textContent = TX.unsaved;
      x.codeIn.hidden = true;
      x.stay.textContent = TX.stay;
      x.qtext.textContent = F.fmt(changed ? TX.qchanged : TX.qleave,
        ed.name);
      x.go.textContent = changed ? TX.readagain : TX.discard;
      x.goFn = changed ? function () {
        stop();
        c.reopen();
      } : function () {
        stop();
        c.close();
      };
      x.box.hidden = false;
      x.stay.focus();
    }

    function stay() {
      x.box.hidden = true;
      x.ta.focus();
    }

    function saved(res) {
      if (res.kst && x.ta.value !== res.text) {
        setText(res.text);
      }
      if (res.r === 'SAVED') {
        note(res.kst ? TX.savedkst
          : (res.esc ? TX.savedesc : F.fmt(TX.saved, ed.name)));
        ui.vmixed.hidden = true;
        ui.vsize.textContent = F.sizeText(res.size);
        c.rescan();
      } else if (res.r === 'NONE') {
        note(TX.nochg);
      } else if (res.r === 'FULL') {
        note(TX.full, true);
      } else if (res.r === 'LINES') {
        note(F.fmt(TX.linesfull, ed.endsMax), true);
      } else if (res.r === 'CHANGED') {
        note(res.a ? res.a.j.text : TX['e.CHANGED'], true);
        ask(true);
      } else if (res.a.j.error === 'WRITES_CODE' && c.queue) {
        note(res.a.j.text, true);
        askCode(res.a.j.text);
      } else {
        note(res.a.j.text, true);
      }
      head();
    }

    // The access code asked in the box; SEND CODE posts it to /code and,
    // taken, saves again.
    function askCode(text) {
      x.qtitle.textContent = TX.code;
      x.qtext.textContent = text;
      x.codeIn.hidden = false;
      x.codeIn.value = '';
      x.stay.textContent = TX.cancel;
      x.go.textContent = TX.codego;
      x.goFn = sendCode;
      x.box.hidden = false;
      x.codeIn.focus();
    }

    function sendCode() {
      var v = x.codeIn.value;
      if (v === '') {
        return;
      }
      x.go.disabled = true;
      c.queue(function () {
        return fetch('/code', { method: 'POST', credentials: 'same-origin',
          redirect: 'manual', cache: 'no-store',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'code=' + encodeURIComponent(v) })
          .then(function (r) {
            return { ok: true, w: codeAnswer(r) };
          }, function () {
            return { ok: false, w: 'CLOSED' };
          });
      }).then(function (a) {
        x.go.disabled = false;
        if (a.w === 'OK') {
          stay();
          save();
        } else if (a.w === 'WRONG') {
          x.qtext.textContent = TX.codewrong;
          x.codeIn.select();
        } else {
          stay();
          note(TX.noans, true);
        }
      });
    }

    function save() {
      if (!ed || saving) {
        return;
      }
      var closed = c.wr ? c.wr.closed() : TX.noans;
      if (closed) {
        note(closed, true);
        return;
      }
      saving = true;
      x.ta.readOnly = true;
      note(TX.saving);
      head();
      var s = ed;
      RcbEd.save(s, x.ta.value, function (b, hdr) {
        return c.wr.put(s.path, s.mount, new Blob([b]), hdr);
      }).then(function (res) {
        saving = false;
        x.ta.readOnly = false;
        if (ed === s) {
          saved(res);
          if (x.box.hidden) {
            x.ta.focus();
          }
        }
      });
    }

    // CLOSE or Escape on the viewer: true when the editor keeps it open
    // (a save under way, the question, or changes to ask about).
    function closing() {
      var what = onClose(ed, ed ? x.ta.value : '', saving, !x.box.hidden);
      if (what === 'stay') {
        stay();
      } else if (what === 'ask') {
        ask(false);
      } else if (what === 'close' && ed) {
        stop();
      }
      return what !== 'close';
    }

    function build() {
      x.edit = c.button(TX.edit, 'btn', start);
      x.save = c.button(TX.save, 'btn', save);
      x.save.hidden = true;
      x.note = c.el('span', 'vn');
      x.note.setAttribute('aria-live', 'polite');
      ui.vclose.parentNode.insertBefore(x.edit, ui.vclose);
      ui.vclose.parentNode.insertBefore(x.save, ui.vclose);
      ui.vclose.parentNode.appendChild(x.note);
      x.wrap = c.el('div', 'ed');
      x.wrap.hidden = true;
      x.pre = c.el('pre', 'text');
      x.ta = c.el('textarea');
      x.ta.wrap = 'off';
      x.ta.spellcheck = false;
      ['autocomplete', 'autocorrect', 'autocapitalize'].forEach(function (k) {
        x.ta.setAttribute(k, 'off');
      });
      x.ta.addEventListener('input', function () {
        later(true);
      });
      x.ta.addEventListener('scroll', function () {
        later(false);
      });
      x.ta.addEventListener('keydown', function (ev) {
        if ((ev.ctrlKey || ev.metaKey) && (ev.key === 's' || ev.key === 'S')) {
          ev.preventDefault();
          save();
        }
      });
      x.wrap.appendChild(x.pre);
      x.wrap.appendChild(x.ta);
      ui.scroll.parentNode.insertBefore(x.wrap, ui.scroll.nextSibling);

      x.box = c.el('div', 'box');
      x.box.hidden = true;
      var card = c.el('div', 'card');
      x.qtitle = c.el('h3', '', TX.unsaved);
      card.appendChild(x.qtitle);
      x.qtext = c.el('p');
      card.appendChild(x.qtext);
      x.codeIn = c.el('input');
      x.codeIn.maxLength = 8;
      x.codeIn.autocomplete = 'off';
      x.codeIn.hidden = true;
      x.codeIn.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter' && x.goFn && !x.go.disabled) {
          x.goFn();
        }
      });
      card.appendChild(x.codeIn);
      var row = c.el('div', 'foot');
      x.stay = c.button(TX.stay, 'btn', stay);
      x.go = c.button('', 'btn', function () {
        if (x.goFn) {
          x.goFn();
        }
      });
      row.appendChild(x.stay);
      row.appendChild(x.go);
      card.appendChild(row);
      x.box.appendChild(card);
      ui.viewer.appendChild(x.box);

      window.addEventListener('beforeunload', function (ev) {
        if (onClose(ed, ed ? x.ta.value : '', false, false) === 'ask') {
          ev.preventDefault();
          ev.returnValue = '';
        }
      });
    }

    build();
    return { closing: closing, stop: stop, whyNot: whyNot,
      active: function () {
        return !!ed;
      } };
  }

  return { decode: decode, encode: encode, open: open, eolOf: eolOf,
    endsOf: endsOf, fits: fits, sumLine: sumLine, cutSum: cutSum,
    dirty: dirty, onClose: onClose, codeAnswer: codeAnswer, save: save,
    attach: attach };
}());

if (typeof module !== 'undefined') {
  module.exports = RcbEd;
}
