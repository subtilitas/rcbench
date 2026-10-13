// The part drawings: one draw function per drawing and the list of drawings.
// A draw function places shapes and pins on a Dwg. It reads no file and no clock.
// Coordinates are px of the viewBox. The sources of the shapes are the owner's
// photos of the boards; a shape without a photo is a generic picture of the part.

import type { Text } from '../contract.ts';
import { CARD, Dwg, INK, SILK } from './lib.ts';

// ---------------------------------------------------------------- coprocessor
// The RP2350-CAN board in local coordinates 0..280 by 0..720, USB-C at the top.
// pads: 'rings' (labelled solder pads) or 'header' (plugged into a carrier).
function rpBoard(d: Dwg, ox: number, oy: number, k: number, mode: 'rings' | 'header'): void {
  const g: string[] = [];
  g.push(d.board(0, 0, 280, 720, 'blue', 6));
  const cx = 134;
  // traces
  g.push(`<g fill="none" stroke="#2a5aa6" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M${cx} 40V110M${cx} 140V200M${cx} 262V300M${cx} 345V362M${cx} 410V432M${cx} 470V500M${cx} 530V650"/><path d="M112 230H96V180M158 230H176V300M112 386H98V450M156 386H172V330"/></g>`);
  g.push(d.usbc(cx - 30, -12, 60, 48, 'N'));
  g.push(d.button(cx - 22, 62, 17), d.button(cx + 22, 62, 17));
  g.push(d.text(cx - 22, 84, 'BOOT', { sz: 8, a: 'middle' }), d.text(cx + 22, 84, 'RESET', { sz: 8, a: 'middle' }));
  g.push(d.smd(cx - 22, 100, 'led', 0, 0.8), d.smd(cx + 22, 100, 'r', 0, 0.8));
  g.push(d.chip(cx, 128, 28, 22, { n: 4 }));
  g.push(d.shadow(d.tin(cx, 168, 30, 12, 3), 's2'));
  g.push(d.smd(cx - 24, 190, 'c', 90, 0.8), d.smd(cx + 24, 190, 'c', 90, 0.8));
  g.push(d.chip(cx, 232, 46, 46, { n: 7, sides: 4, leg: 3, text: ['RP2350'], tsz: 8 }));
  g.push(d.smd(cx - 26, 276, 'c', 0, 0.8), d.smd(cx + 26, 276, 'r', 0, 0.8));
  g.push(d.chip(cx - 14, 304, 16, 12, { n: 3, leg: 3 }));
  g.push(`<rect x="${cx + 4}" y="294" width="22" height="22" rx="3" fill="#33363c" filter="${d.u('s2')}"/><circle cx="${cx + 15}" cy="305" r="6" fill="#4a4e56"/>`);
  g.push(d.shadow(d.tin(cx, 336, 26, 11, 3), 's2'));
  g.push(d.chip(cx, 386, 34, 34, { n: 5, sides: 4, leg: 3, text: ['XL2515'], tsz: 6.5 }));
  g.push(d.smd(cx - 24, 422, 'c', 0, 0.8), d.smd(cx + 24, 422, 'r', 0, 0.8));
  g.push(d.chip(cx, 452, 30, 24, { n: 4, text: ['SIT65', 'HVD230'], tsz: 5.5 }));
  // slide switch of the 120 ohm termination
  g.push(`<g filter="${d.u('s2')}"><rect x="${cx - 17}" y="497" width="34" height="18" rx="2" fill="${d.u('tin')}"/><rect x="${cx - 12}" y="501" width="24" height="10" fill="#17181c"/><rect x="${cx - 1}" y="501" width="11" height="10" fill="#e9e6de"/></g>`);
  g.push(d.tin(cx - 14, 540, 9, 12), d.tin(cx, 540, 9, 12), d.tin(cx + 14, 540, 9, 12));
  g.push(d.text(cx, 584, 'RP2350-CAN', { sz: 11, a: 'middle', ls: '.3' }));
  const an: number[] = [];
  for (let i = 0; i < 20; i++) {
    const y = 70 + 30 * i;
    const nl = i + 1, nr = 40 - i;
    if (mode === 'rings') {
      g.push(d.th(16, y, 9, { sq: i === 0 }), d.th(264, y, 9));
      g.push(d.text(30, y + 3.5, String(nl), { sz: 9.5, f: '#bcd0f2', w: 500 }));
      g.push(d.text(250, y + 3.5, String(nr), { sz: 9.5, f: '#bcd0f2', w: 500, a: 'end' }));
      g.push(d.pin(`pad.${nl}`, ox + 16 * k, oy + y * k, 'W', 46, y + 4.5, { sz: 13 }));
      g.push(d.pin(`pad.${nr}`, ox + 264 * k, oy + y * k, 'E', 235, y + 4.5, { sz: 13, a: 'end' }));
    } else {
      an.push(y);
    }
  }
  if (mode === 'header') {
    g.push(`<rect x="5" y="52" width="22" height="606" rx="2" fill="#17181c"/><rect x="253" y="52" width="22" height="606" rx="2" fill="#17181c"/>`);
    for (const y of an) g.push(`<circle cx="16" cy="${y}" r="5" fill="${d.u('gold')}"/><circle cx="264" cy="${y}" r="5" fill="${d.u('gold')}"/>`);
  }
  d.add(`<g transform="translate(${ox},${oy}) scale(${k})">${g.join('')}</g>`);
}

// The 3-pole CAN screw terminal, wires enter from below. Poles: H next to pad 20, GND, L next to pad 21.
function canTerminal(d: Dwg, cx: number, top: number): void {
  const pitch = 46, h = 66;
  const xs = [cx - pitch, cx, cx + pitch];
  const s: string[] = [];
  s.push(`<g filter="${d.u('s3')}"><rect x="${xs[0] - pitch / 2 - 2}" y="${top}" width="${3 * pitch + 4}" height="${h}" rx="2" fill="#1f7a41"/></g>`);
  s.push(d.pole(xs[0], top + h / 2, pitch, h, 'S'), d.pole(xs[1], top + h / 2, pitch, h, 'S', { dim: true }), d.pole(xs[2], top + h / 2, pitch, h, 'S'));
  s.push(`<rect x="${xs[0] - 21}" y="${top - 17}" width="134" height="15" rx="2" fill="#173c76" opacity=".85"/>`);
  // middle pole: ground of the transceiver, no wire of the bench
  s.push(d.text(xs[1], top - 6, 'GND', { sz: 11, a: 'middle', f: '#9aa0a8' }));
  s.push(d.pin('can.H', xs[0], top + h, 'S', xs[0], top - 6, { sz: 11, a: 'middle' }));
  s.push(d.pin('can.L', xs[2], top + h, 'S', xs[2], top - 6, { sz: 11, a: 'middle' }));
  d.add(...s);
}

function copro(d: Dwg): void {
  rpBoard(d, 30, 40, 1, 'rings');
  canTerminal(d, 30 + 134, 40 + 664);
}

/** The 3 rows of the servo strips of the breakout board, from the screw terminal inward. */
export const STRIP_ROWS = ['S', 'V', 'G'] as const;
export type StripRow = (typeof STRIP_ROWS)[number];
const STRIP = { pitch: 34, y0: 84, left: [126, 135, 144], right: [344, 335, 326], colours: ['#1c4fa8', '#b3261e', '#17181c'] } as const;

/**
 * The pin of a servo strip of `copro--osoyoo.svg` in px of the drawing: the pin of
 * row S, V or G beside the screw terminal of a pad, 1 to 40. A plug leaves towards
 * the viewer, so the point carries the side of the board edge of its pad.
 */
export function stripPoint(pad: number, row: StripRow): { x: number; y: number; side: 'W' | 'E' } {
  if (!Number.isInteger(pad) || pad < 1 || pad > 40) throw new Error(`pad ${pad}: the breakout board has the pads 1 to 40`);
  const left = pad <= 20;
  const i = STRIP_ROWS.indexOf(row);
  return { x: (left ? STRIP.left : STRIP.right)[i]!, y: STRIP.y0 + STRIP.pitch * (left ? pad - 1 : 40 - pad), side: left ? 'W' : 'E' };
}

function coproOsoyoo(d: Dwg): void {
  d.add(d.board(16, 16, 448, 788, 'blue', 8));
  d.add(d.hole(112, 40, 8), d.hole(368, 40, 8), d.hole(112, 780, 8), d.hole(368, 780, 8));
  const { pitch, y0 } = STRIP;
  d.add(`<g filter="${d.u('s3')}"><rect x="20" y="${y0 - pitch / 2 - 2}" width="38" height="${20 * pitch + 4}" rx="2" fill="#1f7a41"/><rect x="422" y="${y0 - pitch / 2 - 2}" width="38" height="${20 * pitch + 4}" rx="2" fill="#1f7a41"/></g>`);
  // Servo strips: 1 plug point with the pins S, V and G beside every screw terminal.
  // V is 1 net; the jumper V Selector feeds it from the 5 V converter or the 3.3 V regulator.
  for (const xs of [STRIP.left, STRIP.right]) {
    xs.forEach((x, i) => {
      d.add(`<rect x="${x - 4}" y="${y0 - 14}" width="8" height="${19 * pitch + 28}" fill="${STRIP.colours[i]}"/>`);
      d.add(d.text(x, y0 + 19 * pitch + 26, STRIP_ROWS[i]!, { sz: 9, a: 'middle' }));
    });
  }
  for (let i = 0; i < 20; i++) {
    const y = y0 + pitch * i;
    const nl = i + 1, nr = 40 - i;
    d.add(d.pole(39, y, 34, pitch, 'W'), d.pole(441, y, 34, pitch, 'E'));
    d.add(d.pin(`pad.${nl}`, 22, y, 'W', 64, y + 4.5, { sz: 13 }));
    d.add(d.pin(`pad.${nr}`, 458, y, 'E', 416, y + 4.5, { sz: 13, a: 'end' }));
    for (const row of STRIP_ROWS) {
      for (const p of [stripPoint(nl, row), stripPoint(nr, row)]) d.add(`<circle cx="${p.x}" cy="${p.y}" r="2.6" fill="${d.u('gold')}"/>`);
    }
  }
  // socket strips and the module
  rpBoard(d, 163, 150, 0.55, 'header');
  canTerminal(d, 163 + 134 * 0.55, 548);
  // Carrier parts at the USB end, in the order of photo 05 from the DC jack across the board:
  // DC jack at the board edge, switch, LED, the 3-pin voltage jumper, 2 capacitors, and the
  // DC-DC converter: an 8-pin chip and its inductor. The group is not to scale.
  // The 2-pole VIN terminal and the 3 x 3 pin header of the carrier are not drawn.
  d.add(`<g filter="${d.u('s3')}"><rect x="170" y="20" width="44" height="68" rx="3" fill="#1b1c20"/><rect x="178" y="20" width="28" height="12" fill="#0b0b0d"/></g>`);
  d.add(`<g filter="${d.u('s2')}"><rect x="232" y="30" width="40" height="18" rx="2" fill="${d.u('tin')}"/><rect x="238" y="34" width="28" height="10" fill="#17181c"/><rect x="240" y="34" width="12" height="10" fill="#e9e6de"/></g>`, d.smd(296, 39, 'led'), d.smd(296, 52, 'r'));
  // Jumper: 3 pins, the cap on 2 of them. The print reads 3V3 and 5V from the left (product photo of the vendor); the cap is drawn on the 5V pair, the position a servo on the V pins needs, and the label states the cap position only. The board is delivered with the cap on 3V3.
  d.add(`<g filter="${d.u('s2')}"><rect x="222" y="62" width="34" height="12" rx="1.5" fill="#17181c"/><rect x="233" y="60" width="23" height="16" rx="2" fill="#2a2c31"/></g>`);
  for (const x of [228, 239, 250]) d.add(`<circle cx="${x}" cy="68" r="2.2" fill="${d.u('gold')}"/>`);
  d.add(d.text(239, 90, '3V3 / 5V', { sz: 9, a: 'middle' }));
  d.add(d.text(256, 101, d.lang === 'de' ? 'Kappe auf 5V' : 'cap on 5V', { sz: 9, a: 'end' }));
  const cap = (x: number, y: number): string => `<circle cx="${x}" cy="${y}" r="11" fill="${d.u('tin')}"/><path d="M${x - 9} ${y - 7}A11 11 0 0 1 ${x + 9} ${y - 7}Z" fill="#1b1c20"/>`;
  d.add(`<g filter="${d.u('s3')}">${cap(279, 76)}${cap(305, 76)}</g>`);
  d.add(d.chip(262, 113, 18, 16, { n: 4, leg: 3 }));
  d.add(`<rect x="284" y="98" width="30" height="30" rx="4" fill="#33363c" filter="${d.u('s2')}"/><circle cx="299" cy="113" r="9" fill="#4a4e56"/>`);
  d.add(d.text(281, 142, 'DC-DC', { sz: 9, a: 'middle' }));
}

// ---------------------------------------------------------------- panel
function panel(d: Dwg): void {
  d.add(`<g filter="${d.u('sh')}"><rect x="12" y="14" width="356" height="262" rx="12" fill="#1a2733"/></g>`);
  d.add(`<rect x="28" y="26" width="324" height="238" rx="4" fill="${d.grad('plate', ['#d9dee2', '#c3c9ce', '#a9b0b6'])}"/>`);
  d.add(d.hole(46, 48, 5, '#70777e'), d.hole(338, 44, 5, '#70777e'), d.hole(46, 244, 5, '#70777e'), d.hole(338, 248, 5, '#70777e'));
  // touch flex cable
  d.add(`<rect x="150" y="14" width="56" height="46" fill="#c9862f"/><rect x="166" y="26" width="22" height="18" rx="1" fill="#1b1c20"/>`);
  d.add(d.board(66, 54, 240, 204, 'blue', 4));
  d.add(d.hole(84, 70, 4.5, '#70777e'), d.hole(288, 70, 4.5, '#70777e'), d.hole(84, 240, 4.5, '#70777e'));
  // card slot, USB-C sockets, radio module
  d.add(`<g filter="${d.u('s3')}"><rect x="100" y="62" width="40" height="34" rx="2" fill="${d.u('tin')}"/></g><path d="M106 70H134M106 78H126" stroke="#8e959d" stroke-width="1.5"/>`);
  d.add(d.usbc(58, 108, 30, 22, 'W'), d.usbc(58, 150, 30, 22, 'W'));
  d.add(`<g filter="${d.u('s3')}"><rect x="72" y="196" width="52" height="54" rx="2" fill="${d.u('tin')}"/></g><rect x="72" y="250" width="52" height="14" fill="#1b1c20"/><path d="M78 260V254H86V260H94V254H102V260H110V254H118" fill="none" stroke="#c9a54a" stroke-width="1.2"/>`);
  // display flex cable and its connector
  d.add(`<rect x="148" y="112" width="56" height="62" fill="#a9a6a0"/><rect x="148" y="174" width="56" height="14" fill="#c9862f"/><rect x="142" y="188" width="68" height="9" rx="1" fill="#e9e6de" filter="${d.u('s2')}"/>`);
  d.add(d.chip(176, 86, 16, 12, { n: 3, leg: 3 }), d.chip(212, 84, 22, 14, { n: 4, leg: 3 }));
  d.add(d.smd(120, 120, 'c', 0, 0.8), d.smd(120, 136, 'r', 0, 0.8), d.smd(104, 152, 'r', 90, 0.8), d.smd(120, 170, 'c', 0, 0.8));
  d.add(d.chip(150, 226, 20, 16, { n: 4, leg: 3 }), d.smd(182, 214, 'r', 0, 0.8), d.smd(182, 230, 'c', 0, 0.8), d.smd(206, 222, 'r', 90, 0.8));
  d.add(`<rect x="138" y="244" width="22" height="9" rx="1" fill="#1b1c20"/><rect x="170" y="244" width="22" height="9" rx="1" fill="#1b1c20"/>`);
  d.add(`<rect x="212" y="238" width="9" height="14" rx="1" fill="#e2b93b"/><rect x="232" y="238" width="9" height="14" rx="1" fill="#e2b93b"/>`);
  // connector column on the right edge
  const conn = (y: number, h: number): string => `<g filter="${d.u('s3')}"><rect x="280" y="${y}" width="30" height="${h}" rx="2" fill="#f4f1e8"/></g><rect x="284" y="${y + 3}" width="18" height="${h - 6}" fill="#d9d5c9"/>`;
  const big = (y: number): string => `<rect x="288" y="${y - 4}" width="12" height="8" rx="1" fill="${d.u('gold')}" stroke="#6b5a1e" stroke-width=".6"/>`;
  const small = (y: number): string => `<rect x="290" y="${y - 2.2}" width="8" height="4.4" rx="1" fill="${d.u('tin')}"/>`;
  d.add(conn(84, 60), big(96), big(114), small(132));
  d.add(d.pin('J8G', 294, 114, 'E', 274, 118.5, { sz: 12, a: 'end' }));
  d.add(d.pin('J8S', 294, 96, 'E', 274, 100.5, { sz: 12, a: 'end' }));
  d.add(conn(150, 44), big(163), big(181));
  d.add(d.pin('CANH', 294, 163, 'E', 274, 167.5, { sz: 12, a: 'end' }));
  d.add(d.pin('CANL', 294, 181, 'E', 274, 185.5, { sz: 12, a: 'end' }));
  d.add(conn(200, 34), small(208), small(214.5), small(221), small(227.5));
  d.add(conn(238, 18), small(244), small(250.5));
}

// ---------------------------------------------------------------- microSD card
function sd(d: Dwg): void {
  d.add(`<g filter="${d.u('sh')}"><path d="M56 30H132Q140 30 140 38V162Q140 170 132 170H64Q56 170 56 162V96L62 90V76L56 70Z" fill="${d.grad('card', ['#34373d', '#1d1e22', '#121316'])}"/></g>`);
  d.add(`<rect x="68" y="44" width="60" height="70" rx="3" fill="${d.grad('lab', ['#f4f1ea', '#dcd8cd'])}"/><rect x="76" y="56" width="44" height="6" rx="2" fill="#b9b4a6"/><rect x="76" y="70" width="30" height="6" rx="2" fill="#cfcabd"/>`);
  d.add(`<path d="M68 150H128" stroke="#3d4047" stroke-width="2"/><path d="M62 160H134" stroke="#fff" stroke-opacity=".08" stroke-width="2"/>`);
}

// ---------------------------------------------------------------- emergency stop
function estop(d: Dwg): void {
  const red = d.grad('red', ['#f0564a', '#c2221b', '#7f0f0c'], 'v');
  const chrome = d.grad('chr', ['#fafbfc', '#c9ced4', '#8b929a', '#d9dde1'], 'v');
  d.add(`<g filter="${d.u('sh')}"><path d="M22 58Q22 40 44 40H96Q112 40 118 58L124 62V138L118 142Q112 160 96 160H44Q22 160 22 142Z" fill="${red}"/></g>`);
  d.add(`<path d="M34 52Q60 44 104 52" fill="none" stroke="#fff" stroke-opacity=".35" stroke-width="5" stroke-linecap="round"/>`);
  d.add(`<rect x="124" y="54" width="14" height="92" rx="3" fill="${chrome}"/>`);
  d.add(`<rect x="138" y="70" width="74" height="60" fill="${chrome}"/><path d="M178 70V130M184 70V130M190 70V130M196 70V130M202 70V130M208 70V130" stroke="#7d848c" stroke-width="1.2"/>`);
  d.add(`<g filter="${d.u('s3')}"><rect x="146" y="56" width="28" height="88" rx="3" fill="${chrome}"/></g><path d="M146 80H174M146 120H174" stroke="#7d848c" stroke-width="1"/>`);
  d.add(`<rect x="212" y="66" width="38" height="68" rx="5" fill="${d.grad('clr', ['#cfe6f5', '#8fc1e6', '#4f8fc4'], 'v')}" stroke="#3f77a8" stroke-width="1"/><rect x="232" y="72" width="18" height="56" rx="2" fill="#2f6fb0" opacity=".7"/>`);
  const tab = (y: number, dim: boolean): string => {
    const L = dim ? 24 : 40, W = dim ? 7 : 11;
    return `<rect x="250" y="${y - W / 2}" width="${L}" height="${W}" rx="2" fill="${d.u('gold')}" stroke="#6b5a1e" stroke-width=".6" filter="${d.u('s2')}"/><circle cx="${250 + L - 7}" cy="${y}" r="${dim ? 1.6 : 2.8}" fill="${CARD}" stroke="#6b5a1e" stroke-width=".6"/>`;
  };
  d.add(tab(72, false), tab(100, true), tab(128, false));
  // 3 tabs in a row: C, NO, NC. The 2 outer tabs conduct with the button released; the middle tab (NO) stays free.
  d.add(d.pin('NC1', 283, 72, 'E', 332, 58, { f: INK, a: 'end' }));
  d.add(d.text(282, 104, 'NO', { f: '#8a8f97', sz: 11 }));
  d.add(d.pin('NC2', 283, 128, 'E', 332, 152, { f: INK, a: 'end' }));
}

// ---------------------------------------------------------------- servo
function servo(d: Dwg): void {
  const body = d.grad('sv', ['#3b4250', '#232831', '#15181e']);
  d.add(`<g filter="${d.u('sh')}"><rect x="38" y="24" width="64" height="176" rx="4" fill="#2b303a"/><rect x="34" y="44" width="72" height="136" rx="6" fill="${body}"/></g>`);
  d.add(d.hole(70, 34, 3.5), d.hole(70, 190, 3.5));
  d.add(`<rect x="42" y="118" width="56" height="50" rx="3" fill="${d.grad('lab', ['#f4f1ea', '#dcd8cd'])}" opacity=".9"/><rect x="50" y="130" width="40" height="5" rx="2" fill="#b9b4a6"/><rect x="50" y="142" width="26" height="5" rx="2" fill="#cfcabd"/>`);
  // horn
  d.add(`<g filter="${d.u('s3')}"><path d="M62 78L66 30Q70 24 74 30L78 78Q84 86 78 94Q70 100 62 94Q56 86 62 78Z" fill="#e9e6de"/></g><circle cx="70" cy="84" r="5" fill="${d.u('ring')}"/><circle cx="70" cy="62" r="1.8" fill="#8e959d"/><circle cx="70" cy="50" r="1.8" fill="#8e959d"/><circle cx="70" cy="38" r="1.8" fill="#8e959d"/>`);
  const ys = [64, 112, 160];
  const cols = ['#e58a1f', '#c8261d', '#6b4423'];
  const ids = ['SIG', 'POS', 'NEG'];
  ids.forEach((id, i) => {
    const y = ys[i];
    d.add(d.lead(`M106 ${156 + i * 6}C150 ${156 + i * 6} 150 ${y} 196 ${y}H268`, cols[i], 5));
    d.add(d.contact(286, y, 'E', { len: 20, wd: 8, gold: true }));
    d.add(d.pin(id, 286, y, 'E', 282, y - 12, { f: INK, a: 'end', sz: id === 'SIG' ? 13 : 17 }));
  });
}

// ---------------------------------------------------------------- ESC and motor
function escPart(d: Dwg): void {
  const shrink = d.grad('es', ['#2f3c56', '#1c2536', '#121824']);
  d.add(`<g filter="${d.u('sh')}"><rect x="22" y="34" width="104" height="204" rx="8" fill="${shrink}"/></g>`);
  d.add(`<rect x="34" y="50" width="80" height="84" rx="2" fill="${d.grad('fin', ['#b9bec5', '#8e959d', '#6f767e'], 'h')}"/><path d="M44 50V134M54 50V134M64 50V134M74 50V134M84 50V134M94 50V134M104 50V134" stroke="#4f555c" stroke-width="2"/>`);
  d.add(`<rect x="34" y="150" width="80" height="44" rx="3" fill="${d.grad('lab', ['#f4f1ea', '#dcd8cd'])}" opacity=".9"/><rect x="42" y="160" width="52" height="5" rx="2" fill="#b9b4a6"/><rect x="42" y="172" width="34" height="5" rx="2" fill="#cfcabd"/>`);
  d.add(`<g filter="${d.u('s3')}"><rect x="40" y="204" width="68" height="26" rx="12" fill="#22252b"/></g><rect x="44" y="208" width="60" height="6" rx="3" fill="#fff" opacity=".12"/>`);
  const rows: Array<[string, string, number, 'c' | 'b', number]> = [
    ['SIG', '#e58a1f', 4.5, 'c', 44],
    ['BEC', '#c8261d', 4.5, 'c', 50],
    ['SG', '#6b4423', 4.5, 'c', 56],
    ['POS', '#c8261d', 8, 'b', 96],
    ['NEG', '#17181c', 8, 'b', 112],
    ['A', '#2a2d33', 7, 'b', 176],
    ['B', '#2a2d33', 7, 'b', 192],
    ['C', '#2a2d33', 7, 'b', 208],
  ];
  rows.forEach(([id, col, wd, kind, y0], i) => {
    const y = 46 + 36 * i;
    d.add(d.lead(`M126 ${y0}C170 ${y0} 160 ${y} 204 ${y}H318`, col, wd));
    d.add(kind === 'c' ? d.contact(338, y, 'E', { len: 22, wd: 8, gold: true }) : d.contact(338, y, 'E', { len: 24, wd: 12, gold: true }));
    const big = id === 'POS' || id === 'NEG';
    d.add(d.pin(id, 338, y, 'E', 334, y - 12, { f: INK, a: 'end', sz: big ? 17 : 13 }));
  });
  // motor with its 3 leads, unplugged
  d.add(`<g filter="${d.u('sh')}"><circle cx="66" cy="296" r="34" fill="${d.grad('mot', ['#4a4f58', '#2a2d33', '#17181c'])}"/></g><circle cx="66" cy="296" r="24" fill="none" stroke="#6f767e" stroke-width="2" stroke-dasharray="9 5"/><circle cx="66" cy="296" r="9" fill="${d.u('ring')}"/><circle cx="66" cy="296" r="3.5" fill="#5d646c"/>`);
  [284, 296, 308].forEach((y) => {
    d.add(d.lead(`M98 ${y}H140`, '#2a2d33', 4));
    d.add(d.contact(154, y, 'E', { len: 20, wd: 10, gold: true, dim: true }));
  });
}

// ---------------------------------------------------------------- PD mini
function pdmini(d: Dwg): void {
  d.add(d.board(30, 24, 280, 196, 'black', 8));
  d.add(d.hole(44, 38, 3.5), d.hole(140, 112, 3.5), d.hole(232, 32, 3.5), d.hole(296, 206, 3.5));
  d.add(`<g fill="none" stroke="#2c2f35" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M64 60H86M112 60H128M172 60H180M206 66H230V96H270M206 48H250V70H270M150 86V100H190"/></g>`);
  d.add(d.usbc(18, 40, 46, 40, 'W'));
  d.add(d.pin('USB', 18, 60, 'W', 36, 100, { sz: 12 }));
  d.add(d.chip(98, 60, 22, 18, { n: 4, leg: 3 }));
  d.add(`<g filter="${d.u('s3')}"><rect x="128" y="36" width="44" height="46" rx="5" fill="${d.grad('ind', ['#c9ced4', '#9aa1a9', '#7d848c'])}"/></g>`, d.text(150, 64, '100', { sz: 12, a: 'middle', f: '#2a2c31', w: 500 }));
  const cap = (x: number, y: number): string => `<g filter="${d.u('s3')}"><circle cx="${x}" cy="${y}" r="13" fill="${d.u('tin')}"/></g><path d="M${x - 10.4} ${y - 7.8}A13 13 0 0 1 ${x + 10.4} ${y - 7.8}Z" fill="#b3261e"/>`;
  d.add(cap(194, 48), cap(194, 80));
  d.add(d.smd(150, 100, 'r', 0, 0.8), d.smd(176, 106, 'c', 0, 0.8), d.chip(206, 108, 14, 10, { n: 3, leg: 3 }));
  // buttons with their print
  ([['S', 122], ['+', 146], ['−', 170], ['ON', 194]] as Array<[string, number]>).forEach(([s, y]) => {
    d.add(d.button(52, y, 17), d.text(68, y + 4.5, s, { sz: 11 }));
  });
  // display
  d.add(`<g filter="${d.u('s3')}"><rect x="96" y="136" width="140" height="70" rx="2" fill="#0b0c0e"/></g><rect x="100" y="140" width="118" height="62" rx="1" fill="${d.grad('lcd', ['#1f2630', '#0e1116'])}"/><rect x="220" y="140" width="12" height="62" fill="#27409a"/>`);
  // 4-pole terminal on the right edge: 2 minus poles at the top end, 2 plus poles at the bottom end
  // Source: statement of the owner. The poles are paired.
  const ys = [44, 70, 96, 122];
  d.add(`<g filter="${d.u('s3')}"><rect x="272" y="29" width="50" height="108" rx="2" fill="#1f7a41"/></g>`);
  ys.forEach((y) => d.add(d.pole(297, y, 46, 26, 'E', {})));
  // Poles 2 and 3 carry the same rails as their outer neighbours: minus, minus, plus, plus.
  d.add(d.text(266, 74.5, '−', { sz: 12, a: 'end', f: '#9aa0a8' }), d.text(266, 100.5, '+', { sz: 12, a: 'end', f: '#9aa0a8' }));
  d.add(d.pin('ON', 320, 44, 'E', 266, 48.5, { sz: 12, a: 'end' }));
  d.add(d.pin('OP', 320, 122, 'E', 266, 126.5, { sz: 12, a: 'end' }));
  // 2 by 4 header, not used by the bench
  d.add(`<rect x="252" y="150" width="20" height="50" rx="1" fill="#17181c"/>`);
  for (let i = 0; i < 4; i++) d.add(`<circle cx="257" cy="${157 + i * 12}" r="2.4" fill="${d.u('gold')}"/><circle cx="267" cy="${157 + i * 12}" r="2.4" fill="${d.u('gold')}"/>`);
}

// ---------------------------------------------------------------- PD charger
function charger(d: Dwg): void {
  d.add(`<rect x="14" y="74" width="24" height="9" rx="2" fill="${d.u('tin')}"/><rect x="14" y="117" width="24" height="9" rx="2" fill="${d.u('tin')}"/>`);
  d.add(`<g filter="${d.u('sh')}"><rect x="36" y="40" width="120" height="120" rx="14" fill="${d.grad('ch', ['#ffffff', '#ecebe7', '#cfcdc6'])}"/></g><rect x="36.5" y="40.5" width="119" height="119" rx="14" fill="none" stroke="#b9b6ad"/>`);
  d.add(`<rect x="52" y="56" width="60" height="6" rx="3" fill="#d6d3ca"/><rect x="52" y="70" width="38" height="6" rx="3" fill="#dedbd3"/>`);
  d.add(`<rect x="146" y="86" width="14" height="28" rx="6" fill="#2a2c31"/><rect x="151" y="92" width="6" height="16" rx="3" fill="#8e959d"/>`);
  d.add(d.pin('USB', 160, 100, 'E', 168, 86, { f: INK }));
}

// ---------------------------------------------------------------- servo supply
function ssup(d: Dwg): void {
  d.add(`<g filter="${d.u('sh')}"><rect x="22" y="34" width="150" height="132" rx="6" fill="${d.grad('bx', ['#565c66', '#3a3f47', '#272a30'])}"/></g>`);
  d.add(`<path d="M38 52H110M38 62H110M38 72H110M38 82H110" stroke="#1b1c20" stroke-width="3" stroke-linecap="round"/>`);
  d.add(`<rect x="38" y="104" width="72" height="44" rx="3" fill="#0e1116"/><rect x="42" y="108" width="64" height="36" rx="2" fill="${d.grad('lcd', ['#22303a', '#111a20'])}"/><rect x="50" y="118" width="34" height="6" rx="2" fill="#5fbf8a" opacity=".8"/><rect x="50" y="130" width="22" height="5" rx="2" fill="#5fbf8a" opacity=".5"/>`);
  const post = (y: number, col: string): string => `<g filter="${d.u('s3')}"><rect x="160" y="${y - 13}" width="30" height="26" rx="5" fill="${col}"/></g><circle cx="190" cy="${y}" r="6" fill="${d.u('ring')}" stroke="#5d646c" stroke-width=".8"/>`;
  d.add(post(70, '#c8261d'), post(130, '#17181c'));
  d.add(d.pin('POS', 196, 70, 'E', 142, 77, { sz: 20, a: 'middle' }));
  d.add(d.pin('NEG', 196, 130, 'E', 142, 137, { sz: 20, a: 'middle' }));
}

// ---------------------------------------------------------------- DC supply of the Osoyoo breakout board
// A generic plug-in adapter: no product is chosen, the part is modelled.
function dcsup(d: Dwg): void {
  d.add(`<rect x="6" y="76" width="18" height="8" rx="2" fill="${d.u('tin')}"/><rect x="6" y="116" width="18" height="8" rx="2" fill="${d.u('tin')}"/>`);
  d.add(`<g filter="${d.u('sh')}"><rect x="22" y="44" width="116" height="112" rx="12" fill="${d.grad('dc', ['#4a4f57', '#33373e', '#22252a'])}"/></g>`);
  d.add(`<rect x="40" y="62" width="70" height="40" rx="3" fill="${d.grad('dcl', ['#f4f1ea', '#dcd8cd'])}" opacity=".9"/><rect x="48" y="72" width="46" height="6" rx="2" fill="#b9b4a6"/><rect x="48" y="86" width="30" height="6" rx="2" fill="#cfcabd"/>`);
  d.add(`<rect x="132" y="90" width="16" height="20" rx="4" fill="#17181c"/>`);
  d.add(d.lead('M148 100C176 100 190 72 224 72H238', '#c8261d', 8), d.lead('M148 100C176 100 190 132 224 132H238', '#17181c', 8));
  d.add(d.contact(260, 72, 'E', { len: 24, wd: 12, gold: true }), d.contact(260, 132, 'E', { len: 24, wd: 12, gold: true }));
  d.add(d.pin('POS', 260, 72, 'E', 250, 56, { f: INK, sz: 20, a: 'middle' }));
  d.add(d.pin('NEG', 260, 132, 'E', 250, 160, { f: INK, sz: 20, a: 'middle' }));
}

// ---------------------------------------------------------------- ESC battery
function batt(d: Dwg): void {
  d.add(`<g filter="${d.u('sh')}"><rect x="20" y="42" width="150" height="116" rx="10" fill="${d.grad('bt', ['#3f86d6', '#2462ad', '#18447d'])}"/></g>`);
  d.add(`<path d="M20 80H170M20 120H170" stroke="#12335f" stroke-width="1.5" opacity=".7"/><rect x="36" y="56" width="92" height="48" rx="3" fill="${d.grad('lab', ['#f4f1ea', '#dcd8cd'])}" opacity=".92"/><rect x="46" y="68" width="58" height="6" rx="2" fill="#b9b4a6"/><rect x="46" y="82" width="36" height="6" rx="2" fill="#cfcabd"/>`);
  d.add(`<rect x="162" y="64" width="12" height="72" rx="3" fill="#17181c"/>`);
  d.add(d.lead('M172 86C200 86 196 72 224 72H238', '#c8261d', 8), d.lead('M172 114C200 114 196 132 224 132H238', '#17181c', 8));
  d.add(d.contact(260, 72, 'E', { len: 24, wd: 12, gold: true }), d.contact(260, 132, 'E', { len: 24, wd: 12, gold: true }));
  d.add(d.pin('POS', 260, 72, 'E', 250, 56, { f: INK, sz: 20, a: 'middle' }));
  d.add(d.pin('NEG', 260, 132, 'E', 250, 160, { f: INK, sz: 20, a: 'middle' }));
}

// ---------------------------------------------------------------- motor simulator
function sim(d: Dwg): void {
  const A = [150, 62], B = [62, 196], C = [238, 196];
  // No colour bands: the visitor chooses the resistance on the page, and the drawing names no value.
  const o = { len: 78, wd: 20, colour: '#4f93c8' };
  d.add(d.axial(A[0], A[1], B[0], B[1], o), d.axial(B[0], B[1], C[0], C[1], o), d.axial(C[0], C[1], A[0], A[1], o));
  const stub = (p: number[], q: number[]): string => d.bare(p[0]!, p[1]!, q[0]!, q[1]!);
  const eA = [150, 28], eB = [30, 214], eC = [270, 214];
  d.add(stub(A, eA), stub(B, eB), stub(C, eC));
  d.add(d.solder(A[0], A[1], 7), d.solder(B[0], B[1], 7), d.solder(C[0], C[1], 7));
  d.add(d.eye(eA[0], eA[1], 7), d.eye(eB[0], eB[1], 7), d.eye(eC[0], eC[1], 7));
  d.add(d.pin('A', eA[0], eA[1], 'N', 166, 32, { f: INK }));
  d.add(d.pin('B', eB[0], eB[1], 'W', 20, 244, { f: INK }));
  d.add(d.pin('C', eC[0], eC[1], 'E', 280, 244, { f: INK, a: 'end' }));
}

// ---------------------------------------------------------------- T-plug
function usbtap(d: Dwg): void {
  d.add(`<g filter="${d.u('sh')}"><path d="M44 30H216Q222 30 222 36V112Q222 118 216 118H178V204Q178 210 172 210H88Q82 210 82 204V118H44Q38 118 38 112V36Q38 30 44 30Z" fill="${d.pcb('black')}"/></g>`);
  // The 2 USB-C sockets. A data file that lists them as the pins IN and OUT gets a wire point at the mouth of
  // each socket and its label; a data file without them gets the print of the part.
  // The label of a pin names the socket in words and is wider than a socket: it stands above the board, IN from the
  // left edge and OUT up to the right edge.
  const sockets: Array<[string, number, number, 'W' | 'E', number, 'start' | 'end']> = [['IN', 80, 28, 'W', 8, 'start'], ['OUT', 180, 232, 'E', 252, 'end']];
  for (const [id, lx, ax, side, px, anchor] of sockets) {
    d.add(d.has(id) ? d.pin(id, ax, 80, side, px, 20, { sz: 11, a: anchor, f: INK }) : d.text(lx, 46, id, { sz: 11, a: 'middle' }));
  }
  d.add(d.usbc(28, 52, 92, 56, 'W'), d.usbc(140, 52, 92, 56, 'E'));
  d.add(d.smd(130, 44, 'r', 0, 0.7));
  d.add(d.usbc(96, 124, 68, 82, 'S'));
  // plug with its breakout board
  d.add(`<g filter="${d.u('s3')}"><rect x="102" y="198" width="56" height="50" rx="6" fill="${d.u('tin')}"/><rect x="98" y="240" width="64" height="12" rx="3" fill="#17181c"/></g><rect x="118" y="210" width="24" height="8" rx="2" fill="#7d848c"/>`);
  d.add(`<g filter="${d.u('s3')}"><rect x="76" y="250" width="108" height="56" rx="3" fill="${d.pcb('green')}"/></g>`);
  d.add(`<path d="M112 252V258M118 252V258M124 252V258M130 252V258M136 252V258M142 252V258M148 252V258" stroke="${SILK}" stroke-opacity=".6" stroke-width="1.5"/>`);
  const xs = [91, 117, 143, 169];
  ['G', 'DP', 'DM', 'V'].forEach((id, i) => {
    d.add(d.tin(xs[i], 291, 15, 20, 2));
    d.add(d.pin(id, xs[i], 298, 'S', xs[i], 274, { sz: 12, a: 'middle' }));
  });
}

// ---------------------------------------------------------------- INA228, MATEK
function ina228Matek(d: Dwg): void {
  d.add(d.board(34, 24, 272, 214, 'blue', 5));
  const big = (x: number): string => {
    let v = '';
    for (let i = 0; i < 5; i++) for (let j = 0; j < 6; j++) v += `<circle cx="${x + 9 + i * 10.5}" cy="${42 + j * 11}" r="1.8" fill="#8e959d"/>`;
    return `<rect x="${x}" y="32" width="60" height="74" rx="4" fill="${d.u('tin')}"/>${v}`;
  };
  d.add(big(42), big(238));
  d.add(`<g filter="${d.u('s3')}"><rect x="96" y="30" width="148" height="78" rx="3" fill="${d.u('copper')}"/></g><rect x="138" y="30" width="64" height="78" fill="#a8672f" opacity=".55"/><path d="M138 30V108M202 30V108" stroke="#7d4a20" stroke-width="1.5"/>`);
  d.add(d.pin('ESC', 42, 69, 'W', 72, 124, { sz: 13, a: 'middle' }));
  d.add(d.pin('BAT', 298, 69, 'E', 268, 124, { sz: 13, a: 'middle' }));
  d.add(`<g fill="none" stroke="#2a5aa6" stroke-width="3" stroke-linecap="round"><path d="M112 152H150M112 170H132M112 188H132M112 206H150M190 150V132M214 150V132"/></g>`);
  const jst = (x: number): string => `<g filter="${d.u('s3')}"><rect x="${x}" y="138" width="40" height="84" rx="2" fill="#f4f1e8"/></g><rect x="${x + 5}" y="142" width="30" height="76" fill="#dedacd"/>`;
  d.add(jst(22), jst(278));
  const ys = [153, 171, 189, 207];
  ['V5', 'SCL', 'SDA', 'G'].forEach((id, i) => {
    d.add(`<rect x="34" y="${ys[i] - 4}" width="20" height="8" rx="1" fill="${d.u('gold')}" stroke="#6b5a1e" stroke-width=".6"/>`);
    d.add(d.pin(id, 30, ys[i], 'W', 70, ys[i] + 4.5, { sz: 13 }));
    d.add(`<rect x="290" y="${ys[i] - 2.5}" width="16" height="5" rx="1" fill="${d.u('tin')}"/>`);
  });
  d.add(d.hole(154, 190, 11));
  d.add(d.chip(214, 166, 26, 24, { n: 5, leg: 3, text: ['INA', '228'], tsz: 6 }));
  d.add(d.smd(178, 140, 'r', 0, 0.8), d.smd(240, 140, 'c', 0, 0.8), d.chip(138, 138, 14, 10, { n: 3, leg: 3 }));
  d.add(d.smd(196, 204, 'r', 90, 0.7), d.smd(212, 204, 'r', 90, 0.7), d.smd(228, 204, 'r', 90, 0.7), d.smd(244, 204, 'c', 90, 0.7));
  d.add(d.tin(206, 226, 8, 7), d.tin(215, 226, 8, 7), d.tin(240, 226, 8, 7), d.tin(249, 226, 8, 7));
}

// ---------------------------------------------------------------- INA228, XT90 module
function ina228Xt90(d: Dwg): void {
  d.add(`<g filter="${d.u('sh')}"><rect x="60" y="14" width="300" height="252" rx="12" fill="#17181c"/></g>`);
  d.add(`<rect x="70" y="24" width="280" height="232" rx="6" fill="${d.pcb('blue')}"/>`);
  d.add(d.hole(90, 48, 6, '#8e959d'), d.hole(330, 48, 6, '#8e959d'), d.hole(90, 232, 6, '#8e959d'), d.hole(330, 232, 6, '#8e959d'));
  // unpopulated regulator area and the GND and 5V pads of the board
  d.add(d.tin(150, 44, 9, 14), d.tin(166, 44, 9, 14), d.tin(196, 52, 16, 16), d.tin(224, 44, 12, 10), d.tin(242, 44, 12, 10), d.tin(224, 60, 12, 10), d.tin(242, 60, 12, 10));
  d.add(d.th(300, 44, 5, { dim: true, under: '#8e959d' }), d.th(300, 62, 5, { dim: true, under: '#8e959d' }));
  for (let i = 0; i < 11; i++) d.add(`<rect x="${138 + i * 14}" y="80" width="9" height="4" rx="2" fill="${CARD}" transform="rotate(-40 ${142 + i * 14} 82)"/>`);
  // solder bars and shunt
  d.add(`<rect x="126" y="96" width="168" height="24" rx="6" fill="${d.grad('sol', ['#f6f7f8', '#b9bec5', '#8e959d', '#d7dbe0'], 'v')}" filter="${d.u('s2')}"/>`);
  d.add(`<rect x="126" y="150" width="56" height="24" rx="6" fill="${d.u('sol')}" filter="${d.u('s2')}"/><rect x="238" y="150" width="56" height="24" rx="6" fill="${d.u('sol')}" filter="${d.u('s2')}"/>`);
  d.add(`<g filter="${d.u('s3')}"><rect x="176" y="142" width="68" height="40" rx="2" fill="${d.u('copper')}"/></g><rect x="196" y="142" width="28" height="40" fill="#a8672f" opacity=".55"/>`);
  const xt = (x: number, flip: boolean): string => {
    const body = `<g filter="${d.u('s3')}"><rect x="${x}" y="88" width="118" height="94" rx="5" fill="${d.grad('xt', ['#f7cf4a', '#e9b520', '#c79412'])}"/></g><rect x="${flip ? x + 62 : x + 6}" y="96" width="50" height="78" rx="3" fill="#d8a516" opacity=".6"/>`;
    const pins = flip
      ? `<rect x="${x - 10}" y="100" width="22" height="16" rx="8" fill="${d.u('gold')}"/><rect x="${x - 10}" y="154" width="22" height="16" rx="8" fill="${d.u('gold')}"/>`
      : `<rect x="${x + 106}" y="100" width="22" height="16" rx="8" fill="${d.u('gold')}"/><rect x="${x + 106}" y="154" width="22" height="16" rx="8" fill="${d.u('gold')}"/>`;
    return pins + body;
  };
  d.add(xt(12, false), xt(290, true));
  d.add(d.pin('BAT', 12, 108, 'W', 48, 113, { f: INK, sz: 14, a: 'middle' }));
  d.add(d.pin('ESC', 408, 108, 'E', 372, 113, { f: INK, sz: 14, a: 'middle' }));
  // minus rail: the lower contacts, the shunt sits in it; the board marks plus and minus at the battery side
  d.add(d.pin('BATN', 12, 162, 'W', 48, 167, { f: INK, sz: 14, a: 'middle' }));
  d.add(d.pin('ESCN', 408, 162, 'E', 372, 167, { f: INK, sz: 14, a: 'middle' }));
  d.add(`<circle cx="140" cy="88" r="5" fill="none" stroke="${SILK}" stroke-width="1"/>`, d.text(140, 91.2, '+', { sz: 9, a: 'middle' }));
  d.add(`<circle cx="140" cy="190" r="5" fill="none" stroke="${SILK}" stroke-width="1"/>`, d.text(140, 193.2, '−', { sz: 9, a: 'middle' }));
  // sensor chip and header with the print of the board: L A V G 5
  d.add(d.chip(134, 218, 24, 22, { n: 5, leg: 3 }), d.smd(110, 206, 'r', 90, 0.7), d.smd(110, 228, 'c', 90, 0.7), d.smd(158, 206, 'r', 90, 0.7), d.smd(158, 228, 'c', 90, 0.7));
  const xs = [184, 214, 244, 274, 304];
  const print = ['L', 'A', 'V', 'G', '5'];
  // L = SCL, A = SDA, V = supply of the chip, G = ground. Hole 5 is the 5 V output of the
  // regulator that is not fitted: no pin, no wire.
  const ids = ['SCL', 'SDA', 'V3', 'G', null];
  d.add(`<rect x="170" y="213" width="148" height="22" rx="1" fill="none" stroke="${SILK}" stroke-width="1.2"/>`);
  xs.forEach((x, i) => {
    d.add(d.th(x, 224, 7.5, { dim: i === 4, under: '#8e959d' }));
    d.add(`<circle cx="${x}" cy="244" r="6" fill="none" stroke="${SILK}" stroke-width="1"/>`, d.text(x, 247.2, print[i], { sz: 9, a: 'middle' }));
    const id = ids[i];
    if (id) d.add(d.pin(id, x, 224, 'S', x, 207, { sz: 12, a: 'middle' }));
  });
}

// ---------------------------------------------------------------- phase tap
function tap(d: Dwg): void {
  const bands: Array<[number, string]> = [[0.2, '#e2c21f'], [0.34, '#7d3fb0'], [0.48, '#c8261d'], [0.8, '#c9a54a']];
  d.add(d.axial(42, 66, 258, 66, { len: 84, wd: 24, colour: '#d9c39a', bands }));
  d.add(d.eye(42, 66, 7), d.eye(258, 66, 7));
  d.add(d.pin('IN', 42, 66, 'W', 22, 40, { f: INK, sz: 12.5 }));
  d.add(d.pin('OUT', 258, 66, 'E', 278, 104, { f: INK, sz: 12.5, a: 'end' }));
  // The zener diode of the phase tap is a wire of the data file (kind 'component')
  // between 2 coprocessor pads. It is not part of this drawing.
}

// ---------------------------------------------------------------- AS5600
function as5600(d: Dwg): void {
  d.add(d.board(22, 22, 236, 236, 'white', 14));
  d.add(d.hole(48, 48, 9), d.hole(232, 48, 9), d.hole(48, 232, 9), d.hole(232, 232, 9));
  d.add(`<g fill="none" stroke="#cfcdc4" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M106 66V104H126M140 66V118M174 66V104H154M89 214V176H126M123 214V190L133 162M157 214V190L147 162M191 214V176H154"/></g>`);
  const dark = '#2a2c31';
  d.add(`<rect x="88" y="53" width="104" height="26" fill="none" stroke="${dark}" stroke-width="1.3"/>`);
  ([['GND', 106], ['OUT', 140], ['VCC', 174]] as Array<[string, number]>).forEach(([id, x]) => {
    d.add(d.th(x, 66, 9, { sq: id === 'VCC' }));
    d.add(d.pin(id, x, 66, 'N', x, 45, { f: dark, sz: 12.5, a: 'middle' }));
  });
  d.add(`<rect x="71" y="201" width="138" height="26" fill="none" stroke="${dark}" stroke-width="1.3"/>`);
  ([['GPO', 89], ['SDA', 123], ['SCL', 157], ['DIR', 191]] as Array<[string, number]>).forEach(([id, x]) => {
    d.add(d.th(x, 214, 9, { sq: id === 'DIR' }));
    d.add(d.pin(id, x, 214, 'S', x, 245, { f: dark, sz: 12.5, a: 'middle' }));
  });
  d.add(d.chip(140, 140, 32, 30, { n: 4, rot: 90, text: ['AS5600'], tsz: 6.5 }));
  d.add(d.smd(72, 112, 'r'), d.smd(72, 140, 'r'), d.smd(72, 168, 'r'), d.smd(208, 112, 'c'), d.smd(208, 140, 'c'), d.smd(208, 168, 'r'));
  // diametric magnet, lying next to the board
  d.add(`<g filter="${d.u('s3')}"><circle cx="292" cy="220" r="17" fill="${d.u('ring')}"/></g><path d="M292 203A17 17 0 0 1 292 237Z" fill="#8e959d" opacity=".55"/><path d="M292 203V237" stroke="#6d737b" stroke-width="1"/>`);
}

// ---------------------------------------------------------------- pull-ups
function pull(d: Dwg): void {
  const bands: Array<[number, string]> = [[0.2, '#c8261d'], [0.34, '#c8261d'], [0.48, '#c8261d'], [0.8, '#c9a54a']];
  const o = { len: 70, wd: 20, colour: '#d9c39a', bands };
  const J = [236, 96];
  d.add(d.axial(42, 58, J[0], J[1], o), d.axial(42, 134, J[0], J[1], o));
  d.add(d.bare(J[0]!, J[1]!, 276, 96));
  d.add(d.solder(J[0], J[1], 7));
  d.add(d.eye(42, 58, 7), d.eye(42, 134, 7), d.eye(276, 96, 7));
  d.add(d.pin('SDA', 42, 58, 'W', 22, 34, { f: INK, sz: 12.5 }));
  d.add(d.pin('SCL', 42, 134, 'W', 22, 168, { f: INK, sz: 12.5 }));
  d.add(d.pin('V', 276, 96, 'E', 290, 140, { f: INK, sz: 12.5, a: 'end' }));
}

// ---------------------------------------------------------------- USB-C cables
function usb(d: Dwg): void {
  const cable = (cx: number, cy: number, r: number, col: string): string => {
    const p1 = `M${cx - r} ${cy}A${r} ${r * 0.72} 0 1 1 ${cx + r} ${cy}A${r} ${r * 0.72} 0 1 1 ${cx - r} ${cy}`;
    const r2 = r - 10;
    const p2 = `M${cx - r2} ${cy}A${r2} ${r2 * 0.72} 0 1 1 ${cx + r2} ${cy}A${r2} ${r2 * 0.72} 0 1 1 ${cx - r2} ${cy}`;
    const plug = (x: number, y: number, rot: number): string => `<g transform="translate(${x},${y}) rotate(${rot})" filter="${d.u('s3')}"><rect x="-11" y="-16" width="22" height="30" rx="4" fill="${col}"/><rect x="-8" y="-28" width="16" height="14" rx="4" fill="${d.u('tin')}"/></g>`;
    return d.lead(p1, col, 6) + d.lead(p2, col, 6) + d.lead(`M${cx - r} ${cy}C${cx - r} ${cy + r} ${cx - r * 0.6} ${cy + r * 0.9} ${cx - r * 0.5} ${cy + r * 1.05}`, col, 6)
      + d.lead(`M${cx + r2} ${cy}C${cx + r2} ${cy + r} ${cx + r * 0.5} ${cy + r * 0.9} ${cx + r * 0.45} ${cy + r * 1.05}`, col, 6)
      + plug(cx - r * 0.5, cy + r * 1.05 + 12, 180) + plug(cx + r * 0.45, cy + r * 1.05 + 12, 180);
  };
  d.add(cable(84, 78, 54, '#2a2d33'), cable(206, 84, 48, '#e9e6de'));
}

// ---------------------------------------------------------------- hook-up wire, multimeter
function wire(d: Dwg): void {
  const cols = ['#c8261d', '#17181c', '#e2c21f', '#2f7fd0', '#2f9a57'];
  cols.forEach((c, i) => {
    const y = 40 + i * 26;
    d.add(d.lead(`M22 ${y}C60 ${y - 16} 90 ${y + 16} 132 ${y}`, c, 4.5));
    d.add(d.contact(22, y, 'E', { len: 12, wd: 3 }), d.contact(132, y, 'W', { len: 12, wd: 3 }));
  });
  // multimeter
  d.add(`<g filter="${d.u('sh')}"><rect x="170" y="22" width="96" height="156" rx="12" fill="${d.grad('mm', ['#f0c53a', '#dba51c', '#b98512'])}"/></g><rect x="180" y="32" width="76" height="136" rx="6" fill="#2a2c31"/>`);
  d.add(`<rect x="188" y="40" width="60" height="32" rx="2" fill="${d.grad('lcd', ['#c9d4c0', '#aab8a2'])}"/><rect x="214" y="50" width="28" height="12" rx="2" fill="#56624f" opacity=".7"/>`);
  d.add(`<circle cx="218" cy="110" r="24" fill="#17181c" stroke="#4a4e56" stroke-width="2"/><rect x="215" y="88" width="6" height="26" rx="3" fill="#e9e6de"/>`);
  d.add(`<circle cx="198" cy="154" r="5" fill="#17181c" stroke="#6d737b"/><circle cx="218" cy="154" r="5" fill="#17181c" stroke="#6d737b"/><circle cx="238" cy="154" r="5" fill="#c8261d" stroke="#6d737b"/>`);
}

// ---------------------------------------------------------------- INA3221
// Front view after the owner's photos. The front carries no pin print at the
// header and at the channel pads; the names are the print on the back of the
// board, mirrored to the front.
const INA3221_HEADER = ['VS', 'GND', 'SCL', 'SDA', 'PV', 'CRI', 'WAR', 'TC'];
const INA3221_NATIVE = Object.fromEntries([
  ...INA3221_HEADER.map((n) => [n, n]),
  ...['VIN3+', 'VIN3-', 'VIN2+', 'VIN2-', 'VIN1+', 'VIN1-'].map((n) => [n, n]),
  ['PAD1', 'GND'], ['PAD2', 'GND'], ['PAD3', 'VPU'],
]);
function ina3221(d: Dwg): void {
  const k = 0.42;
  const X = (p: number): number => Math.round((22 + p * k) * 10) / 10;
  const Y = (p: number): number => Math.round((40 + p * k) * 10) / 10;
  d.add(d.board(22, 40, 437, 361, 'black', 5));
  const tr: string[] = [];
  const ch: Array<[string, number, number, number, number, number, number]> = [['3', 45, 320, 115, 250, 280, 0], ['2', 370, 645, 430, 575, 505, 0], ['1', 695, 980, 765, 910, 735, 180]];
  for (const [, , , pa, pb, sx] of ch) {
    tr.push(`M${X(pa)} ${Y(140)}L${X(sx - 75)} ${Y(250)}V${Y(310)}M${X(pb)} ${Y(140)}L${X(sx + 75)} ${Y(250)}V${Y(310)}`);
    tr.push(`M${X(sx - 55)} ${Y(390)}V${Y(420)}L${X(sx - 14)} ${Y(440)}M${X(sx + 55)} ${Y(390)}V${Y(420)}L${X(sx + 14)} ${Y(440)}`);
  }
  tr.push(`M${X(280)} ${Y(450)}L${X(330)} ${Y(500)}H${X(450)}L${X(520)} ${Y(560)}M${X(505)} ${Y(450)}V${Y(490)}H${X(560)}V${Y(520)}M${X(735)} ${Y(450)}V${Y(520)}L${X(660)} ${Y(560)}`);
  tr.push(`M${X(650)} ${Y(600)}H${X(760)}L${X(880)} ${Y(515)}H${X(960)}M${X(650)} ${Y(625)}H${X(780)}L${X(860)} ${Y(587)}H${X(960)}M${X(620)} ${Y(650)}V${Y(680)}H${X(890)}L${X(930)} ${Y(655)}H${X(960)}`);
  tr.push(`M${X(410)} ${Y(542)}H${X(520)}M${X(410)} ${Y(585)}H${X(520)}M${X(410)} ${Y(625)}H${X(520)}`);
  d.add(`<g fill="none" stroke="#2c2f35" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="${tr.join('')}"/></g>`);
  for (const [n, x1, x2, pa, pb, sx, rot] of ch) {
    d.add(`<rect x="${X(x1)}" y="${Y(25)}" width="${Math.round((x2 - x1) * k)}" height="${Math.round(140 * k)}" fill="none" stroke="${SILK}" stroke-width="1.6"/>`);
    d.add(d.th(X(pa), Y(95), 19), d.th(X(pb), Y(95), 19));
    d.add(d.pin(`VIN${n}+`, X(pa), Y(95), 'N', X(pa), 31, { f: INK, sz: 12.5, a: 'middle' }));
    d.add(d.pin(`VIN${n}-`, X(pb), Y(95), 'N', X(pb), 31, { f: INK, sz: 12.5, a: 'middle' }));
    const cx = X((x1 + x2) / 2 - 8);
    d.add(`<rect x="${cx - 32}" y="${Y(195)}" width="64" height="29" fill="${SILK}"/>`, d.text(cx, Y(195) + 8.5, `CH${n}`, { sz: 17, a: 'middle', f: '#15161a', rot: 180 }));
    d.add(d.shunt(X(sx), Y(310), 78, 36, 'R100', rot));
    d.add(d.smd(X(sx - 55), Y(390), 'r', 0, 0.9), d.smd(X(sx + 55), Y(390), 'r', 0, 0.9), d.smd(X(sx), Y(440), 'c', 0, 0.9));
  }
  d.add(d.hole(X(85), Y(305), 12), d.hole(X(955), Y(290), 12), d.hole(X(965), Y(790), 12));
  // 3 large pads on the left edge, print GND GND VPU
  d.add(`<rect x="${X(30)}" y="${Y(430)}" width="59" height="172" fill="none" stroke="${SILK}" stroke-width="1.6"/>`);
  ([['PAD1', 490], ['PAD2', 635], ['PAD3', 770]] as Array<[string, number]>).forEach(([id, py]) => {
    d.add(d.th(X(95), Y(py), 19));
    d.add(d.pin(id, X(95), Y(py), 'W', X(205), Y(py), { sz: 12.5, a: 'middle', rot: -90 }));
  });
  // status LEDs with their print and series resistors
  ([['TC', 542], ['W', 585], ['C', 625], ['PV', 667], ['VS', 710]] as Array<[string, number]>).forEach(([s, py]) => {
    d.add(d.smd(X(262), Y(py), 'led', 0, 0.85), d.smd(X(368), Y(py), 'r', 0, 0.85));
    d.add(d.text(X(318), Y(py), s, { sz: 7, a: 'middle', rot: -90 }));
  });
  d.add(d.chip(X(585), Y(580), 46, 46, { n: 4, sides: 4, leg: 3, text: ['INA', '3221'], tsz: 7 }));
  ([[433, 'r'], [477, 'r'], [522, 'c'], [567, 'c'], [655, 'r'], [700, 'r'], [745, 'r'], [793, 'r']] as Array<[number, 'r' | 'c']>).forEach(([px, kd]) => d.add(d.smd(X(px), Y(735), kd, 90, 0.85)));
  d.add(`<circle cx="${X(610)}" cy="${Y(718)}" r="3.4" fill="${d.u('ring')}"/><circle cx="${X(610)}" cy="${Y(752)}" r="3.4" fill="${d.u('ring')}"/>`);
  // header, 8 holes, square hole on the left
  INA3221_HEADER.forEach((id, i) => {
    const x = X(310 + i * 69.3);
    d.add(d.th(x, Y(815), 8.5, { sq: i === 0 }));
    d.add(d.pin(id, x, Y(815), 'S', x, i % 2 ? 434 : 419, { f: INK, sz: 12.5, a: 'middle' }));
  });
  // address bridges between the two A0 marks: solder bridges, no wire points
  d.add(`<path d="M${X(1006)} ${Y(430)}V${Y(672)}" stroke="${SILK}" stroke-width="1"/>`);
  d.add(d.text(X(992), Y(400) - 3, 'A0', { sz: 8, a: 'middle', rot: 180 }), d.text(X(992), Y(700) - 3, 'A0', { sz: 8, a: 'middle', rot: 180 }));
  ([['GND', 448], ['SDA', 515], ['SCL', 587], ['VS', 655]] as Array<[string, number]>).forEach(([s, py]) => {
    d.add(d.tin(X(978), Y(py), 4.5, 4.5, 0.5), d.tin(X(1002), Y(py), 4.5, 4.5, 0.5));
    d.add(d.text(X(925), Y(py) - 3, s, { sz: 8, a: 'middle', rot: 180 }));
  });
}

// ---------------------------------------------------------------- INA219
// Front view after the owner's photo, turned so that the header is at the bottom.
const INA219_NATIVE = { Vcc: 'Vcc', Gnd: 'Gnd', Scl: 'Scl', Sda: 'Sda', 'Vin-': 'Vin-', 'Vin+': 'Vin+', 'T.Vin-': 'Vin-', 'T.Vin+': 'Vin+' };
function ina219(d: Dwg): void {
  const k = 0.42;
  const X = (p: number): number => Math.round((15 + p * k) * 10) / 10;
  const Y = (p: number): number => Math.round((18 + p * k) * 10) / 10;
  d.add(d.board(15, 18, 270, 246, 'teal', 8));
  d.add(d.hole(X(45), Y(70), 12), d.hole(X(588), Y(75), 12), d.hole(X(53), Y(535), 12), d.hole(X(588), Y(528), 12));
  const tc = '#3a9bc9';
  d.add(`<g fill="none" stroke="${tc}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M${X(167)} ${Y(505)}V${Y(470)}L${X(120)} ${Y(440)}H${X(60)}V${Y(330)}H${X(250)}M${X(232)} ${Y(505)}V${Y(455)}H${X(160)}V${Y(420)}M${X(300)} ${Y(505)}V${Y(440)}L${X(312)} ${Y(415)}M${X(368)} ${Y(505)}L${X(330)} ${Y(460)}V${Y(415)}M${X(350)} ${Y(415)}L${X(380)} ${Y(455)}H${X(590)}V${Y(300)}M${X(395)} ${Y(360)}H${X(420)}V${Y(300)}H${X(540)}M${X(290)} ${Y(345)}L${X(275)} ${Y(290)}M${X(350)} ${Y(345)}L${X(372)} ${Y(290)}"/></g>`);
  d.add(`<g fill="none" stroke="${tc}" stroke-width="7" stroke-linejoin="round"><path d="M${X(275)} ${Y(105)}V${Y(190)}H${X(255)}M${X(363)} ${Y(105)}V${Y(190)}H${X(395)}"/></g>`);
  // 2-pad terminal with the shunt below
  d.add(`<path d="M${X(225)} ${Y(40)}V${Y(185)}H${X(410)}V${Y(40)}Z" fill="none" stroke="${SILK}" stroke-width="1.3"/>`);
  d.add(d.th(X(275), Y(92), 10), d.th(X(363), Y(92), 10));
  d.add(d.pin('T.Vin-', X(275), Y(92), 'N', X(214), Y(102), { sz: 12, a: 'end' }));
  d.add(d.pin('T.Vin+', X(363), Y(92), 'N', X(422), Y(102), { sz: 12 }));
  d.add(d.shunt(X(325), Y(235), 82, 34, 'R100', 180));
  d.add(d.text(X(322), Y(318), 'R5', { sz: 7, a: 'middle' }));
  ([['INA219 DC', 172], ['Current', 222], ['Sensor', 270]] as Array<[string, number]>).forEach(([s, py]) => d.add(d.text(X(22), Y(py), s, { sz: 9, w: 500 })));
  // sensor chip and passives
  d.add(d.chip(X(320), Y(380), 26, 34, { n: 4, rot: 90, leg: 3.5 }));
  ([[90, 'c'], [160, 'r'], [222, 'r']] as Array<[number, 'r' | 'c']>).forEach(([px, kd]) => {
    d.add(`<rect x="${X(px) - 9}" y="${Y(373) - 15}" width="18" height="30" fill="none" stroke="${SILK}" stroke-width="1"/>`, d.smd(X(px), Y(373), kd, 90, 1.1));
  });
  d.add(d.text(X(515), Y(302), 'I2C Address', { sz: 7.5, a: 'middle', w: 500 }));
  ([['A1', 347], ['A0', 413]] as Array<[string, number]>).forEach(([s, py]) => {
    d.add(`<rect x="${X(465) - 15}" y="${Y(py) - 9}" width="30" height="18" fill="none" stroke="${SILK}" stroke-width="1"/>`, d.smd(X(465), Y(py), 'r', 0, 1.1));
    d.add(`<rect x="${X(577) - 15}" y="${Y(py) - 9}" width="30" height="18" rx="3" fill="none" stroke="${SILK}" stroke-width="1"/>`, d.tin(X(563), Y(py), 8, 11), d.tin(X(591), Y(py), 8, 11));
    d.add(d.text(X(622), Y(py), s, { sz: 7, a: 'middle', rot: -90 }));
  });
  // header, 6 holes, print alternating below and above the holes as on the board
  ['Vcc', 'Gnd', 'Scl', 'Sda', 'Vin-', 'Vin+'].forEach((id, i) => {
    const x = X(167 + i * 67);
    d.add(d.th(x, Y(528), 8));
    d.add(d.pin(id, x, Y(528), 'S', x, i % 2 ? Y(528) - 13 : Y(528) + 21, { sz: 12, a: 'middle' }));
  });
}

// ---------------------------------------------------------------- ADXL345
const ADXL345_HEADER = ['GND', 'VCC', 'CS', 'INT1', 'INT2', 'SDO', 'SDA', 'SCL'];
const ADXL345_NATIVE = Object.fromEntries(ADXL345_HEADER.map((n) => [n, n]));
function adxl345(d: Dwg): void {
  const ox = 18, oy = 15;
  d.add(d.board(ox, oy, 225, 280, 'indigo', 4));
  d.add(d.hole(ox + 187, oy + 32, 15), d.hole(ox + 187, oy + 245, 15));
  const tc = '#5a7fdc';
  d.add(`<g fill="none" stroke="${tc}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" transform="translate(${ox},${oy})"><path d="M125 102V70H112M150 108V86M150 168V190M96 122H84V87M96 148H84V217H96M96 160H76V239H96"/></g>`);
  ADXL345_HEADER.forEach((id, i) => {
    const y = oy + 17.5 + 35 * i;
    d.add(d.th(ox + 27, y, 9.5));
    d.add(d.pin(id, ox + 27, y, 'W', ox + 43, y + 4.5, { sz: 12.5 }));
  });
  d.add(d.chip(ox + 117, oy + 26, 16, 26, { n: 3, leg: 4 }));
  d.add(d.smd(ox + 105, oy + 62, 'c'), d.smd(ox + 105, oy + 85, 'r'), d.smd(ox + 167, oy + 86, 'c'), d.smd(ox + 167, oy + 191, 'c'));
  d.add(d.chip(ox + 125, oy + 137, 44, 64, { n: 7, leg: 3, tsz: 6 }));
  d.add(`<g filter="${d.u('s2')}"><rect x="${ox + 170}" y="${oy + 118}" width="18" height="38" rx="2" fill="${d.u('tin')}"/><rect x="${ox + 170}" y="${oy + 123}" width="18" height="28" fill="#d9b25a"/><rect x="${ox + 170}" y="${oy + 123}" width="18" height="5" fill="#a67c1f"/></g>`);
  d.add(d.smd(ox + 110, oy + 195, 'r'), d.smd(ox + 110, oy + 217, 'r'), d.smd(ox + 110, oy + 239, 'r'));
  d.add(d.text(ox + 212, oy + 140, 'ADXL345', { sz: 13, a: 'middle', rot: -90 }));
  // axis print
  const ax = ox + 144, ay = oy + 219;
  d.add(`<g fill="none" stroke="${SILK}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M${ax} ${ay}H${ax + 26}M${ax + 20} ${ay - 4}L${ax + 26} ${ay}L${ax + 20} ${ay + 4}M${ax} ${ay}V${ay + 24}M${ax - 4} ${ay + 18}L${ax} ${ay + 24}L${ax + 4} ${ay + 18}"/></g>`);
  d.add(d.text(ax + 31, ay + 4, 'Y', { sz: 11 }), d.text(ax, ay + 38, 'X', { sz: 11, a: 'middle' }), d.text(ax - 6, ay - 6, 'Z', { sz: 11, a: 'middle' }));
}

// ---------------------------------------------------------------- the list
const T = (en: string, de: string): Text => ({ en, de });

/** One drawing: 2 files, `<file>.svg` in English and `de/<file>.svg` in German. */
export type Entry = {
  file: string;
  /** Part id of the catalogue. null: the part is not in the catalogue and no wire ends on it. */
  part: string | null;
  /** Option value the drawing shows, for a part with 2 boards. */
  variant?: string;
  /** Prefix of every id in the file. */
  pfx: string;
  w: number;
  h: number;
  draw: (d: Dwg) => void;
  /** Pin id to the print of the board. Set where the drawing carries more points than the catalogue names. */
  native?: Record<string, string>;
  /** Pads carry the print of the breakout board. */
  breakout?: boolean;
  /**
   * Title of a drawing whose part is not in the catalogue, or of a drawing
   * that shows no value of its part where the name of the part states values.
   */
  name?: Text;
  desc: Text;
};

export const LIST: Entry[] = [
  { file: 'ina3221', part: 'ina3221', pfx: 'ina', w: 480, h: 444, draw: ina3221, native: INA3221_NATIVE,
    desc: T('Illustration of the INA3221 board, front: 3 channel pad pairs at the top, 3 large pads GND, GND, VPU at the left, 8-hole header VS, GND, SCL, SDA, PV, CRI, WAR, TC at the bottom.', 'Illustration der INA3221-Platine, Vorderseite: 3 Kanal-Padpaare oben, 3 große Pads GND, GND, VPU links, 8-polige Leiste VS, GND, SCL, SDA, PV, CRI, WAR, TC unten.') },
  { file: 'ina219', part: null, pfx: 'i219', w: 300, h: 284, draw: ina219, native: INA219_NATIVE,
    name: T('INA219 board, not wired by this release', 'INA219-Platine, in diesem Release nicht verdrahtet'),
    desc: T('Illustration of the INA219 board, front: 2-pad terminal Vin- and Vin+ at the top, 6-hole header Vcc, Gnd, Scl, Sda, Vin-, Vin+ at the bottom.', 'Illustration der INA219-Platine, Vorderseite: 2-Pad-Anschluss Vin- und Vin+ oben, 6-polige Leiste Vcc, Gnd, Scl, Sda, Vin-, Vin+ unten.') },
  { file: 'adxl345', part: null, pfx: 'adx', w: 262, h: 310, draw: adxl345, native: ADXL345_NATIVE,
    name: T('ADXL345 board, not wired by this release', 'ADXL345-Platine, in diesem Release nicht verdrahtet'),
    desc: T('Illustration of the ADXL345 board, front: 8-hole header GND, VCC, CS, INT1, INT2, SDO, SDA, SCL at the left edge.', 'Illustration der ADXL345-Platine, Vorderseite: 8-polige Leiste GND, VCC, CS, INT1, INT2, SDO, SDA, SCL an der linken Kante.') },
  { file: 'panel', part: 'panel', pfx: 'pnl', w: 380, h: 290, draw: panel,
    desc: T('Illustration of the back of the panel: connector J8 with GND and GPIO6, CAN connector with CAN H and CAN L.', 'Illustration der Rückseite des Panels: Stecker J8 mit GND und GPIO6, CAN-Stecker mit CAN H und CAN L.') },
  { file: 'copro', part: 'copro', pfx: 'cop', w: 340, h: 790, draw: copro,
    desc: T('Illustration of the RP2350-CAN board: pads 1 to 20 down the left edge, pads 40 to 21 down the right edge, CAN screw terminal at the bottom.', 'Illustration der RP2350-CAN-Platine: Pads 1 bis 20 an der linken Kante abwärts, Pads 40 bis 21 an der rechten Kante abwärts, CAN-Schraubklemme unten.') },
  { file: 'copro--osoyoo', part: 'copro', variant: 'osoyoo', pfx: 'cos', w: 480, h: 820, draw: coproOsoyoo, breakout: true,
    desc: T('Illustration of the RP2350-CAN board in the Osoyoo breakout board: 20 screw terminals on each long edge, beside each a servo plug point with the pins S, V and G, a jumper for 3.3 V or 5 V on V, CAN screw terminal on the module.', 'Illustration der RP2350-CAN-Platine in der Osoyoo-Breakout-Platine: je 20 Schraubklemmen an den langen Kanten, daneben je ein Servo-Steckplatz mit den Pins S, V und G, ein Jumper für 3,3 V oder 5 V an V, CAN-Schraubklemme am Modul.') },
  { file: 'sd', part: 'sd', pfx: 'sdc', w: 200, h: 200, draw: sd,
    desc: T('Illustration of a microSD card.', 'Illustration einer microSD-Karte.') },
  { file: 'estop', part: 'estop', pfx: 'est', w: 340, h: 200, draw: estop,
    desc: T('Illustration of the emergency stop button from the side: red head, nut, switch body, solder tabs.', 'Illustration des Not-Aus-Tasters von der Seite: roter Kopf, Mutter, Schaltkörper, Lötfahnen.') },
  { file: 'servo', part: 'servo', pfx: 'srv', w: 320, h: 224, draw: servo,
    desc: T('Illustration of a servo with its 3-wire lead: signal, plus, minus.', 'Illustration eines Servos mit 3-adriger Leitung: Signal, Plus, Minus.') },
  { file: 'esc', part: 'esc', pfx: 'esc', w: 370, h: 344, draw: escPart,
    desc: T('Illustration of an ESC with signal lead, supply leads and 3 phase leads, and a motor.', 'Illustration eines ESC mit Signalleitung, Versorgungsleitungen und 3 Phasenleitungen, dazu ein Motor.') },
  { file: 'pdmini', part: 'pdmini', pfx: 'pdm', w: 350, h: 244, draw: pdmini,
    desc: T('Illustration of the PD mini board: USB-C input, 4 buttons, display, 4-pole output terminal.', 'Illustration der PD-mini-Platine: USB-C-Eingang, 4 Tasten, Display, 4-polige Ausgangsklemme.') },
  { file: 'charger', part: 'charger', pfx: 'chg', w: 240, h: 200, draw: charger,
    desc: T('Illustration of a USB-C PD charger.', 'Illustration eines USB-C-PD-Ladegeräts.') },
  { file: 'ssup', part: 'ssup', pfx: 'ssu', w: 230, h: 200, draw: ssup,
    desc: T('Illustration of a servo supply with a plus and a minus terminal.', 'Illustration einer Servoversorgung mit Plus- und Minusklemme.') },
  { file: 'dcsup', part: 'dcsup', pfx: 'dcs', w: 290, h: 200, draw: dcsup,
    desc: T('Illustration of a plug-in DC supply with a plus and a minus lead. No product is chosen.', 'Illustration einer steckbaren DC-Versorgung mit Plus- und Minusleitung. Kein Produkt ist gewählt.') },
  { file: 'batt', part: 'batt', pfx: 'bat', w: 290, h: 200, draw: batt,
    desc: T('Illustration of a battery pack with a plus and a minus lead.', 'Illustration eines Akkupacks mit Plus- und Minusleitung.') },
  // The name of the part states the resistance and the rating a visitor chooses; the drawing shows neither.
  { file: 'sim', part: 'sim', pfx: 'sim', w: 300, h: 264, draw: sim,
    name: T('Motor simulator, self-built: 3 resistors in delta', 'Motorsimulator, selbst gebaut: 3 Widerstände im Dreieck'),
    desc: T('Illustration of the motor simulator: 3 resistors soldered into a triangle with corners A, B and C.', 'Illustration des Motorsimulators: 3 Widerstände zu einem Dreieck gelötet, Ecken A, B und C.') },
  { file: 'usbtap', part: 'usbtap', pfx: 'tpl', w: 260, h: 324, draw: usbtap,
    desc: T('Illustration of the T-plug: USB-C sockets IN and OUT, third socket with a plug board that has the pads G, D+, D− and V.', 'Illustration des T-Steckers: USB-C-Buchsen IN und OUT, dritte Buchse mit Steckerplatine mit den Pads G, D+, D− und V.') },
  { file: 'ina228--matek', part: 'ina228', variant: 'matek', pfx: 'imt', w: 340, h: 262, draw: ina228Matek,
    desc: T('Illustration of the MATEK INA228 board: pads ESC+ and BAT+ beside the shunt, connector with 5V, SCL, SDA and G.', 'Illustration der MATEK-INA228-Platine: Pads ESC+ und BAT+ neben dem Shunt, Stecker mit 5V, SCL, SDA und G.') },
  { file: 'ina228--xt90', part: 'ina228', variant: 'xt90', pfx: 'ixt', w: 420, h: 280, draw: ina228Xt90,
    desc: T('Illustration of the INA228 module with 2 XT90 plugs and a 5-pin header printed L A V G 5.', 'Illustration des INA228-Moduls mit 2 XT90-Steckern und 5-poliger Leiste mit dem Aufdruck L A V G 5.') },
  { file: 'tap', part: 'tap', pfx: 'tap', w: 300, h: 132, draw: tap,
    desc: T('Illustration of the series resistor of the phase tap, with 2 ends.', 'Illustration des Vorwiderstands des Phasenabgriffs, mit 2 Enden.') },
  { file: 'as5600', part: 'as5600', pfx: 'as5', w: 326, h: 284, draw: as5600,
    desc: T('Illustration of the AS5600 board: header GND, OUT, VCC and header GPO, SDA, SCL, DIR, with the magnet beside it.', 'Illustration der AS5600-Platine: Leiste GND, OUT, VCC und Leiste GPO, SDA, SCL, DIR, daneben der Magnet.') },
  { file: 'pull', part: 'pull', pfx: 'pul', w: 310, h: 192, draw: pull,
    desc: T('Illustration of the 2 pull-up resistors R1 and R2, joined at one end.', 'Illustration der 2 Pull-up-Widerstände R1 und R2, an einem Ende verbunden.') },
  { file: 'usb', part: 'usb', pfx: 'usb', w: 280, h: 200, draw: usb,
    desc: T('Illustration of 2 USB-C cables.', 'Illustration von 2 USB-C-Kabeln.') },
  { file: 'wire', part: 'wire', pfx: 'wir', w: 290, h: 200, draw: wire,
    desc: T('Illustration of hook-up wire and a multimeter.', 'Illustration von Schaltdraht und einem Multimeter.') },
];

/**
 * Catalogue pin id of the INA3221 to the pin ids of its drawing. null: the board
 * has no wire point for the pin. C1A is the supply side of the shunt (VIN1+),
 * C1B the load side (VIN1-).
 */
export const INA3221_MAP: Record<string, string[] | null> = {
  A0: null, VS: ['VS'], SCL: ['SCL'], SDA: ['SDA'], GND: ['GND'],
  C1A: ['VIN1+'], C1B: ['VIN1-'], LP: ['PAD1', 'PAD2', 'PAD3'],
};
