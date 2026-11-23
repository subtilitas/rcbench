// The browser: no Web Serial and no WebUSB, WebUSB missing alone, the
// language, the footer links, the width of a phone.

import { expect, test } from '@playwright/test';
import { connectEsp, expectStep, isDe, label, setup, walk } from './helpers.ts';

test('a browser without Web Serial and WebUSB gets the notice; the tasks stay listed and disabled', async ({ page }) => {
  await setup(page, { none: true });
  await expect(page.locator('#notice')).toBeVisible();
  await expect(page.locator('#notice')).toContainText('Chrome');
  await expect(page.locator('#notice')).toContainText('Edge');
  const tiles = page.locator('#tiles [data-task]');
  await expect(tiles).toHaveCount(4);
  for (const tile of await tiles.all()) await expect(tile).toBeDisabled();
});

test('a browser without WebUSB greys the IOmcu task with the reason; the others stay usable', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 }, noUsb: true });
  const iomcu = page.locator('[data-task="iomcu"]');
  await expect(iomcu).toBeDisabled();
  await expect(iomcu).toContainText('WebUSB');
  await expect(iomcu).toContainText(label(page, 'which this browser lacks', 'das diesem Browser fehlt'));
  for (const t of ['panel', 'wireless', 'c6']) await expect(page.locator(`[data-task="${t}"]`)).toBeEnabled();
  await expect(page.locator('#notice')).toBeHidden();
});

test('the first screen lists 4 tasks in order, the C6 radio repair last; the Core2350B is not in simple mode', async ({ page }) => {
  await setup(page, { iomcu: true });
  await expect(page.locator('#tiles [data-task]')).toHaveCount(4);
  const ids = await page.locator('#tiles [data-task]').evaluateAll((els) => els.map((e) => (e as HTMLElement).dataset.task));
  expect(ids).toEqual(['panel', 'iomcu', 'wireless', 'c6']);
  await expect(page.locator('#tiles')).toContainText(label(page, 'Repair', 'reparieren'));
  await page.locator('[data-task="iomcu"]').click();
  await expect(page.locator('[data-board]')).toHaveCount(2);
  await expect(page.locator('[data-board="core2350b"]')).toHaveCount(0);
});

test('the language follows the browser and the buttons switch it; the footer links stay on this site', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await expect(page.locator('html')).toHaveAttribute('lang', isDe(page) ? 'de' : 'en');
  await walk(page, 'panel');
  await connectEsp(page);
  await page.locator('.lang button[data-lang="de"]').click();
  await expect(page.locator('#write')).toHaveText('SCHREIBEN');
  await expect(page.locator('#back')).toHaveText('ZURÜCK');
  await page.locator('.lang button[data-lang="en"]').click();
  await expect(page.locator('#write')).toHaveText('WRITE');
  await expect(page.locator('#cancel')).toHaveText('CANCEL');
  await expectStep(page, 'check');
  await expect(page.locator('#liclink')).toHaveAttribute('href', 'licenses.txt');
  await expect(page.locator('#cfglink')).toHaveAttribute('href', '../');
  const lic = await page.request.get('licenses.txt');
  expect(await lic.text()).toContain('esptool-js 0.7.1');
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(overflow).toBeLessThanOrEqual(0);
});

test('no step is wider than the screen', async ({ page }) => {
  await setup(page, { esp: { chipId: 13, usb: [0x1a86, 0x55d3] } });
  const overflow = (): Promise<number> => page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  expect(await overflow()).toBeLessThanOrEqual(0);
  await page.locator('[data-task="c6"]').click();
  await expectStep(page, 'prepare');
  expect(await overflow()).toBeLessThanOrEqual(0);
  await page.locator('#next').click();
  expect(await overflow()).toBeLessThanOrEqual(0);
  await connectEsp(page);
  expect(await overflow()).toBeLessThanOrEqual(0);
});
