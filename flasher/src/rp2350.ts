// Writes an image into an RP2350's flash over PICOBOOT: erase, write, read
// back, reboot.

import { FLASH_SECTOR, NO_PARTITION_INDEX, PicobootError } from './picoboot.ts';
import type { Picoboot, TargetPartition } from './picoboot.ts';
import { FAMILY, FLASH_BASE, FLASH_END, familyName, regionBytes } from './uf2.ts';
import type { Region, Uf2Image } from './uf2.ts';

/** Bytes per erase, write and read command: 16 sectors. */
export const CHUNK = 16 * FLASH_SECTOR;

export interface EraseRange {
  /** Bytes from the flash start; 4096-byte aligned. */
  offset: number;
  size: number;
}

/**
 * The image slots A and B of the IOmcu's flash table, a copy of the
 * partitions of `firmware/iomcu/partitions.json` that take an image family.
 * The module, the rbmod carrier and the Core2350B share the table; a unit
 * test holds the copy equal to the file.
 */
export const IOMCU_SLOTS: readonly EraseRange[] = [
  { offset: 0x2000, size: 2036 * 1024 },
  { offset: 2044 * 1024, size: 2036 * 1024 },
];

/**
 * The slots an install image leaves as they are, as the release manifest
 * names them (`tools/flash_manifest.py` `erase_ranges`): every slot that
 * holds none of the written sectors. Empty when the image writes into no
 * slot, so a file of another layout erases nothing.
 */
export function otherSlots(writes: readonly Region[], slots: readonly EraseRange[] = IOMCU_SLOTS): EraseRange[] {
  const hit = (s: EraseRange): boolean => writes.some((w) => {
    const start = w.addr - FLASH_BASE;
    return start < s.offset + s.size && start + w.data.length > s.offset;
  });
  if (!slots.some(hit)) return [];
  return slots.filter((s) => !hit(s)).map((s) => ({ ...s }));
}

/** What the page sends: sector-aligned regions and extra ranges to erase. */
export interface FlashPlan {
  /** 4096-byte aligned regions at addresses in the 0x10000000 window. */
  writes: Region[];
  erases: EraseRange[];
}

/**
 * Pads each region to whole 4096-byte sectors with 0xFF and merges regions
 * that share a sector, so every erase covers bytes the plan writes.
 */
export function sectorAlign(regions: readonly Region[]): Region[] {
  const sectors = new Map<number, Uint8Array>();
  for (const r of regions) {
    for (let i = 0; i < r.data.length;) {
      const addr = r.addr + i;
      const base = addr - (addr % FLASH_SECTOR);
      let sector = sectors.get(base);
      if (!sector) { sector = new Uint8Array(FLASH_SECTOR).fill(0xff); sectors.set(base, sector); }
      const n = Math.min(r.data.length - i, base + FLASH_SECTOR - addr);
      sector.set(r.data.subarray(i, i + n), addr - base);
      i += n;
    }
  }
  const bases = [...sectors.keys()].sort((a, b) => a - b);
  const out: Region[] = [];
  for (const base of bases) {
    const last = out[out.length - 1];
    if (last && last.addr + last.data.length === base) {
      const data = new Uint8Array(last.data.length + FLASH_SECTOR);
      data.set(last.data);
      data.set(sectors.get(base)!, last.data.length);
      last.data = data;
    } else {
      out.push({ addr: base, data: sectors.get(base)! });
    }
  }
  return out;
}

/** Moves regions linked at 0x10000000 to a partition that starts `offset` bytes into the flash. */
export function shiftRegions(regions: readonly Region[], offset: number): Region[] {
  return regions.map((r) => ({ addr: r.addr + offset, data: r.data }));
}

/** A plan that writes a raw binary at the flash start. */
export function planBin(bytes: Uint8Array): FlashPlan {
  if (bytes.length === 0) throw new PicobootError('plan', 'the file is empty');
  if (FLASH_BASE + bytes.length > FLASH_END) throw new PicobootError('plan', 'the file exceeds 16MB');
  return { writes: sectorAlign([{ addr: FLASH_BASE, data: bytes }]), erases: [] };
}

/**
 * A plan for a UF2 image. Blocks of the absolute family go where they say.
 * Blocks of another family go into the partition the boot ROM names for the
 * family, as a drag-and-drop would place them; `target` is that answer.
 */
export function planUf2(img: Uf2Image, target: TargetPartition | 'refused' | null, erases: readonly EraseRange[] = []): FlashPlan {
  let regions = img.regions;
  if (img.family !== FAMILY.ABSOLUTE) {
    if (target === 'refused') throw new PicobootError('partition', `the device takes no ${familyName(img.family)} image`);
    if (target && target.index !== null && target.index !== NO_PARTITION_INDEX) {
      regions = shiftRegions(regions, target.start);
      const end = FLASH_BASE + target.end;
      for (const r of regions) {
        if (r.addr + r.data.length > end) throw new PicobootError('partition', `the image exceeds partition ${target.index}`);
      }
    }
  }
  for (const e of erases) {
    if (e.offset % FLASH_SECTOR !== 0 || e.size % FLASH_SECTOR !== 0 || e.size <= 0) {
      throw new PicobootError('plan', `erase range 0x${e.offset.toString(16)}+0x${e.size.toString(16)} is not sector aligned`);
    }
  }
  return { writes: sectorAlign(regions), erases: [...erases] };
}

export type Rp2350Step = 'exclusive' | 'partition' | 'erase' | 'write' | 'verify' | 'reboot';

export interface Progress {
  step: Rp2350Step;
  done: number;
  total: number;
}

/** A failure with the step it happened in. */
export class StepError extends Error {
  readonly step: string;
  readonly cause: unknown;
  constructor(step: string, cause: unknown) {
    super(`${step}: ${cause instanceof Error ? cause.message : String(cause)}`);
    this.step = step;
    this.cause = cause;
  }
}

async function inStep<T>(step: string, run: () => Promise<T>): Promise<T> {
  try { return await run(); } catch (e) { throw e instanceof StepError ? e : new StepError(step, e); }
}

function* chunks(addr: number, size: number): Generator<[number, number]> {
  for (let off = 0; off < size; off += CHUNK) yield [addr + off, Math.min(CHUNK, size - off)];
}

const firstDiff = (a: Uint8Array, b: Uint8Array): number => {
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return i;
  return -1;
};

/**
 * Runs a plan: exclusive access, exit XIP, erase the extra ranges, erase and
 * write each region, read every region and every extra range back, reboot.
 * The progress callback counts bytes per step.
 */
export async function runPlan(pb: Picoboot, plan: FlashPlan, onProgress: (p: Progress) => void): Promise<void> {
  await inStep('exclusive', async () => {
    await pb.exclusive();
    await pb.exitXip();
  });
  const writeBytes = regionBytes(plan.writes);
  const eraseBytes = plan.erases.reduce((n, e) => n + e.size, 0);
  await inStep('erase', async () => {
    let done = 0;
    onProgress({ step: 'erase', done, total: eraseBytes + writeBytes });
    for (const e of plan.erases) {
      for (const [a, n] of chunks(FLASH_BASE + e.offset, e.size)) {
        await pb.erase(a, n);
        done += n;
        onProgress({ step: 'erase', done, total: eraseBytes + writeBytes });
      }
    }
    for (const r of plan.writes) {
      for (const [a, n] of chunks(r.addr, r.data.length)) {
        await pb.erase(a, n);
        done += n;
        onProgress({ step: 'erase', done, total: eraseBytes + writeBytes });
      }
    }
  });
  await inStep('write', async () => {
    let done = 0;
    onProgress({ step: 'write', done, total: writeBytes });
    for (const r of plan.writes) {
      for (const [a, n] of chunks(r.addr, r.data.length)) {
        await pb.write(a, r.data.subarray(a - r.addr, a - r.addr + n));
        done += n;
        onProgress({ step: 'write', done, total: writeBytes });
      }
    }
  });
  await inStep('verify', async () => {
    let done = 0;
    const total = writeBytes + eraseBytes;
    onProgress({ step: 'verify', done, total });
    for (const r of plan.writes) {
      for (const [a, n] of chunks(r.addr, r.data.length)) {
        const got = await pb.read(a, n);
        const want = r.data.subarray(a - r.addr, a - r.addr + n);
        const at = firstDiff(want, got);
        if (at >= 0) throw new PicobootError('verify', `flash differs from the image at 0x${(a + at).toString(16)}`);
        done += n;
        onProgress({ step: 'verify', done, total });
      }
    }
    for (const e of plan.erases) {
      for (const [a, n] of chunks(FLASH_BASE + e.offset, e.size)) {
        const got = await pb.read(a, n);
        const at = got.findIndex((b) => b !== 0xff);
        if (at >= 0) throw new PicobootError('verify', `erased flash is not blank at 0x${(a + at).toString(16)}`);
        done += n;
        onProgress({ step: 'verify', done, total });
      }
    }
  });
  await inStep('reboot', async () => {
    onProgress({ step: 'reboot', done: 0, total: 1 });
    await pb.reboot();
    onProgress({ step: 'reboot', done: 1, total: 1 });
  });
}
