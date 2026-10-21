// The parts list of the result: its rows, owned parts, variants, the "have
// it" ticks, the copy buttons and the links that leave the page only on a
// click.

import { expect, test } from '@playwright/test';
import type { BrowserContext, Page } from '@playwright/test';
import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { t } from '../src/i18n.ts';
import { optionsOf } from '../src/names.ts';
import { listCsv, listText, partsList, searchUrl } from '../src/view/buy.ts';
import type { View } from '../src/view/chooser.ts';
import { watchErrors as watch } from './lib/watch.ts';

const SIZES = [{ width: 360, height: 800 }, { width: 1280, height: 800 }];

function view(selection: Partial<Selection>, lang: Lang = 'en', have: string[] = []): View {
  const full: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS }, ...selection };
  const setup = resolve(CATALOGUE, full);
  return { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection: full, have };
}

const hashOf = (page: Page): Promise<string> => page.evaluate(() => location.hash);

async function open(page: Page, hash: string): Promise<void> {
  await page.goto('about:blank');
  await page.goto(`./${hash}`);
  await page.locator('#h-buy').waitFor();
}

/** Counts every request to a Google host of the context, and answers it with a plain page. */
async function countGoogle(context: BrowserContext): Promise<string[]> {
  const seen: string[] = [];
  await context.route(/^https?:\/\/([a-z0-9-]+\.)*google\.[a-z.]+\//, async (route) => {
    seen.push(route.request().url());
    await route.fulfill({ status: 200, contentType: 'text/plain', body: 'test' });
  });
  return seen;
}

const deny = (page: Page): Promise<void> => page.evaluate(() => {
  Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText: () => Promise.reject(new Error('denied')) } });
});

for (const size of SIZES) {
  test.describe(`${size.width} px`, () => {
    test.use({ viewport: size, locale: 'en-GB' });

    test('the list shows the rows of the setup and the parts the visitor brings, and loads nothing from Google', async ({ page, context }) => {
      const google = await countGoogle(context);
      const seen = watch(page);
      await open(page, '#v=1&f=servo,escpow');
      const list = partsList(view({ functions: ['servo', 'escpow'] }));
      await expect(page.locator('#buy > li')).toHaveCount(list.buy.length);
      await expect(page.locator('#bring > li')).toHaveCount(list.bring.length);
      await expect(page.locator('#buy-note')).toHaveText(`Parts list as of release v${CATALOGUE.release}. Makers change products without notice. Check every part against its check line before ordering. No warranty.`);
      await expect(page.locator('#buy > li[data-part="panel"]')).toContainText('1 × Waveshare ESP32-P4-WIFI6-Touch-LCD-7B');
      await expect(page.locator('#bring > li[data-part="servo"]')).toBeVisible();
      await expect(page.locator('#bring a')).toHaveCount(0);
      // Every link of the list opens a new tab without an opener.
      const links = page.locator('#buy a');
      expect(await links.count()).toBe(list.buy.length + list.buy.filter((l) => l.page !== null).length);
      for (const a of await links.all()) {
        await expect(a).toHaveAttribute('target', '_blank');
        await expect(a).toHaveAttribute('rel', 'noopener noreferrer');
      }
      // The search link of a spec without a maker.
      await expect(page.locator('#buy > li[data-part="pull"] a.websearch')).toHaveAttribute('href', searchUrl(null, 'Resistor 2.2 kΩ, 0.25 W'));
      await expect(page.locator('#buy > li[data-part="pull"] a.websearch')).toHaveAttribute('href', 'https://www.google.com/search?q=%22Resistor%202.2%20k%CE%A9%22%20%220.25%20W%22');
      await expect(page.locator('#buy > li[data-part="panel"] a.websearch')).toHaveAttribute('href', 'https://www.google.com/search?q=%22Waveshare%22%20%22ESP32-P4-WIFI6-Touch-LCD-7B%22');
      // The links start at the left edge of their row, the search after the maker page.
      const row = page.locator('#buy > li[data-part="panel"]');
      const [li, maker, search] = await Promise.all([row.boundingBox(), row.locator('a.maker').boundingBox(), row.locator('a.websearch').boundingBox()]);
      expect(Math.abs(maker!.x - li!.x)).toBeLessThanOrEqual(1);
      expect(search!.x).toBeGreaterThan(maker!.x + maker!.width);
      expect(search!.x - (maker!.x + maker!.width)).toBeLessThanOrEqual(24);
      await page.locator('#buy').scrollIntoViewIfNeeded();
      await page.locator('#b-copy-csv').hover();
      await page.waitForLoadState('networkidle');
      expect(google).toEqual([]);
      expect(seen).toEqual({ foreign: [], errors: [] });
      await expect.poll(() => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    });

    test('a search link opens Google in a new tab only on a click, without an opener', async ({ page, context }) => {
      const google = await countGoogle(context);
      await open(page, '#v=1');
      const link = page.locator('#buy > li[data-part="panel"] a.websearch');
      const href = await link.getAttribute('href');
      expect(google).toEqual([]);
      const [popup] = await Promise.all([context.waitForEvent('page'), link.click()]);
      await popup.waitForLoadState();
      expect(popup.url()).toBe(href);
      expect(google).toEqual([href]);
      expect(await popup.evaluate(() => window.opener)).toBeNull();
      await popup.close();
    });

    test('a part ticked under "Parts you own" leaves the list', async ({ page }) => {
      await open(page, '#v=1&f=escpow');
      await expect(page.locator('#buy > li[data-part="ina228"]')).toHaveCount(1);
      // A tick of its own under "Parts you own", given before the function asks for the part.
      await open(page, '#v=1');
      await page.locator('#own-summary').click();
      await expect(page.locator('#own')).toHaveAttribute('open', '');
      await page.locator('#p-ina228').check();
      await expect(page.locator('#buy > li[data-part="ina228"]')).toHaveCount(0);
      await page.locator('#f-escpow').check();
      await expect(page.locator('#buy > li[data-part="ina228"]')).toHaveCount(0);
      await expect.poll(() => page.evaluate(() => location.hash)).toContain('p=ina228');
      await open(page, '#v=1&f=escpow&p=ina228');
      await expect(page.locator('#buy > li[data-part="ina228"]')).toHaveCount(0);
      await expect(page.locator('#buy > li[data-part="panel"]')).toHaveCount(1);
    });

    test('a variant switch changes the row', async ({ page }) => {
      await open(page, '#v=1&f=escpow');
      const row = page.locator('#buy > li[data-part="ina228"]');
      await expect(row).toContainText('Matek I2C-INA-BM');
      await page.locator('#o-ina228Board-xt90').check();
      await expect(row).toContainText('INA228 module, XT90');
      await expect(row).not.toContainText('Matek');
      await expect(row.locator('a.maker')).toHaveCount(0);
      await expect(page.locator('#buy > li[data-part="copro"]')).toHaveCount(1);
      await page.locator('#o-connection-osoyoo').check();
      await expect(page.locator('#buy > li[data-part="copro"]')).toHaveCount(2);
      await expect(page.locator('#buy > li[data-part="copro"]').nth(1)).toContainText('Osoyoo Breakout Board for Pico series');
    });

    test('Copy as text and Copy as CSV write the list to the clipboard', async ({ page, context }) => {
      await context.grantPermissions(['clipboard-read', 'clipboard-write']);
      await open(page, '#v=1&f=servo,escpow&o=xt90');
      const v = view({ functions: ['servo', 'escpow'], options: { ...DEFAULT_OPTIONS, ina228Board: 'xt90' } });
      await page.locator('#b-copy-text').click();
      await expect(page.locator('#live')).toHaveText(t('en', 'copiedText'));
      const text = await page.evaluate(() => navigator.clipboard.readText());
      expect(text).toBe(listText(v, page.url()));
      expect(text.split('\n')[1]).toBe(page.url());
      await page.locator('#b-copy-csv').click();
      await expect(page.locator('#live')).toHaveText(t('en', 'copiedCsv'));
      const csv = await page.evaluate(() => navigator.clipboard.readText());
      expect(csv).toBe(listCsv(v));
      expect(csv.split('\r\n')[0]).toBe('"Quantity","Maker","Ordering name","Check","Maker page","Search link"');
    });

    test('a refused clipboard shows the list in a selectable field', async ({ page }) => {
      await open(page, '#v=1&f=servo&l=de');
      const v = view({ functions: ['servo'] }, 'de');
      await deny(page);
      await page.locator('#b-copy-csv').click();
      await expect(page.locator('#buyfield')).toBeVisible();
      await expect(page.locator('#buyfield')).toBeFocused();
      expect(await page.locator('#buyfield').inputValue()).toBe(listCsv(v).replace(/\r\n/g, '\n'));
      await page.locator('#b-copy-text').click();
      expect(await page.locator('#buyfield').inputValue()).toBe(listText(v, page.url()));
      await expect.poll(() => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    });

    test('a "have it" tick moves a row to "You have", the link keeps it, a reload keeps it, the untick returns it', async ({ page, context }) => {
      const google = await countGoogle(context);
      const seen = watch(page);
      await open(page, '#v=1&f=escpow');
      const buyRow = page.locator('#buy > li[data-part="wire"]');
      await expect(buyRow).toHaveCount(3);
      const tick = page.locator('#buy [data-h="wire.dmm"]');
      await expect(tick).toHaveAttribute('type', 'checkbox');
      await expect(tick).toHaveAccessibleName('Have it: Digital multimeter');
      await expect(page.locator('#have')).toHaveCount(0);
      // The control moves to the other list: a click, not check(), which reads the moved control under the old locator.
      await tick.click();
      await expect(buyRow).toHaveCount(2);
      await expect(page.locator('#h-have')).toHaveText('You have');
      await expect(page.locator('#have > li')).toHaveCount(1);
      await expect(page.locator('#have > li')).toContainText('1 × Digital multimeter');
      await expect(page.locator('#have [data-h="wire.dmm"]')).toBeChecked();
      await expect(page.locator('#have a')).toHaveCount(0);
      await expect(page.locator('#live')).toContainText('Moved to "You have".');
      await expect.poll(() => hashOf(page)).toBe('#v=1&f=escpow&h=wire.dmm');
      // A second tick: the link names both in catalogue order.
      await page.locator('#buy [data-h="panel.p47b"]').click();
      await expect.poll(() => hashOf(page)).toBe('#v=1&f=escpow&h=panel.p47b,wire.dmm');
      await expect(page.locator('#have > li')).toHaveCount(2);
      await page.reload();
      await page.locator('#h-buy').waitFor();
      await expect(page.locator('#have > li')).toHaveCount(2);
      await expect(page.locator('#buy [data-h="wire.dmm"]')).toHaveCount(0);
      await expect(page.locator('.msg')).toHaveCount(0);
      // The untick returns the row to the rows to buy, in its place.
      await page.locator('#have [data-h="wire.dmm"]').click();
      await expect(buyRow).toHaveCount(3);
      await expect(buyRow.nth(2)).toContainText('Digital multimeter');
      await expect(page.locator('#buy [data-h="wire.dmm"]')).not.toBeChecked();
      await expect(page.locator('#live')).toContainText('Back under "Parts to buy".');
      await expect.poll(() => hashOf(page)).toBe('#v=1&f=escpow&h=panel.p47b');
      await page.locator('#have [data-h="panel.p47b"]').click();
      await expect(page.locator('#have')).toHaveCount(0);
      await expect.poll(() => hashOf(page)).toBe('#v=1&f=escpow');
      await page.waitForLoadState('networkidle');
      expect(google).toEqual([]);
      expect(seen).toEqual({ foreign: [], errors: [] });
      await expect.poll(() => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    });

    test('the "have it" tick works with the keyboard alone', async ({ page }) => {
      await open(page, '#v=1');
      // The tick follows the links of its row in the tab order.
      await page.locator('#buy > li[data-part="panel"] a.websearch').focus();
      await page.keyboard.press('Tab');
      await expect(page.locator('#buy [data-h="panel.p47b"]')).toBeFocused();
      await page.keyboard.press('Space');
      // The focus moves with the tick to "You have".
      await expect(page.locator('#have [data-h="panel.p47b"]')).toBeFocused();
      await expect(page.locator('#have [data-h="panel.p47b"]')).toBeChecked();
      await expect.poll(() => hashOf(page)).toBe('#v=1&h=panel.p47b');
      await page.keyboard.press('Space');
      await expect(page.locator('#buy [data-h="panel.p47b"]')).toBeFocused();
      await expect(page.locator('#have')).toHaveCount(0);
      await expect.poll(() => hashOf(page)).toBe('#v=1');
    });

    test('a variant that gives the part another row takes its "have it" tick out of the link', async ({ page }) => {
      await open(page, '#v=1&f=escpow&h=ina228.matek');
      await expect(page.locator('#have [data-h="ina228.matek"]')).toBeChecked();
      await page.locator('#o-ina228Board-xt90').check();
      await expect(page.locator('#have')).toHaveCount(0);
      await expect(page.locator('#buy [data-h="ina228.xt90"]')).not.toBeChecked();
      await expect.poll(() => hashOf(page)).toBe('#v=1&f=escpow&o=xt90');
      // A link with a stale and a malformed tick opens with the note that names both; the link of the print header leaves both out.
      await open(page, '#v=1&f=escpow&o=xt90&h=ina228.matek,Panel,panel.p47b');
      await expect(page.locator('.msg.note')).toContainText('Panel, ina228.matek');
      await expect(page.locator('#have > li')).toHaveCount(1);
      await expect(page.locator('#have [data-h="panel.p47b"]')).toBeChecked();
      await expect(page.locator('p.printonly')).toContainText(/#v=1&f=escpow&o=xt90&h=panel.p47b$/);
    });

    test('Copy as text, Copy as CSV and Copy link follow the "have it" ticks', async ({ page, context }) => {
      await context.grantPermissions(['clipboard-read', 'clipboard-write']);
      await open(page, '#v=1&f=servo');
      await page.locator('#buy [data-h="wire.dmm"]').click();
      await page.locator('#buy [data-h="usb.cable"]').click();
      await expect.poll(() => hashOf(page)).toBe('#v=1&f=servo&h=usb.cable,wire.dmm');
      const v = view({ functions: ['servo'] }, 'en', ['usb.cable', 'wire.dmm']);
      await page.locator('#b-copy-text').click();
      await expect(page.locator('#live')).toHaveText(t('en', 'copiedText'));
      const text = await page.evaluate(() => navigator.clipboard.readText());
      expect(text).toBe(listText(v, page.url()));
      expect(text.split('\n')[1]).toMatch(/#v=1&f=servo&h=usb\.cable,wire\.dmm$/);
      expect(text).toContain('\nYou have\n2 × USB-C data cable\n1 × Digital multimeter\n');
      await page.locator('#b-copy-csv').click();
      await expect(page.locator('#live')).toHaveText(t('en', 'copiedCsv'));
      const csv = await page.evaluate(() => navigator.clipboard.readText());
      expect(csv).toBe(listCsv(v));
      expect(csv).not.toContain('Digital multimeter');
      expect(csv).not.toContain('USB-C data cable');
      expect(csv.split('\r\n').length).toBe(partsList(v).buy.length + 2);
      await page.locator('#b-copy').click();
      expect(await page.evaluate(() => navigator.clipboard.readText())).toMatch(/#v=1&f=servo&h=usb\.cable,wire\.dmm$/);
    });
  });
}
