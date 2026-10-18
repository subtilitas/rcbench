// Placement of the part drawings: columns, the gaps between stacked parts and
// the channels between columns. Pure. All figures are px of the picture.
//
//   channel 0 | column 0 | channel 1 | column 1 | ... | column N-1 | channel N
//
// A column stacks its parts. Gap j of a column lies above its part j; the gap
// after the last part lies below it. Wires run in gaps and channels only.

import type { Scene, ScenePart } from './scene.ts';

/** Stroke width of a signal wire. */
export const THIN = 3;
/** Stroke width of a wire that carries load current. */
export const THICK = 7;
/** Clear distance between the edges of 2 wires that run side by side. */
export const CLEAR = 5;
/** Pitch of 2 parallel signal wires, and the grid of the part boxes. */
export const U = THIN + CLEAR;
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
 * `gaps[c][j]`: stroke widths of the tracks in gap j of column c, top to bottom.
 * `chans[i]`: stroke widths of the tracks in channel i, left to right.
 */
export type Sizes = {
  gaps: number[][][]; chans: number[][]; gapEnds: boolean[][]; chanEnds: boolean[];
  /** `bays[i]`: px of channel i kept free of tracks beside its left and its right column, for a component drawn across 2 pads. */
  bays: Array<[number, number]>;
};

/** A placed part. `x, y, w, h`: name strip and drawing. `ix, iy, iw, ih`: the drawing. */
export type Box = {
  id: string; col: number; idx: number;
  x: number; y: number; w: number; h: number;
  ix: number; iy: number; iw: number; ih: number;
};

export type Placement = {
  w: number; h: number;
  boxes: Record<string, Box>;
  cols: Array<{ x: number; w: number; gapY: number[]; gapH: number[]; gapOff: number[][] }>;
  /** `lead`: px from the left edge of the channel to the band before its tracks. */
  chans: Array<{ x: number; w: number; lead: number; off: number[] }>;
};

/** Distance between the centre lines of 2 wires that run side by side. */
export const pitch = (a: number, b: number): number => (a + b) / 2 + CLEAR;

/** Centre lines of tracks of the given stroke widths, from the start of their strip. Neighbours keep `CLEAR` edge to edge. */
export function offsets(widths: ReadonlyArray<number>): number[] {
  const out: number[] = [];
  let edge = U - THIN / 2;
  for (const w of widths) {
    out.push(edge + w / 2);
    edge += w + CLEAR;
  }
  return out;
}

/** Width of the strip that `offsets` fills: 1 pitch per signal track and 1 pitch of margin. */
export const span = (widths: ReadonlyArray<number>): number => widths.reduce((n, w) => n + w + CLEAR, U);

const up = (v: number): number => Math.ceil(v / U) * U;
const byOrder = (p: ScenePart, q: ScenePart): number => p.order - q.order || (p.id < q.id ? -1 : 1);
const tall = (list: ScenePart[]): number => list.reduce((n, p) => n + up(CAP + p.h), 0);

/**
 * Columns from left to right. The IOmcu column is `centre`. A side has 2
 * columns when it holds parts without a wire to the IOmcu column: those
 * stand in the outer column. A part without a wire joins the shorter column.
 *
 * A part with `between` whose 2 neighbours stand on its side makes a side of 3
 * columns: the first neighbour alone in the outermost, the part in the middle
 * with the other outer parts, the second neighbour in the inner column. The 3
 * parts then follow each other across the picture in the order of the path.
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
  const side = (column: 'L' | 'R'): ScenePart[][] => {
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
    const mid = all.find((p) => p.between !== undefined && p.between.every((id) => id !== p.id && all.some((q) => q.id === id))
      && p.between[0] !== p.between[1]);
    if (!mid) return [inner.sort(byOrder), outer.sort(byOrder)];
    const [first, second] = mid.between!;
    const rest = [...inner, ...outer].filter((p) => p.id !== mid.id && p.id !== first && p.id !== second);
    const is = (id: string) => (p: ScenePart): boolean => p.id === id;
    return [
      [...rest.filter((p) => inner.includes(p)), all.find(is(second))!].sort(byOrder),
      [...rest.filter((p) => outer.includes(p)), mid].sort(byOrder),
      [all.find(is(first))!],
    ];
  };
  const left = side('L').reverse().filter((c) => c.length > 0);
  const right = side('R').filter((c) => c.length > 0);
  return { cols: [...left, ...(centre.length > 0 ? [centre] : []), ...right], centre: left.length };
}

/** Sizes without a wire: the first placement, which the router reads for directions only. */
export function emptySizes(columns: Columns): Sizes {
  return {
    gaps: columns.cols.map((c) => Array.from({ length: c.length + 1 }, (): number[] => [])),
    chans: Array.from({ length: columns.cols.length + 1 }, (): number[] => []),
    gapEnds: columns.cols.map((c) => new Array<boolean>(c.length + 1).fill(false)),
    chanEnds: new Array<boolean>(columns.cols.length + 1).fill(false),
    bays: Array.from({ length: columns.cols.length + 1 }, (): [number, number] => [0, 0]),
  };
}

/** Rounds up to the grid of the picture. */
export const onGrid = (v: number): number => Math.ceil(v / U) * U;

/** Width of a channel or height of a gap: 2 bands and the strip of its tracks, or the bands alone below wire ends. */
function room(tracks: ReadonlyArray<number>, ends: boolean, unused: number): number {
  if (tracks.length > 0) return 2 * BAND + up(span(tracks));
  return ends ? 2 * BAND : unused;
}

/**
 * `shifts[c]`: px that column c starts lower than its centred place. The router
 * moves a column when a line of it meets a line of the column before it.
 */
export function place(columns: Columns, sizes: Sizes, shifts: ReadonlyArray<number> = []): Placement {
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
    const [bayLeft, bayRight] = sizes.bays[i]!;
    const w = room(sizes.chans[i]!, sizes.chanEnds[i]!, i === 0 || i === count ? 0 : CHANNEL) + bayLeft + bayRight;
    chans.push({ x, w, lead: bayLeft, off: offsets(sizes.chans[i]!) });
    x += w;
    if (i === count) break;
    const parts = columns.cols[i]!;
    const gapY: number[] = [];
    let y = EDGE + Math.floor((highest - heights[i]!) / 2 / U) * U + (shifts[i] ?? 0);
    parts.forEach((p, idx) => {
      gapY.push(y);
      y += gapH[i]![idx]!;
      const bx = x + Math.floor((widths[i]! - p.w) / 2 / U) * U;
      const h = up(CAP + p.h);
      boxes[p.id] = { id: p.id, col: i, idx, x: bx, y, w: p.w, h, ix: bx, iy: y + CAP, iw: p.w, ih: p.h };
      y += h;
    });
    gapY.push(y);
    cols.push({ x, w: widths[i]!, gapY, gapH: gapH[i]!, gapOff: sizes.gaps[i]!.map((g) => offsets(g)) });
    x += widths[i]!;
  }
  return { w: x + EDGE, h: highest + 2 * EDGE + Math.max(0, ...shifts), boxes, cols, chans };
}

/** y of track `i` of gap `gap` in column `col`. */
export function trackY(p: Placement, col: number, gap: number, i: number): number {
  return p.cols[col]!.gapY[gap]! + BAND + p.cols[col]!.gapOff[gap]![i]!;
}

/** x of track `i` of channel `chan`. */
export function trunkX(p: Placement, chan: number, i: number): number {
  return p.chans[chan]!.x + p.chans[chan]!.lead + BAND + p.chans[chan]!.off[i]!;
}
