// URL hash codec. Format: #v=1&f=<ids>&p=<ids>&o=<ids>&l=<lang>
//
// Parsing never throws. Function and part ids are passed on as written, the
// resolver decides which exist. Option tokens are a closed set, so the parser
// decides those.

import type { Catalogue, Lang, Options, Selection } from '../contract.ts';

export const HASH_VERSION = 1;
/** A longer hash is treated as a link from another version. */
export const MAX_HASH = 2000;
/** Ids kept per key. Further ids of a key are dropped. */
export const MAX_IDS = 64;
/** Ids named in a notice. */
export const MAX_REPORTED = 8;

const ID = /^[a-z0-9]{1,32}$/;

/** Hash token of each option value that differs from the default. */
const TOKENS: ReadonlyArray<readonly [string, keyof Options, string]> = [
  ['osoyoo', 'connection', 'osoyoo'],
  ['xt90', 'ina228Board', 'xt90'],
  ['pd', 'escPower', 'pd'],
];

export type Parsed = {
  selection: Selection;
  /** Language named by the hash, or null. */
  lang: Lang | null;
  /** The hash comes from another format version. The selection is the base setup. */
  badVersion: boolean;
  /** Tokens the parser rejects: malformed ids and unknown option tokens. */
  unknown: string[];
};

function list(value: string | null, unknown: string[]): string[] {
  const out: string[] = [];
  for (const raw of (value ?? '').split(',')) {
    if (raw === '') continue;
    if (!ID.test(raw)) unknown.push(raw);
    else if (!out.includes(raw) && out.length < MAX_IDS) out.push(raw);
  }
  return out;
}

export function parseHash(hash: string, defaults: Options): Parsed {
  const base = (): Selection => ({ functions: [], owned: [], options: { ...defaults } });
  const out: Parsed = { selection: base(), lang: null, badVersion: false, unknown: [] };
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
    out.badVersion = v !== null || q.has('f') || q.has('p') || q.has('o');
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
  return out;
}

/**
 * The hash of a selection, without '#'. Ids stand in catalogue order; ids the
 * catalogue does not know are left out. `lang` is written when it is not the
 * language the page starts in without a hash.
 */
export function formatHash(selection: Selection, lang: Lang, startLang: Lang, catalogue: Catalogue): string {
  const keys = [`v=${HASH_VERSION}`];
  const f = catalogue.functions.map((x) => x.id).filter((id) => selection.functions.includes(id));
  const p = catalogue.parts.filter((x) => x.selectable).map((x) => x.id).filter((id) => selection.owned.includes(id));
  const o = TOKENS.filter((x) => selection.options[x[1]] === x[2]).map((x) => x[0]);
  if (f.length > 0) keys.push(`f=${f.join(',')}`);
  if (p.length > 0) keys.push(`p=${p.join(',')}`);
  if (o.length > 0) keys.push(`o=${o.join(',')}`);
  if (lang !== startLang) keys.push(`l=${lang}`);
  return keys.join('&');
}

/** The ids of a notice as text: at most MAX_REPORTED, each cut to 32 characters. */
export function reportIds(ids: string[]): string {
  const unique = [...new Set(ids)];
  const shown = unique.slice(0, MAX_REPORTED).map((id) => (id.length > 32 ? `${id.slice(0, 32)}…` : id));
  return shown.join(', ') + (unique.length > MAX_REPORTED ? ', …' : '');
}
