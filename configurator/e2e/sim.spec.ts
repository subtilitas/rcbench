// The heat calculation on the card of the motor simulator, by keyboard alone:
// the 4 inputs change the figures, the warning and the hint, the link, the
// name of the part, its row of the parts list, its "have it" tick and both
// copies.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import type { Lang, SimSettings } from '../contract.ts';
import { DEFAULT_SIM } from '../src/core.ts';
import { t } from '../src/i18n.ts';
import { searchUrl } from '../src/view/buy.ts';
import { simText } from '../src/view/sim.ts';
import { watchErrors as watch } from './lib/watch.ts';

/** Presses Tab until the element with `id` has the focus. The page holds fewer than 400 stops before it. */
async function tabTo(page: Page, id: string, back = false): Promise<void> {
  for (let i = 0; i < 400; i++) {
    if (await page.evaluate(() => document.activeElement?.id ?? '') === id) return;
    await page.keyboard.press(back ? 'Shift+Tab' : 'Tab');
  }
  throw new Error(`Tab does not reach #${id}`);
}

/** The figures, the warning, the hint and the star line show the text of `sim`. */
async function expectFigures(page: Page, lang: Lang, sim: SimSettings): Promise<void> {
  const text = simText(lang, sim);
  await expect(page.locator('#sim-hot')).toHaveText(text.hot);
  await expect(page.locator('#sim-rest')).toHaveText(text.rest);
  await expect(page.locator('#sim-basis')).toHaveText(text.basis);
  await expect(page.locator('#sim-over')).toHaveCount(text.level?.kind === 'over' ? 1 : 0);
  await expect(page.locator('#sim-hint')).toHaveCount(text.level?.kind === 'hint' ? 1 : 0);
  if (text.level) await expect(page.locator(`#sim-${text.level.kind}`)).toHaveText(text.level.text);
  await expect(page.locator('#sim-star')).toHaveCount(text.star ? 1 : 0);
  for (const key of ['cells', 'ohms', 'watts', 'wiring'] as const) await expect(page.locator(`#sim-${key}`)).toHaveValue(String(sim[key]));
}

const ORDER = { en: 'Resistor', de: 'Widerstand' } as const;

for (const lang of ['en', 'de'] as const) {
  for (const size of [{ width: 360, height: 800 }, { width: 1280, height: 800 }]) {
    test.describe(`${lang}, ${size.width} px`, () => {
      test.use({ viewport: size, locale: lang === 'de' ? 'de-DE' : 'en-GB' });

      test('the inputs change the figures, the warning, the link, the name, the parts list and both copies', async ({ page, context }) => {
        await context.grantPermissions(['clipboard-read', 'clipboard-write']);
        const seen = watch(page);
        await page.goto('about:blank');
        await page.goto('./#v=1&f=tap');
        await page.locator('#sim').waitFor();
        await expectFigures(page, lang, DEFAULT_SIM);
        await expect(page.locator('#buy > li[data-part="sim"]')).toContainText(`3 × ${ORDER[lang]} 470 Ω, 2 W`);

        // Cells: 4S to 6S. 470 Ω, 2 W in delta: 1.35 W, the hint.
        await page.locator('body').focus();
        await tabTo(page, 'sim-cells');
        await page.keyboard.press('ArrowDown');
        await page.keyboard.press('ArrowDown');
        let sim: SimSettings = { ...DEFAULT_SIM, cells: 6 };
        await expectFigures(page, lang, sim);
        await expect(page.locator('#sim-cells')).toBeFocused();
        await expect(page.locator('#sim-hint')).toBeVisible();
        await expect(page.locator('#live')).toContainText(simText(lang, sim).hot);

        // Rating: 2 W to 1 W: above the rating.
        await tabTo(page, 'sim-watts');
        await page.keyboard.press('ArrowUp');
        sim = { ...sim, watts: 1 };
        await expectFigures(page, lang, sim);
        await expect(page.locator('#sim-over')).toBeVisible();
        await expect(page.locator('#live')).toContainText(simText(lang, sim).level!.text);

        // Resistance: 470 Ω to 560 Ω, back with Shift+Tab.
        await tabTo(page, 'sim-ohms', true);
        await page.keyboard.press('ArrowDown');
        sim = { ...sim, ohms: 560 };
        await expectFigures(page, lang, sim);

        // Wiring: delta to star: 0.284 W, no warning, no hint.
        await tabTo(page, 'sim-wiring');
        await page.keyboard.press('ArrowDown');
        sim = { ...sim, wiring: 'star' };
        await expectFigures(page, lang, sim);
        await expect(page.locator('#sim-over')).toHaveCount(0);
        await expect(page.locator('#sim-hint')).toHaveCount(0);

        // The link, the name of the part and its row follow.
        await expect.poll(() => page.evaluate(() => location.hash)).toBe('#v=1&f=tap&m=6s,560r,1w,star');
        const name = lang === 'de' ? 'Motorsimulator, selbst gebaut: 3 Widerstände im Stern, 560 Ω, 1 W' : 'Motor simulator, self-built: 3 resistors in star, 560 Ω, 1 W';
        await expect(page.locator('ul.parts b', { hasText: name })).toHaveCount(1);
        const row = `3 × ${ORDER[lang]} 560 Ω, 1 W`;
        await expect(page.locator('#buy > li[data-part="sim"]')).toContainText(row);
        await expect(page.locator('#buy > li[data-part="sim"] a.websearch')).toHaveAttribute('href', searchUrl(null, `${ORDER[lang]} 560 Ω, 1 W`));
        await expect(page.locator('#buy')).not.toContainText('470');
        await expect(page.locator('ul.parts b', { hasText: '470' })).toHaveCount(0);

        // Both copies, by keyboard.
        await tabTo(page, 'b-copy-text');
        await page.keyboard.press('Enter');
        await expect(page.locator('#b-copy-text')).toHaveText(t(lang, 'copiedText'));
        const text = await page.evaluate(() => navigator.clipboard.readText());
        expect(text).toContain(`${row}\n`);
        expect(text).toContain('&m=6s,560r,1w,star');
        expect(text).not.toContain('470');
        await tabTo(page, 'b-copy-csv');
        await page.keyboard.press('Enter');
        await expect(page.locator('#b-copy-csv')).toHaveText(t(lang, 'copiedCsv'));
        const csv = await page.evaluate(() => navigator.clipboard.readText());
        expect(csv).toContain(`"3","","${ORDER[lang]} 560 Ω, 1 W",`);
        expect(csv).not.toContain('470');

        // A reload opens the same values from the link.
        await page.reload();
        await page.locator('#sim').waitFor();
        await expectFigures(page, lang, sim);
        await expect.poll(() => page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
        expect(seen).toEqual({ foreign: [], errors: [] });
      });

      test('a link with a value the page does not offer gets the notice and the default value', async ({ page }) => {
        const seen = watch(page);
        await page.goto('about:blank');
        await page.goto('./#v=1&p=sim&m=7s,1k,star');
        await page.locator('#sim').waitFor();
        await expect(page.locator('.msg.note[role="alert"]')).toHaveText(t(lang, 'badId', { x: '7s' }));
        await expectFigures(page, lang, { ...DEFAULT_SIM, ohms: 1000, wiring: 'star' });
        // An old link without the key: the default values, no notice.
        await page.goto('./#v=1&p=sim&o=pd');
        await expect(page.locator('#sim-hot')).toHaveText(simText(lang, DEFAULT_SIM).hot);
        await expect(page.locator('.msg[role="alert"]')).toHaveCount(0);
        await expectFigures(page, lang, DEFAULT_SIM);
        expect(seen).toEqual({ foreign: [], errors: [] });
      });

      test('a "have it" tick of the resistors stays with a new pack or wiring and leaves with a new resistance', async ({ page }) => {
        const seen = watch(page);
        await page.goto('about:blank');
        await page.goto('./#v=1&f=tap');
        await page.locator('#sim').waitFor();
        await page.locator('body').focus();
        await tabTo(page, 'have-sim');
        await page.keyboard.press('Space');
        await expect(page.locator('#have [data-h="sim"]')).toBeChecked();
        await expect(page.locator('#buy [data-h="sim"]')).toHaveCount(0);
        await expect.poll(() => page.evaluate(() => location.hash)).toBe('#v=1&f=tap&h=sim');
        // Wiring: the row stays "Resistor 470 Ω, 2 W", the tick stays.
        await tabTo(page, 'sim-wiring', true);
        await page.keyboard.press('ArrowDown');
        await expect.poll(() => page.evaluate(() => location.hash)).toBe('#v=1&f=tap&h=sim&m=star');
        await expect(page.locator('#have [data-h="sim"]')).toBeChecked();
        // Resistance: the row names other resistors, the tick leaves and the row is back under "Parts to buy".
        await tabTo(page, 'sim-ohms', true);
        await page.keyboard.press('ArrowDown');
        await expect.poll(() => page.evaluate(() => location.hash)).toBe('#v=1&f=tap&m=560r,star');
        await expect(page.locator('#have [data-h="sim"]')).toHaveCount(0);
        await expect(page.locator('#buy [data-h="sim"]')).not.toBeChecked();
        await expect(page.locator('#buy > li[data-part="sim"]')).toContainText(`3 × ${ORDER[lang]} 560 Ω, 2 W`);
        await expect(page.locator('#sim-ohms')).toBeFocused();
        expect(seen).toEqual({ foreign: [], errors: [] });
      });

      test('Start over brings back the default values', async ({ page }) => {
        await page.goto('about:blank');
        await page.goto('./#v=1&p=sim&m=2s,0w5');
        await page.locator('#sim').waitFor();
        await page.locator('body').focus();
        await tabTo(page, 'b-reset');
        await page.keyboard.press('Enter');
        await expect.poll(() => page.evaluate(() => location.hash)).toBe('#v=1');
        await expect(page.locator('#sim')).toHaveCount(0);
        await page.goto('./#v=1&p=sim');
        await expectFigures(page, lang, DEFAULT_SIM);
      });
    });
  }
}
