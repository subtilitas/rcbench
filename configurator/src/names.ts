// Names of wire ends as the table and the drawing's titles show them.

import type {
  Catalogue, EndRef, Lang, Options, Pad, Pin, PowerSource, ResolvedPart, ResolvedPower, Setup, Wire,
} from '../contract.ts';
import { t, tx } from './i18n.ts';

export type NameCtx = { lang: Lang; catalogue: Catalogue; options: Options };

const FALLBACK: Options = { connection: 'pads', ina228Board: 'matek', escPower: 'battery', servoPower: 'external' };

/** The bench hardware of a setup: its parts without the test objects. */
export function benchParts(setup: Setup): ResolvedPart[] {
  return setup.parts.filter((r) => r.part.testObject !== true);
}

/** The test objects a setup connects. */
export function testObjectsOf(setup: Setup): ResolvedPart[] {
  return setup.parts.filter((r) => r.part.testObject === true);
}

/** The power sources of a setup by what they feed, in the order of the setup. */
export function powerGroups(setup: Setup): Array<{ feeds: PowerSource['feeds']; sources: ResolvedPower[] }> {
  const out: Array<{ feeds: PowerSource['feeds']; sources: ResolvedPower[] }> = [];
  for (const entry of setup.power) {
    const group = out.find((g) => g.feeds === entry.source.feeds);
    if (group) group.sources.push(entry);
    else out.push({ feeds: entry.source.feeds, sources: [entry] });
  }
  return out;
}

/** Voltage and current of a power source as text: "1 to 20 V, 0.05 to 3 A. Vendor values, not measured." Empty without a number. */
export function powerNumbers(lang: Lang, source: PowerSource): string {
  const num = (v: number): string => (lang === 'de' ? String(v).replace('.', ',') : String(v));
  const span = (min: number, max: number): string => (min === max ? num(min) : t(lang, 'pwTo', { a: num(min), b: num(max) }));
  const out: string[] = [];
  const { volts, amps } = source;
  if (volts) out.push(`${volts.steps ? volts.steps.map(num).join(t(lang, 'pwOr')) : span(volts.min, volts.max)} V`);
  if (amps) out.push(`${amps.min === null ? t(lang, 'pwUpTo', { a: num(amps.max) }) : span(amps.min, amps.max)} A`);
  if (out.length === 0) return '';
  return `${out.join(', ')}.${source.basis ? ` ${tx(lang, source.basis)}` : ''}`;
}

/** Option values of a setup as one record. */
export function optionsOf(setup: Setup): Options {
  const out: Record<string, string> = { ...FALLBACK };
  for (const o of setup.options) out[o.key] = o.value;
  return out as Options;
}

/** Splits "part.PIN" at the first dot. */
export function splitRef(ref: EndRef): [string, string] {
  const i = ref.indexOf('.');
  return i < 0 ? [ref, ''] : [ref.slice(0, i), ref.slice(i + 1)];
}

/** Text on a pad row: the pin name, or the print of the Osoyoo breakout board. */
export function padLabel(pad: Pad, options: Options): string {
  return options.connection === 'osoyoo' ? pad.breakoutLabel : pad.name;
}

/** A pin whose `only` names another option value is not on the chosen board. */
export function pinPresent(pin: Pin, options: Options): boolean {
  if (!pin.only) return true;
  return (Object.keys(pin.only) as Array<keyof Options>).every((k) => pin.only![k] === options[k]);
}

/** A pin whose name or position no source confirms. */
export function pinUnconfirmed(pin: Pin): boolean {
  return !pin.confirmed;
}

export function endName(ref: EndRef, ctx: NameCtx): string {
  const { lang, catalogue, options } = ctx;
  const [head, tail] = splitRef(ref);
  if (head === 'can') return `${t(lang, 'copro')} CAN ${tail} (${t(lang, 'screw')})`;
  if (head === 'pad') {
    const n = Number(tail);
    const pad = catalogue.pads.find((p) => p.n === n);
    if (!pad) return ref;
    const label = padLabel(pad, options);
    if (options.connection !== 'osoyoo') return `${t(lang, 'copro')} ${label} (${t(lang, 'pad')} ${n})`;
    if (pad.name !== 'GND') return `${t(lang, 'term')} ${label}`;
    const before = catalogue.pads.find((p) => p.n === n - 1);
    const after = catalogue.pads.find((p) => p.n === n + 1);
    if (!before || !after) return `${t(lang, 'term')} ${label}`;
    return `${t(lang, 'term')} ${label} (${t(lang, 'between', { a: padLabel(before, options), b: padLabel(after, options) })})`;
  }
  const part = catalogue.parts.find((p) => p.id === head);
  const pin = part?.pins.find((p) => p.id === tail);
  if (!part || !pin) return ref;
  const mark = pinUnconfirmed(pin) ? ` (${t(lang, 'padUnconfirmed')})` : '';
  return `${tx(lang, part.short)} ${tx(lang, pin.label)}${mark}`;
}

/** Target of a wire's second end: a named end, or the stub's text. */
export function targetName(wire: Wire, ctx: NameCtx): string {
  if (wire.b !== null) return endName(wire.b, ctx);
  return wire.to ? tx(ctx.lang, wire.to) : t(ctx.lang, 'nc');
}
