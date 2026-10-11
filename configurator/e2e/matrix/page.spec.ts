// Each setup in each cell of the matrix: the page shows the setup of the
// resolver, stays inside the viewport, logs nothing, loads nothing from
// another origin, does not animate, and prints the result without the chooser.

import { expect, test } from '@playwright/test';

import { CATALOGUE } from '../../src/core.ts';
import { BASE_FUNCTIONS, SEL, expectSetup } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';
import { SETUPS, cellOf, openCase } from './setups.ts';

for (const setup of SETUPS) {
  const { name, selection } = setup;
  test(`${name}: shows the setup, fits the viewport, stays quiet and on its origin`, async ({ page }, info) => {
    const cell = cellOf(info);
    const seen = watchPage(page);
    await openCase(page, setup);
    const shown = await expectSetup(page, selection);
    expect(shown.lang).toBe(cell.lang);
    // The page reports each id of the link it ignores, and lists what is in no release.
    expect(shown.notices.map((n) => n.kind)).toEqual(setup.ignored.length > 0 ? ['note'] : []);
    for (const id of setup.ignored) expect(shown.notices[0]?.text).toContain(id);
    await expect(page.locator('#choose .nir li')).toHaveCount(CATALOGUE.notInRelease.length);
    // The functions that need no part: a row each, without a control, and each among what the setup does.
    await expect(page.locator(SEL.baseRows)).toHaveCount(BASE_FUNCTIONS.length);
    await expect(page.locator(`${SEL.baseRows} :is(input, button, a, select, [tabindex])`)).toHaveCount(0);
    await expect(page.locator(SEL.doesBase)).toHaveCount(BASE_FUNCTIONS.length);
    for (const id of BASE_FUNCTIONS) expect(shown.hash.split(/[=,&]/)).toEqual(setup.hash?.split(/[=,&]/).includes(id) ? expect.arrayContaining([id]) : expect.not.arrayContaining([id]));
    await expect(page.locator(SEL.picture)).toBeVisible();
    await expect(page.locator(SEL.picture)).toHaveAccessibleName(/\S/);

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
    await openCase(page, setup);
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
    await openCase(page, setup);
    const shown = await expectSetup(page, selection);
    await page.emulateMedia({ media: 'print' });

    for (const hidden of ['#choose', SEL.bar, '.lang', SEL.copy, SEL.print, SEL.pdf, SEL.pdfStatus, SEL.reset]) {
      await expect(page.locator(hidden), hidden).toBeHidden();
    }
    // No control of the chooser is left on paper. The number of a wire in the table is a button that marks
    // the wire in the picture: it stays, as the number of its row.
    expect(await page.locator('input:visible, button:not(.wn):visible, select:visible, summary:visible').count()).toBe(0);
    await expect(page.locator(`${SEL.row} button.wn:visible`)).toHaveCount(shown.wires.length);
    await expect(page.locator('.printonly')).toBeVisible();
    await expect(page.locator('.printonly')).toContainText(shown.hash);
    // Both views of the wiring are on paper.
    await expect(page.locator(SEL.picture)).toBeVisible();
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
