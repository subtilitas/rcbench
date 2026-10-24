// The router of the pictorial wiring view. Pure and deterministic.
//
// Every wire is a chain of horizontal and vertical segments. A wire leaves its
// pad at a right angle to the edge the drawing names, on a line of its own (its
// lane). From an east or west edge it runs straight into the channel beside
// the column. From a north or south edge it runs into the gap above or below
// the part, turns on a track of its own and runs to a channel. In a channel it
// runs on a vertical track of its own. To reach a channel further away it
// crosses each column between through a gap, on a track of its own.
//
// What this gives by construction:
// - a wire crosses no part other than the 2 it ends on,
// - tracks and lanes keep `CLEAR` px between the edges of 2 wires, whatever
//   their stroke widths, so a bundle is a set of parallel lines,
// - 2 wires on 1 pad end on it side by side and leave it side by side.
//
// 2 horizontal lines that enter 1 channel from its 2 sides at 1 height do not
// meet when the line from the left turns before the line from the right. The
// order of the tracks of a channel follows from that. When no order separates
// a pair, the column right of the channel moves down by a few px. A pair that
// is left after `SHIFTS` is listed in `clashes`.

import type { Lang } from '../../contract.ts';
import type { Side } from '../../drawings/lib.ts';
import { BAND, THICK, THIN, columnsOf, emptySizes, offsets, onGrid, pitch, place, trackY, trunkX } from './layout.ts';
import type { Box, Columns, Placement, Sizes } from './layout.ts';
import type { Scene, ScenePin, SceneWire } from './scene.ts';

export type Pt = [number, number];

/** A wire number on the picture: its centre. */
export type Label = { x: number; y: number };

/**
 * The body of a component on its wire. `x, y`: its centre. `upright`: its axis
 * is vertical. `len`: its length along the axis. `band`: -1 when the band lies
 * towards the start of the axis (up or left), 1 otherwise; the band lies on the
 * side of end `a` of the wire. `text`: where its lines stand; `x` is the start,
 * the end or the middle of each line, `y` the middle of the block.
 */
export type Body = {
  x: number; y: number; upright: boolean; len: number; band: 1 | -1;
  text: { x: number; y: number; anchor: 'start' | 'end' | 'middle' };
};

/**
 * A routed wire. `paths`: 1 polyline from pad to pad, or 1 short lead per wire
 * point for a wire that ends open. `labels`: the places of the wire number,
 * 2 for a wire, 1 per lead for an open end. `ends`: the pads the paths start
 * and end on, in the order of the path points. A component has a `body` and
 * the lines of its `text`; drawn across 2 pads of 1 edge it has 1 number, beside the body.
 */
export type Routed = {
  n: number;
  kind: SceneWire['kind'];
  colour: string | null;
  /** True for a wire that carries load current. It has the stroke width `THICK`. */
  thick: boolean;
  /** True for a USB-C cable: a sheath as wide as the wire around a core of `THIN` px. */
  cable?: boolean;
  paths: Pt[][];
  labels: Label[];
  ends: ScenePin[];
  body?: Body;
  text?: Record<Lang, string[]>;
};

export type Diagram = {
  w: number;
  h: number;
  parts: Array<Box & { file: string }>;
  wires: Routed[];
  undrawn: number[];
  /** Part ids per column, left to right. */
  columns: string[][];
  /** Index of the IOmcu column. */
  centre: number;
  /** Pairs of wire numbers whose lines overlap in a channel because no order of its tracks separates them. */
  clashes: Array<[number, number]>;
};

/** Size of a wire number. */
export const LABEL = { w: 16, h: 11 };
/** Steps in px a column moves down to keep its lines off the lines of the column before it. */
export const SHIFTS: ReadonlyArray<number> = [0, 4, 8, 12, 16, 20, 24, 28, 32];
/** Width and greatest length of the body of a component, the width of its band, the least length of each lead beside the body. */
export const BODY = { w: 12, len: 20, band: 4, lead: 3 };
/** Estimated width of 1 character of a component line in px, at the 12px of the export form. The page draws 10px. */
export const TEXT_CHAR = 6.6;
/** From the axis of a body to the centre of its number, and to its lines. */
const BODY_NUMBER = BODY.w / 2 + 3 + LABEL.w / 2;
const BODY_TEXT = BODY_NUMBER + LABEL.w / 2 + 4;

/** Width of the strip of a channel that holds a component across 2 pads of the edge beside it: leads, body, number, lines. */
export function bayOf(text: Record<Lang, string[]> | undefined): number {
  const chars = Math.max(0, ...Object.values(text ?? {}).flat().map((line) => line.length));
  return onGrid(BAND - 2 + BODY_TEXT + chars * TEXT_CHAR + 6);
}

/** Distance between 2 bends that lead from pads of 1 edge to lanes away from them. */
const JOG = 12;

type End = {
  n: number;
  which: number;
  pin: ScenePin;
  box: Box;
  /** True for an east or west edge. */
  flat: boolean;
  /** Stroke width of the wire. */
  w: number;
  /** Lane in px of the drawing: y for a flat end, x otherwise. */
  lane: number;
  /** Place of the wire end on its pad, along the edge, from the wire point. Not 0 when the pad holds several wires. */
  off: number;
  /** Count of the bends between the pad and the bend of this end. Used when the lane does not start on the pad. */
  depth: number;
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
    n: wire.n, which, pin, box, flat, w: wire.thick ? THICK : THIN, lane: 0, off: 0, depth: 0, level: 0,
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
    } else {
      // The 2 ends face each other: on a pad with further wires each takes the place nearest to the other end.
      const along = (e: End): number => (e.flat ? e.pin.y : e.pin.x);
      const [low, high] = along(a) <= along(b) ? [a, b] : [b, a];
      low.hint = 1e9;
      high.hint = -1e9;
    }
    out.push({ wire, mode: bracket ? 'bracket' : 'wire', ends: [a, b], cross: [], trunks: [] });
  }
  return out;
}

/**
 * Lanes and number steps of the ends on each edge of each part. The wires of
 * 1 pad lie side by side, centred on the wire point. A lane starts on its pad
 * unless the pads of the edge are closer than their wires are wide: then it
 * moves along the edge and the lead bends once.
 */
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
    const clear = ends[0]!.flat ? LABEL.h + 1 : LABEL.w + 1;
    for (let i = 0; i < ends.length; i++) {
      const e = ends[i]!;
      const group = ends.filter((x) => x.pin.pin === e.pin.pin && along(x) === along(e));
      const lines = offsets(group.map((x) => x.w));
      e.off = lines[group.indexOf(e)]! - (lines[0]! + lines[lines.length - 1]!) / 2;
      const before = ends[i - 1];
      e.lane = Math.max(along(e) + e.off, before ? before.lane + pitch(before.w, e.w) : -Infinity);
      const taken = ends.slice(0, i).filter((x) => e.lane - x.lane < clear).map((x) => x.level);
      e.level = [0, 1, 2].find((level) => !taken.includes(level)) ?? 0;
    }
    // A lead that bends further along the edge bends nearer to its pad, so that no lead meets the lane of another.
    const bent = ends.filter((e) => e.lane !== along(e) + e.off);
    bent.forEach((e, i) => { e.depth = bent.length - 1 - i; });
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
  type Entry = { role: number; left: boolean; x: number; n: number; k: number; w: number; set: (track: number) => void };
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
        role: e.pin.side === 'S' ? 0 : 2, left: e.chan === e.box.col, x: e.box.ix + e.lane, n: e.n, k: e.which, w: e.w,
        set: (track) => { e.track = track; },
      });
    }
    plan.cross.forEach((cross, k) => add(cross.col, cross.gap, {
      role: 1, left: false, x: 0, n: plan.wire.n, k, w: plan.wire.thick ? THICK : THIN, set: (track) => { cross.track = track; },
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
    sizes.gaps[col]![gap] = entries.map((entry) => entry.w);
  }
}

/** The channels each wire runs through, and the stroke widths each channel has to hold. */
function trunks(list: Plan[], sizes: Sizes): void {
  for (const plan of list) {
    for (const e of plan.ends) {
      if (!e.flat || plan.mode === 'wire') continue;
      sizes.chanEnds[position(e)] = true;
      if (plan.mode === 'bracket') {
        const bay = sizes.bays[position(e)]!;
        const side = e.pin.side === 'E' ? 0 : 1;
        bay[side] = Math.max(bay[side], bayOf(plan.wire.text));
      }
    }
    if (plan.mode !== 'wire') continue;
    const [a, b] = plan.ends as [End, End];
    const step = Math.sign(b.chan - a.chan);
    const hops = Math.abs(b.chan - a.chan);
    for (let k = 0; k <= hops; k++) {
      const chan = a.chan + k * step;
      plan.trunks.push({ chan, index: -1 });
      sizes.chans[chan]!.push(plan.wire.thick ? THICK : THIN);
    }
  }
}

/**
 * Order of the tracks of each channel, on the final placement. The first
 * order nests wires that enter and leave on 1 side and keeps 2 wires that run
 * the same way from crossing. Then a wire whose line from the left lies at the
 * height of a line of another wire from the right takes a track left of it.
 */
function chanTracks(list: Plan[], p: Placement, only = -1): Array<[number, number]> {
  type Term = { left: boolean; y: number };
  type Entry = { group: number; key: number; n: number; k: number; w: number; terms: [Term, Term]; trunk: Trunk };
  const chans = new Map<number, Entry[]>();
  for (const plan of list) {
    if (plan.mode !== 'wire') continue;
    const [a, b] = plan.ends as [End, End];
    const endTerm = (e: End): Term => ({
      left: e.flat ? e.pin.side === 'E' : e.chan === e.box.col + 1,
      y: e.flat ? e.box.iy + e.lane : trackY(p, e.box.col, e.gap, e.track),
    });
    const crossTerm = (c: Cross, chan: number): Term => ({ left: c.col === chan - 1, y: trackY(p, c.col, c.gap, c.track) });
    plan.trunks.forEach((trunk, k) => {
      if (only >= 0 && trunk.chan !== only) return;
      const t1 = k === 0 ? endTerm(a) : crossTerm(plan.cross[k - 1]!, trunk.chan);
      const t2 = k === plan.trunks.length - 1 ? endTerm(b) : crossTerm(plan.cross[k]!, trunk.chan);
      const reach = Math.abs(t1.y - t2.y);
      let group: number;
      let key: number;
      if (t1.left && t2.left) {
        group = 0;
        key = reach;
      } else if (!t1.left && !t2.left) {
        group = 3;
        key = -reach;
      } else {
        const left = t1.left ? t1 : t2;
        const right = t1.left ? t2 : t1;
        group = left.y > right.y ? 1 : 2;
        key = left.y > right.y ? left.y : -left.y;
      }
      const entry: Entry = { group, key, n: plan.wire.n, k, w: plan.wire.thick ? THICK : THIN, terms: [t1, t2], trunk };
      chans.set(trunk.chan, [...(chans.get(trunk.chan) ?? []), entry]);
    });
  }
  const clashes: Array<[number, number]> = [];
  for (const [chan, entries] of chans) {
    entries.sort((p1, p2) => p1.group - p2.group || p1.key - p2.key || p1.n - p2.n || p1.k - p2.k);
    // first[j]: the entries that take a track left of entry j.
    const first = entries.map(() => new Set<number>());
    entries.forEach((left, i) => entries.forEach((right, j) => {
      if (i === j) return;
      const near = left.terms.some((s) => s.left
        && right.terms.some((t) => !t.left && Math.abs(s.y - t.y) < pitch(left.w, right.w)));
      if (near) first[j]!.add(i);
    }));
    const order: number[] = [];
    const done = new Set<number>();
    while (order.length < entries.length) {
      const open = entries.map((_, i) => i).filter((i) => !done.has(i));
      const free = open.find((i) => [...first[i]!].every((j) => done.has(j)));
      const pick = free ?? open[0]!;
      if (free === undefined) {
        for (const j of first[pick]!) {
          if (!done.has(j)) clashes.push([Math.min(entries[pick]!.n, entries[j]!.n), Math.max(entries[pick]!.n, entries[j]!.n)]);
        }
      }
      done.add(pick);
      order.push(pick);
    }
    order.forEach((i, at) => { entries[i]!.trunk.index = at; });
    p.chans[chan]!.off = offsets(order.map((i) => entries[i]!.w));
  }
  return clashes.sort((p1, p2) => p1[0] - p2[0] || p1[1] - p2[1]);
}

/** Points from the pad to the start of its lane, and the place of the wire number. */
function lead(e: End, p: Placement): { points: Pt[]; label: Label; reach: Pt } {
  const box = p.boxes[e.pin.part]!;
  const dir = sign(e.pin.side);
  const bend = JOG * (1 + e.depth);
  if (e.flat) {
    const ax = box.ix + e.pin.x;
    const from = box.iy + e.pin.y + e.off;
    const y = box.iy + e.lane;
    const edge = dir > 0 ? box.x + box.w : box.x;
    const points: Pt[] = y === from ? [[ax, y]] : [[ax, from], [ax + dir * bend, from], [ax + dir * bend, y]];
    const label = { x: edge + dir * (LABEL.w / 2 + 3 + e.level * LABEL.w), y };
    return { points, label, reach: [edge + dir * (BAND - 2), y] };
  }
  const ay = box.iy + e.pin.y;
  const from = box.ix + e.pin.x + e.off;
  const x = box.ix + e.lane;
  const edge = dir > 0 ? box.y + box.h : box.y;
  const points: Pt[] = x === from ? [[x, ay]] : [[from, ay], [from, ay + dir * bend], [x, ay + dir * bend]];
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

/** Length of a body on a run of `span` px: `BODY.len`, or what the run leaves beside 2 leads, at least the width of the band and 2px on each side. */
const bodyLen = (span: number): number => Math.max(BODY.band + 4, Math.min(BODY.len, span - 2 * BODY.lead));

function realise(plan: Plan, p: Placement): Routed {
  const { wire } = plan;
  const base = { n: wire.n, kind: wire.kind, colour: wire.colour, thick: wire.thick, ...(wire.cable ? { cable: true } : {}) };
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
    points.push(la.reach, lb.reach, ...[...lb.points].reverse());
    const dir = sign(a.pin.side);
    const [from, to] = [la.reach, lb.reach];
    const mid: Pt = [(from[0] + to[0]) / 2, (from[1] + to[1]) / 2];
    const span = Math.abs(a.flat ? to[1] - from[1] : to[0] - from[0]);
    const band = (a.flat ? from[1] <= to[1] : from[0] <= to[0]) ? -1 : 1;
    const label: Label = a.flat ? { x: mid[0] + dir * BODY_NUMBER, y: mid[1] } : { x: mid[0], y: mid[1] + dir * (BODY.w / 2 + 3 + LABEL.h / 2) };
    const text: Body['text'] = a.flat
      ? { x: mid[0] + dir * BODY_TEXT, y: mid[1], anchor: dir > 0 ? 'start' : 'end' }
      : { x: mid[0], y: mid[1] + dir * (BODY.w / 2 + 3 + LABEL.h + 16), anchor: 'middle' };
    const body: Body = { x: mid[0], y: mid[1], upright: a.flat, len: bodyLen(span), band, text };
    return { ...base, paths: [simplify(points)], labels: [label], ends: [a.pin, b.pin], body, ...(wire.text ? { text: wire.text } : {}) };
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
  const path = simplify(points);
  if (wire.kind !== 'component') return { ...base, paths: [path], labels: [la.label, lb.label], ends: [a.pin, b.pin] };
  // A component between 2 parts: its body lies on the longest run of the wire.
  let at = 1;
  const run = (i: number): number => Math.abs(path[i]![0] - path[i - 1]![0]) + Math.abs(path[i]![1] - path[i - 1]![1]);
  for (let i = 2; i < path.length; i++) if (run(i) > run(at)) at = i;
  const [from, to] = [path[at - 1]!, path[at]!];
  const upright = from[0] === to[0];
  const mid: Pt = [(from[0] + to[0]) / 2, (from[1] + to[1]) / 2];
  const body: Body = {
    x: mid[0], y: mid[1], upright, len: bodyLen(run(at)), band: (upright ? from[1] <= to[1] : from[0] <= to[0]) ? -1 : 1,
    text: upright ? { x: mid[0] + BODY.w / 2 + 4, y: mid[1], anchor: 'start' } : { x: mid[0], y: mid[1] + BODY.w / 2 + 18, anchor: 'middle' },
  };
  return { ...base, paths: [path], labels: [la.label, lb.label], ends: [a.pin, b.pin], body, ...(wire.text ? { text: wire.text } : {}) };
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
  trunks(list, sizes);
  // Channel c lies between the columns c - 1 and c. Each column takes the first shift that leaves its channel without a clash.
  const shifts = columns.cols.map(() => 0);
  for (let c = 1; c < columns.cols.length; c++) {
    for (const shift of SHIFTS) {
      shifts[c] = shift;
      const trial = place(columns, sizes, shifts);
      rebind(list, trial);
      if (chanTracks(list, trial, c).length === 0) break;
      shifts[c] = 0;
    }
  }
  const final = place(columns, sizes, shifts);
  rebind(list, final);
  const clashes = chanTracks(list, final);
  const files = new Map(scene.parts.map((part) => [part.id, part.file]));
  return {
    w: final.w,
    h: final.h,
    parts: columns.cols.flat().map((part) => ({ ...final.boxes[part.id]!, file: files.get(part.id)! })),
    wires: list.map((plan) => realise(plan, final)),
    undrawn: [...scene.undrawn],
    columns: columns.cols.map((c) => c.map((part) => part.id)),
    centre: columns.centre,
    clashes,
  };
}
