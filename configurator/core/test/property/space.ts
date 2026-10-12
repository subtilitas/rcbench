// The selection space of the property tests, and the generator of the sample.
//
// Functions that need a part or drive a test object: every subset under every
// option combination. A function that does neither is in every setup; a
// selection names it in the sample alone.
// Parts you own: every subset under every option combination.
// Both together: 2^(functions + parts) * options is past the limit, so a
// sample from a generator with a fixed seed stands in for it.

import type { Catalogue, Options, Selection } from '../../../contract.ts';
import { OPTION_VALUES } from '../../resolve.ts';

/** A space with more cases than this is sampled, not enumerated. */
export const CASE_LIMIT = 100_000;

/** Seed of every sample. A failing case prints it with the index of the case. */
export const SEED = 0x2026_1011;

/** Mixed selections of the sample. */
export const MIXED = 25_000;

// The pin of the servo signal is in no selection the page makes: the function
// that programs a servo forces it. The option sets keep the standard outputs.
export const OPTION_SETS: ReadonlyArray<Options> = OPTION_VALUES.connection.flatMap((connection) =>
  OPTION_VALUES.ina228Board.flatMap((ina228Board) =>
    OPTION_VALUES.escPower.flatMap((escPower) =>
      OPTION_VALUES.servoPower.map((servoPower) => ({ connection, ina228Board, escPower, servoPower, servoPort: 'outputs' as const })))));

/** mulberry32: a 32-bit generator with a fixed sequence per seed. */
export function generator(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let x = state;
    x = Math.imul(x ^ (x >>> 15), x | 1);
    x ^= x + Math.imul(x ^ (x >>> 7), x | 61);
    return ((x ^ (x >>> 14)) >>> 0) / 4294967296;
  };
}

/** The ids whose bit is set in `mask`, in the order of `ids`. */
export function subset(ids: ReadonlyArray<string>, mask: number): string[] {
  return ids.filter((_, i) => (mask & (1 << i)) !== 0);
}

/** A copy in an order drawn from `random` (Fisher-Yates). */
export function shuffled<T>(list: ReadonlyArray<T>, random: () => number): T[] {
  const out = [...list];
  for (let i = out.length - 1; i > 0; i--) {
    const j = Math.floor(random() * (i + 1));
    const held = out[i] as T;
    out[i] = out[j] as T;
    out[j] = held;
  }
  return out;
}

export type Space = {
  /** The functions that need a part or drive a test object: the ones a selection turns on. */
  functions: string[];
  /** The functions that need no part and drive no test object. */
  base: string[];
  selectable: string[];
  /** Cases of the function space: subsets times option combinations. */
  functionCases: number;
  /** Cases of the parts space: subsets times option combinations. */
  partCases: number;
  /** Cases of the product of both spaces. */
  productCases: number;
};

export function spaceOf(catalogue: Catalogue): Space {
  const ticked = (f: Catalogue['functions'][number]): boolean => f.needs.length > 0 || f.drives !== undefined;
  const functions = catalogue.functions.filter(ticked).map((f) => f.id);
  const base = catalogue.functions.filter((f) => !ticked(f)).map((f) => f.id);
  const selectable = catalogue.parts.filter((p) => p.selectable).map((p) => p.id);
  const functionCases = 2 ** functions.length * OPTION_SETS.length;
  const partCases = 2 ** selectable.length * OPTION_SETS.length;
  return {
    functions,
    base,
    selectable,
    functionCases,
    partCases,
    productCases: 2 ** (functions.length + selectable.length) * OPTION_SETS.length,
  };
}

/** A case of a space, with the text that names it in a failure. */
export type Case = { selection: Selection; where: string };

function describe(selection: Selection): string {
  return JSON.stringify(selection);
}

/** Every subset of `ids` as `key` of a selection, under every option combination. */
export function* enumerate(ids: ReadonlyArray<string>, key: 'functions' | 'owned'): Generator<Case & { mask: number; option: number }> {
  const count = 2 ** ids.length;
  for (let mask = 0; mask < count; mask++) {
    const chosen = subset(ids, mask);
    for (let option = 0; option < OPTION_SETS.length; option++) {
      const selection: Selection = {
        functions: key === 'functions' ? chosen : [],
        owned: key === 'owned' ? chosen : [],
        options: { ...(OPTION_SETS[option] as Options) },
      };
      yield { selection, where: describe(selection), mask, option };
    }
  }
}

/** `count` selections with functions, owned parts and options drawn from the generator. */
export function* sample(space: Space, count: number, seed: number): Generator<Case> {
  const random = generator(seed);
  const pick = (ids: string[]): string[] => {
    // The density changes from case to case, so sparse and full selections both occur.
    const density = random();
    return ids.filter(() => random() < density);
  };
  for (let index = 0; index < count; index++) {
    const selection: Selection = {
      functions: shuffled(pick([...space.functions, ...space.base]), random),
      owned: shuffled(pick(space.selectable), random),
      options: { ...(OPTION_SETS[Math.floor(random() * OPTION_SETS.length)] as Options) },
    };
    yield { selection, where: `${describe(selection)} (seed 0x${seed.toString(16)}, case ${index})` };
  }
}
