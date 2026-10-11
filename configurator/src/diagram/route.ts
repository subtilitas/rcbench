// The router of the pictorial wiring view. Pure and deterministic.
//
// A wire leaves its pad on the edge the drawing names, on a line of its own
// (its lane). From an east or west edge it runs straight into the channel
// beside the column. From a north or south edge it runs into the gap above or
// below the part, turns on a track of its own and runs to a channel. In a
// channel it runs on a vertical track of its own. To reach a channel further
// away it crosses each column between through a gap, on a track of its own.
//
// What this gives by construction:
// - a wire crosses no part other than the 2 it ends on,
// - 2 wires never share a line: tracks and lanes are 8 px apart, and lines of
//   neighbouring columns are 4 px apart,
// - 2 wires on 1 pad leave it side by side.

import type { Side } from '../../drawings/lib.ts';
import { BAND, U, columnsOf, emptySizes, place, trackY, trunkX } from './layout.ts';
import type { Box, Columns, Placement, Sizes } from './layout.ts';
import type { Scene, ScenePin, SceneWire } from './scene.ts';

export type Pt = [number, number];

/** A wire number on the picture: its centre. */
export type Label = { x: number; y: number };

/**
 * A routed wire. `paths`: 1 polyline from pad to pad, or 1 short lead per wire
 * point for a wire that ends open. `labels`: the places of the wire number,
 * 2 for a wire, 1 per lead for an open end. `ends`: the pads the paths start
 * and end on, in the order of the path points.
 */
export type Routed = {
  n: number;
  kind: SceneWire['kind'];
  colour: string | null;
  paths: Pt[][];
  labels: Label[];
  ends: ScenePin[];
};

export type Diagram = {
  w: number;
  h: number;
  parts: Array<Box & { file: string }>;
  wires: Routed[];
  undrawn: number[];
  /** Part ids per column, left to right. */
  columns: string[][];
  /** Index of the coprocessor column. */
  centre: number;
};

/** Size of a wire number. */
export const LABEL = { w: 16, h: 11 };
/** Distance of the bend that leads from a pad to its lane. */
const KNEE = 12;

type End = {
  n: number;
  which: number;
  pin: ScenePin;
  box: Box;
  /** True for an east or west edge. */
  flat: boolean;
  /** Lane in px of the drawing: y for a flat end, x otherwise. */
  lane: number;
  /** Step of the wire number away from the edge, so that numbers of neighbouring lanes do not overlap. */
  level: number;
  chan: number;
  gap: number;
  track: number;
  /** Where the other end lies along the edge, from the first placement. */
  hint: number;
};

type Cross = { col: number; gap: number; track: number };
type Trunk = { chan: number; index: number };
type Plan = {
  wire: SceneWire;
  mode: 'wire' | 'open' | 'bracket';
  ends: End[];
  cross: Cross[];
  trunks: Trunk[];
};

const isFlat = (side: Side): boolean => side === 'E' || side === 'W';
const sign = (side: Side): number => (side === 'E' || side === 'S' ? 1 : -1);

function endOf(wire: SceneWire, which: number, pin: ScenePin, p: Placement): End {
  const box = p.boxes[pin.part]!;
  const flat = isFlat(pin.side);
  return {
    n: wire.n, which, pin, box, flat, lane: 0, level: 0,
    chan: -1, gap: flat ? -1 : box.idx + (pin.side === 'S' ? 1 : 0), track: -1, hint: 0,
  };
}

/** Position of an end between the channels: a flat end lies in its channel, another end halfway through its column. */
const position = (e: End): number => (e.flat ? e.box.col + (e.pin.side === 'E' ? 1 : 0) : e.box.col + 0.5);

function plans(scene: Scene, p: Placement): Plan[] {
  const out: Plan[] = [];
  for (const wire of scene.wires) {
    if (wire.b === null) {
      const ends = wire.a.map((pin, i) => endOf(wire, i, pin, p));
      for (const e of ends) e.hint = e.flat ? e.pin.y : e.pin.x;
      out.push({ wire, mode: 'open', ends, cross: [], trunks: [] });
      continue;
    }
    const a = endOf(wire, 0, wire.a[0]!, p);
    const b = endOf(wire, 1, wire.b[0]!, p);
    a.hint = a.flat ? b.box.iy + b.pin.y : b.box.ix + b.pin.x;
    b.hint = b.flat ? a.box.iy + a.pin.y : a.box.ix + a.pin.x;
    const bracket = wire.kind === 'component' && a.pin.part === b.pin.part && a.pin.side === b.pin.side;
    if (!bracket) {
      a.chan = a.flat ? position(a) : position(b) <= a.box.col ? a.box.col : a.box.col + 1;
      b.chan = b.flat ? position(b) : position(a) <= b.box.col ? b.box.col : b.box.col + 1;
    }
    out.push({ wire, mode: bracket ? 'bracket' : 'wire', ends: [a, b], cross: [], trunks: [] });
  }
  return out;
}

/** Lanes and number steps of the ends on each edge of each part. */
function lanes(all: End[]): void {
  const edges = new Map<string, End[]>();
  for (const e of all) {
    const key = `${e.pin.part}|${e.pin.side}`;
    edges.set(key, [...(edges.get(key) ?? []), e]);
  }
  for (const ends of edges.values()) {
    const along = (e: End): number => (e.flat ? e.pin.y : e.pin.x);
    ends.sort((p, q) => along(p) - along(q) || (p.pin.pin < q.pin.pin ? -1 : p.pin.pin > q.pin.pin ? 1 : 0)
      || p.hint - q.hint || p.n - q.n || p.which - q.which);
    const flat = ends[0]!.flat;
    // Lines of a flat edge alternate by half a pitch from column to column.
    const offset = flat ? (ends[0]!.box.col % 2) * (U / 2) : 0;
    const clear = flat ? LABEL.h + 1 : LABEL.w + 1;
    let previous = -Infinity;
    for (let i = 0; i < ends.length; i++) {
      const e = ends[i]!;
      const group = ends.filter((x) => x.pin.pin === e.pin.pin);
      const ideal = along(e) + (group.indexOf(e) - (group.length - 1) / 2) * U;
      e.lane = Math.max(Math.round((ideal - offset) / U) * U + offset, previous + U);
      previous = e.lane;
      const taken = ends.slice(0, i).filter((x) => e.lane - x.lane < clear).map((x) => x.level);
      e.level = [0, 1, 2].find((level) => !taken.includes(level)) ?? 0;
    }
  }
}

const gapMid = (p: Placement, col: number, gap: number): number => p.cols[col]!.gapY[gap]! + p.cols[col]!.gapH[gap]! / 2;
const hintY = (e: End, p: Placement): number => (e.flat ? e.box.iy + e.lane : gapMid(p, e.box.col, e.gap));

/** The gap each wire crosses a column through: the one with the shortest way from the last point to the far end. */
function crossings(list: Plan[], p: Placement): void {
  for (const plan of list) {
    if (plan.mode !== 'wire') continue;
    const [a, b] = plan.ends as [End, End];
    const step = Math.sign(b.chan - a.chan);
    const target = hintY(b, p);
    let y = hintY(a, p);
    for (let chan = a.chan; chan !== b.chan; chan += step) {
      const col = step > 0 ? chan : chan - 1;
      let best = 0;
      let cost = Infinity;
      for (let gap = 0; gap < p.cols[col]!.gapY.length; gap++) {
        const mid = gapMid(p, col, gap);
        const c = Math.abs(mid - y) + Math.abs(mid - target);
        if (c < cost) {
          cost = c;
          best = gap;
        }
      }
      plan.cross.push({ col, gap: best, track: -1 });
      y = gapMid(p, col, best);
    }
  }
}

/**
 * Tracks of each gap, top to bottom: the ends of the part above, the crossing
 * wires, the ends of the part below. A lead of the part above then never meets
 * a lead of the part below. Inside a group the order keeps 2 leads that turn
 * the same way from crossing.
 */
function gapTracks(list: Plan[], sizes: Sizes): void {
  type Entry = { role: number; left: boolean; x: number; n: number; k: number; set: (track: number) => void };
  const gaps = new Map<string, Entry[]>();
  const add = (col: number, gap: number, entry: Entry): void => {
    const key = `${col}|${gap}`;
    gaps.set(key, [...(gaps.get(key) ?? []), entry]);
  };
  for (const plan of list) {
    for (const e of plan.ends) {
      if (e.flat) continue;
      if (plan.mode !== 'wire') {
        sizes.gapEnds[e.box.col]![e.gap] = true;
        continue;
      }
      add(e.box.col, e.gap, {
        role: e.pin.side === 'S' ? 0 : 2, left: e.chan === e.box.col, x: e.box.ix + e.lane, n: e.n, k: e.which,
        set: (track) => { e.track = track; },
      });
    }
    plan.cross.forEach((cross, k) => add(cross.col, cross.gap, {
      role: 1, left: false, x: 0, n: plan.wire.n, k, set: (track) => { cross.track = track; },
    }));
  }
  for (const [key, entries] of gaps) {
    entries.sort((p, q) => {
      if (p.role !== q.role) return p.role - q.role;
      if (p.role !== 1 && p.left !== q.left) return p.left ? -1 : 1;
      // Below a part: a lead that turns left takes a higher track the further left it starts. Above a part: the reverse.
      const along = p.role === 1 ? 0 : (p.x - q.x) * (p.left ? 1 : -1) * (p.role === 0 ? 1 : -1);
      return along || p.n - q.n || p.k - q.k;
    });
    entries.forEach((entry, i) => entry.set(i));
    const [col, gap] = key.split('|').map(Number) as [number, number];
    sizes.gaps[col]![gap] = entries.length;
  }
}

/** Tracks of each channel. The order nests wires that enter and leave on 1 side, and keeps 2 wires that run the same way from crossing. */
function chanTracks(list: Plan[], p: Placement, sizes: Sizes): void {
  type Term = { left: boolean; y: number };
  type Entry = { group: number; key: number; n: number; k: number; trunk: Trunk };
  const chans = new Map<number, Entry[]>();
  for (const plan of list) {
    for (const e of plan.ends) {
      if (e.flat && plan.mode !== 'wire') sizes.chanEnds[position(e)] = true;
    }
    if (plan.mode !== 'wire') continue;
    const [a, b] = plan.ends as [End, End];
    const endTerm = (e: End): Term => ({ left: e.flat ? e.pin.side === 'E' : e.chan === e.box.col + 1, y: hintY(e, p) });
    const crossTerm = (c: Cross, chan: number): Term => ({ left: c.col === chan - 1, y: gapMid(p, c.col, c.gap) });
    const step = Math.sign(b.chan - a.chan);
    const hops = Math.abs(b.chan - a.chan);
    for (let k = 0; k <= hops; k++) {
      const chan = a.chan + k * step;
      const t1 = k === 0 ? endTerm(a) : crossTerm(plan.cross[k - 1]!, chan);
      const t2 = k === hops ? endTerm(b) : crossTerm(plan.cross[k]!, chan);
      const span = Math.abs(t1.y - t2.y);
      let group: number;
      let key: number;
      if (t1.left && t2.left) {
        group = 0;
        key = span;
      } else if (!t1.left && !t2.left) {
        group = 3;
        key = -span;
      } else {
        const left = t1.left ? t1 : t2;
        const right = t1.left ? t2 : t1;
        group = left.y > right.y ? 1 : 2;
        key = left.y > right.y ? left.y : -left.y;
      }
      const trunk: Trunk = { chan, index: -1 };
      plan.trunks.push(trunk);
      chans.set(chan, [...(chans.get(chan) ?? []), { group, key, n: plan.wire.n, k, trunk }]);
    }
  }
  for (const [chan, entries] of chans) {
    entries.sort((p1, p2) => p1.group - p2.group || p1.key - p2.key || p1.n - p2.n || p1.k - p2.k);
    entries.forEach((entry, i) => { entry.trunk.index = i; });
    sizes.chans[chan] = entries.length;
  }
}

/** Points from the pad to the edge of its lane, and the place of the wire number. */
function lead(e: End, p: Placement): { points: Pt[]; label: Label; reach: Pt } {
  const box = p.boxes[e.pin.part]!;
  const ax = box.ix + e.pin.x;
  const ay = box.iy + e.pin.y;
  const dir = sign(e.pin.side);
  const points: Pt[] = [[ax, ay]];
  if (e.flat) {
    const y = box.iy + e.lane;
    const edge = dir > 0 ? box.x + box.w : box.x;
    const knee = Math.min(KNEE, Math.max(0, dir > 0 ? edge - ax : ax - edge));
    if (y !== ay) points.push([ax + dir * knee, y]);
    const label = { x: edge + dir * (LABEL.w / 2 + 3 + e.level * LABEL.w), y };
    return { points, label, reach: [edge + dir * (BAND - 2), y] };
  }
  const x = box.ix + e.lane;
  const edge = dir > 0 ? box.y + box.h : box.y;
  const inner = dir > 0 ? box.iy + box.ih : box.iy;
  const knee = Math.min(KNEE, Math.max(0, dir > 0 ? inner - ay : ay - inner));
  if (x !== ax) points.push([x, ay + dir * knee]);
  const label = { x, y: edge + dir * (LABEL.h / 2 + 3 + e.level * (LABEL.h + 1)) };
  return { points, label, reach: [x, edge + dir * (BAND - 2)] };
}

/** Drops a point that repeats its neighbour or lies on the straight line between its neighbours. */
export function simplify(points: Pt[]): Pt[] {
  const out: Pt[] = [];
  for (const point of points) {
    const last = out[out.length - 1];
    if (last && last[0] === point[0] && last[1] === point[1]) continue;
    const before = out[out.length - 2];
    if (last && before
      && ((before[0] === last[0] && last[0] === point[0]) || (before[1] === last[1] && last[1] === point[1]))) {
      out.pop();
    }
    out.push(point);
  }
  return out;
}

function realise(plan: Plan, p: Placement): Routed {
  const { wire } = plan;
  const base = { n: wire.n, kind: wire.kind, colour: wire.colour };
  if (plan.mode === 'open') {
    const leads = plan.ends.map((e) => lead(e, p));
    return {
      ...base,
      paths: leads.map((l) => simplify([...l.points, [l.label.x, l.label.y]])),
      labels: leads.map((l) => l.label),
      ends: plan.ends.map((e) => e.pin),
    };
  }
  const [a, b] = plan.ends as [End, End];
  const la = lead(a, p);
  const lb = lead(b, p);
  const points: Pt[] = [...la.points];
  if (plan.mode === 'bracket') {
    points.push(la.reach, lb.reach);
  } else {
    const endY = (e: End): number => (e.flat ? e.box.iy + e.lane : trackY(p, e.box.col, e.gap, e.track));
    if (!a.flat) points.push([a.box.ix + a.lane, endY(a)]);
    plan.trunks.forEach((trunk, k) => {
      const x = trunkX(p, trunk.chan, trunk.index);
      const before = plan.cross[k - 1];
      const after = plan.cross[k];
      points.push([x, before ? trackY(p, before.col, before.gap, before.track) : endY(a)]);
      points.push([x, after ? trackY(p, after.col, after.gap, after.track) : endY(b)]);
    });
    if (!b.flat) points.push([b.box.ix + b.lane, endY(b)]);
  }
  points.push(...[...lb.points].reverse());
  return { ...base, paths: [simplify(points)], labels: [la.label, lb.label], ends: [a.pin, b.pin] };
}

/** Rebinds the ends of the plans to the boxes of the final placement. */
function rebind(list: Plan[], p: Placement): void {
  for (const plan of list) for (const e of plan.ends) e.box = p.boxes[e.pin.part]!;
}

export function diagramOf(scene: Scene): Diagram {
  const columns: Columns = columnsOf(scene);
  const sizes = emptySizes(columns);
  const first = place(columns, sizes);
  const list = plans(scene, first);
  lanes(list.flatMap((plan) => plan.ends));
  crossings(list, first);
  gapTracks(list, sizes);
  chanTracks(list, first, sizes);
  const final = place(columns, sizes);
  rebind(list, final);
  const files = new Map(scene.parts.map((part) => [part.id, part.file]));
  return {
    w: final.w,
    h: final.h,
    parts: columns.cols.flat().map((part) => ({ ...final.boxes[part.id]!, file: files.get(part.id)! })),
    wires: list.map((plan) => realise(plan, final)),
    undrawn: [...scene.undrawn],
    columns: columns.cols.map((c) => c.map((part) => part.id)),
    centre: columns.centre,
  };
}
