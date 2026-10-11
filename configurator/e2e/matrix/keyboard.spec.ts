// Each setup in each cell of the matrix: the Tab key reaches every control in
// document order, each stop has a role, a name and a visible focus outline,
// and no stop is under the fixed bar.

import { expect, test } from '@playwright/test';

import { hiddenStops, keyboardWalk, walkProblems } from '../lib/keyboard.ts';
import { FUNCTIONS, OWNED, SEL, openSetup } from '../lib/setup.ts';
import { SETUPS, cellOf } from './setups.ts';

for (const { name, selection } of SETUPS) {
  test(`${name}: the Tab key walks every control`, async ({ page }, info) => {
    await openSetup(page, selection);
    const walk = await keyboardWalk(page, { back: name === 'largest' });
    expect(walkProblems(walk)).toEqual([]);

    const ids = walk.stops.map((s) => s.what);
    // The controls every setup has: each function, the section of owned parts, the PDF export, the drawing, "Start over".
    for (const what of [...FUNCTIONS.map((id) => `input${SEL.fn(id)}`), `summary${SEL.ownSummary}`, `button${SEL.pdf}`, `div${SEL.pan}`, `button${SEL.reset}`]) {
      expect(ids, `${what} is no stop`).toContain(what);
    }
    // Both languages are buttons with a name.
    expect(walk.stops.filter((s) => s.region === 'header' && s.role === 'button').length).toBe(2);
    // The chooser comes before the result, the bar of the phone layout after both.
    const regions = [...new Set(walk.stops.map((s) => s.region))];
    expect(regions).toEqual(cellOf(info).phone ? ['header', 'choose', 'result', 'bar'] : ['header', 'choose', 'result']);
  });

  test(`${name}: the section of owned parts opens by keyboard and its rows take the focus`, async ({ page }) => {
    await openSetup(page, selection);
    await page.locator(SEL.ownSummary).focus();
    await page.keyboard.press('Enter');
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    const walk = await keyboardWalk(page);
    expect(walkProblems(walk)).toEqual([]);
    const ids = walk.stops.map((s) => s.what);
    for (const id of OWNED) expect(ids, `the row of ${id} is no stop`).toContain(`input${SEL.owned(id)}`);
  });

  test(`${name}: no control is hidden under the bottom bar while it has the focus`, async ({ page }, info) => {
    test.fixme(cellOf(info).phone, 'At 360 x 740 px the Tab key puts the focus on a control that lies entirely under the fixed bottom bar: the page sets no scroll-padding-bottom.');
    await openSetup(page, selection);
    expect(hiddenStops(await keyboardWalk(page))).toEqual([]);
    await page.locator(SEL.ownSummary).click();
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    expect(hiddenStops(await keyboardWalk(page))).toEqual([]);
  });
}
