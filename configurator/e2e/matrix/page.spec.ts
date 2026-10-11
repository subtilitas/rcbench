// Each setup in each cell of the matrix: the page shows the setup of the
// resolver, stays inside the viewport, logs nothing, loads nothing from
// another origin, does not animate, and prints the result without the chooser.

import { expect, test } from '@playwright/test';

import { SEL, expectSetup, openSetup } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';
import { SETUPS, cellOf } from './setups.ts';

for (const { name, selection } of SETUPS) {
  test(`${name}: shows the setup, fits the viewport, stays quiet and on its origin`, async ({ page }, info) => {
    const cell = cellOf(info);
    const seen = watchPage(page);
    await openSetup(page, selection);
    const shown = await expectSetup(page, selection);
    expect(shown.lang).toBe(cell.lang);
    await expect(page.locator(SEL.drawing)).toHaveAttribute('aria-label', /\S/);

    await expectNoHorizontalScroll(page);
    // Open sections and the end of the page are inside the viewport as well.
    await page.locator(SEL.ownSummary).click();
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    await expectNoHorizontalScroll(page);
    await page.evaluate(() => window.scrollTo(0, document.documentElement.scrollHeight));
    await expectNoHorizontalScroll(page);

    // Every picture of the page loads from the origin of the page.
    for (const img of await page.locator('img').all()) await img.scrollIntoViewIfNeeded();
    await expect.poll(() => page.evaluate(() => [...document.images].filter((i) => !i.complete || i.naturalWidth === 0).map((i) => i.src))).toEqual([]);

    expect(seen.local).toBeGreaterThan(0);
    expectQuiet(seen);
  });

  test(`${name}: reduced motion leaves nothing that moves`, async ({ page }, info) => {
    await openSetup(page, selection);
    const motion = await page.evaluate(() => ({
      asked: matchMedia('(prefers-reduced-motion: reduce)').matches,
      scroll: getComputedStyle(document.documentElement).scrollBehavior,
      running: document.getAnimations().length,
      timed: [...document.querySelectorAll('*')].filter((el) => {
        const style = getComputedStyle(el);
        const seconds = (list: string): number => Math.max(...list.split(',').map((s) => Number.parseFloat(s) * (s.trim().endsWith('ms') ? 0.001 : 1)));
        return seconds(style.transitionDuration) > 0 || (style.animationName !== 'none' && seconds(style.animationDuration) > 0);
      }).map((el) => el.tagName.toLowerCase() + (el.id ? `#${el.id}` : '')),
    }));
    expect(motion).toEqual({ asked: true, scroll: 'auto', running: 0, timed: [] });

    // A jump of the bottom bar lands at once.
    if (cellOf(info).phone) {
      const jump = page.locator(`${SEL.bar} a[data-jump]`).last();
      const target = await jump.getAttribute('data-jump');
      await jump.click();
      const top = await page.evaluate((id) => document.getElementById(id ?? '')?.getBoundingClientRect().top ?? -1, target);
      const again = await page.evaluate((id) => document.getElementById(id ?? '')?.getBoundingClientRect().top ?? -2, target);
      expect(again).toBe(top);
      expect(top).toBeLessThan(page.viewportSize()?.height ?? 0);
    }
  });

  test(`${name}: print shows the result without the chooser`, async ({ page }) => {
    const seen = watchPage(page);
    await openSetup(page, selection);
    const shown = await expectSetup(page, selection);
    await page.emulateMedia({ media: 'print' });

    for (const hidden of ['#choose', SEL.bar, '.lang', SEL.copy, SEL.print, SEL.pdf, SEL.pdfStatus, SEL.reset]) {
      await expect(page.locator(hidden), hidden).toBeHidden();
    }
    // No control of the chooser is left on paper.
    expect(await page.locator('input:visible, button:visible, select:visible, summary:visible').count()).toBe(0);
    await expect(page.locator('.printonly')).toBeVisible();
    await expect(page.locator('.printonly')).toContainText(shown.hash);
    await expect(page.locator(SEL.drawing)).toBeVisible();
    await expect(page.locator(SEL.row)).toHaveCount(shown.wires.length);
    if (shown.wires.length > 0) await expect(page.locator(SEL.row).first()).toBeVisible();
    await expect(page.locator(SEL.part).first()).toBeVisible();
    // Print uses the light colours in both themes.
    const ink = await page.evaluate(() => getComputedStyle(document.body).color);
    const light = await page.evaluate(() => {
      const [r, g, b] = (getComputedStyle(document.body).color.match(/[\d.]+/g) ?? []).map(Number) as [number, number, number];
      return r + g + b < 3 * 96;
    });
    expect(light, `the text on paper is ${ink}`).toBe(true);
    expectQuiet(seen);
  });
}
