// PICOBOOT: the vendor-class USB (Universal Serial Bus) interface of the RP2350
// boot ROM in BOOTSEL mode, spoken over WebUSB.
//
// The command values, the 32-byte command layout, the status codes and the
// GET_INFO types follow `boot/picoboot.h` and `boot/picoboot_constants.h` of
// pico-sdk 2.3.0; the order of a transfer (command, data, zero-length
// acknowledgement in the opposite direction) and the recovery after a stall
// follow `picoboot_connection.c` of picotool 2.3.0. Both carry:
//
//   Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
//   SPDX-License-Identifier: BSD-3-Clause
//
// No code is copied; this file is written for this page. The licence text is
// in flasher/public/licenses.txt and NOTICE.

export const PICOBOOT_MAGIC = 0x431fd10b;
export const RPI_VENDOR_ID = 0x2e8a;
export const RP2350_BOOT_PID = 0x000f;
/** USB product ID of a pico-sdk application with USB stdio, RP2350. */
export const PICO_STDIO_PID = 0x0009;

export const CMD = {
  EXCLUSIVE_ACCESS: 0x01,
  FLASH_ERASE: 0x03,
  READ: 0x84,
  WRITE: 0x05,
  EXIT_XIP: 0x06,
  REBOOT2: 0x0a,
  GET_INFO: 0x8b,
} as const;

/** Control requests to the interface. */
export const IF_RESET = 0x41;
export const IF_CMD_STATUS = 0x42;

export const EXCLUSIVE = 1;
export const GET_INFO_SYS = 1;
export const GET_INFO_UF2_TARGET_PARTITION = 3;
export const REBOOT2_TYPE_NORMAL = 0x0;
/** Partition index GET_INFO returns for "no partition: absolute addresses". */
export const NO_PARTITION_INDEX = 0xff;

export const STATUS_NAMES = [
  'OK', 'UNKNOWN_CMD', 'INVALID_CMD_LENGTH', 'INVALID_TRANSFER_LENGTH', 'INVALID_ADDRESS',
  'BAD_ALIGNMENT', 'INTERLEAVED_WRITE', 'REBOOTING', 'UNKNOWN_ERROR', 'INVALID_STATE',
  'NOT_PERMITTED', 'INVALID_ARG', 'BUFFER_TOO_SMALL', 'PRECONDITION_NOT_MET', 'MODIFIED_DATA',
  'INVALID_DATA', 'NOT_FOUND', 'UNSUPPORTED_MODIFICATION',
] as const;

export const FLASH_SECTOR = 4096;
export const FLASH_PAGE = 256;

export interface Command {
  token: number;
  id: number;
  /** Argument bytes, at most 16. */
  args: Uint8Array;
  /** Bytes of the data phase; 0 for none. */
  transferLength: number;
}

/** The 32 bytes of a command, little endian. */
export function encodeCommand(c: Command): Uint8Array {
  if (c.args.length > 16) throw new RangeError('PICOBOOT arguments exceed 16 bytes');
  const out = new Uint8Array(32);
  const v = new DataView(out.buffer);
  v.setUint32(0, PICOBOOT_MAGIC, true);
  v.setUint32(4, c.token >>> 0, true);
  v.setUint8(8, c.id);
  v.setUint8(9, c.args.length);
  v.setUint16(10, 0, true);
  v.setUint32(12, c.transferLength >>> 0, true);
  out.set(c.args, 16);
  return out;
}

const words = (...w: number[]): Uint8Array => {
  const out = new Uint8Array(w.length * 4);
  const v = new DataView(out.buffer);
  w.forEach((x, i) => v.setUint32(i * 4, x >>> 0, true));
  return out;
};

export const argsExclusive = (type: number): Uint8Array => Uint8Array.of(type);
export const argsRange = (addr: number, size: number): Uint8Array => words(addr, size);
export const argsReboot2 = (flags: number, delayMs: number, p0: number, p1: number): Uint8Array =>
  words(flags, delayMs, p0, p1);
/** GET_INFO: bType, bParam, wParam, then 3 parameter words: 16 bytes. */
export function argsGetInfo(type: number, params: readonly number[] = []): Uint8Array {
  const out = new Uint8Array(16);
  const v = new DataView(out.buffer);
  v.setUint8(0, type);
  for (let i = 0; i < 3; i++) v.setUint32(4 + i * 4, (params[i] ?? 0) >>> 0, true);
  return out;
}

export interface CmdStatus {
  token: number;
  code: number;
  cmdId: number;
  inProgress: boolean;
}

/** The 16 bytes IF_CMD_STATUS returns. */
export function decodeStatus(bytes: Uint8Array): CmdStatus {
  if (bytes.length < 16) throw new RangeError(`status of ${bytes.length} bytes, 16 expected`);
  const v = new DataView(bytes.buffer, bytes.byteOffset, 16);
  return { token: v.getUint32(0, true), code: v.getUint32(4, true), cmdId: v.getUint8(8), inProgress: v.getUint8(9) !== 0 };
}

export const statusName = (code: number): string => STATUS_NAMES[code] ?? `status ${code}`;

/** Location of a partition: first and last 4096-byte sector, bits 0-12 and 13-25. */
export function decodeLocation(word: number): { start: number; end: number } {
  const first = word & 0x1fff;
  const last = (word >>> 13) & 0x1fff;
  return { start: first * FLASH_SECTOR, end: (last + 1) * FLASH_SECTOR };
}

export interface TargetPartition {
  /** Partition index, or null for absolute addresses. */
  index: number | null;
  /** Flash offset of the partition, bytes from the flash start. */
  start: number;
  end: number;
}

/**
 * Reads the answer of GET_INFO UF2_TARGET_PARTITION: word count 3, the
 * partition index (negative: the family goes nowhere), the location word.
 */
export function decodeTargetPartition(bytes: Uint8Array): TargetPartition | 'refused' {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
  if (bytes.length < 12 || v.getUint32(0, true) < 2) throw new RangeError('GET_INFO answer too short');
  const index = v.getInt32(4, true);
  if (index < 0) return 'refused';
  const loc = decodeLocation(v.getUint32(8, true));
  return { index: index === NO_PARTITION_INDEX ? null : index, ...loc };
}

/** A PICOBOOT command that failed, with the boot ROM's status. */
export class PicobootError extends Error {
  readonly status: string;
  constructor(what: string, status: string) {
    super(`${what}: ${status}`);
    this.status = status;
  }
}

/** The part of WebUSB's USBDevice this module calls. */
export type UsbDeviceLike = Pick<USBDevice,
  'open' | 'close' | 'opened' | 'configuration' | 'selectConfiguration' | 'claimInterface'
  | 'releaseInterface' | 'transferIn' | 'transferOut' | 'controlTransferIn' | 'controlTransferOut' | 'clearHalt'
  | 'vendorId' | 'productId'>;

/** The vendor interface with one bulk OUT and one bulk IN endpoint. */
export function findInterface(dev: UsbDeviceLike): { iface: number; outEp: number; inEp: number } | null {
  for (const i of dev.configuration?.interfaces ?? []) {
    const alt = i.alternate;
    if (alt.interfaceClass !== 0xff) continue;
    const out = alt.endpoints.find((e) => e.direction === 'out' && e.type === 'bulk');
    const inn = alt.endpoints.find((e) => e.direction === 'in' && e.type === 'bulk');
    if (out && inn) return { iface: i.interfaceNumber, outEp: out.endpointNumber, inEp: inn.endpointNumber };
  }
  return null;
}

/** One claimed PICOBOOT interface. */
export class Picoboot {
  readonly dev: UsbDeviceLike;
  readonly iface: number;
  readonly outEp: number;
  readonly inEp: number;
  private token = 1;

  private constructor(dev: UsbDeviceLike, iface: number, outEp: number, inEp: number) {
    this.dev = dev;
    this.iface = iface;
    this.outEp = outEp;
    this.inEp = inEp;
  }

  /** Opens the device and claims its PICOBOOT interface. */
  static async open(dev: UsbDeviceLike): Promise<Picoboot> {
    if (dev.vendorId !== RPI_VENDOR_ID || dev.productId !== RP2350_BOOT_PID) {
      throw new PicobootError('open', `USB ID ${hex4(dev.vendorId)}:${hex4(dev.productId)} is no RP2350 in BOOTSEL`);
    }
    if (!dev.opened) await dev.open();
    if (!dev.configuration) await dev.selectConfiguration(1);
    const found = findInterface(dev);
    if (!found) throw new PicobootError('open', 'no PICOBOOT interface');
    await dev.claimInterface(found.iface);
    const pb = new Picoboot(dev, found.iface, found.outEp, found.inEp);
    await pb.reset();
    return pb;
  }

  /** IF_RESET: clears a stall and the boot ROM's command state. */
  async reset(): Promise<void> {
    await this.dev.clearHalt('in', this.inEp).catch(() => undefined);
    await this.dev.clearHalt('out', this.outEp).catch(() => undefined);
    await this.dev.controlTransferOut({ requestType: 'vendor', recipient: 'interface', request: IF_RESET, value: 0, index: this.iface });
  }

  async status(): Promise<CmdStatus> {
    const r = await this.dev.controlTransferIn({ requestType: 'vendor', recipient: 'interface', request: IF_CMD_STATUS, value: 0, index: this.iface }, 16);
    if (r.status !== 'ok' || !r.data) throw new PicobootError('status', String(r.status));
    return decodeStatus(new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength));
  }

  private async fail(what: string): Promise<never> {
    let name = 'transfer stalled';
    try { name = statusName((await this.status()).code); } catch { /* the stall is the report */ }
    await this.reset().catch(() => undefined);
    throw new PicobootError(what, name);
  }

  /**
   * Sends one command. An OUT command sends `data`; an IN command (bit 7 of
   * the ID) returns `inLength` bytes. The acknowledgement is a zero-length
   * packet in the direction opposite to the data.
   */
  async command(what: string, id: number, args: Uint8Array, data?: Uint8Array, inLength = 0): Promise<Uint8Array> {
    const isIn = (id & 0x80) !== 0;
    const transferLength = isIn ? inLength : (data?.length ?? 0);
    const cmd = encodeCommand({ token: this.token++, id, args, transferLength });
    const sent = await this.dev.transferOut(this.outEp, cmd as Uint8Array<ArrayBuffer>);
    if (sent.status !== 'ok') return this.fail(what);
    let result: Uint8Array = new Uint8Array(0);
    if (transferLength > 0) {
      if (isIn) {
        const r = await this.dev.transferIn(this.inEp, transferLength);
        if (r.status !== 'ok' || !r.data || r.data.byteLength !== transferLength) return this.fail(what);
        result = new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
      } else {
        const r = await this.dev.transferOut(this.outEp, data as Uint8Array<ArrayBuffer>);
        if (r.status !== 'ok' || r.bytesWritten !== transferLength) return this.fail(what);
      }
    }
    if (isIn) {
      const ack = await this.dev.transferOut(this.outEp, new Uint8Array(0));
      if (ack.status !== 'ok') return this.fail(what);
    } else {
      const ack = await this.dev.transferIn(this.inEp, 64);
      if (ack.status !== 'ok') return this.fail(what);
    }
    return result;
  }

  exclusive(): Promise<Uint8Array> { return this.command('exclusive access', CMD.EXCLUSIVE_ACCESS, argsExclusive(EXCLUSIVE)); }
  exitXip(): Promise<Uint8Array> { return this.command('exit XIP', CMD.EXIT_XIP, new Uint8Array(0)); }
  erase(addr: number, size: number): Promise<Uint8Array> { return this.command('erase', CMD.FLASH_ERASE, argsRange(addr, size)); }
  write(addr: number, data: Uint8Array): Promise<Uint8Array> { return this.command('write', CMD.WRITE, argsRange(addr, data.length), data); }
  read(addr: number, size: number): Promise<Uint8Array> { return this.command('read', CMD.READ, argsRange(addr, size), undefined, size); }

  async targetPartition(family: number): Promise<TargetPartition | 'refused'> {
    const r = await this.command('get info', CMD.GET_INFO, argsGetInfo(GET_INFO_UF2_TARGET_PARTITION, [family]), undefined, 256);
    return decodeTargetPartition(r);
  }

  /**
   * REBOOT2 into the application: type NORMAL, partition parameter -1 (none)
   * and 0, the parameters of `picotool reboot`, after `delayMs`.
   */
  reboot(delayMs = 500): Promise<Uint8Array> {
    return this.command('reboot', CMD.REBOOT2, argsReboot2(REBOOT2_TYPE_NORMAL, delayMs, 0xffffffff, 0));
  }

  async close(): Promise<void> {
    await this.dev.releaseInterface(this.iface).catch(() => undefined);
    await this.dev.close().catch(() => undefined);
  }
}

const hex4 = (n: number): string => `0x${n.toString(16).padStart(4, '0')}`;
