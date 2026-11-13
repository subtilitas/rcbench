// A printed circuit board seen flat from its front, drawn as SVG from the
// file tools/video_board.py writes: the tracks of the front copper, the
// pads and the outline of every part.  Units are mm from the drawing's
// top left corner.  A storyboard's board scene moves the view over it and
// draws work on it: a part taking its place, solder joints, a net lit up,
// a track cut, a wire, a part soldered across two pads.
//
// SPDX-License-Identifier: MIT

const NS = 'http://www.w3.org/2000/svg';
const cl = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const ss = x => { x = cl(x); return x * x * (3 - 2 * x); };
const ramp = (t, a, b) => ss((t - a) / (b - a));

// [pattern on a part's footprint, fill, the kind of drawing].
const LOOKS = [
  [/RP2350/, '#1d4fa8', 'module'],
  [/INA3221/, '#5a2d82', 'module'],
  [/DCDC_Buck|USBC_PD_Trigger/, '#15803a', 'module'],
  [/PinHeader/, '#15171c', 'plug'],
  [/JST|Molex/, '#e9e4d6', 'plug'],
  [/R_Axial|D_DO/, '#b9a27a', 'axial'],
  [/R_1206/, 'none', 'empty'],
];
const MASK = '#17503f', TRACK = '#2c8f70', GOLD = '#d9b04a', HOLE = '#0b1512';
// The colour bands of an axial resistor by its text or its value.
const BANDS = {'2.2 kΩ': ['#d0122a', '#d0122a', '#d0122a', '#c9a227'],
               '4k7': ['#e8c81a', '#7a3fb0', '#d0122a', '#c9a227']};

export function buildBoard(data) {
  const svg = document.querySelector('#pcb');
  const still = document.createElementNS(NS, 'g');
  const live = document.createElementNS(NS, 'g');
  const [W, H] = data.size;
  const pads = Object.fromEntries(data.pads.map(p => [p.name, p]));
  const look = part => LOOKS.find(([re]) => re.test(part.footprint))
    || [null, '#23262c', 'plug'];
  const padSvg = p => {
    const r = p.round ? Math.min(p.w, p.h) / 2 : .18;
    return `<rect x="${p.x - p.w / 2}" y="${p.y - p.h / 2}" width="${p.w}"
      height="${p.h}" rx="${r}" fill="${GOLD}"/>`
      + (p.drill ? `<circle cx="${p.x}" cy="${p.y}" r="${p.drill / 2}"
      fill="${HOLE}"/>` : '');
  };
  const own = Object.fromEntries(data.parts.map(q => [q.ref, look(q)[2]]));
  let h = `<defs><linearGradient id="pcbmask" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#1b5c49"/><stop offset="1"
      stop-color="#113c30"/></linearGradient>
    <filter id="pcbshadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="1.2" stdDeviation="1.6" flood-opacity=".55"/>
    </filter></defs>
    <rect x="0" y="0" width="${W}" height="${H}" rx="1.6"
      fill="url(#pcbmask)" filter="url(#pcbshadow)"/>`;
  for (const t of data.tracks) {
    h += `<line x1="${t.a[0]}" y1="${t.a[1]}" x2="${t.b[0]}" y2="${t.b[1]}"
      stroke="${TRACK}" stroke-width="${t.w}" stroke-linecap="round"/>`;
  }
  // Every pad is the carrier's; a module covers its own and carries a
  // pad of its own over each.
  for (const p of data.pads) h += padSvg(p);
  still.innerHTML = h;
  const groups = {};
  for (const q of data.parts) {
    const [, fill, kind] = look(q), [x0, y0, x1, y1] = q.box;
    const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    h = '';
    if (q.back) {
      // A part on the back: its outline, dashed.
      h += `<rect x="${x0}" y="${y0}" width="${x1 - x0}" height="${y1 - y0}"
        rx=".8" fill="#e9e4d6" fill-opacity=".12" stroke="#e9e4d6"
        stroke-width=".18" stroke-dasharray=".7 .5"/>
        <text x="${cx}" y="${cy}" text-anchor="middle"
        dominant-baseline="middle" font-family="Inter, sans-serif"
        font-weight="700" font-size="1.3" fill="#e8f1ec"
        fill-opacity=".8">${q.ref}</text>`;
    } else if (kind === 'empty') {
      h += `<rect x="${x0 + .25}" y="${y0 + .25}" width="${x1 - x0 - .5}"
        height="${y1 - y0 - .5}" rx=".3" fill="none" stroke="#e8f1ec"
        stroke-opacity=".45" stroke-width=".12"/>`;
    } else if (kind === 'axial') {
      // Drawn along x from its pad 1, then turned onto its pads: a
      // diode in glass with its band at pad 1, the cathode, or a
      // resistor with the colour bands of its value.
      const [a, b] = data.pads.filter(p => p.ref === q.ref);
      const len = Math.hypot(b.x - a.x, b.y - a.y);
      const deg = Math.atan2(b.y - a.y, b.x - a.x) * 180 / Math.PI;
      const diode = /D_DO/.test(q.footprint);
      const body = diode ? 3.0 : Math.min(6.3, len * .62);
      const thick = diode ? 1.5 : 2.2, bx = (len - body) / 2;
      let s = `<line x1="0" y1="0" x2="${len}" y2="0" stroke="#c8ccd2"
        stroke-width=".5"/>`;
      if (diode) {
        s += `<rect x="${bx}" y="${-thick / 2}" width="${body}"
          height="${thick}" rx=".45" fill="#c9622e"/>
          <rect x="${bx + .3}" y="${-thick / 2}" width=".5"
          height="${thick}" fill="#15171c"/>`;
      } else {
        s += `<rect x="${bx}" y="${-thick / 2}" width="${body}"
          height="${thick}" rx=".9" fill="${fill}"/>`;
        (BANDS[q.value] || []).forEach((c, i, all) => {
          const at = bx + .9 + i * .85 + (i === all.length - 1 ? .9 : 0);
          s += `<rect x="${at}" y="${-thick / 2}" width=".45"
            height="${thick}" fill="${c}"/>`;
        });
      }
      s += `<rect x="${bx}" y="${-thick / 2}" width="${body}"
        height="${thick * .3}" rx=".3" fill="#fff" fill-opacity=".18"/>`;
      h += `<g transform="translate(${a.x} ${a.y}) rotate(${deg})">${s}</g>`;
    } else {
      h += `<rect x="${x0}" y="${y0}" width="${x1 - x0}" height="${y1 - y0}"
        rx=".8" fill="${fill}" fill-opacity="${kind === 'module' ? .93 : .96}"
        stroke="#000" stroke-opacity=".35" stroke-width=".15"/>`;
    }
    if (q.back) {
      // Drawn above.
    } else if (kind === 'module') {
      for (const p of data.pads) if (p.ref === q.ref) h += padSvg(p);
      h += `<text x="${cx}" y="${cy}" text-anchor="middle"
        dominant-baseline="middle" font-family="Inter, sans-serif"
        font-weight="700" font-size="1.9" fill="#fff"
        fill-opacity=".5">${q.ref}</text>`;
    } else if (kind !== 'axial') {
      h += `<text x="${cx}" y="${kind === 'empty' ? y0 - .5 : cy}"
        text-anchor="middle" dominant-baseline="middle"
        font-family="Inter, sans-serif" font-weight="700"
        font-size="${kind === 'empty' ? 1.0 : 1.5}"
        fill="${fill === '#e9e4d6' ? '#333' : '#e8f1ec'}"
        fill-opacity=".7">${q.ref}</text>`;
    }
    const g = document.createElementNS(NS, 'g');
    g.innerHTML = h;
    still.appendChild(g);
    groups[q.ref] = {g, fixed: kind === 'empty'};
  }
  svg.replaceChildren(still, live);

  // A point of the board: a pad's name, or [x, y] in mm.
  const point = at => {
    if (Array.isArray(at)) return at;
    const p = pads[at];
    if (!p) throw new Error('the board has no pad ' + at);
    return [p.x, p.y];
  };
  let view = [0, 0, W];
  const blob = ([x, y], k, r = .95) => `<circle cx="${x}" cy="${y}"
    r="${r * k}" fill="#cfd4da"/><circle cx="${x - r * .23}"
    cy="${y - r * .26}" r="${r * .32 * k}" fill="#fff" fill-opacity=".75"/>`;

  return {
    data, point,
    // The frame pixel of a point of the board.
    project: at => {
      const [x, y] = point(at), s = 1920 / view[2];
      return [(x - view[0]) * s, (y - view[1]) * s];
    },
    // Draw the board at time lt of scene sc.  cam is [[cx, cy], width]:
    // the point at the frame's centre and the mm the frame is wide.
    draw: (sc, lt, cam) => {
      const [[cx, cy], w] = cam, hh = w * 9 / 16;
      view = [cx - w / 2, cy - hh / 2, w];
      svg.setAttribute('viewBox', `${view[0]} ${view[1]} ${w} ${hh}`);
      let g = '';
      // A bare board shows a part from its mount on: the part comes
      // down onto its place.
      const when = {};
      for (const m of sc.mount || []) {
        for (const ref of m.refs) when[ref] = m.t;
      }
      for (const [ref, part] of Object.entries(groups)) {
        const k = part.fixed || !sc.bare ? 1
          : ref in when ? ramp(lt, when[ref], when[ref] + .7) : 0;
        part.g.setAttribute('opacity', k);
        part.g.setAttribute('transform',
                            `translate(0 ${(1 - k) * -3.5})`);
      }
      // Solder joints, one pad after another over the span.
      for (const s of sc.solder || []) {
        const list = s.pads || data.pads.filter(p => s.refs.includes(p.ref))
          .map(p => p.name);
        list.forEach((name, i) => {
          const p = pads[name];
          if (!p) throw new Error('the board has no pad ' + name);
          const t0 = s.t[0] + (s.t[1] - s.t[0]) * i / list.length;
          const k = ramp(lt, t0, t0 + .25);
          const r = Math.max(.8, Math.min(p.w, p.h) * .47);
          if (s.back) {
            // A joint on the back of the board: a ring that fades.
            const o = k * (1 - ramp(lt, t0 + .5, t0 + 1.6));
            if (o > 0) {
              g += `<circle cx="${p.x}" cy="${p.y}" r="${r * (.7 + .6 * k)}"
                fill="#cfd4da" fill-opacity="${.25 * o}" stroke="#e9edf2"
                stroke-opacity="${o}" stroke-width=".2"/>`;
            }
          } else if (k > 0) {
            g += blob([p.x, p.y], k, r);
          }
        });
      }
      for (const n of sc.nets || []) {
        const o = Math.min(ramp(lt, n.from, n.from + .5),
                           1 - ramp(lt, n.to - .5, n.to));
        if (o <= 0) continue;
        const col = n.style === 'warn' ? '#ffb400' : '#00e5ff';
        // The light runs along the net from its first track to its last.
        for (const t of data.tracks) {
          if (t.net !== n.net) continue;
          g += `<line x1="${t.a[0]}" y1="${t.a[1]}" x2="${t.b[0]}"
            y2="${t.b[1]}" stroke="${col}" stroke-opacity="${.3 * o}"
            stroke-width="${t.w + .9}" stroke-linecap="round"/>
            <line x1="${t.a[0]}" y1="${t.a[1]}" x2="${t.b[0]}" y2="${t.b[1]}"
            stroke="${col}" stroke-opacity="${o}" stroke-width="${t.w}"
            stroke-linecap="round"/>`;
        }
        for (const p of data.pads) {
          if (p.net !== n.net) continue;
          g += `<circle cx="${p.x}" cy="${p.y}"
            r="${Math.max(p.w, p.h) / 2 + .35}" fill="none" stroke="${col}"
            stroke-opacity="${o}" stroke-width=".22"/>`;
        }
      }
      for (const c of sc.cuts || []) {
        const [a, b] = c.between.map(point);
        const k = ramp(lt, c.t[0], c.t[1]);
        if (k <= 0) continue;
        const mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2;
        const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
        const ux = (b[0] - a[0]) / len, uy = (b[1] - a[1]) / len;
        const gap = (c.gap || .7) * k / 2, wide = .62;
        // The track is opened: the bare board shows through, and the
        // scratches of the blade lie across it.
        g += `<line x1="${mx - ux * gap}" y1="${my - uy * gap}"
          x2="${mx + ux * gap}" y2="${my + uy * gap}" stroke="#0e2f26"
          stroke-width="${wide}"/>`;
        for (const s of [-.6, 0, .6]) {
          const px = mx + ux * gap * s, py = my + uy * gap * s;
          g += `<line x1="${px + uy * wide * .7}" y1="${py - ux * wide * .7}"
            x2="${px - uy * wide * .7}" y2="${py + ux * wide * .7}"
            stroke="#d7e2dc" stroke-opacity="${.5 * k}" stroke-width=".06"/>`;
        }
        // The blade: a line across the track that passes once.
        const q = cl((lt - c.t[0]) / (c.t[1] - c.t[0])), far = 1.7;
        if (q > 0 && q < 1) {
          const s = (q * 2 - 1) * far;
          g += `<line x1="${mx + uy * s - ux * .9}"
            y1="${my - ux * s - uy * .9}" x2="${mx + uy * s + ux * .9}"
            y2="${my - ux * s + uy * .9}" stroke="#ff5b6e"
            stroke-width=".16" stroke-linecap="round"/>`;
        }
        const ring = ramp(lt, c.t[1], c.t[1] + .4);
        g += `<ellipse cx="${mx}" cy="${my}" rx="${.9 + uy * .3}"
          ry="${.9 + ux * .3}" fill="none" stroke="#ff5b6e"
          stroke-opacity="${.9 * Math.max(k * (1 - ring), .55 * ring)}"
          stroke-width=".12" stroke-dasharray=".35 .25"/>`;
      }
      for (const w of sc.wires || []) {
        const k = ramp(lt, w.t[0], w.t[1]);
        if (k <= 0) continue;
        const pts = [w.from, ...(w.via || []), w.to].map(point);
        let d = `M${pts[0][0]} ${pts[0][1]}`, len = 0;
        for (let i = 1; i < pts.length; i++) {
          d += `L${pts[i][0]} ${pts[i][1]}`;
          len += Math.hypot(pts[i][0] - pts[i - 1][0],
                            pts[i][1] - pts[i - 1][1]);
        }
        const dash = `stroke-dasharray="${len}"
          stroke-dashoffset="${len * (1 - k)}"`;
        g += `<path d="${d}" fill="none" stroke="#000" stroke-opacity=".35"
          stroke-width=".8" stroke-linejoin="round" stroke-linecap="round"
          transform="translate(.25 .4)" ${dash}/>
          <path d="${d}" fill="none" stroke="${w.color || '#e5484d'}"
          stroke-width=".7" stroke-linejoin="round" stroke-linecap="round"
          ${dash}/>
          <path d="${d}" fill="none" stroke="#fff" stroke-opacity=".3"
          stroke-width=".16" stroke-linejoin="round" stroke-linecap="round"
          transform="translate(-.08 -.14)" ${dash}/>`;
        g += blob(pts[0], ramp(lt, w.t[0] - .3, w.t[0]) || k);
        // A loose wire leaves the board: no joint at its far end.
        if (k >= 1 && !w.loose) {
          g += blob(pts.at(-1), ramp(lt, w.t[1], w.t[1] + .3));
        }
      }
      for (const p of sc.fit || []) {
        const k = ramp(lt, p.t, p.t + .7);
        if (k <= 0) continue;
        const a = point(p.a), b = point(p.b);
        const len = Math.hypot(b[0] - a[0], b[1] - a[1]);
        const deg = Math.atan2(b[1] - a[1], b[0] - a[0]) * 180 / Math.PI;
        const body = Math.min(p.kind === 'zener' ? 3.4 : 4.2, len * .55);
        const thick = p.kind === 'zener' ? 1.5 : 1.7, x0 = (len - body) / 2;
        // Drawn along x from pad a, then turned onto the pads; the part
        // comes down onto the board.
        let s = `<line x1="0" y1="0" x2="${len}" y2="0" stroke="#c8ccd2"
          stroke-width=".4"/>`;
        if (p.kind === 'zener') {
          s += `<rect x="${x0}" y="${-thick / 2}" width="${body}"
            height="${thick}" rx=".3" fill="#1b1d22"/>
            <rect x="${x0 + .3}" y="${-thick / 2}" width=".55"
            height="${thick}" fill="#e9edf2"/>`;
        } else {
          s += `<rect x="${x0}" y="${-thick / 2}" width="${body}"
            height="${thick}" rx=".75" fill="#c9ae82"/>`;
          (BANDS[p.text] || []).forEach((c, i, all) => {
            const bx = x0 + .7 + i * .62 + (i === all.length - 1 ? .5 : 0);
            s += `<rect x="${bx}" y="${-thick / 2}" width=".34"
              height="${thick}" fill="${c}"/>`;
          });
        }
        s += `<rect x="${x0}" y="${-thick / 2}" width="${body}"
          height="${thick * .32}" rx=".3" fill="#fff" fill-opacity=".16"/>`;
        g += `<g opacity="${k}" transform="translate(${a[0]} ${a[1]})
          rotate(${deg}) translate(0 ${(1 - k) * -2.2})">
          <g transform="translate(.25 .45)" opacity=".22">
          <line x1="0" y1="0" x2="${len}" y2="0" stroke="#000"
          stroke-width="${thick * .8}" stroke-linecap="round"
          stroke-opacity=".5"/></g>${s}</g>`
          + blob(a, ramp(lt, p.t + .5, p.t + .9))
          + blob(b, ramp(lt, p.t + .5, p.t + .9));
      }
      live.innerHTML = g;
    },
  };
}
