// The setups every matrix spec runs on, and what a project of the matrix states about itself.

import type { Page, TestInfo } from '@playwright/test';

import type { Lang, Selection } from '../../contract.ts';
import { OPTION_SETS } from '../../core/test/property/space.ts';
import { CATALOGUE, resolve } from '../../src/core.ts';
import { BASE_FUNCTIONS, FUNCTIONS, OWNED, hashOf, openHash, selectionOf } from '../lib/setup.ts';

export type Case = {
  name: string;
  /** The selection the page holds once the link is read. */
  selection: Selection;
  /** The link the case opens, when it is another one than the page writes for `selection`. */
  hash?: string;
  /** The ids of the link that the page reports as ignored. */
  ignored: string[];
};

/** Every function and every part ticked, under the options that give the most wires. */
function largest(): Selection {
  let best = selectionOf({ functions: FUNCTIONS, owned: OWNED });
  for (const options of OPTION_SETS) {
    const candidate = selectionOf({ functions: FUNCTIONS, owned: OWNED, options });
    if (resolve(CATALOGUE, candidate).wires.length > resolve(CATALOGUE, best).wires.length) best = candidate;
  }
  return best;
}

/**
 * A link that names every id of the list "not in a release" as a function and
 * as a part, next to the first function of a release and the first function
 * that needs a part. No id of the list is a function or a part of the
 * catalogue: the page ignores each and reports it.
 */
function notInRelease(): Case {
  const listed = CATALOGUE.notInRelease.map((n) => n.id);
  const withPart = CATALOGUE.functions.find((f) => f.needs.length > 0 && f.id !== FUNCTIONS[0])?.id;
  const functions = [FUNCTIONS[0] as string, ...(withPart ? [withPart] : [])];
  return {
    name: 'not in a release',
    selection: selectionOf({ functions }),
    hash: `#v=1&f=${[...functions, ...listed].join(',')}&p=${listed.join(',')}`,
    ignored: listed,
  };
}

export const SETUPS: Case[] = [
  { name: 'empty', selection: selectionOf(), ignored: [] },
  ...FUNCTIONS.map((id) => ({ name: `function ${id}`, selection: selectionOf({ functions: [id] }), ignored: [] })),
  { name: 'largest', selection: largest(), ignored: [] },
  notInRelease(),
  // A link that names the functions that need no part: the page shows the setup of the other ids and writes a hash without them.
  {
    name: 'functions without a part in the link',
    selection: selectionOf({ functions: [FUNCTIONS[0] as string] }),
    hash: `#v=1&f=${[...BASE_FUNCTIONS, FUNCTIONS[0] as string].join(',')}`,
    ignored: [],
  },
];

/** Loads the page with the link of a case and waits for the drawing. */
export async function openCase(page: Page, setup: Case): Promise<void> {
  await openHash(page, setup.hash ?? hashOf(setup.selection));
}

export type Cell = { lang: Lang; theme: 'light' | 'dark'; phone: boolean };

/** The cell of the matrix a test runs in, from the metadata of its project. */
export function cellOf(info: TestInfo): Cell {
  const meta = info.project.metadata as Partial<Cell>;
  return { lang: meta.lang ?? 'en', theme: meta.theme ?? 'light', phone: meta.phone ?? false };
}
