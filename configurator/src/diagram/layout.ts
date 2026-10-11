// Placement of the part drawings: columns, the gaps between stacked parts and
// the channels between columns. Pure. All figures are px of the picture.
//
//   channel 0 | column 0 | channel 1 | column 1 | ... | column N-1 | channel N
//
// A column stacks its parts. Gap j of a column lies above its part j; the gap
// after the last part lies below it. Wires run in gaps and channels only.

import type { Scene, ScenePart } from './scene.ts';

/** Pitch of 2 parallel wires. */
export const U = 8;
/** Strip around a part that holds the wire numbers. No wire runs along it. */
export const BAND = 40;
/** Height of the name strip above a drawing. */
export const CAP = 24;
/** Margin of the picture. */
export const EDGE = 16;
/** Gap between 2 stacked parts that no wire uses. */
export const GAP = 24;
/** Width of a channel between 2 columns that no wire uses. */
export const CHANNEL = 32;

export type Columns = { cols: ScenePart[][]; centre: number };

/**
 * Tracks per gap and per channel, and whether a wire end faces them.
 * `gaps[c][j]`: tracks in gap j of column c. `chans[i]`: tracks in channel i.
 */
export type Sizes = { gaps: number[][]; chans: number[]; gapEnds: boolean[][]; chanEnds: boolean[] };

/** A placed part. `x, y, w, h`: name strip and drawing. `ix, iy, iw, ih`: the drawing. */
export type Box = {
  id: string; col: number; idx: number;
  x: number; y: number; w: number; h: number;
  ix: number; iy: number; iw: number; ih: number;
};

export type Placement = {
  w: number; h: number;
  boxes: Record<string, Box>;
  cols: Array<{ x: number; w: number; gapY: number[]; gapH: number[] }>;
  chans: Array<{ x: number; w: number }>;
};

const up = (v: number): number => Math.ceil(v / U) * U;
const byOrder = (p: ScenePart, q: ScenePart): number => p.order - q.order || (p.id < q.id ? -1 : 1);
const tall = (list: ScenePart[]): number => list.reduce((n, p) => n + up(CAP + p.h), 0);

/**
 * Columns from left to right. The coprocessor column is `centre`. A side has 2
 * columns when it holds parts without a wire to the coprocessor column: those
 * stand in the outer column. A part without a wire joins the shorter column.
 */
export function columnsOf(scene: Scene): Columns {
  const centre = scene.parts.filter((p) => p.column === 'C').sort(byOrder);
  const central = new Set(centre.map((p) => p.id));
  const wired = new Set<string>();
  const near = new Set<string>();
  for (const wire of scene.wires) {
    const ids = [...wire.a, ...(wire.b ?? [])].map((pin) => pin.part);
    for (const id of ids) wired.add(id);
    if (ids.some((id) => central.has(id))) for (const id of ids) near.add(id);
  }
  const side = (column: 'L' | 'R'): [ScenePart[], ScenePart[]] => {
    const all = scene.parts.filter((p) => p.column === column).sort(byOrder);
    let inner = all.filter((p) => near.has(p.id));
    let outer = all.filter((p) => wired.has(p.id) && !near.has(p.id));
    if (inner.length === 0) {
      inner = outer;
      outer = [];
    }
    for (const p of all.filter((q) => !wired.has(q.id))) {
      (outer.length > 0 && tall(outer) <= tall(inner) ? outer : inner).push(p);
    }
    return [inner.sort(byOrder), outer.sort(byOrder)];
  };
  const [leftInner, leftOuter] = side('L');
  const [rightInner, rightOuter] = side('R');
  const left = [leftOuter, leftInner].filter((c) => c.length > 0);
  const right = [rightInner, rightOuter].filter((c) => c.length > 0);
  return { cols: [...left, ...(centre.length > 0 ? [centre] : []), ...right], centre: left.length };
}

/** Sizes without a wire: the first placement, which the router reads for directions only. */
export function emptySizes(columns: Columns): Sizes {
  return {
    gaps: columns.cols.map((c) => new Array<number>(c.length + 1).fill(0)),
    chans: new Array<number>(columns.cols.length + 1).fill(0),
    gapEnds: columns.cols.map((c) => new Array<boolean>(c.length + 1).fill(false)),
    chanEnds: new Array<boolean>(columns.cols.length + 1).fill(false),
  };
}

/** Width of a channel or height of a gap: 2 bands and 1 pitch per track, or the bands alone below wire ends. */
function room(tracks: number, ends: boolean, unused: number): number {
  if (tracks > 0) return 2 * BAND + U * (tracks + 1);
  return ends ? 2 * BAND : unused;
}

export function place(columns: Columns, sizes: Sizes): Placement {
  const count = columns.cols.length;
  const widths = columns.cols.map((c) => up(Math.max(...c.map((p) => p.w))));
  const gapH = columns.cols.map((c, ci) => sizes.gaps[ci]!.map((tracks, j) =>
    room(tracks, sizes.gapEnds[ci]![j]!, j === 0 || j === c.length ? 0 : GAP)));
  const heights = columns.cols.map((c, ci) => tall(c) + gapH[ci]!.reduce((n, g) => n + g, 0));
  const highest = Math.max(0, ...heights);

  const boxes: Record<string, Box> = {};
  const cols: Placement['cols'] = [];
  const chans: Placement['chans'] = [];
  let x = EDGE;
  for (let i = 0; i <= count; i++) {
    const w = room(sizes.chans[i]!, sizes.chanEnds[i]!, i === 0 || i === count ? 0 : CHANNEL);
    chans.push({ x, w });
    x += w;
    if (i === count) break;
    const parts = columns.cols[i]!;
    const gapY: number[] = [];
    let y = EDGE + Math.floor((highest - heights[i]!) / 2 / U) * U;
    parts.forEach((p, idx) => {
      gapY.push(y);
      y += gapH[i]![idx]!;
      const bx = x + Math.floor((widths[i]! - p.w) / 2 / U) * U;
      const h = up(CAP + p.h);
      boxes[p.id] = { id: p.id, col: i, idx, x: bx, y, w: p.w, h, ix: bx, iy: y + CAP, iw: p.w, ih: p.h };
      y += h;
    });
    gapY.push(y);
    cols.push({ x, w: widths[i]!, gapY, gapH: gapH[i]! });
    x += widths[i]!;
  }
  return { w: x + EDGE, h: highest + 2 * EDGE, boxes, cols, chans };
}

/** y of track `i` of gap `gap` in column `col`. Columns alternate by half a pitch, so 2 columns never share a line. */
export function trackY(p: Placement, col: number, gap: number, i: number): number {
  return p.cols[col]!.gapY[gap]! + BAND + U * (i + 1) + (col % 2) * (U / 2);
}

/** x of track `i` of channel `chan`. */
export function trunkX(p: Placement, chan: number, i: number): number {
  return p.chans[chan]!.x + BAND + U * (i + 1);
}
