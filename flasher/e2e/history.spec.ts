// BACK, CANCEL, the browser's back, the URL hash and a reload.

import { expect, test } from '@playwright/test';
import { FILES, IMAGES, connectEsp, expectImage, expectStep, setup, walk } from './helpers.ts';

test('browser back from Check returns to Connect with the connection kept; back from Connect drops it', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await walk(page, 'wireless', 'dial');
  await expect(page).toHaveURL(/#t=wireless&b=dial&s=connect$/);
  await connectEsp(page);
  await expect(page).toHaveURL(/s=check$/);
  await page.goBack();
  await expectStep(page, 'connect');
  await expect(page.locator('#conn-state')).toBeVisible();
  await page.locator('#next').click();
  await expectStep(page, 'check');
  await expectImage(page, FILES.dial, IMAGES.dial);
  await page.goBack();
  await expectStep(page, 'connect');
  await page.goBack();
  await expectStep(page, 'board');
  await page.locator('[data-board="dial"]').click();
  await expectStep(page, 'connect');
  await expect(page.locator('#conn-state')).toHaveCount(0);
  await expect(page.locator('#connect-esp')).toBeVisible();
});

test('BACK is the browser back: Connect to Board to Task; CANCEL from Check goes to Task', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await walk(page, 'wireless', 'dial');
  await page.locator('#back').click();
  await expectStep(page, 'board');
  await page.locator('#back').click();
  await expectStep(page, 'task');
  await expect(page).not.toHaveURL(/#/);
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await page.locator('#cancel').click();
  await expectStep(page, 'task');
  await expect(page.locator('#step-title')).toBeFocused();
});

test('the browser back during a write leaves the Write screen as it is', async ({ page }) => {
  await setup(page, { bootsel: true, rpDelayMs: 15 });
  await walk(page, 'iomcu', 'iomcu');
  await page.locator('#connect-pico').click();
  await expectStep(page, 'check');
  await expect(page.locator('#write')).toBeEnabled();
  await page.locator('#write').click();
  await expectStep(page, 'write');
  await page.evaluate(() => history.back());
  await page.waitForTimeout(200);
  await expectStep(page, 'write');
  await expect(page.locator('#prog-wrap')).toBeVisible();
  await expectStep(page, 'done');
});

test('the browser back from Done leads to Task', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } });
  await walk(page, 'panel');
  await connectEsp(page);
  await page.locator('#write').click();
  await expectStep(page, 'done');
  await page.goBack();
  await expectStep(page, 'task');
});

test('a pasted link to Write lands on Connect', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } }, { hash: 't=panel&s=write' });
  await expectStep(page, 'connect');
  await expect(page).toHaveURL(/#t=panel&b=panel&s=connect$/);
  await connectEsp(page);
  await expectImage(page, FILES.panel, IMAGES.panel);
});

test('a reload on Check lands on Connect of the same board', async ({ page }) => {
  await setup(page, { esp: { chipId: 9 } });
  await walk(page, 'wireless', 'dial');
  await connectEsp(page);
  await page.reload();
  await expectStep(page, 'connect');
  await expect(page).toHaveURL(/#t=wireless&b=dial&s=connect$/);
  await connectEsp(page);
  await expectImage(page, FILES.dial, IMAGES.dial);
});

test('an unknown hash opens Task', async ({ page }) => {
  await setup(page, { esp: { chipId: 18 } }, { hash: 't=nothing&s=check' });
  await expectStep(page, 'task');
});
