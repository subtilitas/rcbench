// The setups every matrix spec runs on, and what a project of the matrix states about itself.

import type { TestInfo } from '@playwright/test';

import type { Lang, Selection } from '../../contract.ts';
import { OPTION_SETS } from '../../core/test/property/space.ts';
import { CATALOGUE, resolve } from '../../src/core.ts';
import { FUNCTIONS, OWNED, selectionOf } from '../lib/setup.ts';

export type Case = { name: string; selection: Selection };

/** Every function and every part ticked, under the options that give the most wires. */
function largest(): Selection {
  let best = selectionOf({ functions: FUNCTIONS, owned: OWNED });
  for (const options of OPTION_SETS) {
    const candidate = selectionOf({ functions: FUNCTIONS, owned: OWNED, options });
    if (resolve(CATALOGUE, candidate).wires.length > resolve(CATALOGUE, best).wires.length) best = candidate;
  }
  return best;
}

/** One function and one part of the list "not in a release", next to a function of a release. */
function notInRelease(): Selection {
  const listed = CATALOGUE.notInRelease.map((n) => n.id);
  const fn = listed.find((id) => FUNCTIONS.includes(id));
  const part = listed.find((id) => OWNED.includes(id));
  return selectionOf({ functions: [FUNCTIONS[0] as string, ...(fn ? [fn] : [])], owned: part ? [part] : [] });
}

export const SETUPS: Case[] = [
  { name: 'empty', selection: selectionOf() },
  ...FUNCTIONS.map((id) => ({ name: `function ${id}`, selection: selectionOf({ functions: [id] }) })),
  { name: 'largest', selection: largest() },
  { name: 'not in a release', selection: notInRelease() },
];

export type Cell = { lang: Lang; theme: 'light' | 'dark'; phone: boolean };

/** The cell of the matrix a test runs in, from the metadata of its project. */
export function cellOf(info: TestInfo): Cell {
  const meta = info.project.metadata as Partial<Cell>;
  return { lang: meta.lang ?? 'en', theme: meta.theme ?? 'light', phone: meta.phone ?? false };
}
