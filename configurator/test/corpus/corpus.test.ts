// Holds the page logic to the hash corpus.
//
// Each entry of hashes.json is decoded and resolved, and the result is compared
// with what the entry stores. The comparison runs in the ids the corpus starts
// with: a part or a function a later catalogue adds is outside every stored
// setup. An entry that names such an id is held to the rules of a well-formed
// result only, because its meaning changes on the day the id is added.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import type { OptionKey } from '../../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, plainDrive } from '../../src/core.ts';
import { MAX_HASH, parseHash } from '../../src/hash.ts';
import {
  CORPUS_FILE, WIRES_FILE, expectationOf, hashOf, idsOf, namesLater, namesRetired, outcome, readCorpus, readWires,
} from './corpus.ts';
import type { Outcome } from './corpus.ts';
import { added, corpusText, wiresOf, wiresText } from './tool.ts';

const corpus = readCorpus();
const wires = readWires();
const now = idsOf();
/** Digest of the 164 entries the corpus starts with: names, hashes and stored outcomes. */
const DIGEST = 3359036518;

const BASE = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } };

/** Ids the catalogue holds and the corpus did not start with. */
const later = new Set([
  ...now.functions.filter((id) => !corpus.ids.functions.includes(id)),
  ...now.owned.filter((id) => !corpus.ids.owned.includes(id)),
]);

/** The test objects: parts a visitor connects and tests. A link names one as an owned part or by a function. */
const OBJECTS = CATALOGUE.parts.filter((p) => p.testObject === true).map((p) => p.id);

/**
 * Parts the corpus starts with as ticks and the page no longer offers a tick for.
 * A part of them is in a setup when a rule adds it. A link that names one
 * leaves it out of the owned parts and reports it, so its stored outcome
 * no longer holds; the check in each entry holds it to a note and no tick.
 */
const RETIRED = corpus.ids.owned.filter((id) => !now.owned.includes(id) && !OBJECTS.includes(id));

/** An outcome reduced to the ids the corpus starts with, and to the option keys `stored` holds. */
function inCorpusIds(o: Outcome, stored: Outcome): Outcome {
  const keys = Object.keys(stored.options) as OptionKey[];
  return {
    ...o,
    options: Object.fromEntries(keys.map((key) => [key, o.options[key]])) as Outcome['options'],
    parts: o.parts.filter((id) => corpus.ids.parts.includes(id)),
    enabled: o.enabled.filter((id) => corpus.ids.functions.includes(id)),
  };
}

/**
 * A stored outcome as the page gives it with test objects: a test object
 * under the owned parts is the tick of the function that drives it alone.
 * The parts, the enabled functions, the options, the language and the
 * notices stay as stored. `canonical` is null for such an entry: the page
 * writes the function in place of the part.
 */
function withObjects(stored: Outcome): Omit<Outcome, 'canonical'> & { canonical: string | null } {
  const named = stored.owned.filter((id) => OBJECTS.includes(id));
  if (named.length === 0) return stored;
  const ticks = new Set([...stored.functions, ...named.map((id) => plainDrive(CATALOGUE, id) as string)]);
  return {
    ...stored,
    functions: CATALOGUE.functions.map((f) => f.id).filter((id) => ticks.has(id)),
    owned: stored.owned.filter((id) => !OBJECTS.includes(id)),
    canonical: null,
  };
}

test('the corpus keeps the ids it starts with', () => {
  for (const id of corpus.ids.functions) assert.ok(now.functions.includes(id), `the function ${id} of a stored link is gone from the catalogue`);
  for (const id of corpus.ids.owned) {
    // A test object is no part to tick: a link that names it ticks the function that drives it alone.
    if (OBJECTS.includes(id)) assert.ok(plainDrive(CATALOGUE, id) !== null, `the test object ${id} of a stored link has no function that drives it alone`);
    else if (RETIRED.includes(id)) assert.ok(now.parts.includes(id), `the part ${id} of a stored link is gone from the catalogue`);
    else assert.ok(now.owned.includes(id), `the part ${id} of a stored link can no longer be ticked`);
  }
  assert.deepEqual(OBJECTS, ['servo', 'esc']);
  for (const id of corpus.ids.parts) assert.ok(now.parts.includes(id), `the part ${id} of a stored setup is gone from the catalogue`);
  assert.equal(CATALOGUE.hashVersion, 1, 'a new hash version needs a decoder for version 1 and a corpus of its own');
});

test('every entry has a name of its own, a hash of its own and a reason', () => {
  const names = new Set<string>();
  const hashes = new Set<string>();
  assert.ok(corpus.entries.length >= 164, `the corpus holds ${corpus.entries.length} entries: an entry is never removed`);
  for (const entry of corpus.entries) {
    assert.match(entry.name, /^[a-z0-9]+(-[a-z0-9]+)*$/);
    assert.ok(!names.has(entry.name), `${entry.name} occurs twice`);
    names.add(entry.name);
    const hash = hashOf(entry);
    assert.ok(!hashes.has(hash), `${entry.name} repeats a hash`);
    hashes.add(hash);
    assert.ok(entry.why.trim().length > 0, entry.name);
    assert.ok(hash === '' || hash.startsWith('#'), `${entry.name} has no number sign`);
  }
});

test('the first 164 entries are the ones the corpus starts with', () => {
  // The names of the first and the last entry of each block, and a digest of all hashes.
  const first = corpus.entries.slice(0, 164);
  assert.equal(first[0]?.name, 'page-base');
  assert.equal(first[163]?.name, 'bad-shape-javascript');
  let digest = 0;
  for (const entry of first) {
    for (const ch of `${entry.name}\n${hashOf(entry)}\n${JSON.stringify(entry.expect)}\n`) digest = (Math.imul(digest, 31) + (ch.codePointAt(0) as number)) >>> 0;
  }
  assert.equal(digest, DIGEST, 'an entry the corpus starts with is edited, moved or removed');
});

for (const entry of corpus.entries) {
  test(`${entry.name}: ${entry.why}`, () => {
    const hash = hashOf(entry);
    let actual: (Outcome & { wires: number[] }) | undefined;
    assert.doesNotThrow(() => { actual = outcome(hash); });
    assert.ok(actual);
    const { wires: actualWires, ...rest } = actual;

    // Whatever the hash holds: the locked parts are in the setup and the language is one of two.
    for (const part of CATALOGUE.parts) if (part.locked) assert.ok(rest.parts.includes(part.id));
    assert.ok(rest.lang === 'en' || rest.lang === 'de');
    assert.ok(rest.canonical.startsWith('#v=1') && rest.canonical.length <= MAX_HASH + 1);
    assert.deepEqual(outcome(rest.canonical).functions, rest.functions, 'the hash the page writes gives other functions');
    assert.deepEqual(outcome(rest.canonical).owned, rest.owned, 'the hash the page writes gives other parts');
    assert.deepEqual(outcome(rest.canonical).notices, [], 'the hash the page writes gives a notice');

    // An option key the entry does not store has its default value, or the hash names it.
    for (const key of Object.keys(rest.options) as OptionKey[]) {
      if (key in entry.expect.options) continue;
      assert.equal(rest.options[key], DEFAULT_OPTIONS[key], `${key} is not the default`);
    }
    assert.ok(!rest.owned.some((id) => OBJECTS.includes(id)), 'a test object is a ticked part');

    if (namesLater(entry, corpus)) return;
    // A retired part under the owned parts is reported and left out: the setup it gave before is not stored any more.
    const names = namesRetired(entry, corpus);
    if (names.length > 0) {
      assert.ok(rest.notices.includes('note'), `${entry.name} names ${names.join(', ')} and gives no note`);
      for (const id of names) assert.ok(!rest.owned.includes(id) && rest.reported.split(', ').includes(id), `${id} is not left out`);
      return;
    }
    // Both sides are compared in the ids the corpus starts with: a later entry stores every id.
    const reduced = withObjects(entry.expect);
    const expected = {
      ...reduced,
      parts: reduced.parts.filter((id) => corpus.ids.parts.includes(id)),
      enabled: reduced.enabled.filter((id) => corpus.ids.functions.includes(id)),
    };
    const got = inCorpusIds(rest, entry.expect);
    if (expected.canonical === null) {
      // The page writes the function for the part: the hash differs, the setup it gives is the same.
      const { canonical, ...same } = got;
      const { canonical: _stored, ...stored } = expected;
      assert.deepEqual(same, stored);
      assert.ok(!/[&]p=(?:[a-z0-9]+,)*(?:servo|esc)(?:,|&|$)/.test(canonical), `${canonical} names a test object as a part`);
      assert.deepEqual(outcome(canonical).parts, rest.parts, 'the hash the page writes gives other parts of the setup');
      assert.deepEqual(outcome(canonical).wires, actualWires, 'the hash the page writes gives other wires');
    } else {
      assert.deepEqual(got, expected);
    }
    assert.deepEqual(actualWires, wires[entry.name], 'the wires differ: see README.md, "When a wire of the catalogue changes"');
  });
}

test('malformed input falls back to the base setup', () => {
  // The entries whose hash carries no selection the page can read.
  const fallback = corpus.entries.filter((e) => e.name.startsWith('bad-') && e.expect.functions.length === 0 && e.expect.owned.length === 0);
  assert.ok(fallback.length >= 60, `${fallback.length} entries fall back`);
  for (const entry of fallback) {
    const parsed = parseHash(hashOf(entry), DEFAULT_OPTIONS);
    if (parsed.badVersion) assert.deepEqual(parsed.selection, BASE, entry.name);
    assert.deepEqual(entry.expect.parts, CATALOGUE.parts.filter((p) => p.locked).map((p) => p.id), entry.name);
    assert.deepEqual(entry.expect.enabled, [], entry.name);
    assert.equal(entry.expect.canonical, entry.expect.lang === 'de' ? '#v=1&l=de' : '#v=1', entry.name);
  }
});

test('the corpus holds each kind of input', () => {
  const count = (prefix: string): number => corpus.entries.filter((e) => e.name.startsWith(prefix)).length;
  assert.ok(count('page-') >= 49);
  assert.ok(count('bad-cut-') >= 14);
  assert.ok(count('bad-version-') >= 18);
  assert.ok(count('bad-id-') >= 23);
  assert.ok(count('bad-percent-') >= 20);
  assert.ok(count('bad-shape-') >= 20);
  const sizes = corpus.entries.map((e) => hashOf(e).length);
  assert.ok(sizes.some((n) => n >= 64 * 1024), 'no entry of 64 kB');
  assert.ok(sizes.includes(MAX_HASH + 1) && sizes.includes(MAX_HASH + 2), 'no entry at the length limit');
  assert.ok(corpus.entries.some((e) => hashOf(e) === ''), 'no empty hash');
  // Every function and every part of the starting catalogue is ticked by at least one entry.
  for (const id of corpus.ids.functions) assert.ok(corpus.entries.some((e) => e.expect.functions.includes(id)), id);
  for (const id of corpus.ids.owned) assert.ok(corpus.entries.some((e) => e.expect.owned.includes(id)), id);
  for (const id of corpus.ids.parts) assert.ok(corpus.entries.some((e) => e.expect.parts.includes(id)), id);
});

test('the files are in the form the tool writes', () => {
  assert.equal(readFileSync(CORPUS_FILE, 'utf8'), corpusText(corpus));
  assert.equal(readFileSync(WIRES_FILE, 'utf8'), wiresText(wires));
  assert.deepEqual(Object.keys(wires), corpus.entries.map((e) => e.name));
  if (later.size === 0) assert.deepEqual(wiresOf(corpus), wires);
});

test('the tool appends and never changes an entry', () => {
  const more = added(corpus, 'test-entry', 'An entry of this test.', '#v=1&f=servo&x=test-entry');
  assert.equal(more.entries.length, corpus.entries.length + 1);
  assert.deepEqual(more.entries.slice(0, -1), corpus.entries);
  assert.deepEqual(more.entries.at(-1)?.expect, expectationOf('#v=1&f=servo&x=test-entry'));
  assert.throws(() => added(corpus, 'page-base', 'x', '#v=1&f=servo&x=other'), /holds an entry named page-base/);
  assert.throws(() => added(corpus, 'other', 'x', '#v=1'), /holds this hash as page-base/);
  assert.throws(() => added(corpus, 'Other Name', 'x', '#v=1&x=2'), /is no entry name/);
  assert.throws(() => added(corpus, 'other', ' ', '#v=1&x=2'), /states what it is in the corpus for/);
  assert.throws(() => added(corpus, 'other', 'x', { prefix: '#v=1&f=servo&x=', unit: 'b', count: 1986, suffix: '' }), /holds this hash as bad-limit-2000/);
});
