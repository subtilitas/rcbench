// A figure of plots and circuits, drawn as SVG from the file
// tools/video_scope.py writes: traces as polylines over a time axis whose
// view moves, lines, bands and sample points on them, and a circuit whose
// parts appear, switch and carry a drawn current.  A storyboard's scope
// scene places panels on the frame; each panel is a plot or a circuit.
//
// SPDX-License-Identifier: MIT

const cl = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const ss = x => { x = cl(x); return x * x * (3 - 2 * x); };
const ramp = (t, a, b) => ss((t - a) / (b - a));
const win = (t, a, b, f = .35) => Math.min(ramp(t, a, a + f),
                                           1 - ramp(t, b - f, b));
const span = (o, t, f) => o.from === undefined ? 1 : win(t, o.from, o.to, f);
// A value over [[t, v], ...], eased between the keys; v a number or a
// list of numbers.
const track = (keys, t) => {
  let i = 0;
  while (i < keys.length - 2 && t > keys[i + 1][0]) i++;
  if (keys.length === 1) return keys[0][1];
  const a = keys[i], b = keys[i + 1], f = ss((t - a[0]) / (b[0] - a[0]));
  return Array.isArray(a[1]) ? a[1].map((v, n) => v + (b[1][n] - v) * f)
                             : a[1] + (b[1] - a[1]) * f;
};
const COL = {'': '#00e5ff', warn: '#ffb400', ok: '#00f58c',
             muted: '#93a1b8', red: '#ff5d6c', white: '#eef3fb',
             violet: '#b48cff'};
const INK = '#c9d3e4', DIM = 'rgba(255,255,255,.10)';
const f1 = v => Math.round(v * 10) / 10;
const HALO = 'paint-order="stroke" stroke="#0f1626" stroke-width="7" '
  + 'stroke-linejoin="round"';

// A step of 1, 2 or 5 times a power of ten that gives about n ticks.
function step(width, n) {
  const raw = width / n, p = Math.pow(10, Math.floor(Math.log10(raw)));
  const m = raw / p;
  return (m < 1.5 ? 1 : m < 3.5 ? 2 : m < 7.5 ? 5 : 10) * p;
}
// A time in ns, in the unit that suits the width of the view.
function clock(ns, width) {
  const [div, unit] = width >= 2e9 ? [1e9, 's'] : width >= 2e6
    ? [1e6, 'ms'] : width >= 2e3 ? [1e3, 'µs'] : [1, 'ns'];
  const v = ns / div;
  return (Math.abs(v) >= 100 || Number.isInteger(+v.toFixed(2))
    ? String(+v.toFixed(2)) : v.toFixed(Math.abs(v) < 10 ? 2 : 1))
    + ' ' + unit;
}

function drawPlot(p, fig, lt, a) {
  const [bx, by, bw, bh] = p.box;
  const padL = 86, padR = 24, padT = p.heading ? 58 : 20, padB = 54;
  const x0 = bx + padL, y0 = by + padT, w = bw - padL - padR,
        h = bh - padT - padB;
  const [v0, v1] = track(p.view, lt);
  const [lo, hi] = p.y;
  const X = v => x0 + (v - v0) / (v1 - v0) * w;
  const Y = v => y0 + h - (v - lo) / (hi - lo) * h;
  const id = 'clip-' + p.id;
  let s = `<g opacity="${a}"><rect x="${bx}" y="${by}" width="${bw}"
    height="${bh}" rx="22" fill="rgba(18,26,42,.86)" stroke="${DIM}"/>
    <clipPath id="${id}"><rect x="${x0}" y="${y0 - 6}" width="${w}"
    height="${h + 12}"/></clipPath>`;
  if (p.heading) {
    s += `<text x="${bx + 28}" y="${by + 40}" fill="#eef3fb" font-size="25"
      font-weight="700">${p.heading}</text>`;
  }
  // The grid and its numbers.
  const dx = step(v1 - v0, 7);
  for (let v = Math.ceil(v0 / dx) * dx; v <= v1 + dx * 1e-6; v += dx) {
    s += `<line x1="${f1(X(v))}" y1="${y0}" x2="${f1(X(v))}" y2="${y0 + h}"
        stroke="${DIM}"/>
      <text x="${f1(X(v))}" y="${y0 + h + 32}" fill="#93a1b8"
        font-size="20" text-anchor="middle">${clock(v, v1 - v0)}</text>`;
  }
  for (const v of p.ticks || []) {
    const text = Array.isArray(v) ? v[1] : `${v} ${p.unit || ''}`;
    const at = Array.isArray(v) ? v[0] : v;
    s += `<line x1="${x0}" y1="${f1(Y(at))}" x2="${x0 + w}"
        y2="${f1(Y(at))}" stroke="${DIM}"/>
      <text x="${x0 - 14}" y="${f1(Y(at)) + 7}" fill="#93a1b8"
        font-size="20" text-anchor="end">${text}</text>`;
  }
  s += `<g clip-path="url(#${id})">`;
  for (const b of p.bands || []) {
    const o = span(b, lt);
    if (o <= 0) continue;
    const col = COL[b.style || ''];
    const xa = X(b.x[0]), xb = X(b.x[1]);
    s += `<rect x="${f1(xa)}" y="${y0}" width="${f1(xb - xa)}"
        height="${h}" fill="${col}" opacity="${.13 * o}"/>`;
    if (b.text) {
      s += `<text x="${f1((xa + xb) / 2)}" y="${y0 + (b.dy || 30)}"
        fill="${col}" opacity="${o}" font-size="21" font-weight="700"
        ${HALO} text-anchor="middle">${b.text}</text>`;
    }
  }
  for (const l of p.lines || []) {
    const o = span(l, lt);
    if (o <= 0) continue;
    const col = COL[l.style || 'muted'];
    if (l.y !== undefined) {
      s += `<line x1="${x0}" y1="${f1(Y(l.y))}" x2="${x0 + w}"
          y2="${f1(Y(l.y))}" stroke="${col}" stroke-width="2.5"
          stroke-dasharray="10 8" opacity="${o}"/>
        <text x="${x0 + w - 12}" y="${f1(Y(l.y)) + 28}" fill="${col}"
          opacity="${o}" font-size="21" font-weight="700" ${HALO}
          text-anchor="end">${l.text || ''}</text>`;
    } else {
      s += `<line x1="${f1(X(l.x))}" y1="${y0}" x2="${f1(X(l.x))}"
          y2="${y0 + h}" stroke="${col}" stroke-width="2.5"
          stroke-dasharray="10 8" opacity="${o}"/>
        <text x="${f1(X(l.x)) + (l.left ? -10 : 10)}"
          y="${y0 + (l.dy || 26)}" fill="${col}" opacity="${o}"
          font-size="21" font-weight="700" ${HALO}
          text-anchor="${l.left ? 'end' : 'start'}">${l.text || ''}</text>`;
    }
  }
  for (const tr of p.traces || []) {
    const o = span(tr, lt);
    if (o <= 0) continue;
    const d = fig.series[tr.series];
    const [a0, a1] = tr.lane || [lo, hi];       // a logic trace's rows
    const yy = tr.lane ? v => Y(a0 + v * (a1 - a0)) : Y;
    const end = tr.reveal ? track(tr.reveal, lt) : Infinity;
    const xs = d.x, ys = d.y, off = tr.shift || 0;
    const from = v0 - off, to = Math.min(v1, end) - off;
    // The points inside the view and one on either side.
    let i = 0, j = xs.length - 1, n = xs.length - 1;
    for (let l = 0, r = n; l <= r;) {
      const m = (l + r) >> 1;
      if (xs[m] < from) { i = m; l = m + 1; } else r = m - 1;
    }
    for (let l = 0, r = n; l <= r;) {
      const m = (l + r) >> 1;
      if (xs[m] > to) { j = m; r = m - 1; } else l = m + 1;
    }
    if (tr.points) {
      const col = COL[tr.style || ''];
      for (let k = i; k <= j; k++) {
        if (xs[k] < from || xs[k] > to) continue;
        const v = tr.on ? fig.series[tr.on] : null;
        const py = v ? Y(valueAt(v, xs[k])) : yy(ys[k]);
        s += `<circle cx="${f1(X(xs[k] + off))}" cy="${f1(py)}"
          r="${tr.r || 6}" fill="${ys[k] ? col : '#070b14'}"
          stroke="${col}" stroke-width="2.5" opacity="${o}"/>`;
      }
      continue;
    }
    let pts = '';
    for (let k = i; k <= j; k++) {
      let x = xs[k], y = ys[k];
      if (x > to && k > 0) {      // cut the trace at the reveal's end
        const q = (to - xs[k - 1]) / (x - xs[k - 1]);
        y = ys[k - 1] + (y - ys[k - 1]) * cl(q);
        x = to;
      }
      pts += `${f1(X(x + off))},${f1(yy(y))} `;
    }
    s += `<polyline points="${pts}" fill="none"
      stroke="${COL[tr.style || '']}" stroke-width="${tr.width || 3.5}"
      stroke-linejoin="round" opacity="${o}"/>`;
  }
  s += '</g>';
  // The names of the traces, in the plot's corner.
  let ly = by + 40;
  for (const tr of p.traces || []) {
    const o = span(tr, lt);
    if (!tr.label || o <= 0) continue;
    const col = COL[tr.style || ''];
    s += `<g opacity="${o}"><text x="${x0 + w - 14}" y="${ly}"
        fill="${col}" font-size="21" font-weight="700" ${HALO}
        text-anchor="end">— ${tr.label}</text></g>`;
    ly += 30;
  }
  if (p.ylabel) {
    s += `<text x="${bx + 26}" y="${y0 + h / 2}" fill="#93a1b8"
      font-size="20" text-anchor="middle"
      transform="rotate(-90 ${bx + 26} ${y0 + h / 2})">${p.ylabel}</text>`;
  }
  p.project = (x, y) => [X(x), Y(y)];
  return s + '</g>';
}

function valueAt(d, x) {
  let l = 0, r = d.x.length - 1;
  while (r - l > 1) {
    const m = (l + r) >> 1;
    if (d.x[m] <= x) l = m; else r = m;
  }
  const q = d.x[r] === d.x[l] ? 0 : (x - d.x[l]) / (d.x[r] - d.x[l]);
  return d.y[l] + (d.y[r] - d.y[l]) * cl(q);
}

// A part between two points, drawn along the line from a to b: f gives
// the part in a frame whose x runs along the line, over the length len.
function along(a, b, f) {
  const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
  const ang = Math.atan2(b[1] - a[1], b[0] - a[0]) * 180 / Math.PI;
  return `<g transform="translate(${a[0]} ${a[1]}) rotate(${ang})">`
    + f(len) + '</g>';
}

function drawCircuit(p, fig, lt, a, t) {
  const c = fig.circuits[p.circuit];
  const [bx, by, bw, bh] = p.box;
  const k = Math.min(bw / c.size[0], bh / c.size[1]);
  const ox = bx + (bw - c.size[0] * k) / 2, oy = by + (bh - c.size[1] * k) / 2;
  const val = (name, id, def) => p[name] && p[name][id]
    ? track(p[name][id], lt) : def;
  let s = `<g opacity="${a}"><rect x="${bx}" y="${by}" width="${bw}"
    height="${bh}" rx="22" fill="rgba(18,26,42,.86)" stroke="${DIM}"/>
    <g transform="translate(${ox} ${oy}) scale(${k})" fill="none"
      stroke-linecap="round" stroke-linejoin="round">`;
  const text = (x, y, str, col = INK, anchor = 'start', size = 20) =>
    `<text x="${x}" y="${y}" fill="${col}" stroke="none"
      font-size="${size}" font-weight="600"
      text-anchor="${anchor}">${str}</text>`;
  for (const it of c.items) {
    const o = val('show', it.id, 1);
    if (o <= 0) continue;
    const lit = val('lit', it.id, 0);
    const col = lit > 0 ? mix(INK, COL[(p.tint || {})[it.id] || ''], lit)
                        : INK;
    s += `<g opacity="${o}" stroke="${col}" stroke-width="3.5">`;
    if (it.t === 'box') {
      const [x, y, w, h] = it.r;
      s += `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="16"
          stroke="rgba(255,255,255,.22)" stroke-width="2"
          stroke-dasharray="9 7" fill="rgba(255,255,255,.03)"/>`
        + text(x + 16, y + h - 16, it.text, '#93a1b8');
    } else if (it.t === 'wire') {
      s += `<polyline points="${it.p.map(q => q.join(',')).join(' ')}"/>`;
      if (it.text) {
        s += text(it.at[0], it.at[1], it.text, '#93a1b8', 'middle');
      }
    } else if (it.t === 'dot') {
      s += `<circle cx="${it.at[0]}" cy="${it.at[1]}" r="7" fill="${col}"/>`;
    } else if (it.t === 'rail') {
      s += `<line x1="${it.at[0] - 26}" y1="${it.at[1]}"
          x2="${it.at[0] + 26}" y2="${it.at[1]}"/>`
        + text(it.at[0], it.at[1] - 14, it.text, col, 'middle');
    } else if (it.t === 'gnd') {
      const [x, y] = it.at;
      s += `<path d="M${x} ${y}v18M${x - 22} ${y + 18}h44M${x - 14} ${y + 27}
        h28M${x - 6} ${y + 36}h12"/>`;
    } else if (it.t === 'res') {
      s += along(it.a, it.b, len => {
        const m = len / 2;
        return `<path d="M0 0H${m - 42}l7 -13l14 26l14 -26l14 26l14 -26
          l14 26l7 -13H${len}"/>`;
      }) + (it.label ? text(it.label[0], it.label[1], it.text, col, 'middle')
        : text(Math.max(it.a[0], it.b[0]) + 24,
               (it.a[1] + it.b[1]) / 2 + 7, it.text, col));
    } else if (it.t === 'zener') {
      // A zener diode from its anode a to its cathode b.
      s += along(it.a, it.b, len => {
        const m = len / 2;
        return `<path d="M0 0H${m - 18}M${m - 18} -20L${m + 18} 0
          L${m - 18} 20ZM${m + 18} 0H${len}M${m + 26} -24L${m + 18} -18
          V18L${m + 10} 24"/>`;
      }) + text(Math.min(it.a[0], it.b[0]) - 34,
                (it.a[1] + it.b[1]) / 2 + 7, it.text, col, 'end');
    } else if (it.t === 'cap') {
      s += along(it.a, it.b, len => {
        const m = len / 2;
        return `<path d="M0 0H${m - 8}M${m - 8} -24v48M${m + 8} -24v48
          M${m + 8} 0H${len}"/>`;
      }) + text(Math.max(it.a[0], it.b[0]) + 34,
                (it.a[1] + it.b[1]) / 2 + 7, it.text, col);
    } else if (it.t === 'sw') {
      // A switch: its arm swings shut as its value goes from 0 to 1.
      const shut = val('set', it.id, 0);
      s += along(it.a, it.b, len => {
        const m = len / 2, ang = -32 * (1 - shut);
        return `<path d="M0 0H${m - 30}M${m + 30} 0H${len}"/>
          <circle cx="${m - 30}" cy="0" r="6" fill="${col}"/>
          <circle cx="${m + 30}" cy="0" r="6" fill="${col}"/>
          <line x1="${m - 30}" y1="0" x2="${m + 30}" y2="0"
            transform="rotate(${ang} ${m - 30} 0)"
            stroke="${shut > .5 ? COL.ok : COL.warn}" stroke-width="5"/>`;
      }) + text(it.a[0] + (it.side === 'r' ? 30 : -30),
                (it.a[1] + it.b[1]) / 2 + 7, it.text, col,
                it.side === 'r' ? 'start' : 'end');
    } else if (it.t === 'buf') {
      s += along(it.a, it.b, len => {
        const m = len / 2;
        return `<path d="M0 0H${m - 26}M${m - 26} -26L${m + 26} 0
          L${m - 26} 26ZM${m + 26} 0H${len}"/>`;
      }) + text(it.a[0] - 30, (it.a[1] + it.b[1]) / 2 + 7, it.text, col,
                'end');
    }
    s += '</g>';
  }
  // A current: dashes that run along a path.
  for (const fl of p.flow || []) {
    const o = win(lt, fl.from, fl.to, .3);
    if (o <= 0) continue;
    const pts = c.paths[fl.path].map(q => q.join(',')).join(' ');
    s += `<polyline points="${pts}" stroke="${COL[fl.style || '']}"
      stroke-width="9" stroke-dasharray="4 30"
      stroke-dashoffset="${-((t * (fl.speed || 150)) % 34)}"
      opacity="${o}"/>`;
  }
  // A level beside a node: a number that follows a track.
  for (const m of p.meters || []) {
    const o = span(m, lt);
    if (o <= 0) continue;
    const [x, y] = c.nodes[m.node];
    const v = track(m.value, lt);
    s += `<g opacity="${o}"><rect x="${x + (m.dx || 20)}"
        y="${y + (m.dy || -62)}" width="126" height="44" rx="12"
        fill="#070b14" stroke="${COL[m.style || '']}" stroke-width="2"/>`
      + text(x + (m.dx || 20) + 63, y + (m.dy || -62) + 31,
             v.toFixed(2) + ' V', COL[m.style || ''], 'middle', 24)
      + '</g>';
  }
  p.project = name => {
    const [x, y] = c.nodes[name];
    return [ox + x * k, oy + y * k];
  };
  return s + '</g></g>';
}

function mix(a, b, f) {
  const h = c => [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16));
  const [p, q] = [h(a), h(b)];
  return '#' + p.map((v, i) => Math.round(v + (q[i] - v) * f)
    .toString(16).padStart(2, '0')).join('');
}

// Draw a scope scene's panels at the scene's time lt; t is the video's
// time, which moves the currents.
export function drawScope(svg, sc, fig, lt, a, t) {
  let s = '';
  for (const p of sc.panels) {
    const o = span(p, lt, .45) * a;
    if (o <= 0) { p.project = null; continue; }
    s += p.kind === 'plot' ? drawPlot(p, fig, lt, o)
                           : drawCircuit(p, fig, lt, o, t);
  }
  svg.innerHTML = s;
}

// A label's anchor: [x, y] on the frame, [panel, x, y] on a plot or
// [panel, node] on a circuit.
export function projectScope(sc, anchor) {
  if (typeof anchor[0] !== 'string') return anchor;
  const p = sc.panels.find(q => q.id === anchor[0]);
  if (!p || !p.project) return [-100, -100];
  return anchor.length === 3 ? p.project(anchor[1], anchor[2])
                             : p.project(anchor[1]);
}
