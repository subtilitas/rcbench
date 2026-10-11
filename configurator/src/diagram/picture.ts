// The pictorial wiring view as markup: the part drawings side by side on a
// plain ground, each wire from pad to pad in horizontal and vertical runs, in
// its palette colour, with its number at both ends. A wire that carries load
// current is drawn `THICK` px wide, every other wire `THIN` px.

import type { Lang } from '../../contract.ts';
import { MINUS, PALETTE, PLUS, SHEATH } from '../../drawings/wires.ts';
import { esc, t, tx } from '../i18n.ts';
import type { Key } from '../i18n.ts';
import { endName, targetName } from '../names.ts';
import type { View } from '../view/chooser.ts';
import { ANCHORS } from './anchors.ts';
import { CAP, THICK, THIN } from './layout.ts';
import { BODY, LABEL, diagramOf } from './route.ts';
import type { Body, Diagram, Pt, Routed } from './route.ts';
import { sceneOf } from './scene.ts';

/** Folder of the part drawings, relative to the page. German drawings lie in `de/` below it. */
export const DRAWINGS = 'drawings/';
/** Radius of a wire corner in px. */
export const RADIUS = 2;
/** Estimated width of 1 character of a part name, in px. */
const NAME_CHAR = 7.4;

const f = (v: number): string => String(Math.round(v * 10) / 10);
const at = (p: Pt): string => `${f(p[0])} ${f(p[1])}`;

/** Path data of a polyline. A corner between 2 axis-parallel segments is rounded. */
export function pathOf(points: Pt[], radius = RADIUS): string {
  let d = `M${at(points[0]!)}`;
  for (let i = 1; i < points.length; i++) {
    const a = points[i - 1]!;
    const b = points[i]!;
    const c = points[i + 1];
    const axis = (p: Pt, q: Pt): boolean => p[0] === q[0] || p[1] === q[1];
    if (!c || !axis(a, b) || !axis(b, c)) {
      d += `L${at(b)}`;
      continue;
    }
    const l1 = Math.abs(b[0] - a[0]) + Math.abs(b[1] - a[1]);
    const l2 = Math.abs(c[0] - b[0]) + Math.abs(c[1] - b[1]);
    const r = Math.min(radius, l1 / 2, l2 / 2);
    const p: Pt = [b[0] + Math.sign(a[0] - b[0]) * r, b[1] + Math.sign(a[1] - b[1]) * r];
    const q: Pt = [b[0] + Math.sign(c[0] - b[0]) * r, b[1] + Math.sign(c[1] - b[1]) * r];
    d += `L${at(p)}Q${at(b)} ${at(q)}`;
  }
  return d;
}

/** Colour sample of a wire for the table: its core colour, with the casing as outline. Empty for a wire without a colour. */
export function swatch(colour: string | null): string {
  const c = colour === null ? undefined : PALETTE[colour];
  if (!c) return '';
  return '<svg class="sw" width="18" height="10" viewBox="0 0 18 10" aria-hidden="true" focusable="false">'
    + `<rect x="1" y="1" width="16" height="8" rx="2" fill="${c.core}" stroke="${c.casing ?? c.core}" stroke-width="1.5"/></svg>`;
}

function middle(wire: Routed): Pt {
  const path = wire.paths[0]!;
  let best: Pt = path[0]!;
  let longest = -1;
  for (let i = 1; i < path.length; i++) {
    const a = path[i - 1]!;
    const b = path[i]!;
    const length = Math.abs(b[0] - a[0]) + Math.abs(b[1] - a[1]);
    if (length > longest) {
      longest = length;
      best = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    }
  }
  return best;
}

/** Colours of the body of a component and of its band. */
export const BODY_FILL = '#d8643a';
export const BAND_FILL = '#17181c';

/** Body and band of a component as 2 rectangles: `[x, y, width, height]` each. */
export function bodyRects(b: Body): [[number, number, number, number], [number, number, number, number]] {
  const centre = b.band * (b.len / 2 - 2 - BODY.band / 2);
  return b.upright
    ? [[b.x - BODY.w / 2, b.y - b.len / 2, BODY.w, b.len], [b.x - BODY.w / 2, b.y + centre - BODY.band / 2, BODY.w, BODY.band]]
    : [[b.x - b.len / 2, b.y - BODY.w / 2, b.len, BODY.w], [b.x + centre - BODY.band / 2, b.y - BODY.w / 2, BODY.band, BODY.w]];
}

/** Baselines of the lines of a component: `count` lines of `size` px, `pitch` px apart, centred on `y`. */
export const baselines = (y: number, count: number, size: number, pitch: number): number[] =>
  Array.from({ length: count }, (_, i) => y + (i - (count - 1) / 2) * pitch + size * 0.35);

function wireMarkup(wire: Routed, label: string, first: boolean, lang: Lang): string {
  const colour = wire.colour === null ? undefined : PALETTE[wire.colour];
  const stroke = colour ? colour.core : 'currentColor';
  const mid = middle(wire);
  let out = `<g class="dw k-${wire.kind}${wire.thick ? ' thick' : ''}${wire.cable ? ' cable' : ''}" data-wire="${wire.n}" data-c="${f(mid[0])} ${f(mid[1])}" tabindex="${first ? 0 : -1}" role="button" aria-pressed="false" aria-label="${esc(label)}">`
    + `<title>${esc(label)}</title>`;
  for (const path of wire.paths) {
    const d = pathOf(path);
    out += `<path class="hit" d="${d}"/>`;
    if (colour?.casing) out += `<path class="case" d="${d}" stroke="${colour.casing}"/>`;
    if (wire.cable) out += `<path class="sheath" d="${d}" stroke="${PALETTE[SHEATH]!.core}"/>`;
    out += `<path class="core" d="${d}" stroke="${stroke}"/>`;
  }
  if (wire.body) {
    const [body, band] = bodyRects(wire.body);
    out += `<rect class="cb" x="${f(body[0])}" y="${f(body[1])}" width="${f(body[2])}" height="${f(body[3])}" rx="2.5"/>`
      + `<rect class="cbd" x="${f(band[0])}" y="${f(band[1])}" width="${f(band[2])}" height="${f(band[3])}"/>`;
    const lines = wire.text?.[lang] ?? [];
    const at = wire.body.text;
    baselines(at.y, lines.length, 10, 11).forEach((y, i) => {
      out += `<text class="ct" x="${f(at.x)}" y="${f(y)}" text-anchor="${at.anchor}">${esc(lines[i]!)}</text>`;
    });
  }
  for (const l of wire.labels) {
    out += `<rect class="nb" x="${f(l.x - LABEL.w / 2)}" y="${f(l.y - LABEL.h / 2)}" width="${LABEL.w}" height="${LABEL.h}" rx="3" stroke="${stroke}"/>`
      + `<text class="nt" x="${f(l.x)}" y="${f(l.y + 3.2)}">${wire.n}</text>`;
  }
  return `${out}</g>`;
}

/** The SVG of a routed diagram. `names`: part id to its name. `labels`: wire number to its accessible name. */
export function pictureSvg(
  d: Diagram, lang: Lang, names: ReadonlyMap<string, string>, labels: ReadonlyMap<number, string>, title: string, desc: string,
): string {
  const folder = DRAWINGS + (lang === 'de' ? 'de/' : '');
  let parts = '';
  for (const p of d.parts) {
    const name = names.get(p.id) ?? p.id;
    const fit = name.length * NAME_CHAR > p.w - 4 ? ` textLength="${p.w - 4}" lengthAdjust="spacingAndGlyphs"` : '';
    parts += `<g class="dp" data-part="${esc(p.id)}"><text class="cap" x="${p.x + 2}" y="${p.y + CAP - 8}"${fit}>${esc(name)}</text>`
      + `<image href="${folder}${esc(p.file)}" x="${p.ix}" y="${p.iy}" width="${p.iw}" height="${p.ih}"/></g>`;
  }
  const wires = d.wires.map((w, i) => wireMarkup(w, labels.get(w.n) ?? String(w.n), i === 0, lang)).join('');
  return `<svg class="picture" id="dia-svg" viewBox="0 0 ${d.w} ${d.h}" width="${d.w}" height="${d.h}" role="group" aria-labelledby="dia-t" aria-describedby="dia-d wires-cap" focusable="false">`
    + `<title id="dia-t">${esc(title)}</title><desc id="dia-d">${esc(desc)}</desc>`
    + `<g class="vp"><g class="parts">${parts}</g><g class="wires">${wires}</g></g></svg>`;
}

/** The legend below the picture: the 2 stroke widths, the USB-C cable and the 2 supply colours. */
export function legend(lang: Lang): string {
  const line = (width: number, colour: string, casing: string | null): string =>
    `<svg class="lg" width="34" height="12" viewBox="0 0 34 12" aria-hidden="true" focusable="false">`
    + (casing ? `<path d="M3 6H31" stroke="${casing}" stroke-width="${width + 2}"/>` : '')
    + `<path d="M3 6H31" stroke="${colour}" stroke-width="${width}"/></svg>`;
  const plus = PALETTE[PLUS]!;
  const minus = PALETTE[MINUS]!;
  const items: Array<[string, Key]> = [
    [line(THICK, 'currentColor', null), 'dlPower'],
    [line(THIN, plus.core, null).replace('<path', `<path d="M3 6H31" stroke="${PALETTE[SHEATH]!.core}" stroke-width="${THICK}"/><path`), 'dlCable'],
    [line(THIN, 'currentColor', null), 'dlSignal'],
    [line(THIN, plus.core, plus.casing), 'dlPlus'],
    [line(THIN, minus.core, minus.casing), 'dlMinus'],
  ];
  return `<ul class="legend" id="dia-legend" aria-label="${esc(t(lang, 'dlTitle'))}">`
    + items.map(([mark, key]) => `<li>${mark}<span>${esc(t(lang, key))}</span></li>`).join('') + '</ul>';
}

const TOOLS: ReadonlyArray<readonly [string, Key, string]> = [
  ['out', 'zOut', '−'], ['in', 'zIn', '+'], ['fit', 'zFit', ''],
  ['left', 'mLeft', '←'], ['up', 'mUp', '↑'], ['down', 'mDown', '↓'], ['right', 'mRight', '→'],
];

/** The pictorial view of a setup: controls, the box with the picture, the hint. */
export function picture(view: View): string {
  const { lang, catalogue, setup, options } = view;
  const d = diagramOf(sceneOf(setup, ANCHORS));
  const ctx = { lang, catalogue, options };
  const names = new Map(setup.parts.map((r) => [r.part.id, tx(lang, r.part.short)]));
  const labels = new Map(setup.wires.map((w) =>
    [w.n, t(lang, 'diaWire', { n: w.n, a: endName(w.a, ctx), b: targetName(w, ctx) })]));
  const title = t(lang, 'diaTitle', { p: d.parts.length, w: d.wires.length });
  const desc = t(lang, 'diaDesc')
    + (d.undrawn.length > 0 ? ` ${t(lang, 'diaUndrawn', { n: d.undrawn.join(', ') })}` : '');
  const tools = TOOLS.map(([id, key, glyph]) => (glyph
    ? `<button type="button" class="tool" data-dia="${id}" aria-label="${esc(t(lang, key))}" title="${esc(t(lang, key))}">${glyph}</button>`
    : `<button type="button" class="tool" data-dia="${id}">${esc(t(lang, key))}</button>`)).join('');
  return `<div class="tools" role="group" aria-label="${esc(t(lang, 'diaTools'))}">${tools}</div>`
    + `<div class="dia" id="dia" tabindex="0" role="group" aria-label="${esc(t(lang, 'diaBox'))}">`
    + pictureSvg(d, lang, names, labels, title, desc)
    + '</div>'
    + legend(lang)
    + `<p class="hint" id="dia-hint">${esc(t(lang, 'diaHint'))}`
    + (d.undrawn.length > 0 ? ` ${esc(t(lang, 'diaUndrawn', { n: d.undrawn.join(', ') }))}` : '')
    + '</p>';
}
