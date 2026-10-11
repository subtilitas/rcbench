// Wire palette of the pictorial wiring view, and the anchor points a wire ends on.
// Pure: no file, no clock.

import type { Wire } from '../contract.ts';
import type { Anchors, DrawingRecord } from './generate.ts';

/**
 * Backgrounds a wire is seen on. The first 4 are the page tokens `--surface` and
 * `--bg` of `src/style.css` in the light and the dark theme. The fifth is the card
 * of the part drawings.
 */
export const BACKGROUNDS: Readonly<Record<string, string>> = {
  'light surface': '#ffffff',
  'light page': '#f4f5f7',
  'dark surface': '#171b22',
  'dark page': '#0e1116',
  'drawing card': '#efebe3',
};

/** Lowest contrast ratio of a wire against a background, WCAG 2.2 success criterion 1.4.11. */
export const MIN_RATIO = 3;

/**
 * A colour of the palette: the core, and a casing or null. A casing is an outline
 * of 1 px on each side of the core. Black and white carry one: no single colour
 * reaches 3:1 on both the light and the dark backgrounds.
 */
export type Colour = { core: string; casing: string | null };

export const PALETTE: Readonly<Record<string, Colour>> = {
  red: { core: '#d6281e', casing: null },
  black: { core: '#15191f', casing: '#c9d1dc' },
  white: { core: '#f4f5f7', casing: '#15191f' },
  orange: { core: '#c96a0a', casing: null },
  ochre: { core: '#9a8000', casing: null },
  olive: { core: '#688c12', casing: null },
  green: { core: '#1c9440', casing: null },
  teal: { core: '#0e918b', casing: null },
  cyan: { core: '#1a8ac8', casing: null },
  blue: { core: '#4a74ea', casing: null },
  violet: { core: '#9163f2', casing: null },
  magenta: { core: '#c247cf', casing: null },
  pink: { core: '#e0478a', casing: null },
  brown: { core: '#9a6a45', casing: null },
  grey: { core: '#79838f', casing: null },
};

/** Colour of every supply plus wire. No signal wire has it. */
export const PLUS = 'red';
/** Colour of every ground and supply minus wire. No signal wire has it. */
export const MINUS = 'black';

/** Nets of supply plus: battery, PD supply, servo supply, the 2 shunt outputs, 3.3 V and VBUS of the coprocessor. */
export const PLUS_NETS: ReadonlySet<string> = new Set([
  'batt_pos', 'pd_in', 'pd_out', 'servo_v', 'servo_shunt', 'esc_shunt', '3v3', 'vbus',
]);
/** Nets of ground and supply minus. */
export const MINUS_NETS: ReadonlySet<string> = new Set(['gnd', 'batt_neg', 'esc_neg']);

/** Colour of a signal by net. A net listed here overrides the colour of its class. */
export const BY_NET: Readonly<Record<string, string>> = {
  hb_in: 'pink',
  pd_tx: 'cyan', pd_rx: 'olive',
  phase_a: 'brown', phase_b: 'grey', phase_c: 'white', tap: 'orange',
};

/**
 * Colour of a signal by class. The classes `uart` and `phase` have none: each of
 * their nets is in BY_NET. The classes `power` and `gnd` have none: a supply wire
 * takes its colour from PLUS_NETS and MINUS_NETS alone.
 */
export const BY_CLASS: Readonly<Record<string, string>> = {
  canh: 'ochre', canl: 'green', hb: 'magenta', out: 'violet',
  sda: 'blue', scl: 'teal', nc: 'grey',
};

/**
 * Name of the palette colour of a wire. A wire on a plus net is red, a wire on a
 * minus net is black, a signal has the colour of its net or of its class. null for
 * a wire of kind `component`: it is the lead of a part and has no insulation.
 * Throws when the net is none of these, and for a wire of the class `power` or
 * `gnd` on a net that is neither plus nor minus.
 */
export function colourOf(wire: Pick<Wire, 'n' | 'kind' | 'net' | 'cls'>): string | null {
  if (wire.kind === 'component') return null;
  if (PLUS_NETS.has(wire.net)) return PLUS;
  if (MINUS_NETS.has(wire.net)) return MINUS;
  const name = BY_NET[wire.net] ?? BY_CLASS[wire.cls];
  if (name === undefined) throw new Error(`wire ${wire.n}: no colour for net ${wire.net}, class ${wire.cls}`);
  return name;
}

const lin = (c: number): number => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);

function rgb(hex: string): [number, number, number] {
  if (!/^#[0-9a-f]{6}$/i.test(hex)) throw new Error(`colour ${hex}: not of the form #rrggbb`);
  const at = (i: number): number => parseInt(hex.slice(i, i + 2), 16) / 255;
  return [at(1), at(3), at(5)];
}

/** Relative luminance after WCAG 2.2, 0 to 1. */
export function luminance(hex: string): number {
  const [r, g, b] = rgb(hex);
  return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

/** Contrast ratio of 2 colours after WCAG 2.2, 1 to 21. */
export function ratio(a: string, b: string): number {
  const la = luminance(a);
  const lb = luminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

/** Contrast ratio of a wire against a background: the higher of core and casing. */
export function wireRatio(colour: Colour, background: string): number {
  const core = ratio(colour.core, background);
  return colour.casing === null ? core : Math.max(core, ratio(colour.casing, background));
}

/** One anchor point of a wire end: the drawing file and the pin id in that file. */
export type EndPoint = { file: string; pin: string };

/**
 * Anchor points of a wire end in every drawing of its part. An end on `pad.N` or
 * `can.X` lies on the coprocessor. A drawing with a `catalogueMap` leads from the
 * catalogue pin to the pins of the board; a pin mapped to null has no wire point.
 */
export function endPoints(end: string, anchors: Anchors): EndPoint[] {
  const out: EndPoint[] = [];
  const dot = end.indexOf('.');
  const head = dot < 0 ? end : end.slice(0, dot);
  const pin = dot < 0 ? '' : end.slice(dot + 1);
  for (const [file, rec] of Object.entries(anchors.files)) {
    if (head === 'pad' || head === 'can') {
      if (rec.part === 'copro' && rec.pins[end]) out.push({ file, pin: end });
    } else if (rec.part === head) {
      const names = rec.catalogueMap ? rec.catalogueMap[pin] ?? [] : [pin];
      for (const n of names) if (rec.pins[n]) out.push({ file, pin: n });
    }
  }
  return out;
}

/** True when the drawings state that the end has no wire point: its part maps the pin to null. */
export function withoutWirePoint(end: string, anchors: Anchors): boolean {
  const dot = end.indexOf('.');
  if (dot < 0) return false;
  const head = end.slice(0, dot);
  const pin = end.slice(dot + 1);
  const recs = Object.values(anchors.files).filter((r) => r.part === head);
  return recs.length > 0 && recs.every((r) => r.catalogueMap !== undefined && pin in r.catalogueMap && r.catalogueMap[pin] === null);
}

/** Factor of the neighbour rule: 2 pins are neighbours within this multiple of the distance to the nearest pin. */
export const NEIGHBOUR_FACTOR = 1.6;

/**
 * Neighbours of every pin of a drawing. 2 pins are neighbours when their distance
 * is at most 1.6 times the distance from each of them to its nearest pin. The rule
 * is mutual: a pin far from a dense row is not a neighbour of the pins in that row.
 */
export function neighbours(rec: DrawingRecord): Record<string, string[]> {
  const ids = Object.keys(rec.pins);
  const dist = (a: string, b: string): number => {
    const p = rec.pins[a]!;
    const q = rec.pins[b]!;
    return Math.hypot(p.x - q.x, p.y - q.y);
  };
  const nearest: Record<string, number> = {};
  for (const a of ids) nearest[a] = Math.min(...ids.filter((b) => b !== a).map((b) => dist(a, b)));
  const out: Record<string, string[]> = {};
  for (const a of ids) {
    out[a] = ids.filter((b) => b !== a && dist(a, b) <= NEIGHBOUR_FACTOR * nearest[a]! && dist(a, b) <= NEIGHBOUR_FACTOR * nearest[b]!);
  }
  return out;
}
