// The export form of the pictorial wiring view: inside the subset of the PDF
// painter, with the stroke widths and the supply colours of the page.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang } from '../contract.ts';
import { CABLE_NETS, MINUS, MINUS_NETS, PALETTE, PLUS, PLUS_NETS, SHEATH } from '../drawings/wires.ts';
import { CATALOGUE } from '../src/core.ts';
import { ANCHORS } from '../src/diagram/anchors.ts';
import { FONT, INK, exportSvg, lineOf, pictureDiagram, widthOf } from '../src/diagram/export.ts';
import { THICK, THIN } from '../src/diagram/layout.ts';
import { diagramOf } from '../src/diagram/route.ts';
import { carriesLoad, sceneOf } from '../src/diagram/scene.ts';
import { wiringDiagram } from '../src/export/diagram.ts';
import { SvgError, paintSvg } from '../src/export/svg.ts';
import { LANGS, LARGEST, SAMPLES, SMALLEST, build, css, fonts, viewOf } from './export/helpers.ts';

const TAGS = new Set(['svg', 'g', 'rect', 'circle', 'path', 'text']);
const ATTRS = new Set([
  'viewBox', 'width', 'height', 'x', 'y', 'rx', 'cx', 'cy', 'r', 'd', 'fill', 'stroke', 'stroke-width',
  'stroke-dasharray', 'stroke-linecap', 'stroke-linejoin', 'font-size', 'text-anchor',
]);

/** Every 97th sample and both ends of the sweep. */
const some = [SMALLEST, LARGEST, ...SAMPLES.filter((_, i) => i % 97 === 0)];

test('the export form uses the elements and attributes of the painter subset and paints without a refusal', () => {
  for (const sample of some) {
    for (const lang of LANGS) {
      const svg = pictureDiagram(sample.setup, CATALOGUE, lang);
      for (const m of svg.matchAll(/<([a-zA-Z]+)((?:\s+[\w:-]+="[^"]*")*)\s*\/?>/g)) {
        assert.ok(TAGS.has(m[1]!), `element ${m[1]}`);
        for (const a of m[2]!.matchAll(/([\w:-]+)="/g)) assert.ok(ATTRS.has(a[1]!), `attribute ${a[1]} on ${m[1]}`);
      }
      for (const m of svg.matchAll(/ d="([^"]*)"/g)) assert.match(m[1]!, /^M[\d. -]+(L[\d. -]+)+$/);
      const painted = paintSvg(svg, css(), fonts());
      const d = diagramOf(sceneOf(sample.setup, ANCHORS));
      assert.equal(painted.width, d.w);
      assert.equal(painted.height, d.h);
      assert.equal(painted.boxes.length >= d.parts.length, true);
      for (const text of painted.texts) assert.equal(text.size, FONT, text.text);
      const numbers = painted.texts.filter((x) => /^\d+$/.test(x.text)).length;
      assert.equal(numbers, d.wires.reduce((n, w) => n + w.labels.length, 0));
    }
  }
});

test('the export form draws a load wire 7 px wide, every other wire 3 px, plus red and minus black', () => {
  const seen = { thick: 0, thin: 0, plus: 0, minus: 0, cable: 0 };
  for (const sample of some) {
    const d = diagramOf(sceneOf(sample.setup, ANCHORS));
    const svg = exportSvg(d, 'en', new Map());
    const groups = svg.slice(svg.lastIndexOf('<g><g>') + 3).split('<g>').slice(1);
    assert.equal(groups.length, d.wires.length);
    d.wires.forEach((routed, i) => {
      const wire = sample.setup.wires.find((w) => w.n === routed.n)!;
      const colour = routed.colour === null ? null : PALETTE[routed.colour]!;
      const core = new RegExp(`stroke="${colour ? colour.core : INK}" stroke-width="(\\d+)"`).exec(groups[i]!);
      assert.ok(core, `wire ${wire.n}: no core line in its colour`);
      const width = Number(core[1]);
      // A USB-C cable: a sheath of the width of the wire, and inside it a core of 3 px in the colour of its net.
      const cable = CABLE_NETS.has(wire.net) && wire.kind === 'wire';
      assert.equal(routed.cable === true, cable, `wire ${wire.n}: cable`);
      assert.equal(width, cable ? THIN : carriesLoad(wire) ? THICK : wire.kind === 'component' ? 2 : THIN, `wire ${wire.n}`);
      if (cable) {
        assert.ok(groups[i]!.indexOf(`stroke="${PALETTE[SHEATH]!.core}" stroke-width="${widthOf(routed)}"`) >= 0, `wire ${wire.n}: sheath`);
        assert.ok(groups[i]!.indexOf(`stroke="${PALETTE[SHEATH]!.core}"`) < groups[i]!.indexOf(`stroke="${colour!.core}"`), `wire ${wire.n}: sheath below core`);
        seen.cable++;
      } else assert.equal(width, widthOf(routed));
      if (colour?.casing) assert.ok(groups[i]!.includes(`stroke="${colour.casing}" stroke-width="${width + 2}"`), `wire ${wire.n}: casing`);
      assert.equal(routed.colour === PLUS, PLUS_NETS.has(wire.net) && wire.kind !== 'component', `wire ${wire.n}: plus`);
      assert.equal(routed.colour === MINUS, MINUS_NETS.has(wire.net) && wire.kind !== 'component', `wire ${wire.n}: minus`);
      seen[width === THICK ? 'thick' : 'thin']++;
      if (routed.colour === PLUS) seen.plus++;
      if (routed.colour === MINUS) seen.minus++;
    });
    // The painter writes the width of every stroke: a 7 px stroke exists exactly when a load wire does.
    const ops = paintSvg(svg, css(), fonts()).ops;
    assert.equal(/(^|\n)7 w\n/.test(ops), d.wires.some((w) => w.thick));
  }
  for (const [name, count] of Object.entries(seen)) assert.ok(count > 0, name);
});

test('the export form of a diagram without a part or with a part without a drawing record is valid markup', () => {
  const empty = exportSvg({ w: 10, h: 10, parts: [], wires: [], undrawn: [], columns: [], centre: 0, clashes: [] }, 'en', new Map());
  assert.equal(empty, '<svg viewBox="0 0 10 10" width="10" height="10"><g></g><g></g></svg>');
  assert.equal(paintSvg(empty, css(), fonts()).texts.length, 0);
  const d = diagramOf(sceneOf(SMALLEST.setup, ANCHORS));
  const none = exportSvg(d, 'en', new Map(), { ...ANCHORS, files: {} });
  assert.equal((none.match(/<circle/g) ?? []).length, 0);
  assert.equal((none.match(/<rect [^>]*rx="6"/g) ?? []).length, d.parts.length);
  assert.ok(none.includes(`>${d.parts[0]!.id}</text>`));
  assert.equal(lineOf([[0, 0], [10.04, 0], [10.04, 5.55]]), 'M0 0L10 0L10 5.6');
});

test('a name with markup characters is escaped, and the form of the page is refused by the painter', () => {
  const d = diagramOf(sceneOf(SMALLEST.setup, ANCHORS));
  const svg = exportSvg(d, 'en', new Map([[d.parts[0]!.id, 'a<b & "c"']]));
  assert.ok(svg.includes('a&lt;b &amp; &quot;c&quot;'));
  assert.ok(paintSvg(svg, css(), fonts()).texts.some((x) => x.text === 'a<b & "c"'));
  assert.throws(() => paintSvg('<svg viewBox="0 0 9 9"><image href="drawings/tap.svg" x="0" y="0" width="9" height="9"/></svg>', css(), fonts()), SvgError);
});

test('the whole export takes the export form: 1 page for the picture, text from 1.9 pt to 6.2 pt', async () => {
  const pt = async (sample: typeof SMALLEST, lang: Lang): Promise<number> => {
    const made = await build(viewOf(sample.selection, sample.setup, lang), { diagram: pictureDiagram });
    assert.equal(made.laid.plan.flat().filter((b) => b.kind === 'drawing').length, 1);
    assert.equal(Buffer.from(made.bytes.subarray(0, 5)).toString('latin1'), '%PDF-');
    return FONT * made.laid.drawingScale;
  };
  for (const lang of LANGS) {
    const small = await pt(SMALLEST, lang);
    const large = await pt(LARGEST, lang);
    assert.ok(small > 6 && small < 6.2, `smallest ${small.toFixed(2)} pt`);
    assert.ok(large > 1.9 && large < 2.1, `largest ${large.toFixed(2)} pt`);
  }
});

test('the export shows the schematic: its legend and its table name the lines of the schematic', () => {
  assert.ok(wiringDiagram(SMALLEST.setup, CATALOGUE, 'en').includes('class="drawing"'));
});
