// Geometry checks on one drawing, and the sweep of selections they run over.
// Used by layout.test.ts. A finding is one line of text; no finding means the
// drawing holds every rule below.

import type { Catalogue, Core, Options, Selection, Setup } from '../contract.ts';
import { WIDTH } from '../src/draw/svg.ts';
import { SUB_CHAR } from '../src/draw/layout.ts';
import type { Layout, Pt, WirePath } from '../src/draw/layout.ts';

type Seg = { n: number; wire: WirePath; x1: number; y1: number; x2: number; y2: number; half: number };

/** Smallest distance in px between the centre lines of two parallel runs of different nets. */
export const CLEARANCE = 5;

function segments(l: Layout): Seg[] {
  const out: Seg[] = [];
  for (const w of l.wires) {
    for (let i = 1; i < w.points.length; i++) {
      const p = w.points[i - 1] as Pt;
      const q = w.points[i] as Pt;
      out.push({
        n: w.n, wire: w, half: WIDTH[w.cls] / 2,
        x1: Math.min(p[0], q[0]), y1: Math.min(p[1], q[1]), x2: Math.max(p[0], q[0]), y2: Math.max(p[1], q[1]),
      });
    }
  }
  return out;
}

const refs = (w: WirePath): string[] => (w.b === null ? [w.a] : [w.a, w.b]);
const sharesEnd = (p: WirePath, q: WirePath): boolean => refs(p).some((r) => refs(q).includes(r));
const horizontal = (s: Seg): boolean => s.y1 === s.y2;

function hits(s: Seg, x1: number, y1: number, x2: number, y2: number): boolean {
  return s.x1 - s.half < x2 && s.x2 + s.half > x1 && s.y1 - s.half < y2 && s.y2 + s.half > y1;
}

export function findings(l: Layout, setup: Setup): string[] {
  const out: string[] = [];
  const g = l.geo;
  const segs = segments(l);

  if (l.skipped.length > 0) out.push(`wires without a place: ${l.skipped.join(', ')}`);
  const drawn = l.wires.map((w) => w.n).join(',');
  const wanted = [...setup.wires].map((w) => w.n).sort((a, b) => a - b).join(',');
  if (drawn !== wanted) out.push(`drawn wires ${drawn} differ from the setup's wires ${wanted}`);

  for (const w of l.wires) {
    if (w.points.length < 2) out.push(`wire ${w.n} has no path`);
    if (w.title.length === 0) out.push(`wire ${w.n} has no title`);
  }
  for (const s of segs) {
    if (s.x1 !== s.x2 && s.y1 !== s.y2) out.push(`wire ${s.n} has a slanted segment`);
    if (s.x1 - s.half < 0 || s.x2 + s.half > l.width || s.y1 - s.half < 0 || s.y2 + s.half > l.height) {
      out.push(`wire ${s.n} leaves the canvas at ${s.x1},${s.y1} ${s.x2},${s.y2}`);
    }
    if (s.x1 === s.x2 && s.y1 !== s.y2) {
      const x = s.x1;
      const outer = x >= 10 && x <= g.outerLane0;
      const left = x >= g.leftLane0 && x < g.coproX;
      const right = x > g.coproX + g.coproW && x < g.rightX - g.pillOffset - g.pillW / 2;
      if (!outer && !left && !right) out.push(`wire ${s.n} runs vertically outside a gutter at x=${x}`);
    }
  }

  // No two wires of different nets on one line, and none closer than CLEARANCE beside each other.
  for (let i = 0; i < segs.length; i++) {
    for (let j = i + 1; j < segs.length; j++) {
      const a = segs[i] as Seg;
      const b = segs[j] as Seg;
      if (a.n === b.n || sharesEnd(a.wire, b.wire)) continue;
      const ha = horizontal(a);
      if (ha !== horizontal(b)) continue;
      const [a1, a2, b1, b2, da, db] = ha ? [a.x1, a.x2, b.x1, b.x2, a.y1, b.y1] : [a.y1, a.y2, b.y1, b.y2, a.x1, b.x1];
      const shared = Math.min(a2, b2) - Math.max(a1, b1);
      if (shared <= 0) continue;
      const where = ha ? `y=${da}/${db}, x ${Math.max(a1, b1)} to ${Math.min(a2, b2)}` : `x=${da}/${db}, y ${Math.max(a1, b1)} to ${Math.min(a2, b2)}`;
      if (da === db) out.push(`wires ${a.n} and ${b.n} share a line: ${where}`);
      else if (Math.abs(da - db) < CLEARANCE) out.push(`wires ${a.n} and ${b.n} run ${Math.abs(da - db)} px apart: ${where}`);
    }
  }

  // Pills: inside the canvas, apart from each other, and on no wire of another pin.
  const hw = g.pillW / 2;
  const hh = g.pillH / 2;
  l.pills.forEach((p, i) => {
    if (p.x - hw < 0 || p.x + hw > l.width || p.y - hh < 0 || p.y + hh > l.height) out.push(`pill ${p.n} leaves the canvas`);
    for (const q of l.pills.slice(i + 1)) {
      if (Math.abs(p.x - q.x) < g.pillW - 1.5 && Math.abs(p.y - q.y) < g.pillH) out.push(`pills ${p.n} and ${q.n} overlap at ${p.x},${p.y}`);
    }
    for (const s of segs) {
      if (refs(s.wire).includes(p.at)) continue;
      if (hits(s, p.x - hw, p.y - hh, p.x + hw, p.y + hh)) out.push(`pill ${p.n} at ${p.x},${p.y} covers wire ${s.n}`);
    }
    if (!l.wires.some((w) => w.n === p.n)) out.push(`pill ${p.n} has no wire`);
  });
  for (const w of l.wires) {
    const want = refs(w).filter((r) => !/^(pad|can)\./.test(r)).length + (w.route === 'Z' ? 1 : 0);
    const have = l.pills.filter((p) => p.n === w.n).length;
    if (have !== want) out.push(`wire ${w.n} has ${have} pills, ${want} expected`);
  }

  // Cross marks of stubs lie on no other wire.
  for (const w of l.wires) {
    if (!w.cross) continue;
    const r = g.cross / 2;
    for (const s of segs) {
      if (s.n !== w.n && hits(s, w.cross[0] - r, w.cross[1] - r, w.cross[0] + r, w.cross[1] + r)) {
        out.push(`cross mark of wire ${w.n} at ${w.cross[0]},${w.cross[1]} lies on wire ${s.n}`);
      }
    }
  }

  // A junction dot sits on at least 2 wires.
  for (const d of l.dots) {
    const on = new Set(segs.filter((s) => s.x1 <= d[0] && s.x2 >= d[0] && s.y1 <= d[1] && s.y2 >= d[1]).map((s) => s.n));
    if (on.size < 2) out.push(`dot at ${d[0]},${d[1]} joins ${on.size} wires`);
  }

  // Modules: inside the canvas, apart, in slot order, texts inside the box.
  const room = g.moduleW - 16;
  l.modules.forEach((m, i) => {
    if (m.x < 0 || m.x + m.w > l.width || m.y < g.top || m.y + m.h > l.height - g.bottom) out.push(`module ${m.id} leaves the canvas`);
    for (const o of l.modules.slice(i + 1)) {
      if (o.column === m.column && o.y < m.y + m.h + g.gap) out.push(`modules ${m.id} and ${o.id} are closer than ${g.gap} px`);
    }
    if (m.title.length * 7.6 > room) out.push(`title of ${m.id} is wider than the module`);
    for (const line of m.sub) {
      const width = line.length * SUB_CHAR;
      if (width > room && m.subFit === null) out.push(`sub line of ${m.id} is wider than the module`);
      if (width > room * 1.2) out.push(`sub line of ${m.id} is squeezed by more than 20 %`);
    }
    if (m.y + 31 + (m.sub.length - 1) * 13 > m.y + (m.pins.length > 0 ? 36 : m.h - 5)) out.push(`sub lines of ${m.id} leave the header`);
    for (const p of m.pins) if (p.drawn && p.label.length * 6.6 > room) out.push(`pin label ${m.id}.${p.id} is wider than the module`);
  });
  if (l.copro.y + l.copro.h > l.height - g.bottom) out.push('coprocessor leaves the canvas');
  if (l.copro.sub.length * SUB_CHAR > g.coproW - 16) out.push('sub line of the coprocessor is wider than its box');
  if (l.copro.title.length * 7.6 > g.coproW - 16) out.push('title of the coprocessor is wider than its box');
  return out;
}

const OPTION_SETS: Options[] = [];
for (const connection of ['pads', 'osoyoo'] as const) {
  for (const ina228Board of ['matek', 'xt90'] as const) {
    for (const escPower of ['battery', 'pd'] as const) {
      for (const servoPower of ['external', 'bench'] as const) OPTION_SETS.push({ connection, ina228Board, escPower, servoPower, servoPort: 'outputs' });
    }
  }
}

function subsets(ids: string[]): string[][] {
  const out: string[][] = [];
  for (let mask = 0; mask < 1 << ids.length; mask++) out.push(ids.filter((_, i) => (mask >> i) & 1));
  return out;
}

export type Sample = { selection: Selection; setup: Setup; key: string };

/**
 * Every subset of the functions and every subset of the selectable parts, each
 * with every option combination, resolved and reduced to the distinct setups.
 */
export function sweep(core: Core, catalogue: Catalogue = core.CATALOGUE): Sample[] {
  const seen = new Map<string, Sample>();
  const selections: Selection[] = [];
  // The functions that need no part and drive no test object add no part and no wire.
  const fnSets = subsets(catalogue.functions.filter((f) => f.needs.length > 0 || f.drives !== undefined).map((f) => f.id));
  const partSets = subsets(catalogue.parts.filter((p) => p.selectable).map((p) => p.id));
  for (const options of OPTION_SETS) {
    for (const functions of fnSets) selections.push({ functions, owned: [], options });
    for (const owned of partSets) selections.push({ functions: [], owned, options });
  }
  for (const selection of selections) {
    const setup = core.resolve(catalogue, selection);
    const key = [
      setup.parts.map((p) => p.part.id).join(','),
      setup.wires.map((w) => w.n).join(','),
      setup.options.map((o) => o.value).join(','),
    ].join('|');
    if (!seen.has(key)) seen.set(key, { selection, setup, key });
  }
  return [...seen.values()];
}

export function describe(selection: Selection): string {
  const o = selection.options;
  return `f=${selection.functions.join(',')} p=${selection.owned.join(',')} o=${o.connection},${o.ina228Board},${o.escPower},${o.servoPower}`;
}
