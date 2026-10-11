import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { HASH_VERSION, MAX_HASH, MAX_IDS, MAX_REPORTED, formatHash, parseHash, reportIds } from '../src/hash.ts';
import { describe, sweep } from './geometry.ts';

const base = (): Selection => ({ functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });
const sorted = (s: Selection): Selection => ({
  functions: [...s.functions].sort(), owned: [...s.owned].sort(), options: { ...s.options },
});

test('an empty hash is the base setup without a notice', () => {
  for (const hash of ['', '#']) {
    assert.deepEqual(parseHash(hash, DEFAULT_OPTIONS), { selection: base(), lang: null, badVersion: false, unknown: [] });
  }
});

test('the base setup is written as the version alone', () => {
  assert.equal(formatHash(base(), 'en', 'en', CATALOGUE), `v=${HASH_VERSION}`);
  assert.equal(formatHash(base(), 'de', 'de', CATALOGUE), `v=${HASH_VERSION}`);
});

test('every selection of the sweep survives format and parse', () => {
  const samples = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS });
  assert.ok(samples.length > 100);
  for (const { selection } of samples) {
    for (const lang of ['en', 'de'] as Lang[]) {
      const hash = formatHash(selection, lang, 'en', CATALOGUE);
      assert.ok(hash.length <= MAX_HASH, describe(selection));
      const parsed = parseHash(`#${hash}`, DEFAULT_OPTIONS);
      assert.equal(parsed.badVersion, false, describe(selection));
      assert.deepEqual(parsed.unknown, [], describe(selection));
      assert.deepEqual(sorted(parsed.selection), sorted(selection), describe(selection));
      assert.equal(parsed.lang ?? 'en', lang);
      assert.equal(formatHash(parsed.selection, lang, 'en', CATALOGUE), hash);
    }
  }
});

test('the full selection survives format and parse', () => {
  const full: Selection = {
    functions: CATALOGUE.functions.map((f) => f.id),
    owned: CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id),
    options: { connection: 'osoyoo', ina228Board: 'xt90', escPower: 'pd' },
  };
  const hash = formatHash(full, 'de', 'en', CATALOGUE);
  assert.ok(hash.length <= MAX_HASH);
  assert.deepEqual(parseHash(hash, DEFAULT_OPTIONS).selection, full);
  assert.ok(hash.endsWith('&o=osoyoo,xt90,pd&l=de'));
});

test('ids stand in catalogue order whatever the tick order', () => {
  const ids = CATALOGUE.functions.map((f) => f.id);
  const a = formatHash({ ...base(), functions: ids }, 'en', 'en', CATALOGUE);
  const b = formatHash({ ...base(), functions: [...ids].reverse() }, 'en', 'en', CATALOGUE);
  assert.equal(a, b);
});

test('the language key is written only when it differs from the start language', () => {
  assert.equal(formatHash(base(), 'de', 'en', CATALOGUE), 'v=1&l=de');
  assert.equal(formatHash(base(), 'en', 'de', CATALOGUE), 'v=1&l=en');
  assert.equal(parseHash('#v=1&l=de', DEFAULT_OPTIONS).lang, 'de');
  assert.equal(parseHash('#v=1&l=en', DEFAULT_OPTIONS).lang, 'en');
  assert.equal(parseHash('#v=1&l=fr', DEFAULT_OPTIONS).lang, null);
  assert.equal(parseHash('#l=de', DEFAULT_OPTIONS).lang, 'de');
  assert.equal(parseHash('#l=de', DEFAULT_OPTIONS).badVersion, false);
});

test('unknown but well-formed ids pass to the resolver, which reports them', () => {
  const parsed = parseHash('#v=1&f=servo,warp&p=ina3221,flux', DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, ['servo', 'warp']);
  assert.deepEqual(parsed.selection.owned, ['ina3221', 'flux']);
  assert.deepEqual(parsed.unknown, []);
  const setup = resolve(CATALOGUE, parsed.selection);
  assert.deepEqual([...setup.ignored].sort(), ['flux', 'warp']);
  assert.equal(formatHash(parsed.selection, 'en', 'en', CATALOGUE), 'v=1&f=servo&p=ina3221');
});

test('malformed ids and unknown option tokens are dropped and reported', () => {
  const parsed = parseHash('#v=1&f=servo,<img>,SERVO,a%20b&p=x_y&o=pd,turbo', DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, ['servo']);
  assert.deepEqual(parsed.selection.owned, []);
  assert.equal(parsed.selection.options.escPower, 'pd');
  assert.deepEqual(parsed.unknown, ['<img>', 'SERVO', 'a b', 'x_y', 'turbo']);
  assert.equal(parsed.badVersion, false);
});

test('a repeated id counts once', () => {
  assert.deepEqual(parseHash('#v=1&f=servo,servo,,servo', DEFAULT_OPTIONS).selection.functions, ['servo']);
});

test('another version gives the base setup and the version notice', () => {
  for (const hash of ['#v=2&f=servo', '#v=0', '#v=&f=servo', '#v=1x', '#v=01&f=servo', '#f=servo', '#p=ina3221', '#o=pd']) {
    const parsed = parseHash(hash, DEFAULT_OPTIONS);
    assert.equal(parsed.badVersion, true, hash);
    assert.deepEqual(parsed.selection, base(), hash);
  }
  const parsed = parseHash('#v=2&f=servo&l=de', DEFAULT_OPTIONS);
  assert.equal(parsed.lang, 'de');
});

test('a hash without a known key is the base setup without a notice', () => {
  for (const hash of ['#wiring', '#choose', '#x=1']) {
    const parsed = parseHash(hash, DEFAULT_OPTIONS);
    assert.equal(parsed.badVersion, false, hash);
    assert.deepEqual(parsed.selection, base(), hash);
  }
});

test('an oversized hash gives the base setup and the version notice', () => {
  const at = `v=1&f=${'a,'.repeat((MAX_HASH - 6) / 2)}`;
  assert.equal(at.length, MAX_HASH);
  assert.equal(parseHash(`#${at}`, DEFAULT_OPTIONS).badVersion, false);
  const parsed = parseHash(`#${at}a`, DEFAULT_OPTIONS);
  assert.equal(parsed.badVersion, true);
  assert.deepEqual(parsed.selection, base());
  assert.deepEqual(parseHash(`#v=1&f=${'servo,'.repeat(100000)}`, DEFAULT_OPTIONS).selection, base());
});

test('at most MAX_IDS ids are kept per key', () => {
  const ids = Array.from({ length: MAX_IDS + 20 }, (_, i) => `id${i}`);
  const parsed = parseHash(`#v=1&f=${ids.join(',')}`, DEFAULT_OPTIONS);
  assert.equal(parsed.selection.functions.length, MAX_IDS);
  assert.equal(parsed.badVersion, false);
});

test('an id longer than 32 characters is malformed', () => {
  const parsed = parseHash(`#v=1&f=${'a'.repeat(32)},${'b'.repeat(33)}`, DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, ['a'.repeat(32)]);
  assert.deepEqual(parsed.unknown, ['b'.repeat(33)]);
});

test('parsing never throws and never changes the defaults', () => {
  const defaults = { ...DEFAULT_OPTIONS };
  const inputs = ['#%', '#%E0%A4%A', '#v=1&f=%', '#&&&', '#=', '#v', '#v=1&v=2', '#v=1&o=,,,', '#\u0000', '#v=1&f=__proto__,constructor', '#v=1&o=__proto__'];
  for (const hash of inputs) {
    const parsed = parseHash(hash, defaults);
    assert.ok(Array.isArray(parsed.selection.functions), hash);
    assert.doesNotThrow(() => resolve(CATALOGUE, parsed.selection), hash);
  }
  assert.deepEqual(defaults, DEFAULT_OPTIONS);
  // @ts-expect-error a caller without types
  assert.deepEqual(parseHash(undefined, DEFAULT_OPTIONS).selection, base());
});

test('a notice names at most MAX_REPORTED ids, each cut to 32 characters', () => {
  assert.equal(reportIds(['a', 'b', 'a']), 'a, b');
  const many = Array.from({ length: MAX_REPORTED + 1 }, (_, i) => `x${i}`);
  assert.equal(reportIds(many), `${many.slice(0, MAX_REPORTED).join(', ')}, …`);
  assert.equal(reportIds(['z'.repeat(40)]), `${'z'.repeat(32)}…`);
});

test('a link written without the functions that need no part resolves to the same parts and wires', () => {
  const hash = 'v=1&f=servo,esc&p=sd';
  const parsed = parseHash(`#${hash}`, DEFAULT_OPTIONS);
  assert.equal(parsed.badVersion, false);
  assert.deepEqual(parsed.unknown, []);
  assert.deepEqual(parsed.selection, { functions: ['servo', 'esc'], owned: ['sd'], options: { ...DEFAULT_OPTIONS } });
  const setup = resolve(CATALOGUE, parsed.selection);
  assert.deepEqual(setup.ignored, []);
  assert.deepEqual(setup.parts.map((p) => p.part.id), ['panel', 'copro', 'usb', 'wire', 'servo', 'ssup', 'esc', 'batt', 'sd']);
  assert.deepEqual(setup.wires.map((w) => w.n), [1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
  assert.deepEqual(setup.functions.filter((f) => f.chosen).map((f) => f.fn.id), ['servo', 'esc']);
  // The functions that need no part are enabled and are not part of the link.
  const base = CATALOGUE.functions.filter((f) => f.needs.length === 0).map((f) => f.id);
  assert.equal(base.length, 10);
  for (const id of base) assert.ok(setup.functions.find((f) => f.fn.id === id)?.enabled, id);
  const written = formatHash(parsed.selection, 'en', 'en', CATALOGUE);
  assert.equal(written, hash);
  assert.ok(base.every((id) => !written.includes(id)));
  assert.equal(HASH_VERSION, 1);
});
