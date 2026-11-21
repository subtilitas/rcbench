// Synthetic images for the unit tests and the browser tests: UF2 files with
// chosen blocks, ESP-IDF images with a chosen chip, kind and offset.

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
