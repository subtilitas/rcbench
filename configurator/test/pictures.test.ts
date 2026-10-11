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
  assert.deepEqual(files('ina3221'), ['ina3221']);
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
    assert.equal(withPicture, 10);
  }
  const matek = view({ functions: FUNCTIONS });
  assert.ok(!result(matek, 'https://example.invalid/', []).includes('current-sensor'));
});

test('the page holds no caption next to a picture', () => {
  const html = result(view({ functions: FUNCTIONS }), 'https://example.invalid/', []);
  assert.ok(!/<figcaption|Illustration, not a photo/.test(html));
});

// --- the drawing of the INA3221 board against the data ----------------------

const svg = readFileSync(`${DIR}ina3221.svg`, 'utf8');
const texts = (markup: string): string[] => [...markup.matchAll(/<text\b[^>]*>([^<]*)<\/text>/g)].map((m) => m[1]!);
const group = (id: string): string => {
  const at = svg.indexOf(`<g id="${id}"`);
  assert.ok(at >= 0, `the drawing has the group ${id}`);
  return svg.slice(at, svg.indexOf('</g>', at));
};

test('the INA3221 drawing names the header pins of the data, in the order of the data', () => {
  const ina = part('ina3221');
  // The header pins are the pins the data lists before the channel pads.
  const firstPad = ina.pins.findIndex((p) => /^CH\d/.test(p.label.en));
  assert.ok(firstPad > 0);
  const header = ina.pins.slice(0, firstPad).map((p) => p.label.en);
  assert.deepEqual(texts(group('ina-header')), header);
  // The sub line of the module in the wiring drawing states the same header.
  assert.equal(ina.slot.sub?.en, `header: ${header.join(' ')}`);
  // The alt text names them too, in both languages.
  const picture = ina.pictures![0]!;
  assert.equal(picture.kind, 'drawing');
  for (const lang of ['en', 'de'] as Lang[]) assert.ok(picture.alt[lang].includes(header.join(', ')), picture.alt[lang]);
});

test('the INA3221 drawing names the channels of the data and no component value', () => {
  const ina = part('ina3221');
  const channels = Number(/(\d+) channels/.exec(ina.name.en)?.[1]);
  assert.equal(channels, 3);
  const expected = Array.from({ length: channels }, (_, i) => `CH${i + 1}`);
  const all = texts(svg);
  assert.deepEqual(all.filter((x) => /^CH\d+$/.test(x)), expected);
  // Each channel pad the data wires belongs to a channel the drawing shows.
  for (const pin of ina.pins) {
    const channel = /^(CH\d+), pad [12]$/.exec(pin.label.en)?.[1];
    if (channel !== undefined) assert.ok(expected.includes(channel), pin.label.en);
  }
  // The address bridge of the data: pad A0, bridged to one of the header nets.
  const header = texts(group('ina-header'));
  const bridge = texts(group('ina-bridge'));
  assert.equal(bridge[0], 'A0');
  assert.ok(ina.pins.some((p) => p.id === 'A0'));
  assert.deepEqual(bridge.slice(1).filter((x) => !header.includes(x)), []);
  // The data states no shunt value and no part value, so the drawing prints none.
  const known = new Set([...header, ...expected, ...texts(group('ina-large')), 'INA', '3221']);
  assert.deepEqual(all.filter((x) => !known.has(x)), []);
  assert.ok(!/\d\s*(Ω|ohm|R\d|mΩ|µF|uF|nF|k\b)/i.test(all.join(' ')), all.join(' '));
  assert.equal(texts(group('ina-large')).length, 3);
  assert.equal(`${ina.short.en}`, 'INA3221');
});

// --- the figure of the picture folder in the README -------------------------

test('README.md states the file count and the size of the picture folder', () => {
  const names = readdirSync(DIR);
  const bytes = names.reduce((sum, name) => sum + statSync(`${DIR}${name}`).size, 0);
  const readme = readFileSync(fileURLToPath(new URL('../README.md', import.meta.url)), 'utf8');
  const line = `Picture folder: ${names.length} files, ${(bytes / 1000).toFixed(1)} kB.`;
  assert.ok(readme.includes(line), `README.md does not hold the line "${line}"`);
});
