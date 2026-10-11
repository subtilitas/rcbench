// Properties of the URL hash codec over the selection space.
//
// Enumerated: every subset of the functions and every subset of the parts you
// own, each under every option combination and both languages. Sampled: mixed
// selections and random input, from a generator with a fixed seed. A failure
// names the selection or the input, and for a sampled case the seed.
//
//  1. parse(format(s)) is s, and format(parse(h)) is h for a hash the page writes.
//  2. format does not depend on the order or the repeats of the ids.
//  3. A hash the page writes fits the limits of the parser.
//  4. parse never throws, and its result is a well-formed selection.
//  5. The resolver accepts every parse result without throwing.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Lang, Selection } from '../../contract.ts';
import {
  MIXED, OPTION_SETS, SEED, enumerate, generator, sample, shuffled, spaceOf,
} from '../../core/test/property/space.ts';
import { OPTION_KEYS, OPTION_VALUES } from '../../core/resolve.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../../src/core.ts';
import { MAX_HASH, MAX_IDS, MAX_REPORTED, formatHash, parseHash, reportIds } from '../../src/hash.ts';
import type { Parsed } from '../../src/hash.ts';

const SPACE = spaceOf(CATALOGUE);
const LANGS: Lang[] = ['en', 'de'];
const ID = /^[a-z0-9]{1,32}$/;

const sorted = (s: Selection): Selection => ({
  functions: [...s.functions].sort(), owned: [...s.owned].sort(), options: { ...s.options },
});

function assertRoundTrip(selection: Selection, where: string, random: () => number): void {
  for (const lang of LANGS) {
    for (const start of LANGS) {
      const hash = formatHash(selection, lang, start, CATALOGUE);
      // 3
      assert.ok(hash.length <= MAX_HASH, `the hash has ${hash.length} characters for ${where}`);
      assert.match(hash, /^v=1(&f=[a-z0-9,]+)?(&p=[a-z0-9,]+)?(&o=[a-z0-9,]+)?(&l=(en|de))?$/, where);
      // 1
      const parsed = parseHash(`#${hash}`, DEFAULT_OPTIONS);
      assert.equal(parsed.badVersion, false, where);
      assert.deepEqual(parsed.unknown, [], where);
      assert.deepEqual(sorted(parsed.selection), sorted(selection), where);
      assert.equal(parsed.lang, lang === start ? null : lang, where);
      assert.equal(formatHash(parsed.selection, lang, start, CATALOGUE), hash, where);
      // The parser reads the hash with and without the number sign.
      assert.deepEqual(parseHash(hash, DEFAULT_OPTIONS), parsed, where);
    }
  }
  // 2
  const mixed: Selection = {
    functions: shuffled([...selection.functions, ...selection.functions], random),
    owned: shuffled([...selection.owned, ...selection.owned], random),
    options: { ...selection.options },
  };
  assert.equal(formatHash(mixed, 'en', 'en', CATALOGUE), formatHash(selection, 'en', 'en', CATALOGUE), where);
}

function assertWellFormed(parsed: Parsed, where: string): void {
  assert.ok(Array.isArray(parsed.selection.functions) && Array.isArray(parsed.selection.owned), where);
  for (const list of [parsed.selection.functions, parsed.selection.owned]) {
    assert.ok(list.length <= MAX_IDS, `${list.length} ids for ${where}`);
    assert.equal(new Set(list).size, list.length, `an id twice for ${where}`);
    for (const id of list) assert.match(id, ID, where);
  }
  assert.deepEqual(Object.keys(parsed.selection.options).sort(), [...OPTION_KEYS].sort(), where);
  for (const key of OPTION_KEYS) {
    assert.ok((OPTION_VALUES[key] as ReadonlyArray<string>).includes(parsed.selection.options[key]), where);
  }
  assert.ok(parsed.lang === null || parsed.lang === 'en' || parsed.lang === 'de', where);
  assert.equal(typeof parsed.badVersion, 'boolean', where);
  assert.ok(Array.isArray(parsed.unknown) && parsed.unknown.every((x) => typeof x === 'string'), where);
  if (parsed.badVersion) {
    assert.deepEqual(parsed.selection, { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } }, `another version carries a selection for ${where}`);
    assert.deepEqual(parsed.unknown, [], where);
  }
  // 5
  const setup = resolve(CATALOGUE, parsed.selection);
  assert.ok(setup.parts.length > 0, where);
  // The notice text is bounded whatever the input holds.
  const text = reportIds([...parsed.unknown, ...setup.ignored]);
  assert.ok(text.length <= MAX_REPORTED * 35 + 3, `the notice has ${text.length} characters for ${where}`);
}

function parseOf(hash: string, where: string): Parsed {
  let parsed: Parsed | undefined;
  assert.doesNotThrow(() => { parsed = parseHash(hash, DEFAULT_OPTIONS); }, where);
  assert.ok(parsed, where);
  return parsed;
}

test('1 to 3: every subset of the functions survives format and parse', () => {
  const random = generator(SEED);
  let cases = 0;
  for (const c of enumerate(SPACE.functions, 'functions')) {
    assertRoundTrip(c.selection, c.where, random);
    cases += 1;
  }
  assert.equal(cases, SPACE.functionCases);
});

test('1 to 3: every subset of the parts you own survives format and parse', () => {
  const random = generator(SEED);
  let cases = 0;
  for (const c of enumerate(SPACE.selectable, 'owned')) {
    assertRoundTrip(c.selection, c.where, random);
    cases += 1;
  }
  assert.equal(cases, SPACE.partCases);
});

test(`1 to 3: ${MIXED} mixed selections from the generator survive format and parse`, () => {
  const random = generator(SEED);
  let cases = 0;
  for (const c of sample(SPACE, MIXED, SEED)) {
    assertRoundTrip(c.selection, c.where, random);
    cases += 1;
  }
  assert.equal(cases, MIXED);
});

test('1: format leaves out what the catalogue does not hold, and parse gives it back as written', () => {
  const locked = CATALOGUE.parts.filter((p) => !p.selectable).map((p) => p.id);
  assert.ok(locked.length > 0);
  for (const options of OPTION_SETS) {
    const selection: Selection = { functions: ['nofunction', ...SPACE.functions], owned: [...locked, 'nopart'], options: { ...options } };
    const hash = formatHash(selection, 'en', 'en', CATALOGUE);
    const parsed = parseHash(hash, DEFAULT_OPTIONS);
    assert.deepEqual(parsed.selection, { functions: SPACE.functions, owned: [], options: { ...options } });
  }
});

test('4 and 5: every prefix and every suffix of the longest hash the page writes parses to a well-formed selection', () => {
  const full: Selection = {
    functions: SPACE.functions, owned: SPACE.selectable,
    options: { connection: 'osoyoo', ina228Board: 'xt90', escPower: 'pd' },
  };
  const hash = `#${formatHash(full, 'de', 'en', CATALOGUE)}`;
  for (let cut = 0; cut <= hash.length; cut++) {
    assertWellFormed(parseOf(hash.slice(0, cut), `the first ${cut} characters of ${hash}`), `the first ${cut} characters`);
    assertWellFormed(parseOf(hash.slice(cut), `${hash} from character ${cut}`), `from character ${cut}`);
    // One character removed, and one character doubled.
    assertWellFormed(parseOf(hash.slice(0, cut) + hash.slice(cut + 1), `${hash} without character ${cut}`), `without character ${cut}`);
    assertWellFormed(parseOf(hash.slice(0, cut) + hash.slice(cut > 0 ? cut - 1 : 0), `${hash} with character ${cut} twice`), `character ${cut} twice`);
  }
});

test('4 and 5: 20 000 random inputs from the generator parse to a well-formed selection', () => {
  const random = generator(SEED + 7);
  const pick = <T>(list: ReadonlyArray<T>): T => list[Math.floor(random() * list.length)] as T;
  const ids = [...SPACE.functions, ...SPACE.selectable, ...CATALOGUE.notInRelease.map((n) => n.id), 'panel', 'ssup', 'x', 'a'.repeat(32), 'a'.repeat(33)];
  const tokens = [
    'v', 'f', 'p', 'o', 'l', '=', '&', ',', '#', '1', '2', '0', 'en', 'de', 'fr', 'osoyoo', 'xt90', 'pd', 'pads', 'matek', 'battery',
    '%', '%2C', '%26', '%3D', '%23', '%00', '%zz', '%ff', '%C3%A4', '+', ' ', 'ä', '\u{1f527}', '\ud800', '<', '>', '"', "'", '\\', '/', '?', ';', '.',
    'constructor', '__proto__', 'toString', 'SERVO', 'Servo', '-1', '1e3', 'null', 'undefined', '[', ']', '{', '}',
  ];
  for (let index = 0; index < 20_000; index++) {
    const parts: string[] = random() < 0.7 ? ['#v=1'] : [];
    const length = Math.floor(random() * 24);
    for (let i = 0; i < length; i++) parts.push(random() < 0.5 ? pick(ids) : pick(tokens));
    const hash = parts.join(random() < 0.5 ? '' : pick(['&', ',', '=', '&f=', '&p=', '&o=', '&l=']));
    const where = `${JSON.stringify(hash)} (seed 0x${(SEED + 7).toString(16)}, case ${index})`;
    assertWellFormed(parseOf(hash, where), where);
  }
});

test('4: values that are no hash parse to the base setup', () => {
  const base = { selection: { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } }, lang: null, badVersion: false, unknown: [] };
  for (const value of [undefined, null, 0, 1, true, {}, [], () => '', Symbol('x')]) {
    assert.deepEqual(parseHash(value as unknown as string, DEFAULT_OPTIONS), base);
  }
});

test('4: a result does not share its options with the defaults or with another result', () => {
  const first = parseHash('#v=1&o=pd', DEFAULT_OPTIONS);
  const second = parseHash('#v=1', DEFAULT_OPTIONS);
  const bad = parseHash('#v=2', DEFAULT_OPTIONS);
  assert.equal(first.selection.options.escPower, 'pd');
  assert.equal(second.selection.options.escPower, DEFAULT_OPTIONS.escPower);
  assert.notEqual(second.selection.options, DEFAULT_OPTIONS);
  assert.notEqual(bad.selection.options, DEFAULT_OPTIONS);
  assert.notEqual(first.selection.options, second.selection.options);
});

test('3: the limits at their edges', () => {
  const pad = (length: number): string => `v=1&f=${'a'.repeat(32)},`.padEnd(length, 'b');
  assert.equal(parseHash(`#${'v=1&f=servo&x='.padEnd(MAX_HASH, 'b')}`, DEFAULT_OPTIONS).badVersion, false);
  assert.deepEqual(parseHash(`#${'v=1&f=servo&x='.padEnd(MAX_HASH, 'b')}`, DEFAULT_OPTIONS).selection.functions, ['servo']);
  assert.equal(parseHash(`#${'v=1&f=servo&x='.padEnd(MAX_HASH + 1, 'b')}`, DEFAULT_OPTIONS).badVersion, true);
  assert.equal(parseHash(pad(MAX_HASH + 1), DEFAULT_OPTIONS).badVersion, true);
  assert.equal(parseHash(`#${'x'.repeat(64 * 1024)}`, DEFAULT_OPTIONS).badVersion, true);

  const many = Array.from({ length: MAX_IDS + 6 }, (_, i) => `id${i}`);
  const parsed = parseHash(`#v=1&f=${many.join(',')}&p=${many.join(',')}`, DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, many.slice(0, MAX_IDS));
  assert.deepEqual(parsed.selection.owned, many.slice(0, MAX_IDS));

  assert.deepEqual(parseHash(`#v=1&f=${'a'.repeat(32)}`, DEFAULT_OPTIONS).selection.functions, ['a'.repeat(32)]);
  const long = parseHash(`#v=1&f=${'a'.repeat(33)}`, DEFAULT_OPTIONS);
  assert.deepEqual(long.selection.functions, []);
  assert.deepEqual(long.unknown, ['a'.repeat(33)]);
});
