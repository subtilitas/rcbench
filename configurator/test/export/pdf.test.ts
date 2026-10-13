// The bytes of the PDF export: file structure, text that a reader extracts,
// embedded fonts, metadata, links, and the same bytes for the same input.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang } from '../../contract.ts';
import { CATALOGUE, resolve } from '../../src/core.ts';
import type { Exported } from '../../src/export/build.ts';
import { FontError, parseFont, use } from '../../src/export/font.ts';
import { PAGE } from '../../src/export/layout.ts';
import { deflate, fnv, literal, num, textString, uri, writePdf } from '../../src/export/pdf.ts';
import { PDF_STRINGS } from '../../src/export/text.ts';
import { t, tx } from '../../src/i18n.ts';
import { partsList } from '../../src/view/buy.ts';
import type { View } from '../../src/view/chooser.ts';
import { EMPTY, FONT_BYTES, LANGS, LARGEST, NOW, SMALLEST, build, fonts, latin, readPdf, squeeze, urlOf, viewOf } from './helpers.ts';
import type { Read } from './helpers.ts';

const CASES = [
  { name: 'smallest setup', sample: SMALLEST },
  { name: 'largest setup', sample: LARGEST },
  { name: 'empty selection', sample: { selection: EMPTY, setup: resolve(CATALOGUE, EMPTY), key: '' } },
];
const plain = (text: string): string => text.replace(/<\/?b>/g, '');
/** The link targets of the export: the share link, then the maker pages and web searches of the parts list. */
const targetsOf = (view: View): string[] =>
  [...new Set([urlOf(view), ...partsList(view).buy.flatMap((l) => [l.page, l.search]).filter((u): u is string => u !== null)])].sort();

const made = new Map<string, Promise<{ out: Exported; read: Read }>>();
/** One export per case and language, shared by the tests. */
function exported(index: number, lang: Lang): Promise<{ out: Exported; read: Read }> {
  const key = `${index} ${lang}`;
  let entry = made.get(key);
  if (!entry) {
    const { sample } = CASES[index] as (typeof CASES)[number];
    entry = build(viewOf(sample.selection, sample.setup, lang)).then(async (out) => ({ out, read: await readPdf(out.bytes) }));
    made.set(key, entry);
  }
  return entry;
}

for (const [index, c] of CASES.entries()) {
  for (const lang of LANGS) {
    const label = `${c.name}, ${lang}`;
    const view = viewOf(c.sample.selection, c.sample.setup, lang);

    test(`${label}: the file is a PDF with a valid cross-reference table and the planned page count`, async () => {
      const { out, read } = await exported(index, lang);
      const text = latin(out.bytes);
      assert.ok(text.startsWith('%PDF-1.7\n'));
      assert.ok(text.endsWith('%%EOF\n'));
      const start = Number(/startxref\n(\d+)\n%%EOF\n$/.exec(text)?.[1]);
      assert.equal(text.slice(start, start + 5), 'xref\n');
      const table = /^xref\n0 (\d+)\n((?:\d{10} \d{5} [nf] \n)+)trailer\n/.exec(text.slice(start));
      assert.ok(table, 'no cross-reference table at startxref');
      const rows = (table[2] as string).trimEnd().split('\n');
      assert.equal(rows.length, Number(table[1]));
      assert.equal(rows[0], '0000000000 65535 f ');
      rows.slice(1).forEach((row, i) => {
        const offset = Number(row.slice(0, 10));
        assert.ok(text.startsWith(`${i + 1} 0 obj\n`, offset), `object ${i + 1} is not at its offset`);
      });
      assert.equal(read.pages, out.laid.plan.length);
      assert.equal(read.pages, out.laid.pages.length);
      assert.equal(Number(/\/Type \/Pages [^\n]*\/Count (\d+)/.exec(text)?.[1]), read.pages);
      assert.ok(text.includes(`/MediaBox [0 0 ${PAGE[0]} ${PAGE[1]}]`));
    });

    test(`${label}: every safety note is extractable as text, in full, before the parts`, async () => {
      const { read } = await exported(index, lang);
      const all = squeeze(read.text.join(' '));
      let at = all.indexOf(squeeze(t(lang, 'safety')));
      assert.ok(at >= 0);
      assert.ok(CATALOGUE.safety.length > 0);
      for (const note of CATALOGUE.safety) {
        const next = all.indexOf(squeeze(plain(tx(lang, note.text))), at);
        assert.ok(next >= 0, `a safety note is not in the text in order: ${tx(lang, note.text).slice(0, 40)}`);
        at = next;
      }
      const firstPart = squeeze(tx(lang, (view.setup.parts[0] as (typeof view.setup.parts)[number]).part.name));
      assert.ok(all.indexOf(firstPart, at) > at, 'the parts list does not follow the safety notes');
    });

    test(`${label}: every wire number is extractable as text in its table row`, async () => {
      const { out, read } = await exported(index, lang);
      const table = out.doc.sections.flatMap((s) => s.blocks).find((b) => b.kind === 'table');
      assert.equal(table?.kind === 'table' ? table.rows.length : 0, view.setup.wires.length);
      if (table?.kind !== 'table') return;
      for (const row of table.rows) {
        const page = out.laid.plan.findIndex((p) => p.some((b) => b.kind === 'row' && b.ref === row.n));
        const items = read.items[page] as string[];
        const from = squeeze(row.from);
        const hit = items.some((item, i) => item === String(row.n) && from.startsWith(squeeze(items[i + 1] ?? '\u0000')));
        assert.ok(hit, `wire ${row.n} is not followed by its first end on page ${page + 1}`);
      }
    });

    test(`${label}: every page carries the page number, the page count and the notice`, async () => {
      const { read } = await exported(index, lang);
      const notice = squeeze(t(lang, 'foot'));
      read.text.forEach((text, i) => {
        const label = PDF_STRINGS.page[lang === 'de' ? 1 : 0].replace('{x}', String(i + 1)).replace('{y}', String(read.pages));
        assert.ok(squeeze(text).endsWith(squeeze(label) + notice), `page ${i + 1} does not end with its footer`);
      });
    });

    test(`${label}: title, language, date and the share link are in the file`, async () => {
      const { out, read } = await exported(index, lang);
      assert.equal(read.title, out.doc.title);
      assert.ok(read.title.includes(out.fileName.slice('rcbench-setup-'.length, -'.pdf'.length)));
      assert.equal(read.creator, t(lang, 'title'));
      assert.equal(read.lang, lang);
      assert.ok(read.links.length >= 1);
      assert.deepEqual([...new Set(read.links)].sort(), targetsOf(view));
      const first = squeeze(read.text[0] as string);
      assert.ok(first.includes(squeeze(urlOf(view))));
      assert.ok(first.includes(squeeze(`${CATALOGUE.release}`)));
      assert.ok(first.includes('2026-03-04'));
      const text = latin(out.bytes);
      assert.ok(text.includes('/CreationDate (D:20260304120000Z)'));
      assert.ok(text.includes('/DisplayDocTitle true'));
    });
  }
}

test('both fonts are embedded as subsets with a Unicode map, and the file names no other font', async () => {
  for (const lang of LANGS) {
    const view = viewOf(LARGEST.selection, LARGEST.setup, lang);
    const { bytes } = await build(view, { compress: false });
    const text = latin(bytes);
    assert.equal(text.match(/\/Subtype \/Type0 /g)?.length, 2);
    assert.equal(text.match(/\/FontFile2 \d+ 0 R/g)?.length, 2);
    assert.equal(text.match(/\/ToUnicode \d+ 0 R/g)?.length, 2);
    const names = [...text.matchAll(/\/BaseFont \/([A-Z]{6})\+(Inter-(?:Regular|Bold))\b/g)].map((m) => m[2]);
    assert.deepEqual(names.sort(), ['Inter-Bold', 'Inter-Bold', 'Inter-Regular', 'Inter-Regular']);
    assert.equal(text.match(/\/BaseFont /g)?.length, 4);
    // Stored without deflate, each font file is in the PDF byte for byte.
    assert.ok(text.includes(latin(FONT_BYTES.regular)));
    assert.ok(text.includes(latin(FONT_BYTES.bold)));
    // Every font the pages name is one of the 2.
    const used = new Set([...text.matchAll(/\/(F\d+) [\d.]+ Tf/g)].map((m) => m[1]));
    assert.deepEqual([...used].sort(), ['F1', 'F2']);
  }
});

test('the file holds no script, no form, no attachment, no image and no reference to another file', async () => {
  for (const lang of LANGS) {
    const view = viewOf(LARGEST.selection, LARGEST.setup, lang);
    const { bytes } = await build(view, { compress: false });
    const text = latin(bytes).split(latin(FONT_BYTES.regular)).join('').split(latin(FONT_BYTES.bold)).join('');
    for (const name of ['JavaScript', 'JS', 'OpenAction', 'AA', 'Launch', 'GoToR', 'GoToE', 'EmbeddedFile', 'Filespec', 'F', 'FS', 'AcroForm', 'XObject', 'Image', 'SubmitForm', 'ImportData', 'RichMedia', 'FontFile', 'FontFile3']) {
      assert.ok(!new RegExp(`/${name}[ \\[(<\\/\\n]`).test(text), `the file holds /${name}`);
    }
    const targets = [...text.matchAll(/\/URI \(([^)]*)\)/g)].map((m) => m[1]);
    assert.ok(targets.length >= 1);
    assert.deepEqual([...new Set(targets)].sort(), targetsOf(view));
    const actions = [...text.matchAll(/\/S \/(\w+)/g)].map((m) => m[1]);
    assert.deepEqual([...new Set(actions)], ['URI']);
  }
});

test('the same setup, language, link and date give the same bytes', async () => {
  const view = viewOf(LARGEST.selection, LARGEST.setup, 'de');
  const a = await build(view);
  const b = await build(view);
  assert.deepEqual(a.bytes, b.bytes);
  assert.equal(a.fileName, b.fileName);
  const later = await build(view, { now: new Date(NOW.getTime() + 86_400_000) });
  assert.notDeepEqual(later.bytes, a.bytes);
  const other = await build(viewOf(LARGEST.selection, LARGEST.setup, 'en'));
  assert.notDeepEqual(other.bytes, a.bytes);
  const id = (bytes: Uint8Array): string => /\/ID \[<([0-9a-f]{32})> <\1>\]/.exec(latin(bytes))?.[1] ?? '';
  assert.equal(id(a.bytes).length, 32);
  assert.equal(id(a.bytes), id(b.bytes));
  assert.notEqual(id(a.bytes), id(other.bytes));
});

test('a platform without CompressionStream gets a larger file with the same pages and text', async () => {
  const view = viewOf(SMALLEST.selection, SMALLEST.setup, 'en');
  const packed = await build(view);
  const stored = await build(view, { compress: false });
  const holder = globalThis as { CompressionStream?: unknown };
  const saved = holder.CompressionStream;
  let without: Exported;
  try {
    holder.CompressionStream = undefined;
    assert.equal(await deflate(new Uint8Array([1, 2, 3])), null);
    without = await build(view);
  } finally {
    holder.CompressionStream = saved;
  }
  assert.deepEqual(without.bytes, stored.bytes);
  assert.ok(latin(packed.bytes).includes('/Filter /FlateDecode'));
  assert.ok(!latin(stored.bytes).includes('/FlateDecode'));
  assert.ok(stored.bytes.length > packed.bytes.length);
  const [a, b] = [await readPdf(packed.bytes), await readPdf(stored.bytes)];
  assert.equal(b.pages, a.pages);
  assert.deepEqual(b.text, a.text);
});

test('the export stays under 150 kB for the largest setup', async () => {
  for (const lang of LANGS) {
    const { bytes } = await build(viewOf(LARGEST.selection, LARGEST.setup, lang));
    assert.ok(bytes.length < 150_000, `${lang}: ${bytes.length} bytes`);
    assert.ok(bytes.length > 20_000, `${lang}: ${bytes.length} bytes`);
  }
});

test('the export fails with an error on a broken font, a character without a glyph or a style sheet without the colours', async () => {
  const view = viewOf(SMALLEST.selection, SMALLEST.setup, 'en');
  await assert.rejects(build(view, { fonts: { regular: new Uint8Array(40), bold: FONT_BYTES.bold } }), FontError);
  await assert.rejects(build(view, { fonts: { regular: FONT_BYTES.regular.slice(0, 2000), bold: FONT_BYTES.bold } }), FontError);
  await assert.rejects(build(view, { url: 'https://example.org/#中' }), FontError);
  await assert.rejects(build(view, { css: 'svg text { fill: red; }' }), /colour/);
  await assert.rejects(build(view, { diagram: () => '<svg viewBox="0 0 10 10"><image href="x.png"/></svg>' }), /does not paint/);
  await assert.rejects(build(view, { diagram: () => { throw new Error('no drawing'); } }), /no drawing/);
});

test('writer primitives hold their limits', async () => {
  assert.equal(num(0), '0');
  assert.equal(num(-0.0001), '0');
  assert.equal(num(1.23456), '1.235');
  assert.equal(num(-12.5), '-12.5');
  assert.equal(num(1e-7), '0');
  assert.equal(num(123456789), '123456789');
  assert.throws(() => num(Number.NaN));
  assert.throws(() => num(Number.POSITIVE_INFINITY));
  assert.equal(textString('Ä'), '<FEFF00C4>');
  assert.equal(textString(''), '<FEFF>');
  assert.equal(textString('\u{1F600}'), '<FEFFD83DDE00>');
  assert.equal(literal('a(b)\\c'), '(a\\(b\\)\\\\c)');
  assert.throws(() => literal('ä'));
  assert.throws(() => literal('a\nb'));
  assert.equal(uri('https://example.org/#v=1&f=a,b'), 'https://example.org/#v=1&f=a,b');
  assert.equal(uri('https://example.org/#ä b'), 'https://example.org/#%C3%A4%20b');
  assert.equal(fnv(new Uint8Array([])), 0x811c9dc5);
  assert.equal(fnv(new Uint8Array([0x61])), 0xe40c292c);
  const glyphs = fonts();
  await assert.rejects(writePdf([], glyphs, { title: 't', lang: 'en', creator: 'c', date: NOW }, PAGE), /no page/);
  // A page without text embeds both fonts with an empty Unicode map and still parses.
  const blank = await writePdf([{ ops: '', links: [] }], { regular: use(parseFont(FONT_BYTES.regular)), bold: use(parseFont(FONT_BYTES.bold)) },
    { title: 'Ω (µ)', lang: 'de', creator: 'c', date: NOW }, PAGE);
  const read = await readPdf(blank);
  assert.equal(read.pages, 1);
  assert.equal(read.title, 'Ω (µ)');
  // A link with brackets stays one string object.
  const linked = await writePdf([{ ops: '', links: [{ x1: 0, y1: 0, x2: 10, y2: 10, url: 'https://example.org/a(b)c' }] }], glyphs,
    { title: 't', lang: 'en', creator: 'c', date: NOW }, PAGE);
  assert.deepEqual((await readPdf(linked)).links, ['https://example.org/a(b)c']);
});
