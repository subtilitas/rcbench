// The pictorial wiring view: layout and router over every setup of the sweep,
// then the limits of pan and zoom and the markup.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection, Setup } from '../contract.ts';
import { withoutWirePoint } from '../drawings/wires.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { ANCHORS } from '../src/diagram/anchors.ts';
import { LABEL, diagramOf, simplify } from '../src/diagram/route.ts';
import type { Diagram, Pt } from '../src/diagram/route.ts';
import { pathOf, picture, swatch } from '../src/diagram/picture.ts';
import { sceneOf } from '../src/diagram/scene.ts';
import { PAN_STEP, ZOOM_STEP, centreOn, clamp, fit, pan, shows, transformOf, zoomAt } from '../src/diagram/view.ts';
import { STRINGS } from '../src/i18n.ts';
import { optionsOf } from '../src/names.ts';
import type { View } from '../src/view/chooser.ts';
import { result } from '../src/view/result.ts';
import { describe, sweep } from './geometry.ts';

const samples = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS });

/** Least distance in px between 2 parallel wire segments of 2 wires that run side by side. */
const PITCH = 4;

type Seg = { n: number; x1: number; y1: number; x2: number; y2: number };

function segments(d: Diagram): Seg[] {
  const out: Seg[] = [];
  for (const w of d.wires) {
    for (const path of w.paths) {
      for (let i = 1; i < path.length; i++) {
        const [ax, ay] = path[i - 1]!;
        const [bx, by] = path[i]!;
        out.push({ n: w.n, x1: Math.min(ax, bx), y1: Math.min(ay, by), x2: Math.max(ax, bx), y2: Math.max(ay, by) });
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

  // Ends: each path starts and ends on an anchor of its drawing, moved by the place of the part.
  for (const w of d.wires) {
    const spots = new Set(w.ends.map((e) => {
      const box = boxes.get(e.part)!;
      const pin = ANCHORS.files[box.file]!.pins[e.pin]!;
      if (pin.x !== e.x || pin.y !== e.y) out.push(`wire ${w.n}: ${e.part}.${e.pin} differs from its anchor`);
      return `${box.ix + pin.x},${box.iy + pin.y}`;
    }));
    const starts = new Set<string>();
    for (const path of w.paths) {
      if (path.length < 2) out.push(`wire ${w.n}: path of ${path.length} point`);
      for (const [x, y] of path) {
        if (!(x >= 0 && x <= d.w && y >= 0 && y <= d.h)) out.push(`wire ${w.n}: point ${x},${y} outside the picture`);
      }
      starts.add(path[0]!.join(','));
      if (w.paths.length === 1 && w.ends.length === 2) starts.add(path[path.length - 1]!.join(','));
    }
    for (const s of spots) if (!starts.has(s)) out.push(`wire ${w.n}: no path on the anchor ${s}`);
    for (const s of starts) if (!spots.has(s)) out.push(`wire ${w.n}: path end ${s} on no anchor`);
    if (w.labels.length < 1) out.push(`wire ${w.n}: no number`);
    if (w.paths.length === 1 && w.ends.length === 2 && w.kind !== 'component' && w.labels.length !== 2) {
      out.push(`wire ${w.n}: ${w.labels.length} numbers`);
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

  // No 2 wires share a line: parallel segments that run side by side keep the pitch.
  for (const axis of ['h', 'v'] as const) {
    const lines = segs
      .filter((s) => (axis === 'h' ? s.y1 === s.y2 && s.x1 !== s.x2 : s.x1 === s.x2 && s.y1 !== s.y2))
      .map((s) => (axis === 'h' ? { n: s.n, c: s.y1, a: s.x1, b: s.x2 } : { n: s.n, c: s.x1, a: s.y1, b: s.y2 }))
      .sort((p, q) => p.c - q.c);
    for (let i = 0; i < lines.length; i++) {
      for (let j = i + 1; j < lines.length && lines[j]!.c - lines[i]!.c < PITCH; j++) {
        const p = lines[i]!;
        const q = lines[j]!;
        if (p.n !== q.n && p.a < q.b && q.a < p.b) out.push(`wires ${p.n} and ${q.n} share a line at ${axis} ${p.c}`);
      }
    }
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

test('pathOf rounds a right angle, never by more than half a segment', () => {
  assert.equal(pathOf([[0, 0], [20, 0], [20, 20]], 6), 'M0 0L14 0Q20 0 20 6L20 20');
  assert.equal(pathOf([[0, 0], [4, 0], [4, 20]], 6), 'M0 0L2 0Q4 0 4 2L4 20');
  // A diagonal keeps its corner.
  assert.equal(pathOf([[0, 0], [5, 5], [20, 5]], 6), 'M0 0L5 5L20 5');
  assert.equal(pathOf([[0, 0], [10, 0]]), 'M0 0L10 0');
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
    const groups = [...html.matchAll(/<g class="dw k-(\w+)" data-wire="(\d+)"[^>]* tabindex="(-?\d)" role="button" aria-pressed="false" aria-label="([^"]+)">/g)];
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
