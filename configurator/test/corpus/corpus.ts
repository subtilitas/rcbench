// The hash corpus: links to the page, each with what the page makes of it.
//
// hashes.json  one entry per link. An entry is never edited and never removed.
// wires.json   the wire numbers of each entry under the catalogue of this commit.
//
// `outcome` states what the page does with a hash. It follows `readHash` of
// src/main.ts step by step; e2e/matrix/corpus.spec.ts holds the page to it.

import { readFileSync } from 'node:fs';

import type { Lang, Options, Selection, SimSettings } from '../../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, DEFAULT_SIM, resolve, sameSim } from '../../src/core.ts';
import { formatHash, parseHash, reportIds, withoutFormerTicks, testObjectTicks } from '../../src/hash.ts';

/** A hash too long to write out: `prefix`, then `unit` `count` times, then `suffix`. */
export type Built = { prefix: string; unit: string; count: number; suffix: string };

export type Outcome = {
  /** The ticked functions, in catalogue order. */
  functions: string[];
  /** The ticked parts under "Parts you own", in catalogue order. */
  owned: string[];
  options: Options;
  /** The language of the page, when the browser asks for English. */
  lang: Lang;
  /** The notices above the result: "err" for another version, "note" for ids that are left out. */
  notices: Array<'err' | 'note'>;
  /** The ids the note names, as the page writes them. Empty without a note. */
  reported: string;
  /** The hash the page writes after the first change that keeps this selection. */
  canonical: string;
  /** The parts of the setup, in catalogue order. */
  parts: string[];
  /** The functions the setup enables, in catalogue order. */
  enabled: string[];
  /** The values of the motor simulator. Absent when they are the default. */
  sim?: SimSettings;
};

export type Entry = {
  name: string;
  /** The hash with its number sign, or the recipe of a long one. */
  hash: string | Built;
  /** What the entry is in the corpus for. */
  why: string;
  expect: Outcome;
};

export type Corpus = {
  /** The ids of the catalogue when the corpus starts. A later id is outside every stored setup. */
  ids: { functions: string[]; owned: string[]; parts: string[] };
  entries: Entry[];
};

export type Wires = Record<string, number[]>;

export const CORPUS_FILE = new URL('./hashes.json', import.meta.url);
export const WIRES_FILE = new URL('./wires.json', import.meta.url);

export function readCorpus(): Corpus {
  return JSON.parse(readFileSync(CORPUS_FILE, 'utf8')) as Corpus;
}

export function readWires(): Wires {
  return JSON.parse(readFileSync(WIRES_FILE, 'utf8')) as Wires;
}

export function hashOf(entry: Pick<Entry, 'hash'>): string {
  const h = entry.hash;
  return typeof h === 'string' ? h : h.prefix + h.unit.repeat(h.count) + h.suffix;
}

function inOrder(ids: ReadonlyArray<string>, order: ReadonlyArray<string>): string[] {
  return order.filter((id) => ids.includes(id));
}

/** What the page makes of `hash` when the browser asks for `startLang`. */
export function outcome(hash: string, startLang: Lang = 'en'): Outcome & { wires: number[] } {
  const parsed = parseHash(hash, DEFAULT_OPTIONS);
  const lang = parsed.lang ?? startLang;
  const notices: Array<'err' | 'note'> = [];
  if (parsed.badVersion) notices.push('err');
  // A test object under the owned parts of a link is the tick of its function.
  const given = withoutFormerTicks(testObjectTicks(parsed.selection, CATALOGUE));
  const setup = resolve(CATALOGUE, given);
  const selection: Selection = {
    functions: given.functions.filter((id) => CATALOGUE.functions.some((f) => f.id === id)),
    owned: given.owned.filter((id) => CATALOGUE.parts.some((p) => p.id === id && p.selectable)),
    options: { ...given.options },
  };
  const dropped = [...given.functions, ...given.owned]
    .filter((id) => !selection.functions.includes(id) && !selection.owned.includes(id));
  const unknown = [...parsed.unknown, ...setup.ignored, ...dropped];
  if (unknown.length > 0) notices.push('note');
  // The page draws the selection reduced to known ids.
  const shown = resolve(CATALOGUE, selection);
  return {
    functions: inOrder(selection.functions, CATALOGUE.functions.map((f) => f.id)),
    owned: inOrder(selection.owned, CATALOGUE.parts.map((p) => p.id)),
    options: Object.fromEntries(shown.options.map((o) => [o.key, o.value])) as Options,
    lang,
    notices,
    reported: unknown.length > 0 ? reportIds(unknown) : '',
    canonical: `#${formatHash(selection, lang, startLang, CATALOGUE, parsed.sim)}`,
    parts: shown.parts.map((p) => p.part.id),
    enabled: shown.functions.filter((f) => f.enabled).map((f) => f.fn.id),
    ...(sameSim(parsed.sim, DEFAULT_SIM) ? {} : { sim: parsed.sim }),
    wires: shown.wires.map((w) => w.n),
  };
}

/** `outcome` without the wires: the part of it an entry stores. */
export function expectationOf(hash: string): Outcome {
  const { wires: _wires, ...rest } = outcome(hash);
  return rest;
}

/** The ids of the catalogue an entry is compared in. */
export function idsOf(): Corpus['ids'] {
  return {
    functions: CATALOGUE.functions.map((f) => f.id),
    owned: CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id),
    parts: CATALOGUE.parts.map((p) => p.id),
  };
}

/**
 * The owned parts an entry names that the corpus started with as ticks and the
 * catalogue offers no tick for now, test objects aside (a link reads those as
 * the tick of their function). The page leaves such a part out of the owned
 * parts without a notice; a rule may still bring it into the setup.
 */
export function namesRetired(entry: Pick<Entry, 'hash'>, corpus: Corpus): string[] {
  const now = idsOf();
  const objects = CATALOGUE.parts.filter((p) => p.testObject === true).map((p) => p.id);
  const gone = corpus.ids.owned.filter((id) => !now.owned.includes(id) && !objects.includes(id));
  return parseHash(hashOf(entry), DEFAULT_OPTIONS).selection.owned.filter((id) => gone.includes(id));
}

/**
 * Whether the hash of an entry names a function or a part the catalogue has
 * added since the corpus started. Such an entry means another setup than the
 * stored one from the day the id is added.
 */
export function namesLater(entry: Pick<Entry, 'hash'>, corpus: Corpus): boolean {
  const now = idsOf();
  const later = new Set([
    ...now.functions.filter((id) => !corpus.ids.functions.includes(id)),
    ...now.owned.filter((id) => !corpus.ids.owned.includes(id)),
  ]);
  const parsed = parseHash(hashOf(entry), DEFAULT_OPTIONS);
  return [...parsed.selection.functions, ...parsed.selection.owned].some((id) => later.has(id));
}
