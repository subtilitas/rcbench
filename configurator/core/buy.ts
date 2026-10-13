// The rows of the parts list of a part: what to order, or what the visitor
// brings. Pure, like the resolver.

import type { Buy, Options, Part } from '../contract.ts';

/** The visitor brings the part: a test object, or a part the data marks `yours`. */
export function brought(part: Part): boolean {
  return part.testObject === true || part.yours === true;
}

/** The rows of a part that apply to `options`, in data order. */
export function buyRows(part: Part, options: Options): Buy[] {
  return part.buy.filter((row) =>
    Object.entries(row.only ?? {}).every(([key, value]) => options[key as keyof Options] === value));
}
