// The board check before WRITE: which device the firmware on a connected
// ESP32 board belongs to, from the head of the application the boot loader
// starts. Pure: no device, no page. installed_read.ts reads the flash and
// shows the result.
//
// The head of an ESP-IDF application: the image header (magic 0xE9, chip ID
// u16 at byte 12), esp_app_desc_t at byte 32 (magic 0xABCD5432, version at
// 48, 32 bytes, project name at 80, 32 bytes), and in rcbench's wireless
// devices the role record bleota_role_t right after it at byte 288
// (protocols/bleota/include/bleota.h: magic 0x52626372, kind u8, proto u8).

import { ESP_IMAGE_CHIP_ID } from './boards.ts';
import type { Chip } from './boards.ts';
import { TABLE_MAX, TYPE_APP, TYPE_DATA, hex, readTable } from './partitions.ts';
import type { Partition } from './partitions.ts';

export const APP_DESC_MAGIC = 0xabcd5432;
export const ROLE_OFFSET = 288;
export const ROLE_MAGIC = 0x52626372;
/** Bytes of the head the page reads: header, esp_app_desc_t, role record. */
export const HEAD_LEN = 296;

/** One firmware a board of the page runs, from the repository's sources. */
export interface Known {
  board: string;
  chip: Chip;
  /** The CMake project name, esp_app_desc_t.project_name. */
  project: string;
  /** The BLEOTA_KIND_* of the role record; null for a firmware without one. */
  kind: number | null;
}

/**
 * The firmware of each ESP32 board: firmware/panel (project rcbench-panel),
 * firmware/dial (rcbench-dial, DEVOTA_ROLE(BLEOTA_KIND_DIAL)),
 * firmware/node (rcbench-node, the role per build: power 2 on the C6,
 * balance 3, power on the ES3C28P display board 4), the C6 radio from
 * esp_hosted's slave/ folder (network_adapter, tools/c6_image.py).
 */
export const KNOWN: readonly Known[] = [
  { board: 'panel', chip: 'esp32p4', project: 'rcbench-panel', kind: null },
  { board: 'dial', chip: 'esp32s3', project: 'rcbench-dial', kind: 1 },
  { board: 'node-pwr', chip: 'esp32c6', project: 'rcbench-node', kind: 2 },
  { board: 'node-bal', chip: 'esp32c6', project: 'rcbench-node', kind: 3 },
  { board: 'node-display', chip: 'esp32s3', project: 'rcbench-node', kind: 4 },
  { board: 'c6', chip: 'esp32c6', project: 'network_adapter', kind: null },
];

/** What the head of an application says. */
export interface AppHead {
  chip: Chip | null;
  project: string;
  version: string;
  /** The role record's kind; null without a role record. */
  kind: number | null;
}

const text = (b: Uint8Array): string => {
  const nul = b.indexOf(0);
  return new TextDecoder().decode(nul < 0 ? b : b.subarray(0, nul)).replace(/[^\x20-\x7e]/g, '?');
};

/** The head of an application image, or null when it is no application (erased, another layout). */
export function readHead(b: Uint8Array): AppHead | null {
  if (b.length < 112 || b[0] !== 0xe9) return null;
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (v.getUint32(32, true) !== APP_DESC_MAGIC) return null;
  const id = v.getUint16(12, true);
  let chip: Chip | null = null;
  for (const [c, n] of Object.entries(ESP_IMAGE_CHIP_ID)) if (n === id) chip = c as Chip;
  const kind = b.length >= HEAD_LEN && v.getUint32(ROLE_OFFSET, true) === ROLE_MAGIC ? b[ROLE_OFFSET + 4]! : null;
  return { chip, project: text(b.subarray(80, 112)), version: text(b.subarray(48, 80)), kind };
}

// ---- otadata ----

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});

/** CRC-32 as zlib's crc32(data, value): `value` is the CRC of the bytes before. */
export function crc32(data: Uint8Array, value = 0): number {
  let c = ~value >>> 0;
  for (const x of data) c = CRC_TABLE[(c ^ x) & 0xff]! ^ (c >>> 8);
  return ~c >>> 0;
}

/** ESP_OTA_IMG_INVALID and ESP_OTA_IMG_ABORTED: the boot loader skips such an entry. */
const OTA_DEAD = [3, 4];

/**
 * The sequence number of one esp_ota_select_entry_t (32 bytes: ota_seq u32,
 * seq_label 20 bytes, ota_state u32, crc u32), or null when the boot loader
 * takes it as invalid: ota_seq 0xFFFFFFFF, state invalid or aborted, or the
 * CRC over ota_seq not crc32(ota_seq, 0xFFFFFFFF)
 * (bootloader_common_ota_select_valid of ESP-IDF v6.1).
 */
export function otaSeq(e: Uint8Array): number | null {
  if (e.length < 32) return null;
  const v = new DataView(e.buffer, e.byteOffset, 32);
  const seq = v.getUint32(0, true);
  if (seq === 0xffffffff || OTA_DEAD.includes(v.getUint32(24, true))) return null;
  return v.getUint32(28, true) === crc32(e.subarray(0, 4), 0xffffffff) ? seq : null;
}

/**
 * The OTA slot the boot loader starts: (highest valid sequence - 1) modulo
 * the number of OTA slots; null without a valid entry, where the boot loader
 * starts the factory application, else ota_0.
 */
export function activeSlot(entries: readonly Uint8Array[], otaCount: number): number | null {
  const seqs = entries.map(otaSeq).filter((s): s is number => s !== null);
  if (!seqs.length || otaCount < 1) return null;
  return (Math.max(...seqs) - 1) % otaCount;
}

// ---- The verdict ----

export type Unknown = 'blank' | 'no-app' | 'foreign' | 'ambiguous' | 'unread' | 'no-stub' | 'rp2350';

export type Verdict =
  | { kind: 'match'; head: AppHead }
  | { kind: 'other'; head: AppHead; boards: string[] }
  | { kind: 'unknown'; why: Unknown; head?: AppHead; boards?: string[]; detail?: string };

/** The boards whose firmware an application head can be: the role decides where it is there. */
export function boardsOf(head: AppHead): string[] {
  return KNOWN.filter((k) => k.chip === head.chip && k.project === head.project
    && (head.kind === null || k.kind === null || k.kind === head.kind)).map((k) => k.board);
}

/**
 * Holds the installed application to the chosen board: matches when it is
 * that board's firmware alone; another device when none of its boards is the
 * chosen one; cannot tell for an application of no board of the page, or of
 * several boards one of which is the chosen one (rcbench-node without a role
 * record on an ESP32-C6: a power or a balance node).
 */
export function judge(board: string, head: AppHead | null): Verdict {
  if (!head) return { kind: 'unknown', why: 'no-app' };
  const boards = boardsOf(head);
  if (!boards.length) return { kind: 'unknown', why: 'foreign', head };
  if (!boards.includes(board)) return { kind: 'other', head, boards };
  if (boards.length > 1) return { kind: 'unknown', why: 'ambiguous', head, boards };
  return { kind: 'match', head };
}

// ---- The reads ----

/**
 * The entries of a table read from a board's flash, `bytes` starting at the
 * table; null for a sector without a valid table (wrong magic, no MD5 entry,
 * an MD5 that differs).
 */
export function parseTable(bytes: Uint8Array): Partition[] | null {
  if (bytes[0] !== 0xaa || bytes[1] !== 0x50) return null;
  try {
    return readTable(bytes, 0);
  } catch {
    return null;
  }
}

/** A read of `size` flash bytes at `address`. */
export type Reader = (address: number, size: number) => Promise<Uint8Array>;

/** The partition table of the board: at the chip's offset first, then at the other one. */
async function readTableOf(rd: Reader, chip: Chip): Promise<Partition[] | null> {
  for (const at of chip === 'esp32p4' ? [0xc000, 0x8000] : [0x8000, 0xc000]) {
    const t = parseTable(await rd(at, TABLE_MAX));
    if (t) return t;
  }
  return null;
}

/**
 * The verdict for an ESP32 board: the table, the slot the boot loader
 * starts (otadata), the head of the application in it.
 */
export async function checkEsp(rd: Reader, chip: Chip, boardId: string): Promise<Verdict> {
  const table = await readTableOf(rd, chip);
  if (!table) return { kind: 'unknown', why: 'blank' };
  const apps = table.filter((p) => p.type === TYPE_APP);
  const ota = apps.filter((p) => p.subtype >= 0x10 && p.subtype < 0x20);
  let app = apps.find((p) => p.subtype === 0x00) ?? ota.find((p) => p.subtype === 0x10);
  const otadata = table.find((p) => p.type === TYPE_DATA && p.subtype === 0x00);
  if (otadata && ota.length) {
    const slot = activeSlot([await rd(otadata.offset, 32), await rd(otadata.offset + 0x1000, 32)], ota.length);
    if (slot !== null) app = ota.find((p) => p.subtype === 0x10 + slot) ?? app;
  }
  if (!app) return { kind: 'unknown', why: 'no-app', detail: '—' };
  const v = judge(boardId, readHead(await rd(app.offset, HEAD_LEN)));
  return v.kind === 'unknown' && v.why === 'no-app' ? { ...v, detail: `${app.name} ${hex(app.offset)}` } : v;
}
