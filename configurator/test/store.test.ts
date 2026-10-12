import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { HASH_VERSION, MAX_HASH, formatHash, namesSetup, parseHash } from '../src/hash.ts';
import { MAX_STORED, STORE_KEY, STORE_VERSION, browserStore, readStore, writeStore } from '../src/store.ts';
import type { Store, Stored } from '../src/store.ts';
import { describe, sweep } from './geometry.ts';

const EMPTY: Stored = { lang: null, setup: null };

/** A store in memory. `items` is what it holds. */
function memory(first?: string): Store & { items: Map<string, string> } {
  const items = new Map<string, string>();
  if (first !== undefined) items.set(STORE_KEY, first);
  return {
    items,
    getItem: (key) => items.get(key) ?? null,
    setItem: (key, value) => { items.set(key, value); },
  };
}

const read = (raw?: string): Stored => readStore(memory(raw), DEFAULT_OPTIONS);
const json = (value: unknown): string => JSON.stringify(value);

test('no store and an empty store read as the empty state', () => {
  assert.deepEqual(readStore(null, DEFAULT_OPTIONS), EMPTY);
  assert.deepEqual(read(), EMPTY);
});

test('a written state reads back, under one key', () => {
  const store = memory();
  const stored: Stored = { lang: 'de', setup: 'v=1&f=servo,esc&o=osoyoo' };
  assert.equal(writeStore(store, stored), true);
  assert.deepEqual([...store.items.keys()], [STORE_KEY]);
  assert.deepEqual(JSON.parse(store.items.get(STORE_KEY) as string), { v: STORE_VERSION, ...stored });
  assert.deepEqual(readStore(store, DEFAULT_OPTIONS), stored);
  assert.equal(writeStore(store, EMPTY), true);
  assert.deepEqual(readStore(store, DEFAULT_OPTIONS), EMPTY);
});

test('every selection of the sweep survives the store, in both languages', () => {
  const samples = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS });
  assert.ok(samples.length > 100);
  for (const { selection } of samples) {
    const seen: string[] = [];
    for (const lang of ['en', 'de'] as Lang[]) {
      const store = memory();
      const setup = formatHash(selection, lang, lang, CATALOGUE);
      writeStore(store, { lang, setup });
      const back = readStore(store, DEFAULT_OPTIONS);
      assert.deepEqual(back, { lang, setup }, describe(selection));
      const parsed = parseHash(back.setup as string, DEFAULT_OPTIONS);
      assert.equal(parsed.lang, null, describe(selection));
      assert.deepEqual(parsed.unknown, [], describe(selection));
      assert.equal(formatHash(parsed.selection, lang, lang, CATALOGUE), setup, describe(selection));
      seen.push(setup);
    }
    // The stored selection holds ids alone: it is the same text in both languages.
    assert.equal(seen[0], seen[1], describe(selection));
  }
});

test('a value that is no JSON object of this version reads as the empty state', () => {
  const setup = 'v=1&f=servo';
  for (const raw of [
    '', ' ', '{', '{"v":1,"lang":"de"', 'undefined', 'null', 'true', '1', '"v=1&f=servo"', '[]', `[${json({ v: 1, setup })}]`,
    json({ lang: 'de', setup }), json({ v: 0, lang: 'de', setup }), json({ v: 2, lang: 'de', setup }),
    json({ v: '1', lang: 'de', setup }), json({ v: null, lang: 'de', setup }),
    // The shape of a selection, as another version of the page could store it.
    json({ functions: ['servo'], owned: [], options: DEFAULT_OPTIONS, lang: 'de' }),
  ]) {
    assert.deepEqual(read(raw), EMPTY, raw);
  }
});

test('a field that does not pass the check is null and the other field stays', () => {
  const setup = 'v=1&f=servo';
  for (const lang of [null, undefined, '', 'fr', 'DE', 'de-DE', 1, true, ['de'], { de: 1 }]) {
    assert.deepEqual(read(json({ v: 1, lang, setup })), { lang: null, setup }, json(lang));
  }
  for (const bad of [
    null, undefined, '', 1, true, ['v=1'], { f: 'servo' },
    // Another version of the link format, and a selection without a version.
    'v=2&f=servo', 'f=servo', 'p=pdmini', 'o=pd', 'v=&f=servo',
    `v=1&f=${'a'.repeat(MAX_HASH)}`,
  ]) {
    assert.deepEqual(read(json({ v: 1, lang: 'de', setup: bad })), { lang: 'de', setup: null }, json(bad));
  }
  assert.deepEqual(read(json({ v: 1, lang: 'en', setup, more: { x: 1 } })), { lang: 'en', setup });
});

test('a stored selection of the largest length is read and 1 character more is not', () => {
  const at = (length: number): string => `v=1&f=${'a,'.repeat((length - 6) / 2)}`;
  assert.equal(at(MAX_HASH).length, MAX_HASH);
  assert.deepEqual(read(json({ v: 1, lang: null, setup: at(MAX_HASH) })), { lang: null, setup: at(MAX_HASH) });
  assert.deepEqual(read(json({ v: 1, lang: null, setup: `${at(MAX_HASH)}a` })), EMPTY);
});

test('a stored value of the largest length is read and 1 character more is not', () => {
  const padded = (length: number): string => {
    const head = json({ v: 1, lang: 'de', setup: 'v=1', pad: '' });
    return json({ v: 1, lang: 'de', setup: 'v=1', pad: 'x'.repeat(length - head.length) });
  };
  assert.equal(padded(MAX_STORED).length, MAX_STORED);
  assert.deepEqual(read(padded(MAX_STORED)), { lang: 'de', setup: 'v=1' });
  assert.deepEqual(read(padded(MAX_STORED + 1)), EMPTY);
});

test('ids the catalogue does not know stay in the stored text and parse without a throw', () => {
  const setup = 'v=1&f=servo,gone,alsogone&p=nopart&o=pd,notoken';
  const back = read(json({ v: 1, lang: 'de', setup }));
  assert.deepEqual(back, { lang: 'de', setup });
  const parsed = parseHash(setup, DEFAULT_OPTIONS);
  assert.deepEqual(parsed.selection.functions, ['servo', 'gone', 'alsogone']);
  assert.deepEqual(parsed.unknown, ['notoken']);
  // The resolver drops them: the setup is that of the ids it knows.
  const known = resolve(CATALOGUE, { ...parsed.selection, functions: ['servo'], owned: [] });
  const given = resolve(CATALOGUE, parsed.selection);
  assert.deepEqual(given.wires.map((w) => w.n), known.wires.map((w) => w.n));
  assert.deepEqual([...given.ignored].sort(), ['alsogone', 'gone', 'nopart']);
});

test('a store that throws reads as the empty state and takes no write', () => {
  const denied = (): never => { throw new DOMException('denied', 'SecurityError'); };
  const full = (): never => { throw new DOMException('full', 'QuotaExceededError'); };
  assert.deepEqual(readStore({ getItem: denied, setItem: denied }, DEFAULT_OPTIONS), EMPTY);
  assert.equal(writeStore({ getItem: denied, setItem: denied }, { lang: 'de', setup: 'v=1' }), false);
  const store = memory(json({ v: 1, lang: 'en', setup: 'v=1&f=esc' }));
  assert.equal(writeStore({ getItem: store.getItem, setItem: full }, { lang: 'de', setup: 'v=1' }), false);
  // The value from before the refused write stays.
  assert.deepEqual(readStore(store, DEFAULT_OPTIONS), { lang: 'en', setup: 'v=1&f=esc' });
  assert.equal(writeStore(null, { lang: 'de', setup: 'v=1' }), false);
});

test('a store that returns no string reads as the empty state', () => {
  for (const value of [undefined, 1, {}, []]) {
    const store = { getItem: () => value as unknown as string, setItem: () => undefined };
    assert.deepEqual(readStore(store, DEFAULT_OPTIONS), EMPTY);
  }
});

test('the store of the browser is null where there is none and where its getter throws', () => {
  const had = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
  try {
    Object.defineProperty(globalThis, 'localStorage', { configurable: true, get: () => undefined });
    assert.equal(browserStore(), null);
    Object.defineProperty(globalThis, 'localStorage', { configurable: true, get: () => null });
    assert.equal(browserStore(), null);
    Object.defineProperty(globalThis, 'localStorage', {
      configurable: true,
      get: () => { throw new DOMException('denied', 'SecurityError'); },
    });
    assert.equal(browserStore(), null);
    const store = memory();
    Object.defineProperty(globalThis, 'localStorage', { configurable: true, get: () => store });
    assert.equal(browserStore(), store);
  } finally {
    if (had) Object.defineProperty(globalThis, 'localStorage', had);
    else delete (globalThis as { localStorage?: unknown }).localStorage;
  }
});

test('a hash names a setup when it carries a key other than the language', () => {
  for (const hash of ['', '#', '#l=de', 'l=en', '#l=fr', '#l=de&l=en']) assert.equal(namesSetup(hash), false, hash);
  for (const hash of [
    `#v=${HASH_VERSION}`, '#v=1&l=de', '#v=2', '#f=servo', '#p=pdmini', '#o=pd', '#f=', '#x', '#choose', '#l=de&x=1',
    `#${'a'.repeat(MAX_HASH + 1)}`, `#l=${'a'.repeat(MAX_HASH)}`,
  ]) assert.equal(namesSetup(hash), true, hash.slice(0, 40));
  assert.equal(namesSetup(undefined as unknown as string), false);
  // At the limit the keys decide.
  assert.equal(namesSetup(`#l=${'a'.repeat(MAX_HASH - 2)}`), false);
});
