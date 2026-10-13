// The chooser: functions grouped by the bench parts they need, variants, owned
// parts, the warning before a part leaves, and the static list of what is in
// no release. Returns markup; main.ts owns the document.

import type {
  Catalogue, FunctionId, Lang, OptionKey, Options, Part, PartId, PowerSource, ResolvedFunction, ResolvedPart, Selection,
  Setup, Text,
} from '../../contract.ts';
import { alwaysOn, groups, removalEffect, removalIsPlain } from '../core.ts';
import type { Deselect, Group } from '../core.ts';
import { esc, t, tx } from '../i18n.ts';
import type { Key } from '../i18n.ts';
import { bubble, functionHelp, partHelp } from './help.ts';
import type { Help } from './help.ts';

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
  { key: 'servoPower', label: 'vServo', values: [['external', 'vSExt'], ['bench', 'vSBench']] },
  // Never visible: the function that programs a servo forces it. Its words
  // name the variant that function needs.
  { key: 'servoPort', label: 'vPort', values: [['outputs', 'vPortOut'], ['prog', 'vPortProg']] },
];

const tag = (lang: Lang, key: Key): string => ` <span class="tag">${esc(t(lang, key))}</span>`;

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

/** Whether the rule's condition is one bench part that is no test object: the rule brings a part along with a part. */
export function ruleFollowsPart(catalogue: View['catalogue'], ruleId: string): boolean {
  const rule = catalogue.rules.find((r) => r.id === ruleId);
  const when = rule?.when;
  if (!when || !('has' in when)) return false;
  return catalogue.parts.some((p) => p.id === when.has && p.testObject !== true);
}

/**
 * Why a part is in the setup, as one line: "Needed by: Servo. You own it."
 * A part a rule adds along with another bench part reads "Included with these
 * parts: PD mini."; any other rule reads "Needed by: " and its cause.
 */
export function reasonText(part: ResolvedPart, view: View): string {
  const { lang, catalogue } = view;
  const by: string[] = [];
  const included: string[] = [];
  let always = false;
  let own = false;
  for (const r of part.addedBy) {
    if (r.kind === 'always') always = true;
    else if (r.kind === 'owned') own = true;
    else if (r.kind === 'function') {
      const fn = catalogue.functions.find((f) => f.id === r.id);
      by.push(fn ? tx(lang, fn.name) : r.id);
    } else if (r.kind === 'rule') (ruleFollowsPart(catalogue, r.id) ? included : by).push(tx(lang, r.because));
    else by.push(variantShort(lang, r.key, r.value));
  }
  const out: string[] = [];
  if (always) out.push(t(lang, 'always'));
  if (by.length > 0) out.push(t(lang, 'neededBy') + [...new Set(by)].join(', '));
  if (included.length > 0) out.push(`${t(lang, 'stIncBy')}${[...new Set(included)].join(', ')}`);
  if (own) out.push(t(lang, 'youOwn'));
  return out.length > 0 ? `${out.join('. ')}.` : '';
}

/** "Used by: Drive a servo, Automatic servo test." The enabled functions that drive the test object. */
export function usedByText(view: View, r: ResolvedPart): string {
  const { lang, setup } = view;
  const names = setup.functions.filter((f) => f.enabled && f.fn.drives === r.part.id).map((f) => tx(lang, f.fn.name));
  return names.length > 0 ? `${t(lang, 'usedBy')}${names.join(', ')}.` : '';
}

/** The heading of the power sources of the logic or of a test object. */
export function powerTitle(view: View, feeds: PowerSource['feeds']): string {
  const { lang, catalogue } = view;
  if (feeds === 'logic') return t(lang, 'pwLogic');
  const part = catalogue.parts.find((p) => p.id === feeds);
  return t(lang, 'pwFor', { x: part ? tx(lang, part.short) : feeds });
}

/** The lines of a power source after its text: advantages, drawbacks, unknowns, each with its kind. */
export function powerLines(lang: Lang, source: PowerSource): Array<{ kind: string; text: string }> {
  return [
    ...source.pros.map((x) => ({ kind: t(lang, 'pwPro'), text: tx(lang, x) })),
    ...source.cons.map((x) => ({ kind: t(lang, 'pwCon'), text: tx(lang, x) })),
    ...source.unknowns.map((x) => ({ kind: t(lang, 'kU'), text: tx(lang, x) })),
  ];
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

/** The id of a group's fieldset: "g-base", "g-pdmini", "g-sim-tap". */
export function groupId(group: Group): string {
  return group.parts.length === 0 ? 'g-base' : `g-${group.parts.join('-')}`;
}

/**
 * The heading of a group and the lines under it: the parts the functions
 * need, a part that is one of several boards, and the parts the rules add.
 * The group without a part has no such line: its rows state what a tick adds.
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
  const named = group.parts.length > 0;
  if (named && group.also.length > 0) lines.push(t(lang, 'gAlso', { x: partList(view, group.also) }));
  if (named && group.oneOf.length > 0) {
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

/** True for a function every setup runs: no part beyond the base setup, no test object. It takes no tick. */
export function runsOnBase(catalogue: Catalogue, f: ResolvedFunction): boolean {
  return alwaysOn(catalogue, f.fn);
}

/**
 * The state of a function in words. A function the base setup runs is never
 * "selected by you", whatever the selection names. A function that is off
 * states the parts a tick adds, else the variant it needs, else the test
 * object a tick connects.
 */
export function statusText(view: View, f: ResolvedFunction): string {
  const { lang, catalogue, options, setup } = view;
  if (f.enabled && runsOnBase(catalogue, f)) return t(lang, 'stBase');
  if (f.chosen) return t(lang, 'stOwn');
  const object = f.fn.drives;
  if (f.enabled) return object !== undefined && f.fn.needs.length === 0 ? t(lang, 'stConn', { x: shortName(view, object) }) : t(lang, 'stInc');
  if (f.missing.length > 0) return t(lang, 'adds') + partList(view, f.missing);
  const forced = (Object.entries(f.fn.forces ?? {}) as Array<[OptionKey, string]>)
    .filter(([key, value]) => options[key] !== value)
    .map(([key, value]) => variantShort(lang, key, value));
  if (forced.length > 0) return t(lang, 'stVar', { v: forced.join(', ') });
  const absent = object !== undefined && !setup.parts.some((r) => r.part.id === object);
  return absent ? t(lang, 'stObj', { x: shortName(view, object) }) : '';
}

/** What taking a function out of the selection did, in sentences. */
export function deselectText(view: View, effect: Deselect): string {
  const { lang } = view;
  const out = [t(lang, effect.stillOn ? 'dsOn' : 'dsOff', { f: functionName(view, effect.fn) })];
  const object = view.catalogue.functions.find((f) => f.id === effect.fn)?.drives;
  if (effect.stillOn && object !== undefined) out.push(t(lang, 'dsConn', { x: shortName(view, object) }));
  else if (effect.stillOn && effect.partsStay.length === 0) out.push(t(lang, 'dsBase'));
  if (effect.partsStay.length > 0) out.push(t(lang, 'dsStay', { x: partList(view, effect.partsStay) }));
  if (effect.objectsOut.length > 0) out.push(t(lang, 'outObjects', { x: partList(view, effect.objectsOut) }));
  if (effect.partsOut.length > 0) out.push(t(lang, 'outParts', { x: partList(view, effect.partsOut) }));
  if (effect.partsIn.length > 0) out.push(t(lang, 'inParts', { x: partList(view, effect.partsIn) }));
  if (effect.functionsOff.length > 0) {
    out.push(t(lang, 'offFns', { x: effect.functionsOff.map((id) => functionName(view, id)).join(', ') }));
  }
  return out.join(' ');
}

/**
 * The functions every setup runs: a list without controls. A row has no tick
 * and no state to change; it states its state in words like the other rows.
 */
function baseRows(view: View, fixed: ResolvedFunction[]): string {
  const { lang } = view;
  const items = fixed.map((f) => {
    const status = statusText(view, f);
    return `<li id="b-${esc(f.fn.id)}"><span><b>${esc(tx(lang, f.fn.name))}</b></span>`
      + `${status ? `<small>${esc(status)}</small>` : ''}</li>`;
  });
  return `<p class="hint">${esc(t(lang, 'baseHint'))}</p><ul class="base">${items.join('')}</ul>`;
}

/**
 * A tick row with its help bubble: the label, the mark that opens the bubble
 * by pointer or tap, and the bubble. The mark stands outside the label, so a
 * tap on it never changes the tick; it takes no focus, so the Tab order stays.
 * The tick names the bubble as its description.
 */
function tickRow(label: string, help: Help | null): string {
  if (!help) return label;
  const described = label.replace(/<input /, `<input aria-describedby="${esc(help.id)}" `);
  return `<div class="row">${described}<span class="info" aria-hidden="true"></span>${bubble(help)}</div>`;
}

/**
 * The rows of a group: the functions every setup runs as a list, then one
 * tick row per other function. In the group without a part a tick connects
 * a test object.
 */
function functionRows(view: View, group: Group): string {
  const { lang, catalogue, setup, note } = view;
  const rows = setup.functions.filter((f) => group.functions.includes(f.fn.id));
  const fixed = rows.filter((f) => runsOnBase(catalogue, f));
  let h = fixed.length > 0 ? baseRows(view, fixed) : '';
  if (fixed.length > 0 && fixed.length < rows.length) h += `<p class="hint" id="basetick">${esc(t(lang, 'baseTick'))}</p>`;
  for (const f of rows) {
    if (fixed.includes(f)) continue;
    const status = statusText(view, f);
    h += tickRow(`<label class="opt" data-state="${functionState(f)}"><input type="checkbox" id="f-${esc(f.fn.id)}" data-f="${esc(f.fn.id)}"${f.chosen ? ' checked' : ''}>`
      + `<span><b>${esc(tx(lang, f.fn.name))}</b></span>`
      + `${status ? `<small>${esc(status)}</small>` : ''}</label>`, functionHelp(view, f.fn.id));
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
        + `<span>${esc(t(lang, label))}</span>`
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
    if (removalIsPlain(effect)) lines.push(`<p>${esc(t(lang, 'rmPlain'))}</p>`);
    else if (effect.functionsOff.length === 0) lines.push(`<p>${esc(t(lang, 'rmNone'))}</p>`);
    else {
      const items = effect.functionsOff.map((f) =>
        `<li>${esc(functionName(view, f.id))} <small>${esc(t(lang, f.chosen ? 'stOwn' : 'stInc'))}</small></li>`);
      lines.push(`<p>${esc(t(lang, 'rmOff'))}</p><ul>${items.join('')}</ul>`);
    }
    const line = (key: Key, x: string): void => {
      lines.push(`<p>${esc(t(lang, key, { x }))}</p>`);
    };
    if (effect.partsOut.length > 0) line('outParts', partList(view, effect.partsOut));
    if (effect.objectsOut.length > 0) line('outObjects', partList(view, effect.objectsOut));
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
 * while that removal is pending. Empty for a part of the base setup and for
 * a test object.
 */
export function removeControl(view: View, part: Part): string {
  if (part.locked || part.testObject === true) return '';
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
    h += tickRow(`<label class="opt"><input type="checkbox" id="p-${esc(part.id)}" data-p="${esc(part.id)}"${r ? ' checked' : ''}>`
      + `<span><b>${esc(tx(lang, part.name))}</b></span><small>${esc(sub)}</small></label>`, partHelp(view, part.id));
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
