import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { GEO, SPEC_GEO, layout } from '../src/draw/layout.ts';
import { drawing } from '../src/draw/svg.ts';
import { describe, findings, sweep } from './geometry.ts';

const samples = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS });
const base: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } };
const full: Selection = {
  functions: CATALOGUE.functions.map((f) => f.id),
  owned: CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id),
  options: { ...DEFAULT_OPTIONS },
};

/** Findings per kind, with wire coordinates removed, and the first selection that shows each. */
function kinds(geo: typeof GEO, lang: Lang): Map<string, string> {
  const out = new Map<string, string>();
  for (const s of samples) {
    for (const f of findings(layout(s.setup, CATALOGUE, lang, geo), s.setup)) {
      const kind = f.replace(/ at [\d.,]+/g, '').replace(/: [xy]=.*$/, '');
      if (!out.has(kind)) out.set(kind, `${f} [${describe(s.selection)}]`);
    }
  }
  return out;
}

test('the sweep covers every wire and every drawn part of the catalogue', () => {
  assert.ok(samples.length > 100);
  const wires = new Set(samples.flatMap((s) => s.setup.wires.map((w) => w.n)));
  assert.deepEqual([...wires].sort((a, b) => a - b), CATALOGUE.wires.map((w) => w.n).sort((a, b) => a - b));
  const parts = new Set(samples.flatMap((s) => s.setup.parts.map((p) => p.part.id)));
  for (const p of CATALOGUE.parts) assert.ok(parts.has(p.id), `${p.id} is in no sample`);
});

test('every drawing of the sweep holds the geometry rules, in both languages', () => {
  for (const lang of ['en', 'de'] as Lang[]) {
    const found = kinds(GEO, lang);
    assert.deepEqual([...found.values()], [], `${found.size} kinds of findings (${lang})`);
  }
});

test('the lane table of the specification collides in the places GEO changes', () => {
  const found = [...kinds(SPEC_GEO, 'en').keys()];
  const has = (text: string): boolean => found.some((f) => f.startsWith(text));
  assert.ok(has('wires 12 and 47 share a line'), found.join('\n'));
  assert.ok(has('wires 30 and 47 share a line'));
  assert.ok(has('wires 33 and 34 share a line'));
  assert.ok(has('cross mark of wire 31 lies on wire 47'));
  assert.ok(has('pill 22 covers wire 21'));
});

test('a wire number names the same connection in every selection', () => {
  const ends = new Map<number, string>();
  for (const s of samples) {
    for (const w of layout(s.setup, CATALOGUE, 'en').wires) {
      const key = `${w.a}>${w.b ?? 'open'}`;
      const seen = ends.get(w.n);
      if (seen === undefined) ends.set(w.n, key);
      else assert.equal(key, seen, `wire ${w.n} in ${describe(s.selection)}`);
    }
  }
  for (const w of CATALOGUE.wires) assert.equal(ends.get(w.n), `${w.a}>${w.b ?? 'open'}`, `wire ${w.n}`);
});

test('the width is 970 px and the height holds the coprocessor in every drawing', () => {
  for (const s of samples) {
    const l = layout(s.setup, CATALOGUE, 'en');
    assert.equal(l.width, 970);
    assert.ok(l.height >= l.copro.y + l.copro.h + GEO.bottom, describe(s.selection));
    assert.ok(l.height <= 1400, `${l.height} px in ${describe(s.selection)}`);
  }
});

test('the base setup draws its wires with one path, one title and its pills each', () => {
  const setup = resolve(CATALOGUE, base);
  const l = layout(setup, CATALOGUE, 'en');
  assert.deepEqual(findings(l, setup), []);
  assert.equal(l.wires.length, setup.wires.length);
  const svg = drawing(setup, CATALOGUE, 'en');
  assert.equal((svg.match(/<path class="w c-[a-z]+" /g) ?? []).length, setup.wires.length);
  assert.equal((svg.match(/<title>/g) ?? []).length, setup.wires.length);
  assert.ok(svg.includes(`viewBox="0 0 970 ${l.height}"`));
  assert.ok(svg.includes('role="img"'));
  assert.ok(/aria-label="[^"]*\d+[^"]*"/.test(svg));
  for (const w of l.wires) assert.ok(new RegExp(`^${w.n}: `).test(w.title), w.title);
});

test('the full selection draws without findings in every option set and language', () => {
  for (const connection of ['pads', 'osoyoo'] as const) {
    for (const ina228Board of ['matek', 'xt90'] as const) {
      for (const lang of ['en', 'de'] as Lang[]) {
        const selection = { ...full, options: { ...full.options, connection, ina228Board } };
        const setup = resolve(CATALOGUE, selection);
        assert.deepEqual(findings(layout(setup, CATALOGUE, lang), setup), [], `${describe(selection)} ${lang}`);
      }
    }
  }
});

test('the drawing carries no markup from data text', () => {
  const setup = resolve(CATALOGUE, full);
  const svg = drawing(setup, CATALOGUE, 'de');
  assert.ok(!/<script|onload=|javascript:/i.test(svg));
  assert.ok(!/style="/.test(svg));
});

test('an unknown end is reported as skipped, not drawn', () => {
  const setup = resolve(CATALOGUE, base);
  const first = setup.wires[0];
  assert.ok(first);
  const broken = { ...setup, wires: [{ ...first, a: 'nopart.NOPIN' }, ...setup.wires.slice(1)] };
  const l = layout(broken, CATALOGUE, 'en');
  assert.deepEqual(l.skipped, [first.n]);
  assert.ok(!l.wires.some((w) => w.n === first.n));
});

test('the T-plug is a module with the UART wires of the PD mini', () => {
  const setup = resolve(CATALOGUE, { ...base, owned: ['pdmini'] });
  const l = layout(setup, CATALOGUE, 'en');
  const plug = l.modules.find((m) => m.id === 'usbtap');
  assert.ok(plug, 'the PD mini brings the T-plug');
  assert.equal(plug.dashed, false);
  assert.deepEqual(plug.pins.map((p) => [p.id, p.used]), [['G', true], ['DP', true], ['DM', true], ['V', false]]);
  const ends = (n: number): string[] => l.wires.filter((w) => w.n === n).map((w) => `${w.a} ${w.b}`);
  assert.deepEqual(ends(16), ['pad.10 usbtap.DM']);
  assert.deepEqual(ends(17), ['usbtap.DP pad.9']);
  assert.deepEqual(ends(18), ['pad.18 usbtap.G']);
  assert.ok(!l.modules.find((m) => m.id === 'pdmini')?.pins.some((p) => /^D/.test(p.id)));
  for (const p of CATALOGUE.parts) assert.ok(p.role !== null, `${p.id} states its role`);
});

test('the T-plug without the PD mini is drawn without a wire', () => {
  const setup = resolve(CATALOGUE, { ...base, owned: ['usbtap'] });
  const l = layout(setup, CATALOGUE, 'de');
  assert.deepEqual(findings(l, setup), []);
  const plug = l.modules.find((m) => m.id === 'usbtap');
  assert.ok(plug);
  assert.ok(plug.pins.every((p) => !p.used));
});

test('the INA228 module with XT90 plugs is wired as the MATEK board, its pad names marked', () => {
  const draw = (ina228Board: 'matek' | 'xt90') => {
    const setup = resolve(CATALOGUE, { ...base, owned: ['ina228'], options: { ...DEFAULT_OPTIONS, ina228Board } });
    return layout(setup, CATALOGUE, 'en');
  };
  const [matek, xt90] = [draw('matek'), draw('xt90')];
  assert.deepEqual(xt90.wires.map((w) => [w.n, w.points]), matek.wires.map((w) => [w.n, w.points]));
  for (const n of [41, 42, 43, 44]) assert.ok(xt90.wires.some((w) => w.n === n), `wire ${n}`);
  const labels = (l: typeof xt90): string[] => l.modules.find((m) => m.id === 'ina228')!.pins.map((p) => p.label);
  assert.deepEqual(labels(matek), ['5V', 'G', 'SDA', 'SCL', 'BAT+', 'ESC+']);
  assert.deepEqual(labels(xt90), ['5V ?', 'G ?', 'SDA ?', 'SCL ?', 'BAT+', 'ESC+']);
});

test('the AS5600 draws the GPO strap and the DIR strap as stubs', () => {
  const setup = resolve(CATALOGUE, { ...base, owned: ['as5600'] });
  const l = layout(setup, CATALOGUE, 'en');
  for (const [n, pin] of [[37, 'as5600.DIR'], [54, 'as5600.GPO']] as const) {
    const w = l.wires.find((x) => x.n === n);
    assert.ok(w, `wire ${n}`);
    assert.equal(w.a, pin);
    assert.equal(w.b, null);
    assert.ok(w.cross);
    assert.equal(l.pills.filter((p) => p.n === n).length, 1);
  }
  assert.ok(l.wires.find((w) => w.n === 54)!.title.includes('GND'));
});
