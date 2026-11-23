// The whole flow against fake devices: choose a board, the image of a
// release, connect, WRITE, until the page reports the end. Each board type
// once: the panel (ESP32-P4), the Dial (ESP32-S3), the power node (ESP32-C6)
// and the IOmcu (RP2350 over PICOBOOT, put into BOOTSEL at 1200 baud). The
// ESP images are merged images with a partition table; "keep settings" leaves
// their NVS partition on the fake flash.

import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';
import { createHash, generateKeyPairSync, sign } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { NODE_PARTS, PANEL_TABLE_V0_23_15, makeEspImage, makeMerged, makeTable, makeUf2 } from '../test/fixtures.ts';
import { FAMILY } from '../src/uf2.ts';
import { RELEASE_KEY_PEM } from '../src/release_key.ts';

const FAKES = fileURLToPath(new URL('./fake-devices.js', import.meta.url));
const sha = (b: Uint8Array): string => createHash('sha256').update(b).digest('hex');

const TAG = 'v0.23.15';
const OLD = 'v0.22.1';
const IMAGES = {
  // The panel's table of v0.23.15 at 0xC000: nvs 0xD000+0x6000.
  panel: makeMerged({ chipId: 18, bootloaderOffset: 0x2000, tableOffset: 0xc000, table: PANEL_TABLE_V0_23_15, size: 0x20000 + 24_002 }),
  // nvs 0x9000+0x6000.
  dial: makeMerged({ chipId: 9, bootloaderOffset: 0, tableOffset: 0x8000, table: makeTable(NODE_PARTS), size: 0x20000 + 20_002 }),
  'node-pwr': makeMerged({ chipId: 13, bootloaderOffset: 0, tableOffset: 0x8000, table: makeTable(NODE_PARTS), size: 0x20000 + 16_384 }),
  iomcu: makeUf2(0x10002000, 40, FAMILY.ABSOLUTE, true),
};
const FILES: Record<string, string> = {
  panel: `rcbench-panel-p4-merged-${TAG}.bin`,
  dial: 'rcbench-dial-merged-0.23.15.bin',
  'node-pwr': 'rcbench-node-pwr-merged-0.23.15.bin',
  iomcu: `rcbench-iomcu-install-${TAG}.uf2`,
};
const ERASE = { offset: 0x40000, size: 0x20000 };

// The page checks signatures against the release key it embeds. The tests
// put a TEST key in its place in the served script and sign with that; the
// release key's private half is never used.
const TEST_KEY = generateKeyPairSync('ec', { namedCurve: 'P-256' });
const sigOf = (b: Uint8Array): Uint8Array => new Uint8Array(sign('sha256', b, { key: TEST_KEY.privateKey, dsaEncoding: 'der' }));
const pemLines = (pem: string): string[] => pem.split('\n').filter((l) => l && !l.startsWith('-----'));
const RELEASE_LINES = pemLines(RELEASE_KEY_PEM);
const TEST_LINES = pemLines(TEST_KEY.publicKey.export({ type: 'spki', format: 'pem' }).toString());
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

/** Signatures served beside the images: fw/<path> to the bytes of the .sig. */
type Sigs = Record<string, Uint8Array>;

async function setup(page: Page, config: object, opts: { badSha?: boolean; indexDelayMs?: number; sigs?: Sigs } = {}): Promise<void> {
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
  for (const [path, sig] of Object.entries(opts.sigs ?? {})) files.set(path, sig);
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
    if (path === 'index.json' && opts.indexDelayMs) await new Promise((r) => setTimeout(r, opts.indexDelayMs));
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

test('a language switch during a write keeps the visible progress moving, in the new language', async ({ page }) => {
  await setup(page, { bootsel: true, rpDelayMs: 15 });
  await chooseBoard(page, 'iomcu');
  await page.locator('#tag').selectOption(TAG);
  await expectImage(page, FILES.iomcu!, IMAGES.iomcu);
  await page.locator('#connect-pico').click();
  await expect(page.locator('#conn-state')).toBeVisible();
  const other = isDe(page) ? 'en' : 'de';
  await page.locator('#write').click();
  await expect(page.locator('#prog-text')).not.toBeEmpty();
  await page.locator(`.lang button[data-lang="${other}"]`).click();
  await expect(page.locator('html')).toHaveAttribute('lang', other);
  // The new elements carry the last progress at once and keep moving.
  await expect(page.locator('#prog-text')).toContainText(other === 'de' ? 'Bytes' : 'bytes');
  const first = await page.locator('#prog-text').textContent();
  await expect(page.locator('#prog-text')).not.toHaveText(first ?? '');
  await expect(page.locator('#prog')).not.toHaveJSProperty('value', 0);
  await expect(page.locator('#result.ok')).toBeVisible({ timeout: 30_000 });
});

test('a local file picked while the release list loads stays when the list arrives', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } }, { indexDelayMs: 1500 });
  await chooseBoard(page, 'dial');
  await page.locator('input[name="src"][value="file"]').check();
  await page.locator('#file').setInputFiles({ name: 'dial-local.bin', mimeType: 'application/octet-stream', buffer: Buffer.from(IMAGES.dial) });
  await expect(page.locator('#image')).toContainText('dial-local.bin');
  // The list arrives after the file: the release radio comes to life, the file stays.
  await expect(page.locator('input[name="src"][value="release"]')).toBeEnabled({ timeout: 10_000 });
  await expect(page.locator('input[name="src"][value="file"]')).toBeChecked();
  await expect(page.locator('#image')).toContainText('dial-local.bin');
  await expect(page.locator('#image')).toContainText(sha(IMAGES.dial));
});

type Fake = {
  espFlash: Uint8Array; espOps: number[]; espWrites: [number, number][]; espMd5: [number, number][]; espBauds: number[];
  flashHash: (k: string, s: number, l: number) => Promise<string>;
};
/** The state of the fake ESP after a write: the NVS bytes at 0xD000, the hashes of the two halves of the image, the commands. */
const espState = (page: Page, len: number) => page.evaluate(async (n) => {
  const x = (window as unknown as { __fake: Fake }).__fake;
  return {
    nvs: Array.from(new Set(x.espFlash.subarray(0xd000, 0x13000))),
    all: await x.flashHash('esp', 0, n),
    before: await x.flashHash('esp', 0, 0xd000),
    after: await x.flashHash('esp', 0x13000, n - 0x13000),
    writes: x.espWrites, md5: x.espMd5, ops: x.espOps, bauds: x.espBauds,
  };
}, len);
const pad4 = (n: number): number => Math.ceil(n / 4) * 4;

async function connectPanelWithNvs(page: Page): Promise<void> {
  await setup(page, { esp: { chipId: 18 } });
  await expectImage(page, FILES.panel!, IMAGES.panel);
  // Settings on the board before the write: 24KiB of NVS at 0xD000.
  await page.evaluate(() => { (window as unknown as { __fake: Fake }).__fake.espFlash.fill(0x42, 0xd000, 0x13000); });
  await page.locator('#connect-esp').click();
  await expect(page.locator('#conn-state')).toBeVisible({ timeout: 20_000 });
}

test('panel: keep settings is on, lists nvs at 0xD000, and the NVS bytes survive the write', async ({ page }) => {
  await connectPanelWithNvs(page);
  await expect(page.locator('#keep')).toBeChecked();
  await expect(page.locator('#keep-list')).toContainText('nvs (nvs)');
  await expect(page.locator('#keep-list')).toContainText('0xD000');
  await expect(page.locator('#keep-list')).toContainText(isDe(page) ? '24.576 Bytes' : '24,576 bytes');
  await expect(page.locator('#keep-note')).toContainText('0xC000');
  await writeAndWait(page);
  await expect(page.locator('#result')).toContainText('nvs 0xD000+0x6000');
  const len = IMAGES.panel.length;
  const f = await espState(page, len);
  expect(f.nvs).toEqual([0x42]);
  expect(f.before).toBe(sha(IMAGES.panel.subarray(0, 0xd000)));
  expect(f.after).toBe(sha(IMAGES.panel.subarray(0x13000)));
  expect(f.writes).toEqual([[0, 0xd000], [0x13000, pad4(len - 0x13000)]]);
  // The verify covers exactly the written ranges.
  expect(f.md5).toEqual([[0, 0xd000], [0x13000, pad4(len - 0x13000)]]);
  // USB Serial/JTAG 303a:1001: no baud change command (0x0F), no host rate change.
  expect(f.ops).not.toContain(0x0f);
  expect(f.bauds).toEqual([]);
});

test('panel: keep settings off writes the whole image and erases the NVS bytes', async ({ page }) => {
  await connectPanelWithNvs(page);
  await page.locator('#keep').uncheck();
  await expect(page.locator('#keep-note')).toContainText(label(page, 'erased', 'gelöscht'));
  await expect(page.locator('#keep-list')).toHaveCount(0);
  await writeAndWait(page);
  const len = IMAGES.panel.length;
  const f = await espState(page, len);
  expect(f.nvs).toEqual([0xff]);
  expect(f.all).toBe(sha(IMAGES.panel));
  expect(f.writes).toEqual([[0, pad4(len)]]);
  expect(f.md5).toEqual([[0, pad4(len)]]);
});

test('an image whose partition table is broken: keep settings refuses with the reason, a full write stays possible', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  const broken = Uint8Array.from(IMAGES.panel);
  broken[0xc000 + 8] ^= 0x01;
  await page.locator('input[name="src"][value="file"]').check();
  await page.locator('#file').setInputFiles({ name: 'panel-broken.bin', mimeType: 'application/octet-stream', buffer: Buffer.from(broken) });
  await expect(page.locator('#image')).toContainText('panel-broken.bin');
  await page.locator('#connect-esp').click();
  await expect(page.locator('#conn-state')).toBeVisible({ timeout: 20_000 });
  await expect(page.locator('#keep-note.bad')).toContainText('MD5');
  await expect(page.locator('#keep-note.bad')).toContainText('0xC000');
  await expect(page.locator('#write')).toBeDisabled();
  await page.locator('#keep').uncheck();
  await expect(page.locator('#write')).toBeEnabled();
  const f = await espState(page, IMAGES.panel.length);
  expect(f.ops).not.toContain(0x10);
});

test('a USB-to-UART bridge (1A86:55D3) gets the change to 460800 baud', async ({ page }) => {
  await setup(page, { esp: { chipId: 13, usb: [0x1a86, 0x55d3] } });
  await chooseBoard(page, 'node-pwr');
  await expectImage(page, FILES['node-pwr']!, IMAGES['node-pwr']);
  await page.locator('#connect-esp').click();
  await expect(page.locator('#conn-state')).toBeVisible({ timeout: 20_000 });
  await expect(page.locator('#keep-list')).toContainText('0x9000');
  await writeAndWait(page);
  const f = await espState(page, IMAGES['node-pwr'].length);
  expect(f.ops).toContain(0x0f);
  expect(f.bauds).toContain(460800);
});

// ---- Release signatures ----

const SIG_TEXT = {
  signed: ['signed by the rcbench release key', 'signiert mit dem rcbench-Release-Schlüssel'],
  none: ['no signature', 'keine Signatur'],
  mismatch: ['signature does not match', 'Signatur stimmt nicht'],
  local: ['local file, signature not checked', 'lokale Datei, Signatur nicht geprüft'],
} as const;

async function expectSig(page: Page, state: keyof typeof SIG_TEXT, file: string): Promise<void> {
  const note = page.locator('#sig-state');
  await expect(note).toHaveAttribute('data-state', state);
  await expect(note).toContainText(label(page, SIG_TEXT[state][0], SIG_TEXT[state][1]));
  if (file) await expect(note).toContainText(file);
  await expect(note).toHaveClass(state === 'signed' ? /\bok\b/ : state === 'local' ? /\binfo\b/ : /\bwarn\b/);
}

const panelSig = `${TAG}/${FILES.panel}.sig`;

test('signature: a release image with a valid .sig is signed by the rcbench release key', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } }, { sigs: { [panelSig]: sigOf(IMAGES.panel) } });
  await expectImage(page, FILES.panel!, IMAGES.panel);
  await expectSig(page, 'signed', `${FILES.panel}.sig`);
});

test('signature: a release image without a .sig shows no signature, and WRITE stays possible', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await expectImage(page, FILES.panel!, IMAGES.panel);
  await expectSig(page, 'none', `${FILES.panel}.sig`);
  await page.locator('#connect-esp').click();
  await expect(page.locator('#conn-state')).toBeVisible({ timeout: 20_000 });
  await expect(page.locator('#write')).toBeEnabled();
});

test('signature: a .sig of another image or out of form does not match, and WRITE stays possible', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } }, { sigs: {
    [panelSig]: sigOf(IMAGES.dial),
    [`${TAG}/${FILES.dial}.sig`]: new TextEncoder().encode('not a signature'),
  } });
  await expectSig(page, 'mismatch', `${FILES.panel}.sig`);
  await chooseBoard(page, 'dial');
  await expectImage(page, FILES.dial!, IMAGES.dial);
  await expectSig(page, 'mismatch', `${FILES.dial}.sig`);
  await page.locator('#connect-esp').click();
  await expect(page.locator('#conn-state')).toBeVisible({ timeout: 20_000 });
  await writeAndWait(page);
});

test('signature: a local file is not checked unless its .sig is chosen as well', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await chooseBoard(page, 'dial');
  await page.locator('input[name="src"][value="file"]').check();
  const pick = (id: string, name: string, b: Uint8Array): Promise<void> => page.locator(id).setInputFiles({ name, mimeType: 'application/octet-stream', buffer: Buffer.from(b) });
  await pick('#file', 'dial.bin', IMAGES.dial);
  await expectSig(page, 'local', '');
  await pick('#sigfile', 'dial.bin.sig', sigOf(IMAGES.dial));
  await expectSig(page, 'signed', 'dial.bin.sig');
  await expect(page.locator('#sig-picked')).toHaveText('dial.bin.sig');
  await pick('#sigfile', 'other.sig', sigOf(IMAGES.panel));
  await expectSig(page, 'mismatch', 'other.sig');
  await pick('#sigfile', 'empty.sig', new Uint8Array(0));
  await expectSig(page, 'none', 'empty.sig');
});
