// Drawing helpers of the part drawings. No dependency.
// Every id in an output file starts with the prefix of its drawing.

import type { Lang, Text } from '../contract.ts';

export const FONT = "system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif";
export const SILK = '#f2f2f2';
export const INK = '#24262b';
/** Fill of the card behind every drawing. */
export const CARD = '#efebe3';

export type Side = 'N' | 'E' | 'S' | 'W';
export type PcbColour = 'blue' | 'black' | 'green' | 'white' | 'teal' | 'indigo';
export type TextOpt = {
  sz?: number; a?: 'start' | 'middle' | 'end'; f?: string; w?: number; rot?: number; id?: string; ls?: string;
};
/** The point of one pin where a wire attaches, in px of the viewBox, and the side the wire leaves on. */
export type Anchor = { x: number; y: number; side: Side; label: string; labelId: string };
export type DwgInit = {
  file: string; pfx: string; w: number; h: number; title: string; desc: string; lang: Lang;
  /** Pin id to label text. A pin outside this record is not drawn. */
  labels: Record<string, Text>;
};

export const esc = (s: string | number): string =>
  String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const r1 = (n: number): number => Math.round(n * 10) / 10;

const PCB: Record<PcbColour, [string, string, string]> = {
  blue: ['#3069bb', '#1f4f97', '#173c76'],
  black: ['#24262b', '#17181c', '#101114'],
  green: ['#35a35e', '#1f7d43', '#166131'],
  white: ['#fdfdfb', '#f1f0ec', '#dedcd5'],
  teal: ['#2f8ab8', '#1c6c97', '#145576'],
  indigo: ['#4166c8', '#2b4ba6', '#1f3885'],
};

/** Width of a character of a label in em. An estimate for system-ui at weight 600; the font of the viewer is not measured. */
export const LABEL_EM = 0.62;

/** Estimated width of a label in px. */
export function labelWidth(label: string, size: number): number {
  return label.length * size * LABEL_EM;
}

export class Dwg {
  readonly file: string;
  readonly pfx: string;
  readonly w: number;
  readonly h: number;
  readonly title: string;
  readonly desc: string;
  readonly lang: Lang;
  readonly labels: Record<string, Text>;
  readonly out: string[] = [];
  readonly anchors: Record<string, Anchor> = {};
  readonly grads = new Map<string, string>();
  readonly seen = new Set<string>();
  /** Bare component leads as segments, and the eyelets at lead ends, in px of the viewBox. */
  readonly leads: Array<[number, number, number, number]> = [];
  readonly eyes: Array<[number, number]> = [];

  constructor(init: DwgInit) {
    this.file = init.file;
    this.pfx = init.pfx;
    this.w = init.w;
    this.h = init.h;
    this.title = init.title;
    this.desc = init.desc;
    this.lang = init.lang;
    this.labels = init.labels;
  }

  /** True when the catalogue names this pin for the drawing. */
  has(pinId: string): boolean { return pinId in this.labels; }

  u(n: string): string { return `url(#${this.pfx}-${n})`; }
  add(...s: string[]): this { this.out.push(...s); return this; }

  grad(name: string, stops: string[], dir: 'd' | 'v' | 'h' = 'd'): string {
    const [x2, y2] = dir === 'v' ? [0, 1] : dir === 'h' ? [1, 0] : [1, 1];
    const st = stops.map((c, i) => `<stop offset="${r1(i / (stops.length - 1))}" stop-color="${c}"/>`).join('');
    this.grads.set(name, `<linearGradient id="${this.pfx}-${name}" x1="0" y1="0" x2="${x2}" y2="${y2}">${st}</linearGradient>`);
    return this.u(name);
  }

  pcb(colour: PcbColour): string { return this.grad(`pcb-${colour}`, PCB[colour]); }

  // ---- text ----
  text(x: number, y: number, s: string, o: TextOpt = {}): string {
    const { sz = 13, a = 'start', f = SILK, w = 600, rot, id, ls } = o;
    const tr = rot ? ` transform="rotate(${rot} ${r1(x)} ${r1(y)})"` : '';
    return `<text${id ? ` id="${id}"` : ''} x="${r1(x)}" y="${r1(y)}" font-size="${sz}" font-weight="${w}" fill="${f}" text-anchor="${a}"${ls ? ` letter-spacing="${ls}"` : ''}${tr}>${esc(s)}</text>`;
  }

  // A catalogue pin: its label text and the point where a wire attaches.
  pin(pinId: string, ax: number, ay: number, side: Side, lx: number, ly: number, o: TextOpt = {}): string {
    if (this.seen.has(pinId)) throw new Error(`${this.file}: pin ${pinId} drawn twice`);
    const lab = this.labels[pinId];
    if (!lab) throw new Error(`${this.file}: pin ${pinId} is not in the catalogue`);
    this.seen.add(pinId);
    const safe = pinId.replace(/\+/g, 'P').replace(/-/g, 'M').replace(/[^A-Za-z0-9]/g, '-');
    const label = lab[this.lang];
    this.anchors[pinId] = { x: r1(ax), y: r1(ay), side, label, labelId: `${this.pfx}-lbl-${safe}` };
    if (o.rot === undefined) {
      const width = labelWidth(label, o.sz ?? 13);
      const left = o.a === 'end' ? lx - width : o.a === 'middle' ? lx - width / 2 : lx;
      if (left < 0 || left + width > this.w) {
        throw new Error(`${this.file}: label "${label}" of pin ${pinId} does not fit: ${r1(left)} to ${r1(left + width)} px of ${this.w} px`);
      }
    }
    return this.text(lx, ly, label, { ...o, id: `${this.pfx}-lbl-${safe}` });
  }

  // ---- boards and holes ----
  board(x: number, y: number, w: number, h: number, colour: PcbColour, rx = 5): string {
    return `<g filter="${this.u('sh')}"><rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${rx}" fill="${this.pcb(colour)}"/></g>`
      + `<rect x="${x + 0.5}" y="${y + 0.5}" width="${w - 1}" height="${h - 1}" rx="${rx}" fill="none" stroke="#fff" stroke-opacity=".10"/>`;
  }

  hole(x: number, y: number, r = 10, under = CARD): string {
    return `<circle cx="${x}" cy="${y}" r="${r1(r * 1.4)}" fill="${this.u('ring')}"/><circle cx="${x}" cy="${y}" r="${r}" fill="${under}" stroke="#6d737b"/>`;
  }

  // Through-hole pad. dim: a pad the bench does not use, drawn smaller.
  th(x: number, y: number, r = 8, o: { sq?: boolean; under?: string; dim?: boolean } = {}): string {
    const { sq = false, under = CARD, dim = false } = o;
    const rr = dim ? r * 0.62 : r;
    const ring = sq
      ? `<rect x="${r1(x - rr)}" y="${r1(y - rr)}" width="${r1(2 * rr)}" height="${r1(2 * rr)}" rx="1" fill="${this.u('ring')}"/>`
      : `<circle cx="${x}" cy="${y}" r="${r1(rr)}" fill="${this.u('ring')}"/>`;
    return `${ring}<circle cx="${x}" cy="${y}" r="${r1(rr * 0.45)}" fill="${under}" stroke="#6d737b" stroke-width=".8"/>`;
  }

  // Surface pad (tin rectangle).
  tin(x: number, y: number, w: number, h: number, rx = 1.5): string {
    return `<rect x="${r1(x - w / 2)}" y="${r1(y - h / 2)}" width="${w}" height="${h}" rx="${rx}" fill="${this.u('tin')}"/>`;
  }

  // ---- small parts ----
  smd(x: number, y: number, kind: 'r' | 'c' | 'led' = 'r', rot = 0, k = 1): string {
    const mid = kind === 'c' ? '#a9825a' : kind === 'led' ? '#f3efe2' : '#202227';
    return `<g transform="translate(${x},${y}) rotate(${rot}) scale(${k})" filter="${this.u('s2')}"><rect x="-8" y="-4.5" width="16" height="9" rx="1" fill="${this.u('tin')}"/><rect x="-4.5" y="-4.5" width="9" height="9" fill="${mid}"/></g>`;
  }

  // Leaded chip. sides: 2 (left and right) or 4.
  chip(x: number, y: number, w: number, h: number, o: { n?: number; sides?: 2 | 4; text?: string[]; rot?: number; tsz?: number; leg?: number } = {}): string {
    const { n = 4, sides = 2, text = [], rot = 0, tsz = 6, leg = 4 } = o;
    let legs = '';
    const row = (len: number, cnt: number, f: (p: number) => string): void => {
      const step = len / (cnt + 1);
      for (let i = 1; i <= cnt; i++) legs += f(-len / 2 + step * i);
    };
    row(h, n, (p) => `<rect x="${r1(-w / 2 - leg)}" y="${r1(p - 1.3)}" width="${r1(w + 2 * leg)}" height="2.6" fill="${this.u('tin')}"/>`);
    if (sides === 4) row(w, n, (p) => `<rect x="${r1(p - 1.3)}" y="${r1(-h / 2 - leg)}" width="2.6" height="${r1(h + 2 * leg)}" fill="${this.u('tin')}"/>`);
    const lines = text.map((t, i) => `<text y="${r1((i - (text.length - 1) / 2) * (tsz + 1) + tsz * 0.35)}">${esc(t)}</text>`).join('');
    return `<g transform="translate(${x},${y}) rotate(${rot})" filter="${this.u('s2')}">${legs}<rect x="${-w / 2}" y="${-h / 2}" width="${w}" height="${h}" rx="1.5" fill="${this.u('body')}"/><rect x="${-w / 2}" y="${-h / 2}" width="${w}" height="${r1(h * 0.16)}" rx="1.5" fill="#fff" opacity=".07"/><circle cx="${r1(-w / 2 + 4)}" cy="${r1(-h / 2 + 4)}" r="1.4" fill="#6a6e76"/><g fill="#9aa0a8" font-size="${tsz}" text-anchor="middle">${lines}</g></g>`;
  }

  // Tact button.
  button(x: number, y: number, s = 16): string {
    return `<g filter="${this.u('s2')}"><rect x="${r1(x - s / 2)}" y="${r1(y - s / 2)}" width="${s}" height="${s}" rx="2" fill="${this.u('tin')}"/><circle cx="${x}" cy="${y}" r="${r1(s * 0.3)}" fill="#1c1d21"/><circle cx="${r1(x - s * 0.08)}" cy="${r1(y - s * 0.08)}" r="${r1(s * 0.12)}" fill="#fff" opacity=".12"/></g>`;
  }

  // USB-C socket seen from above. open: the edge the plug enters.
  usbc(x: number, y: number, w: number, h: number, open: Side = 'N'): string {
    const lip = open === 'N' ? `<rect x="${x + 3}" y="${y}" width="${w - 6}" height="5" fill="#2a2c31"/>`
      : open === 'S' ? `<rect x="${x + 3}" y="${y + h - 5}" width="${w - 6}" height="5" fill="#2a2c31"/>`
      : open === 'W' ? `<rect x="${x}" y="${y + 3}" width="5" height="${h - 6}" fill="#2a2c31"/>`
      : `<rect x="${x + w - 5}" y="${y + 3}" width="5" height="${h - 6}" fill="#2a2c31"/>`;
    const vert = open === 'N' || open === 'S';
    const slots = vert
      ? `<rect x="${r1(x + w * 0.22)}" y="${r1(y + h * 0.45)}" width="${r1(w * 0.14)}" height="${r1(h * 0.3)}" rx="2" fill="#7d848c"/><rect x="${r1(x + w * 0.64)}" y="${r1(y + h * 0.45)}" width="${r1(w * 0.14)}" height="${r1(h * 0.3)}" rx="2" fill="#7d848c"/>`
      : `<rect x="${r1(x + w * 0.4)}" y="${r1(y + h * 0.22)}" width="${r1(w * 0.3)}" height="${r1(h * 0.14)}" rx="2" fill="#7d848c"/><rect x="${r1(x + w * 0.4)}" y="${r1(y + h * 0.64)}" width="${r1(w * 0.3)}" height="${r1(h * 0.14)}" rx="2" fill="#7d848c"/>`;
    return `<g filter="${this.u('s3')}"><rect x="${x}" y="${y}" width="${w}" height="${h}" rx="5" fill="${this.u('tin')}"/></g><rect x="${x + 0.5}" y="${y + 0.5}" width="${w - 1}" height="${h - 1}" rx="5" fill="none" stroke="#fff" stroke-opacity=".5"/>${slots}${lip}`;
  }

  // One pole of a green screw terminal block, centre x,y, cell w by h.
  // open: the side the wire enters. dim: a pole the bench does not use.
  pole(x: number, y: number, w: number, h: number, open: Side, o: { dim?: boolean } = {}): string {
    const { dim = false } = o;
    const g = this.grad('term', ['#4fbf7a', '#2f9a57', '#1f7a41']);
    const e = 7;
    const entry = open === 'W' ? [x - w / 2, y - 5, e, 10] : open === 'E' ? [x + w / 2 - e, y - 5, e, 10]
      : open === 'S' ? [x - 5, y + h / 2 - e, 10, e] : [x - 5, y - h / 2, 10, e];
    const sr = Math.min(w, h) * (dim ? 0.2 : 0.3);
    const screw = `<circle cx="${x}" cy="${y}" r="${r1(sr)}" fill="${this.u('ring')}" stroke="#5d646c" stroke-width=".8"${dim ? ' opacity=".55"' : ''}/><path d="M${r1(x - sr * 0.7)} ${r1(y - sr * 0.7)}L${r1(x + sr * 0.7)} ${r1(y + sr * 0.7)}" stroke="#4a5058" stroke-width="${dim ? 1.2 : 2}"${dim ? ' opacity=".55"' : ''}/>`;
    return `<rect x="${r1(x - w / 2)}" y="${r1(y - h / 2)}" width="${w}" height="${h}" fill="${g}" stroke="#145a2c" stroke-width="1"/><rect x="${r1(entry[0])}" y="${r1(entry[1])}" width="${entry[2]}" height="${entry[3]}" fill="#0f3d1e"/>${screw}`;
  }

  // Current shunt: tinned ends, dark body, value print.
  shunt(x: number, y: number, w: number, h: number, text: string, rot = 0): string {
    const e = w * 0.16;
    return `<g transform="translate(${x},${y}) rotate(${rot})" filter="${this.u('s2')}"><rect x="${r1(-w / 2)}" y="${r1(-h / 2)}" width="${w}" height="${h}" rx="2" fill="${this.u('tin')}"/><rect x="${r1(-w / 2 + e)}" y="${r1(-h / 2)}" width="${r1(w - 2 * e)}" height="${h}" fill="${this.u('body')}"/><rect x="${r1(-w / 2 + e)}" y="${r1(-h / 2)}" width="${r1(w - 2 * e)}" height="${r1(h * 0.18)}" fill="#fff" opacity=".07"/><text y="${r1(h * 0.16)}" text-anchor="middle" font-size="${r1(h * 0.44)}" font-weight="500" fill="#d8dadd" letter-spacing=".3">${esc(text)}</text></g>`;
  }

  shadow(inner: string, f = 's3'): string { return `<g filter="${this.u(f)}">${inner}</g>`; }

  // Insulated lead along an SVG path.
  lead(dPath: string, colour: string, wd = 5): string {
    return `<path d="${dPath}" fill="none" stroke="#000" stroke-opacity=".28" stroke-width="${wd + 2}" stroke-linecap="round" stroke-linejoin="round" transform="translate(0,1.5)"/>`
      + `<path d="${dPath}" fill="none" stroke="${colour}" stroke-width="${wd}" stroke-linecap="round" stroke-linejoin="round"/>`
      + `<path d="${dPath}" fill="none" stroke="#fff" stroke-opacity=".28" stroke-width="${r1(wd * 0.28)}" stroke-linecap="round" stroke-linejoin="round" transform="translate(0,${r1(-wd * 0.18)})"/>`;
  }

  // Bare end of a lead: a tinned or gold contact pointing in direction dir.
  contact(x: number, y: number, dir: Side, o: { len?: number; wd?: number; gold?: boolean; dim?: boolean } = {}): string {
    const { len = 16, wd = 7, gold = false, dim = false } = o;
    const k = dim ? 0.65 : 1;
    const L = len * k, W = wd * k;
    const fill = gold ? this.u('gold') : this.u('tin');
    const rect = dir === 'E' ? [x - L, y - W / 2, L, W] : dir === 'W' ? [x, y - W / 2, L, W]
      : dir === 'S' ? [x - W / 2, y - L, W, L] : [x - W / 2, y, W, L];
    return `<rect x="${r1(rect[0])}" y="${r1(rect[1])}" width="${r1(rect[2])}" height="${r1(rect[3])}" rx="${r1(W * 0.35)}" fill="${fill}" stroke="#5d646c" stroke-width=".6" filter="${this.u('s2')}"/>`;
  }

  // Wire loop (eyelet) at the end of a bare component lead.
  eye(x: number, y: number, r = 6, dim = false): string {
    this.eyes.push([x, y]);
    const rr = dim ? r * 0.6 : r;
    return `<circle cx="${x}" cy="${y}" r="${r1(rr)}" fill="${CARD}" stroke="${this.u('tin')}" stroke-width="${dim ? 2 : 3}"/><circle cx="${x}" cy="${y}" r="${r1(rr + (dim ? 1 : 1.5))}" fill="none" stroke="#6d737b" stroke-width=".6"/>`;
  }

  // Axial part (resistor, diode) between two points.
  axial(x1: number, y1: number, x2: number, y2: number, o: { len?: number; wd?: number; colour?: string; bands?: Array<[number, string, number?]>; glass?: boolean } = {}): string {
    const { len = 70, wd = 20, colour = '#4f93c8', bands = [], glass = false } = o;
    this.leads.push([x1, y1, x2, y2]);
    const ang = Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI;
    const cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
    const g = this.grad(`ax-${colour.slice(1)}`, ['#ffffff', colour, colour, '#000000'], 'v');
    let b = '';
    bands.forEach(([pos, c, bw = 5]) => { b += `<rect x="${r1(-len / 2 + pos * len - bw / 2)}" y="${-wd / 2}" width="${bw}" height="${wd}" fill="${c}"/>`; });
    const body = glass
      ? `<rect x="${-len / 2}" y="${-wd / 2}" width="${len}" height="${wd}" rx="${wd / 2.4}" fill="${colour}"/>`
      : `<rect x="${-len / 2}" y="${-wd / 2}" width="${len}" height="${wd}" rx="${wd / 2.2}" fill="${colour}"/>`;
    return `<path d="M${x1} ${y1}L${x2} ${y2}" stroke="#7d848c" stroke-width="4.2" stroke-linecap="round"/><path d="M${x1} ${y1}L${x2} ${y2}" stroke="#d7dbe0" stroke-width="2.4" stroke-linecap="round"/>`
      + `<g transform="translate(${r1(cx)},${r1(cy)}) rotate(${r1(ang)})" filter="${this.u('s3')}">${body}${b}<rect x="${-len / 2}" y="${-wd / 2}" width="${len}" height="${wd}" rx="${wd / 2.2}" fill="${g}" opacity=".28"/></g>`;
  }

  // Bare lead between two points, without a body.
  bare(x1: number, y1: number, x2: number, y2: number): string {
    this.leads.push([x1, y1, x2, y2]);
    const d = x2 !== x1 && y2 === y1 ? `M${x1} ${y1}H${x2}` : `M${x1} ${y1}L${x2} ${y2}`;
    return `<path d="${d}" stroke="#7d848c" stroke-width="4.2" stroke-linecap="round"/><path d="${d}" stroke="#d7dbe0" stroke-width="2.4" stroke-linecap="round"/>`;
  }

  /**
   * The lead ends and eyelets that reach no pin. A lead end reaches a pin when a
   * chain of leads that share end points ends at the wire point of a pin.
   * Returns each loose point as "x,y".
   */
  loose(): string[] {
    // An anchor is stored with 1 decimal. A lead end is compared at the same precision.
    const key = (x: number, y: number): string => `${r1(x)},${r1(y)}`;
    const reached = new Set(Object.values(this.anchors).map((a) => key(a.x, a.y)));
    for (let grown = true; grown;) {
      grown = false;
      for (const [x1, y1, x2, y2] of this.leads) {
        const a = key(x1, y1), b = key(x2, y2);
        if (reached.has(a) !== reached.has(b)) { reached.add(a); reached.add(b); grown = true; }
      }
    }
    const ends = [...this.leads.flatMap(([x1, y1, x2, y2]) => [key(x1, y1), key(x2, y2)]), ...this.eyes.map(([x, y]) => key(x, y))];
    return [...new Set(ends.filter((k) => !reached.has(k)))];
  }

  solder(x: number, y: number, r = 6): string {
    return `<circle cx="${x}" cy="${y}" r="${r}" fill="${this.u('ring')}" stroke="#6d737b" stroke-width=".6" filter="${this.u('s2')}"/>`;
  }

  svg(): string {
    const p = this.pfx;
    const defs = [
      `<linearGradient id="${p}-tin" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f6f7f8"/><stop offset=".5" stop-color="#c9ced4"/><stop offset="1" stop-color="#9aa1a9"/></linearGradient>`,
      `<linearGradient id="${p}-gold" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fbe9ad"/><stop offset=".5" stop-color="#dcb450"/><stop offset="1" stop-color="#a47a1e"/></linearGradient>`,
      `<linearGradient id="${p}-copper" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#eeb184"/><stop offset=".5" stop-color="#c47c45"/><stop offset="1" stop-color="#8f5628"/></linearGradient>`,
      `<radialGradient id="${p}-ring" cx=".35" cy=".3" r=".9"><stop offset="0" stop-color="#ffffff"/><stop offset=".6" stop-color="#cfd3d8"/><stop offset="1" stop-color="#8e959d"/></radialGradient>`,
      `<linearGradient id="${p}-body" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3a3c42"/><stop offset="1" stop-color="#1b1c20"/></linearGradient>`,
      `<filter id="${p}-sh" x="-15%" y="-15%" width="130%" height="135%"><feDropShadow dx="0" dy="7" stdDeviation="8" flood-color="#000" flood-opacity=".35"/></filter>`,
      `<filter id="${p}-s2" x="-30%" y="-30%" width="160%" height="180%"><feDropShadow dx="0" dy="1.5" stdDeviation="1.2" flood-color="#000" flood-opacity=".7"/></filter>`,
      `<filter id="${p}-s3" x="-20%" y="-20%" width="140%" height="150%"><feDropShadow dx="0" dy="3" stdDeviation="2.5" flood-color="#000" flood-opacity=".45"/></filter>`,
      ...this.grads.values(),
    ];
    return `<svg xmlns="http://www.w3.org/2000/svg" lang="${this.lang}" xml:lang="${this.lang}" viewBox="0 0 ${this.w} ${this.h}" width="${this.w}" height="${this.h}" role="img" aria-labelledby="${p}-t" aria-describedby="${p}-d" font-family="${FONT}">\n`
      + `<title id="${p}-t">${esc(this.title)}</title>\n<desc id="${p}-d">${esc(this.desc)}</desc>\n<defs>\n${defs.join('\n')}\n</defs>\n`
      + `<rect width="${this.w}" height="${this.h}" rx="10" fill="${CARD}"/>\n${this.out.join('\n')}\n</svg>\n`;
  }
}
