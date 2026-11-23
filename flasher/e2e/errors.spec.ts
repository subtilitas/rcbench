// The error states of the steps in simple mode: a chip that does not match
// on Connect, a download that fails or differs from the manifest on Check,
// a release list that fails, "Keep settings" refused, the signature states,
// and a write or a verify that fails on Write.

import { expect, test } from '@playwright/test';
import { FILES, IMAGES, TAG, connectEsp, espOps, expectImage, expectStep, label, setup, sha, sigOf, walk } from './helpers.ts';

test('a chip that does not match the board is refused on Connect; CHOOSE ANOTHER BOARD leads to Board', async ({ page }) => {
  await setup(page, { esp: { chipId: 13 } });
  await walk(page, 'wireless', 'dial');
  await page.locator('#connect-esp').click();
  await expect(page.locator('#conn-note.bad')).toContainText('ESP32-S3');
  await expect(page.locator('#conn-note.bad')).toContainText('ESP32-C6');
  await expectStep(page, 'connect');
  const ops = await espOps(page);
  expect(ops).not.toContain(0x10);
  expect(ops).not.toContain(0x11);
  await page.locator('#other-board').click();
  await expectStep(page, 'board');
  await page.locator('[data-board="node-pwr"]').click();
  await connectEsp(page);
  await expectImage(page, FILES['node-pwr'], IMAGES['node-pwr']);
});

test('a download that differs from the manifest: the reason and RETRY on Check, WRITE disabled', async ({ page }) => {
  const files = await setup(page, { esp: { chipId: 9 } }, { badSha: 'dial' });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await expect(page.locator('#image-note.bad')).toContainText('SHA-256');
  await expect(page.locator('#image')).not.toContainText(FILES.dial);
  await expect(page.locator('#write')).toBeDisabled();
  files.set(`${TAG}/${FILES.dial}`, IMAGES.dial);
  await page.locator('#retry').click();
  await expectImage(page, FILES.dial, IMAGES.dial);
  await expect(page.locator('#write')).toBeEnabled();
});

test('a download that fails (HTTP 404), then RETRY with the file served', async ({ page }) => {
  const files = await setup(page, { esp: { chipId: 18 } }, { missing: [`${TAG}/${FILES.panel}`] });
  await walk(page, 'panel');
  await connectEsp(page);
  await expect(page.locator('#image-note.bad')).toContainText('HTTP 404');
  await expect(page.locator('#write')).toBeDisabled();
  files.set(`${TAG}/${FILES.panel}`, IMAGES.panel);
  await page.locator('#retry').click();
  await expectImage(page, FILES.panel, IMAGES.panel);
  await expect(page.locator('#write')).toBeEnabled();
});

test('a release list that fails: the note on Check, WRITE disabled', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } }, { missing: ['index.json'] });
  await walk(page, 'panel');
  await connectEsp(page);
  await expect(page.locator('#releases-note')).toContainText('HTTP 404');
  await expect(page.locator('#write')).toBeDisabled();
  await expect(page.locator('#write')).toHaveAttribute('data-block', 'releases');
});

test('an image whose partition table is broken: "Keep settings" refuses with the reason, WRITE disabled', async ({ page }) => {
  const broken = Uint8Array.from(IMAGES.panel);
  broken[0xc000 + 8] ^= 0x01;
  await setup(page, { esp: { chipId: 18 } }, { images: { panel: broken } });
  await walk(page, 'panel');
  await connectEsp(page);
  await expectImage(page, FILES.panel, broken);
  await expect(page.locator('#keep-note.bad')).toContainText('MD5');
  await expect(page.locator('#keep-note.bad')).toContainText('0xC000');
  await expect(page.locator('#write')).toBeDisabled();
  await expect(page.locator('#write')).toHaveAttribute('data-block', 'keep');
  expect(await espOps(page)).not.toContain(0x10);
});

test('a board that no release on the page carries is greyed on Board', async ({ page }) => {
  await setup(page, { esp: { chipId: 13 } });
  await page.locator('[data-task="wireless"]').click();
  const bal = page.locator('[data-board="node-bal"]');
  await expect(bal).toBeDisabled();
  await expect(bal).toContainText(label(page, 'No release on this page', 'Kein Release auf dieser Seite'));
  await expect(page.locator('[data-board="node-pwr"]')).toBeEnabled();
});

// ---- Release signatures on Check ----

const panelSig = `${TAG}/${FILES.panel}.sig`;

test('signature: a valid .sig is a row of Check, signed by the rcbench release key', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } }, { sigs: { [panelSig]: sigOf(IMAGES.panel) } });
  await walk(page, 'panel');
  await connectEsp(page);
  const row = page.locator('#sig-state');
  await expect(row).toHaveAttribute('data-state', 'signed');
  await expect(row).toContainText(label(page, 'signed by the rcbench release key', 'signiert mit dem rcbench-Release-Schlüssel'));
  await expect(row).toContainText(`${FILES.panel}.sig`);
  await expect(page.locator('#write')).toBeEnabled();
});

test('signature: no .sig is an information note, and WRITE stays enabled', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await walk(page, 'panel');
  await connectEsp(page);
  const note = page.locator('#sig-state');
  await expect(note).toHaveAttribute('data-state', 'none');
  await expect(note).toHaveClass(/\binfo\b/);
  await expect(note).toContainText(label(page, 'no signature', 'keine Signatur'));
  await expect(page.locator('#write')).toBeEnabled();
});

test('signature: a .sig of another image or out of form does not match, and WRITE is disabled', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } }, { sigs: {
    [panelSig]: sigOf(IMAGES.dial),
    [`${TAG}/${FILES.dial}.sig`]: new TextEncoder().encode('not a signature'),
  } });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await expectImage(page, FILES.dial, IMAGES.dial);
  const note = page.locator('#sig-state');
  await expect(note).toHaveAttribute('data-state', 'mismatch');
  await expect(note).toHaveClass(/\bbad\b/);
  await expect(note).toContainText(label(page, 'does not match', 'passt nicht'));
  await expect(page.locator('#write')).toBeDisabled();
  await expect(page.locator('#write')).toHaveAttribute('data-block', 'sig-mismatch');
});

// ---- Write and verify failures ----

test('a write that fails: the error on Write, CONNECT AGAIN leads to Connect with the image kept', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 }, espFailAt: 0x13000 });
  await walk(page, 'panel');
  await connectEsp(page);
  await page.locator('#write').click();
  await expect(page.locator('#result.bad')).toBeVisible({ timeout: 20_000 });
  await expectStep(page, 'write');
  await expect(page.locator('#phases li[data-state="failed"]')).toHaveText(label(page, 'Write', 'Schreiben'));
  await page.locator('#connect-again').click();
  await expectStep(page, 'connect');
  await connectEsp(page);
  // The image is kept: Check shows it at once.
  await expectImage(page, FILES.panel, IMAGES.panel);
  await expect(page.locator('#write')).toBeEnabled();
});

test('an ESP verify that fails: the error in step Verify, CONNECT AGAIN', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 }, espBadMd5: true });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await page.locator('#write').click();
  await expect(page.locator('#result.bad')).toContainText(label(page, 'Verify', 'Prüfen'));
  await expect(page.locator('#phases li[data-state="failed"]')).toHaveText(label(page, 'Verify', 'Prüfen'));
  await expect(page.locator('#connect-again')).toBeEnabled();
});

test('an RP2350 read back that differs: the error in step Verify, CONNECT AGAIN to Connect', async ({ page }) => {
  await setup(page, { bootsel: true, rpCorrupt: true });
  await walk(page, 'iomcu', 'iomcu');
  await page.locator('#connect-pico').click();
  await expectStep(page, 'check');
  await expect(page.locator('#image')).toContainText(sha(IMAGES.iomcu));
  await page.locator('#write').click();
  await expect(page.locator('#result.bad')).toContainText(label(page, 'Verify', 'Prüfen'));
  await expectStep(page, 'write');
  await page.locator('#connect-again').click();
  await expectStep(page, 'connect');
});

test('a reset that fails after a passed verify: Done with the RESET hint, and the port is released for the next CONNECT', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 }, espResetFail: true });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await page.locator('#write').click();
  await expectStep(page, 'done');
  await expect(page.locator('#result.ok')).toContainText(label(page, 'Press RESET', 'Drücke RESET'));
  await page.locator('#again').click();
  await expectStep(page, 'connect');
  await connectEsp(page);
  await expectImage(page, FILES.dial, IMAGES.dial);
});
