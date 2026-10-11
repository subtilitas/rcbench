// The wiring drawing as SVG markup, built from the geometry of layout.ts.
// Draw order: modules, coprocessor, wires in ascending number, dots, pills.

import type { Catalogue, Lang, Setup, SignalClass } from '../../contract.ts';
import { esc } from '../i18n.ts';
import { layout } from './layout.ts';
import type { Layout } from './layout.ts';

/** Stroke width per signal class in px. The halo is 4 px wider. */
export const WIDTH: Record<SignalClass, number> = {
  canh: 2.5, canl: 2.5, gnd: 1.5, hb: 2.5, out: 2.5, uart: 2.5, sda: 2.5, scl: 2.5, logic: 3, power: 5, phase: 2, nc: 1.5,
};

/** Line sample of a signal class, 38 x 12 px, for the legend and the table. */
export function sample(cls: SignalClass): string {
  return `<svg class="sample" width="38" height="12" viewBox="0 0 38 12" aria-hidden="true" focusable="false"><path class="w c-${cls}" d="M1 6 H37"/></svg>`;
}

export function render(l: Layout): string {
  const out: string[] = [];
  for (const m of l.modules) {
    out.push(`<rect class="box${m.dashed ? ' slot' : ''}" x="${m.x}" y="${m.y}" width="${m.w}" height="${m.h}" rx="5"/>`);
    out.push(`<text class="mt" x="${m.x + 8}" y="${m.y + 17}">${esc(m.title)}</text>`);
    const fit = m.subFit === null ? '' : ` textLength="${m.subFit}" lengthAdjust="spacingAndGlyphs"`;
    m.sub.forEach((line, i) => {
      out.push(`<text class="ms" x="${m.x + 8}" y="${m.y + 31 + i * 13}"${fit}>${esc(line)}</text>`);
    });
    for (const p of m.pins) {
      if (!p.drawn) continue;
      out.push(`<text x="${p.x}" y="${p.y + 4}" text-anchor="${p.anchor}"${p.used ? '' : ' class="mu"'}>${esc(p.label)}</text>`);
    }
  }

  const c = l.copro;
  out.push(`<rect class="box" x="${c.x}" y="${c.y}" width="${c.w}" height="${c.h}" rx="5"/>`);
  out.push(`<text class="mt" x="${c.x + 8}" y="${c.y + 17}">${esc(c.title)}</text>`);
  out.push(`<text class="ms" x="${c.x + 8}" y="${c.y + 31}">${esc(c.sub)}</text>`);
  for (const row of c.can) out.push(`<text class="on" x="${c.x + 8}" y="${row.y + 4}">${esc(row.label)}</text>`);
  for (const p of l.pads) {
    const cls = p.used ? 'on' : 'mu';
    if (p.side === 'L') {
      if (p.num) {
        out.push(`<text class="${cls}" x="${c.x + 24}" y="${p.y + 4}" text-anchor="end">${p.num}</text>`);
        out.push(`<text class="${cls}" x="${c.x + 31}" y="${p.y + 4}">${esc(p.label)}</text>`);
      } else {
        out.push(`<text class="${cls}" x="${c.x + 8}" y="${p.y + 4}">${esc(p.label)}</text>`);
      }
    } else if (p.num) {
      out.push(`<text class="${cls}" x="${c.x + c.w - 31}" y="${p.y + 4}" text-anchor="end">${esc(p.label)}</text>`);
      out.push(`<text class="${cls}" x="${c.x + c.w - 8}" y="${p.y + 4}" text-anchor="end">${p.num}</text>`);
    } else {
      out.push(`<text class="${cls}" x="${c.x + c.w - 8}" y="${p.y + 4}" text-anchor="end">${esc(p.label)}</text>`);
    }
  }

  for (const w of l.wires) {
    const d = 'M' + w.points.map((p) => `${p[0]} ${p[1]}`).join(' L');
    out.push(`<path class="halo" d="${d}" stroke-width="${WIDTH[w.cls] + 4}"/>`);
    out.push(`<path class="w c-${w.cls}" d="${d}"><title>${esc(w.title)}</title></path>`);
    if (w.cross) {
      const [x, y] = w.cross;
      const r = l.geo.cross / 2;
      out.push(`<path class="w c-nc x" d="M${x - r} ${y - r} L${x + r} ${y + r} M${x - r} ${y + r} L${x + r} ${y - r}"/>`);
    }
  }
  for (const d of l.dots) out.push(`<circle class="dot" cx="${d[0]}" cy="${d[1]}" r="3"/>`);
  const g = l.geo;
  for (const p of l.pills) {
    out.push(`<g class="pill"><rect x="${p.x - g.pillW / 2}" y="${p.y - g.pillH / 2}" width="${g.pillW}" height="${g.pillH}" rx="8"/><text x="${p.x}" y="${p.y + 4}">${p.n}</text></g>`);
  }
  return `<svg class="drawing" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${l.width} ${l.height}" width="${l.width}" height="${l.height}" role="img" aria-label="${esc(l.label)}">${out.join('')}</svg>`;
}

/** The drawing of one setup. */
export function drawing(setup: Setup, catalogue: Catalogue, lang: Lang): string {
  return render(layout(setup, catalogue, lang));
}
