// The boards the page flashes, their chips, and the checks that hold an image
// to a board and a detected chip to an image.

import { isUf2, parseUf2, RP2350_FAMILIES, familyName, startsWithUf2Magic } from './uf2.ts';

export type Chip = 'esp32p4' | 'esp32s3' | 'esp32c6' | 'rp2350';
export type Link = 'serial' | 'picoboot';

export interface Board {
  id: string;
  chip: Chip;
  group: 'panel' | 'iomcu' | 'wireless';
  /** True for a board no release carries an image for. */
  planned?: boolean;
}

/** Board ids as the manifest builder (tools/flash_manifest.py) writes them. */
export const BOARDS: readonly Board[] = [
  { id: 'panel', chip: 'esp32p4', group: 'panel' },
  { id: 'iomcu', chip: 'rp2350', group: 'iomcu' },
  { id: 'iomcu-rbmod', chip: 'rp2350', group: 'iomcu' },
  { id: 'core2350b', chip: 'rp2350', group: 'iomcu', planned: true },
  { id: 'node-display', chip: 'esp32s3', group: 'wireless' },
  { id: 'dial', chip: 'esp32s3', group: 'wireless' },
  { id: 'dial-k518', chip: 'esp32s3', group: 'wireless' },
  { id: 'node-pwr', chip: 'esp32c6', group: 'wireless' },
  { id: 'node-bal', chip: 'esp32c6', group: 'wireless' },
  { id: 'c6', chip: 'esp32c6', group: 'wireless' },
];

export const CHIPS: readonly Chip[] = ['esp32p4', 'esp32s3', 'esp32c6', 'rp2350'];

export const linkOf = (chip: Chip): Link => (chip === 'rp2350' ? 'picoboot' : 'serial');

export const boardById = (id: string): Board | undefined => BOARDS.find((b) => b.id === id);

/** The name esptool-js gives a chip (ROM.CHIP_NAME), for each chip of the page. */
export const ESP_CHIP_NAMES: Readonly<Record<Exclude<Chip, 'rp2350'>, string>> = {
  esp32p4: 'ESP32-P4',
  esp32s3: 'ESP32-S3',
  esp32c6: 'ESP32-C6',
};

/** The page's chip for an esptool-js chip name, or null for any other chip. */
export function chipFromEspName(name: string): Chip | null {
  for (const [chip, n] of Object.entries(ESP_CHIP_NAMES)) if (name === n) return chip as Chip;
  return null;
}

export const chipLabel = (chip: Chip): string => (chip === 'rp2350' ? 'RP2350' : ESP_CHIP_NAMES[chip]);

/** Chip ID of the ESP-IDF image header (byte 12, 16 bits) for each chip. */
export const ESP_IMAGE_CHIP_ID: Readonly<Record<Exclude<Chip, 'rp2350'>, number>> = {
  esp32s3: 9,
  esp32c6: 13,
  esp32p4: 18,
};

/** Offset of the second-stage bootloader in a merged image. */
export const BOOTLOADER_OFFSET: Readonly<Record<Exclude<Chip, 'rp2350'>, number>> = {
  esp32s3: 0x0,
  esp32c6: 0x0,
  esp32p4: 0x2000,
};

const APP_DESC_MAGIC = 0xabcd5432;

export interface EspImageInfo {
  chip: Chip | null;
  chipId: number;
  /** merged: the bootloader at its offset; app: an application image alone. */
  kind: 'merged' | 'app';
  headerOffset: number;
}

/**
 * Reads the ESP-IDF image header at offset 0x0 or 0x2000 of an image: the
 * magic byte 0xE9, the chip ID at byte 12. An application image carries its
 * description (magic word 0xABCD5432) right after the first segment header,
 * at byte 32; the bootloader of a merged image does not.
 */
export function espImageInfo(bytes: Uint8Array): EspImageInfo | null {
  for (const off of [0x0, 0x2000]) {
    if (bytes.length < off + 64 || bytes[off] !== 0xe9) continue;
    const v = new DataView(bytes.buffer, bytes.byteOffset + off, 64);
    const chipId = v.getUint16(12, true);
    const kind = v.getUint32(32, true) === APP_DESC_MAGIC ? 'app' : 'merged';
    let chip: Chip | null = null;
    for (const [c, id] of Object.entries(ESP_IMAGE_CHIP_ID)) if (id === chipId) chip = c as Chip;
    return { chip, chipId, kind, headerOffset: off };
  }
  return null;
}

export type ImageVerdict =
  | { ok: true; format: 'bin' | 'uf2' }
  | { ok: false; reason: 'not-esp' | 'app-only' | 'chip' | 'offset' | 'not-rp2350' | 'family' | 'uf2' | 'uf2-as-bin' | 'ext'; detail: string };

/**
 * Holds an image to the chip of a board. An ESP image must be a merged image
 * of that chip, its bootloader at the chip's offset, written at 0x0. An RP2350
 * image is a file named .uf2 that parses as UF2 of an RP2350 family, or a file
 * named .bin for the flash start that does not start with the UF2 magic word.
 */
export function checkImage(chip: Chip, bytes: Uint8Array, fileName: string): ImageVerdict {
  if (chip === 'rp2350') {
    const name = fileName.toLowerCase();
    if (name.endsWith('.uf2')) {
      // A .uf2 file always goes through the parser: a damaged or short one is
      // refused, never written as raw bytes.
      try {
        const img = parseUf2(bytes);
        if (!RP2350_FAMILIES.includes(img.family)) return { ok: false, reason: 'family', detail: familyName(img.family) };
      } catch (e) {
        return { ok: false, reason: 'uf2', detail: e instanceof Error ? e.message : String(e) };
      }
      return { ok: true, format: 'uf2' };
    }
    if (!name.endsWith('.bin')) return { ok: false, reason: 'ext', detail: fileName };
    // A .bin that starts with the first UF2 magic word is a UF2 container
    // under the wrong name: refused, so its block headers never reach the flash.
    if (isUf2(bytes) || startsWithUf2Magic(bytes)) return { ok: false, reason: 'uf2-as-bin', detail: fileName };
    if (espImageInfo(bytes)) return { ok: false, reason: 'not-rp2350', detail: fileName };
    return { ok: true, format: 'bin' };
  }
  const info = espImageInfo(bytes);
  if (!info) return { ok: false, reason: 'not-esp', detail: fileName };
  if (info.chip !== chip) return { ok: false, reason: 'chip', detail: info.chip ? chipLabel(info.chip) : `chip ID ${info.chipId}` };
  if (info.kind === 'app') return { ok: false, reason: 'app-only', detail: fileName };
  if (info.headerOffset !== BOOTLOADER_OFFSET[chip]) return { ok: false, reason: 'offset', detail: `0x${info.headerOffset.toString(16)}` };
  return { ok: true, format: 'bin' };
}

/** True when the chip the page detected is the chip the image is for. */
export const chipMatches = (expected: Chip, detected: Chip | null): boolean => detected === expected;
