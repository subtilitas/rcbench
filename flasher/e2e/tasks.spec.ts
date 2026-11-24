// Every task of the first screen to Done against fake devices: the panel
// (ESP32-P4), the IOmcu (RP2350 over PICOBOOT, into BOOTSEL at 1200 baud),
// the Dial and the display node (ESP32-S3) and the power node (ESP32-C6) as
// wireless devices, and
// the C6 radio behind a USB-to-UART bridge. "Keep settings" is on in simple
// mode: the NVS partition stays on the fake flash.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import {
  FILES, IMAGES, TAG, connectEsp, expectImage, expectStep, flashHash, isDe, label, pad4, setup, sha, walk, writeAndWait,
} from './helpers.ts';
import type { Fake } from './helpers.ts';
import { NODE_PARTS, makeMerged, makeTable } from '../test/fixtures.ts';

/** The state of the fake ESP after a write: the NVS bytes at 0xD000, the hashes of the two halves of the image, the commands. */
const espState = (page: Page, len: number) => page.evaluate(async (n) => {
  const x = (window as unknown as { __fake: Fake }).__fake;
  return {
    nvs: Array.from(new Set(x.espFlash.subarray(0xd000, 0x13000))),
    before: await x.flashHash('esp', 0, 0xd000),
    after: await x.flashHash('esp', 0x13000, n - 0x13000),
    writes: x.espWrites, md5: x.espMd5, ops: x.espOps, bauds: x.espBauds,
  };
}, len);

test('panel: Connect, Check with the kept NVS, WRITE, verify, Done; the NVS bytes survive', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await walk(page, 'panel');
  await expect(page.locator('#step-title')).toHaveText(label(page, 'Step 2 of 5: Connect the board', 'Schritt 2 von 5: Board verbinden'));
  await expect(page.locator('#conn-lines')).toContainText('USB-C');
  // Settings on the board before the write: 24KiB of NVS at 0xD000.
  await page.evaluate(() => { (window as unknown as { __fake: Fake }).__fake.espFlash.fill(0x42, 0xd000, 0x13000); });
  await connectEsp(page);
  await expectImage(page, FILES.panel, IMAGES.panel);
  await expect(page.locator('#image')).toContainText('ESP32-P4');
  await expect(page.locator('#image')).toContainText(label(page, 'testing', 'Testing'));
  await expect(page.locator('#keep')).toHaveCount(0);
  await expect(page.locator('#keep-list')).toContainText('nvs (nvs)');
  await expect(page.locator('#keep-list')).toContainText('0xD000');
  await expect(page.locator('#keep-list')).toContainText(isDe(page) ? '24.576 Bytes' : '24,576 bytes');
  await expect(page.locator('#keep-note')).toContainText('0xC000');
  await writeAndWait(page);
  await expect(page.locator('#step-title')).toHaveText(label(page, 'Step 5 of 5: Done', 'Schritt 5 von 5: Fertig'));
  await expect(page.locator('#result')).toContainText(FILES.panel);
  await expect(page.locator('#result')).toContainText('nvs 0xD000+0x6000');
  await expect(page.locator('#next')).toContainText('LINK OK CAN 1Mbit/s');
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

const DIAL_KEPT = ['keeps its settings and its binding to the panel', 'behält seine Einstellungen und seine Bindung an das Panel'] as const;
const NODE_NEXT = ['a node bound before stays bound', 'Ein schon gebundener Knoten bleibt gebunden'] as const;
const WIRELESS = [
  ['dial', 9, 'SETUP > WIRELESS > DIAL', DIAL_KEPT],
  ['node-pwr', 13, 'SETUP > WIRELESS > POWER NODE', NODE_NEXT],
  ['node-display', 9, 'SETUP > WIRELESS > POWER NODE', NODE_NEXT],
] as const;

for (const [board, chipId, next, line] of WIRELESS) {
  test(`wireless device ${board}: Board, Connect, Check, WRITE, verify, Done with the NVS kept and the next step for it`, async ({ page }) => {
    await setup(page, { esp: { chipId } });
    await walk(page, 'wireless', board);
    await expect(page.locator('#step-title')).toContainText(label(page, 'Step 3 of 6', 'Schritt 3 von 6'));
    await expect(page.locator('#conn-lines')).toContainText(board === 'dial' ? 'GPIO0' : 'XT60');
    await connectEsp(page);
    await expectImage(page, FILES[board], IMAGES[board]);
    await expect(page.locator('#keep-list')).toContainText('0x9000');
    await writeAndWait(page);
    await expect(page.locator('#result')).toContainText(FILES[board]);
    await expect(page.locator('#result')).toContainText('nvs 0x9000+0x6000');
    await expect(page.locator('#next')).toHaveAttribute('data-kept', 'nvs');
    await expect(page.locator('#next')).toContainText(label(page, line[0], line[1]));
    await expect(page.locator('#next')).toContainText(next);
    expect(await flashHash(page, 'esp', 0, IMAGES[board].length)).toBe(sha(IMAGES[board]));
  });
}

test('a Dial image without a settings partition: nothing kept, Done shows NOT BOUND and asks to bind', async ({ page }) => {
  // The node table without its nvs entry: the plan writes the whole image.
  const table = makeTable(NODE_PARTS.filter((p) => p.name !== 'nvs'));
  const img = makeMerged({ chipId: 9, bootloaderOffset: 0, tableOffset: 0x8000, table, size: 0x20000 + 20_002 });
  await setup(page, { esp: { chipId: 9 } }, { images: { dial: img } });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await expectImage(page, FILES.dial, img);
  await expect(page.locator('#keep-list')).toHaveCount(0);
  await writeAndWait(page);
  await expect(page.locator('#next')).toHaveAttribute('data-kept', 'none');
  await expect(page.locator('#next')).toHaveText(label(page,
    'Next: the Dial shows NOT BOUND and its name, DIAL- and 4 hex digits. Bind it on the panel: SETUP > WIRELESS > DIAL.',
    'Weiter: Das Dial zeigt NOT BOUND und seinen Namen, DIAL- und 4 Hex-Ziffern. Binde es am Panel: SETUP > WIRELESS > DIAL.'));
});

test('IOmcu: the release of the visit, 1200 baud into BOOTSEL, PICOBOOT erase, write, read back, reboot', async ({ page }) => {
  await setup(page, { iomcu: true });
  await walk(page, 'iomcu', 'iomcu');
  await page.locator('#connect-pico').click();
  await expect(page.locator('#conn-note.bad')).toBeVisible();
  await page.locator('#bootsel').click();
  await expect(page.locator('#bootsel-note.ok')).toBeVisible();
  await page.locator('#connect-pico').click();
  await expectStep(page, 'check');
  await expect(page.locator('#image')).toContainText('PICOBOOT');
  // One release per visit: the panel image's release, not the older stable one that carries the IOmcu alone.
  await expectImage(page, FILES.iomcu, IMAGES.iomcu, TAG);
  await expect(page.locator('#image')).toContainText('0x40000+0x20000');
  await writeAndWait(page);
  await expect(page.locator('#next')).toContainText('SETUP > OUTPUTS');
  const fake = await page.evaluate(async () => {
    const f = (window as unknown as { __fake: Fake }).__fake;
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

test('C6 radio: Prepare, the adapter behind a USB-to-UART bridge (1A86:55D3) at 460800 baud, Done with the next step', async ({ page }) => {
  await setup(page, { esp: { chipId: 13, usb: [0x1a86, 0x55d3] } });
  await page.locator('[data-task="c6"]').click();
  await expectStep(page, 'prepare');
  await expect(page.locator('#step-title')).toContainText(label(page, 'Step 2 of 6', 'Schritt 2 von 6'));
  await expect(page.locator('#prepare li')).toHaveCount(3);
  await expect(page.locator('#prepare')).toContainText('IO9');
  await page.locator('#next').click();
  await expectStep(page, 'connect');
  await expect(page.locator('#conn-lines')).toContainText(label(page, 'USB-to-UART', 'USB-UART'));
  await connectEsp(page);
  await expectImage(page, FILES.c6, IMAGES.c6);
  await writeAndWait(page);
  await expect(page.locator('#next')).toContainText('IO9');
  const f = await page.evaluate(() => { const x = (window as unknown as { __fake: Fake }).__fake; return { ops: x.espOps, bauds: x.espBauds }; });
  expect(f.ops).toContain(0x0f);
  expect(f.bauds).toContain(460800);
});

test('a language switch during a write keeps the visible progress moving, in the new language', async ({ page }) => {
  await setup(page, { bootsel: true, rpDelayMs: 15 });
  await walk(page, 'iomcu', 'iomcu');
  await page.locator('#connect-pico').click();
  await expectStep(page, 'check');
  await expectImage(page, FILES.iomcu, IMAGES.iomcu);
  const other = isDe(page) ? 'en' : 'de';
  await page.locator('#write').click();
  await expectStep(page, 'write');
  await expect(page.locator('#prog-text')).not.toBeEmpty();
  await expect(page.locator('#back')).toBeDisabled();
  await expect(page.locator('#cancel')).toBeDisabled();
  await page.locator(`.lang button[data-lang="${other}"]`).click();
  await expect(page.locator('html')).toHaveAttribute('lang', other);
  // The new elements carry the last progress at once and keep moving.
  await expect(page.locator('#prog-text')).toContainText(other === 'de' ? 'Bytes' : 'bytes');
  const first = await page.locator('#prog-text').textContent();
  await expect(page.locator('#prog-text')).not.toHaveText(first ?? '');
  // Each phase starts at 0%: the phase list shows the write moving on.
  await expect(page.locator('#phases li[data-state="done"]').first()).toBeVisible();
  await expectStep(page, 'done');
});

test('WRITE ANOTHER: two Dials in a row with the same image', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await writeAndWait(page);
  await page.evaluate(() => { (window as unknown as { __fake: Fake }).__fake.espFlash.fill(0xff); });
  await page.locator('#again').click();
  await expectStep(page, 'connect');
  await expect(page.locator('#conn-state')).toHaveCount(0);
  await connectEsp(page);
  await expectImage(page, FILES.dial, IMAGES.dial);
  await writeAndWait(page);
  expect(await flashHash(page, 'esp', 0, IMAGES.dial.length)).toBe(sha(IMAGES.dial));
  await page.locator('#over').click();
  await expectStep(page, 'task');
});
