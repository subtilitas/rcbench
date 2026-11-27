import { test } from 'node:test';
import assert from 'node:assert/strict';
import { crc32 as zlibCrc32 } from 'node:zlib';
import { BOARDS, ESP_IMAGE_CHIP_ID } from '../src/boards.ts';
import type { Chip } from '../src/boards.ts';
import { KNOWN, activeSlot, boardsOf, checkEsp, crc32, judge, otaSeq, readHead } from '../src/installed.ts';
import type { AppHead, Reader } from '../src/installed.ts';
import { NODE_PARTS, PANEL_TABLE_V0_23_15, makeAppHead, makeInstalled, makeOtaEntry } from './fixtures.ts';

const ESP_BOARDS = BOARDS.filter((b) => b.chip !== 'rp2350').map((b) => b.id);
const head = (chip: Chip, project: string, kind: number | null): AppHead => ({ chip, project, version: '0.23.16', kind });

test('CRC-32 is zlib\'s, and the otadata CRC of sequence 1, 2, 3 is the one otatool.py writes', () => {
  const data = new TextEncoder().encode('123456789');
  assert.equal(crc32(data), 0xcbf43926);
  assert.equal(crc32(data, 0xffffffff), zlibCrc32(data, 0xffffffff));
  const seq = (n: number): Uint8Array => new Uint8Array(new Uint32Array([n]).buffer);
  assert.equal(crc32(seq(1), 0xffffffff), 0x4743989a);
  assert.equal(crc32(seq(2), 0xffffffff), 0x55f63774);
  assert.equal(crc32(seq(3), 0xffffffff), 0xed4a5011);
});

test('an otadata entry counts with a matching CRC and a state other than invalid or aborted', () => {
  assert.equal(otaSeq(makeOtaEntry(5)), 5);
  assert.equal(otaSeq(makeOtaEntry(5, 0xffffffff)), 5);
  assert.equal(otaSeq(makeOtaEntry(5, 3)), null);
  assert.equal(otaSeq(makeOtaEntry(5, 4)), null);
  assert.equal(otaSeq(makeOtaEntry(5, 2, 0x12345678)), null);
  assert.equal(otaSeq(new Uint8Array(32).fill(0xff)), null);
  assert.equal(otaSeq(new Uint8Array(31)), null);
});

test('the active slot is (highest valid sequence - 1) modulo the slot count; none without a valid entry', () => {
  const blank = new Uint8Array(32).fill(0xff);
  assert.equal(activeSlot([blank, blank], 2), null);
  assert.equal(activeSlot([makeOtaEntry(1), blank], 2), 0);
  assert.equal(activeSlot([makeOtaEntry(1), makeOtaEntry(2)], 2), 1);
  assert.equal(activeSlot([makeOtaEntry(4), makeOtaEntry(3)], 2), 1);
  assert.equal(activeSlot([makeOtaEntry(5), makeOtaEntry(2, 2, 0)], 2), 0);
  assert.equal(activeSlot([makeOtaEntry(3), makeOtaEntry(4, 3)], 2), 0);
  assert.equal(activeSlot([makeOtaEntry(2)], 0), null);
});

test('the head gives chip, project, version and the role record; erased or another layout gives none', () => {
  assert.deepEqual(readHead(makeAppHead(9, 'rcbench-dial', '0.23.16', 1)), head('esp32s3', 'rcbench-dial', 1));
  assert.deepEqual(readHead(makeAppHead(18, 'rcbench-panel', '0.23.16')), head('esp32p4', 'rcbench-panel', null));
  // A head cut before the role record: no role.
  assert.equal(readHead(makeAppHead(13, 'rcbench-node', '0.23.16', 3).subarray(0, 200))!.kind, null);
  assert.equal(readHead(new Uint8Array(296).fill(0xff)), null);
  const noDesc = makeAppHead(13, 'rcbench-node', '0.23.16', 2);
  noDesc[32] = 0;
  assert.equal(readHead(noDesc), null);
  assert.equal(readHead(makeAppHead(99, 'x', '1'))!.chip, null);
});

test('KNOWN holds every ESP32 board of BOARDS once, and no RP2350 board', () => {
  assert.deepEqual(KNOWN.map((k) => k.board).sort(), [...ESP_BOARDS].sort());
  for (const k of KNOWN) assert.equal(BOARDS.find((b) => b.id === k.board)!.chip, k.chip, k.board);
  for (const k of KNOWN) assert.ok(k.chip in ESP_IMAGE_CHIP_ID);
});

// Every installed firmware the page knows, with and without the role record,
// against every ESP32 board: m match, o another device, ? cannot tell.
const HEADS: [string, AppHead][] = [
  ['panel', head('esp32p4', 'rcbench-panel', null)],
  ['dial', head('esp32s3', 'rcbench-dial', 1)],
  ['dial, no role', head('esp32s3', 'rcbench-dial', null)],
  ['dial K518', head('esp32s3', 'rcbench-dial', 5)],
  ['display node', head('esp32s3', 'rcbench-node', 4)],
  ['display node, no role', head('esp32s3', 'rcbench-node', null)],
  ['power node', head('esp32c6', 'rcbench-node', 2)],
  ['balance node', head('esp32c6', 'rcbench-node', 3)],
  ['C6 node, no role', head('esp32c6', 'rcbench-node', null)],
  ['C6 radio', head('esp32c6', 'network_adapter', null)],
  ['another project', head('esp32c6', 'hello_world', null)],
  ['node on the P4', head('esp32p4', 'rcbench-node', null)],
  ['node of an unknown role', head('esp32c6', 'rcbench-node', 9)],
];
// The K518 came after the role record: an rcbench-dial without one is the
// M5Stack Dial's.
//                                panel  dial  dial-k518  node-display  node-pwr  node-bal  c6
const WANT: Record<string, string> = {
  panel: /*                    */ 'm o o o o o o',
  dial: /*                     */ 'o m o o o o o',
  'dial, no role': /*          */ 'o m o o o o o',
  'dial K518': /*              */ 'o o m o o o o',
  'display node': /*           */ 'o o o m o o o',
  'display node, no role': /*  */ 'o o o m o o o',
  'power node': /*             */ 'o o o o m o o',
  'balance node': /*           */ 'o o o o o m o',
  'C6 node, no role': /*       */ 'o o o o ? ? o',
  'C6 radio': /*               */ 'o o o o o o m',
  'another project': /*        */ '? ? ? ? ? ? ?',
  'node on the P4': /*         */ '? ? ? ? ? ? ?',
  'node of an unknown role': /**/ '? ? ? ? ? ? ?',
};
const COLS = ['panel', 'dial', 'dial-k518', 'node-display', 'node-pwr', 'node-bal', 'c6'];

test('every board against every installed firmware: matches, another device, cannot tell', () => {
  assert.deepEqual([...COLS].sort(), [...ESP_BOARDS].sort());
  for (const [name, h] of HEADS) {
    const got = COLS.map((b) => ({ match: 'm', other: 'o', unknown: '?' })[judge(b, h).kind]).join(' ');
    assert.equal(got, WANT[name], name);
  }
});

test('another device names its boards; the ambiguous pair is the power and the balance node', () => {
  assert.deepEqual(boardsOf(head('esp32c6', 'rcbench-node', null)), ['node-pwr', 'node-bal']);
  const v = judge('c6', head('esp32c6', 'rcbench-node', null));
  assert.equal(v.kind, 'other');
  assert.deepEqual(v.kind === 'other' && v.boards, ['node-pwr', 'node-bal']);
  const a = judge('node-bal', head('esp32c6', 'rcbench-node', null));
  assert.deepEqual(a.kind === 'unknown' && [a.why, a.boards], ['ambiguous', ['node-pwr', 'node-bal']]);
  const d = judge('node-display', head('esp32s3', 'rcbench-dial', 1));
  assert.deepEqual(d.kind === 'other' && d.boards, ['dial']);
  const k = judge('dial', head('esp32s3', 'rcbench-dial', 5));
  assert.deepEqual(k.kind === 'other' && k.boards, ['dial-k518']);
  assert.deepEqual(judge('dial', null), { kind: 'unknown', why: 'no-app' });
});

/** A flash of 0xFF with the parts in place, and a reader that logs each read. */
function flash(parts: [number, Uint8Array][]): { rd: Reader; reads: [number, number][] } {
  const f = new Uint8Array(0x900000).fill(0xff);
  for (const [o, b] of parts) f.set(b, o);
  const reads: [number, number][] = [];
  return { reads, rd: async (a, n) => { reads.push([a, n]); return f.slice(a, a + n); } };
}

test('a node: the table at 0x8000, both otadata entries, the head of ota_0; 4 reads, 3,432 bytes', async () => {
  const { rd, reads } = flash(makeInstalled({ chipId: 13, project: 'rcbench-node', version: '0.23.16', kind: 3, tableOffset: 0x8000, parts: NODE_PARTS }));
  const v = await checkEsp(rd, 'esp32c6', 'node-bal');
  assert.equal(v.kind, 'match');
  assert.deepEqual(reads, [[0x8000, 0xc00], [0xf000, 32], [0x10000, 32], [0x20000, 296]]);
  assert.equal(reads.reduce((n, r) => n + r[1], 0), 3432);
});

test('otadata selects ota_1: its head decides', async () => {
  const parts = makeInstalled({ chipId: 9, project: 'rcbench-dial', version: '0.23.16', kind: 1, tableOffset: 0x8000, parts: NODE_PARTS, slot: 1 });
  // ota_0 holds a display node's firmware, the boot loader starts ota_1.
  parts.push([0x20000, makeAppHead(9, 'rcbench-node', '0.23.12', 4)]);
  const v = await checkEsp(flash(parts).rd, 'esp32s3', 'node-display');
  assert.deepEqual(v.kind === 'other' && [v.boards, v.head.version], [['dial'], '0.23.16']);
});

test('the panel: 0xC000 first on the ESP32-P4, then 0x8000 when 0xC000 holds no table', async () => {
  const panel = flash([[0xc000, PANEL_TABLE_V0_23_15], [0x20000, makeAppHead(18, 'rcbench-panel', '0.23.16')]]);
  assert.equal((await checkEsp(panel.rd, 'esp32p4', 'panel')).kind, 'match');
  assert.deepEqual(panel.reads.map((r) => r[0]), [0xc000, 0x14000, 0x15000, 0x20000]);
  const moved = flash(makeInstalled({ chipId: 18, project: 'rcbench-panel', version: '0.23.16', tableOffset: 0x8000, parts: NODE_PARTS }));
  assert.equal((await checkEsp(moved.rd, 'esp32p4', 'panel')).kind, 'match');
  assert.deepEqual(moved.reads[0], [0xc000, 0xc00]);
  assert.deepEqual(moved.reads[1], [0x8000, 0xc00]);
});

test('a blank board: no table at either offset, two reads; an erased slot: no application', async () => {
  const blank = flash([]);
  assert.deepEqual(await checkEsp(blank.rd, 'esp32c6', 'c6'), { kind: 'unknown', why: 'blank' });
  assert.equal(blank.reads.length, 2);
  const noApp = flash(makeInstalled({ chipId: 13, project: 'x', version: '1', tableOffset: 0x8000, parts: NODE_PARTS }).slice(0, 1));
  assert.deepEqual(await checkEsp(noApp.rd, 'esp32c6', 'c6'), { kind: 'unknown', why: 'no-app', detail: 'ota_0 0x20000' });
});

test('a table with a damaged MD5 entry counts as none', async () => {
  const parts = makeInstalled({ chipId: 13, project: 'network_adapter', version: '2.13.0', tableOffset: 0x8000, parts: NODE_PARTS });
  parts[0]![1][NODE_PARTS.length * 32 + 20] ^= 1;
  assert.equal((await checkEsp(flash(parts).rd, 'esp32c6', 'c6')).kind, 'unknown');
});
