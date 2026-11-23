// Advanced mode: the switch on Task, "Write a file of my own" over all 9
// boards, the release select, the local file and its .sig file, "Keep
// settings" off, the Core2350B with a local file, WRITE on a signature that
// does not match, and the hints on Check that point simple mode to Advanced.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import { makeEspImage } from '../test/fixtures.ts';
import {
  FILES, IMAGES, OLD, TAG, advancedOn, connectEsp, expectImage, expectStep, label, pad4, pickFile, setup, sha, sigOf, walk, writeAndWait,
} from './helpers.ts';
import type { Fake } from './helpers.ts';

const panelSig = `${TAG}/${FILES.panel}.sig`;

/** The fake ESP after a write: the NVS bytes at 0xD000, the hash of the image range, the write and MD5 ranges. */
const espState = (page: Page, len: number) => page.evaluate(async (n) => {
  const x = (window as unknown as { __fake: Fake }).__fake;
  return {
    nvs: Array.from(new Set(x.espFlash.subarray(0xd000, 0x13000))),
    all: await x.flashHash('esp', 0, n),
    writes: x.espWrites, md5: x.espMd5, ops: x.espOps,
  };
}, len);

/** CONNECT on an RP2350 in BOOTSEL: the page moves to Check on its own. */
async function connectPico(page: Page): Promise<void> {
  await page.locator('#connect-pico').click();
  await expectStep(page, 'check');
}

/** BACK twice from Check: Connect, then Board. */
async function backToBoard(page: Page): Promise<void> {
  await page.locator('#back').click();
  await expectStep(page, 'connect');
  await page.locator('#back').click();
  await expectStep(page, 'board');
}

test('the Advanced switch adds "Write a file of my own" with all 9 boards, and the source and the checkbox on Check; off resets them', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await expect(page.locator('[data-task="file"]')).toHaveCount(0);
  await expect(page.locator('#adv')).not.toBeChecked();
  await advancedOn(page);
  await expect(page.locator('#log-box')).toHaveAttribute('open', '');
  await page.locator('[data-task="file"]').click();
  await expectStep(page, 'board');
  await expect(page.locator('[data-board]')).toHaveCount(9);
  await expect(page.locator('[data-board="core2350b"]')).toBeEnabled();
  await page.locator('#cancel').click();
  await expectStep(page, 'task');
  await expect(page.locator('#adv')).toBeChecked();
  await walk(page, 'panel');
  await connectEsp(page);
  await expectImage(page, FILES.panel, IMAGES.panel);
  await expect(page.locator('#tag')).toBeVisible();
  await expect(page.locator('#file')).toBeDisabled();
  await page.locator('#keep').uncheck();
  await page.locator('#cancel').click();
  await page.locator('#adv').uncheck();
  await expect(page.locator('[data-task="file"]')).toHaveCount(0);
  await expect(page).not.toHaveURL(/adv=1/);
  await walk(page, 'panel');
  await connectEsp(page);
  await expectImage(page, FILES.panel, IMAGES.panel);
  await expect(page.locator('#tag')).toHaveCount(0);
  await expect(page.locator('#file')).toHaveCount(0);
  await expect(page.locator('#keep')).toHaveCount(0);
  await expect(page.locator('#keep-list')).toContainText('0xD000');
});

test('the release select lists per board the releases that carry it, newest first, with kind; the release of the visit is selected', async ({ page }) => {
  await setup(page, { bootsel: true });
  await advancedOn(page);
  await walk(page, 'iomcu', 'iomcu');
  await connectPico(page);
  const opts = page.locator('#tag option');
  await expect(opts).toHaveCount(2);
  await expect(opts.nth(0)).toContainText(`${TAG}, ${label(page, 'testing', 'Testing')}`);
  await expect(opts.nth(1)).toContainText(`${OLD}, ${label(page, 'stable', 'Stable')}`);
  await expect(page.locator('#tag')).toHaveValue(TAG);
  await expectImage(page, FILES.iomcu, IMAGES.iomcu, TAG);
  // The rbmod carrier: no release carries it, so the local file is preselected.
  await backToBoard(page);
  await expect(page.locator('[data-board="iomcu-rbmod"]')).toBeEnabled();
  await page.locator('[data-board="iomcu-rbmod"]').click();
  await connectPico(page);
  await expect(page.locator('#tag option')).toHaveCount(0);
  await expect(page.locator('input[name="src"][value="release"]')).toBeDisabled();
  await expect(page.locator('input[name="src"][value="file"]')).toBeChecked();
  await expect(page.locator('#write')).toBeDisabled();
});

test('a picked release stays when the next board has an image in it', async ({ page }) => {
  await setup(page, { bootsel: true });
  await advancedOn(page);
  await walk(page, 'iomcu', 'iomcu');
  await connectPico(page);
  await page.locator('#tag').selectOption(OLD);
  await expectImage(page, FILES.iomcu, IMAGES.iomcu, OLD);
  await backToBoard(page);
  await page.locator('[data-board="iomcu-rbmod"]').click();
  await connectPico(page);
  await expect(page.locator('input[name="src"][value="file"]')).toBeChecked();
  await backToBoard(page);
  await page.locator('[data-board="iomcu"]').click();
  await connectPico(page);
  await expect(page.locator('#tag')).toHaveValue(OLD);
  await expectImage(page, FILES.iomcu, IMAGES.iomcu, OLD);
});

test('a local file of the wrong chip is refused, one of the right chip is shown with its SHA-256 and written', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await advancedOn(page);
  await walk(page, 'file', 'dial');
  await connectEsp(page);
  await expect(page.locator('input[name="src"][value="file"]')).toBeChecked();
  await pickFile(page, '#file', 'c6.bin', IMAGES['node-pwr']);
  await expect(page.locator('#image-note.bad')).toContainText('ESP32-C6');
  await expect(page.locator('#write')).toBeDisabled();
  await pickFile(page, '#file', 'app.bin', makeEspImage(9, { app: true }));
  await expect(page.locator('#image-note.bad')).toContainText('merged');
  await pickFile(page, '#file', 'dial.bin', IMAGES.dial);
  await expect(page.locator('#image')).toContainText(sha(IMAGES.dial));
  await expect(page.locator('#image')).toContainText('dial.bin');
  await expect(page.locator('#image')).toContainText(label(page, 'local file', 'lokale Datei'));
  await expect(page.locator('#image')).not.toContainText(TAG);
  await writeAndWait(page);
  await expect(page.locator('#result')).toContainText('dial.bin');
});

test('a local file picked while the release list loads stays when the list arrives', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } }, { indexDelayMs: 3000 });
  await advancedOn(page);
  await walk(page, 'file', 'dial');
  await connectEsp(page);
  await expect(page.locator('input[name="src"][value="release"]')).toBeDisabled();
  await pickFile(page, '#file', 'dial-local.bin', IMAGES.dial);
  await expect(page.locator('#image')).toContainText('dial-local.bin');
  // The list arrives after the file: the release radio comes to life, the file stays.
  await expect(page.locator('input[name="src"][value="release"]')).toBeEnabled({ timeout: 10_000 });
  await expect(page.locator('input[name="src"][value="file"]')).toBeChecked();
  await expect(page.locator('#image')).toContainText('dial-local.bin');
  await expect(page.locator('#image')).toContainText(sha(IMAGES.dial));
  await expect(page.locator('#write')).toBeEnabled();
});

test('panel: "Keep settings" off writes the whole image and erases the NVS bytes', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await advancedOn(page);
  await walk(page, 'panel');
  await page.evaluate(() => { (window as unknown as { __fake: Fake }).__fake.espFlash.fill(0x42, 0xd000, 0x13000); });
  await connectEsp(page);
  await expectImage(page, FILES.panel, IMAGES.panel);
  await expect(page.locator('#keep')).toBeChecked();
  await expect(page.locator('#keep-list')).toContainText('0xD000');
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

test('a local image whose partition table is broken: "Keep settings" refuses, a full write stays possible', async ({ page }) => {
  const broken = Uint8Array.from(IMAGES.panel);
  broken[0xc000 + 8] ^= 0x01;
  await setup(page, { esp: { chipId: 18 } });
  await advancedOn(page);
  await walk(page, 'file', 'panel');
  await connectEsp(page);
  await pickFile(page, '#file', 'panel-broken.bin', broken);
  await expect(page.locator('#image')).toContainText('panel-broken.bin');
  await expect(page.locator('#keep-note.bad')).toContainText('MD5');
  await expect(page.locator('#keep-note.bad')).toContainText('0xC000');
  await expect(page.locator('#keep-note.bad')).toContainText(label(page, 'Untick it', 'Nimm den Haken weg'));
  await expect(page.locator('#hint-erase')).toHaveCount(0);
  await expect(page.locator('#write')).toBeDisabled();
  await expect(page.locator('#write')).toHaveAttribute('data-block', 'keep');
  await page.locator('#keep').uncheck();
  await writeAndWait(page);
  const len = broken.length;
  const f = await espState(page, len);
  expect(f.all).toBe(sha(broken));
  expect(f.writes).toEqual([[0, pad4(len)]]);
});

test('a local file: signature not checked without its .sig; with one signed, not matching (red, WRITE enabled) or none', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await advancedOn(page);
  await walk(page, 'file', 'dial');
  await connectEsp(page);
  const sig = page.locator('#sig-state');
  await pickFile(page, '#file', 'dial.bin', IMAGES.dial);
  await expect(sig).toHaveAttribute('data-state', 'unchecked');
  await expect(sig).toHaveClass(/\binfo\b/);
  await expect(page.locator('#write')).toBeEnabled();
  await pickFile(page, '#sigfile', 'dial.bin.sig', sigOf(IMAGES.dial));
  await expect(sig).toHaveAttribute('data-state', 'signed');
  await expect(sig).toContainText('dial.bin.sig');
  await expect(page.locator('#sig-picked')).toHaveText('dial.bin.sig');
  await pickFile(page, '#sigfile', 'other.sig', sigOf(IMAGES.panel));
  await expect(sig).toHaveAttribute('data-state', 'mismatch');
  await expect(sig).toHaveClass(/\bbad\b/);
  await expect(page.locator('#write')).toBeEnabled();
  await pickFile(page, '#sigfile', 'empty.sig', new Uint8Array(0));
  await expect(sig).toHaveAttribute('data-state', 'none');
  await expect(page.locator('#write')).toBeEnabled();
});

test('a release image whose signature does not match: Advanced shows the red warning and writes', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } }, { sigs: { [panelSig]: sigOf(IMAGES.dial) } });
  await advancedOn(page);
  await walk(page, 'panel');
  await connectEsp(page);
  const note = page.locator('#sig-state');
  await expect(note).toHaveAttribute('data-state', 'mismatch');
  await expect(note).toHaveClass(/\bbad\b/);
  await expect(note).toContainText(label(page, 'Advanced mode allows WRITE', 'erweiterte Modus erlaubt SCHREIBEN'));
  await writeAndWait(page);
});

test('Core2350B: absent in simple mode; in Advanced a local UF2 file only, written over PICOBOOT', async ({ page }) => {
  await setup(page, { bootsel: true });
  await page.locator('[data-task="iomcu"]').click();
  await expect(page.locator('[data-board="core2350b"]')).toHaveCount(0);
  await page.locator('#cancel').click();
  await advancedOn(page);
  await walk(page, 'iomcu', 'core2350b');
  await connectPico(page);
  await expect(page.locator('input[name="src"][value="release"]')).toBeDisabled();
  await expect(page.locator('input[name="src"][value="file"]')).toBeChecked();
  await pickFile(page, '#file', 'core.bin', IMAGES.iomcu);
  await expect(page.locator('#image-note.bad')).toContainText('.uf2');
  await pickFile(page, '#file', 'core.uf2', IMAGES.iomcu);
  await expect(page.locator('#image')).toContainText(sha(IMAGES.iomcu));
  await writeAndWait(page);
  const image = await page.evaluate(() => (window as unknown as { __fake: Fake }).__fake.flashHash('rp', 0x2000, 40 * 256));
  const payload = new Uint8Array(40 * 256);
  for (let i = 0; i < 40; i++) payload.set(IMAGES.iomcu.subarray((i + 1) * 512 + 32, (i + 1) * 512 + 288), i * 256);
  expect(image).toBe(sha(payload));
});

test('simple mode points to Advanced when the release list fails; Advanced writes a local file then', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } }, { missing: ['index.json'] });
  await walk(page, 'panel');
  await connectEsp(page);
  await expect(page.locator('#releases-note')).toContainText('HTTP 404');
  await expect(page.locator('#hint-file')).toContainText(label(page, 'writes a local file', 'schreibt eine lokale Datei'));
  await expect(page.locator('#write')).toBeDisabled();
  await page.locator('#cancel').click();
  await advancedOn(page);
  await walk(page, 'panel');
  await connectEsp(page);
  await expect(page.locator('#hint-file')).toHaveCount(0);
  await expect(page.locator('#write')).toHaveAttribute('data-block', 'releases');
  await page.locator('input[name="src"][value="file"]').check();
  await pickFile(page, '#file', 'panel.bin', IMAGES.panel);
  await expect(page.locator('#image')).toContainText(sha(IMAGES.panel));
  await expect(page.locator('#write')).toBeEnabled();
});

test('simple mode points to Advanced when "Keep settings" refuses the image', async ({ page }) => {
  const broken = Uint8Array.from(IMAGES.panel);
  broken[0xc000 + 8] ^= 0x01;
  await setup(page, { esp: { chipId: 18 } }, { images: { panel: broken } });
  await walk(page, 'panel');
  await connectEsp(page);
  await expect(page.locator('#keep-note.bad')).toContainText('MD5');
  await expect(page.locator('#hint-erase')).toContainText(label(page, 'writes the whole image', 'schreibt das ganze Image'));
  await expect(page.locator('#write')).toBeDisabled();
});
