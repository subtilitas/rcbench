// Draws one frame of a storyboard.  window.renderAt(t) sets every element
// from t alone, so frames can be captured in any order and at any rate.
// A storyboard is a list of scenes: "case" (the 3D model with labels),
// "screen" (panel screenshots with pointer bubbles), "tour" (panel
// screenshots with a caption and a spotlight) and "end" (a recap).
// tools/video/README.md describes the fields.
//
// SPDX-License-Identifier: MIT
import * as THREE from 'three';
import {buildCase, CONNECTORS, WIRE_COLORS} from './case3d.js';

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
      st.shot = st.lang ? `${st.view}@${st.lang}` : st.view;
      need.add(st.shot);
    }
    sc.dur = a;
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
  if (sc.type !== 'case') continue;
  sc.model = buildCase(sc.display ? imgs[sc.display.view] : null);
  if (sc.servo) sc.model.setServoPort(sc.servo.connector, sc.servo.position);
}

$('#brand').innerHTML = `<b>rcbench</b> · ${sb.brand || ''}`;
const dots = $('#dots');
for (let i = 0; i < (sb.steps || 0); i++) {
  dots.appendChild(document.createElement('i'));
}
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
  const m = sc.model, gl = $('#gl');
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
    const p = proj(typeof l.anchor === 'string' ? m.anchors[l.anchor]()
                                                : l.anchor);
    bubble({...l, x: l.at[0], y: l.at[1], a: la, pulse, tx: p[0], ty: p[1]});
  }
  if (sc.pinout) {
    const p = sc.pinout, pa = win(lt, p.from, p.to) * a, e = $('#pin');
    if (pa > 0 && e.dataset.for !== sc.t + p.connector) {
      e.innerHTML = pinout(p);
      e.dataset.for = sc.t + p.connector;
    }
    e.style.opacity = pa;
    e.style.transform = `translateX(${(1 - pa) * -30}px)`;
  }
  for (const c of sc.chips || []) {
    if (lt >= c.from && lt < c.to) { ui.cn = c.n; ui.ct = c.text; }
    if (lt >= c.from && typeof c.n === 'number') ui.step = c.n;
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

// A screenshot with a caption beside it and an optional spotlight on a
// part of the display.
function drawTour(sc, lt, a, ui) {
  const dev = $('#dev'), cap = $('#cap'), ghost = $('#ghost');
  let i = 0;
  while (i < sc.steps.length - 1 && lt >= sc.steps[i + 1].t) i++;
  const b = sc.steps[i], q0 = lt - b.t, prev = sc.steps[Math.max(0, i - 1)];
  const zoom = 1 + .014 * q0 / b.dur;
  dev.style.display = 'block';
  dev.style.opacity = a;
  dev.style.transform = `scale(${zoom})`;
  const xf = i === 0 ? 1 : ramp(q0, 0, .4);
  $('#s0').src = imgs[prev.shot].src;
  $('#s1').src = imgs[b.shot].src;
  $('#s1').style.opacity = xf;
  ui.step = b.n;
  const out = 1 - ramp(q0, b.dur - .32, b.dur - .04);
  if (b.focus) {
    const [fx, fy, fw, fh] = b.focus;
    const x = SX + fx * SC, y = SY + fy * SC, w = fw * SC, h = fh * SC;
    const fa = ramp(q0, .9, 1.5) * out * a;
    const cx = 56 + 652, cy = 176 + 387;
    const rr = (X, Y, W, H, r) => `M${X + r} ${Y}h${W - 2 * r}`
      + `a${r} ${r} 0 0 1 ${r} ${r}v${H - 2 * r}a${r} ${r} 0 0 1 ${-r} ${r}`
      + `h${-(W - 2 * r)}a${r} ${r} 0 0 1 ${-r} ${-r}v${-(H - 2 * r)}`
      + `a${r} ${r} 0 0 1 ${r} ${-r}z`;
    ov.insertAdjacentHTML('beforeend',
      `<g transform="translate(${cx} ${cy}) scale(${zoom})
         translate(${-cx} ${-cy})" opacity="${fa}">
        <path fill-rule="evenodd" fill="rgba(4,7,12,.62)"
          d="${rr(SX, SY, 1280, 750, 10)}${rr(x, y, w, h, 12)}"/>
        <path d="${rr(x, y, w, h, 12)}" fill="none" stroke="#00e5ff"
          stroke-width="9" opacity=".16"/>
        <path d="${rr(x, y, w, h, 12)}" fill="none" stroke="#00e5ff"
          stroke-width="2.5"/></g>`);
  }
  const key = sc.t + ':' + i;
  if (cap.dataset.for !== key) {
    const pts = (b.points || []).map(x => `<li>${x}</li>`).join('');
    cap.innerHTML = `<div class="k">${b.kicker || ''}</div>`
      + `<h2>${b.title}</h2>` + (b.text ? `<p>${b.text}</p>` : '')
      + (pts ? `<ul>${pts}</ul>` : '')
      + (b.tag ? `<div><span class="tag warn">${b.tag}</span></div>` : '');
    cap.dataset.for = key;
  }
  cap.style.display = 'flex';
  // The kicker stays while the chapter does; the rest comes in line by
  // line.
  const same = i > 0 && prev.kicker === b.kicker;
  const next = sc.steps[i + 1];
  const stay = next && next.kicker === b.kicker;
  let k = 0;
  for (const e of cap.children) {
    const items = e.tagName === 'UL' ? [...e.children] : [e];
    if (e.tagName === 'UL') e.style.opacity = 1;
    for (const it of items) {
      const hold = it.className === 'k';
      const o = (hold && same ? 1 : ramp(q0, .18 + .13 * k, .6 + .13 * k))
        * (hold && stay ? 1 : out) * a;
      it.style.opacity = o;
      it.style.transform = hold && same ? ''
        : `translateY(${(1 - ramp(q0, .18 + .13 * k, .7 + .13 * k)) * 16}px)`;
      k++;
    }
  }
  ghost.textContent = String(b.n).padStart(2, '0');
  ghost.style.opacity = .055 * a;
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
  ui.step = sb.steps || 0;
}

window.TOTAL = TOTAL;
window.renderAt = t => {
  bubs.innerHTML = '';
  ov.innerHTML = '';
  for (const id of ['#gl', '#dev', '#end', '#cap']) {
    $(id).style.display = 'none';
  }
  $('#ghost').style.opacity = 0;
  $('#title').style.opacity = 0;
  $('#pin').style.opacity = 0;
  $('#bar').style.width = (t / TOTAL * 1920) + 'px';
  let n = 0;
  while (n < sb.scenes.length - 1 && t >= sb.scenes[n + 1].t) n++;
  const sc = sb.scenes[n], lt = t - sc.t, last = n === sb.scenes.length - 1;
  const a = (n === 0 ? 1 : ramp(lt, 0, FADE))
    * (last ? 1 : 1 - ramp(lt, sc.dur - FADE, sc.dur));
  const ui = {cn: '', ct: '', step: 0};
  // The dots keep the steps of the scenes before this one.
  for (const p of sb.scenes.slice(0, n)) {
    for (const c of p.chips || []) {
      if (typeof c.n === 'number') ui.step = Math.max(ui.step, c.n);
    }
    for (const s of p.steps || []) ui.step = Math.max(ui.step, s.n);
  }
  const pulse = (t * .9) % 1;
  if (sc.type === 'case') drawCase(sc, lt, a, pulse, ui);
  else if (sc.type === 'screen') drawScreen(sc, lt, a, pulse, ui);
  else if (sc.type === 'tour') drawTour(sc, lt, a, ui);
  else drawEnd(sc, a, ui);
  $('#chip').style.opacity = ui.cn !== '' ? a : 0;
  $('#cn').textContent = ui.cn;
  $('#ct').textContent = ui.ct;
  [...dots.children].forEach((d, i) => { d.className = i < ui.step ? 'on' : ''; });
  const titled = sc.type === 'case' && sc.title && lt < sc.title.to;
  $('#brand').style.opacity = titled ? 0 : 1;
  dots.style.opacity = titled ? 0 : 1;
};
window.READY = true;
