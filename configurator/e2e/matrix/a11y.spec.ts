// Each setup in each cell of the matrix: axe-core finds no violation of the
// Web Content Accessibility Guidelines (WCAG) 2.2 at level AA, and text, wires
// and control borders reach their contrast in the theme of the cell.

import { expect, test } from '@playwright/test';

import { axeOf } from '../lib/axe.ts';
import { contrastOf, contrastProblems } from '../lib/contrast.ts';
import { SEL } from '../lib/setup.ts';
import { SETUPS, cellOf, openCase } from './setups.ts';

for (const setup of SETUPS) {
  const { name } = setup;
  test(`${name}: axe-core reports no violation`, async ({ page }) => {
    await openCase(page, setup);
    await page.locator(SEL.ownSummary).click();
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    const report = await axeOf(page);
    expect(report.violations).toEqual([]);
    expect(report.passes).toBeGreaterThan(10);
  });

  test(`${name}: text, wires and borders reach their contrast`, async ({ page }, info) => {
    const cell = cellOf(info);
    await openCase(page, setup);
    await page.locator(SEL.ownSummary).click();
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    expect(await page.evaluate(() => matchMedia('(prefers-color-scheme: dark)').matches)).toBe(cell.theme === 'dark');
    const contrast = await contrastOf(page);
    expect(contrastProblems(contrast)).toEqual([]);
    expect(contrast.measured).toBeGreaterThan(100);
    for (const kind of ['text', 'svg-text', 'wire']) expect(contrast.pairs.some((p) => p.kind === kind), kind).toBe(true);
  });
}
