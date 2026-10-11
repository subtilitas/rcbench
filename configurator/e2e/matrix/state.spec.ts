// Each setup in each cell of the matrix: a change of language, a reload and
// the back and forward buttons keep the setup.

import { expect, test } from '@playwright/test';

import type { Lang } from '../../contract.ts';
import { CATALOGUE, deselectEffect } from '../../src/core.ts';
import { SEL, expectSetup, hashOf, selectionOf, stateOf } from '../lib/setup.ts';
import { expectQuiet, watchPage } from '../lib/watch.ts';
import { SETUPS, cellOf, openCase } from './setups.ts';

const other = (lang: Lang): Lang => (lang === 'en' ? 'de' : 'en');

for (const setup of SETUPS) {
  const { name, selection } = setup;
  test(`${name}: the language switch keeps the setup`, async ({ page }, info) => {
    const { lang } = cellOf(info);
    const seen = watchPage(page);
    await openCase(page, setup);
    const before = await expectSetup(page, selection);

    await page.locator(SEL.lang(other(lang))).click();
    await expect(page.locator('html')).toHaveAttribute('lang', other(lang));
    const switched = await expectSetup(page, selection);
    expect(stateOf(switched)).toEqual(stateOf(before));
    expect(switched.hash).toBe(hashOf(selection, other(lang)));
    expect(switched.summary).not.toBe(before.summary);

    // The page writes its own hash for the setup on the first change.
    await page.locator(SEL.lang(lang)).click();
    await expect(page.locator('html')).toHaveAttribute('lang', lang);
    expect(await expectSetup(page, selection)).toEqual({ ...before, hash: hashOf(selection) });
    expectQuiet(seen);
  });

  test(`${name}: a reload keeps the setup`, async ({ page }, info) => {
    const { lang } = cellOf(info);
    const seen = watchPage(page);
    await openCase(page, setup);
    const before = await expectSetup(page, selection);
    await page.reload();
    await page.locator(SEL.drawing).waitFor();
    expect(await expectSetup(page, selection)).toEqual(before);

    // A setup the visitor makes on the page survives the reload as well, with the other language.
    await page.locator(SEL.lang(other(lang))).click();
    const first = selection.functions[0] ?? 'servo';
    const off = selection.functions.includes(first);
    const toggled = off
      ? deselectEffect(CATALOGUE, selection, first).selection
      : { ...selection, functions: [...selection.functions, first] };
    await page.locator(SEL.fn(first)).click();
    const made = await expectSetup(page, toggled);
    expect(made.lang).toBe(other(lang));
    expect(made.hash).toBe(hashOf(toggled, other(lang)));
    // The page states what left with the function. The statement is not part of the link.
    expect(made.note !== '').toBe(off && deselectEffect(CATALOGUE, selection, first).changed);
    await page.reload();
    await page.locator(SEL.drawing).waitFor();
    expect(await expectSetup(page, toggled)).toEqual({ ...made, note: '' });
    expectQuiet(seen);
  });

  test(`${name}: back and forward keep the setup`, async ({ page }) => {
    const seen = watchPage(page);
    const second = selectionOf({ functions: ['servo', 'esc'], options: { escPower: 'pd' } });
    await openCase(page, setup);
    const first = await expectSetup(page, selection);

    // A link to another setup, followed on the open page.
    await page.evaluate((hash) => { location.hash = hash; }, hashOf(second));
    const next = await expectSetup(page, second);
    await page.goBack();
    expect(await expectSetup(page, selection)).toEqual(first);
    await page.goForward();
    expect(await expectSetup(page, second)).toEqual(next);

    // Away from the page and back to it.
    await page.goto('about:blank');
    await page.goBack();
    await page.locator(SEL.drawing).waitFor();
    expect(await expectSetup(page, second)).toEqual(next);
    await page.goBack();
    expect(await expectSetup(page, selection)).toEqual(first);
    expectQuiet(seen);
  });
}
