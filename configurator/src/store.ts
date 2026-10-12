// The state the browser keeps between visits, in localStorage under one key.
// Format: {"v":1,"lang":"de","setup":"v=1&f=<ids>&p=<ids>&o=<ids>"}
//
// `setup` is a selection in the format of the URL hash without the language
// key, so it holds ids alone and the limits of a link apply to it. `lang` is
// the language the visitor chose, or null. Either is null when it was never
// stored or does not pass the check.
//
// Reading and writing never throw. A browser that blocks storage, a full
// store, a value of another shape or of another version reads as the empty
// state.

import type { Lang, Options } from '../contract.ts';
import { MAX_HASH, parseHash } from './hash.ts';

export const STORE_KEY = 'rcbench.configurator';
export const STORE_VERSION = 1;
/** A longer stored value is not parsed. */
export const MAX_STORED = 4096;

export type Stored = {
  /** The language the visitor chose, or null. */
  lang: Lang | null;
  /** The selection the visitor made, as a hash without '#' and without the language key, or null. */
  setup: string | null;
};

/** The part of `Storage` the page uses. */
export type Store = Pick<Storage, 'getItem' | 'setItem'>;

const empty = (): Stored => ({ lang: null, setup: null });

/** The localStorage of the browser, or null where the browser has none or blocks it. */
export function browserStore(): Store | null {
  try {
    return typeof localStorage === 'object' && localStorage !== null ? localStorage : null;
  } catch {
    return null;
  }
}

/** The stored state. The fields that do not pass the check are null. */
export function readStore(store: Store | null, defaults: Options): Stored {
  const out = empty();
  let data: unknown;
  try {
    const raw = store ? store.getItem(STORE_KEY) : null;
    if (typeof raw !== 'string' || raw.length > MAX_STORED) return out;
    data = JSON.parse(raw);
  } catch {
    return out;
  }
  if (typeof data !== 'object' || data === null || Array.isArray(data)) return out;
  const { v, lang, setup } = data as Record<string, unknown>;
  if (v !== STORE_VERSION) return out;
  if (lang === 'en' || lang === 'de') out.lang = lang;
  if (typeof setup === 'string' && setup !== '' && setup.length <= MAX_HASH && !parseHash(setup, defaults).badVersion) out.setup = setup;
  return out;
}

/** Stores the state. Returns false when the browser did not take it. */
export function writeStore(store: Store | null, stored: Stored): boolean {
  if (!store) return false;
  try {
    store.setItem(STORE_KEY, JSON.stringify({ v: STORE_VERSION, lang: stored.lang, setup: stored.setup }));
    return true;
  } catch {
    return false;
  }
}
