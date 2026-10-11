// Conditions of wires, checks and rules.

import type { Cond, OptionKey, Options, PartId } from '../contract.ts';

/** What a condition is evaluated against: the parts present and the option values. */
export type CondContext = {
  has: (id: PartId) => boolean;
  options: Options;
};

/** True when the condition holds. `null` is "always". */
export function holds(cond: Cond | null, ctx: CondContext): boolean {
  if (cond === null) return true;
  if ('has' in cond) return ctx.has(cond.has);
  if ('opt' in cond) return ctx.options[cond.opt] === cond.is;
  if ('not' in cond) return !holds(cond.not, ctx);
  if ('all' in cond) return cond.all.every((c) => holds(c, ctx));
  return cond.any.some((c) => holds(c, ctx));
}

/** Every part id a condition names, in order of appearance. */
export function partsIn(cond: Cond | null): PartId[] {
  if (cond === null) return [];
  if ('has' in cond) return [cond.has];
  if ('opt' in cond) return [];
  if ('not' in cond) return partsIn(cond.not);
  const list = 'all' in cond ? cond.all : cond.any;
  return list.flatMap((c) => partsIn(c));
}

/** Every option value a condition names, in order of appearance. */
export function optionsIn(cond: Cond | null): Array<{ opt: OptionKey; is: string }> {
  if (cond === null) return [];
  if ('has' in cond) return [];
  if ('opt' in cond) return [{ opt: cond.opt, is: cond.is }];
  if ('not' in cond) return optionsIn(cond.not);
  const list = 'all' in cond ? cond.all : cond.any;
  return list.flatMap((c) => optionsIn(c));
}

/**
 * True when the condition can hold only while option `opt` has value `is`:
 * the condition is that test, or a conjunction with a member that requires it.
 */
export function requiresOption(cond: Cond | null, opt: OptionKey, is: string): boolean {
  if (cond === null) return false;
  if ('opt' in cond) return cond.opt === opt && cond.is === is;
  if ('all' in cond) return cond.all.some((c) => requiresOption(c, opt, is));
  if ('any' in cond) return cond.any.length > 0 && cond.any.every((c) => requiresOption(c, opt, is));
  return false;
}
