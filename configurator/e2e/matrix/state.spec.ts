// Each setup in each cell of the matrix: a change of language, a reload and
// the back and forward buttons keep the setup.

import { expect, test } from '@playwright/test';

import type { Lang } from '../../contract.ts';
import { SEL, expectSetup, hashOf, openSetup, selectionOf, stateOf } from '../lib/setup.ts';
import { expectQuiet, watchPage } from '../lib/watch.ts';
import { SETUPS, cellOf } from './setups.ts';

const other = (lang: Lang): Lang => (lang === 'en' ? 'de' : 'en');

for (const { name, selection } of SETUPS) {
  test(`${name}: the language switch keeps the setup`, async ({ page }, info) => {
    const { lang } = cellOf(info);
    const seen = watchPage(page);
    await openSetup(page, selection);
    const before = await expectSetup(page, selection);

    await page.locator(SEL.lang(other(lang))).click();
    await expect(page.locator('html')).toHaveAttribute('lang', other(lang));
    const switched = await expectSetup(page, selection);
    expect(stateOf(switched)).toEqual(stateOf(before));
    expect(switched.hash).toBe(hashOf(selection, other(lang)));
    expect(switched.summary).not.toBe(before.summary);

    await page.locator(SEL.lang(lang)).click();
    await expect(page.locator('html')).toHaveAttribute('lang', lang);
    expect(await expectSetup(page, selection)).toEqual(before);
    expectQuiet(seen);
  });

  test(`${name}: a reload keeps the setup`, async ({ page }, info) => {
    const { lang } = cellOf(info);
    const seen = watchPage(page);
    await openSetup(page, selection);
    const before = await expectSetup(page, selection);
    await page.reload();
    await page.locator(SEL.drawing).waitFor();
    expect(await expectSetup(page, selection)).toEqual(before);

    // A setup the visitor makes on the page survives the reload as well, with the other language.
    await page.locator(SEL.lang(other(lang))).click();
    const first = selection.functions[0] ?? 'servo';
    const toggled = selection.functions.includes(first)
      ? { ...selection, functions: selection.functions.filter((id) => id !== first) }
      : { ...selection, functions: [...selection.functions, first] };
    await page.locator(SEL.fn(first)).click();
    const made = await expectSetup(page, toggled);
    expect(made.lang).toBe(other(lang));
    await page.reload();
    await page.locator(SEL.drawing).waitFor();
    expect(await expectSetup(page, toggled)).toEqual(made);
    expectQuiet(seen);
  });

  test(`${name}: back and forward keep the setup`, async ({ page }) => {
    const seen = watchPage(page);
    const second = selectionOf({ functions: ['servo', 'esc'], options: { escPower: 'pd' } });
    await openSetup(page, selection);
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
