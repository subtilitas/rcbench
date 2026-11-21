// Synthetic images for the unit tests and the browser tests: UF2 files with
// chosen blocks, ESP-IDF images with a chosen chip, kind and offset, partition
// tables, and merged images with a bootloader, a table and an application.

import { createHash } from 'node:crypto';
import { FAMILY, FLAG_FAMILY_ID_PRESENT } from '../src/uf2.ts';

export interface BlockSpec {
  addr: number;
  no: number;
  count: number;
  family?: number;
  flags?: number;
  size?: number;
  fill?: number;
  magicEnd?: number;
}

/** One 512-byte UF2 block. */
export function uf2Block(b: BlockSpec): Uint8Array {
  const out = new Uint8Array(512);
  const v = new DataView(out.buffer);
  v.setUint32(0, 0x0a324655, true);
  v.setUint32(4, 0x9e5d5157, true);
  v.setUint32(8, b.flags ?? FLAG_FAMILY_ID_PRESENT, true);
  v.setUint32(12, b.addr, true);
  v.setUint32(16, b.size ?? 256, true);
  v.setUint32(20, b.no, true);
  v.setUint32(24, b.count, true);
  v.setUint32(28, b.family ?? FAMILY.RP2350_ARM_S, true);
  for (let i = 0; i < 256; i++) out[32 + i] = (b.fill ?? 0) + i * 7 + (b.addr >>> 8);
  v.setUint32(508, b.magicEnd ?? 0x0ab16f30, true);
  return out;
}

export const concat = (parts: Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let off = 0;
  for (const p of parts) { out.set(p, off); off += p.length; }
  return out;
};

/** A UF2 file of `count` consecutive pages from `addr`, with the E10 block first when asked. */
export function makeUf2(addr: number, count: number, family: number = FAMILY.RP2350_ARM_S, e10 = false): Uint8Array {
  const blocks: Uint8Array[] = [];
  if (e10) blocks.push(uf2Block({ addr: 0x10ffff00, no: 0, count: 2, family: FAMILY.ABSOLUTE, flags: FLAG_FAMILY_ID_PRESENT | 0x8000 }));
  for (let i = 0; i < count; i++) blocks.push(uf2Block({ addr: addr + i * 256, no: i, count, family }));
  return concat(blocks);
}

/**
 * An ESP-IDF image: zero-filled up to `headerOffset`, the header with magic
 * 0xE9 and the chip ID at byte 12, the application description magic at byte
 * 32 for an application image, then pattern bytes up to `size`.
 */
export function makeEspImage(chipId: number, opts: { headerOffset?: number; app?: boolean; size?: number } = {}): Uint8Array {
  const off = opts.headerOffset ?? 0;
  const out = new Uint8Array(opts.size ?? off + 4096);
  out.fill(0xff, 0, off);
  for (let i = off; i < out.length; i++) out[i] = (i * 31 + chipId) & 0xff;
  const v = new DataView(out.buffer);
  out[off] = 0xe9;
  out[off + 1] = 3;
  v.setUint16(off + 12, chipId, true);
  v.setUint32(off + 32, opts.app ? 0xabcd5432 : 0x50000000, true);
  return out;
}

/** One partition of a table built by makeTable. */
export interface PartSpec {
  name: string;
  type: number;
  subtype: number;
  offset: number;
  size: number;
}

/**
 * A partition table as gen_esp32part.py of ESP-IDF v6.1 writes it: entries of
 * 32 bytes (magic 0xAA 0x50), the MD5 entry (0xEB 0xEB, 14 bytes 0xFF, the MD5
 * of the entries), the end marker of 32 bytes 0xFF, padded with 0xFF to 0xC00.
 */
export function makeTable(parts: readonly PartSpec[], opts: { md5?: boolean } = {}): Uint8Array {
  const entries = new Uint8Array(parts.length * 32);
  const v = new DataView(entries.buffer);
  parts.forEach((p, i) => {
    const o = i * 32;
    entries.set([0xaa, 0x50, p.type, p.subtype], o);
    v.setUint32(o + 4, p.offset, true);
    v.setUint32(o + 8, p.size, true);
    entries.set(new TextEncoder().encode(p.name).subarray(0, 16), o + 12);
  });
  const out = new Uint8Array(0xc00).fill(0xff);
  out.set(entries);
  if (opts.md5 ?? true) {
    out.set([0xeb, 0xeb], entries.length);
    out.set(createHash('md5').update(entries).digest(), entries.length + 16);
  }
  return out;
}

/**
 * The partition table of rcbench-panel-merged.bin of release v0.23.15 (image
 * SHA-256 b5fb0b292473d27aa930a48c2c41a15e7b209e0fc2f23e5ddcff851967d8bb83),
 * 224 bytes at 0xC000: nvs, phy_init, otadata, ota_0, boardart, ota_1 and the
 * MD5 entry. The end marker and the padding follow as 0xFF.
 */
export const PANEL_TABLE_V0_23_15 = Uint8Array.from(Buffer.from(
  'aa50010200d00000006000006e76730000000000000000000000000000000000'
  + 'aa50010100300100001000007068795f696e6974000000000000000000000000'
  + 'aa50010000400100002000006f74616461746100000000000000000000000000'
  + 'aa5000100000020000007c006f74615f30000000000000000000000000000000'
  + 'aa50014000007e0000000200626f617264617274000000000000000000000000'
  + 'aa5000110000800000007c006f74615f31000000000000000000000000000000'
  + 'ebebffffffffffffffffffffffffffff8083df373145b77ce409a750c1aaee12', 'hex'));

/** The 24-byte image header of the panel's bootloader in v0.23.15, at 0x2000: 3 segments, SHA-256 appended. */
export const PANEL_BOOTLOADER_HEADER_V0_23_15 = Uint8Array.from(Buffer.from('e903025fcab2fa4fee0000001200002d018f010000000001', 'hex'));
/** Load address and length of each segment of that bootloader; it ends at 0x81D0. */
export const PANEL_BOOTLOADER_SEGMENTS_V0_23_15: readonly [number, number][] = [
  [0x4ffb60d0, 0x1680], [0x4ffab2c0, 0x12a0], [0x4ffadfc0, 0x385c],
];

/** A bootloader image: the header, then each segment's 8-byte header and its pattern data, padding, checksum, SHA-256. */
export function makeBootloader(chipId: number, segments: readonly [number, number][], header?: Uint8Array): Uint8Array {
  const parts: Uint8Array[] = [];
  const h = header ? Uint8Array.from(header) : new Uint8Array(24);
  if (!header) {
    h[0] = 0xe9;
    h[23] = 1;
    new DataView(h.buffer).setUint16(12, chipId, true);
  }
  h[1] = segments.length;
  parts.push(h);
  for (const [addr, len] of segments) {
    const s = new Uint8Array(8 + len);
    const v = new DataView(s.buffer);
    v.setUint32(0, addr, true);
    v.setUint32(4, len, true);
    for (let i = 8; i < s.length; i++) s[i] = (i * 13 + 5) & 0xff;
    parts.push(s);
  }
  let len = parts.reduce((n, p) => n + p.length, 0);
  const pad = 15 - (len % 16) + 1;
  parts.push(new Uint8Array(pad).fill(0x00));
  len += pad;
  if (h[23] === 1) parts.push(new Uint8Array(32).fill(0x3c));
  return concat(parts);
}

/**
 * A merged image as `esptool.py merge_bin` writes it: 0xFF wherever no part
 * lies, the bootloader at its chip's offset, the partition table at
 * `tableOffset`, an application header (0xE9, chip ID, description magic
 * 0xABCD5432) at the first app partition, pattern bytes after it up to `size`.
 */
export function makeMerged(opts: {
  chipId: number; bootloaderOffset: number; tableOffset: number; table: Uint8Array; size: number;
  bootloader?: Uint8Array; appOffset?: number;
}): Uint8Array {
  const out = new Uint8Array(opts.size).fill(0xff);
  const bl = opts.bootloader ?? makeBootloader(opts.chipId, [[0x40000000, 0x1200], [0x40001200, 0x800], [0x40002000, 0x2000]]);
  out.set(bl, opts.bootloaderOffset);
  out.set(opts.table.subarray(0, Math.min(opts.table.length, out.length - opts.tableOffset)), opts.tableOffset);
  const app = opts.appOffset ?? 0x20000;
  if (app + 64 <= out.length) {
    for (let i = app; i < out.length; i++) out[i] = (i * 31 + opts.chipId) & 0xff;
    out[app] = 0xe9;
    out[app + 1] = 4;
    new DataView(out.buffer).setUint16(app + 12, opts.chipId, true);
    new DataView(out.buffer).setUint32(app + 32, 0xabcd5432, true);
  }
  return out;
}

/** The table of the Dial, the nodes and (with other sizes) the C6 radio: nvs 0x9000, otadata, phy_init, ota_0, ota_1. */
export const NODE_PARTS: readonly PartSpec[] = [
  { name: 'nvs', type: 1, subtype: 0x02, offset: 0x9000, size: 0x6000 },
  { name: 'otadata', type: 1, subtype: 0x00, offset: 0xf000, size: 0x2000 },
  { name: 'phy_init', type: 1, subtype: 0x01, offset: 0x11000, size: 0x1000 },
  { name: 'ota_0', type: 0, subtype: 0x10, offset: 0x20000, size: 0x180000 },
  { name: 'ota_1', type: 0, subtype: 0x11, offset: 0x1a0000, size: 0x180000 },
];
