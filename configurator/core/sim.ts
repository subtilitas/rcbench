// The motor simulator: 3 resistors on the 3 phase wires of an ESC, joined in
// delta or in star. Pure, like the resolver.
//
// Load case: one commutation step held at the full voltage V of the pack. A
// sensorless ESC cannot commutate on resistors, so it can hold a step. The
// current limit of the ESC is not modelled.
//
// - Delta: the resistor across the driven pair carries V/R and takes V²/R.
//   The other 2 are in series across V: V/(2R) and V²/(4R) each.
// - Star: 2 resistors in series across V carry V/(2R) and take V²/(4R) each.
//   The third carries no current.

import type { Catalogue, Lang, Part, SimSettings, Text } from '../contract.ts';

/** Voltage of a full LiPo cell in V. */
export const CELL_VOLTS = 4.2;
/** The LiPo cell counts the visitor chooses from. */
export const SIM_CELLS: readonly number[] = Object.freeze([1, 2, 3, 4, 5, 6]);
/** The resistances in Ω: the E12 values from 100 Ω to 4.7 kΩ. */
export const SIM_OHMS: readonly number[] = Object.freeze([
  100, 120, 150, 180, 220, 270, 330, 390, 470, 560, 680, 820,
  1000, 1200, 1500, 1800, 2200, 2700, 3300, 3900, 4700,
]);
/** The power ratings in W. */
export const SIM_WATTS: readonly number[] = Object.freeze([0.25, 0.5, 1, 2, 3, 5]);
export const SIM_WIRINGS: ReadonlyArray<SimSettings['wiring']> = Object.freeze(['delta', 'star'] as const);

/**
 * 470 Ω, 2 W and delta, as the catalogue builds the part. 4 cells: the 1 ESC
 * reading of the docs ran on a 4S pack, and ESC power B, the PD mini, gives
 * at most 20 V, which 4S (16.8 V) stays under and 5S (21.0 V) exceeds.
 */
export const DEFAULT_SIM: Readonly<SimSettings> = Object.freeze({ cells: 4, ohms: 470, watts: 2, wiring: 'delta' });

/** Share of the rating from which the page shows the derating hint. */
export const HINT_SHARE = 0.5;

/** The id of the part the values apply to. */
export const SIM_PART = 'sim';

/** Current in A and power in W of each of `count` resistors. */
export type Load = { amps: number; watts: number; count: number };

/**
 * - `volts`: the pack voltage in V.
 * - `hot`: the hottest resistors. `rest`: the others.
 * - `share`: power of a hot resistor over the rating.
 * - `level`: `over` above the rating, `hint` from HINT_SHARE of it, else `ok`.
 */
export type SimHeat = { volts: number; hot: Load; rest: Load; share: number; level: 'ok' | 'hint' | 'over' };

/** The values are 1 of the offered ones each. */
export function validSim(sim: SimSettings): boolean {
  return SIM_CELLS.includes(sim.cells) && SIM_OHMS.includes(sim.ohms) && SIM_WATTS.includes(sim.watts)
    && SIM_WIRINGS.includes(sim.wiring);
}

export const sameSim = (a: SimSettings, b: SimSettings): boolean =>
  a.cells === b.cells && a.ohms === b.ohms && a.watts === b.watts && a.wiring === b.wiring;

/** The current and the power of each resistor in the worst case. */
export function simHeat(sim: SimSettings): SimHeat {
  const volts = sim.cells * CELL_VOLTS;
  const r = sim.ohms;
  const half = { amps: volts / (2 * r), watts: (volts * volts) / (4 * r) };
  const hot: Load = sim.wiring === 'delta' ? { amps: volts / r, watts: (volts * volts) / r, count: 1 } : { ...half, count: 2 };
  const rest: Load = sim.wiring === 'delta' ? { ...half, count: 2 } : { amps: 0, watts: 0, count: 1 };
  const share = hot.watts / sim.watts;
  return { volts, hot, rest, share, level: share > 1 ? 'over' : share >= HINT_SHARE ? 'hint' : 'ok' };
}

const comma = (lang: Lang, s: string): string => (lang === 'de' ? s.replace('.', ',') : s);

/** 3 significant figures, the trailing zeros kept: "53.6", "0.601", "252". 0 reads "0". */
export function sig(lang: Lang, value: number): string {
  return comma(lang, value === 0 ? '0' : value.toPrecision(3));
}

/** "16.8 V", "21.0 V". */
export const voltsText = (lang: Lang, volts: number): string => `${comma(lang, volts.toFixed(1))} V`;
/** "470 Ω", "1.2 kΩ". */
export const ohmsText = (lang: Lang, ohms: number): string =>
  (ohms < 1000 ? `${ohms} Ω` : `${comma(lang, String(ohms / 1000))} kΩ`);
/** "0.25 W", "2 W". */
export const wattsText = (lang: Lang, watts: number): string => `${comma(lang, String(watts))} W`;

const both = (f: (lang: Lang) => string): Text => ({ en: f('en'), de: f('de') });

/** The name of the part for the values. */
export function simName(sim: SimSettings): Text {
  return both((lang) => (lang === 'de'
    ? `Motorsimulator, selbst gebaut: 3 Widerstände im ${sim.wiring === 'delta' ? 'Dreieck' : 'Stern'}, ${ohmsText(lang, sim.ohms)}, ${wattsText(lang, sim.watts)}`
    : `Motor simulator, self-built: 3 resistors in ${sim.wiring}, ${ohmsText(lang, sim.ohms)}, ${wattsText(lang, sim.watts)}`));
}

/** The part with its name and its rows of the parts list for the values. Another part is returned as it is. */
export function simPart(part: Part, sim: SimSettings): Part {
  if (part.id !== SIM_PART) return part;
  return {
    ...part,
    name: simName(sim),
    buy: part.buy.map((row) => ({
      ...row,
      order: both((lang) => `${lang === 'de' ? 'Widerstand' : 'Resistor'} ${ohmsText(lang, sim.ohms)}, ${wattsText(lang, sim.watts)}`),
      check: both((lang) => (lang === 'de'
        ? `Je ${wattsText(lang, sim.watts)} belastbar, nicht weniger.`
        : `Rated ${wattsText(lang, sim.watts)} each, not less.`)),
    })),
  };
}

/**
 * The catalogue with the motor simulator named and listed for the values.
 * Every other record is the same object. The default values give the
 * catalogue itself.
 */
export function withSim(catalogue: Catalogue, sim: SimSettings): Catalogue {
  if (sameSim(sim, DEFAULT_SIM)) return catalogue;
  return { ...catalogue, parts: catalogue.parts.map((p) => simPart(p, sim)) };
}
