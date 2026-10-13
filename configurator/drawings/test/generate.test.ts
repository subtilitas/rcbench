import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Catalogue } from '../../contract.ts';
import { CATALOGUE } from '../../core/catalogue.ts';
import { malformed } from '../check.ts';
import { LANGS, anchorsText, cataloguePins, fileOf, generate, mergeAnchors, pathOf } from '../generate.ts';
import type { DrawingRecord } from '../generate.ts';
import { Dwg, LABEL_EM, labelWidth } from '../lib.ts';
import { INA3221_MAP, LIST, STRIP_ROWS, stripPoint } from '../parts.ts';

const clone = (): Catalogue => structuredClone(CATALOGUE) as Catalogue;
const out = generate(CATALOGUE);
const part = (id: string) => CATALOGUE.parts.find((p) => p.id === id)!;

test('2 runs of the generator give the same bytes', () => {
  const again = generate(clone());
  assert.equal(anchorsText(again.anchors), anchorsText(out.anchors));
  assert.deepEqual(Object.keys(again.svgs), Object.keys(out.svgs));
  for (const [path, svg] of Object.entries(out.svgs)) assert.equal(again.svgs[path], svg, path);
});

test('a change of the output does not reach the next run', () => {
  const first = generate(CATALOGUE);
  const map = first.anchors.files['ina3221.svg']!.catalogueMap!;
  map['LP']!.push('PAD9');
  map['A0'] = ['A0'];
  delete map['GND'];
  assert.notEqual(map, INA3221_MAP);
  assert.deepEqual(INA3221_MAP['LP'], ['PAD1', 'PAD2', 'PAD3']);
  assert.equal(INA3221_MAP['A0'], null);
  assert.equal(anchorsText(generate(CATALOGUE).anchors), anchorsText(out.anchors));
});

test('a pin has 1 place in every language: a pass that moves, adds or drops a pin is refused', () => {
  const a = { x: 10, y: 20, side: 'W' as const, label: 'Plus', labelId: 't-lbl-A' };
  const pins: DrawingRecord['pins'] = {};
  mergeAnchors('t', pins, { A: a }, 'en', true);
  mergeAnchors('t', pins, { A: { ...a, label: 'Plus (de)' } }, 'de', false);
  assert.deepEqual(pins, { A: { x: 10, y: 20, side: 'W', labelId: 't-lbl-A', label: { en: 'Plus', de: 'Plus (de)' } } });
  for (const [change, text] of [
    [{ x: 10.1 }, /pin A differs in de: \(10\.1, 20\)/],
    [{ y: 21 }, /pin A differs in de/],
    [{ side: 'E' as const }, /pin A differs in de/],
    [{ labelId: 't-lbl-B' }, /pin A differs in de/],
  ] as const) {
    const fresh: DrawingRecord['pins'] = {};
    mergeAnchors('t', fresh, { A: a }, 'en', true);
    assert.throws(() => mergeAnchors('t', fresh, { A: { ...a, ...change } }, 'de', false), text);
  }
  const more: DrawingRecord['pins'] = {};
  mergeAnchors('t', more, { A: a }, 'en', true);
  assert.throws(() => mergeAnchors('t', more, { A: a, B: a }, 'de', false), /pin B is drawn in de only/);
  assert.throws(() => mergeAnchors('t', more, {}, 'de', false), /pin A is not drawn in de/);
  // The drawings of the catalogue: both passes agree, so the German file holds its labels at the places of the table.
  for (const [file, rec] of Object.entries(out.anchors.files)) {
    for (const pin of Object.values(rec.pins)) {
      for (const lang of LANGS) assert.ok(out.svgs[pathOf(file, lang)]!.includes(`id="${pin.labelId}"`), `${file} ${lang} ${pin.labelId}`);
    }
  }
});

test('the breakout board carries a servo strip pin S, V and G beside every pad, inside the board', () => {
  const rec = out.anchors.files['copro--osoyoo.svg']!;
  const svg = out.svgs['copro--osoyoo.svg']!;
  const seen = new Set<string>();
  for (let pad = 1; pad <= 40; pad++) {
    const terminal = rec.pins[`pad.${pad}`]!;
    const xs = STRIP_ROWS.map((row) => {
      const p = stripPoint(pad, row);
      // In line with the screw terminal of its pad, on the side of that pad.
      assert.equal(p.y, terminal.y, `pad ${pad} ${row}`);
      assert.equal(p.side, terminal.side);
      assert.ok(svg.includes(`<circle cx="${p.x}" cy="${p.y}" r="2.6"`), `pad ${pad} ${row}: no pin drawn`);
      assert.ok(!seen.has(`${p.x} ${p.y}`));
      seen.add(`${p.x} ${p.y}`);
      return p.x;
    });
    // S next to the terminal, G next to the module.
    const inward = terminal.side === 'W' ? 1 : -1;
    assert.ok((xs[1]! - xs[0]!) * inward > 0 && (xs[2]! - xs[1]!) * inward > 0, `pad ${pad}`);
    assert.ok((xs[0]! - terminal.x) * inward > 0);
  }
  assert.equal(seen.size, 120);
  for (const bad of [0, 41, 1.5, Number.NaN]) assert.throws(() => stripPoint(bad, 'S'), /pads 1 to 40/);
  // The strips carry no anchor: the data file lists no pin for them.
  assert.deepEqual(Object.keys(rec.pins).filter((id) => !/^(pad\.\d+|can\.[HL])$/.test(id)), []);
  for (const lang of LANGS) {
    const file = out.svgs[pathOf('copro--osoyoo.svg', lang)]!;
    for (const print of ['>3V3 / 5V<', '>DC-DC<', '>S<', '>V<', '>G<']) assert.ok(file.includes(print), `${lang} ${print}`);
    // The jumper cap sits on the 5V pair (x=233), and the label names that position in the language.
    const label = lang === 'de' ? '>Kappe auf 5V<' : '>cap on 5V<';
    assert.ok(file.includes(label), `${lang} ${label}`);
    assert.ok(!/for the servo|für den Servo/.test(file), `${lang} servo wording`);
    assert.ok(file.includes('<rect x="233" y="60" width="23" height="16" rx="2" fill="#2a2c31"/>'), `${lang} cap at x=233`);
    assert.ok(!file.includes('<rect x="222" y="60"'), `${lang} cap at x=222`);
  }
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

test('the description of every file is the text of its drawing in the language of the file', () => {
  const plain = (s: string): string => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&');
  let n = 0;
  for (const entry of LIST) {
    assert.notEqual(entry.desc.en, entry.desc.de, entry.file);
    for (const lang of LANGS) {
      const path = pathOf(`${entry.file}.svg`, lang);
      const svg = out.svgs[path]!;
      const root = /^<svg[^>]*>/.exec(svg)![0];
      assert.ok(root.includes(` lang="${lang}"`) && root.includes(` xml:lang="${lang}"`), path);
      const desc = /<desc id="[^"]+">([^<]*)<\/desc>/.exec(svg)!;
      assert.equal(plain(desc[1]!), entry.desc[lang], path);
      // Not the text of the other language.
      for (const other of LANGS) if (other !== lang) assert.notEqual(plain(desc[1]!), entry.desc[other], path);
      n++;
    }
  }
  assert.equal(n, Object.keys(out.svgs).length);
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
  assert.equal(n, 177);
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

test('a pin in the pin list of the coprocessor is a pin the drawings owe: the generator refuses it, the check reports it', () => {
  assert.deepEqual(part('copro').pins, []);
  const c = clone();
  const copro = c.parts.find((p) => p.id === 'copro')!;
  copro.pins.push({ ...part('servo').pins[0]!, id: 'EXTRA' });
  for (const variant of [null, 'osoyoo']) {
    const pins = cataloguePins(c, copro, variant);
    assert.ok('EXTRA' in pins, String(variant));
    // The 40 pads and the CAN poles stay.
    assert.ok('pad.1' in pins && 'pad.40' in pins && Object.keys(pins).some((id) => id.startsWith('can.')));
    assert.equal(Object.keys(pins).length, Object.keys(cataloguePins(CATALOGUE, part('copro'), variant)).length + 1);
  }
  assert.throws(() => generate(c), /copro[^:]*: pins not drawn: EXTRA/);
  // A pin limited to the other board is owed by the drawing of that board only.
  const d = clone();
  const key = part('copro').variantName?.option;
  if (key !== undefined) {
    d.parts.find((p) => p.id === 'copro')!.pins.push({ ...part('servo').pins[0]!, id: 'ONE', only: { [key]: 'osoyoo' } as never });
    assert.ok(!('ONE' in cataloguePins(d, d.parts.find((p) => p.id === 'copro')!, null)));
    assert.ok('ONE' in cataloguePins(d, d.parts.find((p) => p.id === 'copro')!, 'osoyoo'));
  }
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

const sheet = (): Dwg => new Dwg({
  file: 'x', pfx: 'x', w: 300, h: 200, title: 'x', desc: 'x', lang: 'en',
  labels: { IN: { en: 'IN', de: 'IN' }, OUT: { en: 'OUT', de: 'OUT' } },
});

test('a lead end reaches a pin directly or over a chain of leads', () => {
  const d = sheet();
  d.axial(40, 60, 120, 60);
  d.bare(120, 60, 160, 60);
  d.axial(160, 60, 260, 60);
  d.eye(40, 60);
  d.eye(260, 60);
  // No pin: every end and both eyelets are loose.
  assert.deepEqual(d.loose(), ['40,60', '120,60', '160,60', '260,60']);
  d.pin('IN', 40, 60, 'W', 20, 40);
  // 1 pin at the first end connects the whole chain.
  assert.deepEqual(d.loose(), []);
});

test('a lead beside a pin, an eyelet without a lead and a second chain are loose', () => {
  const d = sheet();
  d.axial(40, 60, 260, 60);
  d.pin('IN', 40, 60, 'W', 20, 40);
  d.pin('OUT', 260, 60, 'E', 280, 40, { a: 'end' });
  assert.deepEqual(d.loose(), []);
  // 0.5 px beside the wire point of a pin is not at the pin.
  d.axial(40.5, 150, 200, 150);
  assert.deepEqual(d.loose(), ['40.5,150', '200,150']);
  d.eye(150, 20);
  assert.deepEqual(d.loose(), ['40.5,150', '200,150', '150,20']);
});

test('a lead end is compared with a pin at the 1 decimal the anchor is stored with', () => {
  const dwg = (): Dwg => new Dwg({ file: 'x', pfx: 'x', w: 200, h: 100, title: 't', desc: 'd', lang: 'en', labels: { A: { en: 'A', de: 'A' }, B: { en: 'B', de: 'B' } } });
  // Pin and lead at the same place with 2 decimals: the anchor is (10, 20), the lead end reaches it.
  const d = dwg();
  d.pin('A', 10.04, 20.04, 'W', 40, 40);
  d.pin('B', 150.04, 20.04, 'E', 120, 40);
  d.bare(10.04, 20.04, 80.26, 20.04);
  d.bare(80.26, 20.04, 150.04, 20.04);
  d.eye(10.04, 20.04);
  assert.deepEqual(d.anchors['A'] && [d.anchors['A'].x, d.anchors['A'].y], [10, 20]);
  assert.deepEqual(d.loose(), []);
  // Just inside the same tenth on both sides of it, and a chain joined at a rounded point.
  const e = dwg();
  e.pin('A', 10, 20, 'W', 40, 40);
  e.bare(10.04, 19.96, 80.24, 20);
  e.bare(80.16, 20, 120, 20.04);
  e.pin('B', 120.04, 20, 'E', 120, 40);
  assert.deepEqual(e.loose(), []);
  // The next tenth is another place: 0.06 px off rounds to 10.1.
  const g = dwg();
  g.pin('A', 10, 20, 'W', 40, 40);
  g.bare(10.06, 20, 80, 20);
  assert.deepEqual(g.loose(), ['10.1,20', '80,20']);
});

test('the generator refuses a drawing with a lead that reaches no pin', () => {
  const tap = LIST.find((e) => e.file === 'tap')!;
  const draw = tap.draw;
  try {
    (tap as { draw: typeof draw }).draw = (d) => { draw(d); d.add(d.axial(96, 110, 204, 110)); };
    assert.throws(() => generate(CATALOGUE), /tap: lead ends that reach no pin: 96,110 204,110/);
  } finally {
    (tap as { draw: typeof draw }).draw = draw;
  }
  assert.doesNotThrow(() => generate(CATALOGUE));
});

test('the 2 socket labels of the T-plug stand side by side above the board and do not meet, in both languages', () => {
  const rec = out.anchors.files['usbtap.svg']!;
  assert.deepEqual([rec.pins['IN']!.x, rec.pins['IN']!.y, rec.pins['IN']!.side], [28, 80, 'W']);
  assert.deepEqual([rec.pins['OUT']!.x, rec.pins['OUT']!.y, rec.pins['OUT']!.side], [232, 80, 'E']);
  for (const lang of LANGS) {
    const svg = out.svgs[pathOf('usbtap.svg', lang)]!;
    const at = (id: string): { x: number; y: number; anchor: string; size: number; text: string } => {
      const tag = new RegExp(`<text id="tpl-lbl-${id}"([^>]*)>([^<]*)</text>`).exec(svg)!;
      const num = (name: string): number => Number(new RegExp(` ${name}="([^"]+)"`).exec(tag[1]!)![1]);
      return { x: num('x'), y: num('y'), anchor: / text-anchor="([^"]+)"/.exec(tag[1]!)?.[1] ?? 'start', size: num('font-size'), text: tag[2]! };
    };
    const a = at('IN');
    const b = at('OUT');
    assert.equal(a.text, rec.pins['IN']!.label[lang]);
    assert.equal(b.text, rec.pins['OUT']!.label[lang]);
    assert.deepEqual([a.anchor, b.anchor], ['start', 'end']);
    assert.equal(a.y, b.y);
    // Above the board, which starts at y 30.
    assert.ok(a.y < 30);
    const endOfIn = a.x + labelWidth(a.text, a.size);
    const startOfOut = b.x - labelWidth(b.text, b.size);
    assert.ok(a.x >= 0 && b.x <= rec.viewBox[2], lang);
    assert.ok(startOfOut - endOfIn >= 4, `${lang}: ${(startOfOut - endOfIn).toFixed(1)} px between the 2 labels`);
  }
});

test('the T-plug drawing carries a wire point at the mouth of each USB-C socket when the data file lists IN and OUT', () => {
  // Without the 2 pins in the data file the drawing shows the print and 4 pads.
  const plain = generate(CATALOGUE).anchors.files['usbtap.svg']!;
  const listed = part('usbtap').pins.map((p) => p.id);
  assert.deepEqual(Object.keys(plain.pins).sort(), [...listed].sort());
  // With them: the order of the data file OUT, IN, G, DP, DM, V.
  const c = clone();
  const tap = c.parts.find((p) => p.id === 'usbtap')!;
  const pads = tap.pins.filter((p) => p.id !== 'IN' && p.id !== 'OUT');
  const model = pads[0]!;
  tap.pins = [
    { ...model, id: 'OUT', label: { en: 'USB-C out', de: 'USB-C aus' } },
    { ...model, id: 'IN', label: { en: 'USB-C in', de: 'USB-C ein' } },
    ...pads,
  ];
  const withPins = generate(c);
  const rec = withPins.anchors.files['usbtap.svg']!;
  assert.deepEqual(Object.keys(rec.pins).sort(), ['DM', 'DP', 'G', 'IN', 'OUT', 'V']);
  const [, , w, h] = rec.viewBox;
  assert.deepEqual([rec.pins['IN']!.x, rec.pins['IN']!.y, rec.pins['IN']!.side], [28, 80, 'W']);
  assert.deepEqual([rec.pins['OUT']!.x, rec.pins['OUT']!.y, rec.pins['OUT']!.side], [232, 80, 'E']);
  for (const id of ['IN', 'OUT']) {
    const pin = rec.pins[id]!;
    assert.ok(pin.x > 0 && pin.x < w && pin.y > 0 && pin.y < h, id);
    assert.deepEqual(pin.label, id === 'IN' ? { en: 'USB-C in', de: 'USB-C ein' } : { en: 'USB-C out', de: 'USB-C aus' });
  }
  // The label of the data file stands in the file, in its language, and the pads keep their places.
  assert.match(withPins.svgs[pathOf('usbtap.svg', 'de')]!, /<text id="tpl-lbl-IN"[^>]*>USB-C ein<\/text>/);
  assert.match(withPins.svgs[pathOf('usbtap.svg', 'en')]!, /<text id="tpl-lbl-OUT"[^>]*>USB-C out<\/text>/);
  for (const id of ['G', 'DP', 'DM', 'V']) assert.deepEqual(rec.pins[id], plain.pins[id], id);
  // 1 of the 2 pins alone is drawn too: the other socket keeps its print.
  const one = clone();
  const tapOne = one.parts.find((p) => p.id === 'usbtap')!;
  tapOne.pins = [{ ...model, id: 'IN', label: { en: 'IN', de: 'IN' } }, ...pads];
  const single = generate(one);
  assert.ok('IN' in single.anchors.files['usbtap.svg']!.pins);
  assert.ok(!('OUT' in single.anchors.files['usbtap.svg']!.pins));
  assert.match(single.svgs[pathOf('usbtap.svg', 'en')]!, />OUT<\/text>/);
  // A label that leaves the drawing is refused.
  const wide = clone();
  wide.parts.find((p) => p.id === 'usbtap')!.pins = [{ ...model, id: 'IN', label: { en: 'USB-C socket towards the charger, input', de: 'IN' } }, ...pads];
  assert.throws(() => generate(wide), /does not fit/);
});

test('the phase tap drawing shows the resistor only: 2 lead ends, both pins', () => {
  for (const lang of LANGS) {
    const svg = out.svgs[pathOf('tap.svg', lang)]!;
    // 1 axial body: 1 rotated group. The zener diode is wire 53 of the data file, not a drawn part.
    assert.equal(svg.match(/<g transform="translate\([^"]*\) rotate\(/g)?.length, 1, lang);
  }
  assert.deepEqual(Object.keys(out.anchors.files['tap.svg']!.pins).sort(), ['IN', 'OUT']);
  const zener = CATALOGUE.wires.filter((w) => w.kind === 'component');
  assert.deepEqual(zener.map((w) => [w.n, w.a, w.b]), [[53, 'pad.29', 'pad.28']]);
});
