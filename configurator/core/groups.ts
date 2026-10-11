// Groups of functions by the parts they need, and the effect of taking a
// function or a part out of a selection.
//
// Pure, like the resolver: no I/O, no clock, no random number, no global
// state. Every result is derived from the catalogue through `resolve`; no
// group and no effect is listed by hand. Output arrays are in catalogue order.

import type {
  Catalogue, Cond, FunctionId, OptionKey, Options, PartId, Selection, Setup,
} from '../contract.ts';
import { holds } from './cond.ts';
import { DEFAULT_OPTIONS, OPTION_KEYS, OPTION_VALUES, resolve } from './resolve.ts';

/** A needed part that exists as one of several boards. */
export type GroupVariant = { part: PartId; option: OptionKey; values: string[] };

/**
 * The functions that need the same set of parts beyond the base setup.
 *
 * - `parts`: that set. Empty for the functions the base setup runs.
 * - `variants`: parts of the set that are one of several boards.
 * - `also`: parts the rules add to the set under every variant.
 * - `oneOf`: parts the rules add under some variants only, one entry per
 *   distinct set. An empty entry is a variant that adds no part. Empty when
 *   every variant adds the same parts.
 */
export type Group = {
  key: string;
  parts: PartId[];
  functions: FunctionId[];
  variants: GroupVariant[];
  also: PartId[];
  oneOf: PartId[][];
};

/** A variant that takes another value: `from` and `to` are values of the option. */
export type OptionChange = { key: OptionKey; from: string; to: string };

/**
 * Why a part does not leave the setup:
 * - `unknown`: the catalogue holds no part of this id.
 * - `base`: the part belongs to the base setup.
 * - `absent`: the part is not in the setup of the selection.
 * - `held`: a rule adds the part and no removal of a part and no other variant ends it.
 */
export type Refusal = 'unknown' | 'base' | 'absent' | 'held';

/**
 * What removing one part does to a selection.
 *
 * - `selection`: the selection without the part. With a refusal it is the
 *   selection as given, reduced to known ids.
 * - `functionsOff`: functions the setup runs before and not after. `chosen`
 *   is true for a function of the visitor's own selection.
 * - `functionsOn`: functions the setup runs after and not before: a part the
 *   rules add, or a variant that changes, runs them.
 * - `partsOut`: other parts that leave with it.
 * - `partsIn`: parts the rules add once it is gone.
 * - `partsKept`: parts of a function that goes off which stay, as owned parts.
 * - `options`: variants of the setup that change.
 */
export type Removal = {
  part: PartId;
  refused: Refusal | null;
  selection: Selection;
  functionsOff: Array<{ id: FunctionId; chosen: boolean }>;
  functionsOn: FunctionId[];
  partsOut: PartId[];
  partsIn: PartId[];
  partsKept: PartId[];
  options: OptionChange[];
};

/**
 * What taking one function out of the visitor's own selection does.
 *
 * - `changed`: false when the function is not in the own selection.
 * - `stillOn`: the setup runs the function without the tick.
 * - `functionsOff`: other functions that go off with it.
 * - `partsOut`: parts that leave. `partsIn`: parts the rules add.
 * - `partsStay`: parts of the function that stay in the setup.
 */
export type Deselect = {
  fn: FunctionId;
  changed: boolean;
  selection: Selection;
  stillOn: boolean;
  functionsOff: FunctionId[];
  partsOut: PartId[];
  partsIn: PartId[];
  partsStay: PartId[];
};

/** The parts of the base setup: every setup holds them. */
export function baseParts(catalogue: Catalogue): PartId[] {
  return catalogue.parts.filter((p) => p.locked).map((p) => p.id);
}

/** Every combination of option values, the default combination first. */
function optionSets(): Options[] {
  let sets: Array<Record<string, string>> = [{}];
  for (const key of OPTION_KEYS) {
    sets = sets.flatMap((set) => OPTION_VALUES[key].map((value) => ({ ...set, [key]: value })));
  }
  return sets as Options[];
}

function partIds(setup: Setup): PartId[] {
  return setup.parts.map((p) => p.part.id);
}

/**
 * The groups of a catalogue. Each function is in exactly one group. Order:
 * the group without a part first, then by the count of parts, then by the
 * catalogue position of the parts. A part id the catalogue does not hold
 * sorts after every known part.
 */
export function groups(catalogue: Catalogue): Group[] {
  const base = new Set(baseParts(catalogue));
  const position = new Map<PartId, number>();
  for (const part of catalogue.parts) position.set(part.id, position.size);
  for (const fn of catalogue.functions) {
    for (const id of fn.needs) if (!position.has(id)) position.set(id, position.size);
  }
  const at = (id: PartId): number => position.get(id) as number;

  const byKey = new Map<string, Group>();
  for (const fn of catalogue.functions) {
    const parts = [...new Set(fn.needs)].filter((id) => !base.has(id)).sort((a, b) => at(a) - at(b));
    const key = parts.join('+');
    const group = byKey.get(key);
    if (group) group.functions.push(fn.id);
    else byKey.set(key, { key, parts, functions: [fn.id], variants: [], also: [], oneOf: [] });
  }

  const sets = optionSets();
  for (const group of byKey.values()) {
    for (const id of group.parts) {
      const part = catalogue.parts.find((p) => p.id === id);
      const values = part?.variantName ? Object.keys(part.variantName.names) : [];
      if (part?.variantName && values.length > 1) group.variants.push({ part: id, option: part.variantName.option, values });
    }
    // What the rules add: the setup of each function alone, under every
    // combination of variants, without the base parts and the group's parts.
    const named = new Set(group.parts);
    const extras = new Map<string, PartId[]>();
    for (const fn of group.functions) {
      for (const options of sets) {
        const setup = resolve(catalogue, { functions: [fn], owned: [], options });
        const extra = partIds(setup).filter((id) => !base.has(id) && !named.has(id));
        extras.set(extra.join('+'), extra);
      }
    }
    const all = [...extras.values()];
    group.also = (all[0] as PartId[]).filter((id) => all.every((extra) => extra.includes(id)));
    if (all.length > 1) group.oneOf = all.map((extra) => extra.filter((id) => !group.also.includes(id)));
  }

  return [...byKey.values()].sort((a, b) => {
    if (a.parts.length !== b.parts.length) return a.parts.length - b.parts.length;
    return a.parts.reduce((d, id, i) => d || at(id) - at(b.parts[i] as PartId), 0);
  });
}

/** The selection reduced to what the resolver accepts: known ids in catalogue order, valid option values. */
function own(catalogue: Catalogue, selection: Selection, setup: Setup): Selection {
  const options: Record<string, string> = { ...DEFAULT_OPTIONS };
  const given: unknown = typeof selection === 'object' && selection !== null ? selection.options : undefined;
  if (typeof given === 'object' && given !== null) {
    for (const key of OPTION_KEYS) {
      const value = (given as Record<string, unknown>)[key];
      if ((OPTION_VALUES[key] as ReadonlyArray<unknown>).includes(value)) options[key] = value as string;
    }
  }
  return {
    functions: setup.functions.filter((f) => f.chosen).map((f) => f.fn.id),
    owned: catalogue.parts.filter((p) => setup.parts.some((r) => r.ownedTick && r.part.id === p.id)).map((p) => p.id),
    options: options as Options,
  };
}

function enabled(setup: Setup): Set<FunctionId> {
  return new Set(setup.functions.filter((f) => f.enabled).map((f) => f.fn.id));
}

/** Variants that both setups show and that differ. */
function optionChanges(before: Setup, after: Setup): OptionChange[] {
  const out: OptionChange[] = [];
  for (const a of after.options) {
    const b = before.options.find((o) => o.key === a.key);
    if (b && a.visible && b.visible && a.value !== b.value) out.push({ key: a.key, from: b.value, to: a.value });
  }
  return out;
}

type Move = { parts: PartId[]; flips: Array<{ key: OptionKey; to: string }> };

type BreakContext = {
  has: (id: PartId) => boolean;
  options: Options;
  /** Parts of the base setup: they do not leave. */
  locked: Set<PartId>;
  /** Variants a function forces or an earlier step changed: they keep their value. */
  fixed: Set<OptionKey>;
};

/**
 * The ways a condition that holds stops holding, each as parts to take out
 * and variants to change. An empty list when no removal does it. A
 * conjunction ends with one member; a way that changes a variant alone comes
 * before a way that takes a part. A disjunction ends when every member that
 * holds ends. A negation ends only when a part comes in, which a removal does
 * not do.
 */
function breakCond(cond: Cond, ctx: BreakContext): Move[] {
  if ('has' in cond) return ctx.locked.has(cond.has) ? [] : [{ parts: [cond.has], flips: [] }];
  if ('opt' in cond) {
    if (ctx.fixed.has(cond.opt)) return [];
    return (OPTION_VALUES[cond.opt] as ReadonlyArray<string>).filter((value) => value !== cond.is)
      .map((to) => ({ parts: [], flips: [{ key: cond.opt, to }] }));
  }
  if ('not' in cond) return [];
  if ('all' in cond) {
    const moves = cond.all.flatMap((c) => breakCond(c, ctx));
    return [...moves.filter((m) => m.parts.length === 0), ...moves.filter((m) => m.parts.length > 0)];
  }
  let out: Move[] = [{ parts: [], flips: [] }];
  for (const member of cond.any) {
    if (!holds(member, ctx)) continue;
    const ways = breakCond(member, ctx);
    out = out.flatMap((o) => ways.map((w) => ({ parts: [...o.parts, ...w.parts], flips: [...o.flips, ...w.flips] })));
  }
  return out;
}

/**
 * The effect of removing part `partId` from the setup of `selection`.
 *
 * The part leaves the owned parts. Every chosen function that needs it leaves
 * the selection; the other parts of such a function stay as owned parts where
 * the catalogue lets a visitor own them. Where a rule still adds the part,
 * the rule ends: by the other value of a variant it reads, else by removing
 * the part that needs it. No part comes in except by a rule.
 *
 * Adding the part and the selectable parts of `partsOut` back as owned parts,
 * with the variants of the setup before, gives the functions of that setup
 * when every part a function needs is selectable.
 */
export function removalEffect(catalogue: Catalogue, selection: Selection, partId: PartId): Removal {
  const before = resolve(catalogue, selection);
  const start = own(catalogue, selection, before);
  const refuse = (refused: Refusal): Removal => ({
    part: partId, refused, selection: start, functionsOff: [], functionsOn: [], partsOut: [], partsIn: [],
    partsKept: [], options: [],
  });
  const part = catalogue.parts.find((p) => p.id === partId);
  if (!part) return refuse('unknown');
  if (part.locked) return refuse('base');
  if (!before.parts.some((r) => r.part.id === partId)) return refuse('absent');

  const locked = new Set(baseParts(catalogue));
  const selectable = new Set(catalogue.parts.filter((p) => p.selectable).map((p) => p.id));
  const needs = new Map(catalogue.functions.map((f) => [f.id, f.needs]));
  /** The selection without the parts of `gone`, under `options`. */
  const without = (gone: Set<PartId>, options: Record<OptionKey, string>): Selection => {
    const functions = start.functions.filter((id) => !(needs.get(id) as PartId[]).some((p) => gone.has(p)));
    const kept = new Set(start.owned);
    for (const id of start.functions) {
      if (!functions.includes(id)) for (const p of needs.get(id) as PartId[]) if (selectable.has(p)) kept.add(p);
    }
    return {
      functions,
      owned: catalogue.parts.map((p) => p.id).filter((id) => kept.has(id) && !gone.has(id)),
      options: { ...options } as Options,
    };
  };

  // A part of `gone` that is still present is there by a rule. The search ends
  // one such rule per step, by a variant or by a part, and takes the next way
  // where a way leads to a part that no removal releases. A step adds a part
  // to `gone` or fixes a variant, so the depth is bounded; a state is tried once.
  const tried = new Set<string>();
  const search = (gone: Set<PartId>, options: Record<OptionKey, string>, flipped: Set<OptionKey>): Selection | null => {
    const key = `${[...gone].sort().join(',')}|${OPTION_KEYS.map((k) => options[k]).join(',')}|${[...flipped].sort().join(',')}`;
    if (tried.has(key)) return null;
    tried.add(key);
    const next = without(gone, options);
    const after = resolve(catalogue, next);
    const stuck = after.parts.filter((r) => gone.has(r.part.id));
    if (stuck.length === 0) return next;
    const ctx: BreakContext = {
      has: (id) => after.parts.some((r) => r.part.id === id),
      options: Object.fromEntries(after.options.map((o) => [o.key, o.value])) as Options,
      locked,
      fixed: new Set([...flipped, ...after.options.filter((o) => o.forcedBy !== null).map((o) => o.key)]),
    };
    for (const held of stuck) {
      for (const reason of held.addedBy) {
        const rule = reason.kind === 'rule' ? catalogue.rules.find((r) => r.id === reason.id) : undefined;
        for (const move of rule ? breakCond(rule.when, ctx) : []) {
          // A way that names only parts that are gone already ends nothing.
          if (move.flips.length === 0 && move.parts.every((id) => gone.has(id))) continue;
          const moved = { ...options };
          for (const flip of move.flips) moved[flip.key] = flip.to;
          const found = search(new Set([...gone, ...move.parts]), moved, new Set([...flipped, ...move.flips.map((f) => f.key)]));
          if (found) return found;
        }
      }
    }
    return null;
  };

  const next = search(new Set([partId]), { ...start.options }, new Set());
  if (next === null) return refuse('held');
  const after = resolve(catalogue, next);

  const was = partIds(before);
  const is = partIds(after);
  const on = enabled(after);
  const ran = enabled(before);
  return {
    part: partId,
    refused: null,
    selection: next,
    functionsOff: before.functions.filter((f) => f.enabled && !on.has(f.fn.id)).map((f) => ({ id: f.fn.id, chosen: f.chosen })),
    functionsOn: after.functions.filter((f) => f.enabled && !ran.has(f.fn.id)).map((f) => f.fn.id),
    partsOut: was.filter((id) => id !== partId && !is.includes(id)),
    partsIn: is.filter((id) => !was.includes(id)),
    partsKept: next.owned.filter((id) => !start.owned.includes(id)),
    options: optionChanges(before, after),
  };
}

/** True when the removal changes nothing but the part itself. */
export function removalIsPlain(effect: Removal): boolean {
  return effect.refused === null && effect.functionsOff.length === 0 && effect.functionsOn.length === 0
    && effect.partsOut.length === 0
    && effect.partsIn.length === 0 && effect.partsKept.length === 0 && effect.options.length === 0;
}

/**
 * The effect of taking function `fnId` out of the visitor's own selection.
 * Its parts stay where another chosen function, an owned part or a rule
 * still brings them.
 */
export function deselectEffect(catalogue: Catalogue, selection: Selection, fnId: FunctionId): Deselect {
  const before = resolve(catalogue, selection);
  const start = own(catalogue, selection, before);
  const fn = catalogue.functions.find((f) => f.id === fnId);
  if (!fn || !start.functions.includes(fnId)) {
    return {
      fn: fnId, changed: false, selection: start, stillOn: false, functionsOff: [], partsOut: [], partsIn: [], partsStay: [],
    };
  }
  const next: Selection = { ...start, functions: start.functions.filter((id) => id !== fnId) };
  const after = resolve(catalogue, next);
  const was = partIds(before);
  const is = partIds(after);
  const on = enabled(after);
  const base = new Set(baseParts(catalogue));
  return {
    fn: fnId,
    changed: true,
    selection: next,
    stillOn: on.has(fnId),
    functionsOff: before.functions.filter((f) => f.enabled && f.fn.id !== fnId && !on.has(f.fn.id)).map((f) => f.fn.id),
    partsOut: was.filter((id) => !is.includes(id)),
    partsIn: is.filter((id) => !was.includes(id)),
    partsStay: is.filter((id) => fn.needs.includes(id) && !base.has(id)),
  };
}
