// Page state, events and rendering. The selection lives in the URL hash.

import './style.css';
import type { Lang, OptionKey, Options, Selection, Setup } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from './core.ts';
import { formatHash, parseHash, reportIds } from './hash.ts';
import { browserLang, t } from './i18n.ts';
import { optionsOf } from './names.ts';
import { chooser } from './view/chooser.ts';
import type { View } from './view/chooser.ts';
import { result, resultError, summary } from './view/result.ts';
import type { Mode, Notice } from './view/result.ts';
import { install, sync } from './diagram/control.ts';

const startLang: Lang = browserLang(navigator.language);
const base = (): Selection => ({ functions: [], owned: [], options: { ...DEFAULT_OPTIONS } });

let selection: Selection = base();
let lang: Lang = startLang;
/** Notices of the link the page opened with. Each is built in the language of the render. */
let notices: Array<(l: Lang) => Notice> = [];
/** The view of the wiring. Not part of the link. */
let mode: Mode = 'pic';

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

/** The selection reduced to ids the catalogue knows. */
function known(sel: Selection): Selection {
  const functions = sel.functions.filter((id) => CATALOGUE.functions.some((f) => f.id === id));
  const owned = sel.owned.filter((id) => CATALOGUE.parts.some((p) => p.id === id && p.selectable));
  return { functions, owned, options: { ...sel.options } };
}

function readHash(): void {
  const parsed = parseHash(location.hash, DEFAULT_OPTIONS);
  lang = parsed.lang ?? startLang;
  notices = [];
  if (parsed.badVersion) notices.push((l) => ({ kind: 'err', text: t(l, 'badV') }));
  const setup = tryResolve(parsed.selection);
  selection = known(parsed.selection);
  const dropped = [...parsed.selection.functions, ...parsed.selection.owned]
    .filter((id) => !selection.functions.includes(id) && !selection.owned.includes(id));
  const unknown = [...parsed.unknown, ...(setup ? setup.ignored : []), ...dropped];
  if (unknown.length > 0) notices.push((l) => ({ kind: 'note', text: t(l, 'badId', { x: reportIds(unknown) }) }));
}

function writeHash(): void {
  history.replaceState(null, '', `#${formatHash(selection, lang, startLang, CATALOGUE)}`);
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
  // A hidden box has no scroll position.
  const panLeft = before && before.clientWidth > 0 ? before.scrollLeft : null;

  const setup = tryResolve(selection);
  let sub = '';
  if (setup) {
    const view: View = { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
    choose.innerHTML = chooser(view, ownOpen);
    res.innerHTML = result(view, location.href, notices.map((n) => n(lang)), mode);
    sub = `<small>${t(lang, 'bSub', { p: setup.parts.length, w: setup.wires.length })}</small>`;
  } else {
    choose.innerHTML = `<h2 id="h-choose">${t(lang, 'choose')}</h2>`;
    res.innerHTML = resultError(lang, CATALOGUE, location.href);
  }
  bar.innerHTML = `<a href="#choose" data-jump="choose">${t(lang, 'bChoose')}</a>`
    + `<a href="#result" data-jump="result"><span>${t(lang, 'bWiring')}${sub}</span></a>`;

  choose.scrollTop = chooseTop;
  const pan = byId('pan');
  if (pan) pan.scrollLeft = panLeft ?? Math.max(0, PAN_START - pan.clientWidth / 2);
  updatePanHint();
  sync();
  if (focusId) byId(focusId)?.focus({ preventScroll: true });
}

function announce(text: string): void {
  const live = byId('live');
  if (live) live.textContent = text;
}

function announceSummary(): void {
  const setup = tryResolve(selection);
  if (setup) announce(summary({ lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection }));
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
  } else if (el.id === 'b-reset') {
    selection = base();
    notices = [];
    writeHash();
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
  if (f) selection = { ...selection, functions: toggle(selection.functions, f, input.checked) };
  else if (p) selection = { ...selection, owned: toggle(selection.owned, p, input.checked) };
  else if (o) selection = { ...selection, options: { ...selection.options, [o as OptionKey]: input.value } as Options };
  else return;
  notices = [];
  writeHash();
  render(input.id);
  announceSummary();
});

window.addEventListener('hashchange', () => {
  readHash();
  render();
});
window.addEventListener('resize', updatePanHint);

install();
readHash();
render();
