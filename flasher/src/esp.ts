// ESP32-P4, -S3 and -C6 over Web Serial with esptool-js: connect, detect the
// chip, write the ranges of the write plan (src/partitions.ts), compare the
// MD5 of each written flash range with the image, reset. esptool-js loads on
// the first connect, so the page's first load stays small.

import { chipFromEspName } from './boards.ts';
import type { Chip } from './boards.ts';
import { md5Hex } from './digest.ts';
import { StepError } from './rp2350.ts';
import type { ESPLoader } from 'esptool-js';

/** The rate esptool-js asks the flasher stub for behind a USB-to-UART bridge. */
export const ESP_BAUD = 460800;
export const ROM_BAUD = 115200;

/** USB ID of the USB Serial/JTAG built into the ESP32-P4, -S3 and -C6. */
export const USB_SERIAL_JTAG = { usbVendorId: 0x303a, usbProductId: 0x1001 } as const;

/**
 * The rate for the flasher stub on a port. On the built-in USB Serial/JTAG
 * the rate has no effect, and the change makes esptool-js reopen the port,
 * which resets the chip: the page stays at 115200 baud there. A USB-to-UART
 * bridge gets 460800 baud.
 */
export function stubBaud(info: { usbVendorId?: number; usbProductId?: number }): number {
  const jtag = info.usbVendorId === USB_SERIAL_JTAG.usbVendorId && info.usbProductId === USB_SERIAL_JTAG.usbProductId;
  return jtag ? ROM_BAUD : ESP_BAUD;
}

export type EspStep = 'connect' | 'write' | 'verify' | 'reset';

/** Bytes of the image for one address of the flash. */
export interface EspChunk {
  address: number;
  data: Uint8Array;
}

export interface EspSession {
  chip: Chip | null;
  /** The chip as esptool-js describes it, with its revision. */
  description: string;
  /** The rate the flasher stub runs at. */
  baud: number;
  /** Writes each chunk at its address; the progress counts image bytes over all chunks. */
  write(chunks: readonly EspChunk[], onProgress: (done: number, total: number) => void): Promise<void>;
  /** Compares the MD5 of the flash with the MD5 of each chunk, over exactly the written bytes. */
  verify(chunks: readonly EspChunk[]): Promise<void>;
  /** The esptool-js loader, for the board check's flash reads (installed_read.ts). */
  loader: ESPLoader;
  reset(): Promise<void>;
  close(): Promise<void>;
}

/** esptool-js pads an image to a multiple of 4 bytes with 0xFF before it writes it. */
export function padTo4(data: Uint8Array): Uint8Array {
  if (data.length % 4 === 0) return data;
  const out = new Uint8Array(data.length + (4 - (data.length % 4))).fill(0xff);
  out.set(data);
  return out;
}

async function inStep<T>(step: EspStep, run: () => Promise<T>): Promise<T> {
  try { return await run(); } catch (e) { throw e instanceof StepError ? e : new StepError(step, e); }
}

/** Opens the port, syncs with the boot ROM, detects the chip and starts the flasher stub. */
export async function connectEsp(port: SerialPort, log: (line: string) => void): Promise<EspSession> {
  const { ESPLoader, Transport } = await import('esptool-js');
  const transport = new Transport(port, false);
  const terminal = {
    clean: (): void => undefined,
    writeLine: (s: string): void => log(s),
    write: (s: string): void => log(s),
  };
  const baud = stubBaud(port.getInfo());
  if (baud === ROM_BAUD) log(`USB Serial/JTAG (303a:1001): the rate stays at ${ROM_BAUD} baud`);
  const loader = new ESPLoader({ transport, baudrate: baud, romBaudrate: ROM_BAUD, terminal });
  const description = await inStep('connect', async () => {
    try {
      return await loader.main('default_reset');
    } catch (e) {
      await transport.disconnect().catch(() => undefined);
      throw e;
    }
  });
  const chip = chipFromEspName(loader.chip.CHIP_NAME);
  return {
    chip,
    description,
    baud,
    write: (chunks, onProgress) => inStep('write', () => {
      const total = chunks.reduce((n, c) => n + c.data.length, 0);
      const before = chunks.map((_, i) => chunks.slice(0, i).reduce((n, c) => n + c.data.length, 0));
      return loader.writeFlash({
        fileArray: chunks.map((c) => ({ data: c.data, address: c.address })),
        flashMode: 'keep',
        flashFreq: 'keep',
        flashSize: 'keep',
        eraseAll: false,
        compress: true,
        // esptool-js counts compressed bytes of one chunk; the page shows image bytes of all.
        reportProgress: (i, written, size) => onProgress(
          before[i]! + (size > 0 ? Math.round((written / size) * chunks[i]!.data.length) : 0), total),
      });
    }),
    verify: (chunks) => inStep('verify', async () => {
      for (const c of chunks) {
        const bytes = padTo4(c.data);
        const want = md5Hex(bytes);
        const got = (await loader.flashMd5sum(c.address, bytes.length)).toLowerCase();
        if (got !== want) throw new Error(`MD5 of the flash at 0x${c.address.toString(16)} ${got}, of the image ${want}`);
      }
    }),
    loader,
    reset: () => inStep('reset', async () => {
      await loader.after('hard_reset');
      await transport.disconnect();
    }),
    close: () => transport.disconnect().catch(() => undefined),
  };
}
