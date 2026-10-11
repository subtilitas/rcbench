import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import type { Lang, Options, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { optionsOf } from '../src/names.ts';
import type { View } from '../src/view/chooser.ts';
import { PICTURES, picturesOf, result } from '../src/view/result.ts';

const DIR = fileURLToPath(new URL('../public/pictures/', import.meta.url));
const FUNCTIONS = CATALOGUE.functions.map((f) => f.id);

function view(part: Partial<Selection>, lang: Lang = 'en'): View {
  const selection: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS }, ...part };
  const setup = resolve(CATALOGUE, selection);
  return { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
}

/** The part cards of a result: part name to the markup of its list item. */
function cards(v: View): Map<string, string> {
  const html = result(v, 'https://example.invalid/', []);
  const list = /<ul class="parts">(.*?)<\/ul>/s.exec(html)?.[1] ?? '';
  const out = new Map<string, string>();
  for (const item of list.split('</li>').filter((x) => x !== '')) {
    out.set(/<b>(.*?)<\/b>/.exec(item)?.[1] ?? '', item);
  }
  return out;
}

const part = (id: string) => CATALOGUE.parts.find((p) => p.id === id)!;
const files = (id: string, options: Partial<Options> = {}): string[] =>
  picturesOf(part(id), { ...DEFAULT_OPTIONS, ...options }).map((p) => p.file);

test('each part shows the pictures of its variant, the main one first', () => {
  assert.deepEqual(files('panel'), ['01-panel-back']);
  assert.deepEqual(files('pdmini'), ['02-pd-mini']);
  assert.deepEqual(files('usbtap'), ['04-usb-c-tap-closeup']);
  assert.deepEqual(files('copro'), ['08-rp2350-can-b']);
  assert.deepEqual(files('copro', { connection: 'osoyoo' }), ['05-rp2350-can-breakout', '08-rp2350-can-b']);
  assert.deepEqual(files('as5600'), ['10-as5600-b']);
  assert.deepEqual(files('estop'), ['11-estop-front', '12-estop-side']);
  assert.deepEqual(files('ina228'), []);
  assert.deepEqual(files('ina228', { ina228Board: 'xt90' }), ['15-current-sensor-top', '14-current-sensor-oblique']);
  assert.deepEqual(files('ina3221'), ['16-ina3221-front', '17-ina3221-back']);
  const withPictures = CATALOGUE.parts.filter((p) => (p.pictures ?? []).length > 0).map((p) => p.id);
  assert.deepEqual(withPictures, ['panel', 'copro', 'pdmini', 'ina3221', 'as5600', 'ina228', 'estop', 'usbtap']);
});

test('a card with a picture links the thumb to the full file, a card without one has no image and no frame', () => {
  for (const lang of ['en', 'de'] as Lang[]) {
    const v = view({ functions: FUNCTIONS, options: { ...DEFAULT_OPTIONS, ina228Board: 'xt90' } }, lang);
    let withPicture = 0;
    for (const r of v.setup.parts) {
      const shown = picturesOf(r.part, v.options);
      const card = [...cards(v)].find(([name]) => name.startsWith(r.part.name[lang].replaceAll('&', '&amp;')))?.[1];
      assert.ok(card !== undefined, r.part.id);
      const images = [...card.matchAll(/<img [^>]*>/g)].map((m) => m[0]);
      assert.equal(images.length, shown.length, r.part.id);
      assert.ok(!card.includes('class="ph"'), r.part.id);
      assert.equal(card.includes('class="pics"'), shown.length > 0, r.part.id);
      shown.forEach((p, i) => {
        withPicture += 1;
        const img = images[i]!;
        const thumb = p.kind === 'photo' ? `${p.file}-thumb.webp` : `${p.file}.svg`;
        const full = p.kind === 'photo' ? `${p.file}.webp` : `${p.file}.svg`;
        assert.ok(img.includes(` src="${PICTURES}${thumb}"`), img);
        assert.ok(img.includes(` width="${p.width}" height="${p.height}"`), img);
        assert.ok(img.includes(' loading="lazy"') && img.includes(' decoding="async"'), img);
        assert.ok(card.includes(`<a href="${PICTURES}${full}">${img}</a>`), card);
        const alt = / alt="([^"]*)"/.exec(img)?.[1] ?? '';
        assert.ok(alt.length > 20, img);
        assert.equal(alt, p.alt[lang].replaceAll('&', '&amp;'));
      });
    }
    // 8 parts carry a picture; the MATEK variant of the INA228 is not among the files.
    assert.equal(withPicture, 11);
  }
  const matek = view({ functions: FUNCTIONS });
  assert.ok(!result(matek, 'https://example.invalid/', []).includes('current-sensor'));
});

test('the page holds no caption next to a picture', () => {
  const html = result(view({ functions: FUNCTIONS }), 'https://example.invalid/', []);
  assert.ok(!/<figcaption|Illustration, not a photo/.test(html));
});

// --- the photos of the INA3221 board against the data ------------------------

test('the INA3221 photos and the data name the same header, bridge and pads', () => {
  const ina = part('ina3221');
  const [front, back] = ina.pictures!;
  assert.deepEqual([front!.kind, back!.kind], ['photo', 'photo']);
  // The header as the back of the board prints it, from the square hole.
  const header = ['VS', 'GND', 'SCL', 'SDA', 'PV', 'CRI', 'WAR', 'TC'];
  assert.deepEqual(ina.slot.sub, { en: header.join(' '), de: header.join(' ') });
  // The pins the wires use are the first 4 holes, in the order of the board.
  assert.deepEqual(ina.pins.slice(0, 4).map((p) => p.label.en), header.slice(0, 4));
  for (const lang of ['en', 'de'] as Lang[]) {
    assert.ok(ina.role![lang].includes(header.join(', ')), ina.role![lang]);
    assert.ok(back!.alt[lang].includes(header.join(', ')), back!.alt[lang]);
    assert.ok(front!.alt[lang].includes('GND, SDA, SCL, VS'), front!.alt[lang]);
    for (const alt of [front!.alt[lang], back!.alt[lang]]) assert.ok(alt.includes('GND, GND, VPU'), alt);
  }
  // A0 is no header pin. The data names it as the solder bridges it is.
  assert.ok(!header.includes('A0'));
  const a0 = ina.pins.find((p) => p.id === 'A0')!;
  assert.deepEqual(a0.label, { en: 'A0 solder bridges', de: 'A0-Lötbrücken' });
  const strap = CATALOGUE.wires.find((w) => w.a === 'ina3221.A0')!;
  assert.equal(strap.kind, 'stub');
  assert.ok(strap.to?.en.startsWith('GND, solder bridge'));
});

test('the 2 pads of channel 1 of the INA3221 carry their print: VIN1+ takes the supply, VIN1- the servo', () => {
  const ina = part('ina3221');
  const label = (id: string) => ina.pins.find((p) => p.id === id)!.label;
  assert.deepEqual(label('C1A'), { en: 'VIN1+', de: 'VIN1+' });
  assert.deepEqual(label('C1B'), { en: 'VIN1-', de: 'VIN1-' });
  // The supply side: the PD mini output or the servo supply. The load side: the plus of the servo.
  const on = (pin: string) => CATALOGUE.wires.filter((w) => w.b === `ina3221.${pin}`).map((w) => [w.n, w.a]);
  assert.deepEqual(on('C1A'), [[28, 'pdmini.OP'], [29, 'ssup.POS']]);
  assert.deepEqual(on('C1B'), [[30, 'servo.POS']]);
  const shunt = CATALOGUE.wires.find((w) => w.b === 'ina3221.C1B')!;
  assert.ok(shunt.note!.en.includes('VIN1+ and VIN1-'));
  assert.ok(shunt.note!.en.includes('The supply goes to VIN1+, the servo to VIN1-.'));
  assert.ok(shunt.note!.de.includes('Die Versorgung geht an VIN1+, das Servo an VIN1-.'));
  // The 3 large pads carry their printed names and stay unconfirmed and unconnected.
  const large = ina.pins.find((p) => p.id === 'LP')!;
  assert.equal(large.confirmed, false);
  assert.equal(large.label.en, 'GND, GND, VPU pads');
  const open = CATALOGUE.wires.find((w) => w.a === 'ina3221.LP')!;
  assert.equal(open.b, null);
  assert.ok(open.note!.en.startsWith('Not connected.'));
  // Every pin of the board comes from the owner's photos of 2026-10-11.
  for (const pin of ina.pins) assert.deepEqual(pin.src, { kind: 'owner', date: '2026-10-11' });
});

test('no part uses a drawing and the folder holds no SVG file', () => {
  assert.deepEqual(CATALOGUE.parts.flatMap((p) => p.pictures ?? []).filter((p) => p.kind !== 'photo'), []);
  assert.deepEqual(readdirSync(DIR).filter((name) => !name.endsWith('.webp')), []);
});

// --- the figure of the picture folder in the README -------------------------

test('README.md states the file count and the size of the picture folder', () => {
  const names = readdirSync(DIR);
  const bytes = names.reduce((sum, name) => sum + statSync(`${DIR}${name}`).size, 0);
  const readme = readFileSync(fileURLToPath(new URL('../README.md', import.meta.url)), 'utf8');
  const line = `Picture folder: ${names.length} files, ${(bytes / 1000).toFixed(1)} kB.`;
  assert.ok(readme.includes(line), `README.md does not hold the line "${line}"`);
});
