// The resolver on named selections: the limits, each rule, each option, and
// input that is not a selection.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Catalogue, Options, Selection, Setup } from '../../contract.ts';
import core, { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../index.ts';
import { holds, optionsIn, partsIn, requiresOption } from '../cond.ts';
import { OPTION_KEYS, OPTION_VALUES } from '../resolve.ts';

const LOCKED = ['panel', 'copro', 'usb', 'wire'];
const SELECTABLE = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const FUNCTIONS = CATALOGUE.functions.map((f) => f.id);
const BASE = ['link', 'interlock', 'stop', 'bind', 'signals', 'sweep', 'ppm', 'settings', 'language', 'profiles'];

function select(functions: string[] = [], owned: string[] = [], options: Partial<Options> = {}): Selection {
  return { functions, owned, options: { ...DEFAULT_OPTIONS, ...options } };
}

function partIds(setup: Setup): string[] {
  return setup.parts.map((p) => p.part.id);
}

function wireNumbers(setup: Setup): number[] {
  return setup.wires.map((w) => w.n);
}

function option(setup: Setup, key: string): Setup['options'][number] {
  const found = setup.options.find((o) => o.key === key);
  assert.ok(found, `option ${key} is in the setup`);
  return found;
}

function part(setup: Setup, id: string): Setup['parts'][number] {
  const found = setup.parts.find((p) => p.part.id === id);
  assert.ok(found, `part ${id} is in the setup`);
  return found;
}

function fn(setup: Setup, id: string): Setup['functions'][number] {
  const found = setup.functions.find((f) => f.fn.id === id);
  assert.ok(found, `function ${id} is in the setup`);
  return found;
}

function deepFreeze<T>(value: T): T {
  if (typeof value === 'object' && value !== null && !Object.isFrozen(value)) {
    Object.freeze(value);
    for (const item of Object.values(value)) deepFreeze(item);
  }
  return value;
}

// --- the index --------------------------------------------------------------

test('the exported tables are frozen: a write by a caller throws and changes no later setup', () => {
  const before = resolve(CATALOGUE, select(['esc']));
  assert.throws(() => { DEFAULT_OPTIONS.escPower = 'pd'; }, TypeError);
  assert.throws(() => { (core.DEFAULT_OPTIONS as { connection: string }).connection = 'osoyoo'; }, TypeError);
  assert.throws(() => { (OPTION_VALUES.escPower as string[]).push('mains'); }, TypeError);
  assert.throws(() => { (OPTION_KEYS as string[]).reverse(); }, TypeError);
  assert.throws(() => { (core as { resolve: unknown }).resolve = null; }, TypeError);
  // The catalogue, and through a setup its records.
  assert.throws(() => { CATALOGUE.rules.pop(); }, TypeError);
  assert.throws(() => { CATALOGUE.wires[0]!.a = 'pad.1'; }, TypeError);
  assert.throws(() => { CATALOGUE.parts[0]!.name.en = ''; }, TypeError);
  assert.throws(() => { before.wires[0]!.net = 'x'; }, TypeError);
  assert.throws(() => { before.parts[0]!.part.pins.length = 0; }, TypeError);
  // The arrays of a setup are its own.
  before.wires.length = 0;
  before.options[0]!.value = 'osoyoo';
  assert.deepEqual(DEFAULT_OPTIONS, { connection: 'pads', ina228Board: 'matek', escPower: 'battery', servoPower: 'external', servoPort: 'outputs' });
  const after = resolve(CATALOGUE, select(['esc']));
  assert.deepEqual(after.wires.map((w) => w.n), [1, 2, 3, 4, 11, 12, 13, 14, 15]);
  assert.equal(after.options[0]!.value, 'pads');
  assert.equal(after.options[2]!.value, 'battery');
});

test('the index exports the catalogue, the resolver and the default options', () => {
  assert.equal(core.CATALOGUE, CATALOGUE);
  assert.equal(core.resolve, resolve);
  assert.deepEqual(core.DEFAULT_OPTIONS, { connection: 'pads', ina228Board: 'matek', escPower: 'battery', servoPower: 'external', servoPort: 'outputs' });
  for (const key of OPTION_KEYS) assert.equal(OPTION_VALUES[key][0], DEFAULT_OPTIONS[key]);
});

// --- limits -----------------------------------------------------------------

test('empty selection: the locked parts and the wires 1 to 4', () => {
  const setup = resolve(CATALOGUE, select());
  assert.deepEqual(partIds(setup), LOCKED);
  assert.deepEqual(wireNumbers(setup), [1, 2, 3, 4]);
  assert.deepEqual(setup.checks.map((c) => c.id), ['can-60', 'panel-terminals', 'same-release']);
  assert.deepEqual(setup.unknown.map((u) => u.id), []);
  assert.deepEqual(setup.ignored, []);
  for (const p of setup.parts) {
    assert.deepEqual(p.addedBy, [{ kind: 'always' }]);
    assert.equal(p.derived, true);
    assert.equal(p.ownedTick, false);
  }
  assert.deepEqual(setup.functions.map((f) => f.fn.id), FUNCTIONS);
  // The functions every setup runs are enabled. Every other function misses a
  // part or drives a test object that is not connected.
  assert.deepEqual(setup.functions.filter((f) => f.enabled).map((f) => f.fn.id), BASE);
  assert.ok(setup.functions.every((f) => !f.chosen
    && (f.enabled ? f.fn.needs.length === 0 && f.fn.drives === undefined : f.missing.length > 0 || f.fn.drives !== undefined)));
  // "Drive a servo", the servo programmer and "Drive an ESC" miss no part: the test object is not connected.
  assert.deepEqual(
    setup.functions.filter((f) => !f.enabled && f.missing.length === 0).map((f) => f.fn.id),
    ['servo', 'servoprog', 'esc'],
  );
  assert.deepEqual(setup.options, [
    { key: 'connection', value: 'pads', visible: true, forcedBy: null },
    { key: 'ina228Board', value: 'matek', visible: false, forcedBy: null },
    { key: 'escPower', value: 'battery', visible: false, forcedBy: null },
    { key: 'servoPower', value: 'external', visible: false, forcedBy: null },
    { key: 'servoPort', value: 'outputs', visible: false, forcedBy: null },
  ]);
  // The logic has its power source; no test object is connected.
  assert.deepEqual(setup.power.map((p) => [p.source.id, p.used]), [['usb', true]]);
});

test('everything selected: every function and every part you own', () => {
  const setup = resolve(CATALOGUE, select(FUNCTIONS, SELECTABLE));
  // STICK programming forces ESC power B: no battery, and no servo supply beside the PD mini.
  assert.deepEqual(
    partIds(setup),
    CATALOGUE.parts.map((p) => p.id).filter((id) => !['ssup', 'batt', 'pull'].includes(id)),
  );
  assert.deepEqual(option(setup, 'escPower'), { key: 'escPower', value: 'pd', visible: true, forcedBy: 'stick' });
  // The servo programmer puts the servo signal on the programming port: wire 64 in place of wire 7.
  assert.deepEqual(option(setup, 'servoPort'), { key: 'servoPort', value: 'prog', visible: false, forcedBy: 'servoprog' });
  assert.ok(setup.functions.every((f) => f.chosen && f.enabled && f.missing.length === 0));
  assert.deepEqual(wireNumbers(setup), [
    1, 2, 3, 5, 6, 8, 11, 12, 13, 16, 17, 18, 19, 21, 23, 24, 25, 26, 27, 28, 30, 31, 32,
    33, 34, 35, 36, 37, 41, 42, 43, 44, 46, 47, 48, 49, 50, 51, 52, 53, 54, 63, 64,
  ]);
  assert.deepEqual(setup.ignored, []);
  assert.ok(setup.parts.filter((p) => p.part.selectable).every((p) => p.ownedTick));
});

test('everything selected on the other value of each option', () => {
  const setup = resolve(CATALOGUE, select(['servo', 'esc'], SELECTABLE, { connection: 'osoyoo', ina228Board: 'xt90', escPower: 'pd' }));
  // The module with XT90 plugs takes its supply on wire 55, not on wire 41.
  for (const n of [42, 43, 44, 55]) assert.ok(wireNumbers(setup).includes(n), `wire ${n}`);
  assert.ok(!wireNumbers(setup).includes(41));
  const ids = setup.checks.map((c) => c.id);
  for (const id of ['ina228-xt90', 'ina228-xt90-pads', 'osoyoo-labels', 'osoyoo-terminals', 'pd-shared', 'strip-esc']) assert.ok(ids.includes(id), id);
  assert.ok(!ids.includes('matek-pullup'));
});

test('every single part you own resolves to itself, what it needs, and the locked parts', () => {
  const expected: Record<string, string[]> = {
    pdmini: ['pdmini', 'charger', 'usbtap'],
    ina3221: ['ina3221'],
    as5600: ['as5600', 'pull'],
    ina228: ['pull', 'ina228'],
    sd: ['sd'],
    estop: ['estop'],
    sim: ['sim'],
    tap: ['tap'],
    usbtap: ['usbtap'],
  };
  assert.deepEqual(Object.keys(expected), SELECTABLE);
  for (const id of SELECTABLE) {
    const setup = resolve(CATALOGUE, select([], [id]));
    assert.deepEqual(partIds(setup), [...LOCKED, ...expected[id]!], id);
    assert.deepEqual(part(setup, id).addedBy, [{ kind: 'owned' }]);
    assert.equal(part(setup, id).derived, false);
    assert.equal(part(setup, id).ownedTick, true);
    assert.deepEqual(setup.ignored, []);
    // Every wire of the setup ends on a part of the setup or on the coprocessor.
    for (const wire of setup.wires) {
      for (const end of [wire.a, wire.b]) {
        if (end === null || end.startsWith('pad.') || end.startsWith('can.')) continue;
        assert.ok(partIds(setup).includes(end.split('.')[0]!), `${id}: wire ${wire.n} ends on ${end}`);
      }
    }
  }
});

test('every single function resolves to its parts and is enabled', () => {
  for (const f of CATALOGUE.functions) {
    const setup = resolve(CATALOGUE, select([f.id]));
    const own = fn(setup, f.id);
    assert.deepEqual({ chosen: own.chosen, enabled: own.enabled, missing: own.missing }, { chosen: true, enabled: true, missing: [] }, f.id);
    for (const id of f.needs) {
      assert.deepEqual(part(setup, id).addedBy[0], { kind: 'function', id: f.id });
      assert.equal(part(setup, id).derived, true);
      assert.equal(part(setup, id).ownedTick, false);
    }
    assert.deepEqual(setup.ignored, []);
  }
});

test('a single locked or derived part or a test object as "owned" is ignored', () => {
  for (const id of ['panel', 'copro', 'usb', 'wire', 'servo', 'esc', 'ssup', 'batt', 'charger', 'pull']) {
    const setup = resolve(CATALOGUE, select([], [id]));
    assert.deepEqual(partIds(setup), LOCKED, id);
    assert.deepEqual(setup.ignored, [id]);
  }
});

// --- rules ------------------------------------------------------------------

test('rule: a servo without the PD mini adds the servo supply; with it, none', () => {
  const alone = resolve(CATALOGUE, select(['servo']));
  assert.deepEqual(part(alone, 'ssup').addedBy, [
    { kind: 'rule', id: 'servo-supply', because: { en: 'Servo', de: 'Servo' } },
  ]);
  assert.deepEqual(wireNumbers(alone), [1, 2, 3, 4, 7, 8, 9, 10]);

  const supplied = resolve(CATALOGUE, select(['servo', 'supply']));
  assert.ok(!partIds(supplied).includes('ssup'));
  assert.deepEqual(part(supplied, 'charger').addedBy, [
    { kind: 'rule', id: 'pd-charger', because: { en: 'PD mini', de: 'PD mini' } },
  ]);
  // The T-plug carries the serial lines of the PD mini.
  assert.ok(partIds(supplied).includes('usbtap'));
  assert.deepEqual(part(supplied, 'usbtap').addedBy, [
    { kind: 'rule', id: 'pd-tplug', because: { en: 'UART of the PD mini', de: 'UART des PD mini' } },
  ]);
  assert.equal(part(supplied, 'usbtap').derived, true);
  assert.equal(part(supplied, 'usbtap').ownedTick, false);
  assert.deepEqual(wireNumbers(supplied), [1, 2, 3, 4, 7, 8, 16, 17, 18, 19, 20, 21, 63]);
  assert.deepEqual(
    supplied.wires.filter((w) => [16, 17, 18].includes(w.n)).map((w) => [w.a, w.b]),
    [['pad.10', 'usbtap.DM'], ['usbtap.DP', 'pad.9'], ['pad.18', 'usbtap.G']],
  );
  // Owned without the PD mini it has no wire.
  const tapAlone = resolve(CATALOGUE, select([], ['usbtap']));
  assert.deepEqual(wireNumbers(tapAlone), [1, 2, 3, 4]);
  assert.ok(tapAlone.checks.some((c) => c.id === 'usbtap-v'));
});

test('rule: the ESC takes its battery, or with ESC power B the PD mini and its charger', () => {
  const battery = resolve(CATALOGUE, select(['esc']));
  assert.deepEqual(partIds(battery), [...LOCKED, 'esc', 'batt']);
  assert.deepEqual(wireNumbers(battery), [1, 2, 3, 4, 11, 12, 13, 14, 15]);

  const pd = resolve(CATALOGUE, select(['esc'], [], { escPower: 'pd' }));
  assert.deepEqual(partIds(pd), [...LOCKED, 'esc', 'pdmini', 'charger', 'usbtap']);
  assert.deepEqual(part(pd, 'pdmini').addedBy, [
    { kind: 'rule', id: 'esc-pd', because: { en: 'ESC power B', de: 'ESC-Versorgung B' } },
  ]);
  assert.deepEqual(wireNumbers(pd), [1, 2, 3, 4, 11, 12, 13, 16, 17, 18, 19, 22, 23, 63]);
  assert.equal(option(pd, 'escPower').forcedBy, null);

  // ESC power B without an ESC adds nothing.
  assert.deepEqual(partIds(resolve(CATALOGUE, select([], [], { escPower: 'pd' }))), LOCKED);
});

test('rule: a sensor on the bus without the INA3221 adds the pull-ups', () => {
  for (const id of ['as5600', 'ina228']) {
    const without = resolve(CATALOGUE, select([], [id]));
    assert.ok(partIds(without).includes('pull'), id);
    assert.ok([38, 39, 40].every((n) => wireNumbers(without).includes(n)));

    const withBoard = resolve(CATALOGUE, select([], [id, 'ina3221']));
    assert.ok(!partIds(withBoard).includes('pull'), id);
    assert.ok([38, 39, 40].every((n) => !wireNumbers(withBoard).includes(n)));
  }
  assert.ok(!partIds(resolve(CATALOGUE, select([], ['ina3221']))).includes('pull'));
});

test('rules run once and in order: a rule reads the part an earlier rule adds', () => {
  // esc-pd adds the PD mini; pd-charger, after it, adds the charger; servo-supply sees the PD mini.
  const setup = resolve(CATALOGUE, select(['servo', 'esc'], [], { escPower: 'pd' }));
  assert.deepEqual(partIds(setup), [...LOCKED, 'servo', 'esc', 'pdmini', 'charger', 'usbtap']);

  // With the order reversed one pass is not complete: the charger is missing.
  const reversed: Catalogue = { ...CATALOGUE, rules: [...CATALOGUE.rules].reverse() };
  assert.deepEqual(
    partIds(resolve(reversed, select(['servo', 'esc'], [], { escPower: 'pd' }))),
    [...LOCKED, 'servo', 'ssup', 'esc', 'pdmini'],
  );
});

// --- reasons ----------------------------------------------------------------

test('addedBy holds every reason, in the order of work', () => {
  const setup = resolve(CATALOGUE, select(['stick', 'supply', 'servotest', 'esc'], ['pdmini']));
  assert.deepEqual(part(setup, 'pdmini').addedBy, [
    { kind: 'function', id: 'servotest' },
    { kind: 'function', id: 'stick' },
    { kind: 'function', id: 'supply' },
    { kind: 'owned' },
    { kind: 'rule', id: 'esc-pd', because: { en: 'ESC power B', de: 'ESC-Versorgung B' } },
  ]);
  assert.equal(part(setup, 'pdmini').derived, true);
  assert.equal(part(setup, 'pdmini').ownedTick, true);
  assert.deepEqual(part(setup, 'esc').addedBy, [
    { kind: 'function', id: 'esc' },
    { kind: 'function', id: 'stick' },
  ]);
  // A test object is never an owned part.
  assert.equal(part(setup, 'esc').ownedTick, false);
  assert.equal(part(setup, 'esc').derived, true);
  assert.deepEqual(part(setup, 'servo').addedBy, [{ kind: 'function', id: 'servotest' }]);
});

test('a part that is only owned is not derived; a part a rule also adds is', () => {
  const owned = resolve(CATALOGUE, select([], ['pdmini']));
  assert.equal(part(owned, 'pdmini').derived, false);
  assert.equal(part(owned, 'charger').derived, true);
  assert.equal(part(owned, 'charger').ownedTick, false);

  const both = resolve(CATALOGUE, select(['esc'], ['pdmini'], { escPower: 'pd' }));
  assert.equal(part(both, 'pdmini').derived, true);
  assert.equal(part(both, 'pdmini').ownedTick, true);
});

// --- functions ----------------------------------------------------------------

test('a function is enabled by owned parts without being chosen, once its test object is connected', () => {
  const setup = resolve(CATALOGUE, select(['servo'], ['ina3221']));
  assert.deepEqual(fn(setup, 'servocur'), {
    fn: CATALOGUE.functions.find((f) => f.id === 'servocur'), chosen: false, enabled: true, missing: [],
  });
  assert.deepEqual(fn(setup, 'servotest').missing, ['pdmini']);
  assert.equal(fn(setup, 'servotest').enabled, false);
  // No function misses a test object: the ESC is no part the bench needs.
  assert.deepEqual(fn(setup, 'tap').missing, ['sim', 'tap']);

  // The parts alone: the function misses no part and is off, no servo is connected.
  const parts = resolve(CATALOGUE, select([], ['ina3221']));
  assert.deepEqual(
    { enabled: fn(parts, 'servocur').enabled, missing: fn(parts, 'servocur').missing },
    { enabled: false, missing: [] },
  );
  assert.ok(!partIds(parts).includes('servo'));
  // Every function that drives the servo connects it.
  for (const id of ['servo', 'servotest', 'servocur', 'angle']) {
    assert.deepEqual(part(resolve(CATALOGUE, select([id])), 'servo').addedBy, [{ kind: 'function', id }], id);
  }
  for (const id of ['esc', 'escpow', 'stick', 'tap']) {
    assert.deepEqual(part(resolve(CATALOGUE, select([id])), 'esc').addedBy, [{ kind: 'function', id }], id);
  }
});

test('a function that drives a test object needs bench hardware only', () => {
  const objects = CATALOGUE.parts.filter((p) => p.testObject === true).map((p) => p.id);
  assert.deepEqual(objects, ['servo', 'esc']);
  for (const f of CATALOGUE.functions) {
    for (const id of f.needs) assert.ok(!objects.includes(id), `${f.id} needs ${id}`);
    if (f.drives !== undefined) assert.ok(objects.includes(f.drives), f.id);
  }
  // 2 test objects in one setup: each with the parts its rules add.
  const both = resolve(CATALOGUE, select(['servo', 'esc']));
  assert.deepEqual(partIds(both), [...LOCKED, 'servo', 'ssup', 'esc', 'batt']);
  assert.deepEqual(both.functions.filter((f) => f.enabled && f.fn.drives !== undefined).map((f) => f.fn.id), ['servo', 'esc']);
  // A test object and 1 part.
  const one = resolve(CATALOGUE, select(['angle']));
  assert.deepEqual(partIds(one), [...LOCKED, 'servo', 'ssup', 'as5600', 'pull']);
  assert.deepEqual(one.functions.filter((f) => f.enabled && f.fn.drives !== undefined).map((f) => f.fn.id), ['servo', 'angle']);
});

test('STICK programming: enabled with ESC power B only', () => {
  const battery = resolve(CATALOGUE, select(['esc'], ['pdmini']));
  assert.deepEqual(fn(battery, 'stick').missing, []);
  assert.equal(fn(battery, 'stick').enabled, false);
  assert.ok(partIds(battery).includes('batt'));
  assert.ok(!battery.checks.some((c) => c.id === 'esc-programming-power'));

  const pd = resolve(CATALOGUE, select(['esc'], ['pdmini'], { escPower: 'pd' }));
  // The open point of the supply during programming is listed with the function.
  assert.ok(pd.unknown.some((u) => u.id === 'esc-programming-power'));
  assert.equal(fn(pd, 'stick').enabled, true);
  assert.equal(option(pd, 'escPower').forcedBy, null);

  // Chosen, it forces the option over the value of the selection.
  const chosen = resolve(CATALOGUE, select(['stick'], [], { escPower: 'battery' }));
  assert.deepEqual(option(chosen, 'escPower'), { key: 'escPower', value: 'pd', visible: true, forcedBy: 'stick' });
  assert.ok(!partIds(chosen).includes('batt'));
  assert.deepEqual(chosen.ignored, []);
});

test('two functions forcing one option: the first in catalogue order names it', () => {
  const c = structuredClone(CATALOGUE);
  c.functions.find((f) => f.id === 'esc')!.forces = { escPower: 'battery' };
  const setup = resolve(c, select(['stick', 'esc']));
  assert.deepEqual(option(setup, 'escPower'), { key: 'escPower', value: 'battery', visible: true, forcedBy: 'esc' });
  assert.equal(fn(setup, 'esc').enabled, true);
  // The second function is chosen and its option does not hold.
  assert.deepEqual({ chosen: fn(setup, 'stick').chosen, enabled: fn(setup, 'stick').enabled }, { chosen: true, enabled: false });
});

// --- options ----------------------------------------------------------------

test('option visibility follows the parts of the setup', () => {
  const none = resolve(CATALOGUE, select());
  assert.deepEqual(none.options.map((o) => o.visible), [true, false, false, false, false]);
  assert.equal(option(resolve(CATALOGUE, select([], ['ina228'])), 'ina228Board').visible, true);
  assert.equal(option(resolve(CATALOGUE, select(['escpow'])), 'ina228Board').visible, true);
  assert.equal(option(resolve(CATALOGUE, select(['esc'])), 'escPower').visible, true);
  assert.equal(option(resolve(CATALOGUE, select(['servo'])), 'escPower').visible, false);
  // The servo power variant: with a servo and without the PD mini.
  assert.equal(option(resolve(CATALOGUE, select(['servo'])), 'servoPower').visible, true);
  assert.equal(option(resolve(CATALOGUE, select(['servocur'])), 'servoPower').visible, true);
  assert.equal(option(resolve(CATALOGUE, select(['esc'])), 'servoPower').visible, false);
  assert.equal(option(resolve(CATALOGUE, select(['servo', 'supply'])), 'servoPower').visible, false);
  assert.equal(option(resolve(CATALOGUE, select(['servotest'])), 'servoPower').visible, false);
});

test('servo power: the external supply, the 5 V pin, the strip of the Osoyoo breakout board, the PD mini', () => {
  const plus = (setup: Setup): Array<[number, string, string | null]> => setup.wires
    .filter((w) => w.cls === 'power' && [w.a, w.b].some((end) => end === 'servo.POS' || end === 'ina3221.C1A'))
    .map((w) => [w.n, w.a, w.b]);
  const ids = (setup: Setup): string[] => setup.checks.map((c) => c.id);
  const used = (setup: Setup): string[] => setup.power.filter((p) => p.used && p.source.feeds === 'servo').map((p) => p.source.id);
  const WARNING = 'servo-bench-supply';

  const external = resolve(CATALOGUE, select(['servo']));
  assert.deepEqual(plus(external), [[9, 'ssup.POS', 'servo.POS']]);
  assert.ok(partIds(external).includes('ssup'));
  assert.ok(ids(external).includes('servo-external') && !ids(external).includes(WARNING));
  assert.deepEqual(used(external), ['lipo-2s', 'own-supply']);
  // The 2 external sources are one choice: the setup has 1 servo supply and does not state which of them it is.
  assert.deepEqual(external.power.filter((p) => p.source.choice !== undefined).map((p) => [p.source.id, p.source.choice, p.used]),
    [['lipo-2s', 'servo-supply', true], ['own-supply', 'servo-supply', true]]);
  assert.equal(external.parts.filter((r) => r.part.id === 'ssup').length, 1);

  const pin = resolve(CATALOGUE, select(['servo'], [], { servoPower: 'bench' }));
  assert.deepEqual(partIds(pin), [...LOCKED, 'servo']);
  assert.deepEqual(wireNumbers(pin), [1, 2, 3, 4, 7, 8, 59]);
  assert.deepEqual(plus(pin), [[59, 'servo.POS', 'pad.40']]);
  assert.equal(CATALOGUE.pads.find((p) => p.n === 40)?.name, 'VBUS');
  assert.ok(ids(pin).includes(WARNING) && ids(pin).includes('servo-5v-pin') && !ids(pin).includes('servo-external'));
  assert.deepEqual(pin.unknown.map((u) => u.id), ['servo-5v-pin']);
  assert.deepEqual(used(pin), ['copro-5v']);

  const strip = resolve(CATALOGUE, select(['servo'], [], { servoPower: 'bench', connection: 'osoyoo' }));
  assert.deepEqual(partIds(strip), [...LOCKED, 'servo']);
  assert.deepEqual(wireNumbers(strip), [1, 2, 3, 4, 7, 8, 61]);
  assert.deepEqual(plus(strip), [[61, 'servo.POS', null]]);
  for (const id of [WARNING, 'strip-jumper', 'strip-dc', 'strip-measure', 'strip-plug', 'strip-supply']) assert.ok(ids(strip).includes(id), id);
  assert.ok(!ids(strip).includes('servo-5v-pin'));
  assert.deepEqual(used(strip), ['strip-v']);
  // Without the Osoyoo breakout board none of the strip checks appears.
  const STRIP_CHECKS = ['strip-jumper', 'strip-dc', 'strip-measure', 'strip-feed', 'strip-converter', 'strip-input', 'strip-print', 'strip-regulator', 'strip-supply'];
  const bare = resolve(CATALOGUE, select(['servo'], [], { servoPower: 'bench', connection: 'pads' }));
  assert.deepEqual(ids(bare).filter((id) => id.startsWith('strip-')), []);
  for (const id of STRIP_CHECKS) assert.ok(!ids(bare).includes(id), id);
  assert.ok(ids(bare).includes('servo-5v-pin'));
  const extStrip = resolve(CATALOGUE, select(['servo'], [], { servoPower: 'external', connection: 'osoyoo' }));
  for (const id of STRIP_CHECKS) assert.ok(!ids(extStrip).includes(id), id);
  // No wire of the strip variant ends on pad 40.
  assert.ok(!strip.wires.some((w) => w.a === 'pad.40' || w.b === 'pad.40'));

  // Through the shunt of the INA3221 the supply ends on CH1 IN+.
  const shuntPin = resolve(CATALOGUE, select(['servocur'], [], { servoPower: 'bench' }));
  assert.deepEqual(plus(shuntPin), [[30, 'servo.POS', 'ina3221.C1B'], [60, 'pad.40', 'ina3221.C1A']]);
  const shuntStrip = resolve(CATALOGUE, select(['servocur'], [], { servoPower: 'bench', connection: 'osoyoo' }));
  assert.deepEqual(plus(shuntStrip), [[30, 'servo.POS', 'ina3221.C1B'], [62, 'ina3221.C1A', null]]);
  assert.ok(ids(shuntPin).includes(WARNING) && ids(shuntStrip).includes(WARNING));
  assert.ok(!ids(shuntStrip).includes('strip-plug'));

  // The PD mini feeds the servo under either value: no wire to a bench pin and no warning.
  for (const servoPower of OPTION_VALUES.servoPower) {
    const pd = resolve(CATALOGUE, select(['servo', 'supply'], [], { servoPower }));
    assert.deepEqual(plus(pd), [[20, 'pdmini.OP', 'servo.POS']], servoPower);
    assert.ok(!ids(pd).includes(WARNING) && !ids(pd).includes('servo-external'), servoPower);
    assert.deepEqual(used(pd), ['pd-servo'], servoPower);
    assert.deepEqual(option(pd, 'servoPower'), { key: 'servoPower', value: servoPower, visible: false, forcedBy: null });
  }
  // Without a servo the value adds nothing.
  assert.deepEqual(resolve(CATALOGUE, select([], [], { servoPower: 'bench' })).wires.map((w) => w.n), [1, 2, 3, 4]);
});

test('an ESC is connected with signal and ground only, under every variant', () => {
  for (const connection of OPTION_VALUES.connection) {
    for (const escPower of OPTION_VALUES.escPower) {
      for (const servoPower of OPTION_VALUES.servoPower) {
        for (const id of ['esc', 'escpow', 'stick', 'tap']) {
          const setup = resolve(CATALOGUE, select([id, 'servo'], [], { connection, escPower, servoPower }));
          const where = `${id} ${connection} ${escPower} ${servoPower}`;
          // Wire 13 is the centre wire, a stub that ends nowhere.
          const centre = setup.wires.filter((w) => w.a === 'esc.BEC' || w.b === 'esc.BEC');
          assert.deepEqual(centre.map((w) => [w.n, w.kind, w.b]), [[13, 'stub', null]], where);
          // The pads an ESC lead reaches: signal and GND.
          const pads = setup.wires
            .filter((w) => [w.a, w.b].some((end) => end?.startsWith('esc.')) && [w.a, w.b].some((end) => end?.startsWith('pad.')))
            .map((w) => [w.n, w.cls]);
          assert.deepEqual(pads, [[11, 'out'], [12, 'gnd']], where);
          assert.equal(setup.checks.some((c) => c.id === 'strip-esc'), connection === 'osoyoo', where);
          assert.deepEqual(
            setup.power.filter((p) => p.used && p.source.feeds === 'esc').map((p) => p.source.id),
            [setup.options.find((o) => o.key === 'escPower')?.value === 'pd' ? 'pd-esc' : 'esc-battery'], where,
          );
        }
      }
    }
  }
});

test('an option keeps its value while it is not visible', () => {
  const setup = resolve(CATALOGUE, select([], [], { ina228Board: 'xt90', escPower: 'pd', connection: 'osoyoo' }));
  assert.deepEqual(setup.options.map((o) => o.value), ['osoyoo', 'xt90', 'pd', 'external', 'outputs']);
  assert.deepEqual(setup.checks.map((c) => c.id), [
    'can-60', 'panel-terminals', 'same-release', 'osoyoo-labels', 'osoyoo-terminals', 'strip-parts', 'strip-load', 'strip-level',
  ]);
});

test('the INA228 board variant selects the supply wires and the check entry', () => {
  const matek = resolve(CATALOGUE, select(['escpow']));
  assert.ok([41, 42, 43, 44, 45, 47].every((n) => wireNumbers(matek).includes(n)));
  assert.ok(!wireNumbers(matek).includes(14));
  assert.ok(matek.checks.some((c) => c.id === 'matek-pullup'));
  assert.ok(!matek.checks.some((c) => c.id.startsWith('ina228-xt90')));

  const xt90 = resolve(CATALOGUE, select(['escpow'], [], { ina228Board: 'xt90' }));
  // The supply wire 55 replaces wire 41. The shunt of the module is in the
  // minus line: wires 56 and 58 replace the direct minus wire 15.
  assert.ok(wireNumbers(matek).includes(15));
  assert.deepEqual(
    wireNumbers(xt90),
    [...wireNumbers(matek).filter((n) => n !== 41 && n !== 15), 55, 56, 58],
  );
  for (const id of ['ina228-xt90', 'ina228-xt90-pads']) {
    assert.equal(xt90.checks.find((c) => c.id === id)?.kind, 'note', id);
  }
  assert.ok(!xt90.unknown.some((u) => u.id.startsWith('ina228')));
  assert.ok(!xt90.checks.some((c) => c.id === 'matek-pullup'));
});

test('the module with XT90 plugs carries the minus of the ESC through its shunt', () => {
  const minus = (setup: Setup): Array<[number, string, string | null, string]> => setup.wires
    .filter((w) => [15, 23, 56, 57, 58].includes(w.n))
    .map((w) => [w.n, w.a, w.b, w.net]);
  const pick = (ina228Board: 'matek' | 'xt90', escPower: 'battery' | 'pd'): Setup =>
    resolve(CATALOGUE, select(['escpow'], [], { ina228Board, escPower }));

  assert.deepEqual(minus(pick('matek', 'battery')), [[15, 'batt.NEG', 'esc.NEG', 'esc_neg']]);
  assert.deepEqual(minus(pick('matek', 'pd')), [[23, 'pdmini.ON', 'esc.NEG', 'gnd']]);
  assert.deepEqual(minus(pick('xt90', 'battery')), [
    [56, 'batt.NEG', 'ina228.BATN', 'batt_neg'],
    [58, 'esc.NEG', 'ina228.ESCN', 'esc_neg'],
  ]);
  assert.deepEqual(minus(pick('xt90', 'pd')), [
    [57, 'pdmini.ON', 'ina228.BATN', 'gnd'],
    [58, 'esc.NEG', 'ina228.ESCN', 'esc_neg'],
  ]);

  // No wire joins the minus of the source and the minus of the ESC past the module.
  for (const escPower of ['battery', 'pd'] as const) {
    const setup = pick('xt90', escPower);
    assert.ok(!setup.wires.some((w) => w.b === 'esc.NEG' && w.a !== 'esc.NEG'), escPower);
    assert.equal(setup.wires.filter((w) => w.a === 'esc.NEG' || w.b === 'esc.NEG').length, 1, escPower);
  }
  // The parallel ground path under ESC power B is an open point of the data.
  assert.deepEqual(pick('xt90', 'pd').unknown.filter((u) => u.id.startsWith('ina228')).map((u) => u.id), ['ina228-xt90-pd']);
  assert.ok(!pick('matek', 'pd').checks.some((c) => c.id === 'ina228-xt90-pd'));

  // Without the sensor, and with the ESC but no sensor, the direct wire stays.
  const plain = resolve(CATALOGUE, select(['esc'], [], { ina228Board: 'xt90' }));
  assert.deepEqual(minus(plain), [[15, 'batt.NEG', 'esc.NEG', 'esc_neg']]);
  // The module without an ESC has no power wire.
  const alone = resolve(CATALOGUE, select([], ['ina228'], { ina228Board: 'xt90' }));
  assert.deepEqual(minus(alone), []);
});

/** The wires of a setup that end on a supply pin of the INA228 board, as [wire, pad end, pin end, net]. */
function sensorSupply(setup: Setup): Array<[number, string, string | null, string]> {
  return setup.wires
    .filter((w) => w.b === 'ina228.V5' || w.b === 'ina228.V3')
    .map((w) => [w.n, w.a, w.b, w.net]);
}

test('the module with XT90 plugs takes 3.3 V from pad 36 and never VBUS from pad 40', () => {
  const xt90 = resolve(CATALOGUE, select(['escpow'], [], { ina228Board: 'xt90' }));
  assert.deepEqual(sensorSupply(xt90), [[55, 'pad.36', 'ina228.V3', '3v3']]);
  const wire = xt90.wires.find((w) => w.n === 55)!;
  assert.deepEqual(wire.signal, { en: 'supply, 3.3 V', de: 'Versorgung, 3,3 V' });
  // The other wires on pad 36 carry the same net and the same signal text.
  for (const other of CATALOGUE.wires.filter((w) => w.a === 'pad.36')) {
    assert.equal(other.net, '3v3', `wire ${other.n}`);
    assert.deepEqual(other.signal, wire.signal, `wire ${other.n}`);
  }
  assert.equal(CATALOGUE.pads.find((p) => p.n === 36)?.name, '3V3');
  assert.equal(CATALOGUE.pads.find((p) => p.n === 40)?.name, 'VBUS');
  const supplyPin = CATALOGUE.parts.find((p) => p.id === 'ina228')!.pins.find((p) => p.id === 'V3')!;
  assert.deepEqual(supplyPin.label, { en: 'V', de: 'V' });
  assert.equal(supplyPin.confirmed, true);
});

test('the MATEK board takes 5 V from pad 40', () => {
  const matek = resolve(CATALOGUE, select(['escpow']));
  assert.deepEqual(sensorSupply(matek), [[41, 'pad.40', 'ina228.V5', 'vbus']]);
  const wire = matek.wires.find((w) => w.n === 41)!;
  assert.deepEqual(wire.signal, { en: 'supply, 5 V', de: 'Versorgung, 5 V' });
  assert.deepEqual(wire.note, { en: 'The MATEK board needs 4 to 9 V.', de: 'Die MATEK-Platine braucht 4 bis 9 V.' });
});

test('no setup holds both supply wires of the INA228 board, and the board variant decides', () => {
  for (const connection of OPTION_VALUES.connection) {
    for (const ina228Board of OPTION_VALUES.ina228Board) {
      for (const escPower of OPTION_VALUES.escPower) {
        const options = { connection, ina228Board, escPower, servoPower: 'external' as const };
        const selections = [
          select([], [], options), select(['escpow'], [], options), select([], ['ina228'], options),
          select(FUNCTIONS, SELECTABLE, options), select([], SELECTABLE, options),
          ...FUNCTIONS.map((id) => select([id], [], options)),
          ...SELECTABLE.map((id) => select([], [id], options)),
        ];
        for (const selection of selections) {
          const setup = resolve(CATALOGUE, selection);
          const supply = sensorSupply(setup).map(([n, pad]) => `${n}:${pad}`);
          const has = setup.parts.some((p) => p.part.id === 'ina228');
          const want = !has ? [] : ina228Board === 'xt90' ? ['55:pad.36'] : ['41:pad.40'];
          assert.deepEqual(supply, want, JSON.stringify(selection));
          // No wire of a setup with the module with XT90 plugs ends on pad 40.
          if (ina228Board === 'xt90') {
            assert.ok(!setup.wires.some((w) => w.a === 'pad.40' || w.b === 'pad.40'), JSON.stringify(selection));
          }
        }
      }
    }
  }
});

// --- wires --------------------------------------------------------------------

test('the emergency stop replaces wire 4 with the wires 5 and 6', () => {
  assert.deepEqual(wireNumbers(resolve(CATALOGUE, select(['estop']))), [1, 2, 3, 5, 6]);
});

test('the INA3221 moves the plus wire of the servo through its shunt', () => {
  const own = resolve(CATALOGUE, select(['servocur']));
  assert.ok([29, 30, 10].every((n) => wireNumbers(own).includes(n)));
  assert.ok([9, 20, 28].every((n) => !wireNumbers(own).includes(n)));

  const pd = resolve(CATALOGUE, select(['servocur', 'supply']));
  assert.ok([28, 30, 21].every((n) => wireNumbers(pd).includes(n)));
  assert.ok([9, 10, 20, 29].every((n) => !wireNumbers(pd).includes(n)));
});

test('the servo programmer: the 2 boards, the servo on GP13 at pad 17, and the PD mini optional', () => {
  const entry = CATALOGUE.functions.find((f) => f.id === 'servoprog');
  assert.deepEqual([entry?.group, entry?.needs, entry?.drives, entry?.forces], ['servo', [], 'servo', { servoPort: 'prog' }]);
  const prog = CATALOGUE.pinDefaults.find((d) => d.signal === 'prog');
  assert.deepEqual([prog?.gpio, prog?.pad, prog?.choice, prog?.role], [13, 17, 'default', 'output']);
  assert.equal(CATALOGUE.pads.find((p) => p.n === 17)?.gpio, 13);
  const w64 = CATALOGUE.wires.find((w) => w.n === 64);
  assert.deepEqual([w64?.a, w64?.b, w64?.net, w64?.pin], ['pad.17', 'servo.SIG', 'servo_sig', 'default']);

  // Without the PD mini: the panel, the coprocessor and the servo with its own supply.
  const own = resolve(CATALOGUE, select(['servoprog']));
  assert.deepEqual(partIds(own), [...LOCKED, 'servo', 'ssup']);
  assert.deepEqual(wireNumbers(own), [1, 2, 3, 4, 8, 9, 10, 64]);
  assert.deepEqual(option(own, 'servoPort'), { key: 'servoPort', value: 'prog', visible: false, forcedBy: 'servoprog' });
  assert.equal(option(own, 'servoPower').visible, true);
  assert.deepEqual(own.functions.filter((f) => f.enabled && f.fn.drives !== undefined).map((f) => f.fn.id), ['servo', 'servoprog']);
  assert.deepEqual(own.checks.filter((c) => c.id.startsWith('prog-')).map((c) => c.id), ['prog-target', 'prog-supply-own', 'prog-level']);
  assert.deepEqual(own.unknown.map((u) => u.id), ['prog-level']);
  // No wire of the standard outputs and none of their notes.
  assert.ok(!own.checks.some((c) => c.id === 'servo-pin'));

  // With the PD mini: it feeds the servo through wires 20 and 21; CONNECT sets the supply.
  const pd = resolve(CATALOGUE, select(['servoprog'], ['pdmini']));
  assert.ok(!partIds(pd).includes('ssup'));
  assert.ok([20, 21, 64].every((n) => wireNumbers(pd).includes(n)));
  assert.ok([7, 9, 10].every((n) => !wireNumbers(pd).includes(n)));
  assert.deepEqual(pd.checks.filter((c) => c.id.startsWith('prog-')).map((c) => c.id), ['prog-target', 'prog-supply-pd', 'prog-level']);
  assert.deepEqual(pd.power.filter((p) => p.used).map((p) => p.source.id), ['usb', 'pd-servo']);

  // The bench feeds the servo under the other variant: the 5 V pin, or the
  // V pin of a strip of the Osoyoo breakout board, whose plug is at GP2 and so
  // not the plug of a servo on GP13.
  assert.deepEqual(wireNumbers(resolve(CATALOGUE, select(['servoprog'], [], { servoPower: 'bench' }))), [1, 2, 3, 4, 8, 59, 64]);
  const strip = resolve(CATALOGUE, select(['servoprog'], [], { servoPower: 'bench', connection: 'osoyoo' }));
  assert.deepEqual(wireNumbers(strip), [1, 2, 3, 4, 8, 61, 64]);
  assert.ok(!strip.checks.some((c) => c.id === 'strip-plug'));
  assert.ok(resolve(CATALOGUE, select(['servo'], [], { servoPower: 'bench', connection: 'osoyoo' })).checks.some((c) => c.id === 'strip-plug'));

  // The resolver follows the option as the selection gives it. The share link
  // holds no token for it, so on the page the function alone sets it.
  const set = resolve(CATALOGUE, select(['servo'], [], { servoPort: 'prog' }));
  assert.ok(wireNumbers(set).includes(64) && !wireNumbers(set).includes(7));
  assert.deepEqual(wireNumbers(resolve(CATALOGUE, select(['servo']))), [1, 2, 3, 4, 7, 8, 9, 10]);
});

test('the unknown list is the checks of kind unknown, in data order', () => {
  const setup = resolve(CATALOGUE, select(FUNCTIONS, SELECTABLE, { connection: 'osoyoo', ina228Board: 'xt90' }));
  assert.deepEqual(setup.unknown.map((u) => u.id), [
    'pd-shared', 'ina3221-pullups', 'ina228-xt90-pd', 'as5600-board',
    'osoyoo-labels', 'prog-level', 'esc-programming-power',
  ]);
  for (const u of setup.unknown) {
    const check = setup.checks.find((c) => c.id === u.id);
    assert.equal(check?.kind, 'unknown');
    assert.equal(u.text, check?.text);
  }
});

// --- input that is not a selection ---------------------------------------------

test('unknown ids from a URL are ignored and listed', () => {
  const setup = resolve(CATALOGUE, select(['servo', 'warp', 'servo', ''], ['sd', 'gimbal', 'SD', 'sd ', 'constructor', '__proto__']));
  assert.deepEqual(partIds(setup), [...LOCKED, 'servo', 'ssup', 'sd']);
  assert.deepEqual(setup.ignored, ['', 'SD', '__proto__', 'constructor', 'gimbal', 'sd ', 'warp']);
  // Apart from the list, the setup is the one of the known ids alone.
  assert.deepEqual({ ...setup, ignored: [] }, resolve(CATALOGUE, select(['servo'], ['sd'])));
  // A test object as a part you own is no part of a selection: the page maps
  // such an id of a link to the function that drives it before it resolves.
  const object = resolve(CATALOGUE, select([], ['esc', 'servo']));
  assert.deepEqual(partIds(object), LOCKED);
  assert.deepEqual(object.ignored, ['esc', 'servo']);
});

test('a function id is not a part id and a part id is not a function id', () => {
  // "sd" is a part only, "logs" a function only.
  const setup = resolve(CATALOGUE, select(['sd'], ['logs']));
  assert.deepEqual(partIds(setup), LOCKED);
  assert.deepEqual(setup.ignored, ['logs', 'sd']);
  // "servo", "esc", "tap" and "estop" name a function and a part.
  const tap = resolve(CATALOGUE, select([], ['tap']));
  assert.deepEqual(partIds(tap), [...LOCKED, 'tap']);
  assert.equal(fn(tap, 'tap').chosen, false);
});

test('duplicate ids count once', () => {
  const once = resolve(CATALOGUE, select(['servo'], ['sd']));
  const twice = resolve(CATALOGUE, select(['servo', 'servo', 'servo'], ['sd', 'sd']));
  assert.deepEqual(twice, once);
  assert.deepEqual(part(twice, 'servo').addedBy, [{ kind: 'function', id: 'servo' }]);
});

test('option values outside the type fall back to the default and are listed', () => {
  const bad = { functions: ['esc'], owned: [], options: { connection: 'bus', ina228Board: 7, escPower: 'PD' } };
  const setup = resolve(CATALOGUE, bad as unknown as Selection);
  assert.deepEqual(setup.options.map((o) => o.value), ['pads', 'matek', 'battery', 'external', 'outputs']);
  assert.deepEqual(setup.ignored, ['7', 'PD', 'bus']);
  const servo = resolve(CATALOGUE, { functions: ['servo'], owned: [], options: { servoPower: 'usb' } } as unknown as Selection);
  assert.deepEqual(option(servo, 'servoPower'), { key: 'servoPower', value: 'external', visible: true, forcedBy: null });
  assert.deepEqual(servo.ignored, ['usb']);

  // A missing option is the default and is not listed.
  const partial = resolve(CATALOGUE, { functions: [], owned: [], options: { escPower: 'pd' } } as unknown as Selection);
  assert.deepEqual(partial.options.map((o) => o.value), ['pads', 'matek', 'pd', 'external', 'outputs']);
  assert.deepEqual(partial.ignored, []);

  // null is a value outside the type.
  const nulls = resolve(CATALOGUE, { functions: [], owned: [], options: { escPower: null } } as unknown as Selection);
  assert.deepEqual(nulls.options.map((o) => o.value), ['pads', 'matek', 'battery', 'external', 'outputs']);
  assert.deepEqual(nulls.ignored, ['null']);
});

test('input that is not a selection resolves to the base setup without an exception', () => {
  const base = resolve(CATALOGUE, select());
  const hostile = { toString(): string { throw new Error('no'); } };
  for (const input of [
    undefined, null, 0, 'servo', [], {}, { functions: null, owned: null, options: null },
    { functions: 'servo', owned: 'esc', options: 'pd' },
    { functions: { length: 2, 0: 'servo', 1: 'esc' }, owned: 5, options: [] },
  ]) {
    assert.deepEqual(resolve(CATALOGUE, input as unknown as Selection), base);
  }
  const mixed = resolve(CATALOGUE, {
    functions: ['servo', 1, null, undefined, {}, ['esc'], Symbol('x'), hostile, Object.create(null)],
    owned: [true, 'sd', NaN],
    options: { connection: hostile, ina228Board: Symbol('y'), escPower: {}, servoPower: [] },
  } as unknown as Selection);
  assert.deepEqual(partIds(mixed), [...LOCKED, 'servo', 'ssup', 'sd']);
  assert.deepEqual(mixed.ignored, ['', '1', '?', 'NaN', 'Symbol(x)', 'Symbol(y)', '[object Object]', 'esc', 'null', 'true', 'undefined']);
});

// --- purity -------------------------------------------------------------------

test('the resolver changes neither the catalogue nor the selection', () => {
  const catalogue = deepFreeze(structuredClone(CATALOGUE));
  const selection = deepFreeze(select(['stick', 'servo'], ['tap', 'gimbal'], { escPower: 'battery' }));
  const first = resolve(catalogue, selection);
  const second = resolve(catalogue, selection);
  assert.deepEqual(first, second);
  assert.notEqual(first, second);
  assert.notEqual(first.parts, second.parts);
  assert.deepEqual(selection.options, { connection: 'pads', ina228Board: 'matek', escPower: 'battery', servoPower: 'external', servoPort: 'outputs' });
  // DEFAULT_OPTIONS is not the object the resolver writes forced values into.
  assert.deepEqual(DEFAULT_OPTIONS, { connection: 'pads', ina228Board: 'matek', escPower: 'battery', servoPower: 'external', servoPort: 'outputs' });
});

test('output is in catalogue order for any input order', () => {
  const forward = resolve(CATALOGUE, select(FUNCTIONS, SELECTABLE));
  const backward = resolve(CATALOGUE, select([...FUNCTIONS].reverse(), [...SELECTABLE].reverse()));
  assert.deepEqual(backward, forward);
  const order = CATALOGUE.parts.map((p) => p.id);
  const positions = partIds(forward).map((id) => order.indexOf(id));
  assert.deepEqual(positions, [...positions].sort((a, b) => a - b));
  assert.deepEqual(wireNumbers(forward), [...wireNumbers(forward)].sort((a, b) => a - b));
});

test('an empty catalogue resolves to an empty setup', () => {
  const empty: Catalogue = {
    ...CATALOGUE, parts: [], pads: [], wires: [], functions: [], rules: [], checks: [], powerSources: [],
  };
  assert.deepEqual(resolve(empty, select(['servo'], ['esc'])), {
    parts: [], functions: [], wires: [], checks: [], unknown: [], power: [],
    ignored: ['esc', 'servo'],
    options: [
      { key: 'connection', value: 'pads', visible: true, forcedBy: null },
      { key: 'ina228Board', value: 'matek', visible: false, forcedBy: null },
      { key: 'escPower', value: 'battery', visible: false, forcedBy: null },
      { key: 'servoPower', value: 'external', visible: false, forcedBy: null },
      { key: 'servoPort', value: 'outputs', visible: false, forcedBy: null },
    ],
  });
});

// --- conditions -----------------------------------------------------------------

test('conditions: has, opt, not, all, any, and the empty lists', () => {
  const ctx = { has: (id: string): boolean => id === 'esc', options: { ...DEFAULT_OPTIONS } };
  assert.equal(holds(null, ctx), true);
  assert.equal(holds({ has: 'esc' }, ctx), true);
  assert.equal(holds({ has: 'servo' }, ctx), false);
  assert.equal(holds({ opt: 'escPower', is: 'battery' }, ctx), true);
  assert.equal(holds({ opt: 'escPower', is: 'pd' }, ctx), false);
  assert.equal(holds({ not: { has: 'servo' } }, ctx), true);
  assert.equal(holds({ all: [{ has: 'esc' }, { has: 'servo' }] }, ctx), false);
  assert.equal(holds({ any: [{ has: 'esc' }, { has: 'servo' }] }, ctx), true);
  assert.equal(holds({ any: [{ has: 'sd' }, { has: 'servo' }] }, ctx), false);
  // An empty conjunction holds, an empty alternative does not.
  assert.equal(holds({ all: [] }, ctx), true);
  assert.equal(holds({ any: [] }, ctx), false);
});

test('conditions: the walkers list every part and option value', () => {
  const cond = {
    all: [
      { has: 'esc' },
      { not: { any: [{ has: 'servo' }, { opt: 'escPower' as const, is: 'pd' }] } },
      { opt: 'connection' as const, is: 'osoyoo' },
    ],
  };
  assert.deepEqual(partsIn(cond), ['esc', 'servo']);
  assert.deepEqual(optionsIn(cond), [{ opt: 'escPower', is: 'pd' }, { opt: 'connection', is: 'osoyoo' }]);
  assert.deepEqual(partsIn(null), []);
  assert.deepEqual(optionsIn(null), []);
  assert.deepEqual(optionsIn({ has: 'esc' }), []);
  assert.deepEqual(partsIn({ opt: 'escPower', is: 'pd' }), []);

  assert.equal(requiresOption(null, 'escPower', 'pd'), false);
  assert.equal(requiresOption({ opt: 'escPower', is: 'pd' }, 'escPower', 'pd'), true);
  assert.equal(requiresOption({ opt: 'escPower', is: 'battery' }, 'escPower', 'pd'), false);
  assert.equal(requiresOption(cond, 'connection', 'osoyoo'), true);
  // Inside a negation the value is not required.
  assert.equal(requiresOption(cond, 'escPower', 'pd'), false);
  assert.equal(requiresOption({ any: [{ opt: 'escPower', is: 'pd' }, { has: 'esc' }] }, 'escPower', 'pd'), false);
  assert.equal(requiresOption({ any: [{ opt: 'escPower', is: 'pd' }, { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }] }] }, 'escPower', 'pd'), true);
  assert.equal(requiresOption({ any: [] }, 'escPower', 'pd'), false);
  assert.equal(requiresOption({ has: 'esc' }, 'escPower', 'pd'), false);
});

test('the functions that need no part are enabled in every setup and add no part and no wire', () => {
  const empty = resolve(CATALOGUE, select([], []));
  assert.deepEqual(partIds(empty), ['panel', 'copro', 'usb', 'wire']);
  for (const id of BASE) {
    assert.deepEqual({ chosen: fn(empty, id).chosen, enabled: fn(empty, id).enabled, missing: fn(empty, id).missing }, { chosen: false, enabled: true, missing: [] }, id);
  }
  // Adding a part or a function removes none of them.
  for (const part of SELECTABLE) {
    const setup = resolve(CATALOGUE, select([], [part]));
    for (const id of BASE) assert.equal(fn(setup, id).enabled, true, `${id} with ${part}`);
  }
  for (const f of FUNCTIONS) {
    const setup = resolve(CATALOGUE, select([f], []));
    for (const id of BASE) assert.equal(fn(setup, id).enabled, true, `${id} with ${f}`);
  }
  // An id of such a function in the selection changes no part and no wire.
  const named = resolve(CATALOGUE, select(BASE, []));
  assert.deepEqual(partIds(named), partIds(empty));
  assert.deepEqual(named.wires.map((w) => w.n), empty.wires.map((w) => w.n));
  assert.deepEqual(named.ignored, []);
});

test('Osoyoo strip with the INA3221: the 9 strip checks stay and the supply ends on wire 62', () => {
  const both = resolve(CATALOGUE, select(['servo', 'ina3221'], [], { servoPower: 'bench', connection: 'osoyoo' }));
  const ids = both.checks.map((c) => c.id);
  for (const id of ['strip-jumper', 'strip-dc', 'strip-measure', 'strip-feed', 'strip-converter', 'strip-input', 'strip-print', 'strip-regulator', 'strip-supply']) {
    assert.ok(ids.includes(id), id);
  }
  assert.ok(!ids.includes('strip-plug'));
  const numbers = both.wires.map((w) => w.n);
  assert.ok(numbers.includes(62));
  assert.ok(!numbers.includes(61));
});
