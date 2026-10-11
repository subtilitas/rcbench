// Invariant 8: the pin table is legal, and each rule of legality reports a
// table that breaks it.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Catalogue, PinDefault } from '../../contract.ts';
import { CATALOGUE } from '../catalogue.ts';
import { COMPARE_PARTNERS, checkPins, outputCatalogue, sdaAllowed, sliceOf } from '../tools/check.ts';
import type { Finding } from '../tools/check.ts';

function copy(): Catalogue {
  return structuredClone(CATALOGUE);
}

function pin(c: Catalogue, signal: PinDefault['signal']): PinDefault {
  const found = c.pinDefaults.find((d) => d.signal === signal);
  assert.ok(found, `${signal} is in the pin table`);
  return found;
}

/** Moves a signal to another GPIO and its pad. The wires stay where they are. */
function move(c: Catalogue, signal: PinDefault['signal'], gpio: number): void {
  const pad = c.pads.find((p) => p.gpio === gpio);
  assert.ok(pad, `GP${gpio} is on a pad`);
  const entry = pin(c, signal);
  entry.gpio = gpio;
  entry.pad = pad.n;
}

function reports(findings: Finding[], id: string, pattern: RegExp): void {
  const hit = findings.some((f) => f.level === 'error' && f.id === id && pattern.test(f.msg));
  assert.ok(hit, `no error "${id}" matching ${pattern}; got: ${findings.map((f) => `${f.id}: ${f.msg}`).join(' | ') || 'none'}`);
}

test('the pin table of the catalogue is legal', () => {
  assert.deepEqual(checkPins(CATALOGUE), []);
  assert.deepEqual(
    CATALOGUE.pinDefaults.map((d) => [d.signal, d.gpio, d.pad, d.choice]),
    [
      ['heartbeat', 3, 5, 'fixed'],
      ['sda', 16, 21, 'default'],
      ['scl', 17, 22, 'default'],
      ['tap', 22, 29, 'default'],
      ['esc', 5, 7, 'example'],
      ['servo', 2, 4, 'proposal'],
      ['pd_tx', 7, 10, 'proposal'],
      ['pd_rx', 6, 9, 'proposal'],
    ],
  );
});

test('PWM slices and compare-register partners', () => {
  assert.equal(sliceOf(0), 0);
  assert.equal(sliceOf(1), 0);
  assert.equal(sliceOf(2), 1);
  assert.equal(sliceOf(5), 2);
  assert.equal(sliceOf(15), 7);
  // The slice number wraps at GP16: GP16 shares slice 0 with GP0.
  assert.equal(sliceOf(16), 0);
  assert.equal(sliceOf(28), 6);
  for (const [a, b] of COMPARE_PARTNERS) {
    assert.equal(b, a + 16);
    assert.equal(sliceOf(a), sliceOf(b));
    assert.equal(a % 2, b % 2);
  }
  assert.equal(COMPARE_PARTNERS.length, 6);
});

test('the output catalogue is the 20 GPIOs that are neither reserved nor the heartbeat', () => {
  assert.deepEqual(outputCatalogue(CATALOGUE), [
    0, 1, 2, 4, 5, 6, 7, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28,
  ]);
  assert.deepEqual(sdaAllowed(CATALOGUE), [-1, 0, 4, 6, 14, 16, 18, 20, 26]);
  assert.deepEqual(pin(CATALOGUE, 'sda').allowed, [-1, 0, 4, 6, 14, 16, 18, 20, 26]);
  assert.deepEqual(pin(CATALOGUE, 'scl').allowed, [-1, 1, 5, 7, 15, 17, 19, 21, 27]);
});

test('rule 1: a GPIO outside the allowed set, or on a reserved pad', () => {
  const outside = copy();
  move(outside, 'servo', 3);
  reports(checkPins(outside), 'pin.servo', /GP3 is not in the allowed set/);

  const reserved = copy();
  move(reserved, 'esc', 8);
  reports(checkPins(reserved), 'pin.esc', /GP8 is on a reserved pad/);
  reports(checkPins(reserved), 'pin.esc', /not in the allowed set/);

  // Both ends of the reserved range, and the pads beside it.
  for (const gpio of [8, 12]) {
    const c = copy();
    move(c, 'esc', gpio);
    reports(checkPins(c), 'pin.esc', /reserved pad/);
  }
  for (const gpio of [7, 13]) {
    const c = copy();
    move(c, 'esc', gpio);
    assert.ok(!checkPins(c).some((f) => /reserved pad/.test(f.msg)), `GP${gpio}`);
  }

  const list = copy();
  pin(list, 'heartbeat').allowed = [4];
  reports(checkPins(list), 'pin.heartbeat', /GP3 is not in the allowed set/);

  const pad = copy();
  pin(pad, 'servo').pad = 6;
  reports(checkPins(pad), 'pin.servo', /pad 6 is not GP2/);

  const noPad = copy();
  pin(noPad, 'servo').pad = 41;
  reports(checkPins(noPad), 'pin.servo', /pad 41 is not GP2/);
});

test('rule 2: two signals on one GPIO', () => {
  const c = copy();
  move(c, 'servo', 5);
  reports(checkPins(c), 'pins', /GP5 carries two signals/);
});

test('rule 3: SCL is SDA + 1, and both lists are the ones the firmware offers', () => {
  const scl = copy();
  move(scl, 'scl', 19);
  reports(checkPins(scl), 'pin.scl', /GP19 is not SDA \+ 1/);

  // GP2 as SDA: its pair GP3 is the heartbeat input.
  const sda = copy();
  pin(sda, 'sda').allowed = [-1, 0, 2, 4, 6, 14, 16, 18, 20, 26];
  reports(checkPins(sda), 'pin.sda', /allowed set is not -1, 0, 4, 6, 14, 16, 18, 20, 26/);

  const follows = copy();
  pin(follows, 'scl').allowed = [-1, 0, 4, 6, 14, 16, 18, 20, 26];
  reports(checkPins(follows), 'pin.scl', /allowed set is not -1, 1, 5, 7, 15, 17, 19, 21, 27/);

  const open = copy();
  pin(open, 'sda').allowed = 'catalogue';
  reports(checkPins(open), 'pin.sda', /allowed set/);

  const missing = copy();
  missing.pinDefaults = missing.pinDefaults.filter((d) => d.signal !== 'scl');
  reports(checkPins(missing), 'pins', /sda or scl is missing/);
});

test('rule 4: two outputs on one PWM slice', () => {
  // GP4 and GP5 are slice 2.
  const c = copy();
  move(c, 'servo', 4);
  reports(checkPins(c), 'pins', /two outputs share PWM slice 2/);

  // GP21 is slice 2 as well, 16 pins up; it is also the compare partner of GP5.
  const wrapped = copy();
  move(wrapped, 'servo', 21);
  reports(checkPins(wrapped), 'pins', /two outputs share PWM slice 2/);

  // GP6 is slice 3: no finding of this rule.
  const free = copy();
  move(free, 'servo', 6);
  assert.ok(!checkPins(free).some((f) => /PWM slice/.test(f.msg)));
});

test('rule 5: an output whose compare-register partner carries a signal', () => {
  // GP0 is the example of the build guide; GP16 is the sensor bus SDA.
  const guide = copy();
  move(guide, 'servo', 0);
  reports(checkPins(guide), 'pin.servo', /GP0 shares a compare register with GP16 \(sda\)/);

  const scl = copy();
  move(scl, 'servo', 1);
  reports(checkPins(scl), 'pin.servo', /GP1 shares a compare register with GP17 \(scl\)/);

  const tap = copy();
  move(tap, 'esc', 6);
  reports(checkPins(tap), 'pin.esc', /GP6 shares a compare register with GP22 \(tap\)/);

  // The partner of an output is another output.
  const outputs = copy();
  move(outputs, 'esc', 18);
  reports(checkPins(outputs), 'pin.esc', /GP18 shares a compare register with GP2 \(servo\)/);
  reports(checkPins(outputs), 'pin.servo', /GP2 shares a compare register with GP18 \(esc\)/);

  // A bus pin on the partner of a free GPIO is no finding: the rule reads outputs.
  const bus = copy();
  move(bus, 'tap', 21);
  reports(checkPins(bus), 'pin.esc', /GP5 shares a compare register with GP21 \(tap\)/);
  assert.ok(!checkPins(bus).some((f) => f.id === 'pin.tap'));

  // GP13 has no partner.
  const none = copy();
  move(none, 'servo', 13);
  assert.ok(!checkPins(none).some((f) => /compare register/.test(f.msg)));
});

test('rule 6: the tap pin is not GP26 to GP28', () => {
  for (const gpio of [26, 27, 28]) {
    const c = copy();
    move(c, 'tap', gpio);
    reports(checkPins(c), 'pin.tap', new RegExp(`GP${gpio} is an analogue pin`));
  }
  const below = copy();
  move(below, 'tap', 21);
  assert.ok(!checkPins(below).some((f) => /analogue/.test(f.msg)));

  const missing = copy();
  missing.pinDefaults = missing.pinDefaults.filter((d) => d.signal !== 'tap');
  reports(checkPins(missing), 'pins', /tap is missing/);
});

test('rule 7: the heartbeat pin is GP3 and fixed', () => {
  const moved = copy();
  move(moved, 'heartbeat', 4);
  reports(checkPins(moved), 'pin.heartbeat', /not fixed on GP3/);

  const choice = copy();
  pin(choice, 'heartbeat').choice = 'default';
  reports(checkPins(choice), 'pin.heartbeat', /not fixed on GP3/);

  const missing = copy();
  missing.pinDefaults = missing.pinDefaults.filter((d) => d.signal !== 'heartbeat');
  reports(checkPins(missing), 'pins', /heartbeat is missing/);
});

test('wires and the pin table agree', () => {
  // Every wire on a GPIO pad names the choice of that pad's signal.
  const gpioPads = new Set(CATALOGUE.pads.filter((p) => p.gpio !== null).map((p) => `pad.${p.n}`));
  for (const wire of CATALOGUE.wires) {
    const onGpio = gpioPads.has(wire.a) || (wire.b !== null && gpioPads.has(wire.b));
    assert.equal(wire.pin !== undefined, onGpio, `wire ${wire.n}`);
  }
  assert.deepEqual(CATALOGUE.wires.filter((w) => w.pin === 'proposal').map((w) => w.n), [7, 16, 17]);
  assert.deepEqual(CATALOGUE.wires.filter((w) => w.pin === 'example').map((w) => w.n), [11]);
  assert.deepEqual(CATALOGUE.wires.filter((w) => w.pin === 'fixed').map((w) => w.n), [4, 6]);
  assert.deepEqual(CATALOGUE.wires.filter((w) => w.src.kind === 'page').map((w) => w.n), [7]);
  assert.deepEqual(CATALOGUE.wires.filter((w) => w.src.kind === 'owner').map((w) => w.n), [16, 17, 18, 30, 55, 56, 57, 58]);

  const choice = copy();
  choice.wires[6]!.pin = 'default';
  reports(checkPins(choice), 'wire.7', /pin choice is default; pad 4 \(servo\) is proposal/);

  const none = copy();
  delete choice.wires[6]!.pin;
  delete none.wires[10]!.pin;
  reports(checkPins(none), 'wire.11', /pin choice is undefined/);

  const extra = copy();
  extra.wires[0]!.pin = 'fixed';
  reports(checkPins(extra), 'wire.1', /ends on no GPIO pad/);

  const page = copy();
  page.wires[0]!.src = { kind: 'page' };
  reports(checkPins(page), 'wire.1', /without a proposed pin/);

  // A wire on a GPIO that carries no signal of the table.
  const stray = copy();
  stray.wires[6]!.a = 'pad.1';
  reports(checkPins(stray), 'wire.7', /pad 1 carries no signal/);

  const note = copy();
  note.wires[6]!.note = { en: 'Bind it as SERVO PWM.', de: 'Als SERVO PWM binden.' };
  reports(checkPins(note), 'wire.7', /Pin proposed by this page/);

  const noNote = copy();
  noNote.wires[15]!.note = null;
  reports(checkPins(noNote), 'wire.16', /Pin from an owner statement of/);

  // The note follows the source: a page proposal on an owner pin, and the reverse.
  const wrongNote = copy();
  wrongNote.wires[15]!.note = { en: 'Pin proposed by this page.', de: 'Pin ist ein Vorschlag dieser Seite.' };
  reports(checkPins(wrongNote), 'wire.16', /Pin from an owner statement of/);
  const wrongSrc = copy();
  wrongSrc.wires[6]!.src = { kind: 'owner', date: '2026-10-11' };
  reports(checkPins(wrongSrc), 'wire.7', /Pin from an owner statement of/);
  reports(checkPins(wrongSrc), 'wire.7', /no check entry states that GP2 is from an owner statement/);

  const example = copy();
  example.wires[10]!.note = null;
  reports(checkPins(example), 'wire.11', /example/);

  // A proposed pin without a check entry that says so.
  const check = copy();
  check.checks = check.checks.filter((k) => k.id !== 'servo-pin');
  reports(checkPins(check), 'wire.7', /no check entry states that GP2 is a proposal/);

  const pd = copy();
  pd.checks = pd.checks.filter((k) => k.id !== 'pd-pins');
  reports(checkPins(pd), 'wire.16', /GP7 is from an owner statement/);
  reports(checkPins(pd), 'wire.17', /GP6 is from an owner statement/);
});
