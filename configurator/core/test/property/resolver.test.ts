// Properties of the resolver over the selection space.
//
// Enumerated: every subset of the functions and every subset of the parts you
// own, each under every option combination. Sampled: selections that mix both,
// from a generator with a fixed seed. A failure names the selection, and for a
// sampled case the seed and the index.
//
// The properties:
//
//  1. A second call and any order of the input give the same setup.
//  2. A part that a function, a tick or the lock holds never leaves the setup
//     when a function or a part is added. A part that leaves is held by rules
//     alone, the visitor cannot tick it, and the setup names what replaces it.
//  3. A function that is enabled stays enabled when a function or a part is
//     added, unless a function forces the other value of an option, and then
//     the option names that function.
//  4. Every part names at least one reason, and each reason is true.
//  5. Every function that is not enabled names the parts that are missing, or
//     an option it forces has another value, or the test object it drives is
//     not connected.
//  6. Every wire ends on a pad, a CAN terminal, or a pin of a part in the
//     setup that exists under the options of the setup.
//  7. No pin and no pad carries two nets.
//  8. No two wires join the same two ends, and no pin has two stubs.
//  9. The two entry modes agree: the functions a set of parts enables resolve
//     to parts of that set, plus replacement parts the setup names. A test
//     object is no part to own: the tick of its plain drive connects it.
// 10. A setup holds released parts only, and no id of the "not in a release"
//     list.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type {
  Catalogue, Cond, OptionKey, Options, PartId, Pin, ResolvedPart, Rule, Selection, Setup, Wire,
} from '../../../contract.ts';
import { CATALOGUE } from '../../catalogue.ts';
import { holds } from '../../cond.ts';
import { plainDrive } from '../../groups.ts';
import { OPTION_KEYS, OPTION_VALUES, resolve } from '../../resolve.ts';
import {
  CASE_LIMIT, MIXED, OPTION_SETS, SEED, enumerate, generator, sample, shuffled, spaceOf, subset,
} from './space.ts';
import type { Case } from './space.ts';

const SPACE = spaceOf(CATALOGUE);
const RULES = new Map(CATALOGUE.rules.map((r) => [r.id, r]));

function indexOf(catalogue: Catalogue) {
  return {
    PARTS: new Map(catalogue.parts.map((p) => [p.id, p])),
    RULES: new Map(catalogue.rules.map((r) => [r.id, r])),
    FUNCTIONS: new Map(catalogue.functions.map((f) => [f.id, f])),
    PADS: new Set(catalogue.pads.map((p) => p.n)),
    NOT_IN_RELEASE: new Set(catalogue.notInRelease.map((n) => n.id)),
  };
}
const INDEXES = new WeakMap<Catalogue, ReturnType<typeof indexOf>>();

/** The lookups of a catalogue, built once per catalogue. */
function lookups(catalogue: Catalogue): ReturnType<typeof indexOf> {
  let index = INDEXES.get(catalogue);
  if (index === undefined) {
    index = indexOf(catalogue);
    INDEXES.set(catalogue, index);
  }
  return index;
}

const partIds = (setup: Setup): Set<PartId> => new Set(setup.parts.map((p) => p.part.id));
const optionsOf = (setup: Setup): Options =>
  Object.fromEntries(setup.options.map((o) => [o.key, o.value])) as Options;
const enabledIds = (setup: Setup): string[] => setup.functions.filter((f) => f.enabled).map((f) => f.fn.id);

/** Part ids a condition names under an odd count of `not`. */
function negated(cond: Cond | null, odd = false): PartId[] {
  if (cond === null) return [];
  if ('has' in cond) return odd ? [cond.has] : [];
  if ('opt' in cond) return [];
  if ('not' in cond) return negated(cond.not, !odd);
  const list = 'all' in cond ? cond.all : cond.any;
  return list.flatMap((c) => negated(c, odd));
}

/** Part ids a condition names under an even count of `not`. */
function required(cond: Cond | null, odd = false): PartId[] {
  if (cond === null) return [];
  if ('has' in cond) return odd ? [] : [cond.has];
  if ('opt' in cond) return [];
  if ('not' in cond) return required(cond.not, !odd);
  const list = 'all' in cond ? cond.all : cond.any;
  return list.flatMap((c) => required(c, odd));
}

function optionKeys(cond: Cond | null): OptionKey[] {
  if (cond === null) return [];
  if ('has' in cond) return [];
  if ('opt' in cond) return [cond.opt];
  if ('not' in cond) return optionKeys(cond.not);
  const list = 'all' in cond ? cond.all : cond.any;
  return list.flatMap((c) => optionKeys(c));
}

function pinPresent(pin: Pin, options: Options): boolean {
  return OPTION_KEYS.every((key) => pin.only?.[key] === undefined || pin.only[key] === options[key]);
}

function ends(wire: Wire): Array<{ end: string; net: string }> {
  const list = [{ end: wire.a, net: wire.net }];
  if (wire.b !== null) list.push({ end: wire.b, net: wire.netB ?? wire.net });
  return list;
}

// ---------------------------------------------------------------------------
// Properties of one setup: 1 and 4 to 8, and 10.

function assertSetup(catalogue: Catalogue, { selection, where }: Case, random: () => number): Setup {
  const { PARTS, RULES, FUNCTIONS, PADS, NOT_IN_RELEASE } = lookups(catalogue);
  const setup = resolve(catalogue, selection);

  // 1: deterministic and independent of order and repeats.
  assert.deepEqual(resolve(catalogue, selection), setup, `two calls differ for ${where}`);
  const mixed: Selection = {
    functions: shuffled([...selection.functions, ...selection.functions], random),
    owned: shuffled([...selection.owned, ...selection.owned], random),
    options: {
      escPower: selection.options.escPower,
      ina228Board: selection.options.ina228Board,
      connection: selection.options.connection,
      servoPower: selection.options.servoPower,
    },
  };
  assert.deepEqual(resolve(catalogue, mixed), setup, `another input order differs for ${where}`);

  const present = partIds(setup);
  const options = optionsOf(setup);
  const chosen = new Set(selection.functions);
  const owned = new Set(selection.owned);

  // 4: every part names a reason, and each reason is true.
  for (const p of setup.parts) {
    assert.ok(p.addedBy.length > 0, `${p.part.id} names no reason for ${where}`);
    if (p.derived) {
      assert.ok(p.addedBy.some((r) => r.kind !== 'owned'), `${p.part.id} is derived and names no reason but the tick for ${where}`);
    } else {
      assert.ok(owned.has(p.part.id), `${p.part.id} is neither derived nor ticked for ${where}`);
    }
    for (const reason of p.addedBy) {
      if (reason.kind === 'always') {
        assert.ok(p.part.locked, `${p.part.id} is "always" and not locked for ${where}`);
      } else if (reason.kind === 'owned') {
        assert.ok(owned.has(p.part.id) && p.part.selectable, `${p.part.id} is "owned" without a tick for ${where}`);
      } else if (reason.kind === 'function') {
        const fn = FUNCTIONS.get(reason.id);
        assert.ok(fn && chosen.has(fn.id), `${p.part.id} names the function ${reason.id}, which is not chosen, for ${where}`);
        assert.ok(fn.needs.includes(p.part.id) || fn.drives === p.part.id,
          `${p.part.id} names the function ${reason.id}, which neither needs nor drives it, for ${where}`);
        assert.equal(fn.drives === p.part.id, p.part.testObject === true, `${p.part.id} and the function ${reason.id} disagree on the test object for ${where}`);
      } else if (reason.kind === 'rule') {
        const rule = RULES.get(reason.id);
        assert.ok(rule && rule.adds === p.part.id, `${p.part.id} names the rule ${reason.id}, which does not add it, for ${where}`);
        assert.ok(reason.because.en.length > 0 && reason.because.de.length > 0, `rule ${reason.id} gives no reason text for ${where}`);
        assert.ok(holds(rule.when, { has: (id) => present.has(id), options }), `rule ${reason.id} does not hold for ${where}`);
      } else {
        assert.ok((OPTION_VALUES[reason.key] as ReadonlyArray<string>).includes(reason.value), `${p.part.id} names an option value outside the type for ${where}`);
        assert.equal(options[reason.key], reason.value, `${p.part.id} names an option value the setup does not have for ${where}`);
      }
    }
    assert.equal(new Set(p.addedBy.map((r) => JSON.stringify(r))).size, p.addedBy.length, `${p.part.id} names a reason twice for ${where}`);
  }
  for (const part of catalogue.parts) {
    if (part.locked) assert.ok(present.has(part.id), `the locked part ${part.id} is missing for ${where}`);
    if (owned.has(part.id)) assert.ok(present.has(part.id), `the ticked part ${part.id} is missing for ${where}`);
  }
  // A test object is connected by a chosen function that drives it, and by nothing else.
  for (const p of setup.parts) {
    if (p.part.testObject !== true) continue;
    assert.ok(p.addedBy.every((r) => r.kind === 'function'), `the test object ${p.part.id} names a reason that is no function for ${where}`);
  }

  // 5: a function that is not enabled names what is missing.
  assert.equal(setup.functions.length, catalogue.functions.length);
  for (const f of setup.functions) {
    for (const id of f.missing) {
      assert.ok(PARTS.has(id), `${f.fn.id} misses ${id}, which is no part, for ${where}`);
      assert.ok(f.fn.needs.includes(id) && !present.has(id), `${f.fn.id} misses ${id} wrongly for ${where}`);
    }
    const unmet = OPTION_KEYS.filter((key) => f.fn.forces?.[key] !== undefined && f.fn.forces[key] !== options[key]);
    const loose = f.fn.drives !== undefined && !present.has(f.fn.drives);
    if (f.enabled) {
      assert.ok(f.fn.needs.every((id) => present.has(id)), `${f.fn.id} is enabled without its parts for ${where}`);
      assert.deepEqual(unmet, [], `${f.fn.id} is enabled against an option for ${where}`);
      assert.ok(!loose, `${f.fn.id} is enabled without the test object ${f.fn.drives} for ${where}`);
    } else {
      assert.ok(f.missing.length > 0 || unmet.length > 0 || loose, `${f.fn.id} is not enabled and names nothing for ${where}`);
    }
    if (f.chosen) {
      assert.deepEqual(f.missing, [], `${f.fn.id} is chosen and misses a part for ${where}`);
      assert.ok(!loose, `${f.fn.id} is chosen and its test object is not connected for ${where}`);
      // A chosen function loses an option only to a chosen function that names itself.
      for (const key of unmet) {
        const by = setup.options.find((o) => o.key === key)?.forcedBy ?? null;
        assert.ok(by !== null && by !== f.fn.id && chosen.has(by), `${f.fn.id} is chosen, ${key} has another value and no function names itself, for ${where}`);
      }
    }
  }

  // Options: a value of the type; a forced option names a chosen function that forces this value.
  for (const o of setup.options) {
    assert.ok((OPTION_VALUES[o.key] as ReadonlyArray<string>).includes(o.value), `${o.key} is ${o.value} for ${where}`);
    if (o.forcedBy === null) {
      assert.equal(o.value, selection.options[o.key], `${o.key} differs from the selection without a function that forces it, for ${where}`);
    } else {
      const fn = FUNCTIONS.get(o.forcedBy);
      assert.ok(fn && chosen.has(fn.id) && fn.forces?.[o.key] === o.value, `${o.key} names ${o.forcedBy} wrongly for ${where}`);
    }
  }

  // 6: every wire ends on something that exists in this setup.
  for (const wire of setup.wires) {
    for (const { end } of ends(wire)) {
      const dot = end.indexOf('.');
      const head = end.slice(0, dot);
      const tail = end.slice(dot + 1);
      if (head === 'can') {
        assert.ok(tail === 'H' || tail === 'L', `wire ${wire.n} ends on ${end} for ${where}`);
        assert.ok(present.has('copro'), `wire ${wire.n} ends on ${end} without the coprocessor for ${where}`);
      } else if (head === 'pad') {
        assert.ok(/^[1-9][0-9]*$/.test(tail) && PADS.has(Number(tail)), `wire ${wire.n} ends on ${end} for ${where}`);
        assert.ok(present.has('copro'), `wire ${wire.n} ends on ${end} without the coprocessor for ${where}`);
      } else {
        const part = PARTS.get(head);
        const pin = part?.pins.find((x) => x.id === tail);
        assert.ok(part && pin, `wire ${wire.n} ends on the undeclared pin ${end} for ${where}`);
        assert.ok(present.has(head), `wire ${wire.n} ends on ${end}, a part outside the setup, for ${where}`);
        assert.ok(pinPresent(pin, options), `wire ${wire.n} ends on ${end}, a pin of another board variant, for ${where}`);
      }
    }
    if (wire.kind === 'stub') assert.equal(wire.b, null, `stub ${wire.n} has a second end for ${where}`);
    else assert.notEqual(wire.b, null, `wire ${wire.n} has one end for ${where}`);
    assert.notEqual(wire.a, wire.b, `wire ${wire.n} joins ${wire.a} with itself for ${where}`);
  }

  // 7: no pin and no pad carries two nets.
  const nets = new Map<string, string>();
  for (const wire of setup.wires) {
    for (const { end, net } of ends(wire)) {
      const held = nets.get(end);
      assert.ok(held === undefined || held === net, `${end} carries the nets ${held} and ${net} for ${where}`);
      nets.set(end, net);
    }
  }

  // 8: no wire twice.
  const joins = new Map<string, number>();
  const numbers = new Set<number>();
  for (const wire of setup.wires) {
    assert.ok(!numbers.has(wire.n), `wire number ${wire.n} occurs twice for ${where}`);
    numbers.add(wire.n);
    const key = wire.b === null ? `stub ${wire.a}` : [wire.a, wire.b].sort().join(' to ');
    assert.ok(!joins.has(key), `wires ${joins.get(key)} and ${wire.n} are both ${key} for ${where}`);
    joins.set(key, wire.n);
  }

  // 10: released parts only, and no id of the "not in a release" list.
  for (const p of setup.parts) {
    assert.equal(p.part.release, 'released', `${p.part.id} is in the setup and in no release, for ${where}`);
    assert.ok(!NOT_IN_RELEASE.has(p.part.id), `${p.part.id} is on the "not in a release" list, for ${where}`);
  }
  for (const f of setup.functions) {
    if (f.enabled || f.chosen) assert.ok(!NOT_IN_RELEASE.has(f.fn.id), `${f.fn.id} is on the "not in a release" list, for ${where}`);
  }
  for (const wire of setup.wires) {
    for (const { end } of ends(wire)) {
      assert.ok(!NOT_IN_RELEASE.has(end.slice(0, end.indexOf('.'))), `wire ${wire.n} ends on ${end}, which is in no release, for ${where}`);
    }
  }
  assert.deepEqual(setup.ignored, [], `ids are ignored for ${where}`);
  return setup;
}

// ---------------------------------------------------------------------------
// Properties of a pair of setups: 2 and 3.

/**
 * Why `id` is in `before` and not in `after`. Throws when the setups do not
 * name the cause. `added` is the function or part the second selection adds.
 */
function assertReplaced(id: PartId, before: Setup, after: Setup, where: string, trail: PartId[] = []): void {
  const part = (before.parts.find((p) => p.part.id === id) as ResolvedPart);
  const afterParts = partIds(after);
  const beforeParts = partIds(before);
  const afterOptions = optionsOf(after);
  const beforeOptions = optionsOf(before);

  assert.ok(!part.part.selectable, `${id} can be ticked and leaves the setup for ${where}`);
  for (const reason of part.addedBy) {
    assert.equal(reason.kind, 'rule', `${id} is held by "${reason.kind}" and leaves the setup for ${where}`);
  }
  for (const reason of part.addedBy) {
    if (reason.kind !== 'rule') continue;
    const rule = RULES.get(reason.id) as Rule;
    assert.ok(!holds(rule.when, { has: (x) => afterParts.has(x), options: afterOptions }), `rule ${rule.id} holds and ${id} is missing for ${where}`);

    const causes: string[] = [];
    // A part the rule excludes has come in, and the setup names why.
    for (const other of negated(rule.when)) {
      if (!afterParts.has(other) || beforeParts.has(other)) continue;
      const entry = after.parts.find((p) => p.part.id === other) as ResolvedPart;
      assert.ok(entry.addedBy.length > 0, `${other} replaces ${id} and names no reason for ${where}`);
      causes.push(other);
    }
    // An option the rule reads has another value, and the option names the function.
    for (const key of optionKeys(rule.when)) {
      if (afterOptions[key] === beforeOptions[key]) continue;
      const option = after.options.find((o) => o.key === key);
      assert.ok(option && option.forcedBy !== null, `${key} changes, ${id} leaves and no function names itself, for ${where}`);
      causes.push(key);
    }
    // A part the rule needs has left for a cause of its own.
    for (const other of required(rule.when)) {
      if (afterParts.has(other) || !beforeParts.has(other)) continue;
      assert.ok(!trail.includes(other), `${id} and ${other} leave because of each other for ${where}`);
      assertReplaced(other, before, after, where, [...trail, id]);
      causes.push(other);
    }
    assert.ok(causes.length > 0, `${id} leaves the setup and nothing names the cause for ${where}`);
  }
}

function assertGrows(before: Setup, after: Setup, where: string): { left: PartId[] } {
  const afterParts = partIds(after);
  const left: PartId[] = [];
  for (const p of before.parts) {
    if (afterParts.has(p.part.id)) continue;
    assertReplaced(p.part.id, before, after, where);
    left.push(p.part.id);
  }
  // 3: an enabled function stays enabled, or an option names the function that took it.
  for (let i = 0; i < before.functions.length; i++) {
    const was = before.functions[i];
    const is = after.functions[i];
    if (!was || !is || !was.enabled || is.enabled) continue;
    assert.deepEqual(is.missing, [], `${is.fn.id} loses the parts ${is.missing.join(', ')} for ${where}`);
    const taken = after.options.filter((o) => is.fn.forces?.[o.key] !== undefined && is.fn.forces[o.key] !== o.value);
    assert.ok(taken.length > 0, `${is.fn.id} is no longer enabled and nothing names the cause for ${where}`);
    for (const o of taken) assert.notEqual(o.forcedBy, null, `${is.fn.id} loses ${o.key} and no function names itself for ${where}`);
  }
  return { left };
}

// ---------------------------------------------------------------------------

test('the space is counted before it is walked', () => {
  // Both enumerated spaces together stay under the limit. Their product does not.
  assert.equal(SPACE.functionCases, 2 ** SPACE.functions.length * OPTION_SETS.length);
  assert.equal(SPACE.partCases, 2 ** SPACE.selectable.length * OPTION_SETS.length);
  assert.ok(SPACE.functionCases + SPACE.partCases + MIXED <= CASE_LIMIT,
    `${SPACE.functionCases} + ${SPACE.partCases} + ${MIXED} cases are over ${CASE_LIMIT}: sample the subsets`);
  assert.ok(SPACE.productCases > CASE_LIMIT, 'the product of both spaces fits: enumerate it');
  assert.ok(SPACE.functions.length > 0 && SPACE.selectable.length > 0);
  assert.equal(OPTION_SETS.length, 16);
});

test('the generator gives the same cases for the same seed', () => {
  const first = [...sample(SPACE, 50, SEED)].map((c) => c.where);
  const second = [...sample(SPACE, 50, SEED)].map((c) => c.where);
  const other = [...sample(SPACE, 50, SEED + 1)].map((c) => c.where);
  assert.deepEqual(second, first);
  assert.notDeepEqual(other, first);
  assert.ok(first.every((w) => w.includes(`seed 0x${SEED.toString(16)}`)));
});

test('every subset of the functions: the setup holds properties 1, 4 to 8 and 10; adding a function holds 2 and 3', () => {
  const random = generator(SEED);
  const setups: Setup[] = [];
  const index = (mask: number, option: number): number => mask * OPTION_SETS.length + option;
  for (const c of enumerate(SPACE.functions, 'functions')) {
    setups[index(c.mask, c.option)] = assertSetup(CATALOGUE, c, random);
  }
  assert.equal(setups.length, SPACE.functionCases);

  let pairs = 0;
  const leaving = new Set<PartId>();
  for (let mask = 0; mask < 2 ** SPACE.functions.length; mask++) {
    for (let option = 0; option < OPTION_SETS.length; option++) {
      for (let bit = 0; bit < SPACE.functions.length; bit++) {
        if ((mask & (1 << bit)) !== 0) continue;
        const where = `${JSON.stringify({ functions: subset(SPACE.functions, mask), options: OPTION_SETS[option] })} plus the function ${SPACE.functions[bit]}`;
        const { left } = assertGrows(setups[index(mask, option)] as Setup, setups[index(mask | (1 << bit), option)] as Setup, where);
        for (const id of left) leaving.add(id);
        pairs += 1;
      }
    }
  }
  assert.equal(pairs, (SPACE.functionCases * SPACE.functions.length) / 2);
  // Every part that leaves is one a rule adds under a negation or an option.
  for (const id of leaving) {
    const rules = CATALOGUE.rules.filter((r) => r.adds === id);
    assert.ok(rules.length > 0 && rules.every((r) => negated(r.when).length > 0 || optionKeys(r.when).length > 0), id);
  }
});

test('every subset of the parts you own: the setup holds properties 1, 4 to 8 and 10; adding a part holds 2 and 3', () => {
  const random = generator(SEED + 1);
  const setups: Setup[] = [];
  const index = (mask: number, option: number): number => mask * OPTION_SETS.length + option;
  for (const c of enumerate(SPACE.selectable, 'owned')) {
    setups[index(c.mask, c.option)] = assertSetup(CATALOGUE, c, random);
  }
  assert.equal(setups.length, SPACE.partCases);

  let pairs = 0;
  for (let mask = 0; mask < 2 ** SPACE.selectable.length; mask++) {
    for (let option = 0; option < OPTION_SETS.length; option++) {
      for (let bit = 0; bit < SPACE.selectable.length; bit++) {
        if ((mask & (1 << bit)) !== 0) continue;
        const where = `${JSON.stringify({ owned: subset(SPACE.selectable, mask), options: OPTION_SETS[option] })} plus the part ${SPACE.selectable[bit]}`;
        assertGrows(setups[index(mask, option)] as Setup, setups[index(mask | (1 << bit), option)] as Setup, where);
        pairs += 1;
      }
    }
  }
  assert.equal(pairs, (SPACE.partCases * SPACE.selectable.length) / 2);
});

test(`${MIXED} mixed selections from the generator: the setup holds properties 1, 4 to 8 and 10; adding a function or a part holds 2 and 3`, () => {
  const random = generator(SEED + 2);
  let count = 0;
  for (const c of sample(SPACE, MIXED, SEED)) {
    const setup = assertSetup(CATALOGUE, c, random);
    count += 1;
    // One more function and one more part, each drawn from what the selection lacks.
    const freeFunctions = SPACE.functions.filter((id) => !c.selection.functions.includes(id));
    if (freeFunctions.length > 0) {
      const id = freeFunctions[Math.floor(random() * freeFunctions.length)] as string;
      const more = resolve(CATALOGUE, { ...c.selection, functions: [...c.selection.functions, id] });
      assertGrows(setup, more, `${c.where} plus the function ${id}`);
    }
    const freeParts = SPACE.selectable.filter((id) => !c.selection.owned.includes(id));
    if (freeParts.length > 0) {
      const id = freeParts[Math.floor(random() * freeParts.length)] as string;
      const more = resolve(CATALOGUE, { ...c.selection, owned: [...c.selection.owned, id] });
      assertGrows(setup, more, `${c.where} plus the part ${id}`);
    }
  }
  assert.equal(count, MIXED);
});

test('property 9: the functions a set of parts enables resolve to parts of that set', () => {
  let cases = 0;
  const extra = new Set<PartId>();
  for (const c of enumerate(SPACE.selectable, 'owned')) {
    const byParts = resolve(CATALOGUE, c.selection);
    const enabled = enabledIds(byParts);
    const byFunctions = resolve(CATALOGUE, { functions: enabled, owned: [], options: optionsOf(byParts) });
    const had = partIds(byParts);
    const where = `${c.where}, which enables ${enabled.join(', ') || 'no function'}`;

    // The same functions are enabled both ways, and no option moves.
    assert.deepEqual(enabledIds(byFunctions), enabled, `the enabled functions differ for ${where}`);
    assert.deepEqual(optionsOf(byFunctions), optionsOf(byParts), `the options differ for ${where}`);

    for (const p of byFunctions.parts) {
      if (had.has(p.part.id)) continue;
      // A part beyond the set: the visitor cannot tick it, a rule adds it, and
      // the rule excludes a part that the set holds by a tick alone.
      assert.ok(!p.part.selectable, `${p.part.id} can be ticked and is outside the set for ${where}`);
      for (const reason of p.addedBy) {
        assert.equal(reason.kind, 'rule', `${p.part.id} is outside the set, held by "${reason.kind}", for ${where}`);
        if (reason.kind !== 'rule') continue;
        const rule = RULES.get(reason.id) as Rule;
        const excluded = negated(rule.when).filter((id) => had.has(id));
        assert.ok(excluded.length > 0, `${p.part.id} is outside the set and rule ${rule.id} excludes no part of it, for ${where}`);
        for (const id of excluded) {
          const held = byParts.parts.find((x) => x.part.id === id) as ResolvedPart;
          assert.deepEqual(held.addedBy.map((r) => r.kind), ['owned'], `${id} is in both setups for ${where}`);
        }
      }
      extra.add(p.part.id);
    }
    cases += 1;
  }
  assert.equal(cases, SPACE.partCases);
  for (const id of extra) assert.ok(CATALOGUE.rules.some((r) => r.adds === id && negated(r.when).length > 0), id);
});

test('property 9, the other way: the parts of a set of functions enable those functions again', () => {
  for (const c of enumerate(SPACE.functions, 'functions')) {
    const byFunctions = resolve(CATALOGUE, c.selection);
    const owned = byFunctions.parts.filter((p) => p.part.selectable).map((p) => p.part.id);
    // A test object is no part to own: the tick of its plain drive connects it.
    const drives = byFunctions.parts.filter((p) => p.part.testObject === true).map((p) => plainDrive(CATALOGUE, p.part.id));
    assert.ok(drives.every((id) => id !== null), `a test object has no plain drive for ${c.where}`);
    const byParts = resolve(CATALOGUE, { functions: drives as string[], owned, options: optionsOf(byFunctions) });
    assert.deepEqual([...partIds(byParts)], [...partIds(byFunctions)], `the parts differ for ${c.where}`);
    assert.deepEqual(byParts.wires, byFunctions.wires, `the wires differ for ${c.where}`);
    const enabled = new Set(enabledIds(byParts));
    for (const id of c.selection.functions) assert.ok(enabled.has(id), `${id} is not enabled by its own parts for ${c.where}`);
    assert.deepEqual(enabledIds(byParts), enabledIds(byFunctions), `the enabled functions differ for ${c.where}`);
  }
});

test('property 10 on a changed catalogue: an item in no release is reported by the properties', () => {
  // A part on main only, which a released function needs.
  const parts = CATALOGUE.parts.map((p) => (p.id === 'sd' ? { ...p, release: 'main' as const } : p));
  const changed: Catalogue = { ...CATALOGUE, parts };
  const selection: Selection = { functions: ['logs'], owned: [], options: { ...(OPTION_SETS[0] as Options) } };
  assert.throws(
    () => assertSetup(changed, { selection, where: 'logs' }, generator(1)),
    /sd is in the setup and in no release/,
  );
  // An id of the "not in a release" list as a part of the setup.
  const first = CATALOGUE.notInRelease[0];
  assert.ok(first);
  const listed: Catalogue = {
    ...CATALOGUE,
    notInRelease: [...CATALOGUE.notInRelease, { ...first, id: 'sd' }],
  };
  assert.throws(
    () => assertSetup(listed, { selection, where: 'logs' }, generator(1)),
    /sd is on the "not in a release" list/,
  );
  assert.doesNotThrow(() => assertSetup(CATALOGUE, { selection, where: 'logs' }, generator(1)));
});

test('the properties report a setup that breaks them', () => {
  const selection: Selection = { functions: ['servo'], owned: [], options: { ...(OPTION_SETS[0] as Options) } };
  const one = { selection, where: 'servo' };
  const wire = (n: number): Wire => CATALOGUE.wires.find((w) => w.n === n) as Wire;
  const withWires = (wires: Wire[]): Catalogue => ({ ...CATALOGUE, wires });

  // 6: an end on a part outside the setup, and on a pad that does not exist.
  assert.throws(() => assertSetup(withWires([...CATALOGUE.wires, { ...wire(7), n: 90, b: 'esc.SIG', when: null }]), one, generator(1)),
    /a part outside the setup/);
  assert.throws(() => assertSetup(withWires([...CATALOGUE.wires, { ...wire(7), n: 90, a: 'pad.41', when: null }]), one, generator(1)),
    /ends on pad\.41/);
  assert.throws(() => assertSetup(withWires([...CATALOGUE.wires, { ...wire(7), n: 90, b: 'servo.XYZ', when: null }]), one, generator(1)),
    /undeclared pin servo\.XYZ/);
  // 6: a pin of another board variant.
  const variant: Catalogue = {
    ...CATALOGUE,
    parts: CATALOGUE.parts.map((p) => (p.id !== 'servo' ? p : {
      ...p, pins: p.pins.map((pin) => (pin.id === 'SIG' ? { ...pin, only: { connection: 'osoyoo' as const } } : pin)),
    })),
  };
  assert.throws(() => assertSetup(variant, one, generator(1)), /a pin of another board variant/);
  // 7: two nets on one pin.
  assert.throws(() => assertSetup(withWires([...CATALOGUE.wires, { ...wire(7), n: 90, a: 'pad.6', net: 'other', when: null }]), one, generator(1)),
    /servo\.SIG carries the nets/);
  // 8: the same two ends twice, in either direction, and a wire number twice.
  assert.throws(() => assertSetup(withWires([...CATALOGUE.wires, { ...wire(7), n: 90, a: wire(7).b as string, b: wire(7).a, when: null }]), one, generator(1)),
    /wires 7 and 90 are both/);
  assert.throws(() => assertSetup(withWires([...CATALOGUE.wires, { ...wire(7) }]), one, generator(1)),
    /wire number 7 occurs twice/);
  // 4: a rule that names a part it does not add.
  const rules = CATALOGUE.rules.map((r) => (r.id === 'servo-supply' ? { ...r, id: 'bus-pullups' } : r));
  assert.throws(() => assertSetup({ ...CATALOGUE, rules }, one, generator(1)), /names the rule bus-pullups, which does not add it/);
});

test('properties 2 and 3 report a part or a function that leaves without a cause', () => {
  const options = { ...(OPTION_SETS[0] as Options) };
  const before = resolve(CATALOGUE, { functions: ['servo'], owned: [], options });
  const after = resolve(CATALOGUE, { functions: ['servo', 'servotest'], owned: [], options });
  // The servo supply leaves for the PD mini: the cause is named.
  assert.deepEqual(assertGrows(before, after, 'servo plus servotest').left, ['ssup']);

  // A test object a function holds.
  const noServo: Setup = { ...after, parts: after.parts.filter((p) => p.part.id !== 'servo') };
  assert.throws(() => assertGrows(before, noServo, 'x'), /servo is held by "function" and leaves the setup/);
  // A part a function holds, which the visitor can tick.
  const noPd: Setup = { ...after, parts: after.parts.filter((p) => p.part.id !== 'pdmini') };
  assert.throws(() => assertGrows(after, noPd, 'x'), /pdmini can be ticked and leaves the setup/);
  // A locked part.
  const noPanel: Setup = { ...after, parts: after.parts.filter((p) => p.part.id !== 'panel') };
  assert.throws(() => assertGrows(before, noPanel, 'x'), /panel is held by "always" and leaves the setup/);
  // A rule part that leaves while its rule holds.
  const noSupply: Setup = { ...before, parts: before.parts.filter((p) => p.part.id !== 'ssup') };
  assert.throws(() => assertGrows(before, noSupply, 'x'), /rule servo-supply holds and ssup is missing/);
  // A function that is enabled, then not, with no part missing and no option taken.
  const off: Setup = { ...after, functions: after.functions.map((f) => (f.fn.id === 'servo' ? { ...f, enabled: false } : f)) };
  assert.throws(() => assertGrows(before, off, 'x'), /servo is no longer enabled and nothing names the cause/);
  const lost: Setup = { ...after, functions: after.functions.map((f) => (f.fn.id === 'servo' ? { ...f, enabled: false, missing: ['servo'] } : f)) };
  assert.throws(() => assertGrows(before, lost, 'x'), /servo loses the parts servo/);
});

test('a function that forces an option: the option names it, and the part of the other value leaves', () => {
  const forcing = CATALOGUE.functions.filter((f) => f.forces !== undefined && Object.keys(f.forces).length > 0);
  assert.ok(forcing.length > 0);
  for (const fn of forcing) {
    for (const key of OPTION_KEYS) {
      const value = fn.forces?.[key];
      if (value === undefined) continue;
      for (const options of OPTION_SETS) {
        const setup = resolve(CATALOGUE, { functions: [fn.id], owned: [], options: { ...options } });
        const option = setup.options.find((o) => o.key === key);
        assert.ok(option);
        assert.equal(option.value, value);
        assert.equal(option.forcedBy, fn.id);
        assert.ok(setup.functions.find((f) => f.fn.id === fn.id)?.enabled);
      }
    }
  }
});
