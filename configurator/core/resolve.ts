// The resolver: a selection in, a setup out.
//
// Pure: no I/O, no clock, no random number, no global state. Total: it
// returns a setup for every input, including unknown ids, duplicate ids,
// option values outside the type and a selection that is not an object.
// Output arrays are in catalogue order, never in input order.

import type {
  Catalogue, Cond, OptionKey, Options, PartId, Reason, ResolvedFunction, ResolvedOption,
  ResolvedPart, ResolvedPower, Selection, Setup, Unknown,
} from '../contract.ts';
import { holds } from './cond.ts';

// The exported tables are frozen: a write by a caller throws, so the result
// of `resolve` depends on its two arguments only.

/** The values of each option. The first value is the default. */
export const OPTION_VALUES: { readonly [K in OptionKey]: ReadonlyArray<Options[K]> } = Object.freeze({
  connection: Object.freeze(['pads', 'osoyoo'] as const),
  ina228Board: Object.freeze(['matek', 'xt90'] as const),
  escPower: Object.freeze(['battery', 'pd'] as const),
  servoPower: Object.freeze(['external', 'bench'] as const),
});

export const OPTION_KEYS: ReadonlyArray<OptionKey> = Object.freeze(
  ['connection', 'ina228Board', 'escPower', 'servoPower'] as const,
);

export const DEFAULT_OPTIONS: Options = Object.freeze({
  connection: 'pads',
  ina228Board: 'matek',
  escPower: 'battery',
  servoPower: 'external',
});

/**
 * The setups that show an option. `null`: every setup. The servo supply is a
 * choice while a servo is connected and no PD mini feeds it.
 */
const OPTION_WHEN: { readonly [K in OptionKey]: Cond | null } = {
  connection: null,
  ina228Board: { has: 'ina228' },
  escPower: { has: 'esc' },
  servoPower: { all: [{ has: 'servo' }, { not: { has: 'pdmini' } }] },
};

/** A printable form of a selection entry that is not a known id. */
function describe(value: unknown): string {
  if (typeof value === 'string') return value;
  try {
    return String(value);
  } catch {
    return '?';
  }
}

function list(value: unknown): unknown[] {
  return Array.isArray(value) ? value : [];
}

function field(value: unknown, key: string): unknown {
  if (typeof value !== 'object' || value === null) return undefined;
  return (value as Record<string, unknown>)[key];
}

function isValue(key: OptionKey, value: unknown): boolean {
  return typeof value === 'string' && (OPTION_VALUES[key] as ReadonlyArray<string>).includes(value);
}

export function resolve(catalogue: Catalogue, selection: Selection): Setup {
  const ignored = new Set<string>();

  // Chosen functions: known ids only, as a set, so order and repeats in the
  // input have no effect.
  const functionIds = new Set(catalogue.functions.map((f) => f.id));
  const chosen = new Set<string>();
  for (const entry of list(field(selection, 'functions'))) {
    if (typeof entry === 'string' && functionIds.has(entry)) chosen.add(entry);
    else ignored.add(describe(entry));
  }

  // Owned parts: only parts listed under "Parts you own". A test object is
  // none of them.
  const selectable = new Set(catalogue.parts.filter((p) => p.selectable).map((p) => p.id));
  const owned = new Set<string>();
  for (const entry of list(field(selection, 'owned'))) {
    if (typeof entry === 'string' && selectable.has(entry)) owned.add(entry);
    else ignored.add(describe(entry));
  }

  // Options: a value outside the type falls back to the default.
  const options: Options = { ...DEFAULT_OPTIONS };
  const given = field(selection, 'options');
  for (const key of OPTION_KEYS) {
    const value = field(given, key);
    if (value === undefined) continue;
    if (isValue(key, value)) (options as Record<OptionKey, string>)[key] = value as string;
    else ignored.add(describe(value));
  }

  // Reasons per part. Insertion follows the order of work: locked parts,
  // parts and test objects of chosen functions, owned parts, then rules.
  // An id the catalogue does not hold adds nothing; a function that needs it
  // lists it as missing.
  const partIds = new Set(catalogue.parts.map((p) => p.id));
  const reasons = new Map<PartId, Reason[]>();
  const add = (id: PartId, reason: Reason): void => {
    if (!partIds.has(id)) return;
    const present = reasons.get(id);
    if (present) present.push(reason);
    else reasons.set(id, [reason]);
  };

  for (const part of catalogue.parts) {
    if (part.locked) add(part.id, { kind: 'always' });
  }
  for (const fn of catalogue.functions) {
    if (!chosen.has(fn.id)) continue;
    for (const id of fn.needs) add(id, { kind: 'function', id: fn.id });
    if (fn.drives !== undefined) add(fn.drives, { kind: 'function', id: fn.id });
  }
  for (const part of catalogue.parts) {
    if (owned.has(part.id)) add(part.id, { kind: 'owned' });
  }

  // Forced options: chosen functions in catalogue order; the first function
  // that forces an option names it.
  const forcedBy: Record<OptionKey, string | null> = {
    connection: null,
    ina228Board: null,
    escPower: null,
    servoPower: null,
  };
  for (const fn of catalogue.functions) {
    if (!chosen.has(fn.id) || !fn.forces) continue;
    for (const key of OPTION_KEYS) {
      const value = fn.forces[key];
      if (value === undefined || forcedBy[key] !== null) continue;
      (options as Record<OptionKey, string>)[key] = value;
      forcedBy[key] = fn.id;
    }
  }

  const ctx = { has: (id: PartId): boolean => reasons.has(id), options };

  // Rules: array order, each evaluated once against the parts present then.
  for (const rule of catalogue.rules) {
    if (holds(rule.when, ctx)) add(rule.adds, { kind: 'rule', id: rule.id, because: rule.because });
  }

  const parts: ResolvedPart[] = [];
  for (const part of catalogue.parts) {
    const addedBy = reasons.get(part.id);
    if (!addedBy) continue;
    parts.push({
      part,
      addedBy,
      derived: addedBy.some((r) => r.kind !== 'owned'),
      ownedTick: owned.has(part.id),
    });
  }

  const wires = catalogue.wires.filter((w) => holds(w.when, ctx));
  const checks = catalogue.checks.filter((c) => holds(c.when, ctx));
  const unknown: Unknown[] = checks
    .filter((c) => c.kind === 'unknown')
    .map((c) => ({ id: c.id, text: c.text }));

  const functions: ResolvedFunction[] = catalogue.functions.map((fn) => {
    const missing = fn.needs.filter((id) => !reasons.has(id));
    const forces = fn.forces ?? {};
    const optionsHold = OPTION_KEYS.every(
      (key) => forces[key] === undefined || forces[key] === options[key],
    );
    // A function that drives a test object runs when that object is connected.
    const connected = fn.drives === undefined || reasons.has(fn.drives);
    return { fn, chosen: chosen.has(fn.id), enabled: missing.length === 0 && optionsHold && connected, missing };
  });

  const resolvedOptions: ResolvedOption[] = OPTION_KEYS.map((key) => {
    const gate = OPTION_WHEN[key];
    return {
      key,
      value: options[key],
      visible: gate === null || holds(gate, ctx),
      forcedBy: forcedBy[key],
    };
  });

  // Power sources: those of the logic and of each connected test object.
  const power: ResolvedPower[] = list(catalogue.powerSources)
    .map((entry) => entry as ResolvedPower['source'])
    .filter((source) => (source.feeds === 'logic' || reasons.has(source.feeds)) && holds(source.when, ctx))
    .map((source) => ({ source, used: holds(source.used, ctx) }));

  return {
    parts,
    functions,
    options: resolvedOptions,
    wires,
    checks,
    unknown,
    power,
    conflicts: [],
    ignored: [...ignored].sort(),
  };
}
