import { test } from 'node:test';
import assert from 'node:assert/strict';
import { checkImage } from '../src/boards.ts';
import { bootloaderEnd, findTable, planWrite, readImageTable } from '../src/partitions.ts';
import type { PlanVerdict } from '../src/partitions.ts';
import {
  NODE_PARTS, PANEL_BOOTLOADER_HEADER_V0_23_15, PANEL_BOOTLOADER_SEGMENTS_V0_23_15, PANEL_TABLE_V0_23_15,
  makeBootloader, makeMerged, makeTable,
} from './fixtures.ts';
import type { PartSpec } from './fixtures.ts';

/** The size of rcbench-panel-merged.bin of v0.23.15 in bytes. */
const PANEL_SIZE = 7_572_864;

function panelImage(table: Uint8Array = PANEL_TABLE_V0_23_15, size = PANEL_SIZE): Uint8Array {
  return makeMerged({
    chipId: 18, bootloaderOffset: 0x2000, tableOffset: 0xc000, table, size,
    bootloader: makeBootloader(18, PANEL_BOOTLOADER_SEGMENTS_V0_23_15, PANEL_BOOTLOADER_HEADER_V0_23_15),
  });
}

const nodeImage = (parts: readonly PartSpec[] = NODE_PARTS, size = 777_408, tableOffset = 0x8000): Uint8Array =>
  makeMerged({ chipId: 13, bootloaderOffset: 0, tableOffset, table: makeTable(parts), size });

const refusal = (v: PlanVerdict): string => (v.ok ? 'ok' : v.reason);

test('the panel table of v0.23.15 parses as gen_esp32part.py lists it, at 0xC000 behind a bootloader that ends at 0x81D0', () => {
  const img = panelImage();
  assert.deepEqual(checkImage('esp32p4', img, 'rcbench-panel-merged.bin'), { ok: true, format: 'bin' });
  assert.equal(bootloaderEnd(img, 0x2000), 0x81d0);
  const t = findTable(img, 0x2000);
  assert.equal(t.offset, 0xc000);
  assert.deepEqual(t.entries.map((p) => [p.name, p.type, p.subtype, p.offset, p.size]), [
    ['nvs', 1, 0x02, 0xd000, 0x6000],
    ['phy_init', 1, 0x01, 0x13000, 0x1000],
    ['otadata', 1, 0x00, 0x14000, 0x2000],
    ['ota_0', 0, 0x10, 0x20000, 0x7c0000],
    ['boardart', 1, 0x40, 0x7e0000, 0x20000],
    ['ota_1', 0, 0x11, 0x800000, 0x7c0000],
  ]);
});

test('keep settings on the panel: everything but nvs 0xD000+0x6000 is written, otadata included', () => {
  const v = planWrite(panelImage(), 0x2000, true);
  assert.ok(v.ok);
  assert.deepEqual(v.plan.kept, [{ name: 'nvs', subtype: 'nvs', offset: 0xd000, size: 0x6000 }]);
  assert.deepEqual(v.plan.ranges, [{ offset: 0, size: 0xd000 }, { offset: 0x13000, size: PANEL_SIZE - 0x13000 }]);
  assert.equal(v.plan.table, 0xc000);
  const written = v.plan.ranges.reduce((n, r) => n + r.size, 0);
  assert.equal(written, PANEL_SIZE - 0x6000);
  assert.ok(v.plan.ranges.some((r) => r.offset <= 0x14000 && r.offset + r.size >= 0x16000), 'otadata is written');
});

test('without keep settings the whole image goes to 0x0, as today', () => {
  const img = panelImage();
  assert.deepEqual(planWrite(img, 0x2000, false), { ok: true, plan: { ranges: [{ offset: 0, size: PANEL_SIZE }], kept: [], table: null } });
  // A whole-image write needs no table at all.
  assert.equal(refusal(planWrite(new Uint8Array(4096).fill(0xff), 0, false)), 'ok');
});

test('the Dial, the nodes and the C6 radio keep nvs 0x9000+0x6000 (C6: 0x4000) with the table at 0x8000', () => {
  const v = planWrite(nodeImage(), 0, true);
  assert.ok(v.ok);
  assert.equal(v.plan.table, 0x8000);
  assert.deepEqual(v.plan.ranges, [{ offset: 0, size: 0x9000 }, { offset: 0xf000, size: 777_408 - 0xf000 }]);
  const c6 = [
    { name: 'nvs', type: 1, subtype: 0x02, offset: 0x9000, size: 0x4000 },
    { name: 'otadata', type: 1, subtype: 0x00, offset: 0xd000, size: 0x2000 },
    { name: 'phy_init', type: 1, subtype: 0x01, offset: 0xf000, size: 0x1000 },
    { name: 'ota_0', type: 0, subtype: 0x10, offset: 0x10000, size: 0x1e0000 },
    { name: 'ota_1', type: 0, subtype: 0x11, offset: 0x1f0000, size: 0x1e0000 },
  ];
  const w = planWrite(makeMerged({ chipId: 13, bootloaderOffset: 0, tableOffset: 0x8000, table: makeTable(c6), size: 1_349_152, appOffset: 0x10000 }), 0, true);
  assert.ok(w.ok);
  assert.deepEqual(w.plan.kept.map((k) => [k.name, k.offset, k.size]), [['nvs', 0x9000, 0x4000]]);
  assert.deepEqual(w.plan.ranges, [{ offset: 0, size: 0x9000 }, { offset: 0xd000, size: 1_349_152 - 0xd000 }]);
});

test('nvs_keys, fat, spiffs and littlefs are kept as well; partitions past the end of the image are not listed', () => {
  const parts: PartSpec[] = [
    { name: 'nvs', type: 1, subtype: 0x02, offset: 0x9000, size: 0x4000 },
    { name: 'nvs_key', type: 1, subtype: 0x04, offset: 0xd000, size: 0x1000 },
    { name: 'otadata', type: 1, subtype: 0x00, offset: 0xe000, size: 0x2000 },
    { name: 'ota_0', type: 0, subtype: 0x10, offset: 0x10000, size: 0x40000 },
    { name: 'fs', type: 1, subtype: 0x83, offset: 0x50000, size: 0x10000 },
    { name: 'ffat', type: 1, subtype: 0x81, offset: 0x60000, size: 0x10000 },
    { name: 'spiffs', type: 1, subtype: 0x82, offset: 0x200000, size: 0x10000 },
  ];
  const v = planWrite(nodeImage(parts, 0x68000), 0, true);
  assert.ok(v.ok);
  assert.deepEqual(v.plan.kept.map((k) => [k.name, k.subtype, k.offset, k.size]), [
    ['nvs', 'nvs', 0x9000, 0x4000], ['nvs_key', 'nvs_keys', 0xd000, 0x1000],
    ['fs', 'littlefs', 0x50000, 0x10000], ['ffat', 'fat', 0x60000, 0x8000],
  ]);
  assert.deepEqual(v.plan.ranges, [{ offset: 0, size: 0x9000 }, { offset: 0xe000, size: 0x42000 }]);
});

test('a table without a settings partition gives a whole-image plan that names its table', () => {
  const parts = NODE_PARTS.filter((p) => p.name !== 'nvs');
  const v = planWrite(nodeImage(parts), 0, true);
  assert.ok(v.ok);
  assert.deepEqual(v.plan.kept, []);
  assert.deepEqual(v.plan.ranges, [{ offset: 0, size: 777_408 }]);
  assert.equal(v.plan.table, 0x8000);
});

test('keep settings is refused without a table, with a broken table and with two tables', () => {
  const img = panelImage();
  img.fill(0xff, 0xc000, 0xd000);
  assert.equal(refusal(planWrite(img, 0x2000, true)), 'no-table');
  const md5 = Uint8Array.from(PANEL_TABLE_V0_23_15);
  md5[8] ^= 0x01;
  const bad = planWrite(panelImage(md5), 0x2000, true);
  assert.equal(refusal(bad), 'md5');
  assert.ok(!bad.ok && bad.detail.startsWith('0xC000'));
  assert.equal(refusal(planWrite(nodeImage(NODE_PARTS, 777_408), 0, true)), 'ok');
  const noMd5 = makeMerged({ chipId: 13, bootloaderOffset: 0, tableOffset: 0x8000, table: makeTable(NODE_PARTS, { md5: false }), size: 0x40000 });
  assert.equal(refusal(planWrite(noMd5, 0, true)), 'md5-missing');
  const noEnd = nodeImage();
  noEnd.fill(0x00, 0x8000 + 6 * 32, 0x8000 + 0xc00);
  assert.equal(refusal(planWrite(noEnd, 0, true)), 'end');
  const badMagic = makeTable(NODE_PARTS);
  badMagic[64] = 0x55;
  assert.equal(refusal(planWrite(nodeImage(), 0, true)), 'ok');
  const m = nodeImage();
  m.set(badMagic, 0x8000);
  assert.equal(refusal(planWrite(m, 0, true)), 'magic');
  const two = nodeImage();
  two.set(makeTable(NODE_PARTS), 0x7000);
  const v2 = planWrite(two, 0, true);
  assert.equal(refusal(v2), 'two-tables');
  assert.ok(!v2.ok && v2.detail === '0x7000, 0x8000');
  const noBl = nodeImage();
  noBl[0] = 0x00;
  assert.equal(refusal(planWrite(noBl, 0, true)), 'bootloader');
});

test('keep settings is refused when a kept range overlaps the bootloader, the table or an application, or is not sector-aligned', () => {
  const inTable: PartSpec[] = [{ name: 'nvs', type: 1, subtype: 0x02, offset: 0x8000, size: 0x6000 }, ...NODE_PARTS.slice(1)];
  assert.equal(refusal(planWrite(nodeImage(inTable), 0, true)), 'layout');
  const overApp: PartSpec[] = [...NODE_PARTS.slice(0, 3), { name: 'nvs2', type: 1, subtype: 0x02, offset: 0x1f000, size: 0x2000 }, ...NODE_PARTS.slice(3)];
  assert.equal(refusal(planWrite(nodeImage(overApp), 0, true)), 'layout');
  const odd: PartSpec[] = [{ name: 'nvs', type: 1, subtype: 0x02, offset: 0x9000, size: 0x5800 }, ...NODE_PARTS.slice(1)];
  const v = planWrite(nodeImage(odd), 0, true);
  assert.equal(refusal(v), 'align');
  assert.ok(!v.ok && v.detail === 'nvs 0x9000+0x5800');
  // A bootloader-type entry of ESP-IDF v6 lies below the table; a kept range over it is refused.
  const blEntry: PartSpec[] = [{ name: 'bootloader', type: 2, subtype: 0x00, offset: 0x0, size: 0x9000 }, ...NODE_PARTS];
  assert.equal(refusal(planWrite(nodeImage(blEntry), 0, true)), 'ok');
  const blOver: PartSpec[] = [{ name: 'bootloader', type: 2, subtype: 0x00, offset: 0x0, size: 0xa000 }, ...NODE_PARTS];
  assert.equal(refusal(planWrite(nodeImage(blOver), 0, true)), 'overlap');
});

test('readImageTable reports the reason in place of throwing', () => {
  assert.deepEqual(readImageTable(new Uint8Array(100), 0), { ok: false, reason: 'bootloader', detail: '0x0' });
  const r = readImageTable(nodeImage(), 0);
  assert.ok(r.ok && r.table.offset === 0x8000 && r.table.entries.length === 5);
});
