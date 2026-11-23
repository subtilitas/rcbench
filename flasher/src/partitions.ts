// The ESP-IDF partition table inside a merged image, and the write plan of an
// ESP32 board: which ranges of the image go to the flash, and which partition
// ranges stay as they are on the board so that its settings survive.
//
// Binary format after gen_esp32part.py of ESP-IDF v6.1
// (components/partition_table): entries of 32 bytes, little endian:
// magic 0xAA 0x50, type u8, subtype u8, offset u32, size u32, name 16 bytes
// (NUL padded), flags u32. After the last entry an MD5 entry: 0xEB 0xEB,
// 14 bytes 0xFF, then the MD5 of every entry before it. Then the end marker,
// 32 bytes 0xFF. The table takes at most 0xC00 bytes of its 4096-byte sector.

import { md5Hex } from './digest.ts';

export const SECTOR = 0x1000;
export const TABLE_MAX = 0xc00;
/** The highest offset the page looks for the table at: an application starts at 0x10000 at the lowest. */
export const TABLE_SEARCH_END = 0xf000;

export const TYPE_APP = 0x00;
export const TYPE_DATA = 0x01;
export const TYPE_BOOTLOADER = 0x02;
export const TYPE_TABLE = 0x03;

/**
 * Data subtypes that hold what a board keeps between starts: settings, the
 * key of encrypted NVS, file systems. A "keep settings" write leaves them out.
 */
export const KEEP_SUBTYPES: Readonly<Record<number, string>> = {
  0x02: 'nvs',
  0x04: 'nvs_keys',
  0x81: 'fat',
  0x82: 'spiffs',
  0x83: 'littlefs',
};

export interface Partition {
  name: string;
  type: number;
  subtype: number;
  offset: number;
  size: number;
  flags: number;
}

export interface Table {
  /** Offset of the table in the image and on the flash. */
  offset: number;
  entries: Partition[];
}

/** One stretch of the image the page writes, at the same offset on the flash. */
export interface Range {
  offset: number;
  size: number;
}

/** A partition range the page leaves as it is on the board. */
export interface Kept extends Range {
  name: string;
  subtype: string;
}

export interface WritePlan {
  /** The ranges of the image to write, ascending, each with its own MD5 check. */
  ranges: Range[];
  /** The partition ranges inside the image that are not written. */
  kept: Kept[];
  /** Offset of the partition table the plan comes from; null for a whole-image write. */
  table: number | null;
}

export type KeepRefusal =
  | 'bootloader' | 'no-table' | 'two-tables' | 'end' | 'md5-missing' | 'md5' | 'magic' | 'layout' | 'overlap' | 'align';

export type PlanVerdict = { ok: true; plan: WritePlan } | { ok: false; reason: KeepRefusal; detail: string };

export const hex = (n: number): string => `0x${n.toString(16).toUpperCase()}`;

class Refusal extends Error {
  readonly reason: KeepRefusal;
  readonly detail: string;
  constructor(reason: KeepRefusal, detail: string) {
    super(`${reason}: ${detail}`);
    this.reason = reason;
    this.detail = detail;
  }
}

/**
 * The end of the second-stage bootloader in the image: its ESP-IDF image
 * header at `offset` (24 bytes, segment count at byte 1, "hash appended" at
 * byte 23), the segments of 8 header bytes each plus their data, padding up
 * to the checksum byte in the last byte of a 16-byte block, then 32 bytes of
 * SHA-256 when appended. Null when the header or a segment runs past the image.
 */
export function bootloaderEnd(image: Uint8Array, offset: number): number | null {
  if (image.length < offset + 24 || image[offset] !== 0xe9) return null;
  const v = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const segments = image[offset + 1]!;
  let pos = offset + 24;
  for (let i = 0; i < segments; i++) {
    if (pos + 8 > image.length) return null;
    pos += 8 + v.getUint32(pos + 4, true);
  }
  let rel = pos - offset;
  rel += 15 - (rel % 16);
  rel += 1;
  if (image[offset + 23] === 1) rel += 32;
  return offset + rel <= image.length ? offset + rel : null;
}

const isFF = (b: Uint8Array): boolean => b.every((x) => x === 0xff);

/** Reads the table at `offset`; throws a Refusal for a broken one. */
export function readTable(image: Uint8Array, offset: number): Partition[] {
  const v = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const entries: Partition[] = [];
  let md5Seen = false;
  for (let o = 0; o < TABLE_MAX; o += 32) {
    const at = offset + o;
    if (at + 32 > image.length) break;
    const e = image.subarray(at, at + 32);
    if (isFF(e)) {
      if (!md5Seen) throw new Refusal('md5-missing', hex(offset));
      return entries;
    }
    if (md5Seen) throw new Refusal('end', hex(offset));
    if (e[0] === 0xeb && e[1] === 0xeb) {
      const want = md5Hex(image.subarray(offset, at));
      const got = Array.from(e.subarray(16), (b) => b.toString(16).padStart(2, '0')).join('');
      if (got !== want) throw new Refusal('md5', `${hex(offset)}: ${got}, ${want}`);
      md5Seen = true;
      continue;
    }
    if (e[0] !== 0xaa || e[1] !== 0x50) throw new Refusal('magic', hex(at));
    const nameBytes = e.subarray(12, 28);
    const nul = nameBytes.indexOf(0);
    entries.push({
      name: new TextDecoder().decode(nul < 0 ? nameBytes : nameBytes.subarray(0, nul)),
      type: e[2]!,
      subtype: e[3]!,
      offset: v.getUint32(at + 4, true),
      size: v.getUint32(at + 8, true),
      flags: v.getUint32(at + 28, true),
    });
  }
  throw new Refusal('end', hex(offset));
}

/** Holds the partitions to the rules of gen_esp32part.py: above the table sector, no two overlapping. */
function checkLayout(table: Table): void {
  const parts = table.entries.filter((p) => p.type !== TYPE_BOOTLOADER && p.type !== TYPE_TABLE)
    .sort((a, b) => a.offset - b.offset);
  let last: Partition | null = null;
  for (const p of parts) {
    if (p.offset < table.offset + SECTOR) throw new Refusal('layout', `${p.name} ${hex(p.offset)} < ${hex(table.offset + SECTOR)}`);
    if (last && p.offset < last.offset + last.size) throw new Refusal('layout', `${p.name} ${hex(p.offset)}, ${last.name} ${hex(last.offset)}+${hex(last.size)}`);
    last = p;
  }
}

/**
 * Finds the one partition table of a merged image. The page looks at every
 * 4096-byte step from the end of the bootloader up to 0xF000: ESP-IDF puts it
 * at 0x8000 unless CONFIG_PARTITION_TABLE_OFFSET moves it (0xC000 on the
 * panel). A sector that starts with the entry magic 0xAA 0x50 and fails the
 * checks is reported; two valid tables are refused, since the page cannot
 * tell which one the bootloader reads.
 */
export function findTable(image: Uint8Array, bootloaderOffset: number): Table {
  const blEnd = bootloaderEnd(image, bootloaderOffset);
  if (blEnd === null) throw new Refusal('bootloader', hex(bootloaderOffset));
  const found: Table[] = [];
  let broken: Refusal | null = null;
  for (let off = Math.ceil(blEnd / SECTOR) * SECTOR; off <= TABLE_SEARCH_END && off + 32 <= image.length; off += SECTOR) {
    if (image[off] !== 0xaa || image[off + 1] !== 0x50) continue;
    try {
      found.push({ offset: off, entries: readTable(image, off) });
    } catch (e) {
      if (!(e instanceof Refusal)) throw e;
      broken ??= e;
    }
  }
  if (found.length > 1) throw new Refusal('two-tables', found.map((t) => hex(t.offset)).join(', '));
  if (found.length === 0) throw broken ?? new Refusal('no-table', `${hex(Math.ceil(blEnd / SECTOR) * SECTOR)}-${hex(TABLE_SEARCH_END)}`);
  checkLayout(found[0]!);
  return found[0]!;
}

/** The partition table of an image, or the reason there is none to use. */
export function readImageTable(image: Uint8Array, bootloaderOffset: number): { ok: true; table: Table } | { ok: false; reason: KeepRefusal; detail: string } {
  try {
    return { ok: true, table: findTable(image, bootloaderOffset) };
  } catch (e) {
    if (e instanceof Refusal) return { ok: false, reason: e.reason, detail: e.detail };
    throw e;
  }
}

const overlaps = (a: Range, b: Range): boolean => a.offset < b.offset + b.size && b.offset < a.offset + a.size;

/**
 * The write plan of a merged ESP image. Without `keep`: the whole image at
 * 0x0, as `esptool.py write_flash 0x0` writes it. With `keep`: every byte of
 * the image except the ranges of its data partitions of the subtypes in
 * KEEP_SUBTYPES that lie inside the image. otadata is written: its bytes in a
 * merged image are blank, and a blank otadata starts ota_0, the slot that
 * holds the written application.
 *
 * "keep" is refused when the image has no valid partition table, when a kept
 * range overlaps the bootloader, the table sector, an application or any
 * other partition, or when it does not start and end on a 4096-byte sector
 * (the flasher stub erases whole sectors, so an unaligned range next to it
 * would be erased).
 */
export function planWrite(image: Uint8Array, bootloaderOffset: number, keep: boolean): PlanVerdict {
  if (!keep) return { ok: true, plan: { ranges: [{ offset: 0, size: image.length }], kept: [], table: null } };
  const read = readImageTable(image, bootloaderOffset);
  if (!read.ok) return read;
  const { table } = read;
  const kept: Kept[] = [];
  for (const p of table.entries) {
    const sub = KEEP_SUBTYPES[p.subtype];
    if (p.type !== TYPE_DATA || sub === undefined || p.offset >= image.length) continue;
    const k: Kept = { name: p.name, subtype: sub, offset: p.offset, size: Math.min(p.size, image.length - p.offset) };
    const label = `${p.name} ${hex(p.offset)}+${hex(p.size)}`;
    const end = k.offset + k.size;
    if (k.offset % SECTOR !== 0 || (end < image.length && end % SECTOR !== 0)) return { ok: false, reason: 'align', detail: label };
    const guarded: Range[] = [
      { offset: 0, size: table.offset + SECTOR },
      ...table.entries.filter((q) => q !== p).map((q) => ({ offset: q.offset, size: q.size })),
    ];
    if (guarded.some((g) => overlaps(k, g))) return { ok: false, reason: 'overlap', detail: label };
    kept.push(k);
  }
  kept.sort((a, b) => a.offset - b.offset);
  const ranges: Range[] = [];
  let at = 0;
  for (const k of kept) {
    if (k.offset > at) ranges.push({ offset: at, size: k.offset - at });
    at = k.offset + k.size;
  }
  if (at < image.length) ranges.push({ offset: at, size: image.length - at });
  return { ok: true, plan: { ranges, kept, table: table.offset } };
}
