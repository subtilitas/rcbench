// Groups, removal of a part and deselection of a function: on fixture
// catalogues at the edges, and on the data file in a sweep.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Catalogue, Cond, Fn, Options, Part, Rule, Selection, Setup, Src } from '../../contract.ts';
import { CATALOGUE } from '../catalogue.ts';
import {
  alwaysOn, baseParts, deselectEffect, groups, plainDrive, removalEffect, removalIsPlain, testObjects,
} from '../groups.ts';
import type { Removal } from '../groups.ts';
import { DEFAULT_OPTIONS, OPTION_VALUES, resolve } from '../resolve.ts';

const SRC: Src = { kind: 'page' };
const text = (s: string) => ({ en: s, de: s });

function part(id: string, flags: Partial<Part> = {}): Part {
  return {
    id, name: text(id), short: text(id), role: null, locked: false, selectable: true, release: 'released',
    slot: { column: 'none', order: 0 }, pins: [], buy: [], src: SRC, ...flags,
  };
}

function fn(id: string, needs: string[], forces?: Partial<Options>, drives?: string): Fn {
  return {
    id, group: 'bench', name: text(id), needs, src: SRC, ...(forces ? { forces } : {}),
    ...(drives === undefined ? {} : { drives }),
  };
}

/** A test object: what a visitor connects and tests. */
function object(id: string): Part {
  return part(id, { selectable: false, testObject: true });
}

function rule(id: string, when: Cond, adds: string): Rule {
  return { id, when, adds, because: text(id), src: SRC };
}

function catalogue(parts: Part[], functions: Fn[], rules: Rule[] = []): Catalogue {
  return {
    release: '0.0.0', hashVersion: 1, parts, pads: [], wires: [], functions, rules, checks: [], stages: [],
    safety: [], notInRelease: [], baseFunction: text('base'), pinDefaults: [], powerSources: [],
  };
}

function select(functions: string[] = [], owned: string[] = [], options: Partial<Options> = {}): Selection {
  return { functions, owned, options: { ...DEFAULT_OPTIONS, ...options } };
}

const BASE = [part('panel', { locked: true, selectable: false }), part('copro', { locked: true, selectable: false })];

const partIds = (setup: Setup): string[] => setup.parts.map((p) => p.part.id);
const on = (setup: Setup): string[] => setup.functions.filter((f) => f.enabled).map((f) => f.fn.id);
const off = (effect: Removal): string[] => effect.functionsOff.map((f) => f.id);

// The pin of the servo signal is in no selection the page makes: the function
// that programs a servo forces it. The option sets keep the standard outputs.
const OPTION_SETS: Options[] = OPTION_VALUES.connection.flatMap((connection) =>
  OPTION_VALUES.ina228Board.flatMap((ina228Board) =>
    OPTION_VALUES.escPower.flatMap((escPower) =>
      OPTION_VALUES.servoPower.map((servoPower) => ({ connection, ina228Board, escPower, servoPower, servoPort: 'outputs' as const })))));

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

test('groups: one group per set of parts beyond the base setup, the set without a part first', () => {
  const c = catalogue(
    [...BASE, part('a'), part('b'), part('c')],
    [
      fn('ab', ['a', 'b']), fn('c1', ['c']), fn('none', []), fn('a1', ['a']), fn('ba', ['b', 'a', 'b']),
      fn('base', ['panel', 'copro']), fn('a2', ['copro', 'a']), fn('abc', ['c', 'b', 'a']), fn('bc', ['b', 'c']),
    ],
  );
  const g = groups(c);
  assert.deepEqual(g.map((x) => x.key), ['', 'a', 'c', 'a+b', 'b+c', 'a+b+c']);
  assert.deepEqual(g.map((x) => x.functions), [['none', 'base'], ['a1', 'a2'], ['c1'], ['ab', 'ba'], ['bc'], ['abc']]);
  assert.deepEqual(g.map((x) => x.parts), [[], ['a'], ['c'], ['a', 'b'], ['b', 'c'], ['a', 'b', 'c']]);
  for (const x of g) assert.deepEqual([x.variants, x.also, x.oneOf], [[], [], []], x.key);
  assert.deepEqual(baseParts(c), ['panel', 'copro']);
});

test('groups: a catalogue without a function that needs no part has no first group, one without functions has none', () => {
  const c = catalogue([...BASE, part('a')], [fn('a1', ['a'])]);
  assert.deepEqual(groups(c).map((x) => x.key), ['a']);
  assert.deepEqual(groups(catalogue(BASE, [])), []);
  // Functions that need no part only.
  assert.deepEqual(groups(catalogue(BASE, [fn('x', []), fn('y', [])])).map((x) => [x.key, x.functions]), [['', ['x', 'y']]]);
});

test('groups: a part id the catalogue does not hold stays in the key and sorts last', () => {
  const c = catalogue([...BASE, part('a'), part('b')], [fn('ghost', ['nope']), fn('ghost2', ['nope', 'a']), fn('b1', ['b']), fn('ab', ['a', 'b'])]);
  const g = groups(c);
  assert.deepEqual(g.map((x) => x.key), ['b', 'nope', 'a+b', 'a+nope']);
  assert.deepEqual(g.map((x) => x.functions), [['b1'], ['ghost'], ['ab'], ['ghost2']]);
});

test('groups: a part that is one of several boards is named as a variant', () => {
  const two = part('sense', { variantName: { option: 'ina228Board', names: { matek: text('M'), xt90: text('X') } } });
  const one = part('board', { variantName: { option: 'connection', names: { osoyoo: text('O') } } });
  const g = groups(catalogue([...BASE, two, one], [fn('f', ['sense', 'board'])]));
  assert.deepEqual(g[0]?.variants, [{ part: 'sense', option: 'ina228Board', values: ['matek', 'xt90'] }]);
});

test('groups: parts the rules add are "also" under every variant and "one of" under some', () => {
  const c = catalogue(
    [...BASE, part('esc'), part('batt', { selectable: false }), part('pd'), part('plug', { selectable: false }), part('fuse', { selectable: false })],
    [fn('run', ['esc']), fn('prog', ['esc', 'pd'], { escPower: 'pd' }), fn('feed', ['pd'])],
    [
      rule('fuse', { has: 'esc' }, 'fuse'),
      rule('battery', { all: [{ has: 'esc' }, { opt: 'escPower', is: 'battery' }] }, 'batt'),
      rule('pd', { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }] }, 'pd'),
      rule('plug', { has: 'pd' }, 'plug'),
    ],
  );
  const g = groups(c);
  assert.deepEqual(g.map((x) => x.key), ['esc', 'pd', 'esc+pd']);
  assert.deepEqual([g[0]?.also, g[0]?.oneOf], [['fuse'], [['batt'], ['pd', 'plug']]]);
  assert.deepEqual([g[1]?.also, g[1]?.oneOf], [['plug'], []]);
  // The function forces variant B, so the battery is no alternative of its group.
  assert.deepEqual([g[2]?.also, g[2]?.oneOf], [['plug', 'fuse'], []]);
});

test('groups: a variant that adds no part is an empty alternative', () => {
  const c = catalogue(
    [...BASE, part('esc'), part('batt', { selectable: false })],
    [fn('run', ['esc'])],
    [rule('battery', { all: [{ has: 'esc' }, { opt: 'escPower', is: 'battery' }] }, 'batt')],
  );
  assert.deepEqual(groups(c)[0]?.oneOf, [['batt'], []]);
});

test('groups of the data file: every function is in exactly one group, keyed by its needs', () => {
  const g = groups(CATALOGUE);
  const base = new Set(baseParts(CATALOGUE));
  const order = CATALOGUE.parts.map((p) => p.id);
  const seen = g.flatMap((x) => x.functions);
  assert.deepEqual([...seen].sort(), CATALOGUE.functions.map((f) => f.id).sort());
  assert.equal(new Set(seen).size, seen.length, 'a function is in two groups');
  assert.equal(new Set(g.map((x) => x.key)).size, g.length, 'two groups share a key');
  for (const x of g) {
    assert.ok(x.functions.length > 0, x.key);
    assert.deepEqual([...x.parts].sort((a, b) => order.indexOf(a) - order.indexOf(b)), x.parts, `${x.key}: catalogue order`);
    for (const id of x.functions) {
      const f = CATALOGUE.functions.find((y) => y.id === id) as Fn;
      assert.deepEqual([...new Set(f.needs)].filter((p) => !base.has(p)).sort(), [...x.parts].sort(), id);
    }
    for (const id of [...x.also, ...x.oneOf.flat()]) {
      assert.ok(order.includes(id) && !base.has(id) && !x.parts.includes(id), `${x.key}: ${id}`);
    }
    for (const alt of x.oneOf) for (const id of alt) assert.ok(!x.also.includes(id), `${x.key}: ${id} is in both lists`);
    assert.notEqual(x.oneOf.length, 1, x.key);
  }
  // Order: count of parts, then catalogue position of the parts.
  const rank = (x: (typeof g)[number]): number[] => [x.parts.length, ...x.parts.map((p) => order.indexOf(p))];
  for (let i = 1; i < g.length; i++) {
    const [a, b] = [rank(g[i - 1] as (typeof g)[number]), rank(g[i] as (typeof g)[number])];
    const at = a.findIndex((v, k) => v !== b[k]);
    assert.ok(at >= 0 && (a[at] as number) < (b[at] as number), `${g[i - 1]?.key} stands before ${g[i]?.key}`);
  }
  // The functions that need no part form the first group when the data holds such functions.
  const none = CATALOGUE.functions.filter((f) => f.needs.every((p) => base.has(p))).map((f) => f.id);
  if (none.length > 0) assert.deepEqual([g[0]?.key, g[0]?.functions], ['', none]);
  else assert.ok(g.every((x) => x.parts.length > 0));
  // The INA228 board is one of 2 boards, and an ESC takes its power from one of 2 sources.
  const escpow = g.find((x) => x.functions.includes('escpow'));
  assert.deepEqual(escpow?.variants, [{ part: 'ina228', option: 'ina228Board', values: ['matek', 'xt90'] }]);
  assert.deepEqual(escpow?.oneOf, [['batt'], ['pdmini', 'charger', 'usbtap']]);
  // A servo takes its power from the servo supply or, under the other variant, from the bench:
  // the 5 V pin with no further part, or the V pin of the Osoyoo breakout board with its DC supply.
  assert.deepEqual(g.find((x) => x.key === 'ina3221')?.oneOf, [['ssup'], [], ['dcsup']]);
  // No group names a test object: not as a part it needs, not as a part the rules add.
  const objects = testObjects(CATALOGUE);
  assert.deepEqual(objects, ['servo', 'esc']);
  for (const x of g) {
    for (const id of [...x.key.split('+'), ...x.parts, ...x.also, ...x.oneOf.flat(), ...x.variants.map((v) => v.part)]) {
      assert.ok(!objects.includes(id), `${x.key}: ${id}`);
    }
  }
  assert.deepEqual(g.map((x) => x.key), ['', 'pdmini', 'ina3221', 'as5600', 'ina228', 'sd', 'estop', 'sim+tap']);
  // The first group: the 10 functions every setup runs, then the 3 that drive a test object on the base setup.
  assert.deepEqual(g[0]?.functions.filter((id) => !alwaysOn(CATALOGUE, CATALOGUE.functions.find((f) => f.id === id) as Fn)), ['servo', 'servoprog', 'esc']);
  assert.equal(g[0]?.functions.length, 13);
  assert.deepEqual([plainDrive(CATALOGUE, 'servo'), plainDrive(CATALOGUE, 'esc'), plainDrive(CATALOGUE, 'sd')], ['servo', 'esc', null]);
  assert.deepEqual(g.find((x) => x.key === 'pdmini')?.functions, ['servotest', 'stick', 'supply']);
});

// ---------------------------------------------------------------------------
// Test objects on fixtures
// ---------------------------------------------------------------------------

const RIG = catalogue(
  [
    ...BASE, object('servo'), object('esc'), part('meter'), part('horn'), part('feed', { selectable: false }),
    part('pack', { selectable: false }),
  ],
  [
    fn('ui', []), fn('move', [], undefined, 'servo'), fn('current', ['meter'], undefined, 'servo'),
    fn('both', ['meter', 'horn'], undefined, 'servo'), fn('spin', [], undefined, 'esc'),
    fn('thrust', ['horn'], undefined, 'esc'), fn('read', ['meter']),
  ],
  [rule('feed', { has: 'servo' }, 'feed'), rule('pack', { has: 'esc' }, 'pack')],
);

test('test objects: a function with a test object only is in the group without a part, and is ticked', () => {
  const g = groups(RIG);
  assert.deepEqual(g.map((x) => [x.key, x.functions]), [
    ['', ['ui', 'move', 'spin']], ['meter', ['current', 'read']], ['horn', ['thrust']], ['meter+horn', ['both']],
  ]);
  // No group lists a test object, also not as a part the rules add next to it.
  assert.deepEqual(g.map((x) => [x.parts, x.also, x.oneOf]), [
    [[], [], [[], ['feed'], ['pack']]], [['meter'], [], [['feed'], []]], [['horn'], ['pack'], []], [['meter', 'horn'], ['feed'], []],
  ]);
  assert.deepEqual(testObjects(RIG), ['servo', 'esc']);
  assert.deepEqual(RIG.functions.filter((f) => alwaysOn(RIG, f)).map((f) => f.id), ['ui']);
  assert.deepEqual([plainDrive(RIG, 'servo'), plainDrive(RIG, 'esc'), plainDrive(RIG, 'meter'), plainDrive(RIG, 'nope')], ['move', 'spin', null, null]);

  // Not ticked, the function is off: the test object is not connected. It misses no part.
  const none = resolve(RIG, select());
  assert.deepEqual(on(none), ['ui']);
  assert.deepEqual(none.functions.find((f) => f.fn.id === 'move')?.missing, []);
  assert.deepEqual(partIds(none), ['panel', 'copro']);
  // Ticked, it connects the test object; the rules read it as a part of the setup.
  const moved = resolve(RIG, select(['move']));
  assert.deepEqual(partIds(moved), ['panel', 'copro', 'servo', 'feed']);
  assert.deepEqual(on(moved), ['ui', 'move']);
});

test('test objects: a test object and 1 part, and 2 test objects in one setup', () => {
  // The part alone enables the function that needs no test object only.
  const meter = resolve(RIG, select([], ['meter']));
  assert.deepEqual(on(meter), ['ui', 'read']);
  assert.deepEqual(meter.functions.find((f) => f.fn.id === 'current')?.missing, []);
  // With the test object connected the part enables the function that drives it.
  const one = resolve(RIG, select(['current']));
  assert.deepEqual(partIds(one), ['panel', 'copro', 'servo', 'meter', 'feed']);
  assert.deepEqual(on(one), ['ui', 'move', 'current', 'read']);
  const two = resolve(RIG, select(['move', 'thrust']));
  assert.deepEqual(partIds(two), ['panel', 'copro', 'servo', 'esc', 'horn', 'feed', 'pack']);
  assert.deepEqual(on(two), ['ui', 'move', 'spin', 'thrust']);
  // A test object is no part a visitor owns: the id is ignored.
  const owned = resolve(RIG, select([], ['servo', 'esc', 'horn']));
  assert.deepEqual([partIds(owned), on(owned), owned.ignored], [['panel', 'copro', 'horn'], ['ui'], ['esc', 'servo']]);
});

test('test objects: a test object is not removable, and stays when a part of its function leaves', () => {
  for (const id of ['servo', 'esc']) {
    const refused = removalEffect(RIG, select(['move', 'spin']), id);
    assert.equal(refused.refused, 'testObject', id);
    assert.deepEqual(refused.selection, select(['move', 'spin']), id);
    assert.ok(!removalIsPlain(refused));
  }
  // Refused also where the test object is not in the setup.
  assert.equal(removalEffect(RIG, select(), 'servo').refused, 'testObject');

  // The meter leaves: its function goes off, the servo stays through the function that drives it alone.
  const meter = removalEffect(RIG, select(['current']), 'meter');
  assert.equal(meter.refused, null);
  assert.deepEqual(meter.selection, select(['move']));
  assert.deepEqual([off(meter), meter.partsOut, meter.partsIn, meter.partsKept, meter.objectsOut], [['current', 'read'], [], [], [], []]);
  assert.deepEqual(partIds(resolve(RIG, meter.selection)), ['panel', 'copro', 'servo', 'feed']);
  // 2 test objects, 1 shared part: both stay, the other part of a function stays as an owned part.
  const horn = removalEffect(RIG, select(['both', 'thrust']), 'horn');
  assert.deepEqual(horn.selection, select(['move', 'spin'], ['meter']));
  assert.deepEqual([off(horn), horn.partsOut, horn.partsKept, horn.objectsOut], [['both', 'thrust'], [], ['meter'], []]);
  assert.deepEqual(on(resolve(RIG, horn.selection)), ['ui', 'move', 'current', 'spin', 'read']);
  // A part the rules add for the test object is held: no removal of bench hardware ends the rule.
  assert.equal(removalEffect(RIG, select(['move']), 'feed').refused, 'held');
});

test('test objects: without a function that drives it alone, the test object leaves with the function', () => {
  const c = catalogue(
    [...BASE, object('servo'), part('meter'), part('feed', { selectable: false })],
    [fn('current', ['meter'], undefined, 'servo'), fn('read', ['meter'])],
    [rule('feed', { has: 'servo' }, 'feed')],
  );
  assert.equal(plainDrive(c, 'servo'), null);
  const effect = removalEffect(c, select(['current']), 'meter');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select());
  assert.deepEqual([off(effect), effect.partsOut, effect.objectsOut], [['current', 'read'], ['feed'], ['servo']]);
  assert.ok(!removalIsPlain(effect));
  // An owned part that enables nothing leaves alone, and the removal is plain.
  const plain = removalEffect(catalogue([...BASE, object('servo'), part('meter')], [fn('m', ['meter'], undefined, 'servo')]), select([], ['meter']), 'meter');
  assert.deepEqual([plain.refused, plain.functionsOff, plain.objectsOut], [null, [], []]);
  assert.ok(removalIsPlain(plain));
});

test('test objects: a removal adds the tick of the plain drive only when no function that stays drives the test object', () => {
  const pick = (functions: string[]): Selection => ({ functions, owned: [], options: { ...DEFAULT_OPTIONS } }) as Selection;
  const after = (functions: string[], gone: string): string[] => removalEffect(CATALOGUE, pick(functions), gone).selection.functions;
  // The angle check stays and drives the servo: no further tick.
  assert.deepEqual(after(['servotest', 'angle'], 'pdmini'), ['angle']);
  // The test alone: the servo stays connected by the tick of the plain drive.
  assert.deepEqual(after(['servotest'], 'pdmini'), ['servo']);
  // A tick of the plain drive that stands before stays as it is.
  assert.deepEqual(after(['servo', 'servotest', 'angle'], 'pdmini'), ['servo', 'angle']);
  // Both functions go: 1 tick for the servo.
  assert.deepEqual(after(['servocur', 'angle'], 'as5600').includes('servo'), false);
  // 2 test objects: the function that stays drives the servo, none drives the ESC.
  const two = after(['angle', 'escpow'], 'ina228');
  assert.deepEqual(two, ['angle', 'esc']);
  for (const functions of [['servotest', 'angle'], ['servotest'], ['angle', 'escpow']]) {
    const gone = functions.includes('escpow') ? 'ina228' : 'pdmini';
    const effect = removalEffect(CATALOGUE, pick(functions), gone);
    assert.deepEqual(effect.objectsOut, [], functions.join());
    const setup = resolve(CATALOGUE, effect.selection);
    for (const id of testObjects(CATALOGUE)) {
      const before = resolve(CATALOGUE, pick(functions)).parts.some((r) => r.part.id === id);
      assert.equal(setup.parts.some((r) => r.part.id === id), before, `${id} after ${functions.join()}`);
    }
  }
});

test('removal: the Osoyoo DC supply leaves by servo power A, and the servo supply comes in', () => {
  // The rule osoyoo-dc reads servoPower: bench. The smallest change that
  // releases dcsup is servo power A, which adds the own servo supply.
  for (const functions of [['servo'], ['servocur'], ['servoprog']]) {
    const selection = select(functions, [], { connection: 'osoyoo', servoPower: 'bench' });
    const what = functions.join();
    assert.ok(resolve(CATALOGUE, selection).parts.some((r) => r.part.id === 'dcsup'), `dcsup in ${what}`);
    const effect = removalEffect(CATALOGUE, selection, 'dcsup');
    assert.equal(effect.refused, null, what);
    assert.deepEqual(effect.options, [{ key: 'servoPower', from: 'bench', to: 'external' }], what);
    assert.deepEqual(effect.partsIn, ['ssup'], what);
    assert.deepEqual([effect.functionsOff, effect.functionsOn, effect.partsOut, effect.objectsOut], [[], [], [], []], what);
    assert.deepEqual(effect.selection.functions, functions, what);
    assert.equal(effect.selection.options.connection, 'osoyoo', what);
    assert.equal(effect.selection.options.servoPower, 'external', what);
    const after = resolve(CATALOGUE, effect.selection).parts.map((r) => r.part.id);
    assert.ok(!after.includes('dcsup') && after.includes('ssup'), `${what}: ${after.join()}`);
  }
});

test('test objects: deselecting the last function that drives one disconnects it', () => {
  const last = deselectEffect(RIG, select(['current']), 'current');
  assert.deepEqual(last.selection, select());
  assert.deepEqual([last.stillOn, last.functionsOff, last.partsOut, last.partsStay, last.objectsOut], [false, ['move', 'read'], ['meter', 'feed'], [], ['servo']]);
  // Another function still drives it: the function goes off for its part, the test object stays.
  const other = deselectEffect(RIG, select(['current', 'move']), 'current');
  assert.deepEqual([other.stillOn, other.functionsOff, other.partsOut, other.objectsOut], [false, ['read'], ['meter'], []]);
  // The function that drives it alone, next to one with a part: it stays on.
  const plain = deselectEffect(RIG, select(['current', 'move']), 'move');
  assert.deepEqual([plain.stillOn, plain.functionsOff, plain.partsOut, plain.partsStay, plain.objectsOut], [true, [], [], [], []]);
  // The only function: it goes off with the test object and the part the rules add for it.
  const only = deselectEffect(RIG, select(['spin']), 'spin');
  assert.deepEqual([only.stillOn, only.functionsOff, only.partsOut, only.objectsOut], [false, [], ['pack'], ['esc']]);
  // A function that is not chosen changes nothing.
  assert.deepEqual(deselectEffect(RIG, select(['spin']), 'move').objectsOut, []);
});

// ---------------------------------------------------------------------------
// removalEffect on fixtures
// ---------------------------------------------------------------------------

const SHOP = catalogue(
  [
    ...BASE,
    part('servo'), part('meter'), part('horn'), part('card'), part('supply', { selectable: false }), part('pd'),
    part('jig', { selectable: false }),
  ],
  [fn('move', ['servo']), fn('current', ['servo', 'meter']), fn('angle', ['servo', 'horn']), fn('log', ['card']), fn('hold', ['servo', 'jig'])],
  [rule('servo-supply', { all: [{ has: 'servo' }, { not: { has: 'pd' } }] }, 'supply')],
);

test('removal: a part nothing needs leaves alone', () => {
  const effect = removalEffect(SHOP, select([], ['card', 'pd']), 'pd');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select([], ['card']));
  assert.deepEqual([effect.functionsOff, effect.partsOut, effect.partsIn, effect.partsKept, effect.options], [[], [], [], [], []]);
  assert.ok(removalIsPlain(effect));
});

test('removal: a part selected under "parts you own" takes the functions it enables with it', () => {
  const effect = removalEffect(SHOP, select([], ['card', 'servo']), 'card');
  assert.deepEqual(effect.selection, select([], ['servo']));
  assert.deepEqual(effect.functionsOff, [{ id: 'log', chosen: false }]);
  assert.ok(!removalIsPlain(effect));
});

test('removal: a part shared by 2 chosen functions takes both, their other parts stay as owned', () => {
  const effect = removalEffect(SHOP, select(['current', 'angle', 'log']), 'servo');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select(['log'], ['meter', 'horn']));
  assert.deepEqual(effect.functionsOff, [
    { id: 'move', chosen: false }, { id: 'current', chosen: true }, { id: 'angle', chosen: true },
  ]);
  assert.deepEqual(effect.partsOut, ['supply']);
  assert.deepEqual(effect.partsKept, ['meter', 'horn']);
  assert.deepEqual(partIds(resolve(SHOP, effect.selection)), ['panel', 'copro', 'meter', 'horn', 'card']);
});

test('removal: a shared part goes, the function that does not need it stays chosen', () => {
  const effect = removalEffect(SHOP, select(['current', 'angle']), 'meter');
  assert.deepEqual(effect.selection, select(['angle'], ['servo']));
  assert.deepEqual(off(effect), ['current']);
  assert.deepEqual([effect.partsOut, effect.partsIn, effect.partsKept], [[], [], ['servo']]);
});

test('removal: an alternative part still satisfies the need', () => {
  // The PD module feeds the servo. Without it the rule brings the servo supply.
  const effect = removalEffect(SHOP, select(['move'], ['pd']), 'pd');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select(['move']));
  assert.deepEqual([effect.functionsOff, effect.partsOut, effect.partsIn], [[], [], ['supply']]);
  assert.deepEqual(on(resolve(SHOP, effect.selection)), ['move']);
  assert.ok(!removalIsPlain(effect));
});

test('removal: a part of the base setup, an unknown id and an absent part are refused', () => {
  const selection = select(['move'], ['card']);
  for (const [id, why] of [['panel', 'base'], ['copro', 'base'], ['warp', 'unknown'], ['', 'unknown'], ['horn', 'absent']] as const) {
    const effect = removalEffect(SHOP, selection, id);
    assert.equal(effect.refused, why, id);
    assert.deepEqual(effect.selection, select(['move'], ['card']), id);
    assert.deepEqual([effect.functionsOff, effect.partsOut, effect.partsIn, effect.partsKept, effect.options], [[], [], [], [], []], id);
    assert.ok(!removalIsPlain(effect), id);
  }
});

test('removal: a part added by a rule leaves with the part that needs it', () => {
  const effect = removalEffect(SHOP, select(['current'], ['card']), 'supply');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select([], ['meter', 'card']));
  assert.deepEqual(off(effect), ['move', 'current']);
  assert.deepEqual(effect.partsOut, ['servo']);
  assert.deepEqual(effect.partsKept, ['meter']);
});

test('removal: a needed part no visitor can own leaves with its function', () => {
  const effect = removalEffect(SHOP, select(['hold']), 'servo');
  assert.deepEqual(effect.selection, select());
  assert.deepEqual(effect.partsOut, ['supply', 'jig']);
  assert.deepEqual(effect.partsKept, []);
  const other = removalEffect(SHOP, select(['hold']), 'jig');
  assert.deepEqual(other.selection, select([], ['servo']));
  assert.deepEqual([off(other), other.partsOut, other.partsKept], [['hold'], [], ['servo']]);
});

test('removal: the selection given is not written to, and ids it does not know are dropped', () => {
  const selection = Object.freeze({
    functions: Object.freeze(['log', 'warp', 'move', 'log']) as unknown as string[],
    owned: Object.freeze(['pd', 'supply', 'nope']) as unknown as string[],
    options: Object.freeze({ ...DEFAULT_OPTIONS, escPower: 'fusion' }) as unknown as Options,
  });
  const effect = removalEffect(SHOP, selection, 'card');
  assert.deepEqual(effect.selection, select(['move'], ['pd']));
  assert.deepEqual(off(effect), ['log']);
  for (const junk of [null, undefined, 7, 'x', {}, { options: null }, { functions: 'move', owned: 3, options: 4 }]) {
    const none = removalEffect(SHOP, junk as unknown as Selection, 'card');
    assert.equal(none.refused, 'absent');
    assert.deepEqual(none.selection, select());
    assert.equal(deselectEffect(SHOP, junk as unknown as Selection, 'move').changed, false);
  }
});

const POWER = catalogue(
  [
    ...BASE,
    part('esc'), part('batt', { selectable: false }), part('pd'), part('plug', { selectable: false }),
    part('sense'), part('angle'), part('pull', { selectable: false }), part('bus'),
    part('cap', { selectable: false }), part('seal', { selectable: false }), part('tag', { selectable: false }),
  ],
  [fn('run', ['esc']), fn('prog', ['esc', 'pd'], { escPower: 'pd' }), fn('feed', ['pd']), fn('amps', ['sense']), fn('turn', ['angle'])],
  [
    rule('battery', { all: [{ has: 'esc' }, { opt: 'escPower', is: 'battery' }] }, 'batt'),
    rule('pd', { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }] }, 'pd'),
    rule('plug', { has: 'pd' }, 'plug'),
    rule('pull', { all: [{ any: [{ has: 'sense' }, { has: 'angle' }] }, { not: { has: 'bus' } }] }, 'pull'),
    rule('cap', { has: 'copro' }, 'cap'),
    rule('seal', { not: { has: 'bus' } }, 'seal'),
    rule('tag', { any: [{ has: 'sense' }, { has: 'panel' }] }, 'tag'),
  ],
);

test('removal: a part added under a variant leaves by the other variant', () => {
  const battery = removalEffect(POWER, select(['run']), 'batt');
  assert.equal(battery.refused, null);
  assert.deepEqual(battery.selection, select(['run'], [], { escPower: 'pd' }));
  assert.deepEqual([battery.functionsOff, battery.partsOut, battery.partsIn], [[], [], ['pd', 'plug']]);
  // The module the rules add runs 2 more functions.
  assert.deepEqual(battery.functionsOn, ['prog', 'feed']);
  assert.ok(!removalIsPlain(battery));
  assert.deepEqual(battery.options, [{ key: 'escPower', from: 'battery', to: 'pd' }]);

  const pd = removalEffect(POWER, select(['run', 'feed'], [], { escPower: 'pd' }), 'pd');
  assert.deepEqual(pd.selection, select(['run']));
  assert.deepEqual(pd.functionsOff, [{ id: 'prog', chosen: false }, { id: 'feed', chosen: true }]);
  assert.deepEqual([pd.partsOut, pd.partsIn], [['plug'], ['batt']]);
  assert.deepEqual(pd.options, [{ key: 'escPower', from: 'pd', to: 'battery' }]);
});

test('removal: a variant forced by a function that goes off falls back to the selection', () => {
  const effect = removalEffect(POWER, select(['prog']), 'pd');
  assert.deepEqual(effect.selection, select([], ['esc']));
  assert.deepEqual(effect.functionsOff, [{ id: 'prog', chosen: true }, { id: 'feed', chosen: false }]);
  assert.deepEqual([effect.partsOut, effect.partsIn, effect.partsKept], [['plug'], ['batt'], ['esc']]);
  assert.deepEqual(effect.options, [{ key: 'escPower', from: 'pd', to: 'battery' }]);
});

test('removal: a variant that leaves with its part is not reported as changed', () => {
  const effect = removalEffect(POWER, select(['run'], [], { escPower: 'pd' }), 'esc');
  assert.deepEqual(effect.selection, select([], [], { escPower: 'pd' }));
  assert.deepEqual([effect.partsOut, effect.options], [['pd', 'plug'], []]);
});

test('removal: a rule on a variant that a kept function forces ends by the part instead', () => {
  // `stay` forces variant B and does not need the PD module; the rule adds it.
  const c = catalogue(
    [...BASE, part('esc'), part('pd'), part('lamp')],
    [fn('stay', ['lamp'], { escPower: 'pd' }), fn('run', ['esc'])],
    [rule('pd', { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }] }, 'pd')],
  );
  const effect = removalEffect(c, select(['stay', 'run']), 'pd');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select(['stay']));
  assert.deepEqual([off(effect), effect.partsOut, effect.options], [['run'], ['esc'], []]);
});

test('removal: a rule over alternatives ends when every part that holds it is gone', () => {
  const effect = removalEffect(POWER, select(['amps', 'turn']), 'pull');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select());
  assert.deepEqual(off(effect), ['amps', 'turn']);
  assert.deepEqual(effect.partsOut, ['sense', 'angle']);
  // One alternative alone.
  assert.deepEqual(removalEffect(POWER, select(['turn']), 'pull').partsOut, ['angle']);
  // Removing one alternative leaves the rule to the other.
  const one = removalEffect(POWER, select(['amps', 'turn']), 'sense');
  assert.deepEqual([one.selection, off(one), one.partsOut], [select(['turn']), ['amps'], []]);
});

test('removal: a part held by a rule that no removal ends is refused as held', () => {
  // On the base setup, on a part that is absent, and next to a base part in a disjunction.
  for (const id of ['cap', 'seal', 'tag']) {
    const effect = removalEffect(POWER, select(['amps']), id);
    assert.equal(effect.refused, 'held', id);
    assert.deepEqual(effect.selection, select(['amps']), id);
    assert.deepEqual([effect.functionsOff, effect.partsOut, effect.partsIn], [[], [], []], id);
  }
});

test('removal: a rule on a variant alone ends by the variant, or by a part of the function that forces it', () => {
  const c = catalogue(
    [...BASE, part('lamp'), part('shield', { selectable: false })],
    [fn('glow', ['lamp'], { connection: 'osoyoo' })],
    [rule('shield', { opt: 'connection', is: 'osoyoo' }, 'shield'), rule('none', { all: [] }, 'nope')],
  );
  const free = removalEffect(c, select([], [], { connection: 'osoyoo' }), 'shield');
  assert.equal(free.refused, null);
  assert.deepEqual(free.selection, select());
  // The connection variant is always visible, so the change is reported.
  assert.deepEqual(free.options, [{ key: 'connection', from: 'osoyoo', to: 'pads' }]);
  // The function that forces the variant goes off with its part, and the variant falls back.
  const forced = removalEffect(c, select(['glow']), 'shield');
  assert.equal(forced.refused, null);
  assert.deepEqual(forced.selection, select());
  assert.deepEqual(forced.functionsOff, [{ id: 'glow', chosen: true }]);
  assert.deepEqual(forced.partsOut, ['lamp']);
  assert.deepEqual(forced.options, [{ key: 'connection', from: 'osoyoo', to: 'pads' }]);
  // A function on the base setup alone has no part to leave with: the variant stays.
  const base = catalogue(c.parts, [fn('always', ['panel'], { connection: 'osoyoo' })], c.rules);
  assert.equal(removalEffect(base, select(['always']), 'shield').refused, 'held');
  // The selection holds the forced value as well: the part of the function and the variant both change.
  const both = removalEffect(c, select(['glow'], [], { connection: 'osoyoo' }), 'shield');
  assert.equal(both.refused, null);
  assert.deepEqual(both.selection, select());
});

test('removal: a negated variant ends by the value it excludes, a negated part holds', () => {
  const c = catalogue(
    [...BASE, part('x'), part('target', { selectable: false }), part('lonely', { selectable: false })],
    [],
    [
      rule('t', { not: { opt: 'connection', is: 'osoyoo' } }, 'target'),
      rule('l', { not: { has: 'x' } }, 'lonely'),
    ],
  );
  const effect = removalEffect(c, select(), 'target');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select([], [], { connection: 'osoyoo' }));
  assert.deepEqual(effect.options, [{ key: 'connection', from: 'pads', to: 'osoyoo' }]);
  // A removal adds no part, so the part that needs x to be absent stays.
  assert.equal(removalEffect(c, select(), 'lonely').refused, 'held');
});

test('removal: a part a later rule adds does not count for a rule that ran before it', () => {
  // The resolver runs each rule once, in order: "target" is there by x alone.
  const c = catalogue(
    [...BASE, part('x'), part('target', { selectable: false }), part('mid', { selectable: false })],
    [],
    [rule('t', { any: [{ has: 'x' }, { has: 'mid' }] }, 'target'), rule('m', { all: [] }, 'mid')],
  );
  const effect = removalEffect(c, select([], ['x']), 'target');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select());
  assert.deepEqual(effect.partsOut, ['x']);
});

test('removal: a part and the variant its function forces release a disjunction in one step', () => {
  const c = catalogue(
    [...BASE, part('x'), part('target', { selectable: false })],
    [fn('f', ['x'], { escPower: 'pd' })],
    [rule('t', { any: [{ has: 'x' }, { opt: 'escPower', is: 'pd' }] }, 'target')],
  );
  const effect = removalEffect(c, select(['f']), 'target');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select());
  assert.deepEqual(effect.functionsOff, [{ id: 'f', chosen: true }]);
  // With the value in the selection as well, the variant changes in a second step.
  const two = removalEffect(c, select(['f'], [], { escPower: 'pd' }), 'target');
  assert.equal(two.refused, null);
  assert.deepEqual(two.selection, select());
});

test('removal: a rule with an empty conjunction holds its part', () => {
  const c = catalogue([...BASE, part('always', { selectable: false })], [], [rule('always', { all: [] }, 'always')]);
  assert.equal(removalEffect(c, select(), 'always').refused, 'held');
});

test('removal: a way that leads to a part no removal releases gives place to the next way', () => {
  // Under "pads" the rule ends by the variant or by part x. Under "osoyoo" a base part holds the target.
  const c = catalogue(
    [...BASE, part('x'), part('target', { selectable: false })],
    [],
    [
      rule('pads', { all: [{ opt: 'connection', is: 'pads' }, { has: 'x' }] }, 'target'),
      rule('osoyoo', { all: [{ opt: 'connection', is: 'osoyoo' }, { has: 'panel' }] }, 'target'),
    ],
  );
  const effect = removalEffect(c, select([], ['x']), 'target');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select());
  assert.deepEqual([effect.partsOut, effect.options], [['x'], []]);
  // Without a part to take out, both ways end at the base part.
  const held = catalogue(c.parts, [], [rule('pads', { opt: 'connection', is: 'pads' }, 'target'), c.rules[1] as Rule]);
  assert.equal(removalEffect(held, select([], ['x']), 'target').refused, 'held');
});

test('removal: the search goes back over 2 steps, and tries the second rule of a part', () => {
  // Rule a ends by the variant alone. The variant then brings rule b, which ends by part y.
  // The part y is needed by nothing else, so the first way holds.
  const c = catalogue(
    [...BASE, part('y'), part('z'), part('target', { selectable: false }), part('mid', { selectable: false })],
    [fn('keep', ['z'], { ina228Board: 'matek' })],
    [
      rule('mid', { any: [{ has: 'y' }, { all: [{ has: 'z' }, { opt: 'ina228Board', is: 'matek' }] }] }, 'mid'),
      rule('a', { opt: 'escPower', is: 'battery' }, 'target'),
      rule('b', { all: [{ opt: 'escPower', is: 'pd' }, { has: 'mid' }] }, 'target'),
    ],
  );
  // With y alone, mid leaves with y.
  const one = removalEffect(c, select([], ['y']), 'target');
  assert.equal(one.refused, null);
  assert.deepEqual(one.selection, select([], [], { escPower: 'pd' }));
  assert.deepEqual(one.partsOut, ['y', 'mid']);
  // With y and z, both members of the disjunction hold: y and the board variant end it together.
  const two = removalEffect(c, select([], ['y', 'z']), 'target');
  assert.equal(two.refused, null);
  assert.deepEqual(two.selection, select([], ['z'], { escPower: 'pd', ina228Board: 'xt90' }));
  // A function that forces the board variant leaves z as the way.
  const three = removalEffect(c, select(['keep'], ['y']), 'target');
  assert.equal(three.refused, null);
  assert.deepEqual(three.selection, select([], [], { escPower: 'pd' }));
  assert.deepEqual(three.functionsOff, [{ id: 'keep', chosen: true }]);
  assert.deepEqual(three.partsOut, ['y', 'z', 'mid']);
});

test('removal: a rule that names a part already taken out ends by the rule of that part', () => {
  // The target is there by part mid, and mid is there by the variant.
  const c = catalogue(
    [...BASE, part('target', { selectable: false }), part('mid', { selectable: false })],
    [],
    [rule('m', { opt: 'connection', is: 'osoyoo' }, 'mid'), rule('t', { has: 'mid' }, 'target')],
  );
  const effect = removalEffect(c, select([], [], { connection: 'osoyoo' }), 'target');
  assert.equal(effect.refused, null);
  assert.deepEqual(effect.selection, select());
  assert.deepEqual(effect.partsOut, ['mid']);
});

// ---------------------------------------------------------------------------
// deselectEffect
// ---------------------------------------------------------------------------

test('deselect: the parts of a function leave when nothing else needs them', () => {
  const effect = deselectEffect(SHOP, select(['current', 'log']), 'current');
  assert.equal(effect.changed, true);
  assert.deepEqual(effect.selection, select(['log']));
  assert.equal(effect.stillOn, false);
  assert.deepEqual(effect.functionsOff, ['move']);
  assert.deepEqual([effect.partsOut, effect.partsStay, effect.partsIn], [['servo', 'meter', 'supply'], [], []]);
});

test('deselect: parts stay where another function or an owned part needs them', () => {
  const shared = deselectEffect(SHOP, select(['current', 'angle']), 'current');
  assert.deepEqual(shared.selection, select(['angle']));
  assert.deepEqual([shared.stillOn, shared.functionsOff, shared.partsOut, shared.partsStay], [false, [], ['meter'], ['servo']]);
  // The function stays on: another function holds its only part.
  const covered = deselectEffect(SHOP, select(['move', 'current']), 'move');
  assert.deepEqual([covered.stillOn, covered.functionsOff, covered.partsOut, covered.partsStay], [true, [], [], ['servo']]);
  // The function stays on: the visitor owns its parts.
  const owned = deselectEffect(SHOP, select(['current'], ['servo', 'meter']), 'current');
  assert.deepEqual([owned.stillOn, owned.partsOut, owned.partsStay], [true, [], ['servo', 'meter']]);
});

test('deselect: a part the rules add after the tick is gone is named', () => {
  const effect = deselectEffect(POWER, select(['run', 'prog']), 'prog');
  assert.deepEqual(effect.selection, select(['run']));
  assert.deepEqual([effect.stillOn, effect.functionsOff], [false, ['feed']]);
  assert.deepEqual([effect.partsOut, effect.partsIn, effect.partsStay], [['pd', 'plug'], ['batt'], ['esc']]);
});

test('deselect: a function that is not chosen, or not known, changes nothing', () => {
  for (const id of ['move', 'warp', '']) {
    const effect = deselectEffect(SHOP, select(['current']), id);
    assert.equal(effect.changed, false, id);
    assert.deepEqual(effect.selection, select(['current']), id);
    assert.deepEqual([effect.stillOn, effect.functionsOff, effect.partsOut, effect.partsIn, effect.partsStay], [false, [], [], [], []], id);
  }
});

test('deselect: a function without a part stays on', () => {
  const c = catalogue(BASE, [fn('ui', []), fn('link', ['copro'])]);
  for (const id of ['ui', 'link']) {
    const effect = deselectEffect(c, select(['ui', 'link']), id);
    assert.deepEqual([effect.changed, effect.stillOn, effect.partsOut, effect.partsStay], [true, true, [], []], id);
  }
  assert.deepEqual(on(resolve(c, select())), ['ui', 'link']);
});

// ---------------------------------------------------------------------------
// The data file
// ---------------------------------------------------------------------------

const SELECTABLE = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const LOCKED = new Set(baseParts(CATALOGUE));
/** The functions every setup runs: no part beyond the base setup and no test object. */
const ON_BASE = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
/** The functions a visitor ticks: a part beyond the base setup or a test object. The sweeps go over every set of them. */
const FUNCTIONS = CATALOGUE.functions.filter((f) => !ON_BASE.includes(f.id)).map((f) => f.id);
const OBJECTS = testObjects(CATALOGUE);
/** The function that drives each test object on the base setup. */
const PLAIN = new Map(OBJECTS.map((id) => [plainDrive(CATALOGUE, id) as string, id]));

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

/** Every part of the setup beyond the base is removed once. Returns the count of removals. */
function assertRemovals(selection: Selection): number {
  const where = JSON.stringify(selection);
  const before = resolve(CATALOGUE, selection);
  const options = Object.fromEntries(before.options.map((o) => [o.key, o.value])) as Options;
  let count = 0;
  for (const id of partIds(before)) {
    const effect = removalEffect(CATALOGUE, selection, id);
    if (LOCKED.has(id)) {
      assert.equal(effect.refused, 'base', `${id} of ${where}`);
      continue;
    }
    if (OBJECTS.includes(id)) {
      assert.equal(effect.refused, 'testObject', `${id} of ${where}`);
      assert.deepEqual(effect.selection.functions, CATALOGUE.functions.map((f) => f.id).filter((f) => selection.functions.includes(f)), `${id} of ${where}`);
      continue;
    }
    count += 1;
    const what = `removing ${id} from ${where}`;
    assert.equal(effect.refused, null, what);
    const after = resolve(CATALOGUE, effect.selection);
    assert.deepEqual(after.ignored, [], what);
    const [was, is] = [partIds(before), partIds(after)];
    assert.ok(!is.includes(id), `${what}: the part is still there`);
    // The lists are the difference of the two setups.
    assert.deepEqual(effect.partsOut, was.filter((p) => p !== id && !is.includes(p)), what);
    // No test object leaves by the removal of a part: the function that drives it alone keeps it.
    assert.deepEqual(effect.objectsOut, [], what);
    assert.deepEqual(OBJECTS.filter((p) => was.includes(p)), OBJECTS.filter((p) => is.includes(p)), what);
    assert.ok(effect.partsOut.every((p) => !OBJECTS.includes(p)) && effect.partsKept.every((p) => !OBJECTS.includes(p)), what);
    assert.deepEqual(effect.partsIn, is.filter((p) => !was.includes(p)), what);
    assert.deepEqual(off(effect), on(before).filter((f) => !on(after).includes(f)), what);
    // A function the base setup runs is on before and after, whatever leaves.
    for (const f of ON_BASE) {
      assert.ok(on(before).includes(f) && on(after).includes(f), `${what}: ${f} is not on`);
      assert.ok(!off(effect).includes(f) && !effect.functionsOn.includes(f), `${what}: ${f} is listed`);
      assert.equal(effect.selection.functions.includes(f), selection.functions.includes(f), `${what}: ${f} changes in the selection`);
    }
    assert.deepEqual(effect.functionsOn, on(after).filter((f) => !on(before).includes(f)), what);
    // A function comes on only with a part the rules add or with a variant that changes.
    if (effect.functionsOn.length > 0) {
      assert.ok(effect.partsIn.length > 0 || effect.options.length > 0, `${what}: a function comes on without a cause`);
    }
    assert.ok(effect.functionsOn.every((f) => !selection.functions.includes(f) && !PLAIN.has(f)), what);
    // A function goes off only because one of its parts leaves or its variant changes.
    for (const f of effect.functionsOff) {
      const needs = (CATALOGUE.functions.find((x) => x.id === f.id) as Fn).needs;
      assert.ok(needs.some((p) => p === id || effect.partsOut.includes(p)) || effect.options.length > 0, `${what}: ${f.id}`);
      assert.equal(f.chosen, selection.functions.includes(f.id), `${what}: ${f.id}`);
    }
    // The selection shrinks: a part is added only as a kept part, and a function
    // only to keep the test object of a chosen function that goes off.
    for (const f of effect.selection.functions.filter((x) => !selection.functions.includes(x))) {
      const kept = PLAIN.get(f);
      assert.ok(kept !== undefined, `${what}: ${f} is added`);
      const left = selection.functions.filter((x) => !effect.selection.functions.includes(x));
      assert.ok(left.some((x) => CATALOGUE.functions.find((y) => y.id === x)?.drives === kept), `${what}: ${f} is added without a cause`);
      assert.ok(on(before).includes(f), `${what}: ${f} was not on`);
    }
    assert.deepEqual(effect.selection.owned.filter((p) => !selection.owned.includes(p)), effect.partsKept, what);
    // Only a part that a rule adds, and no visitor can own, comes in.
    for (const p of effect.partsIn) {
      const reasons = after.parts.find((r) => r.part.id === p)?.addedBy ?? [];
      assert.ok(reasons.length > 0 && reasons.every((r) => r.kind === 'rule'), `${what}: ${p} comes in`);
    }
    // Adding the part back restores the functions.
    const back = [id, ...effect.partsOut].filter((p) => SELECTABLE.includes(p));
    const restored = resolve(CATALOGUE, {
      functions: effect.selection.functions,
      owned: [...effect.selection.owned, ...back],
      options,
    });
    assert.deepEqual(on(restored), on(before), `${what}: adding it back gives other functions`);
    assert.deepEqual(partIds(restored), was, `${what}: adding it back gives other parts`);
    const sameVariants = JSON.stringify(effect.selection.options) === JSON.stringify(options);
    if (effect.partsOut.every((p) => !SELECTABLE.includes(p)) && sameVariants && SELECTABLE.includes(id)) {
      // No other selectable part left and the variants are those of the setup before: the part alone does it.
      const alone = resolve(CATALOGUE, { ...effect.selection, owned: [...effect.selection.owned, id] });
      assert.deepEqual(on(alone), on(before), `${what}: the part alone gives other functions`);
    }
    // A second removal of the same part is refused: it is absent.
    assert.equal(removalEffect(CATALOGUE, effect.selection, id).refused, 'absent', what);
  }
  return count;
}

test('sweep over the data file: every part beyond the base is removable from every set of functions', () => {
  assert.ok(FUNCTIONS.length <= 16, 'the sweep over 2^n sets of functions is a decision above 16 functions');
  let count = 0;
  for (let mask = 0; mask < 2 ** FUNCTIONS.length; mask++) {
    for (const escPower of OPTION_VALUES.escPower) count += assertRemovals(select(subset(FUNCTIONS, mask), [], { escPower }));
  }
  assert.ok(count > 2 ** FUNCTIONS.length, `${count} removals`);
});

test('sweep over the data file: the functions the base setup runs change no removal', () => {
  assert.ok(ON_BASE.length > 0, 'the data file holds no function without a part');
  // The first group: these, the plain drives of the test objects, and the
  // servo programmer, which drives the servo on the base setup and forces the
  // programming port.
  assert.deepEqual([...PLAIN.keys()], ['servo', 'esc']);
  assert.deepEqual(groups(CATALOGUE)[0]?.functions, [...ON_BASE, 'servo', 'servoprog', 'esc']);
  // The same in the terms of the page: these are the functions that name no part and no test object.
  assert.deepEqual(CATALOGUE.functions.filter((f) => f.needs.length === 0 && f.drives === undefined).map((f) => f.id), ON_BASE);
  const strip = (effect: Removal): unknown => ({ ...effect, selection: { ...effect.selection, functions: effect.selection.functions.filter((f) => !ON_BASE.includes(f)) } });
  let count = 0;
  for (let mask = 0; mask < 2 ** FUNCTIONS.length; mask++) {
    const ticked = subset(FUNCTIONS, mask);
    // In turn all of them, and a set of them that changes with the mask, in the selection.
    for (const extra of [mask % 2 === 0 ? ON_BASE : subset(ON_BASE, (mask >> 1) % 2 ** ON_BASE.length)]) {
      const selection = select([...extra, ...ticked]);
      count += assertRemovals(selection);
      const [bare, full] = [resolve(CATALOGUE, select(ticked)), resolve(CATALOGUE, selection)];
      assert.deepEqual(partIds(full), partIds(bare), selection.functions.join());
      assert.deepEqual(full.wires, bare.wires, selection.functions.join());
      assert.deepEqual(on(full), on(bare), selection.functions.join());
      // The removal of each part is the removal without them, but for their place in the selection.
      if (extra !== ON_BASE) continue;
      for (const id of partIds(bare)) {
        assert.deepEqual(strip(removalEffect(CATALOGUE, selection, id)), strip(removalEffect(CATALOGUE, select(ticked), id)), `${id} of ${selection.functions.join()}`);
      }
    }
  }
  assert.ok(count > 2 ** FUNCTIONS.length, `${count} removals`);
});

test('sweep over the data file: every set of the functions the base setup runs is the base setup', () => {
  assert.ok(ON_BASE.length <= 16, 'the sweep over 2^n sets of functions is a decision above 16 functions');
  const base = resolve(CATALOGUE, select());
  assert.deepEqual(partIds(base), [...LOCKED]);
  for (let mask = 0; mask < 2 ** ON_BASE.length; mask++) {
    const selection = select(subset(ON_BASE, mask));
    const setup = resolve(CATALOGUE, selection);
    assert.deepEqual([partIds(setup), setup.wires, on(setup), setup.checks], [partIds(base), base.wires, ON_BASE, base.checks], selection.functions.join());
    // Nothing to remove: every part is a part of the base setup.
    for (const id of partIds(setup)) assert.equal(removalEffect(CATALOGUE, selection, id).refused, 'base');
    for (const id of ON_BASE) {
      const effect = deselectEffect(CATALOGUE, selection, id);
      assert.equal(effect.changed, selection.functions.includes(id));
      assert.deepEqual(
        [effect.stillOn, effect.functionsOff, effect.partsOut, effect.partsIn, effect.partsStay, effect.objectsOut],
        [effect.changed, [], [], [], [], []],
        `deselecting ${id} from ${selection.functions.join()}`,
      );
    }
  }
});

test('sweep over the data file: every part is removable from every set of owned parts', () => {
  assert.ok(SELECTABLE.length <= 16, 'the sweep over 2^n sets of parts is a decision above 16 parts');
  let count = 0;
  for (let mask = 0; mask < 2 ** SELECTABLE.length; mask++) {
    for (const escPower of OPTION_VALUES.escPower) count += assertRemovals(select([], subset(SELECTABLE, mask), { escPower }));
  }
  assert.ok(count > 2 ** SELECTABLE.length, `${count} removals`);
});

test('sample of the data file: 1500 mixed selections under all variants', () => {
  const random = generator(0x16_00_5e1e);
  const pick = (ids: string[]): string[] => ids.filter(() => random() < 0.3);
  let count = 0;
  for (let i = 0; i < 1500; i++) {
    const options = OPTION_SETS[Math.floor(random() * OPTION_SETS.length)] as Options;
    count += assertRemovals({ functions: pick([...ON_BASE, ...FUNCTIONS]), owned: pick(SELECTABLE), options });
  }
  assert.ok(count > 1500, `${count} removals`);
});

test('sweep over the data file: deselecting a function is the selection without it', () => {
  // Every set of the functions that need a part, without and with the functions the base setup runs.
  for (let mask = 0; mask < 2 ** (FUNCTIONS.length + 1); mask++) {
    const selection = select(CATALOGUE.functions.map((f) => f.id).filter((id) =>
      (ON_BASE.includes(id) ? mask >= 2 ** FUNCTIONS.length : subset(FUNCTIONS, mask).includes(id))));
    const before = resolve(CATALOGUE, selection);
    for (const id of [...FUNCTIONS, ...ON_BASE]) {
      const effect = deselectEffect(CATALOGUE, selection, id);
      const what = `deselecting ${id} from ${selection.functions.join(',')}`;
      assert.equal(effect.changed, selection.functions.includes(id), what);
      if (!effect.changed) {
        assert.deepEqual(effect.selection, selection, what);
        continue;
      }
      const after = resolve(CATALOGUE, effect.selection);
      // A function the base setup runs never goes off, and without its tick it stays on and moves no part.
      assert.ok(effect.functionsOff.every((f) => !ON_BASE.includes(f)), what);
      if (ON_BASE.includes(id)) assert.deepEqual([effect.stillOn, effect.functionsOff, effect.partsOut, effect.partsIn, effect.partsStay], [true, [], [], [], []], what);
      assert.deepEqual(effect.selection.functions, selection.functions.filter((f) => f !== id), what);
      assert.equal(effect.stillOn, on(after).includes(id), what);
      assert.deepEqual(effect.functionsOff, on(before).filter((f) => f !== id && !on(after).includes(f)), what);
      const gone = partIds(before).filter((p) => !partIds(after).includes(p));
      assert.deepEqual(effect.partsOut, gone.filter((p) => !OBJECTS.includes(p)), what);
      assert.deepEqual(effect.objectsOut, gone.filter((p) => OBJECTS.includes(p)), what);
      // A test object leaves exactly when no function that stays chosen drives it.
      const drives = (CATALOGUE.functions.find((f) => f.id === id) as Fn).drives;
      const still = effect.selection.functions.some((f) => CATALOGUE.functions.find((x) => x.id === f)?.drives === drives);
      assert.deepEqual(effect.objectsOut, drives !== undefined && !still ? [drives] : [], what);
      const needs = (CATALOGUE.functions.find((f) => f.id === id) as Fn).needs.filter((p) => !LOCKED.has(p));
      // Each part of the function either stays or leaves.
      assert.deepEqual([...effect.partsStay, ...effect.partsOut.filter((p) => needs.includes(p))].sort(), [...needs].sort(), what);
      // Ticking it again gives the setup before.
      assert.deepEqual(resolve(CATALOGUE, { ...effect.selection, functions: [...effect.selection.functions, id] }), before, what);
    }
  }
});
