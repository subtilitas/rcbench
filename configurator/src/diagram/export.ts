// The export form of the pictorial wiring view: 1 SVG (Scalable Vector
// Graphics) document without a reference to another file, for the painter of
// `src/export/svg.ts`. It uses svg, g, rect, circle, path with M and L, and
// text, styled by attributes only. Text has no class: the painter gives it
// the 12 px of the rule `svg text` of the style sheet, the size the attributes
// state.
//
// Placement, routes, stroke widths and wire colours are those of the page.
// A part is an outline of its drawing with its wire points and pin names; the
// drawing file itself is not part of this form. A component is its leads, a
// body with a band as 2 rectangles, 1 number and its lines.

import type { Catalogue, Lang, Setup } from '../../contract.ts';
import type { Anchors } from '../../drawings/generate.ts';
import { PALETTE, SHEATH } from '../../drawings/wires.ts';
import { esc, tx } from '../i18n.ts';
import { ANCHORS } from './anchors.ts';
import { CAP, THICK, THIN } from './layout.ts';
import { BAND_FILL, BODY_FILL, baselines, bodyRects } from './picture.ts';
import { LABEL, diagramOf } from './route.ts';
import type { Diagram, Pt, Routed } from './route.ts';
import { sceneOf } from './scene.ts';

/** Colours of the export form. A wire takes its colour from the palette. */
export const INK = '#15191f';
export const BOARD = '#eef1f5';
export const EDGE_INK = '#5b6573';
export const PAPER = '#ffffff';
/** Font size of every text in px. */
export const FONT = 12;
/** Radius of a wire point in px. */
export const POINT = 4;

const f = (v: number): string => String(Math.round(v * 10) / 10);

/** Path data of a polyline with the commands M and L. */
export const lineOf = (points: Pt[]): string => points.map((p, i) => `${i === 0 ? 'M' : 'L'}${f(p[0])} ${f(p[1])}`).join('');

/** Stroke width of a wire in px. */
export const widthOf = (wire: Pick<Routed, 'kind' | 'thick'>): number =>
  wire.kind === 'component' ? 2 : wire.thick ? THICK : THIN;

function partMarkup(p: Diagram['parts'][number], name: string, lang: Lang, anchors: Anchors): string {
  const rec = anchors.files[p.file];
  let out = `<g><text x="${f(p.x + 2)}" y="${f(p.y + CAP - 8)}" font-size="${FONT}" fill="${INK}">${esc(name)}</text>`
    + `<rect x="${f(p.ix)}" y="${f(p.iy)}" width="${f(p.iw)}" height="${f(p.ih)}" rx="6" fill="${BOARD}" stroke="${EDGE_INK}" stroke-width="1.5"/>`;
  if (!rec) return `${out}</g>`;
  const kx = p.iw / rec.viewBox[2];
  const ky = p.ih / rec.viewBox[3];
  for (const pin of Object.values(rec.pins)) {
    const x = p.ix + pin.x * kx;
    const y = p.iy + pin.y * ky;
    const [tx0, ty0, anchor] = pin.side === 'W' ? [x + POINT + 3, y + 4, 'start']
      : pin.side === 'E' ? [x - POINT - 3, y + 4, 'end']
        : pin.side === 'N' ? [x, y + POINT + 12, 'middle']
          : [x, y - POINT - 4, 'middle'];
    out += `<circle cx="${f(x)}" cy="${f(y)}" r="${POINT}" fill="${PAPER}" stroke="${EDGE_INK}" stroke-width="1"/>`
      + `<text x="${f(tx0)}" y="${f(ty0)}" font-size="${FONT}" text-anchor="${anchor}" fill="${INK}">${esc(pin.label[lang])}</text>`;
  }
  return `${out}</g>`;
}

function wireMarkup(wire: Routed, lang: Lang): string {
  const colour = wire.colour === null ? undefined : PALETTE[wire.colour];
  const stroke = colour ? colour.core : INK;
  const width = widthOf(wire);
  const line = 'fill="none" stroke-linecap="round" stroke-linejoin="round"';
  let out = '<g>';
  for (const path of wire.paths) {
    const d = lineOf(path);
    if (colour?.casing) out += `<path d="${d}" ${line} stroke="${colour.casing}" stroke-width="${width + 2}"/>`;
    if (wire.cable) out += `<path d="${d}" ${line} stroke="${PALETTE[SHEATH]!.core}" stroke-width="${width}"/>`;
    out += `<path d="${d}" ${line} stroke="${stroke}" stroke-width="${wire.cable ? THIN : width}"${wire.kind === 'stub' ? ' stroke-dasharray="5 4"' : ''}/>`;
  }
  if (wire.body) {
    const [body, band] = bodyRects(wire.body);
    out += `<rect x="${f(body[0])}" y="${f(body[1])}" width="${f(body[2])}" height="${f(body[3])}" rx="2.5" fill="${BODY_FILL}" stroke="${INK}" stroke-width="1"/>`
      + `<rect x="${f(band[0])}" y="${f(band[1])}" width="${f(band[2])}" height="${f(band[3])}" fill="${BAND_FILL}"/>`;
    const lines = wire.text?.[lang] ?? [];
    const at = wire.body.text;
    baselines(at.y, lines.length, FONT, FONT + 1).forEach((y, i) => {
      out += `<text x="${f(at.x)}" y="${f(y)}" font-size="${FONT}" text-anchor="${at.anchor}" fill="${INK}">${esc(lines[i]!)}</text>`;
    });
  }
  for (const l of wire.labels) {
    out += `<rect x="${f(l.x - LABEL.w / 2)}" y="${f(l.y - LABEL.h / 2)}" width="${LABEL.w}" height="${LABEL.h}" rx="3" fill="${PAPER}" stroke="${stroke}" stroke-width="1.5"/>`
      + `<text x="${f(l.x)}" y="${f(l.y + 4.2)}" font-size="${FONT}" text-anchor="middle" fill="${INK}">${wire.n}</text>`;
  }
  return `${out}</g>`;
}

/** The export form of a routed diagram. `names`: part id to its name. */
export function exportSvg(d: Diagram, lang: Lang, names: ReadonlyMap<string, string>, anchors: Anchors = ANCHORS): string {
  const parts = d.parts.map((p) => partMarkup(p, names.get(p.id) ?? p.id, lang, anchors)).join('');
  const wires = d.wires.map((w) => wireMarkup(w, lang)).join('');
  return `<svg viewBox="0 0 ${d.w} ${d.h}" width="${d.w}" height="${d.h}"><g>${parts}</g><g>${wires}</g></svg>`;
}

/** The pictorial wiring view of a setup in its export form. The signature is the one of `src/export/diagram.ts`. */
export function pictureDiagram(setup: Setup, _catalogue: Catalogue, lang: Lang): string {
  const names = new Map(setup.parts.map((r) => [r.part.id, tx(lang, r.part.short)]));
  return exportSvg(diagramOf(sceneOf(setup, ANCHORS)), lang, names);
}
