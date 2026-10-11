// Each setup in each cell of the matrix: axe-core finds no violation of the
// Web Content Accessibility Guidelines (WCAG) 2.2 at level AA, and text, wires
// and control borders reach their contrast in the theme of the cell.

import { AxeBuilder } from '@axe-core/playwright';
import { expect, test } from '@playwright/test';

import { contrastOf, contrastProblems } from '../lib/contrast.ts';
import { SEL, openSetup } from '../lib/setup.ts';
import { SETUPS, cellOf } from './setups.ts';

const TAGS = ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa'];

for (const { name, selection } of SETUPS) {
  test(`${name}: axe-core reports no violation`, async ({ page }) => {
    await openSetup(page, selection);
    await page.locator(SEL.ownSummary).click();
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    const report = await new AxeBuilder({ page }).withTags(TAGS).analyze();
    expect(report.violations.map((v) => `${v.id}: ${v.help} (${v.nodes.map((n) => n.target.join(' ')).join('; ')})`)).toEqual([]);
    expect(report.passes.length).toBeGreaterThan(10);
  });

  test(`${name}: text, wires and borders reach their contrast`, async ({ page }, info) => {
    const cell = cellOf(info);
    await openSetup(page, selection);
    await page.locator(SEL.ownSummary).click();
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    expect(await page.evaluate(() => matchMedia('(prefers-color-scheme: dark)').matches)).toBe(cell.theme === 'dark');
    const contrast = await contrastOf(page);
    expect(contrastProblems(contrast)).toEqual([]);
    expect(contrast.measured).toBeGreaterThan(100);
    for (const kind of ['text', 'svg-text', 'wire']) expect(contrast.pairs.some((p) => p.kind === kind), kind).toBe(true);
  });
}
