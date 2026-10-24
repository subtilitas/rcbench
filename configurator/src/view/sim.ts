// The heat calculation on the card of the motor simulator: 4 inputs, the
// current and the power of the hottest resistor and of the others, the
// warning above the rating, the derating hint and the load case. The page,
// the PDF and the live region take their text from here.

import type { Lang, SimSettings } from '../../contract.ts';
import {
  CELL_VOLTS, DEFAULT_SIM, SIM_CELLS, SIM_OHMS, SIM_PART, SIM_WATTS, SIM_WIRINGS, ohmsText, sig, simHeat, voltsText, wattsText,
} from '../core.ts';
import type { SimHeat } from '../core.ts';
import { esc, t } from '../i18n.ts';
import type { Key } from '../i18n.ts';
import type { View } from './chooser.ts';

/** The values of the view, else the default. */
export const simOf = (view: View): SimSettings => view.sim ?? DEFAULT_SIM;

/**
 * Power over the rating in whole percent. Rounded down, and up above the
 * rating: the figure never contradicts the warning or the hint.
 */
export function percent(heat: SimHeat): number {
  const x = heat.share * 100;
  return heat.level === 'over' ? Math.ceil(x) : Math.floor(x);
}

/** "4S, 16.8V". */
export const packText = (lang: Lang, cells: number): string => `${cells}S, ${voltsText(lang, cells * CELL_VOLTS)}`;

const WIRING: Record<SimSettings['wiring'], Key> = { delta: 'simDelta', star: 'simStar' };

export type SimText = {
  /** The hottest resistor or resistors. */
  hot: string;
  /** The other resistors. */
  rest: string;
  /** The warning above the rating or the derating hint, else null. */
  level: { kind: 'over' | 'hint'; text: string } | null;
  /** The load case, with the sentence on the ESC. */
  basis: string;
  /** How a star is joined, for a star only. */
  star: string | null;
};

/** The text of the calculation for the values. */
export function simText(lang: Lang, sim: SimSettings): SimText {
  const heat = simHeat(sim);
  const ma = (amps: number): string => sig(lang, amps * 1000);
  const w = (watts: number): string => sig(lang, watts);
  const p = percent(heat);
  const delta = sim.wiring === 'delta';
  const hot = t(lang, delta ? 'simHot1' : 'simHot2', { a: ma(heat.hot.amps), w: w(heat.hot.watts), p });
  const rest = delta ? t(lang, 'simRest2', { a: ma(heat.rest.amps), w: w(heat.rest.watts) }) : t(lang, 'simRest1');
  const level = heat.level === 'over' ? { kind: 'over' as const, text: t(lang, 'simOver', { r: wattsText(lang, sim.watts) }) }
    : heat.level === 'hint' ? { kind: 'hint' as const, text: t(lang, 'simHint', { p }) }
    : null;
  const basis = t(lang, 'simBasis', { v: voltsText(lang, heat.volts), n: sim.cells });
  return { hot, rest, level, basis, star: delta ? null : t(lang, 'simStarHow') };
}

function select(lang: Lang, key: keyof SimSettings, label: Key, values: ReadonlyArray<number | string>, chosen: number | string, name: (v: number | string) => string): string {
  return `<div><label for="sim-${key}">${esc(t(lang, label))}</label>`
    + `<select id="sim-${key}" data-sim="${key}">`
    + values.map((v) => `<option value="${esc(String(v))}"${v === chosen ? ' selected' : ''}>${esc(name(v))}</option>`).join('')
    + '</select></div>';
}

/** The inputs and the figures on the card of the motor simulator. */
export function simBlock(view: View): string {
  const { lang } = view;
  const sim = simOf(view);
  const text = simText(lang, sim);
  return '<div class="sim" id="sim">'
    + `<fieldset><legend>${esc(t(lang, 'simHead'))}</legend><div class="simin">`
    + select(lang, 'cells', 'simCells', SIM_CELLS, sim.cells, (v) => packText(lang, Number(v)))
    + select(lang, 'ohms', 'simOhms', SIM_OHMS, sim.ohms, (v) => ohmsText(lang, Number(v)))
    + select(lang, 'watts', 'simWatts', SIM_WATTS, sim.watts, (v) => wattsText(lang, Number(v)))
    + select(lang, 'wiring', 'simWiring', SIM_WIRINGS, sim.wiring, (v) => t(lang, WIRING[v as SimSettings['wiring']]))
    + '</div></fieldset>'
    + `<p id="sim-hot"><strong>${esc(text.hot)}</strong></p>`
    + `<p id="sim-rest">${esc(text.rest)}</p>`
    + (text.level ? `<p class="msg ${text.level.kind === 'over' ? 'err' : 'note'}" id="sim-${text.level.kind}">${esc(text.level.text)}</p>` : '')
    + `<p class="hint" id="sim-basis">${esc(text.basis)}</p>`
    + (text.star ? `<p class="hint" id="sim-star">${esc(text.star)}</p>` : '')
    + '</div>';
}

/**
 * The "have it" ticks after a change of the values. The row of the motor
 * simulator names the resistance and the rating, its token does not: a
 * change of either leaves the tick of that row, as the resistors the
 * visitor has are not the ones the row now names. Pack and wiring leave the
 * row as it is.
 */
export function haveAfterSim(have: readonly string[], before: SimSettings, after: SimSettings): string[] {
  if (before.ohms === after.ohms && before.watts === after.watts) return [...have];
  return have.filter((token) => token !== SIM_PART && !token.startsWith(`${SIM_PART}.`));
}

/** What the live region says after a change of a value: the hottest resistor and the warning or the hint. */
export function simSaid(view: View): string {
  const text = simText(view.lang, simOf(view));
  return text.level ? `${text.hot} ${text.level.text}` : text.hot;
}

/** The lines of the calculation in the PDF: pack, figures, warning or hint, load case, star. */
export function simPdfLines(view: View): Array<{ text: string; muted: boolean }> {
  const { lang } = view;
  const sim = simOf(view);
  const text = simText(lang, sim);
  const lines = [
    { text: `${t(lang, 'simCells')}: ${packText(lang, sim.cells)}`, muted: false },
    { text: text.hot, muted: false },
    { text: text.rest, muted: false },
  ];
  if (text.level) lines.push({ text: text.level.text, muted: false });
  lines.push({ text: text.basis, muted: true });
  if (text.star) lines.push({ text: text.star, muted: true });
  return lines;
}
