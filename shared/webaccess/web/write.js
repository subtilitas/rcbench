// The writes of the Wi-Fi page's file manager (app.js): UPLOAD, NEW
// FOLDER, NEW TEXT FILE, RENAME, MOVE and DELETE, through the file API's
// writes (wa_api.h).
//
// The buttons sit in a second row under app.js's.  Each acts on the active
// pane: UPLOAD and the two NEW into its folder, RENAME, MOVE and DELETE on
// its selected entry, MOVE into the other pane's folder under the same
// name.  A button that would be refused is drawn dimmed and a tap says
// why: the panel's answer of /api/status while it takes no browser write
// (SETUP's Browser writes, the hold on WI-FI, ARMED, a run), no selection,
// a folder of the bench, the servo programmer's record, a file the logger
// writes.  The panel decides each write again; its refusal is shown as it
// comes.
//
// UPLOAD sends one file of 1MiB at most with its progress, makes it only
// where no entry has its name (If-None-Match: *), and asks before it
// replaces one.  A write the panel answers PANEL_BUSY (FILES on the panel
// runs a job) is sent again after 2s, three times at most.  A write that
// needs the access code shows the code form; the code is sent to /code and
// the page loads again with it.
//
// Every write carries the X-Rcbench-Upload header the panel asks of the
// page's own script (wa_guard.h).
//
// SPDX-License-Identifier: MIT

var RcbWr = (function () {
  'use strict';

  var RETRY_MS = 2000;  // after PANEL_BUSY, as Retry-After says
  var RETRIES = 3;

  // A form's body: each value a path or a word already encoded.
  function form(fields) {
    return Object.keys(fields).map(function (k) {
      return k + '=' + fields[k];
    }).join('&');
  }

  // The report a run log takes along (log_name_report_of()): its name with
  // ".TXT", in upper case; '' for any other entry.
  function reportOf(e) {
    if (e.d || e.k !== 'RUN') {
      return '';
    }
    var i = e.n.lastIndexOf('.');
    return (i > 0 ? e.n.slice(0, i) : e.n).toUpperCase() + '.TXT';
  }

  function attach(c) {
    var F = c.F;
    var TX = c.TX;
    var ui = {};

    function selected() {
      return c.selected();
    }

    function pane() {
      return c.st.panes[c.st.active];
    }

    function mountOf(p) {
      return p.list ? p.list.mount : c.st.mount;
    }

    // ---------------------------------------------------------- whyNot

    function closed() {
      var s = c.st.status;
      if (!s) {
        return TX.noans;
      }
      return s.writes === 'open' ? '' : (s.why_text || TX['e.REQUEST']);
    }

    // Why a write would be refused now; '' when the panel is asked.
    function whyNot(what) {
      var why = closed();
      if (why) {
        return why;
      }
      var p = pane();
      if (what === 'upload' || what === 'newdir' || what === 'newfile') {
        return (p && p.list) ? '' : TX.select;
      }
      var s = selected();
      if (!s) {
        return TX.select;
      }
      var e = s.e;
      if (e.b) {
        return TX['e.BUSY'];
      }
      if (e.p === 'LOCKED') {
        return TX['e.LOCKED'];
      }
      if (e.p === 'FIXED' || (e.p === 'FAULT' && what !== 'delete')) {
        return TX['e.FIXED'];
      }
      if (what === 'move') {
        var o = c.st.panes[1 - c.st.active];
        if (!o || !o.list || o.path.toUpperCase() === s.p.path.toUpperCase()) {
          return TX['e.SAME'];
        }
      }
      return '';
    }

    function footer() {
      ['upload', 'newdir', 'newfile', 'rename', 'move', 'delete']
        .forEach(function (w) {
          ui[w].className = 'btn' + (whyNot(w) ? ' dim' : '');
        });
    }

    // --------------------------------------------------------- the box

    function box(title, lines) {
      ui.title.textContent = title;
      ui.body.textContent = '';
      (lines || []).forEach(function (t) {
        ui.body.appendChild(c.el('p', '', t));
      });
      ui.err.textContent = '';
      ui.input.hidden = true;
      ui.code.hidden = true;
      ui.bar.hidden = true;
      ui.go.hidden = false;
      ui.go.disabled = false;
      ui.cancel.disabled = false;
      ui.box.hidden = false;
    }

    function closeBox() {
      ui.box.hidden = true;
      ui.goFn = null;
    }

    // A question with an action button; @p fn runs on it.
    function ask(title, lines, label, fn) {
      box(title, lines);
      ui.go.textContent = label;
      ui.goFn = fn;
      ui.go.focus();
    }

    // A name typed into the box; @p fn(name) runs on the action button or
    // Enter.
    function askName(title, value, label, fn) {
      box(title, []);
      ui.input.hidden = false;
      ui.input.value = value;
      ui.go.textContent = label;
      ui.goFn = function () {
        var n = ui.input.value;
        if (n !== '') {
          fn(n);
        }
      };
      ui.input.focus();
      ui.input.select();
    }

    function busy(on) {
      ui.go.disabled = on;
      ui.cancel.disabled = on;
    }

    // ------------------------------------------------------- requests

    // One write after the request before: {ok, status, j}.  @p body is a
    // string (a form), a Blob or ''; @p prog takes the bytes sent.
    function send(method, url, body, hdr, prog) {
      return c.queue(function () {
        return new Promise(function (done) {
          var x = new XMLHttpRequest();
          x.open(method, url);
          x.setRequestHeader('X-Rcbench-Upload', '1');
          Object.keys(hdr || {}).forEach(function (k) {
            x.setRequestHeader(k, hdr[k]);
          });
          if (prog && x.upload) {
            x.upload.onprogress = function (ev) {
              prog(ev.loaded, ev.total);
            };
          }
          x.onload = function () {
            var j;
            try {
              j = JSON.parse(x.responseText);
            } catch (e) {
              j = { error: 'HTTP', text: TX.noans + ' HTTP ' + x.status };
            }
            done({ ok: x.status >= 200 && x.status < 300, status: x.status,
              j: j });
          };
          x.onerror = x.onabort = function () {
            done({ ok: false, status: 0, j: { error: 'NET', text: TX.noans } });
          };
          x.send(body);
        });
      });
    }

    // @p make() sends one write; PANEL_BUSY waits RETRY_MS and sends it
    // again, RETRIES times at most.
    function retried(make) {
      function attempt(n) {
        return make().then(function (a) {
          if (!a.ok && a.j.error === 'PANEL_BUSY' && n < RETRIES) {
            return new Promise(function (r) {
              setTimeout(r, RETRY_MS);
            }).then(function () {
              return attempt(n + 1);
            });
          }
          return a;
        });
      }
      return attempt(0);
    }

    function post(what, fields) {
      return retried(function () {
        return send('POST', '/api/' + what, form(fields),
          { 'Content-Type': 'application/x-www-form-urlencoded' });
      });
    }

    function put(path, mount, blob, hdr, prog) {
      return retried(function () {
        return send('PUT', '/api/file?path=' + F.encPath(path) + '&mount=' +
          mount, blob, Object.assign({
          'Content-Type': 'application/octet-stream' }, hdr), prog);
      });
    }

    // The panes after a write: both read again, so a move shows in both.
    function after(text) {
      closeBox();
      c.msg(text);
      return c.rescan().then(function () {
        c.msg(text);
      });
    }

    // A refusal: the access code form for WRITES_CODE, the status read
    // again for the setting's and the gate's refusals, its text shown in
    // the box when one is open.
    function refused(a) {
      var e = a.j.error;
      if (e === 'WRITES_CODE') {
        codeBox(a.j.text);
        return null;
      }
      if (/^WRITES_|^ARMED$|^LOGGING$|^CARD_GONE$|^NO_CARD$/.test(e)) {
        closeBox();
        c.msg(a.j.text);
        return c.rescan().then(function () {
          c.msg(a.j.text);
        });
      }
      if (!ui.box.hidden) {
        busy(false);
        ui.bar.hidden = true;
        ui.err.textContent = a.j.text;
        return null;
      }
      c.msg(a.j.text);
      return null;
    }

    function codeBox(text) {
      box(TX.code, [text]);
      ui.go.hidden = true;
      ui.code.hidden = false;
      ui.codeIn.value = '';
      ui.codeIn.focus();
    }

    // -------------------------------------------------------- actions

    function act(what, fn) {
      return function () {
        var why = whyNot(what);
        if (why) {
          c.msg(why);
          return;
        }
        c.msg('');
        fn();
      };
    }

    function upload(file, replace) {
      var p = pane();
      var path = F.join(p.path, file.name);
      if (c.st.status && file.size > c.st.status.file_max) {
        c.msg(TX['e.TOO_LARGE']);
        return;
      }
      box(F.fmt(TX.sending, file.name), []);
      ui.go.hidden = true;
      ui.bar.hidden = false;
      ui.bar.value = 0;
      ui.cancel.disabled = true;
      put(path, mountOf(p), file, replace ? {} : { 'If-None-Match': '*' },
        function (done, total) {
          ui.bar.value = total > 0 ? done / total : 0;
        }).then(function (a) {
        if (a.ok) {
          return after(a.j.note === 'ESC_NEXT_START' ? TX.savedesc
            : F.fmt(TX.saved, file.name));
        }
        if (a.j.error === 'EXISTS' && !replace) {
          ui.bar.hidden = true;
          ui.cancel.disabled = false;
          ask(TX.upload, [F.fmt(TX.replaceq, file.name)], TX.replace,
            function () {
              upload(file, true);
            });
          return null;
        }
        return refused(a);
      });
    }

    function newDir() {
      var p = pane();
      askName(TX.newdir, '', TX['new'], function (n) {
        busy(true);
        post('mkdir', { path: F.encPath(F.join(p.path, n)),
          mount: mountOf(p) }).then(function (a) {
          return a.ok ? after(F.fmt(TX.made, n)) : refused(a);
        });
      });
    }

    function newFile() {
      var p = pane();
      askName(TX.newfile, '', TX['new'], function (n) {
        busy(true);
        put(F.join(p.path, n), mountOf(p), '', { 'If-None-Match': '*' })
          .then(function (a) {
            return a.ok ? after(F.fmt(TX.made, n)) : refused(a);
          });
      });
    }

    function rename() {
      var s = selected();
      askName(TX.tkname, s.e.n, TX.rename, function (n) {
        busy(true);
        post('rename', { from: F.encPath(F.join(s.p.path, s.e.n)),
          to: F.encPath(F.join(s.p.path, n)), dir: s.e.d ? 1 : 0,
          mount: mountOf(s.p) }).then(function (a) {
          return a.ok ? after(F.fmt(TX.renamed, s.e.n, n)) : refused(a);
        });
      });
    }

    function move() {
      var s = selected();
      var o = c.st.panes[1 - c.st.active];
      post('rename', { from: F.encPath(F.join(s.p.path, s.e.n)),
        to: F.encPath(F.join(o.path, s.e.n)), dir: s.e.d ? 1 : 0,
        mount: mountOf(s.p) }).then(function (a) {
        return a.ok ? after(F.fmt(TX.moved, s.e.n)) : refused(a);
      });
    }

    function del() {
      var s = selected();
      var lines = [s.e.n, s.e.d ? TX.deldir : TX.delwhat];
      var rep = reportOf(s.e);
      var L = s.p.list;
      if (rep && L && L.entries.some(function (x) {
        return !x.d && x.n.toUpperCase() === rep;
      })) {
        lines.push(F.fmt(TX.delrep, rep));
      }
      lines.push(TX.delfinal);
      ask(TX.deltitle, lines, TX['delete'], function () {
        busy(true);
        post('delete', { path: F.encPath(F.join(s.p.path, s.e.n)),
          dir: s.e.d ? 1 : 0, mount: mountOf(s.p) }).then(function (a) {
          return a.ok ? after(F.fmt(TX.deleted, s.e.n)) : refused(a);
        });
      });
    }

    // ------------------------------------------------------- the build

    function build() {
      var row = c.el('div', 'foot wr');
      ui.upload = c.button(TX.upload, 'btn', act('upload', function () {
        ui.file.value = '';
        ui.file.click();
      }));
      ui.newdir = c.button(TX.newdir, 'btn', act('newdir', newDir));
      ui.newfile = c.button(TX.newfile, 'btn', act('newfile', newFile));
      ui.rename = c.button(TX.rename, 'btn', act('rename', rename));
      ui.move = c.button(TX.move, 'btn', act('move', move));
      ui['delete'] = c.button(TX['delete'], 'btn', act('delete', del));
      ['upload', 'newdir', 'newfile', 'rename', 'move', 'delete']
        .forEach(function (w) {
          row.appendChild(ui[w]);
        });
      ui.file = c.el('input');
      ui.file.type = 'file';
      ui.file.hidden = true;
      ui.file.addEventListener('change', function () {
        if (ui.file.files && ui.file.files.length === 1) {
          upload(ui.file.files[0], false);
        }
      });
      row.appendChild(ui.file);
      c.ui.foot.parentNode.insertBefore(row, c.ui.foot.nextSibling);

      ui.box = c.el('div', 'box wbox');
      ui.box.hidden = true;
      var card = c.el('div', 'card');
      ui.title = c.el('h3');
      ui.body = c.el('div');
      ui.input = c.el('input', 'name');
      ui.input.type = 'text';
      ui.input.maxLength = 59;
      ui.input.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter' && ui.goFn && !ui.go.disabled) {
          ui.goFn();
        }
      });
      // The code form posts to /code itself: the panel answers with the
      // cookie and the page again.
      ui.code = c.el('form', 'code');
      ui.code.method = 'post';
      ui.code.action = '/code';
      ui.codeIn = c.el('input');
      ui.codeIn.name = 'code';
      ui.codeIn.maxLength = 8;
      ui.codeIn.autocomplete = 'off';
      var codeGo = c.el('button', 'btn', TX.codego);
      codeGo.type = 'submit';
      ui.code.appendChild(ui.codeIn);
      ui.code.appendChild(codeGo);
      ui.bar = c.el('progress');
      ui.bar.max = 1;
      ui.err = c.el('p', 'warn');
      var btns = c.el('div', 'foot');
      ui.go = c.button('', 'btn', function () {
        if (ui.goFn) {
          ui.goFn();
        }
      });
      ui.cancel = c.button(TX.cancel, 'btn', closeBox);
      btns.appendChild(ui.go);
      btns.appendChild(ui.cancel);
      [ui.title, ui.body, ui.input, ui.code, ui.bar, ui.err, btns]
        .forEach(function (x) {
          card.appendChild(x);
        });
      ui.box.appendChild(card);
      c.root.appendChild(ui.box);
      document.addEventListener('keydown', function (ev) {
        if (ev.key === 'Escape' && !ui.box.hidden && !ui.cancel.disabled) {
          closeBox();
        }
      });
    }

    build();
    return { footer: footer, whyNot: whyNot };
  }

  return { attach: attach, form: form, reportOf: reportOf };
}());

if (typeof module !== 'undefined') {
  module.exports = RcbWr;
}
