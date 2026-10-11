// Each setup in each cell of the matrix: the Tab key reaches every control in
// document order, each stop has a role, a name and a visible focus outline,
// and no stop is under the fixed bar.

import { expect, test } from '@playwright/test';

import { hiddenStops, keyboardWalk, walkProblems } from '../lib/keyboard.ts';
import { FUNCTIONS, OWNED, SEL } from '../lib/setup.ts';
import { SETUPS, cellOf, openCase } from './setups.ts';

for (const setup of SETUPS) {
  const { name } = setup;
  test(`${name}: the Tab key walks every control`, async ({ page }, info) => {
    await openCase(page, setup);
    const walk = await keyboardWalk(page, { back: name === 'largest' });
    expect(walkProblems(walk)).toEqual([]);

    const ids = walk.stops.map((s) => s.what);
    // The controls every setup has: each function, the section of owned parts, the PDF export, the switch of the 2 views,
    // the box of the wiring picture, "Start over". The schematic is hidden and is no stop: `views.spec.ts` walks it.
    for (const what of [...FUNCTIONS.map((id) => `input${SEL.fn(id)}`), `summary${SEL.ownSummary}`, `button${SEL.pdf}`, `button${SEL.view('pic')}`, `button${SEL.view('sch')}`, `div${SEL.dia}`, `button${SEL.reset}`]) {
      expect(ids, `${what} is no stop`).toContain(what);
    }
    expect(ids).not.toContain(`div${SEL.pan}`);
    // The wires of the picture are 1 stop: the arrow keys move between them.
    expect(ids.filter((what) => what === 'g').length).toBe(await page.locator(SEL.pictureWire).count() > 0 ? 1 : 0);
    // Both languages are buttons with a name.
    expect(walk.stops.filter((s) => s.region === 'header' && s.role === 'button').length).toBe(2);
    // The chooser comes before the result, the footer with the link to the trademark notice ends the result,
    // the bar of the phone layout comes last.
    const regions = [...new Set(walk.stops.map((s) => s.region))];
    expect(regions).toEqual(cellOf(info).phone ? ['header', 'choose', 'result', 'footer', 'bar'] : ['header', 'choose', 'result', 'footer']);
    expect(ids, 'the link to the trademark notice is no stop').toContain(`a${SEL.noticeLink}`);
  });

  test(`${name}: the section of owned parts opens by keyboard and its rows take the focus`, async ({ page }) => {
    await openCase(page, setup);
    await page.locator(SEL.ownSummary).focus();
    await page.keyboard.press('Enter');
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    const walk = await keyboardWalk(page);
    expect(walkProblems(walk)).toEqual([]);
    const ids = walk.stops.map((s) => s.what);
    for (const id of OWNED) expect(ids, `the row of ${id} is no stop`).toContain(`input${SEL.owned(id)}`);
  });

  test(`${name}: no control is hidden under the bottom bar while it has the focus`, async ({ page }) => {
    await openCase(page, setup);
    expect(hiddenStops(await keyboardWalk(page))).toEqual([]);
    await page.locator(SEL.ownSummary).click();
    await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
    expect(hiddenStops(await keyboardWalk(page))).toEqual([]);
  });
}

// A large text size wraps the labels of the bar and the bar grows. The free space follows its rendered height.
test('largest: no control is hidden under a bottom bar of 3 times its height', async ({ page }, info) => {
  const setup = SETUPS.find((s) => s.name === 'largest');
  if (!setup) throw new Error('no setup "largest"');
  await openCase(page, setup);
  await page.addStyleTag({ content: '.bar a { min-height: 168px; }' });
  const { phone } = cellOf(info);
  if (phone) {
    await expect.poll(() => page.evaluate(() => getComputedStyle(document.documentElement).scrollPaddingBottom)).toBe('185px');
    expect(await page.evaluate(() => getComputedStyle(document.body).paddingBottom)).toBe('185px');
  } else {
    expect(await page.evaluate(() => getComputedStyle(document.documentElement).scrollPaddingBottom)).toBe('0px');
  }
  const walk = await keyboardWalk(page);
  expect(hiddenStops(walk)).toEqual([]);
  if (phone) {
    // The link to the trademark notice ends the document: it lies above the bar while it has the focus.
    await page.locator(SEL.noticeLink).focus();
    const gap = await page.evaluate((sel) => {
      const link = document.querySelector(sel.link)?.getBoundingClientRect();
      const bar = document.querySelector(sel.bar)?.getBoundingClientRect();
      return link && bar ? bar.top - link.bottom : -1;
    }, { link: SEL.noticeLink, bar: SEL.bar });
    expect(gap).toBeGreaterThanOrEqual(0);
  }
});
