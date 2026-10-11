// Page layout of the PDF export at its limits: line breaking, blocks that do
// not fit, the table across pages, and proof that the plan check of plan.ts
// reports a broken plan.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang } from '../../contract.ts';
import { COLUMNS, LayoutError, MARGIN, PAGE, layoutDoc, wrap } from '../../src/export/layout.ts';
import type { Laid } from '../../src/export/layout.ts';
import { documentModel } from '../../src/export/model.ts';
import type { Doc, Row } from '../../src/export/model.ts';
import { EMPTY, LANGS, LARGEST, NOW, SMALLEST, css, fonts, urlOf, viewOf } from './helpers.ts';
import { CATALOGUE, resolve } from '../../src/core.ts';
import { planFindings } from './plan.ts';

const WIDTH = PAGE[0] - 2 * MARGIN;
const model = (lang: Lang, sample = LARGEST, diagram?: () => string): Doc => {
  const view = viewOf(sample.selection, sample.setup, lang);
  return documentModel(view, urlOf(view), NOW, diagram);
};
const lay = (doc: Doc): Laid => layoutDoc(doc, css(), fonts());
const text = (lines: ReturnType<typeof wrap>): string[] => lines.map((l) => l.pieces.map((p) => p.text).join(''));
/** The document with other rows in its table. */
function withRows(doc: Doc, change: (rows: Row[]) => Row[]): Doc {
  return {
    ...doc,
    sections: doc.sections.map((s) => ({ ...s, blocks: s.blocks.map((b) => (b.kind === 'table' ? { ...b, rows: change(b.rows) } : b)) })),
  };
}

test('the table columns fill the text width', () => {
  assert.equal(COLUMNS.length, 6);
  assert.ok(Math.abs(COLUMNS.reduce((sum, w) => sum + w, 0) - WIDTH) < 0.001);
  assert.ok(COLUMNS.every((w) => w > 20));
});

test('a line breaks after a space or a break character, and inside a word only when the word is wider than the line', () => {
  const f = fonts();
  assert.deepEqual(text(wrap(f, [{ text: '' }], 10, 100)), ['']);
  assert.deepEqual(text(wrap(f, [], 10, 100)), ['']);
  assert.deepEqual(text(wrap(f, [{ text: 'one two' }], 10, 500)), ['one two']);
  assert.deepEqual(text(wrap(f, [{ text: 'one two' }], 10, 22)), ['one', 'two']);
  assert.deepEqual(text(wrap(f, [{ text: 'left-right' }], 10, 30)), ['left-', 'right']);
  // A no-break space holds its words together when the line has room for both.
  assert.deepEqual(text(wrap(f, [{ text: 'a 30 cm b' }], 10, 34)), ['a', '30 cm', 'b']);
  // No room for one character: every character gets a line and the loop ends.
  assert.deepEqual(text(wrap(f, [{ text: 'abc' }], 10, 0.1)), ['a', 'b', 'c']);
  const long = wrap(f, [{ text: 'W'.repeat(40) }], 10, 100);
  assert.ok(long.length > 1);
  assert.equal(text(long).join(''), 'W'.repeat(40));
  for (const line of long) assert.ok(line.width <= 100);
  // A line feed counts as a space.
  assert.deepEqual(text(wrap(f, [{ text: 'a\nb' }], 10, 500)), ['a b']);
  // An address breaks after its own set of characters.
  const url = wrap(f, [{ text: 'https://example.org/rcbench/#v=1&f=aaaa,bbbb,cccc,dddd&p=eeee,ffff' }], 10, 120, '-/_&,=?#.');
  assert.ok(url.length > 1);
  for (const line of url) assert.ok(line.width <= 120);
});

test('a line never exceeds its width and keeps every character, with bold runs', () => {
  const f = fonts();
  const runs = [{ text: 'Supply of the ESC from ' }, { text: 'two sources only:', bold: true }, { text: ' A, its own battery, or B, 1 to 20 V, 3 A.' }];
  for (const max of [40, 80, 160, 320, 510]) {
    const lines = wrap(f, runs, 9.5, max);
    for (const line of lines) assert.ok(line.width <= max + 1e-9, `${max}: ${line.width}`);
    assert.equal(text(lines).join(' '), runs.map((r) => r.text).join(''));
    assert.ok(lines.some((l) => l.pieces.some((p) => p.bold)));
  }
});

test('the largest setup breaks its table across pages with the header on each', () => {
  for (const lang of LANGS) {
    const doc = model(lang);
    const laid = lay(doc);
    assert.deepEqual(planFindings(doc, laid), []);
    const withRows = laid.plan.filter((page) => page.some((b) => b.kind === 'row'));
    assert.ok(withRows.length >= 2, `${lang}: the table is on ${withRows.length} pages`);
    for (const page of withRows) assert.equal(page.filter((b) => b.kind === 'head').length, 1);
  }
});

test('a table of 400 rows keeps every row whole and repeats the header on every page', () => {
  const base = model('de');
  const doc = withRows(base, (rows) => Array.from({ length: 400 }, (_, i) => ({ ...(rows[i % rows.length] as Row), n: i + 1 })));
  const laid = lay(doc);
  assert.deepEqual(planFindings(doc, laid), []);
  const pages = laid.plan.filter((page) => page.some((b) => b.kind === 'row'));
  assert.ok(pages.length >= 10);
  assert.equal(laid.plan.flat().filter((b) => b.kind === 'head').length, pages.length);
});

test('a table of 1 row and a document without a wire lay out', () => {
  const one = withRows(model('en'), (rows) => rows.slice(0, 1));
  assert.deepEqual(planFindings(one, lay(one)), []);
  const bare = { ...LARGEST.setup, wires: [], parts: [], checks: [] };
  for (const lang of LANGS) {
    const view = viewOf(LARGEST.selection, bare, lang);
    const doc = documentModel(view, urlOf(view), NOW);
    const laid = lay(doc);
    assert.deepEqual(planFindings(doc, laid), []);
    assert.equal(laid.plan.flat().filter((b) => b.kind === 'head' || b.kind === 'row').length, 0);
  }
});

test('the empty selection and the smallest setup lay out in both languages', () => {
  const setup = resolve(CATALOGUE, EMPTY);
  for (const lang of LANGS) {
    const view = viewOf(EMPTY, setup, lang);
    const doc = documentModel(view, urlOf(view), NOW);
    assert.deepEqual(planFindings(doc, lay(doc)), []);
    const small = model(lang, SMALLEST);
    assert.deepEqual(planFindings(small, lay(small)), []);
  }
});

test('a row taller than a page is an error, not a split row', () => {
  const doc = withRows(model('en'), (rows) => [{ ...(rows[0] as Row), note: 'word '.repeat(9000) }, ...rows.slice(1)]);
  assert.throws(() => lay(doc), (error: unknown) => error instanceof LayoutError && /row .* tall/.test(error.message));
  // A row of about 670 pt fits a page of 725 pt and is placed whole.
  const fits = withRows(model('en'), (rows) => [{ ...(rows[0] as Row), note: 'word '.repeat(1300) }, ...rows.slice(1)]);
  const laid = lay(fits);
  assert.deepEqual(planFindings(fits, laid), []);
  const tallest = Math.max(...laid.plan.flat().filter((b) => b.kind === 'row').map((b) => b.h));
  assert.ok(tallest > 600 && tallest <= PAGE[1] - MARGIN - laid.floor, `${tallest} pt`);
});

test('a drawing taller or wider than the page is scaled to one page', () => {
  const tall = (): string => '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 9000"><rect class="box" width="100" height="9000"/></svg>';
  const wide = (): string => '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 9000 100"><rect class="box" width="9000" height="100"/></svg>';
  const small = (): string => '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><rect class="box" width="10" height="10"/></svg>';
  for (const [diagram, limit] of [[tall, 'h'], [wide, 'w'], [small, 'w']] as const) {
    const doc = model('en', LARGEST, diagram);
    const laid = lay(doc);
    assert.deepEqual(planFindings(doc, laid), []);
    const placed = laid.plan.flat().find((b) => b.kind === 'drawing');
    assert.ok(placed);
    if (limit === 'h') assert.ok(Math.abs(placed.h - (PAGE[1] - MARGIN - laid.floor)) < 0.01);
    else assert.ok(Math.abs((laid.drawing?.width ?? 0) * laid.drawingScale - WIDTH) < 0.01);
  }
});

test('the plan check reports a row that is missing, a header that is missing, a block in the footer and a text outside its box', () => {
  const doc = model('de');
  const laid = lay(doc);
  assert.deepEqual(planFindings(doc, laid), []);
  const copy = (): Laid => ({ ...laid, plan: laid.plan.map((page) => page.map((b) => ({ ...b }))), drawing: laid.drawing ? { ...laid.drawing, texts: laid.drawing.texts.map((x) => ({ ...x })) } : null });
  const pageOf = (l: Laid, kind: string): number => l.plan.findIndex((page) => page.some((b) => b.kind === kind));

  const noRow = copy();
  const p1 = noRow.plan[pageOf(noRow, 'row')] as Laid['plan'][number];
  p1.splice(p1.findIndex((b) => b.kind === 'row'), 1);
  assert.ok(planFindings(doc, noRow).some((f) => f.includes('every row once')));

  const noHead = copy();
  const p2 = noHead.plan[pageOf(noHead, 'head')] as Laid['plan'][number];
  p2.splice(p2.findIndex((b) => b.kind === 'head'), 1);
  assert.ok(planFindings(doc, noHead).some((f) => f.includes('table headers')));

  const low = copy();
  const p3 = low.plan[0] as Laid['plan'][number];
  (p3[p3.length - 1] as (typeof p3)[number]).h += 2000;
  assert.ok(planFindings(doc, low).some((f) => f.includes('reaches into the footer')));

  const over = copy();
  const p4 = over.plan[1] as Laid['plan'][number];
  (p4[1] as (typeof p4)[number]).y += 30;
  assert.ok(planFindings(doc, over).some((f) => f.includes('overlaps')));

  const wideText = copy();
  const label = wideText.drawing?.texts.find((x) => x.classes.includes('ms'));
  assert.ok(label);
  label.x2 += 400;
  assert.ok(planFindings(doc, wideText).some((f) => f.includes('outside its box')));

  const lost = copy();
  (lost.plan[lost.plan.length - 1] as Laid['plan'][number]).push({ section: 'safety', kind: 'text', y: 100, h: 5 });
  assert.ok(planFindings(doc, lost).some((f) => f.includes('follows a later section')));
});

test('the drawing of the largest setup keeps its text at 5 pt or above', () => {
  for (const lang of LANGS) {
    const laid = lay(model(lang));
    const smallest = Math.min(...(laid.drawing?.texts ?? []).map((x) => x.size));
    assert.ok(smallest * laid.drawingScale >= 5, `${lang}: ${(smallest * laid.drawingScale).toFixed(2)} pt`);
  }
});
