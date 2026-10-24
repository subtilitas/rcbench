// The panel drawing of the IOmcu board: the parts of `copro.svg` as flat
// shapes on the real pad grid of the board, for the PICK A PIN screen of the
// panel. tools/gen_board_drawing.py turns `panel.json` into the table the
// IOmcu serves over the link.
//
// Units: 0.01mm (cmm). Origin: the top-left corner of the board outline seen
// from the component side, pad 1 at the bottom left. X runs along the board,
// Y across it.
//
// The configurator drawing is not to scale: its board is longer than the pad
// rows need. The panel drawing maps every part centre between the same pads
// as in `copro.svg` and keeps the size of each part at one scale, the scale
// across the pad rows. The parts beyond the pad rows do not fit that way and
// carry their own placement between the rows: the USB-C socket on the end of
// pad 1, the BOOT and RESET buttons beside it, the CAN screw terminal on the
// end of pad 20. Text is not drawn; the panel prints its own labels.

import type { Pad } from '../contract.ts';
import { RP, RP_PARTS } from './parts.ts';
import type { RpPart } from './parts.ts';

/** The outline and the pad grid of the board, as `k_pico_shape` in shared/outputs/out_bind.c. */
export const PANEL_SHAPE = { board: 1, width: 5100, height: 2100, pitch: 254, inset: 161, perSide: 20 } as const;

/** The pads that are not GPIO pins, as `k_pico_pads` in shared/outputs/out_bind.c. Value: decivolts of a rail. */
export const PANEL_RAILS: Readonly<Record<string, { kind: PanelPadKind; value: number }>> = {
  GND: { kind: 'ground', value: 0 },
  RUN: { kind: 'other', value: 0 },
  VREF: { kind: 'rail', value: 33 },
  '3V3': { kind: 'rail', value: 33 },
  '3V3_EN': { kind: 'other', value: 0 },
  VSYS: { kind: 'rail', value: 0 },
  VBUS: { kind: 'rail', value: 50 },
};

export type PanelPadKind = 'ground' | 'rail' | 'other' | 'gpio';
export type PanelPad = { pad: number; kind: PanelPadKind; value: number; square: boolean; x: number; y: number };
/**
 * One flat shape. rect and rrect: a, b = top-left corner, c, d = width, height;
 * param = corner radius of rrect. disc: a, b = centre, c = radius, d = 0.
 * line: a, b to c, d, param = width.
 */
export type PanelShape = { kind: 'rect' | 'rrect' | 'disc' | 'line'; param: number; colour: string; a: number; b: number; c: number; d: number };
export type PanelDrawing = {
  board: number; name: string; width: number; height: number;
  colour: string; ring: string; corner: number; ringRadius: number; holeRadius: number;
  pads: PanelPad[]; shapes: PanelShape[];
};

/** Flat colours: the middle stop of each gradient of the SVG. */
export const FLAT = {
  pcb: '#1f4f97', trace: '#2a5aa6', tin: '#c9ced4', ring: '#cfd3d8', body: '#2b2c31', dark: '#1c1d21',
  lip: '#2a2c31', slot: '#7d848c', crystal: '#33363c', crystalTop: '#4a4e56', switchBody: '#17181c', switchKnob: '#e9e6de',
  led: '#f3efe2', cap: '#a9825a', res: '#202227', terminal: '#1f7a41', pole: '#2f9a57', entry: '#0f3d1e', screwSlot: '#4a5058',
} as const;

const { width: W, height: H, pitch: PITCH, inset: INSET, perSide: PER } = PANEL_SHAPE;
/** Distance of pad 1 from the left end, as outbind_pad_xy() computes it. */
export const LEAD = (W - (PER - 1) * PITCH) / 2;
/** cmm per local unit across the board: the pad rows of both drawings line up. Every part keeps this scale. */
export const SCALE = (H - 2 * INSET) / (RP.pads.right - RP.pads.left);
/** cmm per local unit along the board, for part centres only. */
const ALONG = PITCH / RP.pads.pitch;

const round = (v: number): number => Math.round(v);
/** Panel X of a local y and panel Y of a local x: centres between the same pads as in the SVG. */
export const along = (y: number): number => LEAD + (y - RP.pads.y0) * ALONG;
export const across = (x: number): number => (H - INSET) - (x - RP.pads.left) * SCALE;

/**
 * A rigid placement: the local point (lx, ly) lands on the panel point (px, py).
 * Local x runs to panel -Y, local y to panel +X, both at SCALE. A quarter turn,
 * no mirror: the panel shows the component side as the SVG does.
 */
type Place = { lx: number; ly: number; px: number; py: number };
const placeOf = (lx: number, ly: number, px: number, py: number): Place => ({ lx, ly, px, py });
const atX = (p: Place, y: number): number => p.px + (y - p.ly) * SCALE;
const atY = (p: Place, x: number): number => p.py - (x - p.lx) * SCALE;

/** A local axis-parallel rectangle (left x, top y, width across, height along) as a panel shape. */
function rect(p: Place, x: number, y: number, w: number, h: number, colour: string, radius = 0): PanelShape {
  const x0 = atX(p, y), y0 = atY(p, x + w);
  return { kind: radius > 0 ? 'rrect' : 'rect', param: round(radius * SCALE), colour, a: round(x0), b: round(y0), c: round(h * SCALE), d: round(w * SCALE) };
}
function disc(p: Place, x: number, y: number, r: number, colour: string): PanelShape {
  return { kind: 'disc', param: 0, colour, a: round(atX(p, y)), b: round(atY(p, x)), c: round(r * SCALE), d: 0 };
}
function line(p: Place, x0: number, y0: number, x1: number, y1: number, w: number, colour: string): PanelShape {
  return { kind: 'line', param: round(w * SCALE), colour, a: round(atX(p, y0)), b: round(atY(p, x0)), c: round(atX(p, y1)), d: round(atY(p, x1)) };
}

/** The shapes of a chip: legs, then the body. Text is not drawn. */
function chip(p: Place, cx: number, cy: number, w: number, h: number, n: number, sides: 2 | 4, leg: number): PanelShape[] {
  const out: PanelShape[] = [];
  const row = (len: number, f: (q: number) => PanelShape): void => {
    const step = len / (n + 1);
    for (let i = 1; i <= n; i++) out.push(f(-len / 2 + step * i));
  };
  row(h, (q) => rect(p, cx - w / 2 - leg, cy + q - 1.3, w + 2 * leg, 2.6, FLAT.tin));
  if (sides === 4) row(w, (q) => rect(p, cx + q - 1.3, cy - h / 2 - leg, 2.6, h + 2 * leg, FLAT.tin));
  out.push(rect(p, cx - w / 2, cy - h / 2, w, h, FLAT.body, 1.5));
  return out;
}

/** The parts outside the pad rows: their own panel placement, see the head of this file. */
function endPlace(part: RpPart): Place | null {
  if (part.kind === 'usbc') {
    // The plug side of the socket on the left end of the outline.
    return placeOf(part.x + part.w / 2, part.y, 0, across(part.x + part.w / 2));
  }
  if (part.kind === 'button') {
    // Beside the USB-C socket across the board, BOOT towards pad 1.
    const usb = RP_PARTS.find((q) => q.kind === 'usbc')!;
    if (usb.kind !== 'usbc') throw new Error('no USB-C socket');
    const mid = across(usb.x + usb.w / 2);
    const gap = 30;
    const off = (usb.w / 2) * SCALE + gap + (part.s / 2) * SCALE;
    const py = part.x < usb.x + usb.w / 2 ? mid + off : mid - off;
    return placeOf(part.x, part.y, round((usb.h / 2) * SCALE), round(py));
  }
  return null;
}

function partShapes(part: RpPart): PanelShape[] {
  const p = endPlace(part) ?? placeOf(part.x, part.y, along(part.y), across(part.x));
  switch (part.kind) {
    case 'usbc': {
      const { x, y, w, h } = part;
      return [
        rect(p, x, y, w, h, FLAT.tin, 5),
        rect(p, x + w * 0.22, y + h * 0.45, w * 0.14, h * 0.3, FLAT.slot, 2),
        rect(p, x + w * 0.64, y + h * 0.45, w * 0.14, h * 0.3, FLAT.slot, 2),
        rect(p, x + 3, y, w - 6, 5, FLAT.lip),
      ];
    }
    case 'button':
      return [rect(p, part.x - part.s / 2, part.y - part.s / 2, part.s, part.s, FLAT.tin, 2), disc(p, part.x, part.y, part.s * 0.3, FLAT.dark)];
    case 'text':
      return [];
    case 'smd': {
      const mid = part.type === 'c' ? FLAT.cap : part.type === 'led' ? FLAT.led : FLAT.res;
      const k = part.k;
      const [lw, lh] = part.rot === 90 ? [9 * k, 16 * k] : [16 * k, 9 * k];
      return [rect(p, part.x - lw / 2, part.y - lh / 2, lw, lh, FLAT.tin, k), rect(p, part.x - 4.5 * k, part.y - 4.5 * k, 9 * k, 9 * k, mid)];
    }
    case 'chip':
      return chip(p, part.x, part.y, part.w, part.h, part.o.n, part.o.sides ?? 2, part.o.leg ?? 4);
    case 'tin':
      return [rect(p, part.x - part.w / 2, part.y - part.h / 2, part.w, part.h, FLAT.tin, part.rx ?? 1.5)];
    case 'crystal':
      return [rect(p, part.x, part.y, part.s, part.s, FLAT.crystal, 3), disc(p, part.x + part.s / 2, part.y + part.s / 2, 6, FLAT.crystalTop)];
    case 'switch': {
      const { x, y, w, h } = part;
      return [rect(p, x, y, w, h, FLAT.tin, 2), rect(p, x + 5, y + 4, w - 10, h - 8, FLAT.switchBody), rect(p, x + 16, y + 4, w / 2 - 6, h - 8, FLAT.switchKnob)];
    }
  }
}

/** The CAN screw terminal on the end after pad 20, wires entering from that end. Poles: H next to pad 20, GND, L next to pad 21. */
function terminalShapes(): PanelShape[] {
  const { x: cx, y: top, pitch, h } = RP.terminal;
  const p = placeOf(cx, top + h, W, across(cx));
  const xs = [cx - pitch, cx, cx + pitch];
  const out: PanelShape[] = [rect(p, xs[0]! - pitch / 2 - 2, top, 3 * pitch + 4, h, FLAT.terminal, 2)];
  xs.forEach((x, i) => {
    const dim = i === 1;
    const y = top + h / 2;
    const sr = Math.min(pitch, h) * (dim ? 0.2 : 0.3);
    out.push(rect(p, x - pitch / 2, y - h / 2, pitch, h, FLAT.pole));
    out.push(rect(p, x - 5, y + h / 2 - 7, 10, 7, FLAT.entry));
    out.push(disc(p, x, y, sr, FLAT.ring));
    out.push(line(p, x - sr * 0.7, y - sr * 0.7, x + sr * 0.7, y + sr * 0.7, dim ? 1.2 : 2, FLAT.screwSlot));
  });
  return out;
}

/**
 * The traces. Their ends follow the centre map of the parts, so a run still
 * ends at the parts it joins; a run that leaves the outline under the USB-C
 * socket is cut at the outline.
 */
function traceShapes(): PanelShape[] {
  const wd = round(RP.traceWidth * SCALE);
  const lo = Math.ceil(wd / 2), hi = W - lo;
  const clamp = (v: number): number => Math.max(lo, Math.min(hi, round(v)));
  const out: PanelShape[] = [];
  for (const pts of [...RP.traces, ...RP.branches]) {
    for (let i = 1; i < pts.length; i++) {
      const [x0, y0] = pts[i - 1]!, [x1, y1] = pts[i]!;
      out.push({ kind: 'line', param: wd, colour: FLAT.trace, a: clamp(along(y0)), b: round(across(x0)), c: clamp(along(y1)), d: round(across(x1)) });
    }
  }
  return out;
}

/** Panel position of a pad, as outbind_pad_xy() places it: pads 1 to 20 on the bottom row from the left, 21 to 40 on the top row from the right. */
export function padXY(pad: number): { x: number; y: number } {
  const bottom = pad <= PER;
  const i = bottom ? pad - 1 : 2 * PER - pad;
  return { x: LEAD + i * PITCH, y: bottom ? H - INSET : INSET };
}

/** The pads in pad order, their kinds from the catalogue pads and the rail table. */
function panelPads(pads: ReadonlyArray<Pad>): PanelPad[] {
  return [...pads].sort((a, b) => a.n - b.n).map((pd): PanelPad => {
    const at = padXY(pd.n);
    const square = pd.n === 1;
    if (pd.gpio !== null) return { pad: pd.n, kind: 'gpio', value: pd.gpio, square, x: at.x, y: at.y };
    const rail = PANEL_RAILS[pd.name];
    if (!rail) throw new Error(`pad ${pd.n} ${pd.name}: no GPIO and not in the rail table`);
    return { pad: pd.n, kind: rail.kind, value: rail.value, square, x: at.x, y: at.y };
  });
}

/** The shapes of the drawing by the part they come from. end: a part with its own placement outside the pad rows. */
export type PanelGroup = { part: string; end: boolean; shapes: PanelShape[] };

/** Every group in drawing order: traces, the parts of `copro.svg`, the CAN screw terminal. */
export function panelGroups(): PanelGroup[] {
  const groups: PanelGroup[] = [{ part: 'traces', end: false, shapes: traceShapes() }];
  for (const part of RP_PARTS) {
    const shapes = partShapes(part);
    if (shapes.length > 0) groups.push({ part: part.kind, end: endPlace(part) !== null, shapes });
  }
  groups.push({ part: 'terminal', end: true, shapes: terminalShapes() });
  return groups;
}

/** The panel drawing of the RP2350-CAN board for the catalogue pads of the IOmcu. */
export function panelDrawing(pads: ReadonlyArray<Pad>): PanelDrawing {
  return {
    board: PANEL_SHAPE.board, name: 'RP2350-CAN', width: W, height: H,
    colour: FLAT.pcb, ring: FLAT.ring, corner: round(RP.corner * SCALE),
    ringRadius: round(RP.pads.r * SCALE), holeRadius: round(RP.pads.r * 0.45 * SCALE),
    pads: panelPads(pads), shapes: panelGroups().flatMap((g) => g.shapes),
  };
}

/** The text of `panel.json`: one pad or shape a line, so a diff names the record that moved. */
export function panelText(d: PanelDrawing): string {
  const { pads, shapes, ...head } = d;
  const lines = [
    '{',
    ...Object.entries(head).map(([k, v]) => `  ${JSON.stringify(k)}: ${JSON.stringify(v)},`),
    '  "pads": [',
    ...pads.map((p, i) => `    ${JSON.stringify(p)}${i < pads.length - 1 ? ',' : ''}`),
    '  ],',
    '  "shapes": [',
    ...shapes.map((s, i) => `    ${JSON.stringify(s)}${i < shapes.length - 1 ? ',' : ''}`),
    '  ]',
    '}',
  ];
  return `${lines.join('\n')}\n`;
}
