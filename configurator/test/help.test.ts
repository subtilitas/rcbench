// The help bubble of each tick in the chooser: one per function with a tick
// and per part under "Parts you own", in both languages, none for anything
// else; the lines after the first restate the resolver.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Catalogue, Lang, Options, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, FUNCTION_HELP, alwaysOn, resolve } from '../src/core.ts';
import { t } from '../src/i18n.ts';
import { optionsOf } from '../src/names.ts';
import { chooser, variantShort } from '../src/view/chooser.ts';
import type { View } from '../src/view/chooser.ts';
import { functionHelp, helpId, partHelp } from '../src/view/help.ts';

const LANGS: Lang[] = ['en', 'de'];
const TICKED = CATALOGUE.functions.filter((f) => !alwaysOn(CATALOGUE, f)).map((f) => f.id);
const ON_BASE = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
const OWNABLE = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);

function view(part: Partial<Selection>, lang: Lang = 'en', catalogue: Catalogue = CATALOGUE): View {
  const selection: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS }, ...part };
  const setup = resolve(catalogue, selection);
  return { lang, catalogue, setup, options: optionsOf(setup), selection };
}

const short = (id: string, lang: Lang): string => CATALOGUE.parts.find((p) => p.id === id)!.short[lang];
const text = (html: string): string => html.replace(/<[^>]+>/g, ' ').replace(/&quot;/g, '"').replace(/&#39;/g, "'")
  .replace(/&amp;/g, '&').replace(/\s+/g, ' ').trim();

/** The text of the bubble `id` in chooser markup, or null without one. */
function bubbleText(html: string, id: string): string | null {
  const start = html.indexOf(`<div class="bubble" id="${id}"`);
  if (start < 0) return null;
  return text(html.slice(start, html.indexOf('</div>', start)));
}

/** The parts a selection adds to the base setup, straight from the resolver: bench parts and test objects. */
function added(selection: Pick<Selection, 'functions' | 'owned'>, options: Options, self: string | null = null) {
  const base = new Set(resolve(CATALOGUE, { functions: [], owned: [], options }).parts.map((r) => r.part.id));
  const parts = resolve(CATALOGUE, { ...selection, options }).parts.filter((r) => !base.has(r.part.id) && r.part.id !== self);
  return {
    bench: parts.filter((r) => r.part.testObject !== true).map((r) => r.part.id),
    objects: parts.filter((r) => r.part.testObject === true).map((r) => r.part.id),
  };
}

test('every function with a tick and every part under "Parts you own" has a bubble in English and German', () => {
  assert.equal(TICKED.length, 12);
  assert.deepEqual(Object.keys(FUNCTION_HELP).sort(), [...TICKED].sort());
  for (const connection of ['pads', 'osoyoo'] as const) {
    for (const lang of LANGS) {
      const v = view({ options: { ...DEFAULT_OPTIONS, connection } }, lang);
      const html = chooser(v, true);
      for (const id of TICKED) {
        const said = bubbleText(html, helpId('f', id));
        assert.ok(said !== null && said.length > 40, `${lang} ${id}: ${said}`);
        assert.ok(said.startsWith(text(FUNCTION_HELP[id]![lang])), `${lang} ${id}`);
        assert.match(html, new RegExp(`<input aria-describedby="${helpId('f', id)}" type="checkbox" id="f-${id}"`));
      }
      for (const id of OWNABLE) {
        const part = CATALOGUE.parts.find((p) => p.id === id)!;
        const said = bubbleText(html, helpId('p', id));
        assert.ok(said !== null && said.length > 20, `${lang} ${id}: ${said}`);
        // The first line is the role of the data file, not a copy of it.
        assert.ok(part.role !== null, id);
        assert.ok(said.startsWith(text(part.role[lang])), `${lang} ${id}`);
        assert.match(html, new RegExp(`<input aria-describedby="${helpId('p', id)}" type="checkbox" id="p-${id}"`));
      }
      // One bubble per tick, no other.
      assert.equal((html.match(/<div class="bubble"/g) ?? []).length, TICKED.length + OWNABLE.length);
      assert.equal((html.match(/<span class="info" aria-hidden="true"><\/span>/g) ?? []).length, TICKED.length + OWNABLE.length);
    }
  }
});

test('the functions of the base setup, the test objects and the parts without a tick have no bubble', () => {
  for (const lang of LANGS) {
    const v = view({ functions: [...TICKED], owned: [...OWNABLE] }, lang);
    const html = chooser(v, true);
    for (const id of ON_BASE) {
      assert.equal(functionHelp(v, id), null, id);
      assert.ok(!html.includes(`id="${helpId('f', id)}"`), id);
    }
    for (const part of CATALOGUE.parts.filter((p) => !p.selectable)) {
      assert.equal(partHelp(v, part.id), null, part.id);
      assert.ok(!html.includes(`id="${helpId('p', part.id)}"`), part.id);
    }
    assert.equal(functionHelp(v, 'nosuch'), null);
    assert.equal(partHelp(v, 'nosuch'), null);
  }
  // A part that leaves "Parts you own" loses its bubble: the T-plug without a tick.
  const withoutTick: Catalogue = { ...CATALOGUE, parts: CATALOGUE.parts.map((p) => (p.id === 'usbtap' ? { ...p, selectable: false } : p)) };
  const v = view({}, 'en', withoutTick);
  assert.equal(partHelp(v, 'usbtap'), null);
  assert.ok(!chooser(v, true).includes(helpId('p', 'usbtap')));
});

test('the needs of a function are the parts the resolver adds to the base setup for it alone', () => {
  const samples: Array<[string, Options]> = [
    ['servotest', DEFAULT_OPTIONS],
    ['angle', DEFAULT_OPTIONS],
    ['escpow', DEFAULT_OPTIONS],
    ['escpow', { ...DEFAULT_OPTIONS, escPower: 'pd' }],
    ['tap', DEFAULT_OPTIONS],
    ['servo', { ...DEFAULT_OPTIONS, servoPower: 'bench' }],
    ['logs', DEFAULT_OPTIONS],
  ];
  for (const [id, options] of samples) {
    for (const lang of LANGS) {
      const help = functionHelp(view({ options }, lang), id)!;
      const { bench, objects } = added({ functions: [id], owned: [] }, options);
      const needs = help.needs.find((line) => line.startsWith(t(lang, 'gNeeds'))) ?? '';
      // Every bench part in catalogue order, with its condition in brackets where a rule adds it.
      const names = needs.slice(t(lang, 'gNeeds').length, -1).split(', ').map((x) => x.replace(/ \(.*\)$/, ''));
      assert.deepEqual(bench.length === 0 ? [''] : names, bench.length === 0 ? [''] : bench.map((p) => short(p, lang)), `${id} ${lang}`);
      assert.deepEqual(help.needs.filter((line) => line === t(lang, 'stObj', { x: objects.map((p) => short(p, lang)).join(', ') })).length, objects.length > 0 ? 1 : 0);
    }
  }
  // The words, once in full.
  const en = (id: string, options: Options = DEFAULT_OPTIONS): string[] => functionHelp(view({ options }), id)!.needs;
  assert.deepEqual(en('servotest'), ['Needs: PD mini, PD charger, T-plug.', 'Connects: Servo.']);
  assert.deepEqual(en('escpow'), ['Needs: ESC battery, 2 × 2.2 kΩ (only without INA3221), INA228.', 'Connects: ESC.']);
  assert.deepEqual(en('servo'), ['Needs: Servo supply (only without PD mini).', 'Connects: Servo.']);
  assert.deepEqual(en('servo', { ...DEFAULT_OPTIONS, servoPower: 'bench' }), ['Connects: Servo.']);
  assert.deepEqual(en('logs'), ['Needs: microSD card.']);
  assert.deepEqual(functionHelp(view({}, 'de'), 'escpow')!.needs, ['Braucht: ESC-Akku, 2 × 2,2 kΩ (nur ohne INA3221), INA228.', 'Schließt an: ESC.']);
});

test('a function that sets a variant names it', () => {
  for (const lang of LANGS) {
    for (const fn of CATALOGUE.functions.filter((f) => TICKED.includes(f.id))) {
      const help = functionHelp(view({}, lang), fn.id)!;
      const forced = Object.entries(fn.forces ?? {}).map(([key, value]) => variantShort(lang, key as keyof Options, value as string));
      const line = help.needs.filter((x) => x.startsWith(t(lang, 'hVar', { x: '' }).slice(0, -1)));
      assert.deepEqual(line, forced.length > 0 ? [t(lang, 'hVar', { x: forced.join(', ') })] : [], fn.id);
    }
  }
  assert.ok(functionHelp(view({}), 'stick')!.needs.includes('Variant: ESC power B.'));
  assert.ok(functionHelp(view({}), 'servoprog')!.needs.includes('Variant: Servo signal on the programming port, GP13.'));
  assert.ok(functionHelp(view({}, 'de'), 'stick')!.needs.includes('Variante: ESC-Versorgung B.'));
});

test('the needs of a part are the parts it brings and the wires of its supply in the variant of the page', () => {
  for (const lang of LANGS) {
    for (const id of OWNABLE) {
      const help = partHelp(view({}, lang), id)!;
      const { bench } = added({ functions: [], owned: [id] }, DEFAULT_OPTIONS, id);
      if (bench.length === 0) assert.equal(help.needs[0], t(lang, 'hNone'), id);
      else assert.ok(help.needs[0]!.startsWith(t(lang, 'gNeeds')), id);
    }
  }
  const needs = (id: string, options: Options = DEFAULT_OPTIONS, lang: Lang = 'en'): string[] => partHelp(view({ options }, lang), id)!.needs;
  // The power input of the PD mini is a supply wire too: the USB-C cable from the T-plug.
  assert.deepEqual(needs('pdmini'), [
    'Needs: PD charger, T-plug.',
    'PD in: T-plug OUT, USB-C socket. USB-C cable, plugged into the socket OUT and into the USB-C socket of the PD mini. The current rating the cable needs is not stated. Output of the PD mini is at least 0.7 V below the input voltage.',
  ]);
  assert.match(needs('pdmini', DEFAULT_OPTIONS, 'de')[1]!, /^PD-Eingang: T-Stecker OUT, USB-C-Buchse\. USB-C-Kabel, /);
  assert.deepEqual(needs('sd'), ['Needs no further part of the bench.']);
  assert.deepEqual(needs('ina3221'), ['Needs no further part of the bench.', 'Supply, 3.3 V: IOmcu 3V3 (pad 36).']);
  // The INA228 board decides the supply: 5V from VBUS for the MATEK board, 3.3V for the XT90 board.
  assert.equal(needs('ina228')[1], 'Supply, 5 V: IOmcu VBUS (pad 40). The MATEK board needs 4 to 9 V.');
  assert.match(needs('ina228', { ...DEFAULT_OPTIONS, ina228Board: 'xt90' })[1]!, /^Supply, 3\.3 V: IOmcu 3V3 \(pad 36\)\. /);
  assert.match(needs('ina228', { ...DEFAULT_OPTIONS, ina228Board: 'xt90' }, 'de')[1]!, /^Versorgung, 3,3 V: IOmcu 3V3 \(Pad 36\)\. /);
  // On the Osoyoo breakout board the supply is a terminal.
  assert.equal(needs('ina3221', { ...DEFAULT_OPTIONS, connection: 'osoyoo' })[1], 'Supply, 3.3 V: terminal 3V3.');
});

test('a part names every wire of its supply, logic and power, that ends on it, in every variant', () => {
  // Each wire of class logic or power that ends on the part in the setup with it alone is one line of its
  // bubble, after the lines of the needs; no other wire is. The PD mini is the one part with a power input.
  const variants: Partial<Options>[] = [{}, { connection: 'osoyoo' }, { ina228Board: 'xt90' }, { escPower: 'pd' }, { servoPower: 'bench' }];
  const withPower = new Set<string>();
  for (const lang of LANGS) {
    for (const v of variants) {
      const options = { ...DEFAULT_OPTIONS, ...v };
      for (const id of OWNABLE) {
        const help = partHelp(view({ options }, lang), id);
        if (!help) continue;
        const setup = resolve(CATALOGUE, { functions: [], owned: [id], options });
        const supply = setup.wires.filter((w) => (w.cls === 'logic' || w.cls === 'power') && w.b !== null && w.b.split('.')[0] === id);
        if (supply.some((w) => w.cls === 'power')) withPower.add(id);
        const lines = help.needs.slice(help.needs.length - supply.length);
        assert.equal(lines.length, supply.length, `${lang} ${id}`);
        supply.forEach((w, i) => {
          assert.ok(w.note === null || lines[i]!.endsWith(` ${w.note[lang]}`), `${lang} ${id} wire ${w.n}: ${lines[i]}`);
        });
        const before = help.needs.slice(0, help.needs.length - supply.length);
        assert.ok(before.length >= 1 && before.every((l) => !l.includes(': ') || /^(?:Needs|Braucht|Connects|Schließt an|Variant|Variante): /.test(l)), `${lang} ${id}: ${help.needs.join(' | ')}`);
      }
    }
  }
  assert.deepEqual([...withPower], ['pdmini']);
});

test('the bubble follows the variants of the page, after the variants a function sets', () => {
  // "Drive an ESC" with the tick of the ESC stick menu: the ESC takes the PD mini, no battery.
  const v = view({ functions: ['esc', 'stick'] });
  assert.equal(v.options.escPower, 'pd');
  assert.deepEqual(functionHelp(v, 'esc')!.needs, [`Needs: ${short('pdmini', 'en')}, ${short('charger', 'en')}, ${short('usbtap', 'en')}.`, 'Connects: ESC.']);
  assert.deepEqual(functionHelp(view({ functions: ['esc'] }), 'esc')!.needs, ['Needs: ESC battery.', 'Connects: ESC.']);
});
