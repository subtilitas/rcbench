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
import type { Lang, OptionKey, Options, Selection, Setup } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, alwaysOn, deselectEffect, removalEffect, resolve } from './core.ts';
import type { Deselect, Removal } from './core.ts';
import { exportPdf, syncPdf } from './export/button.ts';
import { HASH_VERSION, formatHash, namesSetup, parseHash, withoutFormerTicks, reportIds, shareHash, testObjectTicks } from './hash.ts';
import { browserLang, t } from './i18n.ts';
import { benchParts, optionsOf } from './names.ts';
import { browserStore, readStore, writeStore } from './store.ts';
import { chooser, deselectText } from './view/chooser.ts';
import type { Pending, View } from './view/chooser.ts';
import { result, resultError, summary } from './view/result.ts';
import type { Mode, Notice } from './view/result.ts';
import { install, sync } from './diagram/control.ts';

/** The language of the browser. The hash names a language that differs from it. */
const startLang: Lang = browserLang(navigator.language);
const storage = browserStore();
const base = (): Selection => ({ functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });

let selection: Selection = base();
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

/** x in drawing px that the pan box centres on first: midway between the panel's pins and pad 1. */
const PAN_START = 351;

const byId = (id: string): HTMLElement | null => document.getElementById(id);

function tryResolve(sel: Selection): Setup | null {
  try {
    return resolve(CATALOGUE, sel);
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

/** The stored selection, or null: none is stored, or the catalogue resolves no setup from it. */
function storedSelection(setup: string | null): Selection | null {
  if (setup === null) return null;
  const kept = known(testObjectTicks(parseHash(setup, DEFAULT_OPTIONS).selection, CATALOGUE));
  return tryResolve(kept) ? kept : null;
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
    selection = kept;
  } else {
    if (parsed.badVersion) notices.push((l) => ({ kind: 'err', text: t(l, 'badV') }));
    // A test object under the owned parts of a link is the tick of its function.
    const given = withoutFormerTicks(testObjectTicks(parsed.selection, CATALOGUE));
    const setup = tryResolve(given);
    selection = known(given);
    const dropped = [...given.functions, ...given.owned]
      .filter((id) => !selection.functions.includes(id) && !selection.owned.includes(id) && !BASE_IDS.has(id));
    const unknown = [...parsed.unknown, ...(setup ? setup.ignored : []), ...dropped];
    if (unknown.length > 0) notices.push((l) => ({ kind: 'note', text: t(l, 'badId', { x: reportIds(unknown) }) }));
  }
  // What the browser kept stands in the address like a change on the page: the link to copy and the PDF read it there.
  // A link stays as it is until a change.
  const hash = formatHash(selection, lang, startLang, CATALOGUE);
  const restored = kept !== null || (parsed.lang === null && stored.lang !== null);
  if (!linked && restored && hash !== `v=${HASH_VERSION}`) history.replaceState(null, '', `#${hash}`);
}

/** The link the PDF prints: the address of the page without the id of a function that has no tick. */
function shareUrl(): string {
  const at = location.href.indexOf('#');
  return at < 0 ? location.href : location.href.slice(0, at) + shareHash(location.href.slice(at), DEFAULT_OPTIONS, CATALOGUE);
}

function writeHash(): void {
  history.replaceState(null, '', `#${formatHash(selection, lang, startLang, CATALOGUE)}`);
}

/** Stores the selection of the page. The stored language stays. */
function storeSetup(): void {
  writeStore(storage, { ...readStore(storage, DEFAULT_OPTIONS), setup: formatHash(selection, lang, lang, CATALOGUE) });
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
  // First drawing: the box opens on the wires between the panel and the coprocessor.
  // A hidden box has no scroll position: the last one read from the shown box stays.
  if (before && before.clientWidth > 0) panLeft = before.scrollLeft;

  const setup = tryResolve(selection);
  let sub = '';
  if (setup) {
    const asked = pending;
    if (asked && !setup.parts.some((r) => r.part.id === asked.part)) pending = null;
    const view: View = { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection, pending, note };
    choose.innerHTML = chooser(view, ownOpen);
    res.innerHTML = result(view, location.href, notices.map((n) => n(lang)), mode);
    sub = `<small>${t(lang, 'bSub', { p: benchParts(setup).length, w: setup.wires.length })}</small>`;
  } else {
    choose.innerHTML = `<h2 id="h-choose">${t(lang, 'choose')}</h2>`;
    res.innerHTML = resultError(lang, CATALOGUE, location.href);
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
  const view: View = { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
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
  field.value = location.href;
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
    pending = navigator.clipboard ? navigator.clipboard.writeText(location.href) : Promise.reject(new Error('no clipboard'));
  } catch (error) {
    pending = Promise.reject(error);
  }
  pending.then(done, showLinkField);
}

const toggle = (list: string[], id: string, on: boolean): string[] =>
  on ? [...list.filter((x) => x !== id), id] : list.filter((x) => x !== id);

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
    if (setup) void exportPdf({ lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection }, shareUrl());
  } else if (el.id === 'b-reset') {
    selection = base();
    notices = [];
    pending = null;
    note = null;
    writeHash();
    storeSetup();
    render('b-reset');
    announceSummary();
  } else if (el.id === 'b-copy') {
    copyLink(el);
  }
});

document.addEventListener('change', (event) => {
  const input = event.target instanceof HTMLInputElement ? event.target : null;
  if (!input) return;
  if (input.getAttribute('aria-disabled') === 'true') {
    render(input.id);
    return;
  }
  const { f, p, o } = input.dataset;
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
  else return;
  notices = [];
  pending = null;
  note = left;
  writeHash();
  storeSetup();
  render(input.id);
  announceSummary((view) => (left ? deselectText(view, left) : ''));
});

document.addEventListener('keydown', (event) => {
  if (event.key !== 'Escape' || !pending) return;
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
