// The sweep: invariants 11 to 22 on every subset of the parts you own, on
// every subset of the functions, each under all 16 option combinations, and on
// 2000 mixed selections from a generator with a fixed seed.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Catalogue, Options, Selection, Setup, Wire } from '../../contract.ts';
import { CATALOGUE } from '../catalogue.ts';
import { alwaysOn } from '../groups.ts';
import { OPTION_VALUES, resolve } from '../resolve.ts';

const SEED = 0x16_00_2026;
const MIXED = 2000;
/** Above this count of subsets the sweep is a decision, not a default. */
const SUBSET_LIMIT = 2 ** 16;

const SELECTABLE = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
/** The functions the visitor ticks: those that need a part or drive a test object. */
const FUNCTIONS = CATALOGUE.functions.filter((f) => !alwaysOn(CATALOGUE, f)).map((f) => f.id);
const BASE = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
const TEST_OBJECTS = CATALOGUE.parts.filter((p) => p.testObject === true).map((p) => p.id);
/** Parts no set of parts you own reaches: the test objects and what only they bring. */
const MISSING_OWNED = ['servo', 'ssup', 'dcsup', 'esc', 'batt'];
const LOCKED = CATALOGUE.parts.filter((p) => p.locked).map((p) => p.id);

// The pin of the servo signal is in no selection the page makes: the function
// that programs a servo forces it. The option sets keep the standard outputs.
const OPTION_SETS: Options[] = OPTION_VALUES.connection.flatMap((connection) =>
  OPTION_VALUES.ina228Board.flatMap((ina228Board) =>
    OPTION_VALUES.escPower.flatMap((escPower) =>
      OPTION_VALUES.servoPower.map((servoPower) => ({ connection, ina228Board, escPower, servoPower, servoPort: 'outputs' as const })))));

const PADS = new Map(CATALOGUE.pads.map((p) => [p.n, p]));
const PINS = new Set(CATALOGUE.parts.flatMap((part) => part.pins.map((pin) => `${part.id}.${pin.id}`)));

function subset(ids: string[], mask: number): string[] {
  return ids.filter((_, i) => (mask & (1 << i)) !== 0);
}

/** mulberry32: a 32-bit generator with a fixed sequence per seed. */
function generator(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let x = state;
    x = Math.imul(x ^ (x >>> 15), x | 1);
    x ^= x + Math.imul(x ^ (x >>> 7), x | 61);
    return ((x ^ (x >>> 14)) >>> 0) / 4294967296;
  };
}

function ends(wire: Wire): Array<{ end: string; net: string }> {
  const list = [{ end: wire.a, net: wire.net }];
  if (wire.b !== null) list.push({ end: wire.b, net: wire.netB ?? wire.net });
  return list;
}

function describe(selection: Selection): string {
  return JSON.stringify(selection);
}

/** Invariants 11 to 22 on one selection. Throws an assertion error that names the selection. */
function assertInvariants(catalogue: Catalogue, selection: Selection): Setup {
  const where = describe(selection);

  // 11: no exception.
  let setup: Setup;
  try {
    setup = resolve(catalogue, selection);
  } catch (cause) {
    assert.fail(`resolve throws for ${where}: ${String(cause)}`);
  }

  // 12: deterministic, and independent of the order of the input.
  assert.deepEqual(resolve(catalogue, selection), setup, `two calls differ for ${where}`);
  const reversed: Selection = {
    functions: [...selection.functions].reverse(),
    owned: [...selection.owned].reverse(),
    options: selection.options,
  };
  assert.deepEqual(resolve(catalogue, reversed), setup, `reversed input differs for ${where}`);

  const partIds = setup.parts.map((p) => p.part.id);
  const present = new Set(partIds);
  const options = Object.fromEntries(setup.options.map((o) => [o.key, o.value])) as Options;

  // 13: idempotent on its own output.
  const again = resolve(catalogue, {
    functions: setup.functions.filter((f) => f.chosen).map((f) => f.fn.id),
    owned: setup.parts.filter((p) => p.part.selectable).map((p) => p.part.id),
    options,
  });
  assert.deepEqual(again.parts.map((p) => p.part.id), partIds, `not idempotent for ${where}`);
  assert.deepEqual(again.wires, setup.wires, `wires differ on the second pass for ${where}`);

  // 14: no pin carries two nets.
  const nets = new Map<string, Set<string>>();
  for (const wire of setup.wires) {
    for (const { end, net } of ends(wire)) {
      const set = nets.get(end) ?? new Set<string>();
      set.add(net);
      nets.set(end, set);
    }
  }
  for (const [end, set] of nets) {
    assert.equal(set.size, 1, `${end} carries the nets ${[...set].join(', ')} for ${where}`);
  }

  // 15: no two wires of different nets share a lane of one gutter, or a channel.
  // 2 wires of 1 net may: the 2 halves of the power path through the T-plug.
  const lanes = new Map<string, number>();
  const netOf = new Map(setup.wires.map((w) => [w.n, w.net]));
  const claim = (lane: string, n: number): void => {
    const held = lanes.get(lane);
    assert.ok(held === undefined || netOf.get(held) === netOf.get(n), `wires ${held} and ${n} share ${lane} for ${where}`);
    lanes.set(lane, n);
  };
  for (const wire of setup.wires) {
    const route = wire.route;
    if (route.kind === 'L' || route.kind === 'R' || route.kind === 'O') claim(`${route.kind}${route.lane}`, wire.n);
    if (route.kind === 'X') {
      claim(`L${route.left}`, wire.n);
      claim(`R${route.right}`, wire.n);
      claim(`C${route.channel}`, wire.n);
    }
  }

  // 16: every end is a declared pin of a part in the setup, a pad, or a CAN terminal.
  for (const wire of setup.wires) {
    for (const { end } of ends(wire)) {
      if (end === 'can.H' || end === 'can.L') continue;
      if (end.startsWith('pad.')) {
        assert.ok(PADS.has(Number(end.slice(4))), `wire ${wire.n} ends on ${end} for ${where}`);
        continue;
      }
      assert.ok(PINS.has(end), `wire ${wire.n} ends on the undeclared pin ${end} for ${where}`);
      assert.ok(present.has(end.split('.')[0]!), `wire ${wire.n} ends on ${end}, a part outside the setup, for ${where}`);
    }
  }

  // 17: no GPIO carries two signals.
  const signals = new Map<number, Set<string>>();
  for (const wire of setup.wires) {
    for (const { end, net } of ends(wire)) {
      const gpio = end.startsWith('pad.') ? PADS.get(Number(end.slice(4)))?.gpio : null;
      if (gpio === null || gpio === undefined) continue;
      const set = signals.get(gpio) ?? new Set<string>();
      set.add(net);
      signals.set(gpio, set);
    }
  }
  for (const [gpio, set] of signals) {
    assert.equal(set.size, 1, `GPIO ${gpio} carries ${[...set].join(', ')} for ${where}`);
  }

  // 18: every part has a reason.
  for (const p of setup.parts) {
    assert.ok(p.addedBy.length > 0, `${p.part.id} has no reason for ${where}`);
    assert.equal(p.derived, p.addedBy.some((r) => r.kind !== 'owned'));
    assert.equal(p.ownedTick, selection.owned.includes(p.part.id));
  }

  // 20: locked parts, the wires 1 to 3, and one form of the heartbeat wire.
  for (const id of LOCKED) assert.ok(present.has(id), `${id} is missing for ${where}`);
  const numbers = new Set(setup.wires.map((w) => w.n));
  for (const n of [1, 2, 3]) assert.ok(numbers.has(n), `wire ${n} is missing for ${where}`);
  const direct = numbers.has(4);
  const through = numbers.has(5) && numbers.has(6);
  assert.ok(direct !== through && numbers.has(5) === numbers.has(6), `heartbeat wires 4, 5, 6 for ${where}`);

  // 21: the centre wire of the ESC, and one power wire per supply pin.
  const power = (end: string): number =>
    setup.wires.filter((w) => w.cls === 'power' && (w.a === end || w.b === end)).length;
  if (present.has('esc')) {
    assert.ok(numbers.has(13), `wire 13 is missing for ${where}`);
    assert.equal(power('esc.POS'), 1, `power wires on esc.POS for ${where}`);
    assert.equal(power('esc.NEG'), 1, `power wires on esc.NEG for ${where}`);
  }
  // A servo the bench feeds returns through wire 8 to the IOmcu GND alone.
  const benchFed = setup.wires.some((w) => [59, 60, 61, 62].includes(w.n));
  if (present.has('servo')) {
    // The signal on the standard outputs, wire 7, or on the programming port, wire 64.
    assert.ok(numbers.has(7) === (options.servoPort === 'outputs' && !selection.functions.includes('servoprog')), `wire 7 for ${where}`);
    assert.ok(numbers.has(7) !== numbers.has(64) && numbers.has(8), `wires 7 or 64, and 8 for ${where}`);
    assert.equal(power('servo.POS'), 1, `power wires on servo.POS for ${where}`);
    assert.equal(power('servo.NEG'), benchFed ? 0 : 1, `power wires on servo.NEG for ${where}`);
  } else {
    assert.ok(!benchFed, `a servo supply without a servo for ${where}`);
  }
  // The DC supply of the Osoyoo breakout board, with its 2 leads, exactly while
  // the servo takes the V pin of a strip: the jumper V Selector on 5V.
  const strip = present.has('servo') && !present.has('pdmini') && options.servoPower === 'bench' && options.connection === 'osoyoo';
  assert.equal(present.has('dcsup'), strip, `dcsup for ${where}`);
  assert.equal(numbers.has(65) && numbers.has(66), strip, `wires 65 and 66 for ${where}`);
  assert.equal(numbers.has(65) || numbers.has(66), strip, `wire 65 or 66 for ${where}`);
  if (strip) {
    assert.equal(power('dcsup.POS'), 1, `power wires on dcsup.POS for ${where}`);
    assert.equal(power('dcsup.NEG'), 1, `power wires on dcsup.NEG for ${where}`);
  }

  // 22: no power wire on a pad, but the servo supply on pad 40 (VBUS) under
  // the variant that feeds the servo from the bench; no logic supply on the
  // servo or the ESC.
  for (const wire of setup.wires) {
    for (const { end } of ends(wire)) {
      if (wire.cls === 'power' && !(end === 'pad.40' && options.servoPower === 'bench' && present.has('servo'))) {
        assert.ok(!end.startsWith('pad.'), `power wire ${wire.n} ends on ${end} for ${where}`);
      }
      if (wire.cls === 'logic') {
        assert.ok(!end.startsWith('servo.') && !end.startsWith('esc.'), `logic wire ${wire.n} ends on ${end} for ${where}`);
      }
    }
  }

  // Beyond the numbered invariants: order, functions, the unknown list.
  assert.deepEqual([...numbers], [...numbers].sort((a, b) => a - b), `wire order for ${where}`);
  for (const f of setup.functions) {
    assert.equal(f.chosen, selection.functions.includes(f.fn.id));
    assert.deepEqual(f.missing, f.fn.needs.filter((id) => !present.has(id)));
    if (f.chosen) assert.ok(f.enabled, `${f.fn.id} is chosen and not enabled for ${where}`);
    // A function that drives a test object is enabled only with the test object connected.
    if (f.enabled && f.fn.drives !== undefined) assert.ok(present.has(f.fn.drives), `${f.fn.id} without ${f.fn.drives} for ${where}`);
  }
  // A test object is in the setup exactly while a chosen function drives it.
  for (const id of TEST_OBJECTS) {
    const driven = setup.functions.some((f) => f.chosen && f.fn.drives === id);
    assert.equal(present.has(id), driven, `${id} for ${where}`);
  }
  // No function needs a test object, so no test object is a missing part.
  for (const f of setup.functions) for (const id of f.missing) assert.ok(!TEST_OBJECTS.includes(id), where);
  // The servo plus on a bench pin and the warning of the logic supply go together.
  const plusOnBench = setup.wires.some((w) => w.cls === 'power'
    && ends(w).some(({ end }) => end.startsWith('pad.')) || (w.kind === 'stub' && w.net === 'strip_v'));
  assert.equal(plusOnBench, benchFed, `servo plus on a bench pin for ${where}`);
  assert.equal(setup.checks.some((c) => c.id === 'servo-bench-supply'), benchFed, `servo-bench-supply for ${where}`);
  // The power sources: the logic in every setup, and at least one per connected test object.
  assert.ok(setup.power.some((p) => p.source.feeds === 'logic'), where);
  for (const id of TEST_OBJECTS) {
    assert.equal(setup.power.some((p) => p.source.feeds === id), present.has(id), `power sources of ${id} for ${where}`);
    if (present.has(id)) assert.ok(setup.power.some((p) => p.source.feeds === id && p.used), `no source of ${id} in use for ${where}`);
    // One source per test object is in use, or the sources of one choice.
    const inUse = setup.power.filter((p) => p.source.feeds === id && p.used).map((p) => p.source);
    assert.ok(inUse.length <= 1 || inUse.every((s) => s.choice !== undefined && s.choice === inUse[0]!.choice),
      `sources ${inUse.map((s) => s.id).join(', ')} of ${id} are in use at once for ${where}`);
  }
  // The functions every setup runs are enabled in every setup.
  assert.deepEqual(setup.functions.filter((f) => f.enabled && alwaysOn(catalogue, f.fn)).map((f) => f.fn.id), BASE, where);
  assert.deepEqual(setup.unknown.map((u) => u.id), setup.checks.filter((c) => c.kind === 'unknown').map((c) => c.id));
  assert.deepEqual(setup.ignored, [], where);
  return setup;
}

test('the count of subsets is inside the limit of the sweep', () => {
  assert.equal(SELECTABLE.length, 8);
  assert.equal(FUNCTIONS.length, 12);
  assert.equal(BASE.length, 12);
  assert.deepEqual(TEST_OBJECTS, ['servo', 'esc']);
  assert.equal(OPTION_SETS.length, 16);
  for (const [name, count] of [['parts you own', SELECTABLE.length], ['functions', FUNCTIONS.length]] as const) {
    assert.ok(
      2 ** count <= SUBSET_LIMIT,
      `${count} ${name} give ${2 ** count} subsets, over the limit of ${SUBSET_LIMIT}; choose a sampled sweep or raise the limit`,
    );
  }
});

test('sweep: every subset of the parts you own, under every option combination', () => {
  let count = 0;
  const partSets = new Set<string>();
  for (const options of OPTION_SETS) {
    for (let mask = 0; mask < 2 ** SELECTABLE.length; mask += 1) {
      const setup = assertInvariants(CATALOGUE, { functions: [], owned: subset(SELECTABLE, mask), options });
      partSets.add(setup.parts.map((p) => p.part.id).join(','));
      count += 1;
    }
  }
  assert.equal(count, 4096);
  // Every part of the catalogue is in at least one setup, but the test
  // objects: no part you own connects one.
  const seen = new Set([...partSets].flatMap((ids) => ids.split(',')));
  assert.deepEqual(CATALOGUE.parts.map((p) => p.id).filter((id) => !seen.has(id)), MISSING_OWNED);
});

test('sweep: every subset of the functions, under every option combination', () => {
  let count = 0;
  const wires = new Set<number>();
  const parts = new Set<string>();
  for (const options of OPTION_SETS) {
    for (let mask = 0; mask < 2 ** FUNCTIONS.length; mask += 1) {
      const setup = assertInvariants(CATALOGUE, { functions: subset(FUNCTIONS, mask), owned: [], options });
      for (const wire of setup.wires) wires.add(wire.n);
      for (const p of setup.parts) parts.add(p.part.id);
      count += 1;
    }
  }
  assert.equal(count, 65536);
  // Every part of the catalogue is in at least one setup that functions alone reach.
  assert.deepEqual(CATALOGUE.parts.map((p) => p.id).filter((id) => !parts.has(id)), []);
  // Every wire of the catalogue is in at least one setup that functions alone reach.
  assert.deepEqual(CATALOGUE.wires.map((w) => w.n).filter((n) => !wires.has(n)), []);
});

test(`sweep: ${MIXED} mixed selections from the generator with seed ${SEED}`, () => {
  const next = generator(SEED);
  const pick = <T>(list: ReadonlyArray<T>): T => list[Math.floor(next() * list.length)]!;
  const seen = new Set<string>();
  for (let i = 0; i < MIXED; i += 1) {
    const density = next();
    const selection: Selection = {
      functions: FUNCTIONS.filter(() => next() < density),
      owned: SELECTABLE.filter(() => next() < 1 - density),
      options: pick(OPTION_SETS),
    };
    seen.add(describe(selection));
    assertInvariants(CATALOGUE, selection);
  }
  // The generator does not collapse onto a few selections.
  assert.ok(seen.size > MIXED * 0.9, `${seen.size} different selections`);
});

test('the generator gives the same sequence for the same seed', () => {
  const a = generator(SEED);
  const b = generator(SEED);
  const first = Array.from({ length: 5 }, () => a());
  assert.deepEqual(Array.from({ length: 5 }, () => b()), first);
  assert.ok(first.every((x) => x >= 0 && x < 1));
  assert.notDeepEqual(Array.from({ length: 5 }, generator(SEED + 1)), first);
});

// The invariants bite: each of these changes to a copy of the data fails the sweep.

function failsSweep(edit: (c: Catalogue) => void, pattern: RegExp): void {
  const c = structuredClone(CATALOGUE);
  edit(c);
  assert.throws(() => {
    for (const options of OPTION_SETS) {
      for (let mask = 0; mask < 2 ** SELECTABLE.length; mask += 1) {
        assertInvariants(c, { functions: [], owned: subset(SELECTABLE, mask), options });
      }
      for (let mask = 0; mask < 2 ** FUNCTIONS.length; mask += 1) {
        assertInvariants(c, { functions: subset(FUNCTIONS, mask), owned: [], options });
      }
    }
  }, pattern);
}

test('the sweep reports a pin with two nets', () => {
  failsSweep((c) => { c.wires[20]!.net = 'pd_neg'; }, /servo\.NEG carries the nets/);
});

test('the sweep reports two wires in one lane', () => {
  failsSweep((c) => { c.wires[7]!.route = { kind: 'L', lane: 5 }; }, /wires 7 and 8 share L5/);
  failsSweep((c) => { c.wires[29]!.route = { kind: 'X', left: 13, right: 17, channel: 0 }; }, /share C0/);
});

test('the sweep reports a wire that ends on a part outside the setup', () => {
  // Wire 9 without its condition on the servo supply.
  failsSweep((c) => { c.wires[8]!.when = { has: 'servo' }; }, /wire 9 ends on ssup\.POS, a part outside the setup|servo\.POS/);
  failsSweep((c) => { c.wires[51]!.b = 'tap.GATE'; }, /undeclared pin tap\.GATE/);
});

test('the sweep reports a power wire on a pad and a logic supply on the ESC', () => {
  failsSweep((c) => { c.wires[23]!.cls = 'power'; }, /power wire 24 ends on pad\.36/);
  failsSweep((c) => { c.wires[23]!.b = 'esc.BEC'; }, /logic wire 24 ends on esc\.BEC|part outside the setup/);
});

test('the sweep reports a servo fed from the bench without the warning, and a plus on a pad under the external supply', () => {
  failsSweep((c) => { c.checks = c.checks.filter((x) => x.id !== 'servo-bench-supply'); }, /servo-bench-supply/);
  failsSweep((c) => { c.checks.find((x) => x.id === 'servo-bench-supply')!.when = { has: 'servo' }; }, /servo-bench-supply/);
  // Wire 59 without its condition on the variant.
  failsSweep((c) => { c.wires[58]!.when = { has: 'servo' }; }, /power wire 59 ends on pad\.40|servo\.POS/);
  failsSweep((c) => { c.powerSources = c.powerSources.filter((s) => s.feeds !== 'esc'); }, /power sources of esc|no source of esc/);
  failsSweep((c) => { c.powerSources = c.powerSources.filter((s) => s.id !== 'copro-5v'); }, /no source of servo in use/);
  failsSweep((c) => { delete c.powerSources.find((s) => s.id === 'own-supply')!.choice; }, /sources lipo-2s, own-supply of servo are in use at once/);
});

test('the sweep reports the DC supply of the Osoyoo breakout board outside the strip setups, or missing in them', () => {
  // Without the rule the supply is missing where the servo takes the V pin.
  failsSweep((c) => { c.rules = c.rules.filter((r) => r.id !== 'osoyoo-dc'); }, /dcsup for |part outside the setup/);
  // On every Osoyoo setup with a servo, the external supply included.
  failsSweep((c) => {
    c.rules.find((r) => r.id === 'osoyoo-dc')!.when = { all: [{ has: 'servo' }, { opt: 'connection', is: 'osoyoo' }] };
  }, /dcsup for /);
  // With the PD mini in the setup the V pin stays free.
  failsSweep((c) => {
    c.rules.find((r) => r.id === 'osoyoo-dc')!.when = { all: [{ has: 'servo' }, { opt: 'servoPower', is: 'bench' }, { opt: 'connection', is: 'osoyoo' }] };
  }, /dcsup for /);
  // A lead that stays when the supply leaves, and a lead that is missing.
  failsSweep((c) => { c.wires.find((w) => w.n === 66)!.when = { has: 'servo' }; }, /wire 65 or 66 for |part outside the setup/);
  failsSweep((c) => { c.wires = c.wires.filter((w) => w.n !== 65); }, /wires 65 and 66 for /);
});

test('the sweep reports a second power wire on a supply pin and a missing centre wire', () => {
  failsSweep((c) => { c.wires[13]!.when = { has: 'esc' }; }, /esc\.POS/);
  failsSweep((c) => { c.wires[12]!.when = { all: [{ has: 'esc' }, { has: 'sim' }] }; }, /wire 13 is missing/);
});

test('the sweep reports both forms of the heartbeat wire in one setup', () => {
  failsSweep((c) => { c.wires[3]!.when = null; }, /pad\.5 carries the nets|heartbeat wires/);
  failsSweep((c) => { c.wires[5]!.when = { all: [{ has: 'estop' }, { has: 'sd' }] }; }, /heartbeat wires/);
});

test('the sweep reports a rule order that needs a second pass', () => {
  failsSweep((c) => { c.rules.reverse(); }, /not idempotent|part outside the setup/);
});
