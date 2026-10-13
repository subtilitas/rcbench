// The help bubbles of the ticks in the chooser under `vite preview`: the
// keyboard focus of a tick, the pointer over its mark, a tap on the mark,
// Escape, the German text and the width of a phone.

import { expect, test } from '@playwright/test';
import type { Locator, Page } from '@playwright/test';
import { CATALOGUE, FUNCTION_HELP, alwaysOn } from '../src/core.ts';
import { t } from '../src/i18n.ts';
import { watchErrors as watch } from './lib/watch.ts';

const FUNCTIONS = CATALOGUE.functions.filter((f) => !alwaysOn(CATALOGUE, f)).map((f) => f.id);
const OWNED = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const role = (id: string, lang: 'en' | 'de'): string => CATALOGUE.parts.find((p) => p.id === id)!.role![lang];

async function open(page: Page, hash: string): Promise<void> {
  await page.goto('about:blank');
  await page.goto(`./${hash}`);
  await page.locator('svg.drawing').waitFor({ state: 'attached' });
}

const focused = (page: Page): Promise<string> => page.evaluate(() => document.activeElement?.id ?? '');

/** Moves the focus to the control `id` with the Tab key alone, from the top of the page. */
async function tabTo(page: Page, id: string): Promise<void> {
  await page.evaluate(() => {
    (document.activeElement as HTMLElement | null)?.blur();
    window.scrollTo(0, 0);
  });
  for (let i = 0; i < 400; i++) {
    await page.keyboard.press('Tab');
    if (await focused(page) === id) return;
  }
  throw new Error(`Tab does not reach #${id}`);
}

const mark = (page: Page, input: string): Locator => page.locator(`.row:has(#${input}) > .info`);

/** The document is not wider than the viewport, and the bubble lies inside it from left to right. */
async function onScreen(page: Page, bubble: Locator): Promise<void> {
  const [scroll, width] = await page.evaluate(() => [document.documentElement.scrollWidth, window.innerWidth]);
  expect(scroll).toBeLessThanOrEqual(width as number);
  const box = await bubble.boundingBox();
  expect(box).not.toBeNull();
  expect(box!.x).toBeGreaterThanOrEqual(0);
  expect(box!.x + box!.width).toBeLessThanOrEqual(width as number);
}

test.describe('1280 px, keyboard and pointer', () => {
  test.use({ viewport: { width: 1280, height: 800 }, locale: 'en-GB' });

  test('the focus of a tick opens its bubble, Escape and the next Tab close it, the Tab order stays', async ({ page }) => {
    const seen = watch(page);
    await open(page, '#v=1');
    const bubble = page.locator('#hb-f-servo');
    await expect(page.locator('#f-servo')).toHaveAttribute('aria-describedby', 'hb-f-servo');
    await expect(bubble).toHaveAttribute('role', 'tooltip');
    await expect(bubble).toBeHidden();
    // Neither the mark nor the bubble takes the focus: the next stop after a tick is the next tick.
    await expect(page.locator('#choose .info[tabindex], #choose .bubble [tabindex], #choose .bubble a, #choose .bubble button')).toHaveCount(0);
    await tabTo(page, 'f-servo');
    await expect(bubble).toBeVisible();
    await expect(bubble).toContainText(FUNCTION_HELP.servo!.en);
    await expect(bubble).toContainText('Connects: Servo.');
    await expect(page.locator('#choose .bubble:visible')).toHaveCount(1);

    // Escape closes it; the focus and the tick stay.
    await page.keyboard.press('Escape');
    await expect(bubble).toBeHidden();
    await expect(page.locator('#f-servo')).toBeFocused();
    await expect(page.locator('#f-servo')).not.toBeChecked();

    // The next Tab moves on to the next tick and its bubble; back with Shift+Tab, the first opens again.
    await page.keyboard.press('Tab');
    await expect(page.locator('#f-servoprog')).toBeFocused();
    await expect(page.locator('#hb-f-servoprog')).toBeVisible();
    await expect(bubble).toBeHidden();
    await page.keyboard.press('Shift+Tab');
    await expect(bubble).toBeVisible();

    // Space ticks the function; the page draws the chooser again and the bubble of the focused tick stays.
    await page.keyboard.press('Space');
    await expect(page.locator('#f-servo')).toBeChecked();
    await expect(page.locator('#f-servo')).toBeFocused();
    await expect(page.locator('#hb-f-servo')).toBeVisible();

    // A blur closes it.
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await expect(page.locator('#hb-f-servo')).toBeHidden();
    expect(seen).toEqual({ foreign: [], errors: [] });
  });

  test('the pointer over the mark opens the bubble, the bubble holds it open, Escape closes it', async ({ page }) => {
    await open(page, '#v=1');
    const bubble = page.locator('#hb-f-esc');
    await mark(page, 'f-esc').hover();
    await expect(bubble).toBeVisible();
    await expect(bubble).toContainText('Needs: ESC battery.');
    // The pointer moves from the mark into the bubble: it stays open.
    const box = (await bubble.boundingBox())!;
    await page.mouse.move(box.x + box.width - 10, box.y + 6, { steps: 4 });
    await expect(bubble).toBeVisible();
    await page.mouse.move(box.x + 20, box.y + box.height - 4, { steps: 4 });
    await expect(bubble).toBeVisible();
    // Off the bubble and off the mark it closes.
    await page.locator('#h-choose').hover();
    await expect(bubble).toBeHidden();
    // Escape closes it while the pointer rests on the mark; the pointer entering the mark again opens it.
    await mark(page, 'f-esc').hover();
    await expect(bubble).toBeVisible();
    await page.keyboard.press('Escape');
    await expect(bubble).toBeHidden();
    await page.locator('#h-choose').hover();
    await mark(page, 'f-esc').hover();
    await expect(bubble).toBeVisible();
    // The pointer over the label alone opens nothing.
    await page.locator('#h-choose').hover();
    await page.locator('label:has(#f-servo)').hover();
    await expect(page.locator('#choose .bubble:visible')).toHaveCount(0);
  });

  test('a click on the mark opens the bubble and leaves the tick, a click elsewhere closes it', async ({ page }) => {
    await open(page, '#v=1');
    const hash = new URL(page.url()).hash;
    await mark(page, 'f-esc').click();
    await expect(page.locator('#hb-f-esc')).toBeVisible();
    await expect(page.locator('#f-esc')).not.toBeChecked();
    await page.locator('#h-choose').click();
    await expect(page.locator('#hb-f-esc')).toBeHidden();
    expect(new URL(page.url()).hash).toBe(hash);
  });

  test('the question before a removal hides the bubble, and one Escape answers it', async ({ page }) => {
    await open(page, '#v=1&p=sd');
    await page.locator('#own-summary').click();
    await tabTo(page, 'p-sd');
    await expect(page.locator('#hb-p-sd')).toBeVisible();
    await page.keyboard.press('Space');
    // The focus moves to "Keep": the bubble closes and leaves the question under the row free.
    await expect(page.locator('.row:has(#p-sd) + #rmwarn')).toHaveCount(1);
    await expect(page.locator('#choose .bubble:visible')).toHaveCount(0);
    await page.keyboard.press('Escape');
    await expect(page.locator('#rmwarn')).toHaveCount(0);
    await expect(page.locator('#p-sd')).toBeFocused();
    await expect(page.locator('#p-sd')).toBeChecked();
    await expect(page.locator('#hb-p-sd')).toBeVisible();
  });

  test('the bubbles in German', async ({ page }) => {
    await open(page, '#v=1&l=de');
    await tabTo(page, 'f-esc');
    const bubble = page.locator('#hb-f-esc');
    await expect(bubble).toBeVisible();
    await expect(bubble).toContainText(FUNCTION_HELP.esc!.de);
    await expect(bubble).toContainText(`${t('de', 'gNeeds')}ESC-Akku.`);
    await expect(bubble).toContainText('Schließt an: ESC.');
    await page.locator('#own-summary').click();
    await mark(page, 'p-ina228').hover();
    await expect(page.locator('#hb-p-ina228')).toContainText(role('ina228', 'de'));
    await expect(page.locator('#hb-p-ina228')).toContainText('Versorgung, 5 V: Koprozessor VBUS (Pad 40).');
  });
});

test.describe('360 px, touch', () => {
  test.use({ viewport: { width: 360, height: 740 }, hasTouch: true, isMobile: true, locale: 'en-GB' });

  test('a tap on the mark opens the bubble and never changes the tick; the bubble stays on screen', async ({ page }) => {
    const seen = watch(page);
    await open(page, '#v=1');
    const hash = new URL(page.url()).hash;
    for (const id of FUNCTIONS) {
      const bubble = page.locator(`#hb-f-${id}`);
      await mark(page, `f-${id}`).tap();
      await expect(bubble).toBeVisible();
      await expect(page.locator('#choose .bubble:visible')).toHaveCount(1);
      await expect(page.locator(`#f-${id}`)).not.toBeChecked();
      await onScreen(page, bubble);
      // A second tap on the mark closes the bubble: it covers the rows under it.
      await mark(page, `f-${id}`).tap();
      await expect(bubble).toBeHidden();
    }
    expect(new URL(page.url()).hash).toBe(hash);
    // A tap on the open bubble closes it too.
    await mark(page, 'f-esc').tap();
    await page.locator('#hb-f-esc').tap();
    await expect(page.locator('#hb-f-esc')).toBeHidden();
    await expect(page.locator('#f-esc')).not.toBeChecked();

    // The parts you own, in the widest setup.
    await open(page, `#v=1&f=${FUNCTIONS.join(',')}&p=${OWNED.join(',')}&o=osoyoo,xt90,pd&l=de`);
    await page.locator('#own-summary').tap();
    for (const id of OWNED) {
      const bubble = page.locator(`#hb-p-${id}`);
      await mark(page, `p-${id}`).tap();
      await expect(bubble).toBeVisible();
      await expect(bubble).toContainText(role(id, 'de'));
      await expect(page.locator(`#p-${id}`)).toBeChecked();
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await onScreen(page, bubble);
      await mark(page, `p-${id}`).tap();
      await expect(bubble).toBeHidden();
    }
    // A tap elsewhere closes it; a tap on the label still changes the tick.
    await mark(page, 'p-sd').tap();
    await expect(page.locator('#hb-p-sd')).toBeVisible();
    await page.locator('#h-choose').tap();
    await expect(page.locator('#choose .bubble:visible')).toHaveCount(0);
    await page.locator('label:has(#f-servo)').tap();
    await expect(page.locator('#f-servo')).not.toBeChecked();
    await expect(page.locator('#choose .bubble:visible')).toHaveCount(0);
    expect(seen).toEqual({ foreign: [], errors: [] });
  });
});
