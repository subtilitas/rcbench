// Shared set-up of the browser tests: synthetic release images, the fw/
// folder served from memory, the fake devices, a test key in place of the
// release key, and the walk through the wizard's first steps.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import { createHash, generateKeyPairSync, sign } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { NODE_PARTS, PANEL_TABLE_V0_23_15, makeMerged, makeTable, makeUf2 } from '../test/fixtures.ts';
import { FAMILY } from '../src/uf2.ts';
import { RELEASE_KEY_PEM } from '../src/release_key.ts';

const FAKES = fileURLToPath(new URL('./fake-devices.js', import.meta.url));
export const sha = (b: Uint8Array): string => createHash('sha256').update(b).digest('hex');

export const TAG = 'v0.23.15';
export const OLD = 'v0.22.1';

export type ImgBoard = 'panel' | 'dial' | 'node-pwr' | 'c6' | 'iomcu';

export const IMAGES: Record<ImgBoard, Uint8Array> = {
  // The panel's table of v0.23.15 at 0xC000: nvs 0xD000+0x6000.
  panel: makeMerged({ chipId: 18, bootloaderOffset: 0x2000, tableOffset: 0xc000, table: PANEL_TABLE_V0_23_15, size: 0x20000 + 24_002 }),
  // nvs 0x9000+0x6000.
  dial: makeMerged({ chipId: 9, bootloaderOffset: 0, tableOffset: 0x8000, table: makeTable(NODE_PARTS), size: 0x20000 + 20_002 }),
  'node-pwr': makeMerged({ chipId: 13, bootloaderOffset: 0, tableOffset: 0x8000, table: makeTable(NODE_PARTS), size: 0x20000 + 16_384 }),
  c6: makeMerged({ chipId: 13, bootloaderOffset: 0, tableOffset: 0x8000, table: makeTable(NODE_PARTS), size: 0x20000 + 12_288 }),
  iomcu: makeUf2(0x10002000, 40, FAMILY.ABSOLUTE, true),
};
export const FILES: Record<ImgBoard, string> = {
  panel: `rcbench-panel-p4-merged-${TAG}.bin`,
  dial: 'rcbench-dial-merged-0.23.15.bin',
  'node-pwr': 'rcbench-node-pwr-merged-0.23.15.bin',
  c6: 'rcbench-c6-merged-2.13.0.bin',
  iomcu: `rcbench-iomcu-install-${TAG}.uf2`,
};
const CHIPS: Record<ImgBoard, string> = { panel: 'esp32p4', dial: 'esp32s3', 'node-pwr': 'esp32c6', c6: 'esp32c6', iomcu: 'rp2350' };
export const ERASE = { offset: 0x40000, size: 0x20000 };

// The page checks signatures against the release key it embeds. The tests
// put a TEST key in its place in the served script and sign with that; the
// release key's private half is never used.
const TEST_KEY = generateKeyPairSync('ec', { namedCurve: 'P-256' });
export const sigOf = (b: Uint8Array): Uint8Array => new Uint8Array(sign('sha256', b, { key: TEST_KEY.privateKey, dsaEncoding: 'der' }));
const pemLines = (pem: string): string[] => pem.split('\n').filter((l) => l && !l.startsWith('-----'));
const RELEASE_LINES = pemLines(RELEASE_KEY_PEM);
const TEST_LINES = pemLines(TEST_KEY.publicKey.export({ type: 'spki', format: 'pem' }).toString());

function manifest(tag: string, images: Partial<Record<ImgBoard, Uint8Array>>): object {
  return {
    format: 1, tag, kind: tag === OLD ? 'stable' : 'testing', published: tag === OLD ? '2026-10-22T18:28:51Z' : '2026-11-18T21:49:31Z',
    images: (Object.entries(images) as [ImgBoard, Uint8Array][]).map(([b, bytes]) => ({
      board: b, chip: CHIPS[b], file: FILES[b], size: bytes.length, sha256: sha(bytes), offset: 0,
      format: b === 'iomcu' ? 'uf2' : 'bin', ...(b === 'iomcu' ? { erase: [ERASE] } : {}),
    })),
  };
}

export interface SetupOpts {
  /** The served download of a board's image differs from its manifest entry by one bit in every byte. */
  badSha?: ImgBoard;
  /** Release images in place of the ones of IMAGES, manifest and file both. */
  images?: Partial<Record<ImgBoard, Uint8Array>>;
  /** Signatures beside the images: fw/<path> to the bytes of the .sig. */
  sigs?: Record<string, Uint8Array>;
  /** Paths under fw/ that answer 404. */
  missing?: string[];
  /** The hash the page opens with, without '#'. */
  hash?: string;
  /** The release list answers after this many milliseconds. */
  indexDelayMs?: number;
}

/** The fw/ folder as the test serves it; a test may change it after the set-up. */
export type Files = Map<string, Uint8Array | object>;

export async function setup(page: Page, config: object, opts: SetupOpts = {}): Promise<Files> {
  await page.addInitScript((c) => { (window as unknown as { __fakeConfig: object }).__fakeConfig = c; }, config);
  await page.addInitScript({ path: FAKES });
  await page.exposeFunction('__fakeMd5', (bytes: number[]) => createHash('md5').update(Uint8Array.from(bytes)).digest('hex'));
  const images = { ...IMAGES, ...opts.images };
  const files: Files = new Map<string, Uint8Array | object>([
    ['index.json', { format: 1, keep: 6, releases: [
      { tag: OLD, kind: 'stable', published: '2026-10-22T18:28:51Z' },
      { tag: TAG, kind: 'testing', published: '2026-11-18T21:49:31Z' }] }],
    [`${TAG}/manifest.json`, manifest(TAG, images)],
    [`${OLD}/manifest.json`, manifest(OLD, { iomcu: images.iomcu })],
  ]);
  for (const [b, f] of Object.entries(FILES) as [ImgBoard, string][]) {
    const bytes = images[b];
    files.set(`${TAG}/${f}`, opts.badSha === b ? bytes.map((x) => x ^ 1) : bytes);
    files.set(`${OLD}/${f}`, bytes);
  }
  for (const [path, sig] of Object.entries(opts.sigs ?? {})) files.set(path, sig);
  for (const path of opts.missing ?? []) files.delete(path);
  await page.route('**/rcbench/flash/assets/*.js', async (route) => {
    const r = await route.fetch();
    let body = await r.text();
    if (body.includes(RELEASE_LINES[0]!)) {
      expect(RELEASE_LINES.every((l) => body.includes(l))).toBe(true);
      RELEASE_LINES.forEach((l, i) => { body = body.replace(l, TEST_LINES[i]!); });
    }
    return route.fulfill({ response: r, body });
  });
  await page.route('**/rcbench/flash/fw/**', async (route) => {
    const path = new URL(route.request().url()).pathname.replace('/rcbench/flash/fw/', '');
    const body = files.get(path);
    if (path === 'index.json' && opts.indexDelayMs) await new Promise((res) => setTimeout(res, opts.indexDelayMs));
    if (!body) return route.fulfill({ status: 404, body: 'not found' });
    if (body instanceof Uint8Array) return route.fulfill({ status: 200, contentType: 'application/octet-stream', body: Buffer.from(body) });
    return route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
  });
  await page.goto(opts.hash ? `./#${opts.hash}` : './');
  return files;
}

/** The phone project runs in German, the desktop project in English. */
export const isDe = (page: Page): boolean => test.info().project.name === 'phone' && !!page;
export const label = (page: Page, en: string, de: string): string => (isDe(page) ? de : en);

/** The step the page shows, from the section's data attribute. */
export const expectStep = (page: Page, step: string): Promise<void> => expect(page.locator('#step')).toHaveAttribute('data-step', step);

/** The Advanced switch on Task: on, and the Details log open. */
export async function advancedOn(page: Page): Promise<void> {
  await page.locator('#adv').check();
  await expect(page.locator('[data-task="file"]')).toBeVisible();
  await expect(page).toHaveURL(/adv=1/);
}

/** Picks a local file on Check: the image or its .sig file. */
export const pickFile = (page: Page, id: '#file' | '#sigfile', name: string, bytes: Uint8Array): Promise<void> =>
  page.locator(id).setInputFiles({ name, mimeType: 'application/octet-stream', buffer: Buffer.from(bytes) });

/** Taps a task and, for a task of several boards, a board; NEXT on Prepare. Ends on Connect. */
export async function walk(page: Page, task: string, board?: string): Promise<void> {
  await page.locator(`[data-task="${task}"]`).click();
  if (board) {
    await expectStep(page, 'board');
    await page.locator(`[data-board="${board}"]`).click();
  }
  if (task === 'c6') {
    await expectStep(page, 'prepare');
    await page.locator('#next').click();
  }
  await expectStep(page, 'connect');
}

/** CONNECT on an ESP board: the page moves to Check on its own. */
export async function connectEsp(page: Page): Promise<void> {
  await page.locator('#connect-esp').click();
  await expectStep(page, 'check');
}

/** The image rows of Check: file, SHA-256, release. */
export async function expectImage(page: Page, file: string, bytes: Uint8Array, tag = TAG): Promise<void> {
  const img = page.locator('#image');
  await expect(img).toContainText(file);
  await expect(img).toContainText(sha(bytes));
  await expect(img).toContainText(tag);
}

/** WRITE on Check, then Done. */
export async function writeAndWait(page: Page): Promise<void> {
  const write = page.locator('#write');
  await expect(write).toBeEnabled();
  await write.click();
  await expectStep(page, 'done');
  await expect(page.locator('#result.ok')).toBeVisible();
}

export type Fake = {
  espFlash: Uint8Array; espOps: number[]; espWrites: [number, number][]; espMd5: [number, number][]; espBauds: number[];
  rpCmds: number[]; touched: number; rpFlash: Uint8Array;
  flashHash: (k: string, s: number, l: number) => Promise<string>;
};

/** The SHA-256 of a range of a fake device's flash. */
export const flashHash = (page: Page, kind: 'esp' | 'rp', start: number, length: number): Promise<string> =>
  page.evaluate(([k, s, n]) => (window as unknown as { __fake: Fake }).__fake.flashHash(k as string, s as number, n as number), [kind, start, length] as const);

/** The ESP commands the fake received. */
export const espOps = (page: Page): Promise<number[]> => page.evaluate(() => (window as unknown as { __fake: Fake }).__fake.espOps);

export const pad4 = (n: number): number => Math.ceil(n / 4) * 4;
