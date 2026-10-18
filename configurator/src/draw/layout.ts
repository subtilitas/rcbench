// Geometry of the wiring drawing: where every module, pad, wire, junction dot
// and number pill sits. Pure: no document, no strings of markup.

import type { Catalogue, EndRef, Lang, Setup, SignalClass, Wire } from '../../contract.ts';
import { t, tx } from '../i18n.ts';
import { endName, optionsOf, padLabel, pinPresent, pinUnconfirmed, targetName } from '../names.ts';
import type { NameCtx } from '../names.ts';

export type Pt = readonly [number, number];

export type Geometry = {
  width: number;
  top: number;
  bottom: number;
  leftX: number;
  rightX: number;
  moduleW: number;
  head: number;
  pinPitch: number;
  pinPad: number;
  gap: number;
  /** y of the first module of each column. */
  moduleTop: number;
  /** Module tops sit on a grid of this pitch, counted from `moduleTop`. */
  snap: number;
  outerLane0: number;
  outerPitch: number;
  leftLane0: number;
  rightLane0: number;
  lanePitch: number;
  coproX: number;
  coproW: number;
  coproH: number;
  canH: number;
  canL: number;
  padTop: number;
  channel0: number;
  channelPitch: number;
  pillW: number;
  pillH: number;
  pillOffset: number;
  pillPitch: number;
  outerPillPitch: number;
  stub: number;
  cross: number;
  zOut: number;
  zPill: number;
  /** Lanes that differ from the data: wire number to lane (L, R, O) or right lane (X). */
  laneFix: Readonly<Record<number, number>>;
};

/** The geometry as section 6 of the specification writes it. It collides; see GEO. */
export const SPEC_GEO: Geometry = {
  width: 970, top: 28, bottom: 8,
  leftX: 100, rightX: 788, moduleW: 170, head: 40, pinPitch: 20, pinPad: 6, gap: 12,
  moduleTop: 28, snap: 1,
  outerLane0: 66, outerPitch: 8, leftLane0: 300, rightLane0: 616, lanePitch: 7,
  coproX: 432, coproW: 150, coproH: 504, canH: 56, canL: 76, padTop: 96,
  channel0: 548, channelPitch: 9,
  pillW: 22, pillH: 16, pillOffset: 15, pillPitch: 24, outerPillPitch: 24,
  stub: 38, cross: 8, zOut: 12, zPill: 14,
  laneFix: {},
};

/**
 * The geometry the page draws. It differs from SPEC_GEO in 6 places, each of
 * which removes a collision that test/layout.test.ts finds in SPEC_GEO:
 *
 * - Module tops sit on a 20 px grid that starts at y = 34. Every module pin row
 *   then lies 10 px from the nearest pad row, and the two rows of the panel's
 *   CAN terminals meet the IOmcu's CAN rows in a straight line. Without
 *   the grid a pin row can share its y with a pad row, and two wires of
 *   different nets then run on one line.
 * - Channels under the IOmcu: y = 549 + 10 k. They lie 5 px from the
 *   nearest pin row.
 * - Outer lanes: wires 10 and 21 use lane 2, wires 14 and 22 use lane 1. The
 *   second pill on OUT + of the PD mini then covers lanes of its own pin only.
 *   The second pill of an outer edge sits 21 px from the first.
 * - Right lanes of the 5 wires that cross under the IOmcu from the left
 *   modules: 13, 14, 16, 17, 18 in place of 16 to 20. The cross mark of a stub
 *   on a right module is at x = 750 and lanes 19 and 20 pass through it. The 3
 *   minus wires of the INA228 module with XT90 plugs use lanes 1 and 0 in place
 *   of 21 and 22, which lie outside the gutter.
 * - The 2 wires of the servo supply from pad 40: wire 59 uses right lane 5 and
 *   wire 60 right lane 13. Wire 59 runs the height of the IOmcu; lane 5
 *   holds wire 24 of the INA3221 board only, and wire 59 is absent with that
 *   board. Wire 60 is absent with wires 28 and 29, which use lane 13.
 * - The pill of a component between 2 pads sits 8 px from its line. It then
 *   ends 3 px before right lane 0, which wire 58 uses.
 */
export const GEO: Geometry = {
  ...SPEC_GEO,
  moduleTop: 34, snap: 20,
  channel0: 549, channelPitch: 10,
  outerPillPitch: 21, zPill: 8,
  laneFix: {
    10: 2, 21: 2, 14: 1, 22: 1, 28: 13, 29: 13, 30: 14, 45: 16, 46: 16, 47: 17, 51: 18, 56: 1, 57: 1, 58: 0, 59: 5, 60: 13,
  },
};

export type PinBox = {
  id: string;
  label: string;
  x: number;
  y: number;
  anchor: 'start' | 'end';
  used: boolean;
  drawn: boolean;
};

export type ModuleBox = {
  id: string;
  column: 'L' | 'R';
  x: number;
  y: number;
  w: number;
  h: number;
  title: string;
  /** Sub lines under the title. More than 1 only on a module without pins. */
  sub: string[];
  /** Width in px the one sub line of a module with pins is squeezed to, or null. */
  subFit: number | null;
  dashed: boolean;
  pins: PinBox[];
};

export type PadBox = {
  n: number;
  /** Pad number as text, or '' on the Osoyoo breakout board. */
  num: string;
  label: string;
  side: 'L' | 'R';
  y: number;
  used: boolean;
};

export type WirePath = {
  n: number;
  cls: SignalClass;
  route: 'L' | 'R' | 'O' | 'X' | 'Z' | 'S';
  a: EndRef;
  b: EndRef | null;
  points: Pt[];
  title: string;
  /** Centre of the cross mark at the open end of a stub. */
  cross: Pt | null;
};

export type Pill = { n: number; x: number; y: number; at: EndRef };

export type Layout = {
  geo: Geometry;
  width: number;
  height: number;
  modules: ModuleBox[];
  copro: { x: number; y: number; w: number; h: number; title: string; sub: string; can: Array<{ label: string; y: number }> };
  pads: PadBox[];
  wires: WirePath[];
  dots: Pt[];
  pills: Pill[];
  /** Numbers of wires with an end the drawing has no place for. Empty for every resolved setup. */
  skipped: number[];
  label: string;
};

type Anchor = { y: number; inner: number; outer: number; side: 'L' | 'R'; isPad: boolean };
type End = { n: number; ref: EndRef; x0: number; y: number; laneX: number; isPad: boolean };

/** Estimated width of 11 px text in px per character. */
export const SUB_CHAR = 5.6;
const SUB_LINE = 13;

/** Breaks a sub line at spaces into lines of at most `room` px, 2 at most. */
function wrap(text: string, room: number): string[] {
  if (text.length * SUB_CHAR <= room) return text ? [text] : [];
  const words = text.split(' ');
  for (let i = words.length - 1; i > 0; i--) {
    const head = words.slice(0, i).join(' ');
    if (head.length * SUB_CHAR <= room) return [head, words.slice(i).join(' ')];
  }
  return [text];
}

function dedupe(points: Pt[]): Pt[] {
  const out: Pt[] = [];
  for (const p of points) {
    const last = out[out.length - 1];
    if (!last || last[0] !== p[0] || last[1] !== p[1]) out.push(p);
  }
  return out;
}

export function layout(setup: Setup, catalogue: Catalogue, lang: Lang, geo: Geometry = GEO): Layout {
  const options = optionsOf(setup);
  const ctx: NameCtx = { lang, catalogue, options };
  const used = new Set<EndRef>();
  for (const w of setup.wires) {
    used.add(w.a);
    if (w.b !== null) used.add(w.b);
  }

  const anchors = new Map<EndRef, Anchor>();
  const modules: ModuleBox[] = [];
  const nextY = { L: geo.moduleTop, R: geo.moduleTop };
  const bottom = { L: geo.top, R: geo.top };
  const drawn = setup.parts
    .map((r) => r.part)
    .filter((p) => p.id !== 'copro' && (p.slot.column === 'L' || p.slot.column === 'R'))
    .sort((p, q) => p.slot.order - q.slot.order);

  for (const part of drawn) {
    const column = part.slot.column as 'L' | 'R';
    const x = column === 'L' ? geo.leftX : geo.rightX;
    const y = nextY[column];
    let subText = part.slot.sub ? tx(lang, part.slot.sub) : '';
    if (part.id === 'ina228') subText = t(lang, options.ina228Board === 'xt90' ? 'inaSubXt' : 'inaSubMatek');
    const room = geo.moduleW - 16;
    const pinless = part.pins.length === 0;
    const sub = pinless ? wrap(subText, room) : subText ? [subText] : [];
    const subFit = !pinless && subText.length * SUB_CHAR > room ? room : null;
    const h = pinless
      ? geo.head + Math.max(0, sub.length - 1) * SUB_LINE
      : geo.head + part.pins.filter((pin) => pinPresent(pin, options)).length * geo.pinPitch + geo.pinPad;
    // A pin of another board variant takes no row.
    let row = 0;
    const pins = part.pins.map((pin): PinBox => {
      const ref = `${part.id}.${pin.id}`;
      const py = y + geo.head + row * geo.pinPitch + geo.pinPitch / 2;
      const present = pinPresent(pin, options);
      if (present) {
        row += 1;
        anchors.set(ref, {
          y: py,
          inner: column === 'L' ? x + geo.moduleW : x,
          outer: column === 'L' ? x : x + geo.moduleW,
          side: column,
          isPad: false,
        });
      }
      return {
        id: pin.id,
        label: tx(lang, pin.label) + (pinUnconfirmed(pin) ? ' ?' : ''),
        x: column === 'L' ? x + geo.moduleW - 8 : x + 8,
        y: py,
        anchor: column === 'L' ? 'end' : 'start',
        used: used.has(ref),
        drawn: present,
      };
    });
    modules.push({ id: part.id, column, x, y, w: geo.moduleW, h, title: tx(lang, part.short), sub, subFit, dashed: part.slot.dashed === true, pins });
    bottom[column] = y + h;
    const free = y + h + geo.gap - geo.moduleTop;
    nextY[column] = geo.moduleTop + Math.ceil(free / geo.snap) * geo.snap;
  }

  const cy = geo.top;
  const coproRight = geo.coproX + geo.coproW;
  anchors.set('can.H', { y: cy + geo.canH, inner: geo.coproX, outer: geo.coproX, side: 'L', isPad: true });
  anchors.set('can.L', { y: cy + geo.canL, inner: geo.coproX, outer: geo.coproX, side: 'L', isPad: true });
  const half = catalogue.pads.length / 2;
  const pads = catalogue.pads.map((pad): PadBox => {
    const left = pad.n <= half;
    const row = left ? pad.n - 1 : catalogue.pads.length - pad.n;
    const y = cy + geo.padTop + row * geo.pinPitch + geo.pinPitch / 2;
    const ref = `pad.${pad.n}`;
    const edge = left ? geo.coproX : coproRight;
    anchors.set(ref, { y, inner: edge, outer: edge, side: left ? 'L' : 'R', isPad: true });
    return {
      n: pad.n,
      num: options.connection === 'osoyoo' ? '' : String(pad.n),
      label: padLabel(pad, options),
      side: left ? 'L' : 'R',
      y,
      used: used.has(ref),
    };
  });

  const wires: WirePath[] = [];
  const pills: Pill[] = [];
  const skipped: number[] = [];
  const ends = new Map<string, End[]>();
  const addEnd = (w: Wire, ref: EndRef, anchor: Anchor, edge: 'i' | 'o', laneX: number): void => {
    const key = `${ref}|${edge}`;
    const list = ends.get(key) ?? [];
    list.push({ n: w.n, ref, x0: edge === 'o' ? anchor.outer : anchor.inner, y: anchor.y, laneX, isPad: anchor.isPad });
    ends.set(key, list);
  };
  let lastChannelY = 0;

  for (const w of [...setup.wires].sort((p, q) => p.n - q.n)) {
    const A = anchors.get(w.a);
    const B = w.b !== null ? anchors.get(w.b) : undefined;
    const route = w.route;
    const title = `${w.n}: ${endName(w.a, ctx)} – ${targetName(w, ctx)}`;
    const push = (points: Pt[], cross: Pt | null = null): void => {
      wires.push({ n: w.n, cls: w.cls, route: route.kind, a: w.a, b: w.b, points: dedupe(points), title, cross });
    };
    if (!A || (route.kind !== 'S' && !B)) {
      skipped.push(w.n);
      continue;
    }
    if (route.kind === 'S') {
      const dir = A.side === 'L' ? 1 : -1;
      const x1 = A.inner + dir * geo.stub;
      push([[A.inner, A.y], [x1, A.y]], [x1, A.y]);
      pills.push({ n: w.n, x: A.inner + dir * geo.pillOffset, y: A.y, at: w.a });
      continue;
    }
    const b = B as Anchor;
    if (route.kind === 'Z') {
      const dir = A.side === 'L' ? -1 : 1;
      const x1 = A.inner + dir * geo.zOut;
      push([[A.inner, A.y], [x1, A.y], [x1, b.y], [b.inner, b.y]]);
      addEnd(w, w.a, A, 'i', x1);
      addEnd(w, w.b as EndRef, b, 'i', x1);
      pills.push({ n: w.n, x: x1 + dir * geo.zPill, y: (A.y + b.y) / 2, at: w.a });
      continue;
    }
    if (route.kind === 'X') {
      const right = geo.laneFix[w.n] ?? route.right;
      const l1 = geo.leftLane0 + route.left * geo.lanePitch;
      const l2 = geo.rightLane0 + right * geo.lanePitch;
      const y = geo.channel0 + route.channel * geo.channelPitch;
      const [p, q, lp, lq] = A.side === 'L' ? [A, b, l1, l2] : [A, b, l2, l1];
      push([[p.inner, p.y], [lp, p.y], [lp, y], [lq, y], [lq, q.y], [q.inner, q.y]]);
      addEnd(w, w.a, A, 'i', lp);
      addEnd(w, w.b as EndRef, b, 'i', lq);
      lastChannelY = Math.max(lastChannelY, y);
      continue;
    }
    const lane = geo.laneFix[w.n] ?? route.lane;
    if (route.kind === 'O') {
      const lx = geo.outerLane0 - lane * geo.outerPitch;
      push([[A.outer, A.y], [lx, A.y], [lx, b.y], [b.outer, b.y]]);
      addEnd(w, w.a, A, 'o', lx);
      addEnd(w, w.b as EndRef, b, 'o', lx);
      continue;
    }
    const lx = (route.kind === 'L' ? geo.leftLane0 : geo.rightLane0) + lane * geo.lanePitch;
    push([[A.inner, A.y], [lx, A.y], [lx, b.y], [b.inner, b.y]]);
    addEnd(w, w.a, A, 'i', lx);
    addEnd(w, w.b as EndRef, b, 'i', lx);
  }

  // Several wires on one pin edge share the run from the pin. A dot marks where
  // each wire but the farthest leaves it. Module pins get one pill per wire.
  const dots: Pt[] = [];
  for (const group of ends.values()) {
    const first = group[0] as End;
    const byReach = [...group].sort((p, q) => Math.abs(p.laneX - first.x0) - Math.abs(q.laneX - first.x0));
    const farthest = byReach[byReach.length - 1] as End;
    for (const e of byReach) {
      if (e.laneX !== farthest.laneX && !dots.some((d) => d[0] === e.laneX && d[1] === e.y)) dots.push([e.laneX, e.y]);
    }
    if (first.isPad) continue;
    const dir = farthest.laneX >= first.x0 ? 1 : -1;
    const outer = first.x0 === geo.leftX && dir === -1;
    const pitch = outer ? geo.outerPillPitch : geo.pillPitch;
    [...group].sort((p, q) => p.n - q.n).forEach((e, i) => {
      pills.push({ n: e.n, x: first.x0 + dir * (geo.pillOffset + i * pitch), y: e.y, at: e.ref });
    });
  }
  pills.sort((p, q) => p.n - q.n || p.x - q.x);

  const coproBottom = cy + geo.coproH;
  const lowest = Math.max(bottom.L, bottom.R, coproBottom, lastChannelY > 0 ? lastChannelY + 6 : 0);
  return {
    geo,
    width: geo.width,
    height: lowest + geo.bottom,
    modules,
    copro: {
      x: geo.coproX, y: cy, w: geo.coproW, h: geo.coproH,
      title: t(lang, 'copro'),
      sub: t(lang, options.connection === 'osoyoo' ? 'coproSubOso' : 'coproSub'),
      can: [{ label: 'CAN H', y: cy + geo.canH }, { label: 'CAN L', y: cy + geo.canL }],
    },
    pads,
    wires,
    dots,
    pills,
    skipped,
    label: t(lang, 'drawAlt', { w: setup.wires.length }),
  };
}
