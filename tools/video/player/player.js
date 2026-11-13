// Draws one frame of a storyboard.  window.renderAt(t) sets every element
// from t alone, so frames can be captured in any order and at any rate.
// A storyboard is a list of scenes: "case" (the 3D model with labels),
// "board" (a circuit board seen flat, with rework drawn on it),
// "screen" (panel screenshots with pointer bubbles), "tour" (panel
// screenshots with a caption and a spotlight) and "end" (a recap).
// tools/video/README.md describes the fields.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {buildCase, CONNECTORS, WIRE_COLORS} from './case3d.js';
import {buildCadCase, loadCad} from './case_cad.js';
import {buildBoard} from './board.js';

const $ = s => document.querySelector(s);
const cl = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const ss = x => { x = cl(x); return x * x * (3 - 2 * x); };
const ramp = (t, a, b) => ss((t - a) / (b - a));
const win = (t, a, b, f = .35) => Math.min(ramp(t, a, a + f),
                                           1 - ramp(t, b - f, b));
// A piecewise value over [[t, v], ...], eased between the keys.
const track = (keys, t) => {
  if (!keys || !keys.length) return 0;
  let i = 0;
  while (i < keys.length - 2 && t > keys[i + 1][0]) i++;
  if (keys.length === 1) return keys[0][1];
  const a = keys[i], b = keys[i + 1];
  return a[1] + (b[1] - a[1]) * ss((t - a[0]) / (b[0] - a[0]));
};
const loadImage = src => new Promise((ok, bad) => {
  const i = new Image();
  i.onload = () => ok(i);
  i.onerror = () => bad(new Error('missing ' + src));
  i.src = src;
});

const SX = 67, SY = 187, SC = 1.25;   // the screenshot on the frame
const FADE = .45;
const COL = {'': '#00e5ff', warn: '#ffb400', ok: '#00f58c',
             muted: '#93a1b8'};

const sb = await (await fetch('/storyboard.json')).json();
const words = {tap: 'TAP', hold: 'HOLD 2 s', pos: 'pos', gpio: 'GPIO',
               ...(sb.words || {})};

// Lay the scenes out on the timeline and load what they show.
let acc = 0;
const imgs = {};
const need = new Set();
for (const sc of sb.scenes) {
  sc.t = acc;
  if (sc.type === 'screen' || sc.type === 'tour') {
    let a = 0;
    for (const st of sc.steps) {
      st.t = a;
      a += st.dur;
      if (st.card) continue;
      st.shot = st.lang ? `${st.view}@${st.lang}` : st.view;
      need.add(st.shot);
    }
    sc.dur = a;
  }
  if (sc.type === 'tour') {
    // Each chapter's span of the scene, for the bar under its name.
    sc.span = {};
    for (const st of sc.steps) {
      const s = sc.span[st.n] || (sc.span[st.n] = [st.t, 0]);
      s[1] = st.t + st.dur;
    }
  }
  if (sc.type === 'case' && sc.display) need.add(sc.display.view);
  acc += sc.dur;
}
const TOTAL = acc;
await Promise.all([...need].map(async v => {
  imgs[v] = await loadImage('/shots/' + v + '.png');
}));

const renderer = new THREE.WebGLRenderer(
  {canvas: $('#gl'), antialias: true, alpha: true,
   preserveDrawingBuffer: true});
renderer.setSize(1920, 1080, false);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.15;
for (const sc of sb.scenes) {
  if (sc.type === 'board') {
    sc.plan = await (await fetch(`/cad/${sc.board}.json`)).json();
  }
  if (sc.type !== 'case') continue;
  const shown = sc.display ? imgs[sc.display.view] : null;
  // "cad" is the assembly tools/video_cad.py converts; any other scene
  // draws the mockup.
  sc.rig = sc.model === 'cad'
    ? buildCadCase(shown, await loadCad('/cad/case.glb'))
    : buildCase(shown);
  if (sc.servo) sc.rig.setServoPort(sc.servo.connector, sc.servo.position);
}

$('#brand').innerHTML = `<b>rcbench</b> · ${sb.brand || ''}`;
const dots = $('#dots'), nav = $('#nav');
for (let i = 0; i < (sb.steps || 0); i++) {
  dots.appendChild(document.createElement('i'));
}
// A storyboard with chapter names shows them in place of the step marks.
(sb.chapters || []).forEach((name, i) => {
  nav.insertAdjacentHTML('beforeend', '<div><u><s></s></u>'
    + `<b>${String(i + 1).padStart(2, '0')}</b>${name}</div>`);
});
const bubs = $('#bubs'), ov = $('#ov');

// A card with an optional pointer to (tx, ty).  o.a is its opacity.
function bubble(o) {
  const e = document.createElement('div');
  e.className = 'bub ' + (o.size === 'sm' ? 'sm ' : '') + (o.style || '');
  e.style.left = o.x + 'px';
  e.style.top = o.y + 'px';
  e.style.opacity = o.a;
  e.style.transform = `translateY(${(1 - o.a) * 14}px)`;
  const n = o.n !== undefined && o.n !== '' ? `<span class="n">${o.n}</span>`
                                            : '';
  e.innerHTML = `<div class="h">${n}<span>${o.title}</span></div>`
    + (o.text ? `<p>${o.text}</p>` : '') + (o.tag || '');
  bubs.appendChild(e);
  if (o.tx === undefined) return;
  const r = e.getBoundingClientRect();
  const my0 = r.top + Math.min(60, r.height / 2);
  const [sx, sy] = o.side === 'r' ? [r.right, my0]
    : o.side === 'b' ? [r.left + r.width / 2, r.bottom]
    : o.side === 't' ? [r.left + r.width / 2, r.top] : [r.left, my0];
  const dx = o.tx - sx, dy = o.ty - sy;
  const cx = (sx + o.tx) / 2 - dy * .06, cy = (sy + o.ty) / 2 + dx * .06;
  const len = Math.hypot(dx, dy) * 1.08, col = COL[o.style || ''];
  ov.insertAdjacentHTML('beforeend',
    `<path d="M${sx} ${sy} Q${cx} ${cy} ${o.tx} ${o.ty}" fill="none"
      stroke="${col}" stroke-width="3" stroke-linecap="round"
      stroke-dasharray="${len}" stroke-dashoffset="${len * (1 - o.a)}"
      opacity="${o.a}"/>
     <circle cx="${sx}" cy="${sy}" r="5" fill="${col}" opacity="${o.a}"/>
     <circle cx="${o.tx}" cy="${o.ty}" r="${7 * o.a}" fill="${col}"/>
     <circle cx="${o.tx}" cy="${o.ty}" r="${14 + 10 * o.pulse}" fill="none"
      stroke="${col}" stroke-width="2.5"
      opacity="${(1 - o.pulse) * .8 * o.a}"/>`);
}

function pinout(p) {
  const c = CONNECTORS[p.connector];
  const rows = p.rows || c.rows;
  const sel = i => (i + 1 === p.select ? ' class="sel"' : '');
  let h = `<div class="h">${p.connector} · ${p.title || c.title}</div>`
    + `<div class="s">${p.subtitle || ''}</div><table><tr><th></th>`;
  c.positions.forEach(([name], i) => {
    h += `<th>${words.pos} ${i + 1}<b>${name}</b></th>`;
  });
  rows.forEach((row, r) => {
    h += `</tr><tr><td class="r">${row}</td>`;
    c.positions.forEach((_, i) => {
      h += `<td${sel(i)}><i style="background:${WIRE_COLORS[r]}"></i></td>`;
    });
  });
  h += `</tr><tr><td class="r g">${words.gpio}</td>`;
  c.positions.forEach(([, gpio]) => { h += `<td class="g">${gpio}</td>`; });
  return h + '</tr></table>';
}

function drawCase(sc, lt, a, pulse, ui) {
  const m = sc.rig, gl = $('#gl');
  gl.style.display = 'block';
  gl.style.opacity = a;
  const k = sc.camera;
  let i = 0;
  while (i < k.length - 2 && lt > k[i + 1][0]) i++;
  const f = k.length > 1 ? ss((lt - k[i][0]) / (k[i + 1][0] - k[i][0])) : 0;
  const j = Math.min(i + 1, k.length - 1);
  const v = n => new THREE.Vector3(...k[i][n]).lerp(
    new THREE.Vector3(...k[j][n]), f);
  m.cam.position.copy(v(1));
  m.cam.lookAt(v(2));
  m.setLid(track(sc.lid, lt));
  if (sc.parts && m.setParts) {
    // How far out each component stands at this time: a key track each.
    m.setParts(Object.fromEntries(Object.entries(sc.parts).map(
      ([name, keys]) => [name, track(keys, lt)])));
  }
  if (m.clearGlow) {
    m.clearGlow();
    for (const l of sc.labels || []) {
      if (l.glow) m.setGlow(l.glow, win(lt, l.from, l.to));
    }
  }
  if (sc.servo) {
    const s = sc.servo;
    m.setServo(ramp(lt, s.appear, s.appear + .6),
               46 * (1 - ramp(lt, s.plug[0], s.plug[1])));
  }
  renderer.render(m.scene, m.cam);
  const proj = p => {
    const q = new THREE.Vector3(...p).project(m.cam);
    return [(q.x + 1) * 960, (1 - q.y) * 540];
  };
  drawNotes(sc, lt, a, pulse, ui, p => proj(
    typeof p === 'string' ? m.anchors[p]() : p));
  if (sc.pinout) {
    const p = sc.pinout, pa = win(lt, p.from, p.to) * a, e = $('#pin');
    if (pa > 0 && e.dataset.for !== sc.t + p.connector) {
      e.innerHTML = pinout(p);
      e.dataset.for = sc.t + p.connector;
    }
    e.style.opacity = pa;
    e.style.transform = `translateX(${(1 - pa) * -30}px)`;
  }
}

// The title, the chips and the labels a case and a board scene share.
// proj maps a label's anchor to its frame pixel.
function drawNotes(sc, lt, a, pulse, ui, proj) {
  if (sc.title) {
    const t = sc.title, e = $('#title');
    e.style.opacity = win(lt, t.from, t.to, .5) * a;
    e.style.transform = `translateY(${(1 - ramp(lt, t.from, t.from + .7)) * 24}px)`;
    e.querySelector('.k').textContent = t.kicker || '';
    e.querySelector('h1').innerHTML = t.heading;
    e.querySelector('p').innerHTML = t.text || '';
  }
  for (const l of sc.labels || []) {
    const la = win(lt, l.from, l.to) * a;
    if (la <= 0) continue;
    const p = proj(l.anchor);
    bubble({...l, x: l.at[0], y: l.at[1], a: la, pulse, tx: p[0], ty: p[1]});
  }
  for (const c of sc.chips || []) {
    if (lt >= c.from && lt < c.to) { ui.cn = c.n; ui.ct = c.text; }
    if (lt >= c.from && typeof c.n === 'number') ui.step = c.n;
  }
}

// A cross-section through two boards, drawn from the side.  "strip" shows
// a module on the plastic of its pin strips, the pins soldered on both
// sides, beside the same module lying on the carrier; "fill" shows a wire
// through a large hole of both boards, soldered on both sides.
function section(n) {
  const PIN = '#c8ccd2', TIN = '#cfd4da', PCB = '#1f6a54';
  const top = n.color || '#1d4fa8';
  const txt = (x, y, s, col = '#c9d3e4', size = 17) => `<text x="${x}"
    y="${y}" text-anchor="middle" font-family="Inter, sans-serif"
    font-weight="650" font-size="${size}" fill="${col}">${s || ''}</text>`;
  // One stack: the carrier at y 150, the module gap above it, pins.
  const stack = (x0, gap) => {
    const my = 132 - gap;
    let s = `<rect x="${x0}" y="150" width="250" height="20" rx="3"
      fill="${PCB}"/><rect x="${x0 + 22}" y="${my}" width="206" height="18"
      rx="3" fill="${top}"/>`;
    for (let i = 0; i < 5; i++) {
      const x = x0 + 45 + i * 40;
      if (gap) {
        s += `<rect x="${x - 11}" y="${150 - gap}" width="22"
          height="${gap}" fill="#0c0d10" stroke="#3a3f4a"/>`;
      }
      s += `<rect x="${x - 3}" y="${my - 20}" width="6"
          height="${190 - my + 18}" fill="${PIN}"/>
        <ellipse cx="${x}" cy="${my}" rx="12" ry="7" fill="${TIN}"/>
        <ellipse cx="${x}" cy="170" rx="12" ry="7" fill="${TIN}"/>`;
    }
    return s;
  };
  if (n.kind === 'pins') {
    // Pin strips to cut: one row a strip, its pins drawn one by one, its
    // count and its place beside it.  n.rows holds [count, text].
    const rows = n.rows || [];
    let s = '';
    rows.forEach(([count, said], i) => {
      const y = 34 + i * 40;
      s += `<rect x="14" y="${y - 7}" width="${count * 13 + 6}" height="14"
        rx="3" fill="#15171c" stroke="#3a3f4a"/>`;
      for (let k = 0; k < count; k++) {
        s += `<rect x="${20 + k * 13}" y="${y - 13}" width="6" height="26"
          rx="1.5" fill="#d9b04a"/>`;
      }
      s += `<text x="${count * 13 + 34}" y="${y + 6}"
        font-family="Inter, sans-serif" font-weight="700" font-size="17"
        fill="#fff">${count}<tspan fill="#c9d3e4" font-weight="500"
        dx="10">${said}</tspan></text>`;
    });
    return `<svg viewBox="0 0 600 ${rows.length * 40 + 22}">${s}</svg>`;
  }
  if (n.kind === 'extract') {
    // A plug's housing cut open: the contact's tongue stands in the
    // housing's window; a needle presses it down, the wire leaves to
    // the back.
    return `<svg viewBox="0 20 600 200">
      <rect x="110" y="92" width="300" height="76" rx="6" fill="#e9e4d6"/>
      <rect x="230" y="92" width="64" height="16" fill="#0e1420"/>
      <rect x="130" y="124" width="250" height="14" fill="${PIN}"/>
      <path d="M236 124l50 -22l4 8l-36 14z" fill="${PIN}"/>
      <rect x="380" y="121" width="190" height="20" rx="10"
        fill="${n.color || '#e8c81a'}"/>
      <path d="M262 30l8 0l-2 72l-4 0z" fill="#aeb4bd"/>
      <path d="M266 44v40m-9 -12l9 12l9 -12" fill="none" stroke="#00f58c"
        stroke-width="4" stroke-linecap="round" stroke-linejoin="round"
        transform="translate(-34 0)"/>
      <path d="M450 172h84m-14 -10l14 10l-14 10" fill="none"
        stroke="#00f58c" stroke-width="4" stroke-linecap="round"
        stroke-linejoin="round"/>
      ${txt(120, 58, n.good, '#00f58c')}${txt(480, 208, n.bad, '#00f58c')}
      ${txt(170, 190, n.lower, '#c9d3e4', 14)}
      </svg>`;
  }
  if (n.kind === 'fill') {
    return `<svg viewBox="0 20 600 200">
      <rect x="60" y="130" width="480" height="22" rx="3" fill="${PCB}"/>
      <rect x="100" y="84" width="400" height="20" rx="3" fill="${top}"/>
      <rect x="282" y="84" width="36" height="20" fill="${TIN}"/>
      <rect x="282" y="130" width="36" height="22" fill="${TIN}"/>
      <ellipse cx="300" cy="84" rx="40" ry="13" fill="${TIN}"/>
      <ellipse cx="300" cy="152" rx="40" ry="13" fill="${TIN}"/>
      <rect x="292" y="56" width="16" height="124" rx="3" fill="#c77b3b"/>
      <rect x="294" y="56" width="4" height="124" fill="#fff"
        fill-opacity=".25"/>
      ${txt(300, 42, n.good, '#00f58c')}${txt(300, 208, n.bad)}
      ${txt(150, 99, n.upper, '#fff', 14)}${txt(110, 147, n.lower, '#fff', 14)}
      </svg>`;
  }
  return `<svg viewBox="0 30 600 200">${stack(20, 26)}${stack(330, 0)}
    ${txt(145, 62, n.good, '#00f58c')}${txt(455, 62, n.bad, '#ff5b6e')}
    <path d="M360 210L550 96M360 96L550 210" stroke="#ff5b6e"
      stroke-width="5" stroke-linecap="round" opacity=".8"/>
    <path d="M118 200l18 18l36 -40" fill="none" stroke="#00f58c"
      stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/>
    </svg>`;
}

// A board seen flat: the view moves over it by the camera keys
// [t, [x, y], w], the point at the frame's centre and the mm across.
function drawBoard(sc, lt, a, pulse, ui) {
  const e = $('#pcb');
  // One board is drawn into the element at a time.
  if (e.dataset.for !== String(sc.t)) {
    sc.rig = buildBoard(sc.plan);
    e.dataset.for = String(sc.t);
  }
  e.style.display = 'block';
  e.style.opacity = a * (sc.title ? .35 + .65 * ramp(lt, sc.title.to - .6,
                                                     sc.title.to) : 1);
  const k = sc.camera;
  let i = 0;
  while (i < k.length - 2 && lt > k[i + 1][0]) i++;
  const j = Math.min(i + 1, k.length - 1);
  const f = k.length > 1 ? ss((lt - k[i][0]) / (k[j][0] - k[i][0])) : 0;
  const mix = (p, q) => p + (q - p) * f;
  // The width is eased on a log scale, so that a zoom keeps its pace.
  sc.rig.draw(sc, lt, [[mix(k[i][1][0], k[j][1][0]),
                        mix(k[i][1][1], k[j][1][1])],
                       Math.exp(mix(Math.log(k[i][2]), Math.log(k[j][2])))]);
  drawNotes(sc, lt, a, pulse, ui, sc.rig.project);
  // Insets: a cross-section of a solder joint, in a card.
  for (const n of sc.insets || []) {
    const o = win(lt, n.from, n.to) * a;
    if (o <= 0) continue;
    const e = document.createElement('div');
    e.className = 'bub inset';
    e.style.left = n.at[0] + 'px';
    e.style.top = n.at[1] + 'px';
    e.style.opacity = o;
    e.style.transform = `translateY(${(1 - o) * 14}px)`;
    e.innerHTML = `<div class="h"><span>${n.title}</span></div>`
      + section(n) + (n.text ? `<p>${n.text}</p>` : '');
    bubs.appendChild(e);
  }
  // Tags: a pad's name beside it, on a short line.
  for (const m of sc.tags || []) {
    const o = win(lt, m.from, m.to, .3) * a;
    if (o <= 0) continue;
    const [x, y] = sc.rig.project(m.at), d = m.d || 46;
    const side = m.side || 'r';
    const [ex, ey] = side === 'b' ? [x, y + d] : side === 't' ? [x, y - d]
      : side === 'l' ? [x - d, y] : [x + d, y];
    const col = COL[m.style || ''];
    const el = document.createElement('div');
    el.className = 'pill';
    el.textContent = m.text;
    el.style.opacity = o;
    el.style.background = col;
    bubs.appendChild(el);
    const r = el.getBoundingClientRect();
    el.style.left = (side === 'l' ? ex - r.width : side === 'r' ? ex
      : ex - r.width / 2) + 'px';
    el.style.top = (side === 't' ? ey - r.height : side === 'b' ? ey
      : ey - r.height / 2) + 'px';
    ov.insertAdjacentHTML('beforeend',
      `<g opacity="${o}"><line x1="${x}" y1="${y}" x2="${ex}" y2="${ey}"
         stroke="${col}" stroke-width="2.5"/>
       <circle cx="${x}" cy="${y}" r="6" fill="${col}"/></g>`);
  }
}

function drawScreen(sc, lt, a, pulse, ui) {
  const dev = $('#dev');
  dev.style.display = 'block';
  dev.style.opacity = a;
  dev.style.transform = `scale(${.96 + .04 * ramp(lt, 0, .8)})`;
  let i = 0;
  while (i < sc.steps.length - 1 && lt >= sc.steps[i + 1].t) i++;
  const b = sc.steps[i], q0 = lt - b.t, prev = sc.steps[Math.max(0, i - 1)];
  $('#s0').src = imgs[prev.shot].src;
  $('#s1').src = imgs[b.shot].src;
  $('#s1').style.opacity = i === 0 ? 1 : ramp(q0, 0, .28);
  ui.cn = b.n;
  ui.ct = b.chip || b.title;
  ui.step = b.n;
  const [bx, by] = b.target || [512, 300];
  const tx = SX + bx * SC, ty = SY + by * SC;
  const ba = Math.min(ramp(q0, .25, .6),
                      1 - ramp(q0, b.dur - .22, b.dur - .02)) * a;
  const tag = b.action === 'tap' ? `<span class="tag tap">${words.tap}</span>`
    : b.action === 'hold' ? `<span class="tag hold">${words.hold}</span>`
    : b.tag ? `<span class="tag warn">${b.tag}</span>` : '';
  if (ba > 0) {
    bubble({x: 1398, y: cl(ty - 110, 190, 700), n: b.n, title: b.title,
            text: b.text, a: ba, pulse, style: b.style, tag,
            ...(b.target ? {tx, ty} : {})});
  }
  if (b.action === 'tap') {
    const q = q0 - (b.dur - .75);
    if (q > 0) {
      const pr = ramp(q, 0, .2) - ramp(q, .3, .5), rp = cl((q - .28) / .45);
      ov.insertAdjacentHTML('beforeend',
        `<circle cx="${tx}" cy="${ty}" r="${30 - 7 * pr}"
          fill="rgba(255,255,255,${.28 + .3 * pr})" stroke="#fff"
          stroke-width="2.5"
          opacity="${ramp(q, 0, .12) * (1 - ramp(q, .6, .75))}"/>`
        + (rp > 0 ? `<circle cx="${tx}" cy="${ty}" r="${26 + 54 * rp}"
          fill="none" stroke="#fff" stroke-width="3"
          opacity="${(1 - rp) * .9}"/>` : ''));
    }
  }
  if (b.action === 'hold') {
    const q = q0 - (b.dur - 2.45);
    if (q > 0) {
      const pr = cl(q / 2), R = 40, C = 2 * Math.PI * R;
      const o = ramp(q, 0, .15) * (1 - ramp(q, 2.2, 2.42));
      ov.insertAdjacentHTML('beforeend',
        `<g opacity="${o}"><circle cx="${tx}" cy="${ty}" r="24"
          fill="rgba(255,255,255,.5)" stroke="#fff" stroke-width="2.5"/>
         <circle cx="${tx}" cy="${ty}" r="${R}" fill="none"
          stroke="rgba(255,255,255,.25)" stroke-width="7"/>
         <circle cx="${tx}" cy="${ty}" r="${R}" fill="none" stroke="#00f58c"
          stroke-width="7" stroke-linecap="round" stroke-dasharray="${C}"
          stroke-dashoffset="${C * (1 - pr)}"
          transform="rotate(-90 ${tx} ${ty})"/>
         <text x="${tx + 56}" y="${ty + 8}" font-family="Inter"
          font-weight="800" font-size="24"
          fill="#00f58c">${(pr * 2).toFixed(1)} s</text></g>`);
    }
  }
}

// Words that rise into place one after another, from t0, 0.07s apart.
function setWords(el, text) {
  el.innerHTML = text.split(' ').map(
    w => `<span class="w">${w}</span>`).join(' ');
}
function moveWords(el, q, t0, alpha) {
  [...el.querySelectorAll('.w')].forEach((s, i) => {
    const o = ramp(q, t0 + i * .07, t0 + i * .07 + .45);
    s.style.opacity = o * alpha;
    s.style.transform = `translateY(${(1 - o) * .55}em)`;
    s.style.filter = o < 1 ? `blur(${(1 - o) * 7}px)` : '';
  });
}
const CHECK = '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10.5"/>'
  + '<path d="M7 12.5l3.4 3.4L17.2 9"/></svg>';

// The chapter card: a number, a title and a line, on the bare background.
function drawCard(b, q0, a, key) {
  const e = $('#card'), out = 1 - ramp(q0, b.dur - .4, b.dur - .05);
  if (e.dataset.for !== key) {
    e.querySelector('.num').textContent = String(b.n).padStart(2, '0');
    setWords(e.querySelector('h2'), b.card.title);
    e.querySelector('p').innerHTML = b.card.text || '';
    e.dataset.for = key;
  }
  e.style.display = 'block';
  const num = e.querySelector('.num'), o = ramp(q0, 0, .55);
  num.style.opacity = o * out * a;
  num.style.transform = `translateX(${(1 - o) * -40}px)`;
  moveWords(e.querySelector('h2'), q0, .22, out * a);
  const rule = e.querySelector('.rule');
  rule.style.transform = `scaleX(${ramp(q0, .45, 1.15)})`;
  rule.style.opacity = out * a;
  const txt = e.querySelector('p'), ot = ramp(q0, .75, 1.25);
  txt.style.opacity = ot * out * a;
  txt.style.transform = `translateY(${(1 - ot) * 14}px)`;
}

// A screenshot with a caption beside it, an optional spotlight on a part
// of the display and optional marks that name parts of it.
function drawTour(sc, lt, a, ui) {
  const dev = $('#dev'), cap = $('#cap');
  let i = 0;
  while (i < sc.steps.length - 1 && lt >= sc.steps[i + 1].t) i++;
  const b = sc.steps[i], q0 = lt - b.t, key = sc.t + ':' + i;
  ui.step = b.n;
  ui.frac = (lt - sc.span[b.n][0]) / (sc.span[b.n][1] - sc.span[b.n][0]);
  if (b.card) { drawCard(b, q0, a, key); return; }
  const before = i > 0 ? sc.steps[i - 1] : null;
  const prev = before && !before.card ? before : b;
  const entry = !before || before.card ? ramp(q0, 0, .75) : 1;
  const zoom = 1 + .014 * q0 / b.dur;
  const out = 1 - ramp(q0, b.dur - .32, b.dur - .04);
  const next = sc.steps[i + 1];
  const leave = !next || next.card ? out : 1;
  dev.style.display = 'block';
  dev.style.opacity = a * entry * leave;
  dev.style.transform = `perspective(1800px) rotateY(${(1 - entry) * 9}deg)`
    + ` translateX(${(1 - entry) * -46}px) scale(${zoom})`;
  $('#s0').src = imgs[prev.shot].src;
  $('#s1').src = imgs[b.shot].src;
  $('#s1').style.opacity = prev === b ? 1 : ramp(q0, 0, .4);
  const cx = 56 + 652, cy = 176 + 387;
  const zx = x => cx + (SX + x * SC - cx) * zoom;
  const zy = y => cy + (SY + y * SC - cy) * zoom;
  if (b.focus) {
    const [fx, fy, fw, fh] = b.focus;
    const x = zx(fx), y = zy(fy), w = fw * SC * zoom, h = fh * SC * zoom;
    const fa = ramp(q0, .9, 1.5) * out * a;
    const rr = (X, Y, W, H, r) => `M${X + r} ${Y}h${W - 2 * r}`
      + `a${r} ${r} 0 0 1 ${r} ${r}v${H - 2 * r}a${r} ${r} 0 0 1 ${-r} ${r}`
      + `h${-(W - 2 * r)}a${r} ${r} 0 0 1 ${-r} ${-r}v${-(H - 2 * r)}`
      + `a${r} ${r} 0 0 1 ${r} ${-r}z`;
    ov.insertAdjacentHTML('beforeend',
      `<g opacity="${fa}"><path fill-rule="evenodd" fill="rgba(4,7,12,.62)"
          d="${rr(zx(0), zy(0), 1280 * zoom, 750 * zoom, 10)}`
      + `${rr(x, y, w, h, 12)}"/>
        <path d="${rr(x, y, w, h, 12)}" fill="none" stroke="#00e5ff"
          stroke-width="9" opacity=".16"/>
        <path d="${rr(x, y, w, h, 12)}" fill="none" stroke="#00e5ff"
          stroke-width="2.5"/></g>`);
  }
  // Marks: a dot on the display, a short line and a pill with a name.
  (b.marks || []).forEach((m, k) => {
    const t0 = 1.0 + .38 * k, o = ramp(q0, t0, t0 + .4) * out * a;
    if (o <= 0) return;
    const x = zx(m.at[0]), y = zy(m.at[1]), d = m.d || 54;
    const side = m.side || (m.at[1] < 90 ? 'b' : m.at[0] > 700 ? 'l' : 'r');
    const [ex, ey] = side === 'b' ? [x, y + d] : side === 't' ? [x, y - d]
      : side === 'l' ? [x - d, y] : [x + d, y];
    const grow = ramp(q0, t0, t0 + .35);
    const e = document.createElement('div');
    e.className = 'pill';
    e.textContent = m.text;
    e.style.opacity = ramp(q0, t0 + .15, t0 + .5) * out * a;
    bubs.appendChild(e);
    const r = e.getBoundingClientRect();
    e.style.left = (side === 'l' ? ex - r.width : side === 'r' ? ex
      : ex - r.width / 2) + 'px';
    e.style.top = (side === 't' ? ey - r.height : side === 'b' ? ey
      : ey - r.height / 2) + 'px';
    const pu = ((q0 - t0) * .8) % 1;
    ov.insertAdjacentHTML('beforeend',
      `<g opacity="${o}"><line x1="${x}" y1="${y}"
         x2="${x + (ex - x) * grow}" y2="${y + (ey - y) * grow}"
         stroke="#00e5ff" stroke-width="2.5"/>
       <circle cx="${x}" cy="${y}" r="13" fill="rgba(4,7,12,.55)"/>
       <circle cx="${x}" cy="${y}" r="6" fill="#00e5ff"/>
       <circle cx="${x}" cy="${y}" r="${10 + 14 * pu}" fill="none"
         stroke="#00e5ff" stroke-width="2" opacity="${(1 - pu) * .8}"/></g>`);
  });
  if (cap.dataset.for !== key) {
    const path = (b.path || []).map((x, n, all) =>
      `<span class="${n === all.length - 1 ? 'on' : ''}">${x}</span>`)
      .join('<i>›</i>');
    const pts = (b.points || []).map(
      x => `<li>${CHECK}<span>${x}</span></li>`).join('');
    cap.innerHTML = `<div class="k">${b.kicker || ''}</div>`
      + (path ? `<div class="path">${path}</div>` : '')
      + '<h2></h2>' + (b.text ? `<p>${b.text}</p>` : '')
      + (pts ? `<ul>${pts}</ul>` : '')
      + (b.tag ? `<div><span class="tag warn">${b.tag}</span></div>` : '');
    setWords(cap.querySelector('h2'), b.title);
    cap.dataset.for = key;
  }
  cap.style.display = 'flex';
  // The kicker stays while the chapter does; the rest comes in line by
  // line and leaves together.
  const same = before && !before.card && before.kicker === b.kicker;
  const stay = next && !next.card && next.kicker === b.kicker;
  let k = 0;
  for (const e of cap.children) {
    if (e.tagName === 'H2') {
      moveWords(e, q0, .2, out * a);
      k += 2;
      continue;
    }
    const items = e.tagName === 'UL' ? [...e.children] : [e];
    for (const it of items) {
      const hold = it.className === 'k';
      const t0 = .2 + .14 * k, o = ramp(q0, t0, t0 + .45);
      it.style.opacity = (hold && same ? 1 : o) * (hold && stay ? 1 : out)
        * a;
      it.style.transform = hold && same ? ''
        : `translateY(${(1 - o) * 16}px)`;
      const tick = it.querySelector('path');
      if (tick) {
        tick.style.strokeDashoffset = 16 * (1 - ramp(q0, t0 + .2, t0 + .6));
      }
      k++;
    }
  }
}

function drawEnd(sc, a, ui) {
  const e = $('#end');
  e.style.display = 'flex';
  e.style.opacity = a;
  if (e.dataset.for !== String(sc.t)) {
    e.querySelector('h2').textContent = sc.heading;
    e.querySelector('ol').innerHTML = (sc.items || []).map(
      (x, i) => `<li><span>${i + 1}</span>${x}</li>`).join('');
    e.querySelector('p').textContent = sc.note || '';
    e.dataset.for = String(sc.t);
  }
  ui.step = (sb.steps || 0) + 1;
}

window.TOTAL = TOTAL;
window.renderAt = t => {
  bubs.innerHTML = '';
  ov.innerHTML = '';
  for (const id of ['#gl', '#pcb', '#dev', '#end', '#cap', '#card']) {
    $(id).style.display = 'none';
  }
  $('#title').style.opacity = 0;
  $('#pin').style.opacity = 0;
  $('#bar').style.width = (t / TOTAL * 1920) + 'px';
  let n = 0;
  while (n < sb.scenes.length - 1 && t >= sb.scenes[n + 1].t) n++;
  const sc = sb.scenes[n], lt = t - sc.t, last = n === sb.scenes.length - 1;
  const a = (n === 0 ? 1 : ramp(lt, 0, FADE))
    * (last ? 1 : 1 - ramp(lt, sc.dur - FADE, sc.dur));
  const ui = {cn: '', ct: '', step: 0, frac: 1};
  // The dots keep the steps of the scenes before this one.
  for (const p of sb.scenes.slice(0, n)) {
    for (const c of p.chips || []) {
      if (typeof c.n === 'number') ui.step = Math.max(ui.step, c.n);
    }
    for (const s of p.steps || []) ui.step = Math.max(ui.step, s.n);
  }
  const pulse = (t * .9) % 1;
  if (sc.type === 'case') drawCase(sc, lt, a, pulse, ui);
  else if (sc.type === 'board') drawBoard(sc, lt, a, pulse, ui);
  else if (sc.type === 'screen') drawScreen(sc, lt, a, pulse, ui);
  else if (sc.type === 'tour') drawTour(sc, lt, a, ui);
  else drawEnd(sc, a, ui);
  $('#chip').style.opacity = ui.cn !== '' ? a : 0;
  $('#cn').textContent = ui.cn;
  $('#ct').textContent = ui.ct;
  [...dots.children].forEach((d, i) => {
    d.className = i < ui.step ? 'on' : '';
  });
  [...nav.children].forEach((d, i) => {
    d.className = i + 1 === ui.step ? 'on' : i + 1 < ui.step ? 'done' : '';
    d.querySelector('s').style.width = (i + 1 < ui.step ? 100
      : i + 1 === ui.step ? cl(ui.frac) * 100 : 0) + '%';
  });
  nav.style.opacity = sc.type === 'tour' || sc.type === 'end' ? a : 0;
  const titled = (sc.type === 'case' || sc.type === 'board') && sc.title
    && lt < sc.title.to;
  $('#brand').style.opacity = titled ? 0 : 1;
  dots.style.opacity = titled || sb.chapters ? 0 : 1;
};
window.READY = true;
