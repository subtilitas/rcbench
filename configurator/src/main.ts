// Page state, events and rendering. The selection lives in the URL hash. The
// browser keeps the selection and the language the visitor chose between
// visits (`store.ts`).
//
// Precedence at a load and at a change of the hash:
// - Setup: a hash that names one, else the stored one, else the base setup.
// - Language: the key `l` of the hash, else the stored one, else the browser's.
// A link stores nothing. The page stores the setup when the visitor changes
// it, and the language when the visitor picks one.

import './style.css';
import type { Catalogue, Lang, OptionKey, Options, Selection, Setup, SimSettings } from '../contract.ts';
import {
  CATALOGUE, DEFAULT_OPTIONS, DEFAULT_SIM, alwaysOn, deselectEffect, removalEffect, resolve, sameSim, validSim, withSim,
} from './core.ts';
import type { Deselect, Removal } from './core.ts';
import { exportPdf, syncPdf } from './export/button.ts';
import { HASH_VERSION, formatHash, namesSetup, parseHash, withoutFormerTicks, reportIds, shareAddress, testObjectTicks } from './hash.ts';
import { browserLang, t } from './i18n.ts';
import { benchParts, optionsOf } from './names.ts';
import { browserStore, readStore, writeStore } from './store.ts';
import { chooser, deselectText } from './view/chooser.ts';
import type { Pending, View } from './view/chooser.ts';
import { haveTicks, listCsv, listText } from './view/buy.ts';
import { result, resultError, summary } from './view/result.ts';
import { haveAfterSim, simSaid } from './view/sim.ts';
import type { Mode, Notice } from './view/result.ts';
import { install, sync } from './diagram/control.ts';

/** The language of the browser. The hash names a language that differs from it. */
const startLang: Lang = browserLang(navigator.language);
const storage = browserStore();
const base = (): Selection => ({ functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });

let selection: Selection = base();
/** The values of the motor simulator. Part of the link: key `m`. */
let sim: SimSettings = { ...DEFAULT_SIM };
let lang: Lang = startLang;
/** Notices of the link the page opened with. Each is built in the language of the render. */
let notices: Array<(l: Lang) => Notice> = [];
/** The view of the wiring. Not part of the link. */
let mode: Mode = 'pic';
/** The scroll position of the schematic box in px. Null until the box has been shown. Not part of the link. */
let panLeft: number | null = null;
/** A removal that waits for an answer. Not part of the link. */
let pending: Pending | null = null;
/** The effect of the last function taken out of the selection. Not part of the link. */
let note: Deselect | null = null;
/** The row tokens of the "have it" ticks of the parts list, each a row to buy of the setup. Part of the link. */
let have: string[] = [];

/** x in drawing px that the pan box centres on first: midway between the panel's pins and pad 1. */
const PAN_START = 351;

const byId = (id: string): HTMLElement | null => document.getElementById(id);

/** The catalogue with the motor simulator named for its values. Kept while the values stay. */
let named: { sim: SimSettings; catalogue: Catalogue } = { sim: { ...DEFAULT_SIM }, catalogue: CATALOGUE };
function catalogue(): Catalogue {
  if (!sameSim(named.sim, sim)) named = { sim: { ...sim }, catalogue: withSim(CATALOGUE, sim) };
  return named.catalogue;
}

/** The state of a render: the language, the catalogue for the values of the motor simulator, the selection. */
function viewOf(setup: Setup): View {
  return { lang, catalogue: catalogue(), setup, options: optionsOf(setup), selection, sim, pending, note, have };
}

function tryResolve(sel: Selection): Setup | null {
  try {
    return resolve(catalogue(), sel);
  } catch {
    return null;
  }
}

/** The ids of the functions every setup runs. They have no tick and are in no selection. */
const BASE_IDS: ReadonlySet<string> = new Set(CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id));

/** The selection reduced to ids the catalogue knows and the page offers a tick for. */
function known(sel: Selection): Selection {
  const functions = sel.functions.filter((id) => CATALOGUE.functions.some((f) => f.id === id && !alwaysOn(CATALOGUE, f)));
  const owned = sel.owned.filter((id) => CATALOGUE.parts.some((p) => p.id === id && p.selectable));
  return { functions, owned, options: { ...sel.options } };
}

/** The ticks of `tokens` that name a row to buy of the setup of `sel`. None when no setup resolves. */
function haveOf(sel: Selection, tokens: readonly string[]): string[] {
  const setup = tryResolve(sel);
  return setup ? haveTicks(setup, optionsOf(setup), tokens) : [];
}

/**
 * The stored selection and values of the motor simulator, or null: none is stored, or the catalogue resolves no
 * setup from it.
 */
function storedSelection(setup: string | null): { selection: Selection; sim: SimSettings } | null {
  if (setup === null) return null;
  const parsed = parseHash(setup, DEFAULT_OPTIONS);
  const kept = known(testObjectTicks(parsed.selection, CATALOGUE));
  return tryResolve(kept) ? { selection: kept, sim: parsed.sim } : null;
}

function readHash(): void {
  const parsed = parseHash(location.hash, DEFAULT_OPTIONS);
  const stored = readStore(storage, DEFAULT_OPTIONS);
  const linked = namesSetup(location.hash);
  // An id the catalogue does not know leaves a stored selection without a notice.
  const kept = linked ? null : storedSelection(stored.setup);
  lang = parsed.lang ?? stored.lang ?? startLang;
  notices = [];
  pending = null;
  note = null;
  if (kept) {
    selection = kept.selection;
    sim = kept.sim;
    have = haveOf(kept.selection, parseHash(stored.setup ?? '', DEFAULT_OPTIONS).have);
  } else {
    sim = parsed.sim;
    if (parsed.badVersion) notices.push((l) => ({ kind: 'err', text: t(l, 'badV') }));
    // A test object under the owned parts of a link is the tick of its function.
    const given = withoutFormerTicks(testObjectTicks(parsed.selection, CATALOGUE));
    const setup = tryResolve(given);
    selection = known(given);
    have = haveOf(selection, parsed.have);
    const dropped = [...given.functions, ...given.owned]
      .filter((id) => !selection.functions.includes(id) && !selection.owned.includes(id) && !BASE_IDS.has(id));
    // A "have it" tick that names no row to buy of the setup is left out like an unknown id.
    const stale = parsed.have.filter((token) => !have.includes(token));
    const unknown = [...parsed.unknown, ...(setup ? setup.ignored : []), ...dropped, ...stale];
    if (unknown.length > 0) notices.push((l) => ({ kind: 'note', text: t(l, 'badId', { x: reportIds(unknown) }) }));
  }
  // What the browser kept stands in the address like a change on the page: the link to copy and the PDF read it there.
  // A link stays as it is until a change.
  const hash = formatHash(selection, lang, startLang, CATALOGUE, have, sim);
  const restored = kept !== null || (parsed.lang === null && stored.lang !== null);
  if (!linked && restored && hash !== `v=${HASH_VERSION}`) history.replaceState(null, '', `#${hash}`);
}

/**
 * The link that Copy link copies, the print header shows and the PDF prints: the address of the page
 * without the id of a function that has no tick or of a former tick, a test object as the tick of its function.
 */
function shareUrl(): string {
  return shareAddress(location.href, DEFAULT_OPTIONS, CATALOGUE, have);
}

function writeHash(): void {
  history.replaceState(null, '', `#${formatHash(selection, lang, startLang, CATALOGUE, have, sim)}`);
}

/** Stores the selection of the page. The stored language stays. */
function storeSetup(): void {
  writeStore(storage, { ...readStore(storage, DEFAULT_OPTIONS), setup: formatHash(selection, lang, lang, CATALOGUE, have, sim) });
}

/** Stores the language of the page. The stored selection stays. */
function storeLang(): void {
  writeStore(storage, { ...readStore(storage, DEFAULT_OPTIONS), lang });
}

function updatePanHint(): void {
  const pan = byId('pan');
  const hint = byId('panhint');
  if (pan && hint) hint.hidden = pan.scrollWidth <= pan.clientWidth + 1;
}

function render(focusId?: string): void {
  document.documentElement.lang = lang;
  document.title = t(lang, 'title');
  const title = byId('title');
  if (title) title.textContent = t(lang, 'title');
  const rel = byId('rel');
  if (rel) rel.textContent = t(lang, 'rel');
  for (const b of document.querySelectorAll<HTMLElement>('.lang button')) {
    b.setAttribute('aria-pressed', String(b.dataset.lang === lang));
  }
  const choose = byId('choose');
  const res = byId('result');
  const bar = byId('bar');
  if (!choose || !res || !bar) return;
  bar.setAttribute('aria-label', t(lang, 'sections'));

  const own = byId('own') as HTMLDetailsElement | null;
  const ownOpen = own ? own.open : false;
  const chooseTop = choose.scrollTop;
  const before = byId('pan');
  // First drawing: the box opens on the wires between the panel and the IOmcu.
  // A hidden box has no scroll position: the last one read from the shown box stays.
  if (before && before.clientWidth > 0) panLeft = before.scrollLeft;

  const setup = tryResolve(selection);
  let sub = '';
  if (setup) {
    const asked = pending;
    if (asked && !setup.parts.some((r) => r.part.id === asked.part)) pending = null;
    const view = viewOf(setup);
    choose.innerHTML = chooser(view, ownOpen);
    res.innerHTML = result(view, shareUrl(), notices.map((n) => n(lang)), mode);
    sub = `<small>${t(lang, 'bSub', { p: benchParts(setup).length, w: setup.wires.length })}</small>`;
  } else {
    choose.innerHTML = `<h2 id="h-choose">${t(lang, 'choose')}</h2>`;
    res.innerHTML = resultError(lang, catalogue(), shareUrl());
  }
  bar.innerHTML = `<a href="#choose" data-jump="choose">${t(lang, 'bChoose')}</a>`
    + `<a href="#result" data-jump="result"><span>${t(lang, 'bWiring')}${sub}</span></a>`;

  choose.scrollTop = chooseTop;
  const pan = byId('pan');
  if (pan && pan.clientWidth > 0) pan.scrollLeft = panLeft ?? Math.max(0, PAN_START - pan.clientWidth / 2);
  updatePanHint();
  sync();
  syncPdf(lang, shareUrl());
  if (focusId) byId(focusId)?.focus({ preventScroll: true });
  // The question opens under its control; it is brought into view with its 2 answers.
  if (focusId === 'rm-no') byId('rmwarn')?.scrollIntoView({ block: 'nearest' });
}

function announce(text: string): void {
  const live = byId('live');
  if (live) live.textContent = text;
}

/** Announces `said`, then the counts of the setup. */
function announceSummary(said: (view: View) => string = () => ''): void {
  const setup = tryResolve(selection);
  if (!setup) return;
  const view = viewOf(setup);
  announce(`${said(view)} ${summary(view)}`.trim());
}

/** The control a closed question returns the focus to. */
const origin = (asked: Pending): string => (asked.at === 'own' ? `p-${asked.part}` : `rm-${asked.part}`);

/** Closes the question without a change: the part stays. */
function keepPart(): void {
  const asked = pending;
  if (!asked) return;
  pending = null;
  render(origin(asked));
}

/** Applies a removal. The focus goes to `focusId`. */
function applyRemoval(effect: Removal, focusId: string): void {
  const part = CATALOGUE.parts.find((p) => p.id === effect.part);
  selection = effect.selection;
  have = haveOf(selection, have);
  pending = null;
  note = null;
  notices = [];
  writeHash();
  storeSetup();
  render(focusId);
  announceSummary((view) => t(view.lang, 'rmDone', { p: part ? part.short[view.lang] : effect.part }));
}

/**
 * Asks before a part leaves, in every case: the question opens under the
 * control that asked. `at` names the list of that control.
 */
function requestRemoval(part: string, at: Pending['at']): void {
  const effect = removalEffect(CATALOGUE, selection, part);
  const back = at === 'own' ? `p-${part}` : 'h-parts';
  if (effect.refused === null || effect.refused === 'held') {
    pending = { part, at };
    note = null;
    render('rm-no');
  } else render(at === 'own' ? back : undefined);
}

function confirmRemoval(): void {
  const asked = pending;
  if (!asked) return;
  const effect = removalEffect(CATALOGUE, selection, asked.part);
  if (effect.refused !== null) keepPart();
  else applyRemoval(effect, asked.at === 'own' ? `p-${asked.part}` : 'h-parts');
}

function showLinkField(): void {
  const box = byId('linkbox');
  const field = byId('linkfield') as HTMLInputElement | null;
  if (!box || !field) return;
  box.hidden = false;
  field.value = shareUrl();
  field.focus();
  field.select();
}

function copyLink(button: HTMLElement): void {
  const done = (): void => {
    announce(t(lang, 'copied'));
    button.textContent = t(lang, 'copied');
  };
  let pending: Promise<void>;
  try {
    pending = navigator.clipboard ? navigator.clipboard.writeText(shareUrl()) : Promise.reject(new Error('no clipboard'));
  } catch (error) {
    pending = Promise.reject(error);
  }
  pending.then(done, showLinkField);
}

/** Shows the parts list in a selectable field: the clipboard refused it. */
function showListField(text: string): void {
  const box = byId('buybox');
  const field = byId('buyfield') as HTMLTextAreaElement | null;
  if (!box || !field) return;
  box.hidden = false;
  field.value = text;
  field.focus();
  field.select();
}

/** Copies the parts list as text or as CSV. A refused clipboard shows the field, as Copy link does. */
function copyList(button: HTMLElement, form: 'text' | 'csv'): void {
  const setup = tryResolve(selection);
  if (!setup) return;
  const view = viewOf(setup);
  const text = form === 'text' ? listText(view, shareUrl()) : listCsv(view);
  const said = t(lang, form === 'text' ? 'copiedText' : 'copiedCsv');
  const done = (): void => {
    announce(said);
    button.textContent = said;
  };
  let pending: Promise<void>;
  try {
    pending = navigator.clipboard ? navigator.clipboard.writeText(text) : Promise.reject(new Error('no clipboard'));
  } catch (error) {
    pending = Promise.reject(error);
  }
  pending.then(done, () => showListField(text));
}

const toggle = (list: string[], id: string, on: boolean): string[] =>
  on ? [...list.filter((x) => x !== id), id] : list.filter((x) => x !== id);

/** The tick rows whose help bubble shows now: by focus, pointer or tap. */
function shownHelp(): HTMLElement[] {
  return [...document.querySelectorAll<HTMLElement>('#choose .row')].filter((row) => {
    const box = row.querySelector('.bubble');
    return box !== null && getComputedStyle(box).display !== 'none';
  });
}

/**
 * The band of the viewport a bubble of `row` shows in: above the bar of the narrow layout, and inside each
 * scrolling ancestor, #choose at 1024px and wider, which clips what lies outside its box.
 */
function helpRoom(row: HTMLElement): { top: number; bottom: number } {
  let top = 0;
  let bottom = window.innerHeight;
  const bar = byId('bar');
  if (bar && getComputedStyle(bar).display !== 'none') bottom = Math.min(bottom, bar.getBoundingClientRect().top);
  for (let el = row.parentElement; el && el !== document.body; el = el.parentElement) {
    if (getComputedStyle(el).overflowY === 'visible') continue;
    const box = el.getBoundingClientRect();
    top = Math.max(top, box.top + el.clientTop);
    bottom = Math.min(bottom, box.top + el.clientTop + el.clientHeight);
  }
  return { top, bottom };
}

/**
 * Opens the bubble of `row` over the row (data-up) when the room under the row is less than the bubble's height
 * and the room over it holds the whole bubble, else under it. A bubble that fits neither way opens under the row:
 * #choose and the page scroll down to what reaches past their lower edge, never up to what reaches past the top.
 * A hidden bubble is measured without being shown.
 */
function placeHelp(row: HTMLElement): void {
  const box = row.querySelector<HTMLElement>(':scope > .bubble');
  if (!box) return;
  const hidden = getComputedStyle(box).display === 'none';
  if (hidden) {
    box.style.setProperty('display', 'block', 'important');
    box.style.setProperty('visibility', 'hidden');
  }
  const height = box.offsetHeight;
  if (hidden) {
    box.style.removeProperty('display');
    box.style.removeProperty('visibility');
  }
  const r = row.getBoundingClientRect();
  const room = helpRoom(row);
  const below = room.bottom - r.bottom;
  const above = r.top - room.top;
  row.toggleAttribute('data-up', height > below && above >= height);
}

// The rows move under a shown bubble when the page or the chooser scrolls, the focus of a tick scrolls it into
// view, or the window changes size: each shown bubble is placed again, once per frame.
let placing = false;
function placeShownHelp(): void {
  if (placing) return;
  placing = true;
  requestAnimationFrame(() => {
    placing = false;
    for (const row of shownHelp()) placeHelp(row);
  });
}
document.addEventListener('scroll', placeShownHelp, { capture: true, passive: true });
window.addEventListener('resize', placeShownHelp, { passive: true });

/**
 * Makes `row` the latest trigger of a bubble: the focus of its tick or the pointer entering its mark. Every other
 * row keeps its bubble hidden (data-quiet) until it is the latest trigger again, so the focus of one tick and the
 * pointer on the mark of another never show two bubbles.
 */
function triggerHelp(row: HTMLElement): void {
  for (const other of document.querySelectorAll<HTMLElement>('#choose .row')) {
    other.toggleAttribute('data-quiet', other !== row);
  }
}

/** Closes every bubble a tap or a click opened, but that of `keep`. */
function closeOpenHelp(keep: Element | null): void {
  for (const open of document.querySelectorAll<HTMLElement>('#choose .row[data-open]')) {
    if (open !== keep) open.removeAttribute('data-open');
  }
}

// A tap or a click on the mark of a tick opens its bubble and closes the others; a second one closes it, and the
// pointer resting on the mark does not hold it open (data-nohover) until it enters the mark again. The mark
// stands outside the label: the tick stays as it is.
document.addEventListener('click', (event) => {
  const target = event.target instanceof Element ? event.target : null;
  const mark = target?.closest<HTMLElement>('#choose .row > .info');
  const row = mark?.parentElement ?? null;
  closeOpenHelp(row);
  if (!row) return;
  if (row.hasAttribute('data-open')) {
    row.removeAttribute('data-open');
    row.setAttribute('data-nohover', '');
  } else {
    placeHelp(row);
    row.setAttribute('data-open', '');
    row.removeAttribute('data-shut');
    row.removeAttribute('data-nohover');
  }
});

// A press anywhere but on the mark of an open bubble, the bubble included, closes every bubble a tap opened: an
// open bubble covers the rows next to it. iOS Safari sends no click to the document for a tap on text or on an
// empty area, and a pointerdown for every tap.
document.addEventListener('pointerdown', (event) => {
  const target = event.target instanceof Element ? event.target : null;
  closeOpenHelp(target?.closest<HTMLElement>('#choose .row > .info')?.parentElement ?? null);
});

// A bubble closed with Escape opens again when the focus comes back to its tick from elsewhere, or the pointer
// enters its mark again. The focus and the pointer show one bubble at a time: the focus of a tick and the pointer
// entering a mark close the bubbles a tap or a click opened on other rows and quiet the bubbles of the other rows.
// The bubble of the focused tick or of the mark under the pointer is placed before it shows.
document.addEventListener('focusin', (event) => {
  const target = event.target instanceof Node ? event.target : null;
  for (const row of document.querySelectorAll<HTMLElement>('#choose .row[data-shut]')) {
    if (!target || !row.contains(target)) row.removeAttribute('data-shut');
  }
  const row = target instanceof Element ? target.closest<HTMLElement>('#choose .row') : null;
  closeOpenHelp(row);
  if (row) {
    triggerHelp(row);
    placeHelp(row);
    placeShownHelp();
    if (target instanceof Element && target.matches(':focus-visible')) showFocusedRow(row);
  }
});

/**
 * Brings the row of a tick the keyboard focused into view with its bubble, at once. The page scrolls smoothly: a
 * smooth scroll an earlier Tab started goes on after this focus, and the browser starts none for a tick that is in
 * view at the moment of the focus, so that scroll would carry the tick out of view. Stopping it where it is comes
 * first; the bubble is brought in before the row, so the row wins where both do not fit.
 */
function showFocusedRow(row: HTMLElement): void {
  window.scrollTo({ left: window.scrollX, top: window.scrollY, behavior: 'instant' });
  row.querySelector<HTMLElement>(':scope > .bubble')?.scrollIntoView({ block: 'nearest', behavior: 'instant' });
  row.scrollIntoView({ block: 'nearest', behavior: 'instant' });
}
document.addEventListener('pointerover', (event) => {
  const target = event.target instanceof Element ? event.target : null;
  const row = target?.closest<HTMLElement>('#choose .row > .info')?.parentElement;
  if (!row) return;
  closeOpenHelp(row);
  triggerHelp(row);
  row.removeAttribute('data-shut');
  row.removeAttribute('data-nohover');
  placeHelp(row);
});

document.addEventListener('click', (event) => {
  const target = event.target instanceof Element ? event.target : null;
  const el = target?.closest<HTMLElement>('button, a[data-jump], input[aria-disabled="true"]');
  if (!el) return;
  if (el.matches('input[aria-disabled="true"]')) {
    event.preventDefault();
    return;
  }
  if (el.dataset.lang === 'en' || el.dataset.lang === 'de') {
    lang = el.dataset.lang;
    writeHash();
    storeLang();
    render();
    el.focus();
  } else if (el.dataset.jump) {
    event.preventDefault();
    byId(el.dataset.jump)?.scrollIntoView();
  } else if (el.id === 'v-pic' || el.id === 'v-sch') {
    mode = el.id === 'v-pic' ? 'pic' : 'sch';
    render(el.id);
  } else if (el.id === 'b-print') {
    window.print();
  } else if (el.dataset.rm) {
    if (pending?.at === 'result' && pending.part === el.dataset.rm) keepPart();
    else requestRemoval(el.dataset.rm, 'result');
  } else if (el.id === 'rm-yes') {
    confirmRemoval();
  } else if (el.id === 'rm-no') {
    keepPart();
  } else if (el.id === 'b-pdf') {
    const setup = tryResolve(selection);
    if (setup) void exportPdf(viewOf(setup), shareUrl());
  } else if (el.id === 'b-reset') {
    selection = base();
    have = [];
    sim = { ...DEFAULT_SIM };
    notices = [];
    pending = null;
    note = null;
    writeHash();
    storeSetup();
    render('b-reset');
    announceSummary();
  } else if (el.id === 'b-copy') {
    copyLink(el);
  } else if (el.id === 'b-copy-text' || el.id === 'b-copy-csv') {
    copyList(el, el.id === 'b-copy-text' ? 'text' : 'csv');
  }
});

/** A value of the motor simulator: 1 of the offered ones, or the value stays. */
function changeSim(select: HTMLSelectElement): void {
  const key = select.dataset.sim as keyof SimSettings;
  const value = key === 'wiring' ? select.value : Number(select.value);
  const next = { ...sim, [key]: value } as SimSettings;
  if (!(key in sim) || !validSim(next)) return;
  have = haveAfterSim(have, sim, next);
  sim = next;
  notices = [];
  pending = null;
  note = null;
  writeHash();
  storeSetup();
  render(select.id);
  const setup = tryResolve(selection);
  if (setup) announce(simSaid(viewOf(setup)));
}

document.addEventListener('change', (event) => {
  if (event.target instanceof HTMLSelectElement && event.target.dataset.sim) {
    changeSim(event.target);
    return;
  }
  const input = event.target instanceof HTMLInputElement ? event.target : null;
  if (!input) return;
  if (input.getAttribute('aria-disabled') === 'true') {
    render(input.id);
    return;
  }
  const { f, p, o, h } = input.dataset;
  if (p && !input.checked) {
    // The tick comes off a part of the setup: the removal decides what follows.
    requestRemoval(p, 'own');
    return;
  }
  let left: Deselect | null = null;
  if (f && input.checked) selection = { ...selection, functions: toggle(selection.functions, f, true) };
  else if (f) {
    const effect = deselectEffect(CATALOGUE, selection, f);
    selection = effect.selection;
    if (effect.changed) left = effect;
  } else if (p) selection = { ...selection, owned: toggle(selection.owned, p, true) };
  else if (o) selection = { ...selection, options: { ...selection.options, [o as OptionKey]: input.value } as Options };
  else if (h) have = toggle(have, h, input.checked);
  else return;
  // A tick that no longer names a row to buy leaves: its part left the setup, has a tick of its own or another row.
  have = haveOf(selection, have);
  notices = [];
  pending = null;
  note = left;
  writeHash();
  storeSetup();
  render(input.id);
  announceSummary((view) => (left ? deselectText(view, left) : h ? t(view.lang, input.checked ? 'haveOn' : 'haveOff') : ''));
});

// Escape closes a bubble that shows first, then the question before a removal.
document.addEventListener('keydown', (event) => {
  if (event.key !== 'Escape') return;
  const shown = shownHelp();
  if (shown.length > 0) {
    event.preventDefault();
    for (const row of shown) {
      row.setAttribute('data-shut', '');
      row.removeAttribute('data-open');
    }
    return;
  }
  if (!pending) return;
  event.preventDefault();
  keepPart();
});

window.addEventListener('hashchange', () => {
  readHash();
  render();
});
window.addEventListener('resize', updatePanHint);

// The space the page keeps free above its lower edge follows the rendered height of the bottom bar.
const bottomBar = byId('bar');
if (bottomBar && typeof ResizeObserver === 'function') {
  new ResizeObserver(() => {
    const height = Math.ceil(bottomBar.getBoundingClientRect().height);
    if (height > 0) document.documentElement.style.setProperty('--bar-clear', `${height}px`);
    else document.documentElement.style.removeProperty('--bar-clear');
  }).observe(bottomBar);
}

install();
readHash();
render();
