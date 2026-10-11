// Groups, removal of a part and deselection of a function: on fixture
// catalogues at the edges, and on the data file in a sweep.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Catalogue, Cond, Fn, Options, Part, Rule, Selection, Setup, Src } from '../../contract.ts';
import { CATALOGUE } from '../catalogue.ts';
import { baseParts, deselectEffect, groups, removalEffect, removalIsPlain } from '../groups.ts';
import type { Removal } from '../groups.ts';
import { DEFAULT_OPTIONS, OPTION_VALUES, resolve } from '../resolve.ts';

const SRC: Src = { kind: 'page' };
const text = (s: string) => ({ en: s, de: s });

function part(id: string, flags: Partial<Part> = {}): Part {
  return {
    id, name: text(id), short: text(id), role: null, locked: false, selectable: true, release: 'released',
    hardware: 'run', slot: { column: 'none', order: 0 }, pins: [], src: SRC, ...flags,
  };
}

function fn(id: string, needs: string[], forces?: Partial<Options>): Fn {
  return { id, group: 'bench', name: text(id), needs, hardware: 'run', src: SRC, ...(forces ? { forces } : {}) };
}

function rule(id: string, when: Cond, adds: string): Rule {
  return { id, when, adds, because: text(id), src: SRC };
}

function catalogue(parts: Part[], functions: Fn[], rules: Rule[] = []): Catalogue {
  return {
    release: '0.0.0', hashVersion: 1, parts, pads: [], wires: [], functions, rules, checks: [], stages: [],
    safety: [], notInRelease: [], baseFunction: text('base'), pinDefaults: [],
  };
}

function select(functions: string[] = [], owned: string[] = [], options: Partial<Options> = {}): Selection {
  return { functions, owned, options: { ...DEFAULT_OPTIONS, ...options } };
}

const BASE = [part('panel', { locked: true, selectable: false }), part('copro', { locked: true, selectable: false })];

const partIds = (setup: Setup): string[] => setup.parts.map((p) => p.part.id);
const on = (setup: Setup): string[] => setup.functions.filter((f) => f.enabled).map((f) => f.fn.id);
const off = (effect: Removal): string[] => effect.functionsOff.map((f) => f.id);

const OPTION_SETS: Options[] = OPTION_VALUES.connection.flatMap((connection) =>
  OPTION_VALUES.ina228Board.flatMap((ina228Board) =>
    OPTION_VALUES.escPower.map((escPower) => ({ connection, ina228Board, escPower }))));

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
  assert.deepEqual(g.find((x) => x.key === 'esc')?.oneOf, [['batt'], ['pdmini', 'charger', 'usbtap']]);
  assert.deepEqual(g.find((x) => x.key === 'servo')?.also, ['ssup']);
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

test('removal: a rule on a variant alone ends by the variant, and is held when a function forces it', () => {
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
  assert.equal(removalEffect(c, select(['glow']), 'shield').refused, 'held');
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

const FUNCTIONS = CATALOGUE.functions.map((f) => f.id);
const SELECTABLE = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const LOCKED = new Set(baseParts(CATALOGUE));

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
    count += 1;
    const what = `removing ${id} from ${where}`;
    assert.equal(effect.refused, null, what);
    const after = resolve(CATALOGUE, effect.selection);
    assert.deepEqual(after.ignored, [], what);
    const [was, is] = [partIds(before), partIds(after)];
    assert.ok(!is.includes(id), `${what}: the part is still there`);
    // The lists are the difference of the two setups.
    assert.deepEqual(effect.partsOut, was.filter((p) => p !== id && !is.includes(p)), what);
    assert.deepEqual(effect.partsIn, is.filter((p) => !was.includes(p)), what);
    assert.deepEqual(off(effect), on(before).filter((f) => !on(after).includes(f)), what);
    assert.deepEqual(effect.functionsOn, on(after).filter((f) => !on(before).includes(f)), what);
    // A function comes on only with a part the rules add or with a variant that changes.
    if (effect.functionsOn.length > 0) {
      assert.ok(effect.partsIn.length > 0 || effect.options.length > 0, `${what}: a function comes on without a cause`);
    }
    assert.ok(effect.functionsOn.every((f) => !selection.functions.includes(f)), what);
    // A function goes off only because one of its parts leaves or its variant changes.
    for (const f of effect.functionsOff) {
      const needs = (CATALOGUE.functions.find((x) => x.id === f.id) as Fn).needs;
      assert.ok(needs.some((p) => p === id || effect.partsOut.includes(p)) || effect.options.length > 0, `${what}: ${f.id}`);
      assert.equal(f.chosen, selection.functions.includes(f.id), `${what}: ${f.id}`);
    }
    // The selection shrinks: no function is added, a part is added only as a kept part.
    assert.ok(effect.selection.functions.every((f) => selection.functions.includes(f)), what);
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
    count += assertRemovals({ functions: pick(FUNCTIONS), owned: pick(SELECTABLE), options });
  }
  assert.ok(count > 1500, `${count} removals`);
});

test('sweep over the data file: deselecting a function is the selection without it', () => {
  for (let mask = 0; mask < 2 ** FUNCTIONS.length; mask++) {
    const selection = select(subset(FUNCTIONS, mask));
    const before = resolve(CATALOGUE, selection);
    for (const id of FUNCTIONS) {
      const effect = deselectEffect(CATALOGUE, selection, id);
      const what = `deselecting ${id} from ${selection.functions.join(',')}`;
      assert.equal(effect.changed, selection.functions.includes(id), what);
      if (!effect.changed) {
        assert.deepEqual(effect.selection, selection, what);
        continue;
      }
      const after = resolve(CATALOGUE, effect.selection);
      assert.deepEqual(effect.selection.functions, selection.functions.filter((f) => f !== id), what);
      assert.equal(effect.stillOn, on(after).includes(id), what);
      assert.deepEqual(effect.functionsOff, on(before).filter((f) => f !== id && !on(after).includes(f)), what);
      assert.deepEqual(effect.partsOut, partIds(before).filter((p) => !partIds(after).includes(p)), what);
      const needs = (CATALOGUE.functions.find((f) => f.id === id) as Fn).needs.filter((p) => !LOCKED.has(p));
      // Each part of the function either stays or leaves.
      assert.deepEqual([...effect.partsStay, ...effect.partsOut.filter((p) => needs.includes(p))].sort(), [...needs].sort(), what);
      // Ticking it again gives the setup before.
      assert.deepEqual(resolve(CATALOGUE, { ...effect.selection, functions: [...effect.selection.functions, id] }), before, what);
    }
  }
});
