// The panel part against the pin map of the panel firmware in this tree.
//
// A repository anchor reads its file at the release tag. The pin map of the
// 7-inch ESP32-P4 board is newer than the tag the data names, so this test
// reads `board_pins.h` of the working tree: the data and the firmware of one
// commit name the same pins. The heartbeat and the knob pins are on header
// P3; the heartbeat pin is confirmed and no check entry calls it unknown.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { CATALOGUE } from '../catalogue.ts';

const BOARD_PINS = 'firmware/panel/components/board/include/board_pins.h';
const ROOT = new URL('../../../', import.meta.url);

/** GPIOs of the 12-pin header P3 of the board (schematic page 1), after 3V3 and GND. */
const HEADER_GPIOS = [2, 3, 4, 5, 28, 29, 30, 31, 34, 36];
/** GPIOs of the ESP32-P4 that the ESP32-C6 radio uses. */
const C6_GPIOS = [6, 14, 15, 16, 17, 18, 19, 54];

type Define = { gpio: number };

/**
 * Reads `#define <name> GPIO_NUM_<n>` from the text of a C header.
 * @param header The text of the header.
 * @param name The name of the define.
 * @returns The GPIO number.
 * @throws Error when the header defines no `name`, or its value is no `GPIO_NUM_<n>`.
 */
export function defineGpio(header: string, name: string): Define {
  const line = header.split('\n').find((l) => new RegExp(`^#define\\s+${name}\\s`).test(l));
  if (line === undefined) throw new Error(`${name} is not defined`);
  const m = /GPIO_NUM_(\d+)\b/.exec(line);
  if (!m) throw new Error(`${name} names no GPIO_NUM_<n>`);
  return { gpio: Number(m[1]) };
}

const header = readFileSync(new URL(BOARD_PINS, ROOT), 'utf8');
const panel = CATALOGUE.parts.find((p) => p.id === 'panel')!;
const check = (id: string) => CATALOGUE.checks.find((k) => k.id === id);

test('defineGpio reads a define and refuses a missing or malformed one', () => {
  const text = [
    '#define A_PIN   GPIO_NUM_28   /* IO28 */',
    '#define B_PIN   GPIO_NUM_0',
    '#define C_PIN   28',
    '#define A_PIN_X GPIO_NUM_5',
  ].join('\n');
  assert.deepEqual(defineGpio(text, 'A_PIN'), { gpio: 28 });
  assert.deepEqual(defineGpio(text, 'B_PIN'), { gpio: 0 });
  assert.deepEqual(defineGpio(text, 'A_PIN_X'), { gpio: 5 });
  assert.throws(() => defineGpio(text, 'C_PIN'), /names no GPIO_NUM/);
  assert.throws(() => defineGpio(text, 'D_PIN'), /is not defined/);
  assert.throws(() => defineGpio('', 'A_PIN'), /is not defined/);
});

test('the panel is the 7-inch ESP32-P4 board, with no photo and its 4 wired pins', () => {
  assert.match(header, /ESP32-P4-WIFI6-Touch-LCD-7B/);
  assert.equal(panel.buy.length, 1);
  assert.equal(panel.buy[0]!.order.en, 'ESP32-P4-WIFI6-Touch-LCD-7B');
  assert.match(panel.buy[0]!.check.en, /chip revision 3\.2/);
  assert.match(panel.buy[0]!.check.de, /Chip-Revision 3\.2/);
  assert.equal(panel.pictures, undefined);
  assert.deepEqual(panel.pins.map((p) => p.id), ['CANH', 'CANL', 'CANG', 'HB']);
  // No ESP32-S3 part is left in the data.
  assert.ok(!JSON.stringify(CATALOGUE.parts).includes('ESP32-S3'));
  assert.ok(!CATALOGUE.wires.some((w) => [w.a, w.b].some((end) => end?.startsWith('panel.J8'))));
});

test('the CAN pins are those of header H11, whose pin 1 takes no wire', () => {
  assert.match(header, /header H11/);
  const text = check('panel-terminals')!.text;
  assert.match(text.en, /CAN header H11 has the print CAN with L, H, GND and 5V/);
  assert.match(text.en, /The pin 5V is the 5 V rail of the board and takes no wire/);
  assert.match(text.de, /Der Pin 5V ist die 5-V-Schiene der Platine und bekommt keinen Draht/);
  // 5V is drawn as a print, not a pin: no wire can end on it.
  assert.ok(!panel.pins.some((p) => /5V|5 V/.test(p.label.en)));
  assert.deepEqual(CATALOGUE.wires.filter((w) => w.a.startsWith('panel.')).map((w) => [w.n, w.a]),
    [[1, 'panel.CANH'], [2, 'panel.CANL'], [3, 'panel.CANG'], [4, 'panel.HB'], [5, 'panel.HB']]);
});

test('the heartbeat pin is PANEL_HEARTBEAT_PIN of the firmware, a GPIO of header P3, and confirmed', () => {
  const hb = defineGpio(header, 'PANEL_HEARTBEAT_PIN');
  assert.ok(HEADER_GPIOS.includes(hb.gpio), `GPIO${hb.gpio} is not on header P3`);
  assert.ok(!C6_GPIOS.includes(hb.gpio), `GPIO${hb.gpio} belongs to the ESP32-C6`);
  const pin = panel.pins.find((p) => p.id === 'HB')!;
  assert.equal(pin.label.en, `IO${hb.gpio}`);
  assert.equal(pin.label.de, `IO${hb.gpio}`);
  const steps = check('panel-terminals')!.text;
  assert.match(steps.en, new RegExp(`The heartbeat is the pin IO${hb.gpio} of`));
  assert.match(steps.de, new RegExp(`Der Heartbeat ist der Pin IO${hb.gpio} der`));
  // Confirmed by the schematic, and no entry of kind unknown names it.
  assert.equal(pin.confirmed, true);
  assert.equal(pin.src.kind, 'doc');
  assert.equal(check('panel-heartbeat-pin'), undefined);
  assert.ok(!CATALOGUE.checks.some((k) => k.kind === 'unknown' && /heartbeat/i.test(k.text.en)));
});

test('the knob pins of the firmware are named on header P3, and neither is a pin of the ESP32-C6', () => {
  const sda = defineGpio(header, 'BOARD_KNOB_PIN_SDA');
  const scl = defineGpio(header, 'BOARD_KNOB_PIN_SCL');
  for (const d of [sda, scl]) {
    assert.ok(HEADER_GPIOS.includes(d.gpio), `GPIO${d.gpio} is not on header P3`);
    assert.ok(!C6_GPIOS.includes(d.gpio), `GPIO${d.gpio} belongs to the ESP32-C6`);
  }
  const heartbeat = defineGpio(header, 'PANEL_HEARTBEAT_PIN').gpio;
  assert.equal(new Set([sda.gpio, scl.gpio, heartbeat]).size, 3);
  const knob = CATALOGUE.notInRelease.find((n) => n.id === 'knob')!;
  assert.match(knob.text.en, new RegExp(`IO${sda.gpio} \\(SDA\\) and IO${scl.gpio} \\(SCL\\)`));
  assert.match(knob.text.de, new RegExp(`IO${sda.gpio} \\(SDA\\) und IO${scl.gpio} \\(SCL\\)`));
  assert.match(knob.text.en, /12-pin header P3, with 3V3 and GND from the same header/);
  assert.match(knob.text.de, /12-poligen Buchse P3, dazu 3V3 und GND von derselben Buchse/);
  assert.doesNotMatch(knob.text.en, /placeholder|not decided/);
  assert.doesNotMatch(knob.text.de, /Platzhalter|nicht entschieden/);
});
