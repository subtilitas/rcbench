// The chooser: functions grouped by the parts they need, variants, owned
// parts, the warning before a part leaves, and the static list of what is in
// no release. Returns markup; main.ts owns the document.

import type {
  Catalogue, FunctionId, Lang, OptionKey, Options, Part, PartId, ResolvedFunction, ResolvedPart, Selection, Setup, Text,
} from '../../contract.ts';
import { baseParts, groups, removalEffect } from '../core.ts';
import type { Deselect, Group } from '../core.ts';
import { esc, t, tx } from '../i18n.ts';
import type { Key } from '../i18n.ts';

/** The part whose removal waits for an answer, and the list that shows the question. */
export type Pending = { part: PartId; at: 'result' | 'own' };

export type View = {
  lang: Lang;
  catalogue: Catalogue;
  setup: Setup;
  /** Option values of the setup, after forced options. */
  options: Options;
  /** The visitor's own selection: the ticks the hash stores. */
  selection: Selection;
  /** A removal that waits for "remove" or "keep". */
  pending?: Pending | null;
  /** The effect of the last function the visitor took out of the selection. */
  note?: Deselect | null;
};

type Variant = { key: OptionKey; label: Key; values: ReadonlyArray<readonly [string, Key]> };

export const VARIANTS: readonly Variant[] = [
  { key: 'connection', label: 'vConn', values: [['pads', 'vPads'], ['osoyoo', 'vOso']] },
  { key: 'ina228Board', label: 'vIna', values: [['matek', 'vMatek'], ['xt90', 'vXt']] },
  { key: 'escPower', label: 'vEscp', values: [['battery', 'vBatt'], ['pd', 'vPd']] },
];

const tag = (lang: Lang, key: Key = 'nrh'): string => ` <span class="tag">${esc(t(lang, key))}</span>`;

/** The Osoyoo breakout board is in no build guide and has not run on hardware. */
export function variantNotRun(key: OptionKey, value: string): boolean {
  return key === 'connection' && value === 'osoyoo';
}

/** A variant value in few words: "A: the ESC's own battery" gives "ESC power A". */
export function variantShort(lang: Lang, key: OptionKey, value: string): string {
  const variant = VARIANTS.find((v) => v.key === key);
  const pair = variant?.values.find((v) => v[0] === value);
  if (!variant || !pair) return value;
  const text = t(lang, pair[1]);
  const colon = text.indexOf(':');
  return `${t(lang, variant.label)} ${colon > 0 && colon <= 2 ? text.slice(0, colon) : text}`;
}

function shortName(view: View, id: PartId): string {
  const part = view.catalogue.parts.find((p) => p.id === id);
  return part ? tx(view.lang, part.short) : id;
}

function functionName(view: View, id: FunctionId): string {
  const fn = view.catalogue.functions.find((f) => f.id === id);
  return fn ? tx(view.lang, fn.name) : id;
}

const partList = (view: View, ids: PartId[]): string => ids.map((id) => shortName(view, id)).join(', ');

/** Why a part is in the setup, as one line: "Needed by: Servo. You own it." */
export function reasonText(part: ResolvedPart, view: View): string {
  const { lang, catalogue } = view;
  const by: string[] = [];
  let always = false;
  let own = false;
  for (const r of part.addedBy) {
    if (r.kind === 'always') always = true;
    else if (r.kind === 'owned') own = true;
    else if (r.kind === 'function') {
      const fn = catalogue.functions.find((f) => f.id === r.id);
      by.push(fn ? tx(lang, fn.name) : r.id);
    } else if (r.kind === 'rule') by.push(tx(lang, r.because));
    else by.push(variantShort(lang, r.key, r.value));
  }
  const out: string[] = [];
  if (always) out.push(t(lang, 'always'));
  if (by.length > 0) out.push(t(lang, 'neededBy') + [...new Set(by)].join(', '));
  if (own) out.push(t(lang, 'youOwn'));
  return out.length > 0 ? `${out.join('. ')}.` : '';
}

const GROUPS = new WeakMap<Catalogue, Group[]>();

/** The groups of a catalogue, derived once per catalogue. */
function groupsOf(catalogue: Catalogue): Group[] {
  let list = GROUPS.get(catalogue);
  if (!list) {
    list = groups(catalogue);
    GROUPS.set(catalogue, list);
  }
  return list;
}

/** The id of a group's fieldset: "g-base", "g-servo", "g-servo-pdmini". */
export function groupId(group: Group): string {
  return group.parts.length === 0 ? 'g-base' : `g-${group.parts.join('-')}`;
}

/**
 * The heading of a group and the lines under it: the parts the functions
 * need, a part that is one of several boards, and the parts the rules add.
 */
function groupHead(view: View, group: Group): string {
  const { lang, catalogue, setup } = view;
  const plus = (ids: PartId[]): string => ids.map((id) => shortName(view, id)).join(' + ');
  const title = group.parts.length === 0 ? t(lang, 'gBase') : t(lang, 'gNeeds') + plus(group.parts);
  const present = group.parts.length > 0 && group.parts.every((id) => setup.parts.some((r) => r.part.id === id));
  const lines: string[] = [];
  for (const v of group.variants) {
    const names = catalogue.parts.find((p) => p.id === v.part)?.variantName?.names as Record<string, Text> | undefined;
    const boards = v.values.map((value) => (names?.[value] ? tx(lang, names[value]) : value));
    lines.push(t(lang, 'gVariant', { p: shortName(view, v.part), x: boards.join(t(lang, 'or')) }));
  }
  if (group.also.length > 0) lines.push(t(lang, 'gAlso', { x: partList(view, group.also) }));
  if (group.oneOf.length > 0) {
    const alternatives = group.oneOf.map((ids) => (ids.length > 0 ? plus(ids) : t(lang, 'noPart')));
    lines.push(t(lang, 'gOneOf', { x: alternatives.join(t(lang, 'or')) }));
  }
  return `<legend>${esc(title)}${present ? tag(lang, 'gIn') : ''}</legend>`
    + lines.map((line) => `<p class="hint">${esc(line)}</p>`).join('');
}

/** "own": in the visitor's selection. "inc": active by the parts of the setup. "off": not active. */
export function functionState(f: ResolvedFunction): 'own' | 'inc' | 'off' {
  return f.chosen ? 'own' : f.enabled ? 'inc' : 'off';
}

/** True for a function that needs no part beyond the base setup: it takes no tick. */
export function runsOnBase(catalogue: Catalogue, f: ResolvedFunction): boolean {
  const base = baseParts(catalogue);
  return f.fn.needs.every((id) => base.includes(id));
}

/**
 * The state of a function in words. A function the base setup runs is never
 * "selected by you", whatever the selection names.
 */
export function statusText(view: View, f: ResolvedFunction): string {
  const { lang, catalogue, options } = view;
  if (f.enabled && runsOnBase(catalogue, f)) return t(lang, 'stBase');
  if (f.chosen) return t(lang, 'stOwn');
  if (f.enabled) return t(lang, 'stInc');
  if (f.missing.length > 0) return t(lang, 'adds') + partList(view, f.missing);
  const forced = (Object.entries(f.fn.forces ?? {}) as Array<[OptionKey, string]>)
    .filter(([key, value]) => options[key] !== value)
    .map(([key, value]) => variantShort(lang, key, value));
  return forced.length > 0 ? t(lang, 'stVar', { v: forced.join(', ') }) : '';
}

/** What taking a function out of the selection did, in sentences. */
export function deselectText(view: View, effect: Deselect): string {
  const { lang } = view;
  const out = [t(lang, effect.stillOn ? 'dsOn' : 'dsOff', { f: functionName(view, effect.fn) })];
  if (effect.stillOn && effect.partsStay.length === 0) out.push(t(lang, 'dsBase'));
  if (effect.partsStay.length > 0) out.push(t(lang, 'dsStay', { x: partList(view, effect.partsStay) }));
  if (effect.partsOut.length > 0) out.push(t(lang, 'outParts', { x: partList(view, effect.partsOut) }));
  if (effect.partsIn.length > 0) out.push(t(lang, 'inParts', { x: partList(view, effect.partsIn) }));
  if (effect.functionsOff.length > 0) {
    out.push(t(lang, 'offFns', { x: effect.functionsOff.map((id) => functionName(view, id)).join(', ') }));
  }
  return out.join(' ');
}

/**
 * The functions the base setup runs: a list without controls. Every setup
 * runs them, so a row has no tick and no state to change; it states its
 * state in words like the rows of the other groups.
 */
function baseRows(view: View, group: Group): string {
  const { lang, setup } = view;
  const items = setup.functions.filter((f) => group.functions.includes(f.fn.id)).map((f) => {
    const status = statusText(view, f);
    return `<li id="b-${esc(f.fn.id)}"><span><b>${esc(tx(lang, f.fn.name))}</b>${f.fn.hardware === 'not-run' ? tag(lang) : ''}</span>`
      + `${status ? `<small>${esc(status)}</small>` : ''}</li>`;
  });
  return `<p class="hint">${esc(t(lang, 'baseHint'))}</p><ul class="base">${items.join('')}</ul>`;
}

function functionRows(view: View, group: Group): string {
  if (group.parts.length === 0) return baseRows(view, group);
  const { lang, setup, note } = view;
  let h = '';
  for (const f of setup.functions) {
    if (!group.functions.includes(f.fn.id)) continue;
    const status = statusText(view, f);
    h += `<label class="opt" data-state="${functionState(f)}"><input type="checkbox" id="f-${esc(f.fn.id)}" data-f="${esc(f.fn.id)}"${f.chosen ? ' checked' : ''}>`
      + `<span><b>${esc(tx(lang, f.fn.name))}</b>${f.fn.hardware === 'not-run' ? tag(lang) : ''}</span>`
      + `${status ? `<small>${esc(status)}</small>` : ''}</label>`;
    if (note && note.changed && note.fn === f.fn.id && !f.chosen) {
      h += `<p class="msg" id="note" role="status">${esc(deselectText(view, note))}</p>`;
    }
  }
  return h;
}

function variantGroups(view: View): string {
  const { lang, catalogue, setup } = view;
  let h = '';
  for (const v of VARIANTS) {
    const o = setup.options.find((x) => x.key === v.key);
    if (!o || !o.visible) continue;
    const forcing = o.forcedBy === null ? null : catalogue.functions.find((f) => f.id === o.forcedBy);
    const forcedName = forcing ? tx(lang, forcing.name) : o.forcedBy;
    h += `<div class="radios" role="radiogroup" aria-labelledby="v-${v.key}"><p class="hint" id="v-${v.key}">${esc(t(lang, v.label))}</p>`;
    for (const [value, label] of v.values) {
      const checked = o.value === value;
      const locked = forcedName !== null && !checked;
      let sub = '';
      if (forcedName !== null) sub = checked ? `${t(lang, 'neededBy')}${forcedName}.` : t(lang, 'optLocked', { f: forcedName });
      h += `<label class="opt"${locked ? ' data-locked' : ''}><input type="radio" name="${v.key}" id="o-${v.key}-${value}" data-o="${v.key}" value="${value}"`
        + `${checked ? ' checked' : ''}${locked ? ' aria-disabled="true"' : ''}>`
        + `<span>${esc(t(lang, label))}${variantNotRun(v.key, value) ? tag(lang) : ''}</span>`
        + `${sub ? `<small>${esc(sub)}</small>` : ''}</label>`;
    }
    h += '</div>';
  }
  return h;
}

/**
 * The question before a part leaves: the functions that go off, the parts
 * that leave or come in with it, and the 2 answers. An inline region, not a
 * modal one: the rest of the page stays reachable. Empty without a pending
 * removal and for a part that is not in the setup.
 */
export function removalWarning(view: View): string {
  const { lang, catalogue, selection, pending } = view;
  if (!pending) return '';
  const effect = removalEffect(catalogue, selection, pending.part);
  if (effect.refused !== null && effect.refused !== 'held') return '';
  const lines: string[] = [];
  let yes = '';
  if (effect.refused === 'held') lines.push(`<p>${esc(t(lang, 'lockRule'))}</p>`);
  else {
    if (effect.functionsOff.length === 0) lines.push(`<p>${esc(t(lang, 'rmNone'))}</p>`);
    else {
      const items = effect.functionsOff.map((f) =>
        `<li>${esc(functionName(view, f.id))} <small>${esc(t(lang, f.chosen ? 'stOwn' : 'stInc'))}</small></li>`);
      lines.push(`<p>${esc(t(lang, 'rmOff'))}</p><ul>${items.join('')}</ul>`);
    }
    const line = (key: Key, x: string): void => {
      lines.push(`<p>${esc(t(lang, key, { x }))}</p>`);
    };
    if (effect.partsOut.length > 0) line('outParts', partList(view, effect.partsOut));
    if (effect.options.length > 0) line('rmVar', effect.options.map((o) => variantShort(lang, o.key, o.to)).join(', '));
    if (effect.partsIn.length > 0) line('inParts', partList(view, effect.partsIn));
    if (effect.functionsOn.length > 0) line('onFns', effect.functionsOn.map((id) => functionName(view, id)).join(', '));
    if (effect.partsKept.length > 0) line('rmKept', partList(view, effect.partsKept));
    yes = `<button class="act" type="button" id="rm-yes">${esc(t(lang, effect.functionsOff.length > 0 ? 'rmYes' : 'rmYesPlain'))}</button>`;
  }
  return '<div class="warn" id="rmwarn" role="alertdialog" aria-labelledby="rmwarn-h" aria-describedby="rmwarn-d">'
    + `<p id="rmwarn-h"><b>${esc(t(lang, 'rmAsk', { p: shortName(view, pending.part) }))}</b></p>`
    + `<div id="rmwarn-d">${lines.join('')}</div>`
    + `<div class="acts">${yes}<button class="act" type="button" id="rm-no">${esc(t(lang, 'rmNo'))}</button></div></div>`;
}

/**
 * The remove control of a part in the parts list, with the question under it
 * while that removal is pending. Empty for a part of the base setup.
 */
export function removeControl(view: View, part: Part): string {
  if (part.locked) return '';
  const { lang, pending } = view;
  const open = pending?.at === 'result' && pending.part === part.id;
  return `<button class="act rm" type="button" id="rm-${esc(part.id)}" data-rm="${esc(part.id)}" aria-expanded="${open}"`
    + ` aria-label="${esc(t(lang, 'rmName', { p: tx(lang, part.short) }))}">${esc(t(lang, 'rm'))}</button>`
    + (open ? removalWarning(view) : '');
}

function ownedRows(view: View): string {
  const { lang, catalogue, setup, pending } = view;
  let h = '';
  for (const part of catalogue.parts) {
    if (!part.selectable) continue;
    const r = setup.parts.find((x) => x.part.id === part.id);
    // A part the setup holds without a tick of its own: the row states why.
    const sub = r !== undefined && !r.ownedTick ? reasonText(r, view) : part.role ? tx(lang, part.role) : '';
    h += `<label class="opt"><input type="checkbox" id="p-${esc(part.id)}" data-p="${esc(part.id)}"${r ? ' checked' : ''}>`
      + `<span><b>${esc(tx(lang, part.name))}</b></span><small>${esc(sub)}</small></label>`;
    if (pending?.at === 'own' && pending.part === part.id) h += removalWarning(view);
  }
  return h;
}

export function chooser(view: View, ownOpen: boolean): string {
  const { lang, catalogue, setup } = view;
  let h = `<h2 id="h-choose">${esc(t(lang, 'choose'))}</h2><p class="hint">${esc(t(lang, 'chooseHint'))}</p>`;
  for (const group of groupsOf(catalogue)) {
    h += `<fieldset class="grp" id="${esc(groupId(group))}">${groupHead(view, group)}${functionRows(view, group)}</fieldset>`;
  }
  h += `<fieldset><legend>${esc(t(lang, 'variants'))}</legend>${variantGroups(view)}</fieldset>`;
  const ticked = setup.parts.filter((p) => p.part.selectable).length;
  h += `<details class="own" id="own"${ownOpen ? ' open' : ''}><summary id="own-summary">${esc(t(lang, 'own'))} `
    + `<span class="tag">${esc(t(lang, 'ownN', { n: ticked }))}</span></summary>`
    + `<p class="hint">${esc(t(lang, 'ownHint'))}</p>${ownedRows(view)}</details>`;
  h += `<div class="nir"><h3>${esc(t(lang, 'nir'))}</h3><ul>`
    + catalogue.notInRelease.map((n) => `<li>${esc(tx(lang, n.text))}</li>`).join('')
    + '</ul></div>';
  return h;
}
