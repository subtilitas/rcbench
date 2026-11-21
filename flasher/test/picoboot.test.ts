import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  CMD, PICOBOOT_MAGIC, argsExclusive, argsGetInfo, argsRange, argsReboot2, decodeLocation, decodeStatus,
  decodeTargetPartition, encodeCommand, statusName,
} from '../src/picoboot.ts';

const u32 = (b: Uint8Array, off: number): number => new DataView(b.buffer, b.byteOffset).getUint32(off, true);

test('a command is 32 bytes: magic, token, id, argument length, transfer length, arguments', () => {
  const c = encodeCommand({ token: 7, id: CMD.WRITE, args: argsRange(0x10002000, 0x1000), transferLength: 0x1000 });
  assert.equal(c.length, 32);
  assert.equal(u32(c, 0), PICOBOOT_MAGIC);
  assert.deepEqual([...c.subarray(0, 4)], [0x0b, 0xd1, 0x1f, 0x43]);
  assert.equal(u32(c, 4), 7);
  assert.equal(c[8], 0x05);
  assert.equal(c[9], 8);
  assert.equal(c[10], 0);
  assert.equal(c[11], 0);
  assert.equal(u32(c, 12), 0x1000);
  assert.equal(u32(c, 16), 0x10002000);
  assert.equal(u32(c, 20), 0x1000);
  assert.deepEqual([...c.subarray(24)], [0, 0, 0, 0, 0, 0, 0, 0]);
});

test('the command IDs are the boot ROM values, bit 7 set for the IN commands', () => {
  assert.deepEqual(CMD, { EXCLUSIVE_ACCESS: 1, FLASH_ERASE: 3, READ: 0x84, WRITE: 5, EXIT_XIP: 6, REBOOT2: 0x0a, GET_INFO: 0x8b });
  for (const id of [CMD.READ, CMD.GET_INFO]) assert.ok(id & 0x80);
  for (const id of [CMD.WRITE, CMD.FLASH_ERASE, CMD.REBOOT2, CMD.EXCLUSIVE_ACCESS]) assert.equal(id & 0x80, 0);
});

test('exclusive access carries 1 byte, exit XIP none', () => {
  const ex = encodeCommand({ token: 1, id: CMD.EXCLUSIVE_ACCESS, args: argsExclusive(1), transferLength: 0 });
  assert.equal(ex[9], 1);
  assert.equal(ex[16], 1);
  const xip = encodeCommand({ token: 2, id: CMD.EXIT_XIP, args: new Uint8Array(0), transferLength: 0 });
  assert.equal(xip[9], 0);
});

test('REBOOT2 carries flags, delay and 2 parameters: 16 bytes', () => {
  const c = encodeCommand({ token: 3, id: CMD.REBOOT2, args: argsReboot2(0, 500, 0xffffffff, 0), transferLength: 0 });
  assert.equal(c[9], 16);
  assert.deepEqual([u32(c, 16), u32(c, 20), u32(c, 24), u32(c, 28)], [0, 500, 0xffffffff, 0]);
});

test('GET_INFO carries the type in byte 0 and 3 parameter words from byte 4', () => {
  const a = argsGetInfo(3, [0xe48bff59]);
  assert.equal(a.length, 16);
  assert.equal(a[0], 3);
  assert.equal(u32(a, 4), 0xe48bff59);
  assert.equal(u32(a, 8), 0);
  const c = encodeCommand({ token: 4, id: CMD.GET_INFO, args: a, transferLength: 256 });
  assert.equal(c[9], 16);
  assert.equal(u32(c, 12), 256);
});

test('more than 16 argument bytes are refused', () => {
  assert.throws(() => encodeCommand({ token: 1, id: 1, args: new Uint8Array(17), transferLength: 0 }), RangeError);
});

test('the status answer decodes token, code, command and progress', () => {
  const b = new Uint8Array(16);
  const v = new DataView(b.buffer);
  v.setUint32(0, 42, true);
  v.setUint32(4, 4, true);
  b[8] = 0x05;
  b[9] = 1;
  assert.deepEqual(decodeStatus(b), { token: 42, code: 4, cmdId: 5, inProgress: true });
  assert.equal(statusName(4), 'INVALID_ADDRESS');
  assert.equal(statusName(10), 'NOT_PERMITTED');
  assert.equal(statusName(99), 'status 99');
  assert.throws(() => decodeStatus(new Uint8Array(8)), RangeError);
});

test('a partition location gives its first byte and the byte after its last sector', () => {
  // Sectors 2 to 510: slot A of the IOmcu, 8K to 2044K.
  assert.deepEqual(decodeLocation(2 | (510 << 13) | (0x3f << 26)), { start: 0x2000, end: 0x1ff000 });
});

const answer = (index: number, loc: number): Uint8Array => {
  const b = new Uint8Array(256);
  const v = new DataView(b.buffer);
  v.setUint32(0, 3, true);
  v.setInt32(4, index, true);
  v.setUint32(8, loc, true);
  return b;
};

test('the UF2 target partition answer: a partition, absolute space, or refused', () => {
  assert.deepEqual(decodeTargetPartition(answer(1, 511 | (1019 << 13))), { index: 1, start: 0x1ff000, end: 0x3fc000 });
  const abs = decodeTargetPartition(answer(0xff, 0));
  assert.ok(abs !== 'refused');
  assert.equal(abs.index, null);
  assert.equal(decodeTargetPartition(answer(-1, 0)), 'refused');
});
