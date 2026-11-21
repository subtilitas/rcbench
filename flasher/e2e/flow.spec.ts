// The whole flow against fake devices: choose a board, the image of a
// release, connect, WRITE, until the page reports the end. Each board type
// once: the panel (ESP32-P4), the Dial (ESP32-S3), the power node (ESP32-C6)
// and the IOmcu (RP2350 over PICOBOOT, put into BOOTSEL at 1200 baud).

import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { makeEspImage, makeUf2 } from '../test/fixtures.ts';
import { FAMILY } from '../src/uf2.ts';

const FAKES = fileURLToPath(new URL('./fake-devices.js', import.meta.url));
const sha = (b: Uint8Array): string => createHash('sha256').update(b).digest('hex');

const TAG = 'v0.23.15';
const OLD = 'v0.22.1';
const IMAGES = {
  panel: makeEspImage(18, { headerOffset: 0x2000, size: 0x2000 + 24_000 }),
  dial: makeEspImage(9, { size: 20_002 }),
  'node-pwr': makeEspImage(13, { size: 16_384 }),
  iomcu: makeUf2(0x10002000, 40, FAMILY.ABSOLUTE, true),
};
const FILES: Record<string, string> = {
  panel: `rcbench-panel-p4-merged-${TAG}.bin`,
  dial: 'rcbench-dial-merged-0.23.15.bin',
  'node-pwr': 'rcbench-node-pwr-merged-0.23.15.bin',
  iomcu: `rcbench-iomcu-install-${TAG}.uf2`,
};
const ERASE = { offset: 0x40000, size: 0x20000 };
const CHIPS: Record<string, string> = { panel: 'esp32p4', dial: 'esp32s3', 'node-pwr': 'esp32c6', iomcu: 'rp2350' };

function manifest(tag: string, boards: (keyof typeof IMAGES)[]): object {
  return {
    format: 1, tag, kind: tag === OLD ? 'stable' : 'testing', published: '2026-11-18T21:49:31Z',
    images: boards.map((b) => ({
      board: b, chip: CHIPS[b], file: FILES[b], size: IMAGES[b].length, sha256: sha(IMAGES[b]), offset: 0,
      format: b === 'iomcu' ? 'uf2' : 'bin', ...(b === 'iomcu' ? { erase: [ERASE] } : {}),
    })),
  };
}

async function setup(page: Page, config: object, opts: { badSha?: boolean } = {}): Promise<void> {
  await page.addInitScript((c) => { (window as unknown as { __fakeConfig: object }).__fakeConfig = c; }, config);
  await page.addInitScript({ path: FAKES });
  await page.exposeFunction('__fakeMd5', (bytes: number[]) => createHash('md5').update(Uint8Array.from(bytes)).digest('hex'));
  const files = new Map<string, Uint8Array | object>([
    ['index.json', { format: 1, keep: 6, releases: [
      { tag: OLD, kind: 'stable', published: '2026-10-22T18:28:51Z' },
      { tag: TAG, kind: 'testing', published: '2026-11-18T21:49:31Z' }] }],
    [`${TAG}/manifest.json`, manifest(TAG, ['panel', 'iomcu', 'dial', 'node-pwr'])],
    [`${OLD}/manifest.json`, manifest(OLD, ['iomcu'])],
  ]);
  for (const [b, f] of Object.entries(FILES)) {
    const bytes = IMAGES[b as keyof typeof IMAGES];
    files.set(`${TAG}/${f}`, opts.badSha && b === 'dial' ? bytes.map((x) => x ^ 1) : bytes);
    files.set(`${OLD}/${f}`, bytes);
  }
  await page.route('**/rcbench/flash/fw/**', (route) => {
    const path = new URL(route.request().url()).pathname.replace('/rcbench/flash/fw/', '');
    const body = files.get(path);
    if (!body) return route.fulfill({ status: 404, body: 'not found' });
    if (body instanceof Uint8Array) return route.fulfill({ status: 200, contentType: 'application/octet-stream', body: Buffer.from(body) });
    return route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
  });
  await page.goto('./');
}

const isDe = (page: Page): boolean => test.info().project.name === 'phone' && !!page;
const label = (page: Page, en: string, de: string): string => (isDe(page) ? de : en);

async function chooseBoard(page: Page, id: string): Promise<void> {
  await page.locator(`input[name="board"][value="${id}"]`).check();
}

async function expectImage(page: Page, file: string, bytes: Uint8Array): Promise<void> {
  const img = page.locator('#image');
  await expect(img).toContainText(file);
  await expect(img).toContainText(sha(bytes));
  await expect(img).toContainText(TAG);
}

async function writeAndWait(page: Page): Promise<void> {
  const write = page.locator('#write');
  await expect(write).toBeEnabled();
  await write.click();
  await expect(page.locator('#result.ok')).toBeVisible({ timeout: 30_000 });
}

for (const [board, chipId] of [['panel', 18], ['dial', 9], ['node-pwr', 13]] as const) {
  test(`${board}: release image, connect, WRITE, verify, done`, async ({ page }) => {
    await setup(page, { esp: { chipId } });
    if (board !== 'panel') await chooseBoard(page, board);
    // The panel is the first board and needs no choice.
    await expect(page.locator(`input[name="board"][value="${board}"]`)).toBeChecked();
    await expect(page.locator('#tag')).toHaveValue(TAG);
    await expectImage(page, FILES[board]!, IMAGES[board]);
    await expect(page.locator('#write')).toBeDisabled();
    await page.locator('#connect-esp').click();
    await expect(page.locator('#conn-state')).toBeVisible({ timeout: 20_000 });
    await writeAndWait(page);
    await expect(page.locator('#result')).toContainText(FILES[board]!);
    const img = IMAGES[board];
    const hash = await page.evaluate(([n]) => (window as unknown as { __fake: { flashHash: (k: string, s: number, l: number) => Promise<string> } }).__fake.flashHash('esp', 0, n as number), [img.length]);
    expect(hash).toBe(sha(img));
  });
}

test('IOmcu: 1200 baud into BOOTSEL, PICOBOOT erase, write, read back, reboot', async ({ page }) => {
  await setup(page, { iomcu: true });
  await chooseBoard(page, 'iomcu');
  await page.locator('#tag').selectOption(TAG);
  await expectImage(page, FILES.iomcu!, IMAGES.iomcu);
  await expect(page.locator('#image')).toContainText('0x40000+0x20000');
  await page.locator('#connect-pico').click();
  await expect(page.locator('#conn-note.bad')).toBeVisible();
  await page.locator('#bootsel').click();
  await expect(page.locator('#bootsel-note.ok')).toBeVisible();
  await page.locator('#connect-pico').click();
  await expect(page.locator('#conn-state')).toContainText('PICOBOOT');
  await writeAndWait(page);
  const fake = await page.evaluate(async () => {
    const f = (window as unknown as { __fake: { rpCmds: number[]; touched: number; rpFlash: Uint8Array; flashHash: (k: string, s: number, l: number) => Promise<string> } }).__fake;
    return {
      cmds: [...new Set(f.rpCmds)], touched: f.touched,
      image: await f.flashHash('rp', 0x2000, 40 * 256),
      erased: f.rpFlash.subarray(0x40000, 0x60000).every((b) => b === 0xff),
      before: f.rpFlash[0x1fff], after: f.rpFlash[0x60000],
    };
  });
  const uf2Payload = new Uint8Array(40 * 256);
  for (let i = 0; i < 40; i++) uf2Payload.set(IMAGES.iomcu.subarray((i + 1) * 512 + 32, (i + 1) * 512 + 288), i * 256);
  expect(fake.image).toBe(sha(uf2Payload));
  expect(fake.erased).toBe(true);
  expect(fake.before).toBe(0x5a);
  expect(fake.after).toBe(0x5a);
  expect(fake.touched).toBe(1);
  expect(fake.cmds).toEqual([0x01, 0x06, 0x03, 0x05, 0x84, 0x0a]);
});

test('a chip that does not match the image is refused before anything is written', async ({ page }) => {
  await setup(page, { esp: { chipId: 13 } });
  await chooseBoard(page, 'dial');
  await expectImage(page, FILES.dial!, IMAGES.dial);
  await page.locator('#connect-esp').click();
  await expect(page.locator('#conn-note.bad')).toContainText('ESP32-S3');
  await expect(page.locator('#conn-note.bad')).toContainText('ESP32-C6');
  await expect(page.locator('#write')).toBeDisabled();
  const ops = await page.evaluate(() => (window as unknown as { __fake: { espOps: number[] } }).__fake.espOps);
  expect(ops).not.toContain(0x10);
  expect(ops).not.toContain(0x11);
});

test('a download that differs from the manifest is not offered for writing', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } }, { badSha: true });
  await chooseBoard(page, 'dial');
  await expect(page.locator('#image-note.bad')).toContainText('SHA-256');
  await expect(page.locator('#image')).toHaveCount(0);
  await expect(page.locator('#write')).toBeDisabled();
});

test('the release list offers per board the releases with an image for it, the stable one first', async ({ page }) => {
  await setup(page, { iomcu: true });
  await expect(page.locator('#tag option')).toHaveCount(1);
  await chooseBoard(page, 'iomcu');
  await expect(page.locator('#tag option')).toHaveCount(2);
  await expect(page.locator('#tag')).toHaveValue(OLD);
  await expect(page.locator('#tag option').first()).toContainText(label(page, 'testing', 'Testing'));
});

test('the planned Core2350B is listed, greyed out and not selectable', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  const core = page.locator('input[name="board"][value="core2350b"]');
  await expect(core).toBeDisabled();
  await expect(page.locator('label.opt.off', { has: core })).toContainText(label(page, 'not in a release yet', 'noch in keinem Release'));
});

test('a local file of the wrong chip is refused, one of the right chip is shown with its SHA-256', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await chooseBoard(page, 'dial');
  await page.locator('input[name="src"][value="file"]').check();
  await page.locator('#file').setInputFiles({ name: 'c6.bin', mimeType: 'application/octet-stream', buffer: Buffer.from(IMAGES['node-pwr']) });
  await expect(page.locator('#image-note.bad')).toContainText('ESP32-C6');
  const app = makeEspImage(9, { app: true });
  await page.locator('#file').setInputFiles({ name: 'app.bin', mimeType: 'application/octet-stream', buffer: Buffer.from(app) });
  await expect(page.locator('#image-note.bad')).toContainText('merged');
  await page.locator('#file').setInputFiles({ name: 'dial.bin', mimeType: 'application/octet-stream', buffer: Buffer.from(IMAGES.dial) });
  await expect(page.locator('#image')).toContainText(sha(IMAGES.dial));
  await expect(page.locator('#image')).toContainText('dial.bin');
});

test('a browser without Web Serial and WebUSB gets the notice and no flow', async ({ page }) => {
  await setup(page, { none: true });
  await expect(page.locator('#notice')).toBeVisible();
  await expect(page.locator('#notice')).toContainText('Chrome');
  await expect(page.locator('#notice')).toContainText('Edge');
  await expect(page.locator('#flow')).toBeHidden();
});

test('the language follows the browser and the buttons switch it; the footer links stay on this site', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await expect(page.locator('html')).toHaveAttribute('lang', isDe(page) ? 'de' : 'en');
  await page.locator('.lang button[data-lang="de"]').click();
  await expect(page.locator('#write')).toHaveText('SCHREIBEN');
  await page.locator('.lang button[data-lang="en"]').click();
  await expect(page.locator('#write')).toHaveText('WRITE');
  await expect(page.locator('#liclink')).toHaveAttribute('href', 'licenses.txt');
  await expect(page.locator('#cfglink')).toHaveAttribute('href', '../');
  const lic = await page.request.get('licenses.txt');
  expect(await lic.text()).toContain('esptool-js 0.7.1');
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
});

test('a release the visitor picks stays when the next board has an image in it', async ({ page }) => {
  await setup(page, { iomcu: true });
  await chooseBoard(page, 'iomcu');
  await page.locator('#tag').selectOption(TAG);
  await expect(page.locator('#image')).toContainText(TAG);
  await chooseBoard(page, 'iomcu-rbmod');
  await expect(page.locator('#image-note')).toBeVisible();
  await chooseBoard(page, 'iomcu');
  await expect(page.locator('#tag')).toHaveValue(TAG);
});
