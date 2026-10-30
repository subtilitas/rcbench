// The file manager of the Wi-Fi page: the card in two panes, as FILES
// shows it on the panel, read through the page's file API (wa_api.h).
//
// Each pane is one folder, a page of up to 48 entries at a time, folders
// first.  The left pane opens on the card's root, the right one on the
// User log folder; the browser keeps both places for its tab
// (sessionStorage) while the same card is in the slot.  A tap on a row
// selects it and makes its pane the active one; a second tap (so a double
// click) or Enter opens it.  RESCAN, INFO, DOWNLOAD and OPEN act on the
// active pane's selected entry; a button that would be refused is drawn
// dimmed and a tap on it says why.  OPEN shows a text file read-only, its
// lines coloured as the panel's viewer colours them (hl.js); its EDIT and
// SAVE are edit.js's.  The writes (UPLOAD, NEW, RENAME, MOVE, DELETE) are
// write.js's, in a second row of buttons, on the same panes and the same
// one request at a time.
//
// The panel answers one request at a time, so the app sends one at a time.
// The texts and the tables come from the page's data element (wa_app.c),
// in the panel's language.
//
// SPDX-License-Identifier: MIT

var RcbFm = (function () {
  'use strict';

  // A size as FILES writes it (files_size_text()): 512B, 14kB, 7.0MB,
  // 8.00GB; 1kB is 1,024 bytes.
  function sizeText(b) {
    if (b >= 1073741824) {
      return (b / 1073741824).toFixed(2) + 'GB';
    }
    if (b >= 1048576) {
      return (b / 1048576).toFixed(1) + 'MB';
    }
    if (b >= 1024) {
      return Math.floor(b / 1024) + 'kB';
    }
    return b + 'B';
  }

  // A text with its %s, %d and %u replaced by the arguments in order.
  function fmt(s) {
    var a = arguments;
    var k = 1;
    return String(s).replace(/%[sdu]/g, function () {
      return String(a[k++]);
    });
  }

  // A path from the card's root, each name percent-encoded, "/" kept.
  function encPath(p) {
    return p === '' ? '' : p.split('/').map(encodeURIComponent).join('/');
  }

  function join(dir, name) {
    return dir === '' ? name : dir + '/' + name;
  }

  function parent(p) {
    var i = p.lastIndexOf('/');
    return i < 0 ? '' : p.slice(0, i);
  }

  function esc(t) {
    return t.replace(/[&<>]/g, function (c) {
      return c === '&' ? '&amp;' : (c === '<' ? '&lt;' : '&gt;');
    });
  }

  // As the panel draws them: a control character and a byte that starts
  // no UTF-8 sequence are a "?" each.
  function clean(t) {
    return t.replace(/[\u0000-\u0008\u000b-\u001f\u007f\ufffd]/g, '?');
  }

  return { sizeText: sizeText, fmt: fmt, encPath: encPath, join: join,
    parent: parent, esc: esc, clean: clean };
}());

if (typeof document !== 'undefined' && document.getElementById('fm')) {
  (function () {
    'use strict';
    var F = RcbFm;
    var D = JSON.parse(document.getElementById('fm-data').textContent);
    var TX = D.tx;
    var HL = RcbHl.tables(D.hl);
    var KEY = 'rcbench.files';
    var LH = 20;          // px of a line in the viewer, as in app.css
    var AROUND = 200;     // lines drawn above and below the window
    var WAIT_MS = 2000;   // before the line says the panel is busy

    var st = { status: null, mount: -1, active: 0, panes: [] };
    var ui = {};
    var chain = Promise.resolve();
    var pending = 0;
    var waitTimer = null;
    var view = null;
    var viewSeq = 0;
    var infoSeq = 0;
    var writes = null;    // write.js's buttons, once attached
    var editor = null;    // edit.js's EDIT and SAVE, once attached

    function el(tag, cls, text) {
      var e = document.createElement(tag);
      if (cls) {
        e.className = cls;
      }
      if (text !== undefined) {
        e.textContent = text;
      }
      return e;
    }

    function button(text, cls, fn) {
      var b = el('button', cls, text);
      b.type = 'button';
      b.addEventListener('click', fn);
      return b;
    }

    function msg(text) {
      ui.msg.textContent = text || '';
    }

    // ------------------------------------------------------- requests

    function armWait() {
      if (waitTimer === null) {
        waitTimer = setTimeout(function () {
          ui.wait.textContent = TX.wait;
        }, WAIT_MS);
      }
    }

    function disarmWait() {
      if (pending === 0 && waitTimer !== null) {
        clearTimeout(waitTimer);
        waitTimer = null;
        ui.wait.textContent = '';
      }
    }

    function refusal(status) {
      return { error: 'HTTP', text: TX.noans + ' HTTP ' + status };
    }

    // One request after the one before: @p send() starts it and answers
    // {ok, j} with the JSON answer or the refusal.  A missing access code
    // shows the code form again.
    function queue(send) {
      function run() {
        pending++;
        armWait();
        return send().then(function (a) {
          pending--;
          disarmWait();
          if (!a.ok && a.j && a.j.error === 'CODE') {
            location.reload();
          }
          return a;
        });
      }
      var p = chain.then(run, run);
      chain = p;
      return p;
    }

    // A read: {ok, j}, and {body, r} for a file.  A connection that breaks,
    // before the answer's head or during its body, is a refusal too, so
    // every request ends here once.
    function api(url, file) {
      return queue(function () {
        return fetch(url, { cache: 'no-store', credentials: 'same-origin' })
          .then(function (r) {
            if (file && r.ok) {
              return r.arrayBuffer().then(function (b) {
                return { ok: true, r: r, body: b };
              });
            }
            return r.json().then(function (j) {
              return { ok: r.ok, j: j };
            }, function () {
              return { ok: false, j: refusal(r.status) };
            });
          })
          .catch(function () {
            return { ok: false, j: { error: 'NET', text: TX.noans } };
          });
      });
    }

    // --------------------------------------------------------- places

    function save() {
      try {
        sessionStorage.setItem(KEY, JSON.stringify({
          m: st.mount,
          a: st.active,
          p: st.panes.map(function (p) {
            var L = p.list;
            var key = (L && L.ahead > 0 && L.entries.length > 0)
              ? { n: L.entries[0].n, d: L.entries[0].d } : null;
            return { path: p.path, key: key };
          })
        }));
      } catch (e) {
        // A browser without storage starts at the two first places.
      }
    }

    function saved() {
      try {
        var s = JSON.parse(sessionStorage.getItem(KEY));
        return (s && s.m === st.mount && s.p && s.p.length === 2) ? s : null;
      } catch (e) {
        return null;
      }
    }

    function userLogs() {
      return (st.status && st.status.user_logs) || 'USER/LOGS';
    }

    function pane(path, key) {
      return { path: path, key: key, list: null, err: null, read: false,
        sel: null };
    }

    function places(keep) {
      var s = keep ? saved() : null;
      if (s) {
        st.active = s.a === 1 ? 1 : 0;
        st.panes = [pane(s.p[0].path, s.p[0].key), pane(s.p[1].path,
          s.p[1].key)];
      } else {
        st.active = 0;
        st.panes = [pane('', null), pane(userLogs(), null)];
      }
    }

    function status() {
      return api('/api/status').then(function (a) {
        if (a.ok) {
          st.status = a.j;
          st.mount = a.j.card.mount;
        } else {
          msg(a.j.text);
        }
        return a.ok;
      });
    }

    // Both panes at the root and the User log folder of the card in the
    // slot now, or at the places kept for this card.
    function restart(keep) {
      return status().then(function () {
        places(keep);
        draw();
        return Promise.all([reread(0), reread(1)]);
      });
    }

    // ---------------------------------------------------------- lists

    function load(i, go, key) {
      var p = st.panes[i];
      var q = 'path=' + F.encPath(p.path) + '&go=' + go;
      if (key) {
        q += '&key=' + encodeURIComponent(key.n) + '&key_dir=' +
          (key.d ? 1 : 0);
      }
      return api('/api/list?' + q).then(function (a) {
        if (st.panes[i] !== p) {
          return null;
        }
        if (a.ok && a.j.mount !== st.mount && st.mount >= 0) {
          // Another card since the panes were read: both start again.
          st.mount = a.j.mount;
          return restart(false);
        }
        if (!a.ok && a.j.error === 'CARD_GONE') {
          msg(a.j.text);
          return restart(false);
        }
        p.read = true;
        if (a.ok) {
          p.list = a.j;
          p.err = null;
          var sel = p.sel;
          p.sel = null;
          a.j.entries.forEach(function (e) {
            if (sel && e.n === sel.n && e.d === sel.d) {
              p.sel = e;
            }
          });
          save();
        } else {
          p.list = null;
          p.err = a.j;
          p.sel = null;
        }
        drawPane(i);
        footer();
        return null;
      });
    }

    // A pane read again at the page it shows (RESCAN): the first page
    // from the folder's first entry, a later one from its own first.
    function reread(i) {
      var p = st.panes[i];
      var L = p.list;
      if (L && L.ahead > 0 && L.entries.length > 0) {
        return load(i, 'same', L.entries[0]);
      }
      if (!L && p.key) {
        var key = p.key;
        p.key = null;
        return load(i, 'same', key);
      }
      return load(i, 'first');
    }

    function rescan() {
      msg('');
      var mount = st.mount;
      return status().then(function () {
        if (st.mount !== mount) {
          return restart(false);
        }
        return Promise.all([reread(0), reread(1)]);
      });
    }

    function go(i, path) {
      st.panes[i] = pane(path, null);
      msg('');
      activate(i);
      drawPane(i);
      return load(i, 'first');
    }

    function page(i, dir) {
      var L = st.panes[i].list;
      if (!L || L.entries.length === 0) {
        return;
      }
      activate(i);
      load(i, dir, dir === 'prev' ? L.entries[0]
        : L.entries[L.entries.length - 1]);
    }

    // ----------------------------------------------------- the panes

    function kindWord(e) {
      return TX['k.' + e.k] || TX['k.OTHER'];
    }

    function stateWord(e) {
      if (e.b) {
        return TX.writing;
      }
      if (e.p === 'LOCKED') {
        return TX.locked;
      }
      return e.p === 'FIXED' ? TX.fixed : '';
    }

    function heading(p) {
      var L = p.list;
      if (!p.read) {
        return TX.reading;
      }
      if (!L) {
        return (p.err && p.err.error === 'NO_CARD') ? TX.sd : '';
      }
      var h;
      if (L.total > 0 && (L.prev || L.next)) {
        h = F.fmt(TX.of, L.ahead + 1, L.ahead + L.entries.length, L.total);
      } else {
        h = (L.total === 1) ? TX.one : F.fmt(TX.n, L.total);
      }
      return h + (L.unlisted > 0 ? F.fmt(TX.unl, L.unlisted) : '');
    }

    function emptyText(p) {
      if (p.err) {
        return p.err.error === 'NOT_FOUND' ? TX.nofolder : p.err.text;
      }
      if (!p.list) {
        return '';
      }
      return p.list.unlisted > 0 ? TX.hidden : TX.empty;
    }

    function row(i, e) {
      var p = st.panes[i];
      var tr = el('tr', (p.sel === e) ? 'sel' : '');
      tr.rcbEntry = e;
      tr.tabIndex = 0;
      var name = el('td', 'nm');
      name.appendChild(el('div', e.d ? 'n dir' : 'n', e.n));
      var line = el('div', 'k', kindWord(e));
      var word = stateWord(e);
      if (word) {
        line.appendChild(el('span', 'w', word));
      }
      name.appendChild(line);
      tr.appendChild(name);
      tr.appendChild(el('td', 'sz', e.d ? '' : F.sizeText(e.s)));
      tr.addEventListener('click', function () {
        if (p.sel === e && st.active === i) {
          open();
          return;
        }
        select(i, e);
      });
      tr.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter') {
          // The viewer takes the focus to its CLOSE: the key's default
          // would press it.
          ev.preventDefault();
          select(i, e);
          open();
        } else if (ev.key === ' ') {
          ev.preventDefault();
          select(i, e);
        }
      });
      return tr;
    }

    function cardName() {
      var c = st.status && st.status.card;
      return (c && c.mounted && c.name) ? c.name : TX.sd;
    }

    function drawPane(i) {
      var p = st.panes[i];
      var u = ui.panes[i];
      var L = p.list;
      u.box.className = 'pane' + (i === st.active ? ' on' : '');
      u.up.disabled = p.path === '';
      u.up.textContent = p.path === '' ? cardName() : '\u2039 ' + p.path;
      var paged = !!(L && L.total > 0 && (L.prev || L.next));
      u.prev.hidden = !paged;
      u.next.hidden = !paged;
      u.prev.disabled = !(L && L.prev);
      u.next.disabled = !(L && L.next);
      u.head.textContent = heading(p);
      // A row with the focus gets it back in its new drawing, so Enter
      // after Space or a click still reaches it.
      var f = document.activeElement;
      var had = (f && f.parentNode === u.body) ? f.rcbEntry : null;
      u.body.textContent = '';
      if (L && L.entries.length > 0) {
        L.entries.forEach(function (e) {
          var tr = row(i, e);
          u.body.appendChild(tr);
          if (had && e.n === had.n && e.d === had.d) {
            tr.focus();
          }
        });
      } else if (p.read) {
        var tr = el('tr', 'none');
        tr.appendChild(el('td', '', emptyText(p)));
        u.body.appendChild(tr);
      }
    }

    function draw() {
      drawPane(0);
      drawPane(1);
      footer();
    }

    // The active pane now @p i: both borders and the footer, whose
    // DOWNLOAD names the active pane's selection, follow at once.
    function activate(i) {
      if (st.active !== i) {
        st.active = i;
        drawPane(0);
        drawPane(1);
      }
      footer();
    }

    function select(i, e) {
      st.panes[i].sel = e;
      msg('');
      if (st.active !== i) {
        activate(i);
      } else {
        drawPane(i);
        footer();
      }
      save();
    }

    function selected() {
      var p = st.panes[st.active];
      return (p && p.sel) ? { p: p, e: p.sel } : null;
    }

    // ---------------------------------------------------- the footer

    // Why OPEN, INFO or DOWNLOAD would be refused now; '' when not.
    function whyNot(what) {
      var s = selected();
      if (!s) {
        return what === 'rescan' ? '' : TX.select;
      }
      var e = s.e;
      if (what === 'dl') {
        return e.d ? TX['e.ARG'] : '';
      }
      if (what === 'open' && !e.d) {
        if (e.b) {
          return TX['e.BUSY'];
        }
        if (e.p === 'LOCKED') {
          return TX['e.LOCKED'];
        }
        if (st.status && e.s > st.status.file_max) {
          return TX['e.TOO_BIG'];
        }
      }
      return '';
    }

    function footer() {
      ['info', 'dl', 'open'].forEach(function (w) {
        ui[w].className = 'btn' + (whyNot(w) ? ' dim' : '');
      });
      var s = selected();
      if (s && !s.e.d) {
        ui.dl.href = '/card/' + F.encPath(F.join(s.p.path, s.e.n));
      } else {
        ui.dl.removeAttribute('href');
      }
      if (writes) {
        writes.footer();
      }
    }

    function act(what, fn) {
      return function (ev) {
        var why = whyNot(what);
        if (why) {
          if (ev) {
            ev.preventDefault();
          }
          msg(why);
          return;
        }
        msg('');
        fn();
      };
    }

    function open() {
      act('open', function () {
        var s = selected();
        if (s.e.d) {
          go(st.active, F.join(s.p.path, s.e.n));
        } else {
          openFile(st.active, s.e);
        }
      })();
    }

    // ------------------------------------------------------ the boxes

    function infoRow(t, key, value, warn) {
      var tr = el('tr', warn ? 'warn' : '');
      tr.appendChild(el('th', '', key));
      tr.appendChild(el('td', '', value));
      t.appendChild(tr);
    }

    function ruleWord(e) {
      if (e.b) {
        return TX['r.BUSY'];
      }
      return TX['r.' + e.p] || TX['r.FREE'];
    }

    function infoBody(s, a) {
      var e = s.e;
      var t = el('table', 'rows');
      infoRow(t, TX['i.name'], e.n);
      infoRow(t, TX['i.folder'], s.p.path === '' ? TX.root : s.p.path);
      infoRow(t, TX['i.kind'], kindWord(e));
      var size;
      var time;
      if (!a) {
        size = time = TX['i.reading'];
      } else if (e.d) {
        size = a.ok ? F.fmt(TX['i.count'], a.j.entries, F.sizeText(a.j.s)) +
          (a.j.unlisted > 0 ? '; ' + F.fmt(TX['i.unl'], a.j.unlisted) : '')
          : '';
      } else {
        var bytes = a.ok ? a.j.s : e.s;
        size = bytes + 'B' + (bytes >= 1024 ? ', ' + F.sizeText(bytes) : '');
      }
      if (a) {
        time = (a.ok && a.j.time) ? a.j.time : (a.ok ? TX['i.notime'] : '');
      }
      infoRow(t, e.d ? TX['i.entries'] : TX['i.size'], size);
      infoRow(t, TX['i.time'], time);
      infoRow(t, TX['i.rule'], ruleWord(e), e.b || e.p !== 'FREE');
      ui.boxBody.textContent = '';
      ui.boxBody.appendChild(t);
      if (a && !a.ok) {
        ui.boxBody.appendChild(el('p', 'warn', a.j.text));
      }
    }

    // An answer fills the box only while the box it was asked for is
    // open: CLOSE and every new INFO move infoSeq on.
    function info() {
      var s = selected();
      var mount = s.p.list ? s.p.list.mount : st.mount;
      var seq = ++infoSeq;
      ui.boxTitle.textContent = s.e.d ? TX['k.FOLDER'] : TX['i.file'];
      infoBody(s, null);
      ui.box.hidden = false;
      ui.boxClose.focus();
      api('/api/info?path=' + F.encPath(F.join(s.p.path, s.e.n)) + '&dir=' +
        (s.e.d ? 1 : 0) + '&mount=' + mount).then(function (a) {
        if (seq === infoSeq && !ui.box.hidden) {
          infoBody(s, a);
        }
      });
    }

    function closeBox() {
      ui.box.hidden = true;
      infoSeq++;
    }

    // ----------------------------------------------------- the viewer

    function lineHtml(k) {
      var s = view.b.subarray(view.L.s[k], view.L.e[k]);
      var spans = RcbHl.line(HL, s, k, view.kind);
      var out = '';
      for (var j = 0; j < spans.length; ++j) {
        var sp = spans[j];
        out += '<span class="h-' + D.hl.cls[sp[2]] + '">' +
          F.esc(F.clean(view.dec.decode(s.subarray(sp[0],
            sp[0] + sp[1])))) +
          '</span>';
      }
      return out;
    }

    function drawView() {
      var n = view.L.s.length;
      var top = Math.floor(ui.scroll.scrollTop / LH);
      var rows = Math.ceil(ui.scroll.clientHeight / LH) + 1;
      ui.vline.textContent = F.fmt(TX.line, Math.min(top + 1, n), n);
      if (top >= view.from && Math.min(top + rows, n) <= view.to &&
          view.to > view.from) {
        return;
      }
      view.from = Math.max(0, top - AROUND);
      view.to = Math.min(n, top + rows + AROUND);
      var html = '';
      for (var k = view.from; k < view.to; ++k) {
        html += lineHtml(k) + '\n';
      }
      ui.pre.style.top = (view.from * LH) + 'px';
      ui.pre.innerHTML = html;
    }

    function viewNote(text) {
      ui.pre.style.top = '0px';
      ui.pre.textContent = text;
      ui.sizer.style.height = '0px';
      ui.vline.textContent = '';
    }

    // The panes and the buttons under the viewer take no focus, click or
    // key while it shows, so nothing behind it acts on the card or opens
    // another file over the editor.
    function behind(off) {
      [ui.panesEl, ui.barEl].forEach(function (x) {
        x.inert = off;
        if (off) {
          x.setAttribute('aria-hidden', 'true');
        } else {
          x.removeAttribute('aria-hidden');
        }
      });
    }

    function openFile(i, e) {
      if (editor && editor.active()) {
        return;               // the editor's text first: CLOSE asks
      }
      var p = st.panes[i];
      var path = F.join(p.path, e.n);
      var seq = ++viewSeq;
      view = null;
      ui.vpath.textContent = path;
      ui.vsize.textContent = F.sizeText(e.s);
      ui.vmixed.hidden = true;
      viewNote(TX.vreading);
      ui.viewer.hidden = false;
      behind(true);
      ui.scroll.scrollTop = 0;
      ui.scroll.scrollLeft = 0;
      ui.vclose.focus();
      api('/api/file?path=' + F.encPath(path) + '&mount=' +
        (p.list ? p.list.mount : st.mount), true).then(function (a) {
        if (seq !== viewSeq || ui.viewer.hidden) {
          return;
        }
        if (!a.ok) {
          viewNote(a.j.text);
          return;
        }
        var b = new Uint8Array(a.body);
        view = { b: b, L: RcbHl.lines(b), kind: RcbHl.kindOf(e.n),
          dec: new TextDecoder('utf-8'), from: 0, to: 0, i: i, e: e,
          path: path, mount: p.list ? p.list.mount : st.mount,
          tag: a.r.headers.get('ETag') || '' };
        ui.vsize.textContent = F.sizeText(b.length);
        ui.vmixed.hidden = a.r.headers.get('X-Rcbench-Eol') !== 'mixed';
        if (view.L.s.length === 0) {
          viewNote(TX.vempty);
          return;
        }
        ui.sizer.style.height = (view.L.s.length * LH) + 'px';
        drawView();
      });
    }

    function closeView() {
      if (editor && editor.closing()) {
        return;
      }
      ui.viewer.hidden = true;
      behind(false);
      viewSeq++;
      view = null;
      ui.pre.textContent = '';
    }

    // ----------------------------------------------------- the build

    function buildPane(i) {
      var u = {};
      u.box = el('div', 'pane');
      var hd = el('div', 'ph');
      u.up = button('', 'up', function () {
        go(i, F.parent(st.panes[i].path));
      });
      u.up.title = TX.up;
      u.prev = button(TX.prev, 'pg', function () {
        page(i, 'prev');
      });
      u.next = button(TX.next, 'pg', function () {
        page(i, 'next');
      });
      hd.appendChild(u.up);
      hd.appendChild(u.prev);
      hd.appendChild(u.next);
      u.box.appendChild(hd);
      u.head = el('div', 'pc');
      u.box.appendChild(u.head);
      var t = el('table', 'pl');
      u.body = el('tbody');
      t.appendChild(u.body);
      u.box.appendChild(t);
      u.box.addEventListener('click', function () {
        activate(i);
      });
      return u;
    }

    function build(root) {
      var panes = el('div', 'panes');
      ui.panes = [buildPane(0), buildPane(1)];
      panes.appendChild(ui.panes[0].box);
      panes.appendChild(ui.panes[1].box);
      root.appendChild(panes);
      ui.panesEl = panes;
      // The buttons and the message lines stay at the window's foot while
      // a pane of 48 rows scrolls.
      var bar = el('div', 'bar');
      var foot = el('div', 'foot');
      foot.appendChild(button(TX.rescan, 'btn', function () {
        rescan();
      }));
      ui.info = button(TX.info, 'btn', act('info', info));
      ui.dl = el('a', 'btn', TX.dl);
      ui.dl.setAttribute('download', '');
      // The link is set from the selection as it stands at the click.
      ui.dl.addEventListener('click', act('dl', footer));
      ui.open = button(TX.open, 'btn', open);
      [ui.info, ui.dl, ui.open].forEach(function (b) {
        foot.appendChild(b);
      });
      bar.appendChild(foot);
      ui.foot = foot;
      ui.msg = el('p', 'msg');
      ui.msg.setAttribute('aria-live', 'polite');
      ui.wait = el('p', 'msg');
      bar.appendChild(ui.msg);
      bar.appendChild(ui.wait);
      root.appendChild(bar);
      ui.barEl = bar;

      ui.box = el('div', 'box');
      ui.box.hidden = true;
      var card = el('div', 'card');
      ui.boxTitle = el('h3');
      ui.boxBody = el('div');
      ui.boxClose = button(TX.close, 'btn', closeBox);
      card.appendChild(ui.boxTitle);
      card.appendChild(ui.boxBody);
      card.appendChild(ui.boxClose);
      ui.box.appendChild(card);
      root.appendChild(ui.box);

      ui.viewer = el('div', 'viewer');
      ui.viewer.hidden = true;
      var vh = el('div', 'vh');
      ui.vpath = el('span', 'vp');
      ui.vsize = el('span', '');
      ui.vline = el('span', '');
      ui.vmixed = el('span', 'w', TX.mixed);
      ui.vro = el('span', 'ro', TX.ro);
      ui.vclose = button(TX.close, 'btn', closeView);
      [ui.vpath, ui.vsize, ui.vro, ui.vline, ui.vmixed,
        ui.vclose].forEach(function (x) {
        vh.appendChild(x);
      });
      ui.viewer.appendChild(vh);
      ui.scroll = el('div', 'scroll');
      ui.sizer = el('div', 'sizer');
      ui.pre = el('pre', 'text');
      ui.scroll.appendChild(ui.sizer);
      ui.scroll.appendChild(ui.pre);
      ui.scroll.addEventListener('scroll', function () {
        if (view) {
          requestAnimationFrame(drawView);
        }
      });
      ui.viewer.appendChild(ui.scroll);
      root.appendChild(ui.viewer);

      document.addEventListener('keydown', function (ev) {
        if (ev.key === 'Escape') {
          if (!ui.box.hidden) {
            closeBox();
          } else if (!ui.viewer.hidden) {
            closeView();
          }
        }
      });
    }

    var root = document.getElementById('fm');
    build(root);
    if (typeof RcbWr !== 'undefined') {
      writes = RcbWr.attach({ F: F, TX: TX, st: st, ui: ui, root: root,
        el: el, button: button, msg: msg, queue: queue, rescan: rescan,
        selected: selected });
    }
    if (typeof RcbEd !== 'undefined') {
      editor = RcbEd.attach({ F: F, TX: TX, st: st, ui: ui, el: el,
        button: button, hl: HL, cls: D.hl.cls, wr: writes, rescan: rescan,
        queue: queue,
        view: function () {
          return view;
        },
        reopen: function () {
          var v = view;
          if (v) {
            openFile(v.i, v.e);
          }
        },
        close: closeView });
    }
    places(false);
    draw();
    restart(true);
  }());
}

if (typeof module !== 'undefined') {
  module.exports = RcbFm;
}
