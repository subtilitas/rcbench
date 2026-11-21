// ESP32-P4, -S3 and -C6 over Web Serial with esptool-js: connect, detect the
// chip, write the merged image at 0x0, compare the MD5 of the flash with the
// image, reset. esptool-js loads on the first connect, so the page's first
// load stays small.

import { chipFromEspName } from './boards.ts';
import type { Chip } from './boards.ts';
import { md5Hex } from './digest.ts';
import { StepError } from './rp2350.ts';

/** The rate esptool-js asks the flasher stub for. USB-Serial-JTAG ignores it. */
export const ESP_BAUD = 460800;
export const ROM_BAUD = 115200;

export type EspStep = 'connect' | 'write' | 'verify' | 'reset';

export interface EspSession {
  chip: Chip | null;
  /** The chip as esptool-js describes it, with its revision. */
  description: string;
  write(data: Uint8Array, onProgress: (done: number, total: number) => void): Promise<void>;
  verify(data: Uint8Array): Promise<void>;
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
  const loader = new ESPLoader({ transport, baudrate: ESP_BAUD, romBaudrate: ROM_BAUD, terminal });
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
    write: (data, onProgress) => inStep('write', () => loader.writeFlash({
      fileArray: [{ data, address: 0 }],
      flashMode: 'keep',
      flashFreq: 'keep',
      flashSize: 'keep',
      eraseAll: false,
      compress: true,
      reportProgress: (_i, written, total) => onProgress(written, total),
    })),
    verify: (data) => inStep('verify', async () => {
      const image = padTo4(data);
      const want = md5Hex(image);
      const got = (await loader.flashMd5sum(0, image.length)).toLowerCase();
      if (got !== want) throw new Error(`MD5 of the flash ${got}, of the image ${want}`);
    }),
    reset: () => inStep('reset', async () => {
      await loader.after('hard_reset');
      await transport.disconnect();
    }),
    close: () => transport.disconnect().catch(() => undefined),
  };
}
