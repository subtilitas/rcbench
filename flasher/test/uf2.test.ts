import { test } from 'node:test';
import assert from 'node:assert/strict';
import { FAMILY, FLAG_NOT_MAIN_FLASH, Uf2Error, familyName, isUf2, parseUf2, regionBytes } from '../src/uf2.ts';
import { concat, makeUf2, uf2Block } from './fixtures.ts';

const code = (f: () => unknown): string => {
  try { f(); } catch (e) { if (e instanceof Uf2Error) return e.code; throw e; }
  return 'none';
};

test('an RP2350 ARM-S image parses into one region with its family', () => {
  const img = parseUf2(makeUf2(0x10000000, 8));
  assert.equal(img.family, FAMILY.RP2350_ARM_S);
  assert.equal(familyName(img.family), 'rp2350-arm-s');
  assert.equal(img.pages, 8);
  assert.equal(img.regions.length, 1);
  assert.equal(img.regions[0]!.addr, 0x10000000);
  assert.equal(regionBytes(img.regions), 8 * 256);
});

test('an RP2350 RISC-V image keeps its family', () => {
  const img = parseUf2(makeUf2(0x10000000, 3, FAMILY.RP2350_RISCV));
  assert.equal(img.family, 0xe48bff5a);
  assert.equal(familyName(img.family), 'rp2350-riscv');
});

test('the absolute family and the ARM non-secure family are read', () => {
  assert.equal(parseUf2(makeUf2(0x10002000, 2, FAMILY.ABSOLUTE)).family, 0xe48bff57);
  assert.equal(parseUf2(makeUf2(0x10002000, 2, FAMILY.RP2350_ARM_NS)).family, 0xe48bff5b);
  assert.equal(familyName(0x12345678), '0x12345678');
});

test('the RP2350-E10 workaround block is dropped', () => {
  const img = parseUf2(makeUf2(0x10000000, 4, FAMILY.RP2350_ARM_S, true));
  assert.equal(img.droppedE10, true);
  assert.equal(img.pages, 4);
  assert.equal(img.regions[0]!.addr, 0x10000000);
});

test('pages with a hole between them give two regions, the payload in order', () => {
  const bytes = concat([
    uf2Block({ addr: 0x10000000, no: 0, count: 3, fill: 1 }),
    uf2Block({ addr: 0x10000100, no: 1, count: 3, fill: 2 }),
    uf2Block({ addr: 0x10010000, no: 2, count: 3, fill: 3 }),
  ]);
  const img = parseUf2(bytes);
  assert.deepEqual(img.regions.map((r) => [r.addr, r.data.length]), [[0x10000000, 512], [0x10010000, 256]]);
  assert.equal(img.regions[0]!.data[256], bytes[512 + 32]);
});

test('blocks out of order are sorted by address', () => {
  const img = parseUf2(concat([
    uf2Block({ addr: 0x10000100, no: 1, count: 2 }),
    uf2Block({ addr: 0x10000000, no: 0, count: 2 }),
  ]));
  assert.deepEqual(img.regions.map((r) => r.addr), [0x10000000]);
});

test('a missing block number is a gap', () => {
  const bytes = concat([
    uf2Block({ addr: 0x10000000, no: 0, count: 3 }),
    uf2Block({ addr: 0x10000200, no: 2, count: 3 }),
  ]);
  assert.equal(code(() => parseUf2(bytes)), 'gap');
});

test('a repeated block number and a run of another length are refused', () => {
  assert.equal(code(() => parseUf2(concat([
    uf2Block({ addr: 0x10000000, no: 0, count: 2 }),
    uf2Block({ addr: 0x10000100, no: 0, count: 2 }),
  ]))), 'count');
  assert.equal(code(() => parseUf2(concat([
    uf2Block({ addr: 0x10000000, no: 0, count: 2 }),
    uf2Block({ addr: 0x10000100, no: 1, count: 3 }),
  ]))), 'count');
});

test('a bad magic number names its block', () => {
  const bytes = makeUf2(0x10000000, 3);
  bytes[512 + 508] ^= 0xff;
  try { parseUf2(bytes); assert.fail('parsed'); } catch (e) {
    assert.ok(e instanceof Uf2Error);
    assert.equal(e.code, 'magic');
    assert.equal(e.block, 1);
  }
  const start = makeUf2(0x10000000, 1);
  start[0] = 0;
  assert.equal(isUf2(start), false);
  assert.equal(code(() => parseUf2(start)), 'magic');
});

test('a length that is no multiple of 512 bytes and an empty file are refused', () => {
  assert.equal(code(() => parseUf2(makeUf2(0x10000000, 2).subarray(0, 700))), 'size');
  assert.equal(code(() => parseUf2(new Uint8Array(0))), 'empty');
});

test('two families among the written blocks are refused', () => {
  assert.equal(code(() => parseUf2(concat([
    uf2Block({ addr: 0x10000000, no: 0, count: 2, family: FAMILY.RP2350_ARM_S }),
    uf2Block({ addr: 0x10000100, no: 1, count: 2, family: FAMILY.RP2350_RISCV }),
  ]))), 'mixed-families');
});

test('an address outside the flash window, unaligned or twice is refused', () => {
  assert.equal(code(() => parseUf2(makeUf2(0x20000000, 1))), 'address');
  assert.equal(code(() => parseUf2(makeUf2(0x10000080, 1))), 'address');
  assert.equal(code(() => parseUf2(makeUf2(0x10ffff00 + 256, 1))), 'address');
  assert.equal(code(() => parseUf2(concat([
    uf2Block({ addr: 0x10000000, no: 0, count: 2 }),
    uf2Block({ addr: 0x10000000, no: 1, count: 2 }),
  ]))), 'overlap');
});

test('a payload other than 256 bytes and a block without family are refused', () => {
  assert.equal(code(() => parseUf2(uf2Block({ addr: 0x10000000, no: 0, count: 1, size: 476 }))), 'payload');
  assert.equal(code(() => parseUf2(uf2Block({ addr: 0x10000000, no: 0, count: 1, flags: 0 }))), 'no-family');
});

test('blocks flagged "not main flash" are skipped', () => {
  const img = parseUf2(concat([
    uf2Block({ addr: 0x20000000, no: 0, count: 1, flags: FLAG_NOT_MAIN_FLASH }),
    uf2Block({ addr: 0x10000000, no: 0, count: 1 }),
  ]));
  assert.equal(img.pages, 1);
});
