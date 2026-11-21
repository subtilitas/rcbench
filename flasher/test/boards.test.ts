import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BOARDS, chipFromEspName, chipMatches, checkImage, espImageInfo } from '../src/boards.ts';
import { FAMILY } from '../src/uf2.ts';
import { makeEspImage, makeUf2 } from './fixtures.ts';

test('every board of the page has a chip, and the planned Core2350B is the one board without a release', () => {
  assert.deepEqual(BOARDS.filter((b) => b.planned).map((b) => b.id), ['core2350b']);
  assert.equal(BOARDS.find((b) => b.id === 'panel')?.chip, 'esp32p4');
  assert.equal(BOARDS.find((b) => b.id === 'dial')?.chip, 'esp32s3');
  assert.equal(BOARDS.find((b) => b.id === 'node-display')?.chip, 'esp32s3');
  assert.equal(BOARDS.find((b) => b.id === 'node-pwr')?.chip, 'esp32c6');
  assert.equal(BOARDS.find((b) => b.id === 'node-bal')?.chip, 'esp32c6');
  assert.equal(BOARDS.find((b) => b.id === 'iomcu-rbmod')?.chip, 'rp2350');
});

test('esptool-js chip names map to the page chips', () => {
  assert.equal(chipFromEspName('ESP32-P4'), 'esp32p4');
  assert.equal(chipFromEspName('ESP32-S3'), 'esp32s3');
  assert.equal(chipFromEspName('ESP32-C6'), 'esp32c6');
  assert.equal(chipFromEspName('ESP32-C3'), null);
  assert.equal(chipMatches('esp32s3', 'esp32s3'), true);
  assert.equal(chipMatches('esp32s3', 'esp32c6'), false);
  assert.equal(chipMatches('esp32s3', null), false);
});

test('the image header names the chip: merged images at 0x0, the P4 bootloader at 0x2000', () => {
  assert.deepEqual(espImageInfo(makeEspImage(9)), { chip: 'esp32s3', chipId: 9, kind: 'merged', headerOffset: 0 });
  assert.deepEqual(espImageInfo(makeEspImage(13)), { chip: 'esp32c6', chipId: 13, kind: 'merged', headerOffset: 0 });
  assert.deepEqual(espImageInfo(makeEspImage(18, { headerOffset: 0x2000 })), { chip: 'esp32p4', chipId: 18, kind: 'merged', headerOffset: 0x2000 });
  assert.equal(espImageInfo(makeEspImage(9, { app: true }))?.kind, 'app');
  assert.equal(espImageInfo(new Uint8Array(100)), null);
});

test('a merged image of the board chip passes, every other image is refused with its reason', () => {
  assert.deepEqual(checkImage('esp32s3', makeEspImage(9), 'd.bin'), { ok: true, format: 'bin' });
  assert.deepEqual(checkImage('esp32p4', makeEspImage(18, { headerOffset: 0x2000 }), 'p.bin'), { ok: true, format: 'bin' });
  const reason = (r: ReturnType<typeof checkImage>): string => (r.ok ? 'ok' : r.reason);
  assert.equal(reason(checkImage('esp32c6', makeEspImage(9), 'd.bin')), 'chip');
  assert.equal(reason(checkImage('esp32s3', makeEspImage(9, { app: true }), 'd.bin')), 'app-only');
  assert.equal(reason(checkImage('esp32p4', makeEspImage(18, { app: true }), 'p.bin')), 'app-only');
  assert.equal(reason(checkImage('esp32p4', makeEspImage(18), 'p.bin')), 'offset');
  assert.equal(reason(checkImage('esp32s3', new Uint8Array(8192), 'x.bin')), 'not-esp');
  assert.equal(reason(checkImage('esp32s3', makeUf2(0x10000000, 2), 'x.uf2')), 'not-esp');
});

test('an RP2350 board takes an RP2350 UF2 file or a raw binary, never an ESP image or an RP2040 UF2', () => {
  const reason = (r: ReturnType<typeof checkImage>): string => (r.ok ? r.format : r.reason);
  assert.equal(reason(checkImage('rp2350', makeUf2(0x10000000, 2), 'a.uf2')), 'uf2');
  assert.equal(reason(checkImage('rp2350', makeUf2(0x10000000, 2, FAMILY.RP2350_RISCV), 'a.uf2')), 'uf2');
  assert.equal(reason(checkImage('rp2350', makeUf2(0x10000000, 2, FAMILY.RP2040), 'a.uf2')), 'family');
  assert.equal(reason(checkImage('rp2350', new Uint8Array(1024), 'a.bin')), 'bin');
  assert.equal(reason(checkImage('rp2350', makeEspImage(9), 'a.bin')), 'not-rp2350');
  const broken = makeUf2(0x10000000, 3);
  broken.copyWithin(512, 1024);
  assert.equal(reason(checkImage('rp2350', broken, 'a.uf2')), 'uf2');
});
