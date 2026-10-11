// Each setup in each cell of the matrix: the PDF export gives a file by
// keyboard, and the page with the finished file stays inside the viewport,
// quiet, on its origin, reachable by keyboard and without a violation.

import { readFileSync } from 'node:fs';

import { expect, test } from '@playwright/test';

import type { Lang } from '../../contract.ts';
import { t } from '../../src/i18n.ts';
import { axeOf } from '../lib/axe.ts';
import { contrastOf, contrastProblems } from '../lib/contrast.ts';
import { keyboardWalk, walkProblems } from '../lib/keyboard.ts';
import { SEL, expectSetup } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';
import { SETUPS, cellOf, openCase } from './setups.ts';

const other = (lang: Lang): Lang => (lang === 'en' ? 'de' : 'en');

for (const setup of SETUPS) {
  const { name, selection } = setup;
  test(`${name}: the PDF export gives a file and leaves the page as it is`, async ({ page }, info) => {
    const { lang } = cellOf(info);
    const seen = watchPage(page);
    await openCase(page, setup);
    const before = await expectSetup(page, selection);

    const button = page.locator(SEL.pdf);
    await expect(button).toHaveAccessibleName(t(lang, 'pdf'));
    await expect(page.locator(SEL.pdfStatus)).toBeHidden();
    await button.focus();
    const waiting = page.waitForEvent('download');
    await page.keyboard.press('Enter');
    const download = await waiting;
    const file = download.suggestedFilename();
    expect(file).toMatch(new RegExp(`^rcbench-setup-[0-9a-f]{8}-${lang}\\.pdf$`));
    const bytes = readFileSync(await download.path());
    expect(bytes.subarray(0, 5).toString('latin1')).toBe('%PDF-');
    expect(bytes.subarray(-32).toString('latin1')).toContain('%%EOF');
    expect(bytes.length).toBeGreaterThan(10_000);

    // The status line names the file, and the button is ready for the next export.
    const link = page.locator(SEL.pdfLink);
    await expect(link).toBeVisible();
    await expect(link).toHaveAttribute('download', file);
    await expect(link).toHaveText(t(lang, 'pdfSave', { x: file }));
    await expect(page.locator(SEL.pdfStatus)).toHaveAttribute('role', 'status');
    await expect(button).not.toHaveAttribute('aria-disabled', 'true');
    await expect(button).toBeFocused();
    expect(await expectSetup(page, selection)).toEqual(before);

    await expectNoHorizontalScroll(page);
    expect((await axeOf(page)).violations).toEqual([]);
    expect(contrastProblems(await contrastOf(page))).toEqual([]);

    // Paper shows neither the button nor the status line.
    await page.emulateMedia({ media: 'print' });
    await expect(button).toBeHidden();
    await expect(page.locator(SEL.pdfStatus)).toBeHidden();
    await page.emulateMedia({ media: 'screen' });

    // The link to the file is a stop of the Tab key, after the button.
    const walk = await keyboardWalk(page);
    expect(walkProblems(walk)).toEqual([]);
    const stops = walk.stops.map((s) => s.what);
    expect(stops).toContain(`a${SEL.pdfLink}`);
    expect(stops.indexOf(`a${SEL.pdfLink}`)).toBeGreaterThan(stops.indexOf(`button${SEL.pdf}`));

    // The file belongs to one language: the other language shows no link to it.
    await page.locator(SEL.lang(other(lang))).click();
    await expect(page.locator('html')).toHaveAttribute('lang', other(lang));
    await expect(link).toHaveCount(0);
    await expect(page.locator(SEL.pdfStatus)).toBeHidden();
    await expectSetup(page, selection);
    expectQuiet(seen);
  });
}
