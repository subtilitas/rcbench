// Opens a setup in the page and reads the setup the page shows.

import { expect } from '@playwright/test';
import type { Page } from '@playwright/test';

import type { Lang, Options, Selection } from '../../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../../src/core.ts';
import { formatHash } from '../../src/hash.ts';

/** The selectors of the page the tests share. A change of the markup is made here once. */
export const SEL = {
  drawing: 'svg.drawing',
  wire: 'svg.drawing path.w:not(.x)',
  row: 'table.wires tbody tr',
  rowNumber: 'table.wires tbody tr td.n b',
  part: '.parts li',
  notice: '.msg:not(#note)',
  note: '#note',
  summary: '#result .sum p',
  own: '#own',
  ownSummary: '#own-summary',
  pan: '#pan',
  live: '#live',
  bar: '#bar',
  copy: '#b-copy',
  print: '#b-print',
  reset: '#b-reset',
  pdf: '#b-pdf',
  baseRows: '#choose ul.base li',
  doesBase: '#does-base li',
  pdfStatus: '#pdfmsg',
  pdfLink: '#pdflink',
  noticeLink: '#noticelink',
  partsHead: '#h-parts',
  question: '#rmwarn',
  yes: '#rm-yes',
  no: '#rm-no',
  fn: (id: string): string => `#f-${id}`,
  owned: (id: string): string => `#p-${id}`,
  remove: (id: string): string => `#rm-${id}`,
  option: (key: keyof Options, value: string): string => `#o-${key}-${value}`,
  lang: (lang: Lang): string => `.lang button[data-lang="${lang}"]`,
} as const;

/** The functions with a tick: those that need a part. */
export const FUNCTIONS: string[] = CATALOGUE.functions.filter((f) => f.needs.length > 0).map((f) => f.id);
/** The functions that need no part: a list without controls, in no hash. */
export const BASE_FUNCTIONS: string[] = CATALOGUE.functions.filter((f) => f.needs.length === 0).map((f) => f.id);
export const OWNED: string[] = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);

/** A selection from the parts of one: what is left out is empty or the default. */
export function selectionOf(part: { functions?: string[]; owned?: string[]; options?: Partial<Options> } = {}): Selection {
  return {
    functions: part.functions ?? [],
    owned: part.owned ?? [],
    options: { ...DEFAULT_OPTIONS, ...part.options },
  };
}

/**
 * The hash of a selection with its number sign, as the page writes it. `lang`
 * is written out, so the page shows that language whatever the browser asks for.
 */
export function hashOf(selection: Selection, lang?: Lang): string {
  const hash = formatHash(selection, 'en', 'en', CATALOGUE);
  return `#${hash}${lang ? `&l=${lang}` : ''}`;
}

/**
 * Loads the page with a hash and waits for the drawing. A navigation that
 * changes the hash alone does not load the page, so the page starts from blank.
 */
export async function openHash(page: Page, hash: string): Promise<void> {
  await page.goto('about:blank');
  await page.goto(`./${hash}`);
  await page.locator(SEL.drawing).waitFor();
}

/** Loads the page with a selection. */
export async function openSetup(page: Page, selection: Selection, lang?: Lang): Promise<void> {
  await openHash(page, hashOf(selection, lang));
}

export type Shown = {
  lang: string;
  /** The hash of the address bar, with its number sign. */
  hash: string;
  /** The ticked functions, in catalogue order. */
  functions: string[];
  /** The ticked rows under "Parts you own", in catalogue order: the selectable parts of the setup. */
  owned: string[];
  /** The parts with a remove control on their card, in catalogue order. */
  removable: string[];
  /** The checked value of each variant group the page shows. */
  options: Partial<Options>;
  /** The names on the part cards, in the order of `parts`. */
  partNames: string[];
  /** The part ids of the cards, by their names, in catalogue order. A name of no part comes last, with a question mark. */
  parts: string[];
  /** The wire numbers of the table, ascending. */
  wires: number[];
  /** The count of wires in the drawing. */
  drawn: number;
  notices: Array<{ kind: 'err' | 'note'; text: string }>;
  /** What the page states after a function leaves the selection. Empty without such a statement. */
  note: string;
  summary: string;
};

const PARTS: string[] = CATALOGUE.parts.map((p) => p.id);

/** The place of an id in a list; an id outside the list comes after it. */
function place(order: string[], id: string): number {
  const at = order.indexOf(id);
  return at < 0 ? order.length : at;
}

/** `ids` in the order of `order`. The page is free to show them in another one. */
function inOrder(ids: string[], order: string[]): string[] {
  return [...ids].sort((a, b) => place(order, a) - place(order, b));
}

/** The name on the card of each part under a set of options. */
function cardNames(lang: Lang, options: Options): Map<string, string> {
  const names = new Map<string, string>();
  for (const part of CATALOGUE.parts) {
    const variant = part.variantName ? part.variantName.names[options[part.variantName.option]] : undefined;
    names.set(part.name[lang] + (variant ? `, ${variant[lang]}` : ''), part.id);
  }
  return names;
}

/** The setup the page shows, read from the document. */
export async function readSetup(page: Page): Promise<Shown> {
  const raw = await page.evaluate((sel) => {
    const all = <T extends Element>(q: string): T[] => [...document.querySelectorAll<T>(q)];
    const options: Record<string, string> = {};
    for (const input of all<HTMLInputElement>('input[type=radio][data-o]')) {
      if (input.checked && input.dataset.o) options[input.dataset.o] = input.value;
    }
    return {
      lang: document.documentElement.lang,
      hash: location.hash,
      functions: all<HTMLInputElement>('input[data-f]').filter((i) => i.checked).map((i) => i.dataset.f ?? ''),
      owned: all<HTMLInputElement>('input[data-p]').filter((i) => i.checked).map((i) => i.dataset.p ?? ''),
      removable: all<HTMLElement>('button[data-rm]').map((b) => b.dataset.rm ?? ''),
      options,
      partNames: all(`${sel.part} > div:first-child > b:first-of-type`).map((b) => (b.textContent ?? '').trim()),
      wires: all(sel.rowNumber).map((b) => Number(b.textContent)),
      drawn: all(sel.wire).length,
      notices: all(sel.notice).map((m) => ({ kind: m.classList.contains('err') ? 'err' as const : 'note' as const, text: (m.textContent ?? '').trim() })),
      note: (document.querySelector(sel.note)?.textContent ?? '').trim(),
      summary: (document.querySelector(sel.summary)?.textContent ?? '').trim(),
    };
  }, { part: SEL.part, rowNumber: SEL.rowNumber, wire: SEL.wire, notice: SEL.notice, note: SEL.note, summary: SEL.summary });
  const lang: Lang = raw.lang === 'de' ? 'de' : 'en';
  const names = cardNames(lang, { ...DEFAULT_OPTIONS, ...raw.options } as Options);
  const cards = raw.partNames
    .map((name) => ({ name, id: names.get(name) ?? `?${name}` }))
    .sort((a, b) => place(PARTS, a.id) - place(PARTS, b.id));
  return {
    ...raw,
    functions: inOrder(raw.functions, FUNCTIONS),
    owned: inOrder(raw.owned, PARTS),
    removable: inOrder(raw.removable, PARTS),
    partNames: cards.map((c) => c.name),
    parts: cards.map((c) => c.id),
    wires: [...raw.wires].sort((a, b) => a - b),
  };
}

/** What the page shows for a selection, as the resolver states it. */
export function expectedOf(selection: Selection): Pick<Shown, 'functions' | 'owned' | 'removable' | 'options' | 'parts' | 'wires' | 'drawn'> {
  const setup = resolve(CATALOGUE, selection);
  const wires = setup.wires.map((w) => w.n).sort((a, b) => a - b);
  return {
    functions: inOrder(setup.functions.filter((f) => f.chosen).map((f) => f.fn.id), FUNCTIONS),
    owned: CATALOGUE.parts.filter((p) => p.selectable && setup.parts.some((r) => r.part.id === p.id)).map((p) => p.id),
    removable: inOrder(setup.parts.filter((p) => !p.part.locked).map((p) => p.part.id), PARTS),
    // A variant group the resolver states as visible, with its value. No other group is on the page.
    options: Object.fromEntries(setup.options.filter((o) => o.visible).map((o) => [o.key, o.value])),
    parts: inOrder(setup.parts.map((p) => p.part.id), PARTS),
    wires,
    drawn: wires.length,
  };
}

/** Asserts that the page shows the setup of a selection: ticks, part cards, remove controls, variant groups, table rows and drawn wires. */
export async function expectSetup(page: Page, selection: Selection): Promise<Shown> {
  const want = expectedOf(selection);
  await expect(page.locator(SEL.row)).toHaveCount(want.wires.length);
  const shown = await readSetup(page);
  expect({
    functions: shown.functions, owned: shown.owned, removable: shown.removable, options: shown.options, parts: shown.parts, wires: shown.wires, drawn: shown.drawn,
  }).toEqual(want);
  return shown;
}

/** `shown` without the hash, the language and the texts: what a change of language or a reload keeps. */
export function stateOf(shown: Shown): Omit<Shown, 'hash' | 'lang' | 'partNames' | 'notices' | 'note' | 'summary'> {
  const { hash: _hash, lang: _lang, partNames: _names, notices: _notices, note: _note, summary: _summary, ...rest } = shown;
  return rest;
}
