// Windows of a computer drawn over a board scene: a browser page or a
// folder, with files that download or copy, buttons, a progress bar, log
// lines and a pointer that moves, clicks and drags.  A window is
// {from, to, at: [x, y], w, kind, title, url, side, body, pointer}; an item
// of "side" or "body" is one of
//   {h: text}, {p: text}, {log: text}        a heading, a text, a mono line
//   {file: name, note, get: [t0, t1]}        a file; over "get" its bar fills
//   {button: text, style, on}                a button, live from "on"
//   {bar: [t0, t1], text}                    a progress bar with its percent
//   {drive: name, note}                      a drive of a folder's side pane
//   {dialog: title, rows: [{text, id}]}      a list laid over the window
// each with an optional "id", "from" and "to".  A window with "page",
// {w, shots: [[t, name], ...]}, shows captures of a real web page in
// place of items: the picture "name" from t on, cross-faded over 0.2s; its
// pointer keys take a place [x, y] on the page, w CSS pixels wide, where
// an item's id stands.
//
// The kind "desktop" is a Windows desktop, {from, to, at, w, h}: wallpaper
// and a taskbar, drawn under the windows that follow it.  The kind
// "explorer" is a File Explorer window on it: {title, path, nav, rows, h,
// copy}.  "path" is the list of the address bar's names, "nav" the side
// pane's entries {text, kind, on}, "rows" the files {file, date, type,
// size, id, from, to}, and "copy" {t: [t0, t1], text} the copy dialog with
// its bar.  A pointer's ids are looked up in every window of the scene, so
// a file is carried from one window into another.  A timed item keeps its
// place while hidden, so the page does not jump.  "pointer" is a list of
// keys [t, id] or [t, id, op]: the pointer is on the item "id" at t; op is
// "click", "grab" or "drop".  Times are seconds from the scene's start.
//
// SPDX-License-Identifier: MIT

const cl = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const ss = x => { x = cl(x); return x * x * (3 - 2 * x); };
const esc = s => String(s).replace(/[&<>]/g, c => (
  {'&': '&amp;', '<': '&lt;', '>': '&gt;'}[c]));

function item(it, lt, clicks) {
  const shown = lt >= (it.from ?? -1e9) && lt < (it.to ?? 1e9);
  const vis = shown ? '' : 'visibility:hidden;';
  const id = it.id ? ` data-id="${esc(it.id)}"` : '';
  const hit = it.id && clicks.some(c => c[1] === it.id && lt >= c[0]
                                   && lt < c[0] + .28);
  if (it.h !== undefined) {
    return `<h3${id} style="${vis}">${it.h}</h3>`;
  }
  if (it.p !== undefined) return `<p${id} style="${vis}">${it.p}</p>`;
  if (it.log !== undefined) {
    return `<pre${id} style="${vis}">${esc(it.log)}</pre>`;
  }
  if (it.file !== undefined) {
    const g = it.get, k = g ? cl((lt - g[0]) / (g[1] - g[0])) : 0;
    const busy = g && lt >= g[0] && lt < g[1], done = g && lt >= g[1];
    return `<div class="file${hit ? ' hit' : ''}${done ? ' done' : ''}"${id}
      style="${vis}"><i></i><span>${esc(it.file)}</span>
      <em>${busy ? Math.round(k * 100) + '%' : done ? (it.done || '✓')
        : esc(it.note || '')}</em>
      <u style="opacity:${busy ? 1 : 0}"><s style="width:${k * 100}%"></s></u>
      </div>`;
  }
  if (it.button !== undefined) {
    const live = lt >= (it.on ?? -1e9);
    return `<div class="btn ${it.style || ''}${live ? '' : ' off'}${
      hit ? ' hit' : ''}"${id} style="${vis}">${esc(it.button)}</div>`;
  }
  if (it.bar !== undefined) {
    const k = cl((lt - it.bar[0]) / (it.bar[1] - it.bar[0]));
    const on = lt >= it.bar[0];
    return `<div class="prog"${id} style="${on ? vis : 'visibility:hidden'}">
      <span>${esc(k >= 1 ? it.done || it.text : it.text)}</span>
      <em>${Math.round(k * 100)}%</em><u><s style="width:${k * 100}%"></s></u>
      </div>`;
  }
  if (it.drive !== undefined) {
    return `<div class="drive${hit ? ' hit' : ''}"${id} style="${vis}"><i></i>
      <span>${esc(it.drive)}</span><em>${esc(it.note || '')}</em></div>`;
  }
  if (it.dialog !== undefined) {
    const rows = it.rows.map(r => `<div class="row${
      clicks.some(c => c[1] === r.id && lt >= c[0]) ? ' hit' : ''}"
      data-id="${esc(r.id || '')}">${esc(r.text)}</div>`).join('');
    return `<div class="dlg" style="${vis}"><b>${esc(it.dialog)}</b>${rows}
      </div>`;
  }
  return '';
}

// Draw the windows of scene sc at its time lt into the layer "into"; the
// pointer goes into the SVG overlay "ov".  a is the scene's opacity.
const ICON = {
  folder: '<i class="ic fo"></i>', drive: '<i class="ic dr"></i>',
  pc: '<i class="ic pc"></i>', file: '<i class="ic fi"></i>',
  web: '<i class="ic we"></i>', text: '<i class="ic tx"></i>',
};

// A File Explorer window's inside.
function explorer(w, lt, clicks) {
  const shown = it => lt >= (it.from ?? -1e9) && lt < (it.to ?? 1e9);
  const nav = (w.nav || []).map(n => `<div class="nv${n.on ? ' on' : ''}"
    style="${shown(n) ? '' : 'visibility:hidden'}">${
    ICON[n.kind || 'folder']}<span>${esc(n.text)}</span></div>`).join('');
  const rows = (w.rows || []).map(r => `<div class="rw${
    clicks.some(c => c[1] === r.id && lt >= c[0] && lt < c[0] + .5)
      ? ' on' : ''}" data-id="${esc(r.id || '')}"
    style="${shown(r) ? '' : 'visibility:hidden'}">${ICON[r.kind || 'file']}
    <span>${esc(r.file)}</span><em>${esc(r.date || '')}</em>
    <em>${esc(r.type || '')}</em><em class="sz">${esc(r.size || '')}</em>
    </div>`).join('');
  const c = w.copy;
  const k = c ? cl((lt - c.t[0]) / (c.t[1] - c.t[0])) : 0;
  const dlg = c && lt >= c.t[0] && lt < c.t[1] + .3
    ? `<div class="cp"><b>${Math.round(k * 100)}% complete</b>
       <p>${c.text}</p><u><s style="width:${k * 100}%"></s></u></div>` : '';
  const count = (w.rows || []).filter(shown).length;
  return `<div class="xt"><div class="tab">${ICON[w.icon || 'folder']}
      <span>${esc(w.title)}</span><b>✕</b></div><span class="plus">+</span>
      <span class="ctl">–</span><span class="ctl">☐</span>
      <span class="ctl">✕</span></div>
    <div class="xa"><span class="ar">← → ↑ ⟳</span><div class="crumb">${
      (w.path || []).map(esc).join(' <i>›</i> ')}</div>
      <div class="find">Search ${esc(w.title)}</div></div>
    <div class="xb">⊕ New ⌄ &nbsp;│&nbsp; ✂ &nbsp; ⧉ &nbsp; ⎘ &nbsp; ✎
      &nbsp; 🗑 &nbsp;│&nbsp; ↑↓ Sort ⌄ &nbsp; ☰ View ⌄ &nbsp; ···</div>
    <div class="xm" style="height:${w.h || 300}px"><div class="xn">${nav}
      </div><div class="xl" data-id="${esc(w.id || '')}"><div class="hd">
      <span>Name</span><em>Date modified</em><em>Type</em>
      <em class="sz">Size</em></div>${rows}${dlg}</div></div>
    <div class="xs">${count} item${count === 1 ? '' : 's'}</div>`;
}

export function drawWindows(sc, lt, a, into, ov, imgs = {}) {
  for (const w of sc.windows || []) {
    if (w.kind === 'desktop') {
      const o = Math.min(ss((lt - w.from) / .45), ss((w.to - lt) / .35)) * a;
      if (o <= 0) continue;
      const el = document.createElement('div');
      el.className = 'desk';
      Object.assign(el.style, {left: w.at[0] + 'px', top: w.at[1] + 'px',
                               width: w.w + 'px', height: w.h + 'px',
                               opacity: o});
      el.innerHTML = `<div class="tb"><i class="st"></i><span>🔍 Search
        </span>${ICON.folder}<i class="ap a1"></i><i class="ap a2"></i>
        <em>${esc(w.clock || '21:40')}</em></div>`;
      into.appendChild(el);
      continue;
    }
    const o = Math.min(ss((lt - w.from) / .45), ss((w.to - lt) / .35)) * a;
    if (o <= 0) continue;
    const keys = w.pointer || [];
    const clicks = keys.filter(k => k[2] === 'click' || k[2] === 'drop');
    const el = document.createElement('div');
    el.className = 'win ' + (w.kind || 'browser');
    el.style.left = w.at[0] + 'px';
    el.style.top = w.at[1] + 'px';
    el.style.width = w.w + 'px';
    el.style.opacity = o;
    el.style.transform = `translateY(${(1 - o) * 18}px)`;
    const body = (w.body || []).map(it => item(it, lt, clicks)).join('');
    const side = w.side
      ? `<div class="side">${w.side.map(it => item(it, lt, clicks)).join('')
        }</div>` : '';
    let inner = `<div class="in">${side}<div class="body">${body}</div></div>`;
    if (w.kind === 'explorer') inner = explorer(w, lt, clicks);
    if (w.page) {
      const k = w.page.shots;
      let n = 0;
      while (n < k.length - 1 && lt >= k[n + 1][0]) n++;
      const mix = n ? ss((lt - k[n][0]) / .2) : 1;
      inner = `<div class="page"><img src="${
        imgs[k[Math.max(0, n - 1)][1]].src}" alt=""><img src="${
        imgs[k[n][1]].src}" alt="" style="opacity:${mix}"></div>`;
    }
    el.innerHTML = w.kind === 'explorer' ? inner
      : `<div class="top"><i></i><i></i><i></i>${
      w.url ? `<span class="url">${esc(w.url)}</span>`
        : `<b>${esc(w.title || '')}</b>`}</div>${inner}`;
    into.appendChild(el);
    if (!keys.length || lt < keys[0][0] - .5 || lt > keys.at(-1)[0] + .9) {
      continue;
    }
    // The pointer: on an item's centre at its key, eased between keys.
    const centre = id => {
      if (Array.isArray(id)) {
        const r = el.querySelector('.page').getBoundingClientRect();
        const f = r.width / w.page.w;
        return [r.left + id[0] * f, r.top + id[1] * f];
      }
      const t = id && (el.querySelector(`[data-id="${id}"]`)
                       || into.querySelector(`[data-id="${id}"]`));
      const r = (t || el).getBoundingClientRect();
      return [r.left + r.width / 2, r.top + r.height * (t ? .5 : .3)];
    };
    let i = 0;
    while (i < keys.length - 1 && lt >= keys[i + 1][0]) i++;
    const p0 = centre(keys[i][1]);
    let [x, y] = p0;
    if (lt < keys[0][0]) {
      const f = ss((lt - keys[0][0] + .5) / .5);
      x = p0[0] + (1 - f) * 150;
      y = p0[1] + (1 - f) * 110;
    } else if (i < keys.length - 1) {
      // The way to the next key takes its last 0.8s at most.
      const t1 = keys[i + 1][0], t0 = Math.max(keys[i][0], t1 - .8);
      const f = ss((lt - t0) / (t1 - t0)), p1 = centre(keys[i + 1][1]);
      x = p0[0] + (p1[0] - p0[0]) * f;
      y = p0[1] + (p1[1] - p0[1]) * f;
    }
    let g = '';
    // A file under the pointer between "grab" and "drop".
    const held = keys.findLast(k => lt >= k[0] && (k[2] === 'grab'
                                                    || k[2] === 'drop'));
    if (held && held[2] === 'grab') {
      const name = (sc.windows || []).flatMap(x => [
        ...(x.body || []), ...(x.rows || [])]).find(
        it => it.id === held[1])?.file || '';
      g += `<g transform="translate(${x + 18} ${y + 22})"><rect width="${
        name.length * 13.4 + 56}" height="52" rx="11" fill="#1b2740"
        stroke="#00e5ff" stroke-opacity=".7"/><text x="42" y="34"
        font-family="Inter, sans-serif" font-size="23" font-weight="600"
        fill="#e8f0fb">${esc(name)}</text><rect x="14" y="13" width="17"
        height="26" rx="3" fill="#9fb4e6"/></g>`;
    }
    for (const c of keys) {
      const q = (lt - c[0]) / .45;
      if (c[2] && q >= 0 && q < 1) {
        const [cx, cy] = centre(c[1]);
        g += `<circle cx="${cx}" cy="${cy}" r="${8 + q * 30}" fill="none"
          stroke="#00e5ff" stroke-width="3" stroke-opacity="${1 - q}"/>`;
      }
    }
    g += `<path transform="translate(${x} ${y})" d="M0 0L0 27L7 20.5L12 31.5
      L16.5 29.5L11.5 18.5L21 18.5Z" fill="#fff" stroke="#0a0f18"
      stroke-width="2" stroke-linejoin="round"/>`;
    ov.insertAdjacentHTML('beforeend', `<g opacity="${o}">${g}</g>`);
  }
}
