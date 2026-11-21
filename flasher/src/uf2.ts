// UF2 (USB Flashing Format) reader for RP2350 images.
//
// A UF2 file is a run of 512-byte blocks. Each block carries a target address,
// a payload of up to 476 bytes and, with flag 0x2000, a family ID in the field
// that otherwise holds the file size. The block layout and the flag values are
// the public UF2 specification (github.com/microsoft/uf2). The family IDs are
// the values pico-sdk 2.3.0 and picotool 2.3.0 use.
//
// The reader accepts what the RP2350 boot ROM writes to flash: 256-byte
// payloads at 256-byte aligned addresses inside the 16MB flash window at
// 0x10000000. It drops the RP2350-E10 workaround block: a single absolute
// block at 0x10ffff00 in a run of 2 blocks, which picotool adds for a
// drag-and-drop to an RP2350 of stepping A2 and skips on a load over
// PICOBOOT. Blocks flagged "not main flash" are skipped as picotool skips them.

export const UF2_BLOCK = 512;
export const UF2_PAYLOAD = 256;
const MAGIC_START0 = 0x0a324655;
const MAGIC_START1 = 0x9e5d5157;
const MAGIC_END = 0x0ab16f30;

export const FLAG_NOT_MAIN_FLASH = 0x0000_0001;
export const FLAG_FAMILY_ID_PRESENT = 0x0000_2000;

/** Family IDs of the RP2 boot ROMs (pico-sdk `boot/uf2.h`). */
export const FAMILY = {
  RP2040: 0xe48bff56,
  ABSOLUTE: 0xe48bff57,
  DATA: 0xe48bff58,
  RP2350_ARM_S: 0xe48bff59,
  RP2350_RISCV: 0xe48bff5a,
  RP2350_ARM_NS: 0xe48bff5b,
} as const;

/** Families an RP2350 boot ROM takes into flash. */
export const RP2350_FAMILIES: readonly number[] = [
  FAMILY.ABSOLUTE, FAMILY.RP2350_ARM_S, FAMILY.RP2350_RISCV, FAMILY.RP2350_ARM_NS,
];

export const FLASH_BASE = 0x1000_0000;
/** End of the flash window an RP2350 maps for one chip select, 16MB. */
export const FLASH_END = 0x1100_0000;
const E10_ADDR = 0x10ff_ff00;

export type Uf2ErrorCode =
  | 'empty' | 'size' | 'magic' | 'payload' | 'no-family' | 'mixed-families'
  | 'address' | 'overlap' | 'gap' | 'count';

/** A reason the file is not a UF2 image the page writes. */
export class Uf2Error extends Error {
  readonly code: Uf2ErrorCode;
  readonly block: number;
  constructor(code: Uf2ErrorCode, block: number, detail: string) {
    super(`UF2 ${code} at block ${block}: ${detail}`);
    this.code = code;
    this.block = block;
  }
}

/** A run of consecutive flash bytes, at an address in the 0x10000000 window. */
export interface Region {
  addr: number;
  data: Uint8Array;
}

export interface Uf2Image {
  family: number;
  /** Consecutive pages merged, sorted by address. */
  regions: Region[];
  /** Pages written: 256 bytes each. */
  pages: number;
  /** True when the RP2350-E10 workaround block was dropped. */
  droppedE10: boolean;
}

/** Name of a family ID for messages, or its value in hex. */
export function familyName(id: number): string {
  for (const [name, value] of Object.entries(FAMILY)) if (value === id) return name.toLowerCase().replace(/_/g, '-');
  return `0x${id.toString(16).padStart(8, '0')}`;
}

/** True when the bytes start with a UF2 block. */
export function isUf2(bytes: Uint8Array): boolean {
  if (bytes.length < UF2_BLOCK) return false;
  const v = new DataView(bytes.buffer, bytes.byteOffset, 8);
  return v.getUint32(0, true) === MAGIC_START0 && v.getUint32(4, true) === MAGIC_START1;
}

interface Block {
  index: number;
  flags: number;
  addr: number;
  size: number;
  no: number;
  count: number;
  family: number;
  payload: Uint8Array;
}

function readBlock(bytes: Uint8Array, index: number): Block {
  const off = index * UF2_BLOCK;
  const v = new DataView(bytes.buffer, bytes.byteOffset + off, UF2_BLOCK);
  if (v.getUint32(0, true) !== MAGIC_START0 || v.getUint32(4, true) !== MAGIC_START1
    || v.getUint32(UF2_BLOCK - 4, true) !== MAGIC_END) {
    throw new Uf2Error('magic', index, 'a magic number differs');
  }
  const size = v.getUint32(16, true);
  return {
    index,
    flags: v.getUint32(8, true),
    addr: v.getUint32(12, true),
    size,
    no: v.getUint32(20, true),
    count: v.getUint32(24, true),
    family: v.getUint32(28, true),
    payload: bytes.subarray(off + 32, off + 32 + Math.min(size, 476)),
  };
}

const isE10 = (b: Block): boolean =>
  b.family === FAMILY.ABSOLUTE && b.addr === E10_ADDR && b.count === 2 && b.no === 0;

/**
 * Reads a UF2 file into flash regions.
 *
 * Fails on a file whose length is no multiple of 512 bytes, a block with a
 * wrong magic number, a payload other than 256 bytes, a block without a family
 * ID, two families among the blocks that are written, an address outside the
 * flash window or not 256-byte aligned, two blocks for one address, and a
 * block number missing from or repeated in its run.
 */
export function parseUf2(bytes: Uint8Array): Uf2Image {
  if (bytes.length === 0) throw new Uf2Error('empty', 0, 'the file is empty');
  if (bytes.length % UF2_BLOCK !== 0) throw new Uf2Error('size', Math.floor(bytes.length / UF2_BLOCK), `${bytes.length} bytes is no multiple of 512`);
  const blocks: Block[] = [];
  let droppedE10 = false;
  for (let i = 0; i < bytes.length / UF2_BLOCK; i++) {
    const b = readBlock(bytes, i);
    if (b.flags & FLAG_NOT_MAIN_FLASH) continue;
    if (!(b.flags & FLAG_FAMILY_ID_PRESENT)) throw new Uf2Error('no-family', i, 'the block carries no family ID');
    if (isE10(b)) { droppedE10 = true; continue; }
    if (b.size !== UF2_PAYLOAD) throw new Uf2Error('payload', i, `payload of ${b.size} bytes, 256 expected`);
    blocks.push(b);
  }
  if (blocks.length === 0) throw new Uf2Error('empty', 0, 'no block writes flash');
  const family = blocks[0]!.family;
  const count = blocks[0]!.count;
  const seen = new Set<number>();
  const byAddr = new Map<number, Block>();
  for (const b of blocks) {
    if (b.family !== family) throw new Uf2Error('mixed-families', b.index, `${familyName(b.family)} after ${familyName(family)}`);
    if (b.count !== count) throw new Uf2Error('count', b.index, `run of ${b.count} blocks, ${count} expected`);
    if (b.no >= count || seen.has(b.no)) throw new Uf2Error('count', b.index, `block number ${b.no} of ${count}`);
    seen.add(b.no);
    if (b.addr % UF2_PAYLOAD !== 0 || b.addr < FLASH_BASE || b.addr + UF2_PAYLOAD > FLASH_END) {
      throw new Uf2Error('address', b.index, `address 0x${b.addr.toString(16)}`);
    }
    if (byAddr.has(b.addr)) throw new Uf2Error('overlap', b.index, `address 0x${b.addr.toString(16)} twice`);
    byAddr.set(b.addr, b);
  }
  if (seen.size !== count) {
    let missing = 0;
    while (seen.has(missing)) missing++;
    throw new Uf2Error('gap', blocks.length, `block ${missing} of ${count} is missing`);
  }
  const addrs = [...byAddr.keys()].sort((a, b) => a - b);
  const regions: Region[] = [];
  let start = addrs[0]!;
  let run: Uint8Array[] = [];
  let next = start;
  const flush = (): void => {
    const data = new Uint8Array(run.length * UF2_PAYLOAD);
    run.forEach((p, i) => data.set(p, i * UF2_PAYLOAD));
    regions.push({ addr: start, data });
  };
  for (const a of addrs) {
    if (a !== next) { flush(); start = a; run = []; }
    run.push(byAddr.get(a)!.payload);
    next = a + UF2_PAYLOAD;
  }
  flush();
  return { family, regions, pages: addrs.length, droppedE10 };
}

/** Bytes the regions write in all. */
export const regionBytes = (regions: readonly Region[]): number => regions.reduce((n, r) => n + r.data.length, 0);
