// The document model of the PDF export, over every distinct setup of the
// sweep in both languages, and at its limits.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Catalogue, Lang } from '../../contract.ts';
import { CATALOGUE, resolve } from '../../src/core.ts';
import { missing } from '../../src/export/font.ts';
import { SECTION_ORDER, boldRuns, documentModel, fileName, isoDate, shortHash, strings } from '../../src/export/model.ts';
import type { Block, Doc } from '../../src/export/model.ts';
import { LINE_WORDS, PDF_STRINGS } from '../../src/export/text.ts';
import { LEGEND, STRINGS, t, tx } from '../../src/i18n.ts';
import { describe } from '../geometry.ts';
import { EMPTY, LANGS, LARGEST, NOW, SAMPLES, SMALLEST, fonts, urlOf, viewOf } from './helpers.ts';

type Kind = Block['kind'];
const blocks = (doc: Doc, id: string): Block[] => doc.sections.find((s) => s.id === id)?.blocks ?? [];
const one = <K extends Kind>(doc: Doc, id: string, kind: K): Extract<Block, { kind: K }> => {
  const found = blocks(doc, id).filter((b) => b.kind === kind);
  assert.equal(found.length, 1, `${id} holds ${found.length} ${kind} blocks`);
  return found[0] as Extract<Block, { kind: K }>;
};
const lists = (doc: Doc, id: string): Array<Extract<Block, { kind: 'list' }>> =>
  blocks(doc, id).filter((b) => b.kind === 'list') as Array<Extract<Block, { kind: 'list' }>>;
const plain = (text: string): string => text.replace(/<\/?b>/g, '');
const model = (lang: Lang, sample = LARGEST, catalogue: Catalogue = CATALOGUE): Doc => {
  const view = viewOf(sample.selection, sample.setup, lang, catalogue);
  return documentModel(view, urlOf(view), NOW);
};

test('the sweep holds setups from the base setup to the full bench', () => {
  assert.ok(SAMPLES.length > 1000);
  assert.ok(SMALLEST.setup.wires.length < LARGEST.setup.wires.length);
});

test('every setup of the sweep gives the sections in order, every part, every wire and every safety note, in both languages', () => {
  const glyphs = { regular: fonts().regular.font, bold: fonts().bold.font };
  const texts = new Set<string>();
  const findings: string[] = [];
  const stub = (): string => '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"/>';
  for (const sample of SAMPLES) {
    for (const lang of LANGS) {
      const { setup } = sample;
      const view = viewOf(sample.selection, setup, lang);
      const doc = documentModel(view, urlOf(view), NOW, stub);
      const say = (what: string): void => {
        findings.push(`${describe(sample.selection)} ${lang}: ${what}`);
      };
      const want = SECTION_ORDER.filter((id) => id !== 'nir' || CATALOGUE.notInRelease.length > 0);
      if (doc.sections.map((s) => s.id).join() !== want.join()) say(`sections ${doc.sections.map((s) => s.id).join()}`);

      const first = blocks(doc, 'title');
      if (first[0]?.kind !== 'title') say('the document does not start with its title');
      if (!first.some((b) => b.kind === 'link' && b.url === urlOf(view))) say('no share link');
      if (!first.some((b) => b.kind === 'para' && b.runs[0]?.text.includes(CATALOGUE.release))) say('no release');
      if (!first.some((b) => b.kind === 'para' && b.runs[0]?.text.includes('2026-03-04'))) say('no date');

      const safety = blocks(doc, 'safety')[0];
      if (safety?.kind !== 'safety' || blocks(doc, 'safety').length !== 1) say('the safety section is not one safety block');
      else {
        const got = safety.notes.map((n) => n.map((r) => r.text).join(''));
        const full = CATALOGUE.safety.map((s) => plain(tx(lang, s.text)));
        if (got.join('\n') !== full.join('\n')) say('the safety notes differ from the catalogue');
      }

      const parts = lists(doc, 'parts')[0];
      if (!parts || parts.items.length !== setup.parts.length) say('the parts list does not hold every part');
      else {
        setup.parts.forEach((r, i) => {
          const item = parts.items[i];
          if (!item?.runs[0]?.text.startsWith(tx(lang, r.part.name))) say(`part ${r.part.id} is out of place`);
          if (r.part.hardware === 'not-run' && !item?.tags.includes(t(lang, 'nrh'))) say(`part ${r.part.id} lacks its mark`);
        });
      }

      const fn = lists(doc, 'functions');
      const on = setup.functions.filter((f) => f.enabled).length;
      const off = setup.functions.length - on;
      if (fn[0]?.items.length !== on + 1) say('the enabled functions differ');
      if (off > 0) {
        if (fn[1]?.items.length !== off) say('the functions that are not enabled differ');
        else if (fn[1].items.some((item) => item.lines.length === 0)) say('a function that is not enabled names nothing that is missing');
      } else if (fn.length !== 1) say('a list of functions that are not enabled without one');

      const wiring = blocks(doc, 'wiring');
      const kinds = wiring.map((b) => b.kind).join();
      if (kinds !== (setup.wires.length > 0 ? 'heading,legend,drawing,para,table' : 'heading,legend,drawing,para,para')) say(`wiring blocks ${kinds}`);
      const table = wiring.find((b) => b.kind === 'table');
      if (table?.kind === 'table') {
        const numbers = [...setup.wires].map((w) => w.n).sort((a, b) => a - b);
        if (table.rows.map((r) => r.n).join() !== numbers.join()) say('the table rows differ from the wires');
        if (new Set(numbers).size !== numbers.length) say('a wire number repeats');
        if (table.head.length !== 6) say('the table head has not 6 columns');
        for (const row of table.rows) {
          if (row.from === '' || row.to === '' || row.net === '' || row.line !== LINE_WORDS[row.cls][lang === 'de' ? 1 : 0]) say(`row ${row.n} lacks a cell`);
        }
      }

      const order = lists(doc, 'order');
      const staged = new Set<number>();
      for (const item of order[0]?.items ?? []) {
        for (const line of item.lines) {
          if (line.text.startsWith(t(lang, 'wiresN'))) line.text.slice(t(lang, 'wiresN').length).split(', ').forEach((n) => staged.add(Number(n)));
        }
      }
      if (staged.size !== setup.wires.length || setup.wires.some((w) => !staged.has(w.n))) say('the stages do not hold every wire once');
      if (setup.checks.length > 0 && order[1]?.items.length !== setup.checks.length) say('the checks differ');

      if (doc.fileName !== fileName(view) || !/^rcbench-setup-[0-9a-f]{8}-(en|de)\.pdf$/.test(doc.fileName)) say(`file name ${doc.fileName}`);
      for (const s of strings(doc)) texts.add(s);
    }
  }
  assert.deepEqual(findings.slice(0, 10), []);
  // Every character of every document has a glyph in both font files.
  const all = [...texts].join('');
  assert.deepEqual(missing(glyphs.regular, all), []);
  assert.deepEqual(missing(glyphs.bold, all), []);
});

test('the fonts hold the unit signs and symbols of the catalogue texts', () => {
  const signs = 'ÄÖÜäöüßΩµ°−±→←↔×·≤≥≈–—„“”‚‘’…€²';
  assert.deepEqual(missing(fonts().regular.font, signs), []);
  assert.deepEqual(missing(fonts().bold.font, signs), []);
});

test('the drawing of the model is the drawing module output', () => {
  const marker = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><rect width="10" height="10"/></svg>';
  const view = viewOf(LARGEST.selection, LARGEST.setup, 'en');
  const swapped = documentModel(view, urlOf(view), NOW, () => marker);
  assert.equal(one(swapped, 'wiring', 'drawing').svg, marker);
  assert.ok(one(model('en'), 'wiring', 'drawing').svg.includes('class="drawing"'));
});

test('the legend names every signal class with a colour and a pattern in words', () => {
  for (const lang of LANGS) {
    const legend = one(model(lang), 'wiring', 'legend');
    assert.deepEqual(legend.entries.map((e) => e.cls), LEGEND.map((l) => l[0]));
    for (const e of legend.entries) {
      assert.ok(e.label.length > 0);
      assert.ok(e.line.includes(','), `${e.cls}: ${e.line}`);
    }
  }
  const words = Object.values(LINE_WORDS);
  assert.equal(new Set(words.map((w) => w[0])).size, words.length, 'two signal classes share their English words');
  assert.equal(new Set(words.map((w) => w[1])).size, words.length, 'two signal classes share their German words');
});

test('the empty selection gives a full document', () => {
  const setup = resolve(CATALOGUE, EMPTY);
  for (const lang of LANGS) {
    const view = viewOf(EMPTY, setup, lang);
    const doc = documentModel(view, urlOf(view), NOW);
    assert.equal(one(doc, 'safety', 'safety').notes.length, CATALOGUE.safety.length);
    assert.equal(lists(doc, 'functions')[0]?.items.length, 1);
    assert.equal(lists(doc, 'functions')[1]?.items.length, CATALOGUE.functions.length);
    assert.equal(lists(doc, 'parts')[0]?.items.length, setup.parts.length);
  }
});

test('a setup without a part, a wire or a check says so and invents nothing', () => {
  const bare = { ...LARGEST.setup, parts: [], wires: [], checks: [] };
  for (const lang of LANGS) {
    const view = viewOf(LARGEST.selection, bare, lang);
    const doc = documentModel(view, urlOf(view), NOW);
    const texts = strings(doc);
    assert.ok(texts.includes(PDF_STRINGS.noParts[lang === 'de' ? 1 : 0]));
    assert.ok(texts.includes(PDF_STRINGS.noChecks[lang === 'de' ? 1 : 0]));
    assert.ok(texts.includes(t(lang, 'noWires')));
    assert.equal(blocks(doc, 'wiring').some((b) => b.kind === 'table'), false);
    assert.equal(lists(doc, 'parts').length, 0);
  }
});

test('a part that is in no release carries the mark of the page, and so does a part not run on hardware', () => {
  const sample = LARGEST;
  const target = sample.setup.parts[2]?.part.id as string;
  const catalogue: Catalogue = { ...CATALOGUE, parts: CATALOGUE.parts.map((part) => (part.id === target ? { ...part, release: 'main' } : part)) };
  const setup = resolve(catalogue, sample.selection);
  for (const lang of LANGS) {
    const view = viewOf(sample.selection, setup, lang, catalogue);
    const items = lists(documentModel(view, urlOf(view), NOW), 'parts')[0]?.items ?? [];
    const marked = items.filter((item) => item.tags.includes(STRINGS.nir[lang === 'de' ? 1 : 0]));
    assert.equal(marked.length, 1);
    assert.equal(items.indexOf(marked[0] as (typeof items)[number]), setup.parts.findIndex((r) => r.part.id === target));
    // The released catalogue marks no part.
    assert.equal(lists(model(lang), 'parts')[0]?.items.some((item) => item.tags.includes(STRINGS.nir[lang === 'de' ? 1 : 0])), false);
  }
});

test('the section of items in no release repeats the catalogue list and goes when the list is empty', () => {
  for (const lang of LANGS) {
    const items = lists(model(lang), 'nir')[0]?.items ?? [];
    assert.deepEqual(items.map((i) => i.runs[0]?.text), CATALOGUE.notInRelease.map((n) => tx(lang, n.text)));
    const none = model(lang, LARGEST, { ...CATALOGUE, notInRelease: [] });
    assert.deepEqual(none.sections.map((s) => s.id), SECTION_ORDER.filter((id) => id !== 'nir'));
  }
});

test('the file name follows the selection and the language', () => {
  const en = viewOf(LARGEST.selection, LARGEST.setup, 'en');
  const de = viewOf(LARGEST.selection, LARGEST.setup, 'de');
  const hash = (name: string): string => name.slice('rcbench-setup-'.length, 'rcbench-setup-'.length + 8);
  assert.equal(hash(fileName(en)), hash(fileName(de)));
  assert.ok(fileName(en).endsWith('-en.pdf'));
  assert.ok(fileName(de).endsWith('-de.pdf'));
  assert.notEqual(hash(fileName(en)), hash(fileName(viewOf(SMALLEST.selection, SMALLEST.setup, 'en'))));
  assert.equal(shortHash(''), '811c9dc5');
  assert.equal(shortHash('a'), 'e40c292c');
});

test('the date is the local calendar date, padded', () => {
  assert.equal(isoDate(new Date(2026, 0, 5, 23, 59)), '2026-01-05');
  assert.equal(isoDate(new Date(2026, 11, 31, 0, 0)), '2026-12-31');
  assert.equal(isoDate(new Date(987, 5, 1)), '0987-06-01');
});

test('bold runs follow the tags of a safety text', () => {
  assert.deepEqual(boldRuns('a <b>b</b> c'), [{ text: 'a ' }, { text: 'b', bold: true }, { text: ' c' }]);
  assert.deepEqual(boldRuns('<b>x</b>'), [{ text: 'x', bold: true }]);
  assert.deepEqual(boldRuns(''), []);
  assert.deepEqual(boldRuns('1 < 2'), [{ text: '1 < 2' }]);
});

test('the strings of the export hold the writing rules', () => {
  const banned = /!|\b(powerful|seamless|robust|blazing|comprehensive|simply|just|easy|nahtlos|mächtig)\b/i;
  const holes = (s: string): string[] => (s.match(/\{[a-z]+\}/g) ?? []).sort();
  for (const [key, pair] of [...Object.entries(PDF_STRINGS), ...Object.entries(LINE_WORDS)]) {
    assert.equal(pair.length, 2, key);
    for (const s of pair) {
      assert.ok(s.trim().length > 0, key);
      assert.ok(!banned.test(s), `${key}: ${s}`);
    }
    assert.deepEqual(holes(pair[1]), holes(pair[0]), key);
  }
});
