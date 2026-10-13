// URL hash codec. Format: #v=1&f=<ids>&p=<ids>&o=<ids>&h=<rows>&l=<lang>
//
// `h` holds the "have it" ticks of the parts list: one token per row, the
// part id alone or the part id, a dot and the row id (`wire.dmm`). Which
// tokens name a row to buy depends on the setup, so the page decides that.
//
// Parsing never throws. Function and part ids are passed on as written, the
// resolver decides which exist. Option tokens are a closed set, so the parser
// decides those.
//
// A test object is no owned part. A link that names one under `p=` reads as
// the tick of the function that drives it without a further part: `p=servo`
// as `f=servo`, `p=esc` as `f=esc`. Parts, wires and functions stay the same.

import type { Catalogue, Lang, Options, Selection } from '../contract.ts';
import { alwaysOn, plainDrive, rowToken } from './core.ts';

export const HASH_VERSION = 1;
/** A longer hash is treated as a link from another version. */
export const MAX_HASH = 2000;
/** Ids kept per key. Further ids of a key are dropped. */
export const MAX_IDS = 64;
/** Ids named in a notice. */
export const MAX_REPORTED = 8;

const ID = /^[a-z0-9]{1,32}$/;
/** A row token: a part id, or a part id, a dot and a row id of 1 to 12 characters. */
const ROW = /^[a-z0-9]{1,32}(\.[a-z0-9]{1,12})?$/;

/** Hash token of each option value that differs from the default. */
const TOKENS: ReadonlyArray<readonly [string, keyof Options, string]> = [
  ['osoyoo', 'connection', 'osoyoo'],
  ['xt90', 'ina228Board', 'xt90'],
  ['pd', 'escPower', 'pd'],
  ['servo5v', 'servoPower', 'bench'],
];

export type Parsed = {
  selection: Selection;
  /** Language named by the hash, or null. */
  lang: Lang | null;
  /** The hash comes from another format version. The selection is the base setup. */
  badVersion: boolean;
  /** Tokens the parser rejects: malformed ids and row tokens, unknown option tokens. */
  unknown: string[];
  /** The row tokens of the "have it" ticks, as written. */
  have: string[];
};

function list(value: string | null, unknown: string[], form: RegExp = ID): string[] {
  const out: string[] = [];
  for (const raw of (value ?? '').split(',')) {
    if (raw === '') continue;
    if (!form.test(raw)) unknown.push(raw);
    else if (!out.includes(raw) && out.length < MAX_IDS) out.push(raw);
  }
  return out;
}

export function parseHash(hash: string, defaults: Options): Parsed {
  const base = (): Selection => ({ functions: [], owned: [], options: { ...defaults } });
  const out: Parsed = { selection: base(), lang: null, badVersion: false, unknown: [], have: [] };
  if (typeof hash !== 'string') return out;
  const body = hash.startsWith('#') ? hash.slice(1) : hash;
  if (body === '') return out;
  if (body.length > MAX_HASH) {
    out.badVersion = true;
    return out;
  }
  let q: URLSearchParams;
  try {
    q = new URLSearchParams(body);
  } catch {
    out.badVersion = true;
    return out;
  }
  const l = q.get('l');
  if (l === 'de' || l === 'en') out.lang = l;

  const v = q.get('v');
  if (v !== String(HASH_VERSION)) {
    out.badVersion = v !== null || q.has('f') || q.has('p') || q.has('o') || q.has('h');
    return out;
  }
  out.selection.functions = list(q.get('f'), out.unknown);
  out.selection.owned = list(q.get('p'), out.unknown);
  const options: Record<string, string> = { ...defaults };
  for (const raw of (q.get('o') ?? '').split(',')) {
    if (raw === '') continue;
    const token = TOKENS.find((x) => x[0] === raw);
    if (token) options[token[1]] = token[2];
    else if (!out.unknown.includes(raw)) out.unknown.push(raw);
  }
  out.selection.options = options as Options;
  out.have = list(q.get('h'), out.unknown, ROW);
  return out;
}

/**
 * The hash names a setup: it carries a key other than the language, or it is
 * longer than MAX_HASH. An empty hash and `#l=de` name none.
 */
export function namesSetup(hash: string): boolean {
  if (typeof hash !== 'string') return false;
  const body = hash.startsWith('#') ? hash.slice(1) : hash;
  if (body === '') return false;
  if (body.length > MAX_HASH) return true;
  try {
    const q = new URLSearchParams(body);
    return [...q.keys()].some((key) => key !== 'l');
  } catch {
    return true;
  }
}

/**
 * The selection with every test object under the owned parts turned into the
 * tick of the function that drives it and needs no further part. A test
 * object without such a function is dropped from the owned parts.
 */
export function testObjectTicks(selection: Selection, catalogue: Catalogue): Selection {
  const objects = catalogue.parts.filter((p) => p.testObject === true).map((p) => p.id);
  const named = selection.owned.filter((id) => objects.includes(id));
  if (named.length === 0) return selection;
  const functions = [...selection.functions];
  for (const id of named) {
    const fn = plainDrive(catalogue, id);
    if (fn !== null && !functions.includes(fn)) functions.push(fn);
  }
  return { functions, owned: selection.owned.filter((id) => !objects.includes(id)), options: { ...selection.options } };
}

/**
 * The hash of a selection, without '#'. Ids stand in catalogue order; ids the
 * catalogue does not know are left out, and so is a function every setup
 * runs: it has no tick. `have` holds the row tokens of the "have it" ticks;
 * they stand in catalogue order, and a token that names no row of the
 * catalogue is left out. `lang` is written when it is not the language the
 * page starts in without a hash.
 */
export function formatHash(selection: Selection, lang: Lang, startLang: Lang, catalogue: Catalogue, have: readonly string[] = []): string {
  const keys = [`v=${HASH_VERSION}`];
  const f = catalogue.functions.filter((x) => !alwaysOn(catalogue, x)).map((x) => x.id).filter((id) => selection.functions.includes(id));
  const p = catalogue.parts.filter((x) => x.selectable).map((x) => x.id).filter((id) => selection.owned.includes(id));
  const o = TOKENS.filter((x) => selection.options[x[1]] === x[2]).map((x) => x[0]);
  if (f.length > 0) keys.push(`f=${f.join(',')}`);
  if (p.length > 0) keys.push(`p=${p.join(',')}`);
  if (o.length > 0) keys.push(`o=${o.join(',')}`);
  const h = catalogue.parts.flatMap((x) => x.buy.map((row) => rowToken(x, row))).filter((token) => have.includes(token));
  if (h.length > 0) keys.push(`h=${h.join(',')}`);
  if (lang !== startLang) keys.push(`l=${lang}`);
  return keys.join('&');
}

/**
 * Parts that were a tick under "Parts you own" in links of version 1 and are
 * brought by a rule now. A link that names one stays valid.
 */
export const FORMER_TICKS: readonly string[] = ['usbtap'];

/**
 * The selection of a link without its former ticks. Only the owned parts hold
 * them: a function or an option key of that spelling stays and is reported.
 */
export function withoutFormerTicks(selection: Selection): Selection {
  return { ...selection, owned: selection.owned.filter((id) => !FORMER_TICKS.includes(id)) };
}

/** The ids of a notice as text: at most MAX_REPORTED, each cut to 32 characters. */
export function reportIds(ids: string[]): string {
  const unique = [...new Set(ids)];
  const shown = unique.slice(0, MAX_REPORTED).map((id) => (id.length > 32 ? `${id.slice(0, 32)}…` : id));
  return shown.join(', ') + (unique.length > MAX_REPORTED ? ', …' : '');
}

/**
 * A hash as it goes into a document, with '#'. The page keeps the link that
 * opened it until a change, so its address can name a function every setup
 * runs, a test object or a former tick as an owned part, or a "have it" tick
 * that names no row to buy of the setup. Such a hash is written again from
 * its selection: the function ids are left out, the test object becomes the
 * tick of its function, the former tick is dropped, the "have it" ticks are
 * `have`; the language key stays. `have` holds the ticks the page shows; when
 * it is absent, the ticks of the hash stay. Every other hash is returned as it
 * is.
 */
export function shareHash(hash: string, defaults: Options, catalogue: Catalogue, have?: readonly string[]): string {
  const parsed = parseHash(hash, defaults);
  const named = parsed.selection.functions.some((id) => catalogue.functions.some((f) => f.id === id && alwaysOn(catalogue, f)))
    || parsed.selection.owned.some((id) => FORMER_TICKS.includes(id) || catalogue.parts.some((p) => p.id === id && p.testObject === true))
    || (have !== undefined && parsed.have.some((token) => !have.includes(token)));
  if (!named) return hash;
  const lang = parsed.lang;
  // The start language is set so that a language key in the hash is written and none is added.
  return `#${formatHash(testObjectTicks(parsed.selection, catalogue), lang ?? 'en', lang === 'en' ? 'de' : 'en', catalogue, have ?? parsed.have)}`;
}

/**
 * An address as it goes into the clipboard, the print header and a document:
 * the part before '#' as it is, the hash through shareHash with `have`. An address
 * without '#' is returned as it is.
 */
export function shareAddress(href: string, defaults: Options, catalogue: Catalogue, have?: readonly string[]): string {
  const at = href.indexOf('#');
  return at < 0 ? href : href.slice(0, at) + shareHash(href.slice(at), defaults, catalogue, have);
}
