import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Catalogue } from '../../contract.ts';
import { CATALOGUE } from '../../core/catalogue.ts';
import { malformed } from '../check.ts';
import { LANGS, anchorsText, cataloguePins, fileOf, generate, pathOf } from '../generate.ts';
import { LABEL_EM, labelWidth } from '../lib.ts';
import { LIST } from '../parts.ts';

const clone = (): Catalogue => structuredClone(CATALOGUE) as Catalogue;
const out = generate(CATALOGUE);
const part = (id: string) => CATALOGUE.parts.find((p) => p.id === id)!;

test('2 runs of the generator give the same bytes', () => {
  const again = generate(clone());
  assert.equal(anchorsText(again.anchors), anchorsText(out.anchors));
  assert.deepEqual(Object.keys(again.svgs), Object.keys(out.svgs));
  for (const [path, svg] of Object.entries(out.svgs)) assert.equal(again.svgs[path], svg, path);
});

test('the output is 1 file per drawing and language, and the table lists the files in order', () => {
  assert.equal(Object.keys(out.svgs).length, 2 * LIST.length);
  const files = Object.keys(out.anchors.files);
  assert.equal(files.length, LIST.length);
  assert.deepEqual(files, [...files].sort());
  for (const file of files) for (const lang of LANGS) assert.ok(pathOf(file, lang) in out.svgs, pathOf(file, lang));
  assert.equal(out.anchors.release, CATALOGUE.release);
});

test('no output carries a date, a path of the machine or a random number', () => {
  const text = anchorsText(out.anchors) + Object.values(out.svgs).join('');
  assert.doesNotMatch(text, /\b20\d\d-\d\d-\d\d\b/);
  assert.doesNotMatch(text, /\/home\/|[A-Z]:\\/);
});

test('every SVG is well formed and carries a title and a description in its language', () => {
  for (const [file, rec] of Object.entries(out.anchors.files)) {
    const texts: Record<string, { title: string; desc: string }> = {};
    for (const lang of LANGS) {
      const path = pathOf(file, lang);
      const svg = out.svgs[path]!;
      assert.equal(malformed(svg), null, path);
      assert.match(svg, /^<svg xmlns="http:\/\/www\.w3\.org\/2000\/svg"[^>]* role="img"/, path);
      const title = svg.match(/<title id="([^"]+)">([^<]+)<\/title>/);
      const desc = svg.match(/<desc id="([^"]+)">([^<]+)<\/desc>/);
      assert.ok(title && desc, path);
      assert.ok(svg.includes(`aria-labelledby="${title[1]}"`), path);
      assert.ok(svg.includes(`aria-describedby="${desc[1]}"`), path);
      assert.ok(desc[2]!.length >= 20, path);
      if (rec.part !== null) {
        const name = part(rec.part).name[lang].replace(/&/g, '&amp;');
        assert.ok(title[2]!.startsWith(name), `${path}: title "${title[2]}" starts with "${name}"`);
      }
      texts[lang] = { title: title[2]!, desc: desc[2]! };
    }
    assert.notEqual(texts.en!.desc, texts.de!.desc, `${file}: description is translated`);
  }
});

test('a drawing of 1 of 2 boards names the board in its title', () => {
  for (const lang of LANGS) {
    const svg = out.svgs[pathOf('ina228--xt90.svg', lang)]!;
    const board = part('ina228').variantName!.names['xt90']![lang];
    assert.ok(svg.includes(`, ${board}</title>`), lang);
  }
});

test('every anchor lies inside its viewBox, on a side, with a label in both languages', () => {
  let n = 0;
  for (const [file, rec] of Object.entries(out.anchors.files)) {
    const [x0, y0, w, h] = rec.viewBox;
    assert.deepEqual([x0, y0], [0, 0], file);
    assert.ok(w > 0 && h > 0, file);
    for (const [id, a] of Object.entries(rec.pins)) {
      n += 1;
      assert.ok(a.x >= 0 && a.x <= w && a.y >= 0 && a.y <= h, `${file} ${id}`);
      assert.ok(['N', 'E', 'S', 'W'].includes(a.side), `${file} ${id}`);
      assert.ok(a.label.en !== '' && a.label.de !== '', `${file} ${id}`);
      assert.ok(a.labelId.startsWith(`${LIST.find((e) => `${e.file}.svg` === file)!.pfx}-lbl-`), `${file} ${id}`);
    }
  }
  assert.equal(n, 175);
});

test('every part of the catalogue has a drawing, a part with 2 boards has 1 per board', () => {
  for (const p of CATALOGUE.parts) {
    const files = Object.entries(out.anchors.files).filter(([, r]) => r.part === p.id).map(([f]) => f);
    assert.ok(files.length >= 1, p.id);
    for (const v of Object.keys(p.variantName?.names ?? {})) assert.ok(files.includes(fileOf(p.id, v)), `${p.id} ${v}`);
  }
});

test('the 2 parts outside the catalogue are drawn and marked as not wired', () => {
  for (const file of ['ina219.svg', 'adxl345.svg']) {
    const rec = out.anchors.files[file]!;
    assert.equal(rec.part, null);
    assert.equal(rec.names, 'board print');
    assert.match(out.svgs[file]!, /<title id="[^"]+">[^<]*not wired by this release<\/title>/);
    assert.match(out.svgs[`de/${file}`]!, /<title id="[^"]+">[^<]*nicht verdrahtet<\/title>/);
  }
  assert.deepEqual(Object.values(out.anchors.files).filter((r) => r.part === null).length, 2);
});

test('the coprocessor carries 40 pads and the 2 poles of the CAN terminal, with the print of each board', () => {
  const copro = part('copro');
  const bare = cataloguePins(CATALOGUE, copro, null);
  const carrier = cataloguePins(CATALOGUE, copro, 'osoyoo');
  assert.equal(Object.keys(bare).length, 42);
  assert.deepEqual(Object.keys(bare), Object.keys(carrier));
  for (const pad of CATALOGUE.pads) {
    assert.equal(bare[`pad.${pad.n}`]!.en, pad.name);
    assert.equal(carrier[`pad.${pad.n}`]!.en, pad.breakoutLabel);
  }
  assert.ok('can.H' in bare && 'can.L' in bare);
});

test('a pin limited to 1 board is on the drawing of that board only', () => {
  const c = clone();
  const ina = c.parts.find((p) => p.id === 'ina228')!;
  const pin = ina.pins.find((p) => p.id === 'G')!;
  pin.only = { ina228Board: 'xt90' };
  assert.ok(!('G' in cataloguePins(c, ina, 'matek')));
  assert.ok('G' in cataloguePins(c, ina, 'xt90'));
  // A limit on another option than the one that picks the board does not remove the pin.
  pin.only = { escPower: 'pd' };
  assert.ok('G' in cataloguePins(c, ina, 'matek'));
});

test('the generator refuses a catalogue pin that no drawing shows', () => {
  const c = clone();
  const servo = c.parts.find((p) => p.id === 'servo')!;
  servo.pins.push({ ...servo.pins[0]!, id: 'EXTRA' });
  assert.throws(() => generate(c), /servo: pins not drawn: EXTRA/);
});

test('the generator refuses a drawing whose part left the catalogue', () => {
  const c = clone();
  c.parts = c.parts.filter((p) => p.id !== 'tap');
  assert.throws(() => generate(c), /tap: part tap is not in the catalogue/);
});

test('the generator refuses a label that leaves the drawing', () => {
  const c = clone();
  const pin = c.parts.find((p) => p.id === 'estop')!.pins.find((p) => p.id === 'NC1')!;
  // The label ends at 332 px of 340 px. 0.62 em per character at 13 px: 41 characters fit, 42 do not.
  pin.label = { en: 'x'.repeat(41), de: 'x' };
  assert.doesNotThrow(() => generate(c));
  pin.label = { en: 'x', de: 'x'.repeat(42) };
  assert.throws(() => generate(c), /estop: label "x{42}" of pin NC1 does not fit: -6\.5 to 332 px of 340 px/);
  assert.equal(labelWidth('abcd', 10), 4 * 10 * LABEL_EM);
  assert.equal(labelWidth('', 10), 0);
});

test('a label with markup characters is escaped in the file and plain in the table', () => {
  const c = clone();
  const servo = c.parts.find((p) => p.id === 'servo')!;
  servo.pins[0]!.label = { en: 'A<B & "C"', de: 'A<B & "C"' };
  const o = generate(c);
  const svg = o.svgs['servo.svg']!;
  assert.equal(malformed(svg), null);
  assert.ok(svg.includes('A&lt;B &amp; &quot;C&quot;'));
  assert.equal(o.anchors.files['servo.svg']!.pins[servo.pins[0]!.id]!.label.en, 'A<B & "C"');
});
