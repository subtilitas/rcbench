// The heat per resistor of the motor simulator and the part named for its
// values: the formulas, the check figures of the owner, the thresholds, and
// the catalogue that the default values leave as it is.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { SimSettings } from '../../contract.ts';
import { CATALOGUE } from '../catalogue.ts';
import {
  CELL_VOLTS, DEFAULT_SIM, HINT_SHARE, SIM_CELLS, SIM_OHMS, SIM_PART, SIM_WATTS, SIM_WIRINGS, ohmsText, sameSim, sig, simHeat,
  simPart, validSim, voltsText, wattsText, withSim,
} from '../sim.ts';
import { checkBuy, checkString } from '../tools/check.ts';

const near = (actual: number, expected: number, where: string): void => {
  assert.ok(Math.abs(actual - expected) <= 1e-12 * Math.max(1, Math.abs(expected)), `${where}: ${actual} is not ${expected}`);
};

/** Every combination of the offered values: 6 × 21 × 6 × 2. */
function grid(): SimSettings[] {
  const out: SimSettings[] = [];
  for (const cells of SIM_CELLS) for (const ohms of SIM_OHMS) for (const watts of SIM_WATTS) for (const wiring of SIM_WIRINGS) {
    out.push({ cells, ohms, watts, wiring });
  }
  return out;
}

test('the offered values are 1S to 6S, the E12 values from 100 Ω to 4.7 kΩ and 6 ratings', () => {
  assert.deepEqual(SIM_CELLS, [1, 2, 3, 4, 5, 6]);
  assert.deepEqual(SIM_OHMS, [100, 120, 150, 180, 220, 270, 330, 390, 470, 560, 680, 820, 1000, 1200, 1500, 1800, 2200, 2700, 3300, 3900, 4700]);
  assert.deepEqual(SIM_WATTS, [0.25, 0.5, 1, 2, 3, 5]);
  assert.deepEqual(SIM_WIRINGS, ['delta', 'star']);
  assert.equal(CELL_VOLTS, 4.2);
  assert.deepEqual(DEFAULT_SIM, { cells: 4, ohms: 470, watts: 2, wiring: 'delta' });
  assert.equal(grid().length, 1512);
  assert.ok(grid().every(validSim));
});

test('check figure: 470 Ω at 6S (25.2 V) in delta gives 53.6 mA and 1.35 W', () => {
  const heat = simHeat({ cells: 6, ohms: 470, watts: 2, wiring: 'delta' });
  near(heat.volts, 25.2, 'volts');
  assert.equal(sig('en', heat.hot.amps * 1000), '53.6');
  assert.equal(sig('en', heat.hot.watts), '1.35');
  assert.equal(sig('de', heat.hot.watts), '1,35');
  assert.equal(heat.hot.count, 1);
  assert.equal(heat.level, 'hint');
});

test('check figure: 450 Ω at 16.8 V in star gives 18.7 mA and 0.157 W', () => {
  const heat = simHeat({ cells: 4, ohms: 450, watts: 2, wiring: 'star' });
  near(heat.volts, 16.8, 'volts');
  assert.equal(sig('en', heat.hot.amps * 1000), '18.7');
  assert.equal(sig('en', heat.hot.watts), '0.157');
  assert.equal(sig('de', heat.hot.watts), '0,157');
  assert.equal(heat.hot.count, 2);
  assert.equal(heat.level, 'ok');
});

test('delta and star at 1S and at 6S follow V/R, V²/R, V/(2R) and V²/(4R)', () => {
  for (const cells of [1, 6]) {
    for (const ohms of [100, 470, 4700]) {
      const v = cells * 4.2;
      const where = `${cells}S, ${ohms} Ω`;
      const delta = simHeat({ cells, ohms, watts: 2, wiring: 'delta' });
      near(delta.hot.amps, v / ohms, `${where} delta hot A`);
      near(delta.hot.watts, (v * v) / ohms, `${where} delta hot W`);
      assert.equal(delta.hot.count, 1);
      near(delta.rest.amps, v / (2 * ohms), `${where} delta rest A`);
      near(delta.rest.watts, (v * v) / (4 * ohms), `${where} delta rest W`);
      assert.equal(delta.rest.count, 2);
      const star = simHeat({ cells, ohms, watts: 2, wiring: 'star' });
      near(star.hot.amps, v / (2 * ohms), `${where} star hot A`);
      near(star.hot.watts, (v * v) / (4 * ohms), `${where} star hot W`);
      assert.equal(star.hot.count, 2);
      assert.equal(star.rest.amps, 0);
      assert.equal(star.rest.watts, 0);
      assert.equal(star.rest.count, 1);
    }
  }
  // The ends of the grid: the most and the least heat.
  assert.equal(sig('en', simHeat({ cells: 6, ohms: 100, watts: 5, wiring: 'delta' }).hot.watts), '6.35');
  assert.equal(sig('en', simHeat({ cells: 6, ohms: 100, watts: 5, wiring: 'delta' }).hot.amps * 1000), '252');
  assert.equal(sig('en', simHeat({ cells: 1, ohms: 4700, watts: 0.25, wiring: 'star' }).hot.watts), '0.000938');
  assert.equal(sig('en', simHeat({ cells: 1, ohms: 4700, watts: 0.25, wiring: 'star' }).hot.amps * 1000), '0.447');
});

test('the power of the 3 resistors adds up to V² over the resistance between the driven pair', () => {
  for (const sim of grid()) {
    const heat = simHeat(sim);
    const v = heat.volts;
    // Delta: R parallel to 2R, 2R/3. Star: 2R in series.
    const load = sim.wiring === 'delta' ? (2 * sim.ohms) / 3 : 2 * sim.ohms;
    near(heat.hot.watts * heat.hot.count + heat.rest.watts * heat.rest.count, (v * v) / load, JSON.stringify(sim));
    near(heat.hot.watts, heat.hot.amps * heat.hot.amps * sim.ohms, `${JSON.stringify(sim)} P = I²R`);
    assert.equal(heat.hot.count + heat.rest.count, 3);
    assert.ok(heat.hot.watts >= heat.rest.watts);
  }
});

test('the level: over above the rating, hint from half of it, ok below', () => {
  assert.equal(HINT_SHARE, 0.5);
  for (const sim of grid()) {
    const heat = simHeat(sim);
    near(heat.share, heat.hot.watts / sim.watts, 'share');
    assert.equal(heat.level, heat.share > 1 ? 'over' : heat.share >= 0.5 ? 'hint' : 'ok', JSON.stringify(sim));
  }
  // The grid points nearest the 2 thresholds.
  const level = (cells: number, ohms: number, watts: number, wiring: SimSettings['wiring']): string => simHeat({ cells, ohms, watts, wiring }).level;
  assert.equal(level(5, 220, 2, 'delta'), 'over'); // 100.2%
  assert.equal(level(4, 560, 0.5, 'delta'), 'over'); // 100.8%
  assert.equal(level(4, 560, 1, 'delta'), 'hint'); // 50.4%
  assert.equal(level(5, 180, 5, 'delta'), 'ok'); // 49.0%
  assert.equal(level(4, 470, 2, 'delta'), 'ok'); // 30.0%, the default
});

test('the default values: 0.601 W in the hottest resistor, 30 % of 2 W', () => {
  const heat = simHeat(DEFAULT_SIM);
  assert.equal(sig('en', heat.hot.watts), '0.601');
  assert.equal(sig('en', heat.hot.amps * 1000), '35.7');
});

test('numbers carry 3 significant figures, units and a decimal comma in German', () => {
  assert.equal(sig('en', 0), '0');
  assert.equal(sig('en', 0.6), '0.600');
  assert.equal(sig('de', 0.6), '0,600');
  assert.equal(voltsText('en', 21), '21.0 V');
  assert.equal(voltsText('de', 16.8), '16,8 V');
  assert.equal(ohmsText('en', 470), '470 Ω');
  assert.equal(ohmsText('en', 1000), '1 kΩ');
  assert.equal(ohmsText('en', 4700), '4.7 kΩ');
  assert.equal(ohmsText('de', 1200), '1,2 kΩ');
  assert.equal(wattsText('en', 0.25), '0.25 W');
  assert.equal(wattsText('de', 0.5), '0,5 W');
  assert.equal(wattsText('de', 5), '5 W');
});

test('the default values leave the catalogue as it is', () => {
  assert.equal(withSim(CATALOGUE, DEFAULT_SIM), CATALOGUE);
  const sim = CATALOGUE.parts.find((p) => p.id === SIM_PART)!;
  assert.deepEqual(simPart(sim, DEFAULT_SIM), sim);
  assert.ok(sameSim({ ...DEFAULT_SIM }, DEFAULT_SIM));
  assert.ok(!sameSim({ ...DEFAULT_SIM, wiring: 'star' }, DEFAULT_SIM));
});

test('other values name the part and its row; every other record stays the same object', () => {
  const sim: SimSettings = { cells: 2, ohms: 1200, watts: 0.25, wiring: 'star' };
  const c = withSim(CATALOGUE, sim);
  assert.notEqual(c, CATALOGUE);
  const part = c.parts.find((p) => p.id === SIM_PART)!;
  assert.deepEqual(part.name, {
    en: 'Motor simulator, self-built: 3 resistors in star, 1.2 kΩ, 0.25 W',
    de: 'Motorsimulator, selbst gebaut: 3 Widerstände im Stern, 1,2 kΩ, 0,25 W',
  });
  assert.equal(part.buy.length, 1);
  assert.deepEqual(part.buy[0]!.order, { en: 'Resistor 1.2 kΩ, 0.25 W', de: 'Widerstand 1,2 kΩ, 0,25 W' });
  assert.deepEqual(part.buy[0]!.check, { en: 'Rated 0.25 W each, not less.', de: 'Je 0,25 W belastbar, nicht weniger.' });
  assert.equal(part.buy[0]!.qty, 3);
  assert.equal(part.buy[0]!.maker, null);
  assert.equal(part.short, CATALOGUE.parts.find((p) => p.id === SIM_PART)!.short);
  for (const [i, p] of c.parts.entries()) if (p.id !== SIM_PART) assert.equal(p, CATALOGUE.parts[i]);
  assert.equal(c.wires, CATALOGUE.wires);
  assert.equal(c.functions, CATALOGUE.functions);
});

test('every name and row the values give passes the text rules and the parts list check', () => {
  for (const sim of grid()) {
    const part = simPart(CATALOGUE.parts.find((p) => p.id === SIM_PART)!, sim);
    for (const text of [part.name, part.buy[0]!.order, part.buy[0]!.check]) {
      assert.deepEqual(checkString('sim', text.en, false), [], text.en);
      assert.deepEqual(checkString('sim', text.de, false), [], text.de);
    }
  }
  const findings = checkBuy(withSim(CATALOGUE, { cells: 6, ohms: 4700, watts: 5, wiring: 'star' })).filter((f) => f.level === 'error');
  assert.deepEqual(findings, []);
});
