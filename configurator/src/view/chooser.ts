// The chooser: function ticks, variants, owned parts, and the static list of
// what is in no release. Returns markup; main.ts owns the document.

import type { Catalogue, Cond, Fn, Lang, OptionKey, Options, ResolvedPart, Selection, Setup } from '../../contract.ts';
import { esc, t, tx } from '../i18n.ts';
import type { Key } from '../i18n.ts';

export type View = {
  lang: Lang;
  catalogue: Catalogue;
  setup: Setup;
  /** Option values of the setup, after forced options. */
  options: Options;
  /** The visitor's ticks. */
  selection: Selection;
};

const GROUPS: ReadonlyArray<readonly [Fn['group'], Key]> = [['servo', 'gServo'], ['esc', 'gEsc'], ['bench', 'gBench']];

type Variant = { key: OptionKey; label: Key; values: ReadonlyArray<readonly [string, Key]> };

export const VARIANTS: readonly Variant[] = [
  { key: 'connection', label: 'vConn', values: [['pads', 'vPads'], ['osoyoo', 'vOso']] },
  { key: 'ina228Board', label: 'vIna', values: [['matek', 'vMatek'], ['xt90', 'vXt']] },
  { key: 'escPower', label: 'vEscp', values: [['battery', 'vBatt'], ['pd', 'vPd']] },
];

const tag = (lang: Lang): string => ` <span class="tag">${esc(t(lang, 'nrh'))}</span>`;

/** The Osoyoo breakout board is in no build guide and has not run on hardware. */
export function variantNotRun(key: OptionKey, value: string): boolean {
  return key === 'connection' && value === 'osoyoo';
}

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
    else {
      const variant = VARIANTS.find((v) => v.key === r.key);
      const value = variant?.values.find((v) => v[0] === r.value);
      if (!variant || !value) by.push(r.value);
      else {
        // "A: the ESC's own battery" gives "ESC power A".
        const text = t(lang, value[1]);
        const colon = text.indexOf(':');
        by.push(`${t(lang, variant.label)} ${colon > 0 && colon <= 2 ? text.slice(0, colon) : text}`);
      }
    }
  }
  const out: string[] = [];
  if (always) out.push(t(lang, 'always'));
  if (by.length > 0) out.push(t(lang, 'neededBy') + [...new Set(by)].join(', '));
  if (own) out.push(t(lang, 'youOwn'));
  return out.length > 0 ? `${out.join('. ')}.` : '';
}

function functionRows(view: View, group: Fn['group']): string {
  const { lang, catalogue, setup } = view;
  let h = '';
  for (const f of setup.functions) {
    if (f.fn.group !== group) continue;
    const short = f.missing.map((id) => {
      const p = catalogue.parts.find((x) => x.id === id);
      return p ? tx(lang, p.short) : id;
    });
    const sub = f.chosen ? '' : short.length > 0 ? t(lang, 'adds') + short.join(', ') : t(lang, 'nothing');
    h += `<label class="opt"><input type="checkbox" id="f-${esc(f.fn.id)}" data-f="${esc(f.fn.id)}"${f.chosen ? ' checked' : ''}>`
      + `<span><b>${esc(tx(lang, f.fn.name))}</b>${f.fn.hardware === 'not-run' ? tag(lang) : ''}</span>`
      + `${sub ? `<small>${esc(sub)}</small>` : ''}</label>`;
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

/** True when a condition reads a variant. */
function readsOption(c: Cond | null): boolean {
  if (c === null) return false;
  if ('opt' in c) return true;
  if ('not' in c) return readsOption(c.not);
  if ('all' in c) return c.all.some(readsOption);
  if ('any' in c) return c.any.some(readsOption);
  return false;
}

const LOCK: Array<['function' | 'option' | 'rule', Key]> = [['function', 'lockFn'], ['option', 'lockOpt'], ['rule', 'lockRule']];

/**
 * How a locked part leaves the setup: one sentence per kind of reason that
 * holds it. A rule whose condition reads a variant counts as that variant.
 */
export function lockHint(part: ResolvedPart, lang: Lang, catalogue: Catalogue): string {
  const kinds = new Set(part.addedBy.map((r) => {
    if (r.kind !== 'rule') return r.kind;
    const rule = catalogue.rules.find((x) => x.id === r.id);
    return rule && readsOption(rule.when) ? 'option' : 'rule';
  }));
  return LOCK.filter(([kind]) => kinds.has(kind)).map(([, key]) => t(lang, key)).join(' ');
}

function ownedRows(view: View): string {
  const { lang, catalogue, setup } = view;
  let h = '';
  for (const part of catalogue.parts) {
    if (!part.selectable) continue;
    const r = setup.parts.find((x) => x.part.id === part.id);
    const locked = r !== undefined && r.derived && !r.ownedTick;
    const role = part.role ? tx(lang, part.role) : '';
    const sub = locked && r ? `${reasonText(r, view)} ${lockHint(r, lang, catalogue)}` : role;
    h += `<label class="opt"${locked ? ' data-locked' : ''}><input type="checkbox" id="p-${esc(part.id)}" data-p="${esc(part.id)}"`
      + `${r ? ' checked' : ''}${locked ? ' aria-disabled="true"' : ''}>`
      + `<span><b>${esc(tx(lang, part.name))}</b></span><small>${esc(sub)}</small></label>`;
  }
  return h;
}

export function chooser(view: View, ownOpen: boolean): string {
  const { lang, catalogue, setup } = view;
  let h = `<h2 id="h-choose">${esc(t(lang, 'choose'))}</h2>`;
  for (const [group, key] of GROUPS) {
    h += `<fieldset><legend>${esc(t(lang, key))}</legend>${functionRows(view, group)}</fieldset>`;
  }
  h += `<fieldset><legend>${esc(t(lang, 'variants'))}</legend>${variantGroups(view)}</fieldset>`;
  const ticked = setup.parts.filter((p) => p.ownedTick).length;
  h += `<details class="own" id="own"${ownOpen ? ' open' : ''}><summary id="own-summary">${esc(t(lang, 'own'))} `
    + `<span class="tag">${esc(t(lang, 'ownN', { n: ticked }))}</span></summary>${ownedRows(view)}</details>`;
  h += `<div class="nir"><h3>${esc(t(lang, 'nir'))}</h3><ul>`
    + catalogue.notInRelease.map((n) => `<li>${esc(tx(lang, n.text))}</li>`).join('')
    + '</ul></div>';
  return h;
}
