import assert from 'node:assert/strict';
import { test } from 'node:test';
import { CATALOGUE } from '../../core/catalogue.ts';
import { generate } from '../generate.ts';
import type { DrawingRecord } from '../generate.ts';
import {
  BACKGROUNDS, BY_CLASS, BY_NET, MIN_RATIO, PALETTE, colourOf, endPoints, luminance, neighbours, ratio, wireRatio,
  withoutWirePoint,
} from '../wires.ts';

const { anchors } = generate(CATALOGUE);

// Contrast ratio of each colour on: light surface, light page, dark surface, dark page, drawing card.
// Figures of the palette table in the README, 2 decimals.
const FIGURES: Record<string, number[]> = {
  red: [5.02, 4.60, 3.44, 3.77, 4.22],
  black: [17.64, 16.17, 11.21, 12.28, 14.83],
  white: [17.64, 16.17, 15.83, 17.34, 14.83],
  orange: [3.78, 3.47, 4.56, 5.00, 3.18],
  ochre: [3.84, 3.52, 4.49, 4.92, 3.23],
  olive: [3.93, 3.60, 4.40, 4.82, 3.30],
  green: [3.92, 3.59, 4.41, 4.83, 3.30],
  teal: [3.86, 3.54, 4.47, 4.90, 3.25],
  cyan: [3.81, 3.49, 4.53, 4.97, 3.20],
  blue: [4.22, 3.87, 4.09, 4.48, 3.55],
  violet: [3.98, 3.65, 4.33, 4.75, 3.35],
  magenta: [4.12, 3.78, 4.19, 4.59, 3.47],
  pink: [3.86, 3.54, 4.47, 4.90, 3.25],
  brown: [4.64, 4.26, 3.72, 4.07, 3.91],
  grey: [3.85, 3.53, 4.49, 4.91, 3.24],
};

test('the contrast formula matches the reference values of WCAG', () => {
  assert.equal(luminance('#000000'), 0);
  assert.ok(Math.abs(luminance('#ffffff') - 1) < 1e-12);
  assert.ok(Math.abs(ratio('#000000', '#ffffff') - 21) < 1e-9);
  assert.equal(ratio('#123456', '#123456'), 1);
  assert.equal(ratio('#777777', '#ffffff'), ratio('#ffffff', '#777777'));
  // #767676 on white is the darkest grey text colour of 4.5:1, #777777 is below.
  assert.ok(ratio('#767676', '#ffffff') >= 4.5);
  assert.ok(ratio('#777777', '#ffffff') < 4.5);
  assert.throws(() => luminance('#fff'), /not of the form #rrggbb/);
  assert.throws(() => luminance('red'), /not of the form #rrggbb/);
});

test('the backgrounds are the page tokens of both themes and the card of the drawings', async () => {
  const { readFileSync } = await import('node:fs');
  const css = readFileSync(new URL('../../src/style.css', import.meta.url), 'utf8');
  const light = css.slice(css.indexOf(':root {'), css.indexOf('@media (prefers-color-scheme: dark)'));
  const dark = css.slice(css.indexOf('@media (prefers-color-scheme: dark)'), css.indexOf('* { box-sizing'));
  const token = (block: string, name: string): string => block.match(new RegExp(`--${name}: (#[0-9a-f]{6});`))![1]!;
  assert.equal(BACKGROUNDS['light surface'], token(light, 'surface'));
  assert.equal(BACKGROUNDS['light page'], token(light, 'bg'));
  assert.equal(BACKGROUNDS['dark surface'], token(dark, 'surface'));
  assert.equal(BACKGROUNDS['dark page'], token(dark, 'bg'));
  const { CARD } = await import('../lib.ts');
  assert.equal(BACKGROUNDS['drawing card'], CARD);
  assert.equal(Object.keys(BACKGROUNDS).length, 5);
});

test('every colour of the palette reaches the recorded contrast on every background, and at least 3:1', () => {
  assert.deepEqual(Object.keys(PALETTE), Object.keys(FIGURES));
  const backgrounds = Object.values(BACKGROUNDS);
  let lowest = Infinity;
  for (const [name, colour] of Object.entries(PALETTE)) {
    const got = backgrounds.map((bg) => Number(wireRatio(colour, bg).toFixed(2)));
    assert.deepEqual(got, FIGURES[name], name);
    for (const r of got) {
      assert.ok(r >= MIN_RATIO, name);
      lowest = Math.min(lowest, r);
    }
  }
  assert.equal(lowest, 3.18);
});

test('black and white carry a casing, and need it', () => {
  for (const [name, colour] of Object.entries(PALETTE)) {
    assert.equal(colour.casing !== null, name === 'black' || name === 'white', name);
  }
  assert.ok(ratio(PALETTE['black']!.core, BACKGROUNDS['dark surface']!) < MIN_RATIO);
  assert.ok(ratio(PALETTE['white']!.core, BACKGROUNDS['light surface']!) < MIN_RATIO);
});

test('every rule names a colour of the palette', () => {
  for (const name of [...Object.values(BY_NET), ...Object.values(BY_CLASS)]) assert.ok(name in PALETTE, name);
});

test('every wire of the catalogue has a colour; the lead of a component has none', () => {
  for (const wire of CATALOGUE.wires) {
    const c = colourOf(wire);
    if (wire.kind === 'component') assert.equal(c, null, `wire ${wire.n}`);
    else assert.ok(c !== null && c in PALETTE, `wire ${wire.n}`);
  }
  assert.ok(CATALOGUE.wires.some((w) => w.kind === 'component'));
});

test('the net decides before the class', () => {
  assert.equal(colourOf({ n: 1, kind: 'wire', net: 'gnd', cls: 'power' }), 'black');
  assert.equal(colourOf({ n: 1, kind: 'wire', net: 'x', cls: 'power' }), 'red');
  assert.equal(colourOf({ n: 1, kind: 'stub', net: 'x', cls: 'nc' }), 'grey');
  assert.equal(colourOf({ n: 1, kind: 'component', net: 'gnd', cls: 'gnd' }), null);
  assert.throws(() => colourOf({ n: 7, kind: 'wire', net: 'x', cls: 'uart' }), /wire 7: no colour for net x, class uart/);
  assert.throws(() => colourOf({ n: 7, kind: 'wire', net: 'x', cls: 'phase' }), /wire 7/);
});

test('a wire end lies on every drawing of its part', () => {
  assert.deepEqual(endPoints('pad.5', anchors), [
    { file: 'copro--osoyoo.svg', pin: 'pad.5' }, { file: 'copro.svg', pin: 'pad.5' },
  ]);
  assert.deepEqual(endPoints('can.H', anchors).map((p) => p.file), ['copro--osoyoo.svg', 'copro.svg']);
  assert.deepEqual(endPoints('ina228.G', anchors).map((p) => p.file), ['ina228--matek.svg', 'ina228--xt90.svg']);
  assert.deepEqual(endPoints('estop.NC1', anchors), [{ file: 'estop.svg', pin: 'NC1' }]);
});

test('an end on the INA3221 follows the map to the print of the board', () => {
  assert.deepEqual(endPoints('ina3221.C1A', anchors), [{ file: 'ina3221.svg', pin: 'VIN1+' }]);
  assert.deepEqual(endPoints('ina3221.LP', anchors).map((p) => p.pin), ['PAD1', 'PAD2', 'PAD3']);
  assert.deepEqual(endPoints('ina3221.A0', anchors), []);
  assert.equal(withoutWirePoint('ina3221.A0', anchors), true);
  assert.equal(withoutWirePoint('ina3221.VS', anchors), false);
  // A print name of the board is not a catalogue pin.
  assert.deepEqual(endPoints('ina3221.VIN1+', anchors), []);
  assert.equal(withoutWirePoint('ina3221.VIN1+', anchors), false);
});

test('an end without a part, a pin or a dot has no anchor and is not excused', () => {
  for (const end of ['pad.0', 'pad.41', 'can.X', 'estop.NONE', 'nopart.X', 'nodot', '', '.', 'estop.']) {
    assert.deepEqual(endPoints(end, anchors), [], end);
    assert.equal(withoutWirePoint(end, anchors), false, end);
  }
  // A drawing outside the catalogue takes no wire.
  assert.deepEqual(endPoints('ina219.VCC', anchors), []);
  assert.deepEqual(endPoints('null.VCC', anchors), []);
});

const record = (pins: Record<string, [number, number]>): DrawingRecord => ({
  part: 'x', variant: null, viewBox: [0, 0, 1000, 1000], names: 'catalogue',
  pins: Object.fromEntries(Object.entries(pins).map(([id, [x, y]]) => [id, { x, y, side: 'N' as const, labelId: id, label: { en: id, de: id } }])),
});

test('neighbours are mutual and end at 1.6 times the distance to the nearest pin', () => {
  // A row with a pitch of 10 px and 1 pin 100 px away.
  const n = neighbours(record({ a: [0, 0], b: [10, 0], c: [20, 0], far: [120, 0] }));
  assert.deepEqual(n, { a: ['b'], b: ['a', 'c'], c: ['b'], far: [] });
  // At exactly 1.6 times the pitch the pin is a neighbour, 0.1 px further it is not.
  assert.deepEqual(neighbours(record({ a: [0, 0], b: [10, 0], c: [26, 0] }))['b'], ['a', 'c']);
  assert.deepEqual(neighbours(record({ a: [0, 0], b: [10, 0], c: [26.1, 0] }))['b'], ['a']);
  assert.deepEqual(neighbours(record({ a: [0, 0] })), { a: [] });
  assert.deepEqual(neighbours(record({})), {});
});

test('neighbouring pads of the coprocessor are the pads above and below', () => {
  const n = neighbours(anchors.files['copro.svg']!);
  assert.deepEqual(n['pad.5'], ['pad.4', 'pad.6']);
  assert.deepEqual(n['pad.1'], ['pad.2']);
  assert.deepEqual(n['can.H'], ['can.L']);
});
