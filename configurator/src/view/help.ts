// The help bubble of a tick in the chooser: what the function or the part
// does, then what it needs.
//
// "Does" is the help text of the core for a function and the role of the part
// from the data file. "Needs" comes from the resolver: the setup with the item
// alone against the base setup, both in the variants of the page. Rule-added
// parts that a part of the bench makes unneeded carry that part. A part also
// names the wires of its supply. No list of parts is written twice.

import type { Cond, FunctionId, Lang, OptionKey, PartId, ResolvedPart, Selection, Setup } from '../../contract.ts';
import { FUNCTION_HELP, alwaysOn, resolve } from '../core.ts';
import { esc, t, tx } from '../i18n.ts';
import { endName, splitRef } from '../names.ts';
import { variantShort } from './chooser.ts';
import type { View } from './chooser.ts';

/** A bubble in words: its first line says what the item does, the others what it needs. */
export type Help = { id: string; does: string; needs: string[] };

/** The id of the bubble of the function or the part `id`. */
export const helpId = (kind: 'f' | 'p', id: string): string => `hb-${kind}-${id}`;

/** The parts whose absence a condition asks for: the ids under an odd count of `not`. */
function without(cond: Cond | null, negated = false): PartId[] {
  if (cond === null) return [];
  if ('has' in cond) return negated ? [cond.has] : [];
  if ('opt' in cond) return [];
  if ('not' in cond) return without(cond.not, !negated);
  return ('all' in cond ? cond.all : cond.any).flatMap((c) => without(c, negated));
}

function shortName(view: View, id: PartId): string {
  const part = view.catalogue.parts.find((p) => p.id === id);
  return part ? tx(view.lang, part.short) : id;
}

/** A part a rule alone adds, with the parts that make it unneeded: "2 × 2.2 kΩ (only without INA3221)". */
function neededName(view: View, r: ResolvedPart): string {
  const name = shortName(view, r.part.id);
  if (r.addedBy.some((reason) => reason.kind !== 'rule')) return name;
  const ids = new Set<PartId>();
  for (const reason of r.addedBy) {
    if (reason.kind !== 'rule') continue;
    const rule = view.catalogue.rules.find((x) => x.id === reason.id);
    for (const id of without(rule?.when ?? null)) ids.add(id);
  }
  if (ids.size === 0) return name;
  return `${name} (${t(view.lang, 'hWithout', { x: [...ids].map((id) => shortName(view, id)).join(t(view.lang, 'or')) })})`;
}

/** The setup of `selection` in the variants of the page, or null when the resolver throws. */
function setupOf(view: View, selection: Omit<Selection, 'options'>): Setup | null {
  try {
    return resolve(view.catalogue, { ...selection, options: { ...view.options } });
  } catch {
    return null;
  }
}

const BASE = new WeakMap<View['catalogue'], Map<string, ReadonlySet<PartId>>>();

/** The parts of the base setup in the variants of the page, resolved once per catalogue and set of variants. */
function baseOf(view: View): ReadonlySet<PartId> {
  let byOptions = BASE.get(view.catalogue);
  if (!byOptions) {
    byOptions = new Map();
    BASE.set(view.catalogue, byOptions);
  }
  const key = JSON.stringify(view.options);
  let ids = byOptions.get(key);
  if (!ids) {
    ids = new Set((setupOf(view, { functions: [], owned: [] })?.parts ?? []).map((r) => r.part.id));
    byOptions.set(key, ids);
  }
  return ids;
}

/**
 * The parts the item brings beyond the base setup, as lines: the bench parts,
 * the test objects it connects and the variants it sets. `self` is left out.
 * One line "Needs no further part of the bench." when there is none.
 */
function needsLines(view: View, setup: Setup, self: PartId | null, forces: Record<string, string>): string[] {
  const { lang } = view;
  const before = baseOf(view);
  const added = setup.parts.filter((r) => !before.has(r.part.id) && r.part.id !== self);
  const bench = added.filter((r) => r.part.testObject !== true);
  const objects = added.filter((r) => r.part.testObject === true);
  const lines: string[] = [];
  if (bench.length > 0) lines.push(`${t(lang, 'gNeeds')}${bench.map((r) => neededName(view, r)).join(', ')}.`);
  if (objects.length > 0) lines.push(t(lang, 'stObj', { x: objects.map((r) => shortName(view, r.part.id)).join(', ') }));
  const variants = Object.entries(forces).map(([key, value]) => variantShort(lang, key as OptionKey, value));
  if (variants.length > 0) lines.push(t(lang, 'hVar', { x: variants.join(', ') }));
  if (lines.length === 0) lines.push(t(lang, 'hNone'));
  return lines;
}

/** The first letter in upper case: "supply, 3.3 V" gives "Supply, 3.3 V". */
const upper = (lang: Lang, s: string): string => s.charAt(0).toLocaleUpperCase(lang) + s.slice(1);

/**
 * The supply wires of a part in the setup with it, each with its note: the logic supply ("Supply, 3.3 V:
 * Coprocessor 3V3 (pad 36).") and the power input ("PD in: T-plug OUT, USB-C socket."), the wires of class logic and power that
 * end on the part.
 */
function supplyLines(view: View, setup: Setup, id: PartId): string[] {
  const { lang, catalogue } = view;
  const ctx = { lang, catalogue, options: view.options };
  const out: string[] = [];
  for (const w of setup.wires) {
    if ((w.cls !== 'logic' && w.cls !== 'power') || w.b === null || splitRef(w.b)[0] !== id) continue;
    out.push(`${upper(lang, tx(lang, w.signal))}: ${endName(w.a, ctx)}.${w.note ? ` ${tx(lang, w.note)}` : ''}`);
  }
  return out;
}

/** The bubble of a function with a tick. Null for a function every setup runs and for an unknown id. */
export function functionHelp(view: View, id: FunctionId): Help | null {
  const fn = view.catalogue.functions.find((f) => f.id === id);
  if (!fn || alwaysOn(view.catalogue, fn)) return null;
  const text = FUNCTION_HELP[id];
  const setup = setupOf(view, { functions: [id], owned: [] });
  if (!text || !setup) return null;
  return { id: helpId('f', id), does: tx(view.lang, text), needs: needsLines(view, setup, null, { ...fn.forces }) };
}

/** The bubble of a part under "Parts you own". Null for a part without a tick there and for an unknown id. */
export function partHelp(view: View, id: PartId): Help | null {
  const part = view.catalogue.parts.find((p) => p.id === id);
  if (!part || !part.selectable || part.testObject === true) return null;
  const setup = setupOf(view, { functions: [], owned: [id] });
  if (!setup) return null;
  const does = part.role ? tx(view.lang, part.role) : tx(view.lang, part.name);
  return { id: helpId('p', id), does, needs: [...needsLines(view, setup, id, {}), ...supplyLines(view, setup, id)] };
}

/** The bubble as markup. Hidden until its tick has the keyboard focus, or its mark the pointer or a tap. */
export function bubble(help: Help): string {
  return `<div class="bubble" id="${esc(help.id)}" role="tooltip">`
    + [help.does, ...help.needs].map((line) => `<p>${esc(line)}</p>`).join('')
    + '</div>';
}
