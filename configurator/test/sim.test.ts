// The heat calculation on the card of the motor simulator: its text in both
// languages, its markup, and the name, the parts list, the text and CSV copies
// and the PDF that follow the values.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection, SimSettings } from '../contract.ts';
import {
  CATALOGUE, DEFAULT_OPTIONS, DEFAULT_SIM, SIM_CELLS, SIM_OHMS, SIM_WATTS, SIM_WIRINGS, resolve, simHeat, withSim,
} from '../src/core.ts';
import { missing } from '../src/export/font.ts';
import { documentModel, fileName } from '../src/export/model.ts';
import type { Item } from '../src/export/model.ts';
import { optionsOf } from '../src/names.ts';
import { listCsv, listText, partsList, searchUrl } from '../src/view/buy.ts';
import type { View } from '../src/view/chooser.ts';
import { result } from '../src/view/result.ts';
import { haveAfterSim, packText, percent, simBlock, simPdfLines, simSaid, simText } from '../src/view/sim.ts';
import { NOW, fonts } from './export/helpers.ts';

const URL = 'https://example.invalid/#v=1&p=sim';
const LANGS: Lang[] = ['en', 'de'];

function view(sim: SimSettings, lang: Lang = 'en', part: Partial<Selection> = { owned: ['sim'] }): View {
  const selection: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS }, ...part };
  const catalogue = withSim(CATALOGUE, sim);
  const setup = resolve(catalogue, selection);
  return { lang, catalogue, setup, options: optionsOf(setup), selection, sim };
}

function grid(): SimSettings[] {
  const out: SimSettings[] = [];
  for (const cells of SIM_CELLS) for (const ohms of SIM_OHMS) for (const watts of SIM_WATTS) for (const wiring of SIM_WIRINGS) {
    out.push({ cells, ohms, watts, wiring });
  }
  return out;
}

const SIX: SimSettings = { cells: 6, ohms: 470, watts: 2, wiring: 'delta' };
const OWNER: SimSettings = { cells: 4, ohms: 450, watts: 2, wiring: 'star' };

test('check figure 470 Ω, 6S, delta: the text in English and German', () => {
  const en = simText('en', SIX);
  assert.equal(en.hot, 'Hottest resistor: 53.6 mA, 1.35 W, 67 % of the rating.');
  assert.equal(en.rest, 'Other 2 resistors: 26.8 mA, 0.338 W each.');
  assert.equal(en.level?.kind, 'hint');
  assert.ok(en.level?.text.startsWith('At 67 % of the rating. The rating holds only up to the ambient temperature'));
  assert.equal(en.basis, 'Worst case: one commutation step held at 25.2 V (6S at 4.2 V per cell). A sensorless ESC cannot commutate on resistors and can hold a step. The ESC also limits the current; this is not modelled.');
  assert.equal(en.star, null);
  const de = simText('de', SIX);
  assert.equal(de.hot, 'Heißester Widerstand: 53,6 mA, 1,35 W, 67 % der Belastbarkeit.');
  assert.equal(de.rest, 'Andere 2 Widerstände: je 26,8 mA, 0,338 W.');
  assert.ok(de.basis.includes('25,2 V (6S zu 4,2 V je Zelle)'));
  assert.ok(de.basis.endsWith('Auch der ESC begrenzt den Strom; das ist nicht modelliert.'));
});

test('check figure 450 Ω, 16.8 V, star: the text in English and German', () => {
  const en = simText('en', OWNER);
  assert.equal(en.hot, 'Hottest 2 resistors: 18.7 mA, 0.157 W each, 7 % of the rating.');
  assert.equal(en.rest, 'Third resistor: no current.');
  assert.equal(en.level, null);
  assert.ok(en.basis.includes('16.8 V (4S at 4.2 V per cell)'));
  assert.equal(en.star, 'Star: one end of each resistor goes to A, B or C; the other 3 ends are joined. The drawing shows delta.');
  const de = simText('de', OWNER);
  assert.equal(de.hot, 'Heißeste 2 Widerstände: je 18,7 mA, 0,157 W, 7 % der Belastbarkeit.');
  assert.equal(de.rest, 'Dritter Widerstand: kein Strom.');
});

test('the warning above the rating, the hint from half of it', () => {
  const over = simText('en', { cells: 6, ohms: 100, watts: 0.25, wiring: 'delta' });
  assert.equal(over.level?.kind, 'over');
  assert.equal(over.level?.text, 'Above the rating of 0.25 W. Choose a higher rating or a higher resistance.');
  assert.equal(simText('de', { cells: 6, ohms: 100, watts: 0.25, wiring: 'delta' }).level?.text, 'Über der Belastbarkeit von 0,25 W. Eine höhere Belastbarkeit oder einen höheren Widerstand wählen.');
  assert.equal(simText('en', DEFAULT_SIM).level, null);
  for (const sim of grid()) {
    const heat = simHeat(sim);
    const text = simText('en', sim);
    assert.equal(text.level?.kind ?? 'ok', heat.level, JSON.stringify(sim));
  }
});

test('the percent never contradicts the level: down below the rating, up above it', () => {
  const at = (cells: number, ohms: number, watts: number): number => percent(simHeat({ cells, ohms, watts, wiring: 'delta' }));
  assert.equal(at(5, 220, 2), 101); // 100.2 % is above the rating
  assert.equal(at(4, 560, 1), 50); // 50.4 %
  assert.equal(at(5, 180, 5), 49); // 49.0 %
  for (const sim of grid()) {
    const heat = simHeat(sim);
    const p = percent(heat);
    if (heat.level === 'over') assert.ok(p > 100, JSON.stringify(sim));
    else if (heat.level === 'hint') assert.ok(p >= 50 && p <= 100, JSON.stringify(sim));
    else assert.ok(p < 50, JSON.stringify(sim));
  }
});

test('the block: 4 labelled inputs with the values selected, the figures, the warning or the hint', () => {
  for (const lang of LANGS) {
    const html = simBlock(view(DEFAULT_SIM, lang));
    for (const key of ['cells', 'ohms', 'watts', 'wiring']) {
      assert.ok(html.includes(`<label for="sim-${key}">`), key);
      assert.ok(html.includes(`<select id="sim-${key}" data-sim="${key}">`), key);
    }
    assert.equal((html.match(/<option /g) ?? []).length, 6 + 21 + 6 + 2);
    assert.equal((html.match(/ selected>/g) ?? []).length, 4);
    assert.ok(html.includes(`<option value="4" selected>${packText(lang, 4)}</option>`));
    assert.ok(html.includes('<option value="470" selected>470 Ω</option>'));
    assert.ok(html.includes(`<option value="2" selected>2 W</option>`));
    assert.ok(html.includes(`<option value="delta" selected>${lang === 'de' ? 'Dreieck' : 'Delta'}</option>`));
    assert.ok(html.includes(lang === 'de' ? '<option value="1200">1,2 kΩ</option>' : '<option value="1200">1.2 kΩ</option>'));
    assert.ok(html.includes(lang === 'de' ? '<option value="0.25">0,25 W</option>' : '<option value="0.25">0.25 W</option>'));
    assert.ok(!html.includes('id="sim-over"') && !html.includes('id="sim-hint"') && !html.includes('id="sim-star"'));
  }
  const over = simBlock(view({ cells: 6, ohms: 100, watts: 1, wiring: 'delta' }));
  assert.ok(over.includes('<p class="msg err" id="sim-over">'));
  const hint = simBlock(view(SIX));
  assert.ok(hint.includes('<p class="msg note" id="sim-hint">'));
  assert.ok(simBlock(view(OWNER)).includes('<p class="hint" id="sim-star">'));
  assert.equal(simSaid(view(SIX)), `${simText('en', SIX).hot} ${simText('en', SIX).level?.text}`);
  assert.equal(simSaid(view(DEFAULT_SIM)), simText('en', DEFAULT_SIM).hot);
});

test('the card of the motor simulator carries the block; a setup without the part has none', () => {
  const sim: SimSettings = { cells: 3, ohms: 2200, watts: 0.5, wiring: 'star' };
  for (const lang of LANGS) {
    const html = result(view(sim, lang), URL, []);
    assert.equal((html.match(/id="sim"/g) ?? []).length, 1);
    const name = lang === 'de' ? 'Motorsimulator, selbst gebaut: 3 Widerstände im Stern, 2,2 kΩ, 0,5 W' : 'Motor simulator, self-built: 3 resistors in star, 2.2 kΩ, 0.5 W';
    assert.ok(html.includes(`<b>${name}</b>`), name);
    assert.ok(html.indexOf('id="sim"') > html.indexOf(`<b>${name}</b>`));
    assert.ok(html.indexOf('id="sim"') < html.indexOf('id="h-tested"'));
    const none = result(view(sim, lang, { functions: ['servo'] }), URL, []);
    assert.ok(!none.includes('id="sim"'));
    assert.ok(!none.includes('<select'));
  }
});

test('the parts list, the text and the CSV follow the values', () => {
  const sim: SimSettings = { cells: 5, ohms: 1000, watts: 3, wiring: 'delta' };
  // A part ticked under "Parts you own" is no row to buy: the phase tap brings the motor simulator.
  assert.equal(partsList(view(sim)).buy.filter((l) => l.part.id === 'sim').length, 0);
  for (const lang of LANGS) {
    const v = view(sim, lang, { functions: ['tap'] });
    const rows = partsList(v).buy.filter((l) => l.part.id === 'sim');
    assert.equal(rows.length, 1);
    const order = lang === 'de' ? 'Widerstand 1 kΩ, 3 W' : 'Resistor 1 kΩ, 3 W';
    const check = lang === 'de' ? 'Je 3 W belastbar, nicht weniger.' : 'Rated 3 W each, not less.';
    assert.equal(rows[0]!.order, order);
    assert.equal(rows[0]!.check, check);
    assert.equal(rows[0]!.qty, 3);
    assert.equal(rows[0]!.search, searchUrl(null, order));
    const text = listText(v, URL);
    assert.ok(text.includes(`3 × ${order}\n`), text);
    assert.ok(text.includes(check));
    assert.ok(!text.includes('470'));
    const csv = listCsv(v);
    assert.ok(csv.includes(`"3","","${order}","${check}","","${searchUrl(null, order)}"\r\n`), csv);
    assert.ok(!csv.includes('470'));
    const html = result(v, URL, []);
    assert.ok(html.includes(`<span class="qty">3 ×</span> ${order}</b>`));
  }
});

test('the PDF names the part for the values and carries the figures under it', () => {
  const sim: SimSettings = { cells: 6, ohms: 100, watts: 1, wiring: 'delta' };
  for (const lang of LANGS) {
    const v = view(sim, lang, { functions: ['tap'] });
    const doc = documentModel(v, URL, NOW);
    const items = (id: string): Item[] => doc.sections.find((s) => s.id === id)!.blocks.flatMap((b) => (b.kind === 'list' ? b.items : []));
    const name = lang === 'de' ? 'Motorsimulator, selbst gebaut: 3 Widerstände im Dreieck, 100 Ω, 1 W' : 'Motor simulator, self-built: 3 resistors in delta, 100 Ω, 1 W';
    const card = items('parts').find((i) => i.runs[0]?.text === name);
    assert.ok(card, name);
    const lines = card.lines.map((l) => l.text);
    for (const line of simPdfLines(v)) assert.ok(lines.includes(line.text), line.text);
    const text = simText(lang, sim);
    assert.ok(lines.includes(text.hot) && lines.includes(text.rest) && lines.includes(text.level!.text) && lines.includes(text.basis));
    assert.ok(lines.includes(`${lang === 'de' ? 'LiPo-Akku' : 'LiPo pack'}: ${packText(lang, 6)}`));
    const buy = items('buy').find((i) => i.runs[1]?.text === (lang === 'de' ? 'Widerstand 100 Ω, 1 W' : 'Resistor 100 Ω, 1 W'));
    assert.ok(buy);
    assert.ok(!JSON.stringify(doc).includes('470'), 'the PDF names 470 Ω');
  }
  // The file name names the values: other values, another name.
  assert.notEqual(fileName(view(sim)), fileName(view(DEFAULT_SIM)));
  assert.equal(fileName(view(DEFAULT_SIM)), fileName({ ...view(DEFAULT_SIM), sim: undefined }));
});

test('every text of the block has its glyphs in both fonts of the PDF', () => {
  const { regular, bold } = { regular: fonts().regular.font, bold: fonts().bold.font };
  const seen = new Set<string>();
  for (const lang of LANGS) {
    for (const sim of grid()) {
      for (const line of simPdfLines(view(sim, lang))) seen.add(line.text);
      seen.add(withSim(CATALOGUE, sim).parts.find((p) => p.id === 'sim')!.name[lang]);
    }
  }
  const text = [...seen].join('\n');
  assert.deepEqual(missing(regular, text), []);
  assert.deepEqual(missing(bold, text), []);
});

test('a change of the resistance or the rating leaves the "have it" tick of the resistor row; pack and wiring keep it', () => {
  const have = ['panel', 'sim', 'wire.dmm'];
  const d = DEFAULT_SIM;
  assert.deepEqual(haveAfterSim(have, d, { ...d, ohms: 100 }), ['panel', 'wire.dmm']);
  assert.deepEqual(haveAfterSim(have, d, { ...d, watts: 5 }), ['panel', 'wire.dmm']);
  assert.deepEqual(haveAfterSim(have, d, { ...d, cells: 6 }), have);
  assert.deepEqual(haveAfterSim(have, d, { ...d, wiring: 'star' }), have);
  assert.deepEqual(haveAfterSim(have, d, d), have);
  // A row of the part with a row id leaves as well; a part whose id only starts with "sim" stays.
  assert.deepEqual(haveAfterSim(['sim.x', 'simx', 'panel'], d, { ...d, ohms: 1000 }), ['simx', 'panel']);
  // The result is a new list: the list given stays as it is.
  const given = ['sim'];
  assert.notEqual(haveAfterSim(given, d, d), given);
  assert.deepEqual(given, ['sim']);
});
