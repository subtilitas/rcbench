// The pictorial wiring view: layout and router over every setup of the sweep,
// then the limits of pan and zoom and the markup.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection, Setup } from '../contract.ts';
import { MINUS, MINUS_NETS, PLUS, PLUS_NETS, withoutWirePoint } from '../drawings/wires.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { ANCHORS } from '../src/diagram/anchors.ts';
import { BAND, CLEAR, THICK, THIN, pitch } from '../src/diagram/layout.ts';
import { LABEL, diagramOf, simplify } from '../src/diagram/route.ts';
import type { Diagram, Pt } from '../src/diagram/route.ts';
import { RADIUS, legend, pathOf, picture, swatch } from '../src/diagram/picture.ts';
import { sceneOf } from '../src/diagram/scene.ts';
import { PAN_STEP, ZOOM_STEP, centreOn, clamp, fit, pan, shows, transformOf, zoomAt } from '../src/diagram/view.ts';
import { STRINGS } from '../src/i18n.ts';
import { optionsOf } from '../src/names.ts';
import type { View } from '../src/view/chooser.ts';
import { result } from '../src/view/result.ts';
import { describe, sweep } from './geometry.ts';

const samples = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS });

/** Largest distance in px between a wire end and the wire point of its pad, along the edge. A pad holds up to 3 wires. */
const PAD = 12;

type Seg = { n: number; w: number; x1: number; y1: number; x2: number; y2: number };

function segments(d: Diagram): Seg[] {
  const out: Seg[] = [];
  for (const w of d.wires) {
    for (const path of w.paths) {
      for (let i = 1; i < path.length; i++) {
        const [ax, ay] = path[i - 1]!;
        const [bx, by] = path[i]!;
        out.push({ n: w.n, w: w.thick ? THICK : THIN, x1: Math.min(ax, bx), y1: Math.min(ay, by), x2: Math.max(ax, bx), y2: Math.max(ay, by) });
      }
    }
  }
  return out;
}

/** The rule breaks of one diagram. Empty when it holds every rule. */
function breaks(d: Diagram, setup: Setup): string[] {
  const out: string[] = [];
  const boxes = new Map(d.parts.map((p) => [p.id, p]));

  // Parts: inside the picture, the drawing in its own size inside its box, no 2 boxes overlap.
  for (const p of d.parts) {
    const rec = ANCHORS.files[p.file];
    if (!rec) out.push(`${p.id}: no drawing record for ${p.file}`);
    else if (p.iw !== rec.viewBox[2] || p.ih !== rec.viewBox[3]) out.push(`${p.id}: drawing scaled`);
    if (p.x < 0 || p.y < 0 || p.x + p.w > d.w || p.y + p.h > d.h) out.push(`${p.id}: outside the picture`);
    if (p.ix < p.x || p.iy < p.y || p.ix + p.iw > p.x + p.w || p.iy + p.ih > p.y + p.h) out.push(`${p.id}: drawing outside its box`);
  }
  for (let i = 0; i < d.parts.length; i++) {
    for (let j = i + 1; j < d.parts.length; j++) {
      const a = d.parts[i]!;
      const b = d.parts[j]!;
      if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h) out.push(`${a.id} overlaps ${b.id}`);
    }
  }

  // Columns: the coprocessor column lies between the left and the right parts.
  if (!d.columns[d.centre]?.includes('copro')) out.push('the coprocessor is not in the centre column');
  const column = new Map(setup.parts.map((r) => [r.part.id, r.part.slot.column]));
  d.columns.forEach((ids, i) => {
    for (const id of ids) {
      const c = column.get(id);
      if (c === 'L' && i >= d.centre) out.push(`${id}: left part right of the coprocessor`);
      if (c === 'R' && i <= d.centre) out.push(`${id}: right part left of the coprocessor`);
    }
  });

  // Wires: each wire of the setup is routed or reported, never both.
  const routed = new Set(d.wires.map((w) => w.n));
  for (const w of setup.wires) {
    const drawn = routed.has(w.n);
    const reported = d.undrawn.includes(w.n);
    if (drawn === reported) out.push(`wire ${w.n}: ${drawn ? 'routed and reported' : 'missing'}`);
    if (reported && !withoutWirePoint(w.a, ANCHORS) && !(w.b !== null && withoutWirePoint(w.b, ANCHORS))) {
      out.push(`wire ${w.n}: reported although both ends have a point`);
    }
  }
  if (d.wires.length + d.undrawn.length !== setup.wires.length) out.push('wire count differs');

  // Ends: each path starts and ends on the pad of its wire point. The only wire of a pad ends on the point.
  // Several wires of 1 pad end side by side along the edge, a pitch apart.
  const onPad = new Map<string, Array<{ n: number; w: number; at: number }>>();
  for (const w of d.wires) {
    const whole = w.paths.length === 1 && w.ends.length === 2;
    if (!whole && w.paths.length !== w.ends.length) out.push(`wire ${w.n}: ${w.paths.length} paths for ${w.ends.length} ends`);
    for (const path of w.paths) {
      if (path.length < 2) out.push(`wire ${w.n}: path of ${path.length} point`);
      for (const [x, y] of path) {
        if (!(x >= 0 && x <= d.w && y >= 0 && y <= d.h)) out.push(`wire ${w.n}: point ${x},${y} outside the picture`);
      }
      // Every segment is horizontal or vertical.
      for (let i = 1; i < path.length; i++) {
        if (path[i - 1]![0] !== path[i]![0] && path[i - 1]![1] !== path[i]![1]) out.push(`wire ${w.n}: slanted segment`);
      }
    }
    w.ends.forEach((e, i) => {
      const box = boxes.get(e.part)!;
      const pin = ANCHORS.files[box.file]!.pins[e.pin]!;
      if (pin.x !== e.x || pin.y !== e.y) out.push(`wire ${w.n}: ${e.part}.${e.pin} differs from its anchor`);
      const path = whole ? w.paths[0]! : w.paths[i];
      if (!path) return;
      const [x, y] = whole && i === 1 ? path[path.length - 1]! : path[0]!;
      const next = whole && i === 1 ? path[path.length - 2]! : path[1]!;
      const flat = e.side === 'E' || e.side === 'W';
      const across = flat ? x - (box.ix + pin.x) : y - (box.iy + pin.y);
      const along = flat ? y - (box.iy + pin.y) : x - (box.ix + pin.x);
      if (across !== 0 || Math.abs(along) > PAD) out.push(`wire ${w.n}: path end ${x},${y} not on the pad ${e.part}.${e.pin}`);
      // The wire leaves the pad at a right angle to its edge, away from the part.
      const out1 = flat ? next[0] - x : next[1] - y;
      const side1 = flat ? next[1] - y : next[0] - x;
      const away = e.side === 'E' || e.side === 'S' ? 1 : -1;
      if (side1 !== 0 || out1 * away <= 0) out.push(`wire ${w.n}: leaves ${e.part}.${e.pin} along the edge`);
      const key = `${e.part}.${e.pin}`;
      onPad.set(key, [...(onPad.get(key) ?? []), { n: w.n, w: w.thick ? THICK : THIN, at: along }]);
    });
    if (w.labels.length < 1) out.push(`wire ${w.n}: no number`);
    if (whole && w.kind !== 'component' && w.labels.length !== 2) out.push(`wire ${w.n}: ${w.labels.length} numbers`);
  }
  for (const [key, list] of onPad) {
    list.sort((p, q) => p.at - q.at);
    if (list.length === 1 && list[0]!.at !== 0) out.push(`${key}: wire ${list[0]!.n} ends ${list[0]!.at} px off its point`);
    if (Math.abs(list[0]!.at + list[list.length - 1]!.at) > 1e-9) out.push(`${key}: wires not centred on the point`);
    for (let i = 1; i < list.length; i++) {
      if (list[i]!.at - list[i - 1]!.at !== pitch(list[i]!.w, list[i - 1]!.w)) out.push(`${key}: wires ${list[i - 1]!.n} and ${list[i]!.n} not a pitch apart`);
    }
  }

  // No wire runs through the drawing of a part it does not end on.
  const segs = segments(d);
  const own = new Map(d.wires.map((w) => [w.n, new Set(w.ends.map((e) => e.part))]));
  for (const s of segs) {
    for (const p of d.parts) {
      if (own.get(s.n)!.has(p.id)) continue;
      if (s.x1 < p.ix + p.iw && s.x2 > p.ix && s.y1 < p.iy + p.ih && s.y2 > p.iy) out.push(`wire ${s.n} crosses ${p.id}`);
    }
  }

  // No 2 wires share a line: parallel segments that run side by side keep `CLEAR` px between their edges.
  for (const axis of ['h', 'v'] as const) {
    const lines = segs
      .filter((s) => (axis === 'h' ? s.y1 === s.y2 && s.x1 !== s.x2 : s.x1 === s.x2 && s.y1 !== s.y2))
      .map((s) => (axis === 'h' ? { n: s.n, w: s.w, c: s.y1, a: s.x1, b: s.x2 } : { n: s.n, w: s.w, c: s.x1, a: s.y1, b: s.y2 }))
      .sort((p, q) => p.c - q.c);
    for (let i = 0; i < lines.length; i++) {
      for (let j = i + 1; j < lines.length && lines[j]!.c - lines[i]!.c < THICK + CLEAR; j++) {
        const p = lines[i]!;
        const q = lines[j]!;
        if (p.n !== q.n && p.a < q.b && q.a < p.b && q.c - p.c < pitch(p.w, q.w)) {
          out.push(`wires ${p.n} and ${q.n} run ${q.c - p.c} px apart at ${axis} ${p.c}`);
        }
      }
    }
  }
  if (d.clashes.length > 0) out.push(`clashes: ${d.clashes.map((c) => c.join('/')).join(' ')}`);

  // Bundles: the vertical lines of a channel between 2 columns keep the same clear distance, thick or thin.
  const spans = d.columns.map((ids) => {
    const own = ids.map((id) => boxes.get(id)!);
    return { x1: Math.min(...own.map((b) => b.x)), x2: Math.max(...own.map((b) => b.x + b.w)) };
  });
  for (let chan = 0; chan <= spans.length; chan++) {
    const from = chan === 0 ? -Infinity : spans[chan - 1]!.x2 + BAND;
    const to = chan === spans.length ? Infinity : spans[chan]!.x1 - BAND;
    const trunks = new Map<number, Seg>();
    for (const s of segs) if (s.x1 === s.x2 && s.y1 !== s.y2 && s.x1 >= from && s.x1 <= to) trunks.set(s.x1, s);
    const xs = [...trunks.keys()].sort((p, q) => p - q);
    // A wire that passes the channel without a bend has a track and no vertical line: its track stays empty.
    const bent = new Set([...trunks.values()].map((s) => s.n));
    const straight = xs.length === 0 ? [] : segs.filter((s) => s.y1 === s.y2 && s.x1 < xs[0]! && s.x2 > xs[xs.length - 1]! && !bent.has(s.n));
    let spare = [...new Map(straight.map((s) => [s.n, s.w])).values()].reduce((n, w) => n + w + CLEAR, 0);
    for (let i = 1; i < xs.length; i++) {
      const a = trunks.get(xs[i - 1]!)!;
      const b = trunks.get(xs[i]!)!;
      const more = b.x1 - a.x1 - (a.w + b.w) / 2 - CLEAR;
      const whole = [0, 1, 2, 3].some((thick) => more - thick * (THICK + CLEAR) >= 0 && (more - thick * (THICK + CLEAR)) % (THIN + CLEAR) === 0);
      spare -= more;
      if (more < 0 || !whole || spare < 0) out.push(`channel ${chan}: wires ${a.n} and ${b.n} are ${more + CLEAR} px apart`);
    }
  }

  // Stroke width and colour: a wire of the class power is thick, a plus wire red, a minus wire black, no other wire is.
  const byN = new Map(setup.wires.map((w) => [w.n, w]));
  for (const w of d.wires) {
    const wire = byN.get(w.n)!;
    if (w.thick !== (wire.cls === 'power')) out.push(`wire ${w.n}: ${w.thick ? 'thick' : 'thin'} with the class ${wire.cls}`);
    if (wire.kind === 'component') continue;
    if ((w.colour === PLUS) !== PLUS_NETS.has(wire.net)) out.push(`wire ${w.n}: ${w.colour} on the net ${wire.net}`);
    if ((w.colour === MINUS) !== MINUS_NETS.has(wire.net)) out.push(`wire ${w.n}: ${w.colour} on the net ${wire.net}`);
  }

  // Numbers: inside the picture, no 2 overlap.
  const labels = d.wires.flatMap((w) => w.labels.map((l) => ({ n: w.n, ...l })));
  for (const l of labels) {
    if (l.x - LABEL.w / 2 < 0 || l.x + LABEL.w / 2 > d.w || l.y - LABEL.h / 2 < 0 || l.y + LABEL.h / 2 > d.h) {
      out.push(`number ${l.n} outside the picture`);
    }
  }
  for (let i = 0; i < labels.length; i++) {
    for (let j = i + 1; j < labels.length; j++) {
      const a = labels[i]!;
      const b = labels[j]!;
      if (Math.abs(a.x - b.x) < LABEL.w && Math.abs(a.y - b.y) < LABEL.h) out.push(`numbers ${a.n} and ${b.n} overlap`);
    }
  }
  return out;
}

test('every diagram of the sweep holds the layout and routing rules', () => {
  assert.ok(samples.length > 1000, `${samples.length} setups`);
  const bad: string[] = [];
  let wires = 0;
  for (const s of samples) {
    const d = diagramOf(sceneOf(s.setup, ANCHORS));
    wires += d.wires.length;
    const found = breaks(d, s.setup);
    if (found.length > 0 && bad.length < 12) bad.push(`${describe(s.selection)}: ${found.slice(0, 4).join('; ')}`);
  }
  assert.ok(wires > samples.length, 'the sweep routes wires');
  assert.deepEqual(bad, []);
});

test('a diagram depends on the setup alone, not on the order of its parts and wires', () => {
  for (let i = 0; i < samples.length; i += 37) {
    const { setup } = samples[i]!;
    const a = diagramOf(sceneOf(setup, ANCHORS));
    const b = diagramOf(sceneOf(setup, ANCHORS));
    assert.deepEqual(a, b);
    const turned = { ...setup, parts: [...setup.parts].reverse(), wires: [...setup.wires].reverse() };
    const c = diagramOf(sceneOf(turned, ANCHORS));
    const sorted = (d: Diagram): Diagram => ({ ...d, parts: [...d.parts].sort((p, q) => p.id.localeCompare(q.id)) });
    assert.deepEqual(sorted(c), sorted(a), describe(samples[i]!.selection));
  }
});

test('the sweep draws every part with a drawing and every wire with 2 points', () => {
  const parts = new Set<string>();
  const wires = new Set<number>();
  const undrawn = new Set<number>();
  for (const s of samples) {
    const d = diagramOf(sceneOf(s.setup, ANCHORS));
    for (const p of d.parts) parts.add(p.id);
    for (const w of d.wires) wires.add(w.n);
    for (const n of d.undrawn) undrawn.add(n);
  }
  const drawn = new Set(Object.values(ANCHORS.files).map((r) => r.part).filter((p) => p !== null));
  for (const id of drawn) assert.ok(parts.has(id!), `${id} is in no diagram`);
  // Wire 31 ends on the address pad A0 of the INA3221, which the drawing does not carry.
  assert.deepEqual([...undrawn], [31]);
  for (const n of undrawn) assert.ok(!wires.has(n));
  assert.ok(wires.size >= 50, `${wires.size} wires`);
});

test('the base setup is the panel beside the coprocessor with 4 wires', () => {
  const setup = resolve(CATALOGUE, { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });
  const d = diagramOf(sceneOf(setup, ANCHORS));
  assert.deepEqual(d.wires.map((w) => w.n), [1, 2, 3, 4]);
  assert.deepEqual(d.undrawn, []);
  assert.deepEqual(breaks(d, setup), []);
  const panel = d.parts.find((p) => p.id === 'panel')!;
  const copro = d.parts.find((p) => p.id === 'copro')!;
  assert.ok(panel.x + panel.w <= copro.x);
});

test('a scene without a wire and a scene without a part give a diagram', () => {
  const setup = resolve(CATALOGUE, { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });
  const bare = diagramOf(sceneOf({ ...setup, wires: [] }, ANCHORS));
  assert.equal(bare.wires.length, 0);
  assert.equal(bare.parts.length, diagramOf(sceneOf(setup, ANCHORS)).parts.length);
  const empty = diagramOf({ parts: [], wires: [], undrawn: [] });
  assert.deepEqual([empty.parts, empty.wires], [[], []]);
  assert.ok(empty.w > 0 && empty.h > 0);
});

test('a wire to a part that is not in the setup is reported, not drawn', () => {
  const setup = resolve(CATALOGUE, { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });
  const cut = { ...setup, parts: setup.parts.filter((r) => r.part.id !== 'panel') };
  const d = diagramOf(sceneOf(cut, ANCHORS));
  assert.deepEqual(d.wires, []);
  assert.deepEqual(d.undrawn, [1, 2, 3, 4]);
});

test('simplify drops repeated points and points on a straight run', () => {
  const pts: Pt[] = [[0, 0], [0, 0], [4, 0], [8, 0], [8, 0], [8, 8], [8, 16], [12, 20]];
  assert.deepEqual(simplify(pts), [[0, 0], [8, 0], [8, 16], [12, 20]]);
  assert.deepEqual(simplify([[1, 1]]), [[1, 1]]);
  assert.deepEqual(simplify([]), []);
});

test('pathOf rounds a right angle by 2 px, never by more than half a segment', () => {
  assert.equal(RADIUS, 2);
  assert.equal(pathOf([[0, 0], [20, 0], [20, 20]]), 'M0 0L18 0Q20 0 20 2L20 20');
  assert.equal(pathOf([[0, 0], [2, 0], [2, 20]]), 'M0 0L1 0Q2 0 2 1L2 20');
  assert.equal(pathOf([[0, 0], [20, 0], [20, 20]], 0), 'M0 0L20 0Q20 0 20 0L20 20');
  assert.equal(pathOf([[0, 0], [10, 0]]), 'M0 0L10 0');
});

test('a wire that carries load current is thick, in the scene and in the markup, and the legend names both widths', () => {
  // Every wire of the data file: the class power is the load current of the ESC or of the servo.
  const load = CATALOGUE.wires.filter((w) => w.cls === 'power').map((w) => w.n);
  assert.deepEqual(load, [9, 10, 14, 15, 19, 20, 21, 22, 23, 28, 29, 30, 45, 46, 47, 56, 57, 58]);
  const seen = new Map<number, boolean>();
  for (const s of samples) for (const w of sceneOf(s.setup, ANCHORS).wires) seen.set(w.n, w.thick);
  for (const w of CATALOGUE.wires) {
    if (seen.has(w.n)) assert.equal(seen.get(w.n), load.includes(w.n), `wire ${w.n}`);
  }
  for (const n of load) assert.ok(seen.has(n), `wire ${n} is in no scene`);
  // No signal and no supply of a board is thick.
  for (const w of CATALOGUE.wires) if (seen.get(w.n)) assert.equal(w.cls, 'power');
  const html = picture(view(FULL));
  const classes = new Map([...html.matchAll(/<g class="dw k-\w+( thick)?" data-wire="(\d+)"/g)].map((m) => [Number(m[2]), m[1] !== undefined]));
  for (const [n, thick] of classes) assert.equal(thick, load.includes(n), `wire ${n}`);
  assert.ok([...classes.values()].filter(Boolean).length >= 6, `${[...classes.values()].filter(Boolean).length} thick wires`);
  for (const lang of ['en', 'de'] as Lang[]) {
    const l = legend(lang);
    assert.match(l, /stroke-width="7"\/><\/svg><span>[^<]+<\/span><\/li><li>.*?stroke-width="3"\/><\/svg><span>/);
    assert.ok(l.includes(lang === 'en' ? 'power line' : 'Leistungsleitung') && l.includes(lang === 'en' ? 'signal' : 'Signal'));
    assert.ok(picture(view({}, lang)).includes(l));
  }
});

test('pads closer than their wires are wide: the leads bend at right angles and share no line', () => {
  const part = (id: string, column: 'L' | 'C' | 'R', order: number) => ({ id, file: `${id}.svg`, w: 120, h: 120, column, order, name: { en: id, de: id } });
  const pin = (p: string, name: string, x: number, y: number, side: 'E' | 'W' | 'N' | 'S') => ({ part: p, pin: name, x, y, side });
  const wire = (n: number, thick: boolean, a: ReturnType<typeof pin>, b: ReturnType<typeof pin>) =>
    ({ n, kind: 'wire' as const, colour: 'blue', thick, a: [a], b: [b] });
  // 4 pads 3 px apart on the east edge of `left`, 4 pads 3 px apart on the north edge of `right`.
  const scene = {
    parts: [part('left', 'L', 1), part('copro', 'C', 1), part('right', 'R', 1)],
    wires: [0, 1, 2, 3].map((i) => wire(i + 1, i % 2 === 0, pin('left', `p${i}`, 110, 40 + 3 * i, 'E'), pin('copro', `q${i}`, 10, 30 + 20 * i, 'W')))
      .concat([0, 1, 2, 3].map((i) => wire(i + 5, i % 2 === 1, pin('right', `r${i}`, 40 + 3 * i, 10, 'N'), pin('copro', `s${i}`, 110, 30 + 20 * i, 'E')))),
    undrawn: [],
  };
  const d = diagramOf(scene);
  assert.deepEqual(d.clashes, []);
  const segs = d.wires.flatMap((w) => w.paths.flatMap((path) => path.slice(1).map((b, i) => ({ n: w.n, w: w.thick ? THICK : THIN, a: path[i]!, b, lead: i === 0 || i === path.length - 2 }))));
  for (const s of segs) assert.ok(s.a[0] === s.b[0] || s.a[1] === s.b[1], `wire ${s.n} slants`);
  // The first wire of each edge starts on its pad and runs straight. The others bend once to their lane.
  assert.equal(d.wires[0]!.paths[0]![0]![1], d.wires[0]!.paths[0]![1]![1]);
  for (const n of [2, 3, 4]) assert.ok(d.wires[n - 1]!.paths[0]!.length >= 4, `wire ${n}`);
  for (let i = 0; i < segs.length; i++) {
    for (let j = i + 1; j < segs.length; j++) {
      const p = segs[i]!;
      const q = segs[j]!;
      // 2 leads that start on pads 3 px apart run 3 px apart up to their bends: that part is the pads themselves.
      if (p.n === q.n || (p.lead && q.lead)) continue;
      for (const k of [0, 1] as const) {
        const o = k === 0 ? 1 : 0;
        if (p.a[k] !== p.b[k] || q.a[k] !== q.b[k]) continue;
        const lap = Math.min(Math.max(p.a[o], p.b[o]), Math.max(q.a[o], q.b[o])) - Math.max(Math.min(p.a[o], p.b[o]), Math.min(q.a[o], q.b[o]));
        if (lap > 0) assert.ok(Math.abs(p.a[k] - q.a[k]) >= pitch(p.w, q.w), `wires ${p.n} and ${q.n} are ${Math.abs(p.a[k] - q.a[k])} px apart`);
      }
    }
  }
});

test('2 wires that cross over in 1 channel at 1 height move the next column, and a pair that no shift parts is reported', () => {
  const part = (id: string, column: 'L' | 'C' | 'R') => ({ id, file: `${id}.svg`, w: 120, h: 120, column, order: 1, name: { en: id, de: id } });
  const pin = (p: string, name: string, y: number, side: 'E' | 'W') => ({ part: p, pin: name, x: side === 'E' ? 110 : 10, y, side });
  const wire = (n: number, a: ReturnType<typeof pin>, b: ReturnType<typeof pin>) => ({ n, kind: 'wire' as const, colour: 'blue', thick: false, a: [a], b: [b] });
  // Wire 1 runs from y 40 to y 80, wire 2 from y 80 to y 40: without a shift each line from the left meets a line from the right.
  const cross = {
    parts: [part('left', 'L'), part('copro', 'C')],
    wires: [wire(1, pin('left', 'a', 40, 'E'), pin('copro', 'b', 80, 'W')), wire(2, pin('left', 'c', 80, 'E'), pin('copro', 'd', 40, 'W'))],
    undrawn: [],
  };
  const d = diagramOf(cross);
  assert.deepEqual(d.clashes, []);
  const y = (id: string): number => d.parts.find((p) => p.id === id)!.y;
  assert.equal(y('copro') - y('left'), 8);
  // Pads every 4 px on both edges, joined crosswise: every shift leaves a pair.
  const dense = {
    parts: cross.parts,
    wires: Array.from({ length: 24 }, (_, i) => wire(i + 1, pin('left', `a${i}`, 10 + 4 * i, 'E'), pin('copro', `b${i}`, 10 + 4 * (23 - i), 'W'))),
    undrawn: [],
  };
  const e = diagramOf(dense);
  assert.ok(e.clashes.length > 0);
  for (const [a, b] of e.clashes) assert.ok(a < b && a >= 1 && b <= 24);
  assert.equal(y('left'), e.parts.find((p) => p.id === 'left')!.y);
});

test('the view stays between the fit and the largest scale and never shows a blank edge', () => {
  const size = { w: 1000, h: 500 };
  assert.deepEqual(fit(), { k: 1, x: 0, y: 0 });
  assert.deepEqual(clamp({ k: 0.2, x: 50, y: -50 }, size, 4), { k: 1, x: 0, y: 0 });
  assert.deepEqual(clamp({ k: 9, x: 99, y: -9999 }, size, 4), { k: 4, x: 0, y: -1500 });
  assert.deepEqual(clamp({ k: NaN, x: Infinity, y: NaN }, size, 4), { k: 1, x: 0, y: 0 });
  // A largest scale below 1 leaves the fit.
  assert.deepEqual(clamp({ k: 3, x: -10, y: 0 }, size, 0.5), { k: 1, x: 0, y: 0 });
  let v = fit();
  for (let i = 0; i < 20; i++) v = zoomAt(v, ZOOM_STEP, 500, 250, size, 4);
  assert.equal(v.k, 4);
  // The point under the zoom stays in place.
  assert.deepEqual([(500 - v.x) / v.k, (250 - v.y) / v.k].map(Math.round), [500, 250]);
  assert.deepEqual(zoomAt(fit(), 2, 500, 250, size, 4), { k: 2, x: -500, y: -250 });
  for (let i = 0; i < 20; i++) v = pan(v, -size.w * PAN_STEP, -size.h * PAN_STEP, size, 4);
  assert.deepEqual(v, { k: 4, x: -3000, y: -1500 });
  for (let i = 0; i < 40; i++) v = pan(v, size.w * PAN_STEP, size.h * PAN_STEP, size, 4);
  assert.deepEqual(v, { k: 4, x: 0, y: 0 });
  for (let i = 0; i < 20; i++) v = zoomAt(v, 1 / ZOOM_STEP, 0, 0, size, 4);
  assert.deepEqual(v, fit());
  assert.deepEqual(pan(fit(), 100, 100, size, 4), fit());
});

test('centreOn brings a point into the box and shows tells whether it is there', () => {
  const size = { w: 1000, h: 500 };
  const v = { k: 4, x: 0, y: 0 };
  assert.equal(shows(v, 900, 400, size), false);
  const c = centreOn(v, 900, 400, size, 4);
  assert.equal(shows(c, 900, 400, size), true);
  assert.deepEqual(c, { k: 4, x: -3000, y: -1350 });
  assert.equal(shows(fit(), 0, 0, size), true);
  assert.equal(shows(fit(), 1000, 500, size), true);
  assert.equal(transformOf({ k: 1.23456, x: 1.006, y: -2 }), 'translate(1.01 -2) scale(1.235)');
});

function view(part: Partial<Selection>, lang: Lang = 'en'): View {
  const selection: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS }, ...part };
  const setup = resolve(CATALOGUE, selection);
  return { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
}

const FULL: Partial<Selection> = {
  functions: CATALOGUE.functions.map((f) => f.id),
  owned: CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id),
};

test('the picture carries a title, a description, the table as its text and 1 focusable group per wire', () => {
  for (const lang of ['en', 'de'] as Lang[]) {
    const v = view(FULL, lang);
    const html = picture(v);
    assert.match(html, /<svg class="picture"[^>]* aria-labelledby="dia-t" aria-describedby="dia-d wires-cap"/);
    assert.match(html, /<title id="dia-t">[^<]+<\/title><desc id="dia-d">[^<]+<\/desc>/);
    const groups = [...html.matchAll(/<g class="dw k-(\w+)(?: thick)?" data-wire="(\d+)"[^>]* tabindex="(-?\d)" role="button" aria-pressed="false" aria-label="([^"]+)">/g)];
    const drawn = v.setup.wires.filter((w) => w.n !== 31);
    assert.deepEqual(groups.map((g) => Number(g[2])), drawn.map((w) => w.n));
    // 1 wire is in the tab order; the arrow keys reach the others.
    assert.deepEqual(groups.map((g) => g[3]).filter((x) => x === '0').length, 1);
    for (const g of groups) assert.ok(g[4]!.includes(`${g[2]}:`), g[4]);
    assert.ok(html.includes(STRINGS.diaUndrawn[lang === 'en' ? 0 : 1].replace('{n}', '31')));
    // German drawings come from the folder de/.
    const images = [...html.matchAll(/<image href="([^"]+)"/g)].map((m) => m[1]!);
    assert.equal(images.length, sceneOf(v.setup, ANCHORS).parts.length);
    assert.equal(images.length, 16);
    for (const href of images) assert.equal(href.startsWith('drawings/de/'), lang === 'de', href);
    for (const id of ['out', 'in', 'fit', 'left', 'up', 'down', 'right']) assert.ok(html.includes(`data-dia="${id}"`), id);
  }
});

test('a black or white wire has a casing under its core, a coloured wire has none', () => {
  const html = picture(view(FULL));
  const group = (n: number): string => html.match(new RegExp(`<g class="dw [^>]*data-wire="${n}".*?</g>`))![0];
  assert.match(group(3), /<path class="case"[^>]*stroke="#[0-9a-f]{6}"\/><path class="core"/);
  assert.ok(!group(1).includes('class="case"'));
  assert.equal(swatch(null), '');
  assert.equal(swatch('no such colour'), '');
  assert.match(swatch('black'), /^<svg class="sw"[^>]*aria-hidden="true"/);
});

test('each table row carries a button with the number of its wire', () => {
  for (const lang of ['en', 'de'] as Lang[]) {
    const v = view(FULL, lang);
    const html = result(v, 'https://example.invalid/', []);
    const buttons = [...html.matchAll(/<button type="button" class="wn" data-wire="(\d+)" aria-pressed="false" aria-label="([^"]+)"><b>(\d+)<\/b>/g)];
    assert.deepEqual(buttons.map((b) => Number(b[1])), v.setup.wires.map((w) => w.n));
    for (const b of buttons) assert.equal(b[1], b[3]);
    assert.ok(html.includes('<caption id="wires-cap">'));
  }
});

test('the result shows the picture first and keeps the schematic in the page', () => {
  const v = view({});
  const pic = result(v, 'https://example.invalid/', []);
  assert.match(pic, /id="v-pic" aria-pressed="true"/);
  assert.match(pic, /<div class="view" id="view-pic">/);
  assert.match(pic, /<div class="view" id="view-sch" hidden>/);
  const sch = result(v, 'https://example.invalid/', [], 'sch');
  assert.match(sch, /id="v-sch" aria-pressed="true"/);
  assert.match(sch, /<div class="view" id="view-pic" hidden>/);
  assert.match(sch, /<div class="view" id="view-sch">/);
  for (const html of [pic, sch]) assert.ok(html.includes('svg class="picture"') && html.includes('class="pan"'));
});
