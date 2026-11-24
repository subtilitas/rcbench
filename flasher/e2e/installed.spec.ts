// The board check on Check: the page reads the head of the application an
// ESP32 board runs and shows matches, another device or cannot tell. A
// warning only: WRITE stays enabled in every case, in simple mode and in
// Advanced, and the reads end before the write's first command.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import { FILES, IMAGES, advancedOn, connectEsp, expectImage, flashHash, installed, label, setup, sha, walk, writeAndWait } from './helpers.ts';
import type { Fake } from './helpers.ts';

const fake = (page: Page) => page.evaluate(() => {
  const x = (window as unknown as { __fake: Fake }).__fake;
  return { ops: x.espOps, reads: x.espReads };
});

/** The verdict row of Check, its text, and WRITE enabled. */
async function expectVerdict(page: Page, verdict: string, text: string): Promise<void> {
  const row = page.locator('#installed');
  await expect(row).toHaveAttribute('data-verdict', verdict);
  await expect(row).toContainText(text);
  await expect(page.locator('#write')).toBeEnabled();
}

test('matches: a Dial that runs rcbench-dial; read only, 4 reads before the write, then WRITE to Done', async ({ page }) => {
  await setup(page, { esp: { chipId: 9, installed: installed(9, 'rcbench-dial', '0.23.14', 1) } });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await expectImage(page, FILES.dial, IMAGES.dial);
  await expectVerdict(page, 'match', label(page, 'rcbench-dial 0.23.14: the firmware of this board', 'rcbench-dial 0.23.14: die Firmware dieses Boards'));
  await expect(page.locator('#installed-note')).toHaveCount(0);
  const before = await fake(page);
  expect(before.reads).toEqual([[0x8000, 0xc00], [0xf000, 32], [0x10000, 32], [0x20000, 296]]);
  // No write or erase command for the check.
  expect(before.ops.filter((op) => [0x02, 0x03, 0x10, 0x11, 0xd0, 0xd1].includes(op))).toEqual([]);
  await expect(page.locator('#log')).toContainText(/board check: 4 reads, 3432 bytes, \d+ms/);
  await writeAndWait(page);
  const after = await fake(page);
  expect(after.ops.lastIndexOf(0xd2)).toBeLessThan(after.ops.indexOf(0x10));
  expect(await flashHash(page, 'esp', 0, IMAGES.dial.length)).toBe(sha(IMAGES.dial));
});

test('another device: a display node on the cable while the Dial is chosen; the warning names it, WRITE stays', async ({ page }) => {
  await setup(page, { esp: { chipId: 9, installed: installed(9, 'rcbench-node', '0.23.15', 4) } });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await expectVerdict(page, 'other', label(page, 'rcbench-node 0.23.15: the firmware of another device: Display node',
    'rcbench-node 0.23.15: die Firmware eines anderen Geräts: Anzeigeknoten'));
  await expect(page.locator('#installed-note.warn')).toContainText(label(page, 'WRITE stays possible', 'SCHREIBEN bleibt möglich'));
  await writeAndWait(page);
});

test('another device: a C6 node without role record on the C6 radio\'s adapter names both nodes', async ({ page }) => {
  await setup(page, { esp: { chipId: 13, usb: [0x1a86, 0x55d3], installed: installed(13, 'rcbench-node', '0.23.12') } });
  await walk(page, 'c6');
  await connectEsp(page);
  await expectVerdict(page, 'other', label(page, 'Power node or Balance node', 'Leistungsknoten oder Auswuchtknoten'));
});

test('cannot tell: a power node without role record could be either node', async ({ page }) => {
  await setup(page, { esp: { chipId: 13, installed: installed(13, 'rcbench-node', '0.23.12') } });
  await walk(page, 'wireless', 'node-pwr');
  await connectEsp(page);
  await expectVerdict(page, 'unknown', label(page, 'cannot tell', 'nicht erkennbar'));
  await expect(page.locator('#installed')).toContainText('v0.23.13');
  await expect(page.locator('#installed-note')).toHaveCount(0);
});

test('cannot tell: a blank board, 2 reads, then WRITE to Done', async ({ page }) => {
  await setup(page, { esp: { chipId: 13 } });
  await walk(page, 'wireless', 'node-pwr');
  await connectEsp(page);
  await expectVerdict(page, 'unknown', '0x8000');
  expect((await fake(page)).reads).toEqual([[0x8000, 0xc00], [0xc000, 0xc00]]);
  await writeAndWait(page);
});

test('cannot tell: another project on the board', async ({ page }) => {
  await setup(page, { esp: { chipId: 18, installed: installed(18, 'hello_world', '1.0') } });
  await walk(page, 'panel');
  await connectEsp(page);
  await expectVerdict(page, 'unknown', label(page, 'hello_world 1.0 is no firmware of this page', 'hello_world 1.0 ist keine Firmware dieser Seite'));
});

test('cannot tell: the stub refuses the read; the write still runs to Done', async ({ page }) => {
  await setup(page, { esp: { chipId: 13 }, espReadFail: true });
  await walk(page, 'wireless', 'node-pwr');
  await connectEsp(page);
  await expectVerdict(page, 'unknown', label(page, 'reading the flash failed', 'das Lesen des Flashs schlug fehl'));
  await writeAndWait(page);
  expect(await flashHash(page, 'esp', 0, IMAGES['node-pwr'].length)).toBe(sha(IMAGES['node-pwr']));
});

test('cannot tell on the RP2350: no read, the IOmcu images share one name', async ({ page }) => {
  await setup(page, { bootsel: true });
  await walk(page, 'iomcu', 'iomcu');
  await page.locator('#connect-pico').click();
  await expectVerdict(page, 'unknown', 'rcbench-iomcu');
  const cmds = await page.evaluate(() => (window as unknown as { __fake: Fake }).__fake.rpCmds);
  expect(cmds).not.toContain(0x84);
});

test('Advanced: the same warning, WRITE enabled', async ({ page }) => {
  await setup(page, { esp: { chipId: 9, installed: installed(9, 'rcbench-node', '0.23.15', 4) } });
  await advancedOn(page);
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await expect(page.locator('#source')).toBeVisible();
  await expectVerdict(page, 'other', label(page, 'Display node', 'Anzeigeknoten'));
  await expect(page.locator('#installed-note.warn')).toBeVisible();
});

test('a slow first check, CANCEL and a new connect: one set of reads, on the new connection only', async ({ page }) => {
  await setup(page, { esp: { chipId: 9, installed: installed(9, 'rcbench-dial', '0.23.14', 1) } });
  // The check's code is held until the second connect: both checks wait for the same load.
  let release: () => void = () => undefined;
  const held = new Promise<void>((res) => { release = res; });
  await page.route('**/rcbench/flash/assets/installed_read-*.js', async (route) => { await held; await route.fallback(); });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await page.locator('#cancel').click();
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  release();
  await expectVerdict(page, 'match', 'rcbench-dial 0.23.14');
  expect((await fake(page)).reads).toEqual([[0x8000, 0xc00], [0xf000, 32], [0x10000, 32], [0x20000, 296]]);
  await writeAndWait(page);
  expect((await fake(page)).reads).toHaveLength(4);
});
