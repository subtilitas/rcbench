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

/**
 * The document is not wider than the viewport, the bubble lies inside it from left to right, and from top to
 * bottom between the top of the viewport and the top of the bar of the narrow layout.
 */
async function onScreen(page: Page, bubble: Locator): Promise<void> {
  const [scroll, width, barTop] = await page.evaluate(() => [
    document.documentElement.scrollWidth,
    window.innerWidth,
    document.getElementById('bar')!.getBoundingClientRect().top,
  ]);
  expect(scroll).toBeLessThanOrEqual(width);
  const box = await bubble.boundingBox();
  expect(box).not.toBeNull();
  expect(box!.x).toBeGreaterThanOrEqual(0);
  expect(box!.x + box!.width).toBeLessThanOrEqual(width);
  expect(box!.y).toBeGreaterThanOrEqual(0);
  expect(box!.y + box!.height).toBeLessThanOrEqual(barTop + 0.5);
}

/**
 * How far the box of `bubble` reaches over the top and under the bottom of the part of the viewport that shows
 * it: the viewport, cut by the client box of #choose where #choose scrolls (1024 px and wider).
 */
const cut = (bubble: Locator): Promise<{ top: number; bottom: number }> => bubble.evaluate((box) => {
  const choose = document.getElementById('choose')!;
  const b = box.getBoundingClientRect();
  let top = 0;
  let bottom = window.innerHeight;
  if (getComputedStyle(choose).overflowY !== 'visible') {
    const c = choose.getBoundingClientRect();
    top = Math.max(top, c.top + choose.clientTop);
    bottom = Math.min(bottom, c.top + choose.clientTop + choose.clientHeight);
  }
  return { top: Math.max(0, top - b.top), bottom: Math.max(0, b.bottom - bottom) };
});

/** The sum of both parts of `cut`: 0 when the whole bubble shows. */
const cutOff = async (bubble: Locator): Promise<number> => {
  const c = await cut(bubble);
  return c.top + c.bottom;
};

/**
 * Tabs from the top of the page through every tick, functions and the parts you own, and runs `each` on the
 * bubble of each tick as the focus reaches it.
 */
async function tabSweep(page: Page, each: (id: string, bubble: Locator) => Promise<void>): Promise<void> {
  await page.locator('#own-summary').click();
  const ids = await page.locator('#choose .row input').evaluateAll((inputs) => inputs.map((i) => i.id));
  expect(ids).toEqual(expect.arrayContaining(['f-stick', `f-${FUNCTIONS.at(-1)}`, `p-${OWNED.at(-1)}`]));
  expect(ids).toHaveLength(FUNCTIONS.length + OWNED.length);
  // The page scrolls smoothly: a smooth scroll to the top would go on after the first Tab and undo its scroll.
  await page.evaluate(() => {
    (document.activeElement as HTMLElement | null)?.blur();
    window.scrollTo({ top: 0, behavior: 'instant' });
    document.getElementById('choose')!.scrollTop = 0;
  });
  const seen: string[] = [];
  for (let i = 0; i < 400 && seen.length < ids.length; i++) {
    await page.keyboard.press('Tab');
    const id = await focused(page);
    if (!ids.includes(id) || seen.includes(id)) continue;
    seen.push(id);
    const bubble = page.locator(`#hb-${id}`);
    await expect(bubble).toBeVisible();
    await each(id, bubble);
  }
  expect(seen.sort()).toEqual([...ids].sort());
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

  test('a bubble opened by a click gives way to the focus and the pointer: one bubble at a time', async ({ page }) => {
    await open(page, '#v=1');
    const shown = page.locator('#choose .bubble:visible');
    // A click opens the bubble of f-esc; with the pointer elsewhere, Tab moves the focus to the next tick and
    // shows its bubble alone.
    await mark(page, 'f-esc').click();
    await expect(page.locator('#hb-f-esc')).toBeVisible();
    await page.locator('#h-choose').hover();
    await expect(page.locator('#hb-f-esc')).toBeVisible();
    await page.keyboard.press('Tab');
    const next = await focused(page);
    expect(next).toMatch(/^f-/);
    expect(next).not.toBe('f-esc');
    await expect(page.locator(`#hb-${next}`)).toBeVisible();
    await expect(shown).toHaveCount(1);

    // A click opens the bubble of f-esc; the pointer entering the mark of f-servo shows that bubble alone.
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await mark(page, 'f-esc').click();
    await expect(page.locator('#hb-f-esc')).toBeVisible();
    await mark(page, 'f-servo').hover();
    await expect(page.locator('#hb-f-servo')).toBeVisible();
    await expect(shown).toHaveCount(1);
  });

  test('a second click on the mark closes the bubble while the pointer rests on the mark', async ({ page }) => {
    await open(page, '#v=1');
    const bubble = page.locator('#hb-f-esc');
    await mark(page, 'f-esc').click();
    await expect(bubble).toBeVisible();
    await mark(page, 'f-esc').click();
    await expect(bubble).toBeHidden();
    await expect(page.locator('#choose .bubble:visible')).toHaveCount(0);
    // The pointer leaving the mark and entering it again opens the bubble; the tick stays as it is.
    await page.locator('#h-choose').hover();
    await mark(page, 'f-esc').hover();
    await expect(bubble).toBeVisible();
    await expect(page.locator('#f-esc')).not.toBeChecked();
    // The focus of the tick still opens a bubble a click closed.
    await mark(page, 'f-esc').click();
    await mark(page, 'f-esc').click();
    await page.locator('#h-choose').hover();
    await tabTo(page, 'f-esc');
    await expect(bubble).toBeVisible();
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

  for (const lang of ['en', 'de'] as const) {
    test(`the bubble of every tick shows whole when Tab reaches it, ${lang}`, async ({ page }) => {
      // #choose scrolls at this width and clips its content: a bubble under a row at the lower edge opens over it.
      await open(page, `#v=1&l=${lang}`);
      await tabSweep(page, async (id, bubble) => {
        await expect.poll(() => cutOff(bubble), { message: `the bubble of #${id} is cut off` }).toBe(0);
      });
    });
  }

  test('the pointer over the mark of a row at the lower edge of the chooser opens a bubble that shows whole', async ({ page }) => {
    await open(page, '#v=1&l=de');
    await page.locator('#own-summary').click();
    const last = `p-${OWNED.at(-1)}`;
    // The chooser sticks at the top of the viewport; the row of the last part sits on its lower edge.
    await page.evaluate(() => window.scrollTo({ top: 400, behavior: 'instant' }));
    const gap = await page.locator(`.row:has(#${last})`).evaluate((row) => {
      const c = document.getElementById('choose')!;
      c.scrollTop += row.getBoundingClientRect().bottom - (c.getBoundingClientRect().top + c.clientTop + c.clientHeight);
      return c.getBoundingClientRect().top + c.clientTop + c.clientHeight - row.getBoundingClientRect().bottom;
    });
    expect(Math.abs(gap)).toBeLessThan(2);
    await mark(page, last).hover();
    const bubble = page.locator(`#hb-${last}`);
    await expect(bubble).toBeVisible();
    await expect.poll(() => cutOff(bubble)).toBe(0);
  });
});

// A window too short for the tallest bubbles: the bubble of a row in the middle fits neither under nor over the
// row. It opens under the row, where #choose scrolls down to its end; its top is never cut off.
for (const viewport of [{ width: 1100, height: 450 }, { width: 1024, height: 500 }]) {
  test.describe(`${viewport.width} x ${viewport.height} px, keyboard`, () => {
    test.use({ viewport, locale: 'en-GB' });

    for (const lang of ['en', 'de'] as const) {
      test(`the top of every bubble shows when Tab reaches it, and #choose scrolls to its end, ${lang}`, async ({ page }) => {
        await open(page, `#v=1&l=${lang}`);
        let under = 0;
        await tabSweep(page, async (id, bubble) => {
          await expect.poll(async () => (await cut(bubble)).top, { message: `the top of the bubble of #${id} is cut off` }).toBe(0);
          if ((await cut(bubble)).bottom > 0) under++;
          // Scrolled into view, the whole bubble shows, to the rounding of the scroll position (under 1 px). The
          // scroll repeats on each poll: a smooth scroll that the focus started can still run and move the row.
          await expect.poll(async () => {
            await bubble.evaluate((box) => box.scrollIntoView({ block: 'nearest', behavior: 'instant' }));
            return cutOff(bubble);
          }, { message: `the bubble of #${id} is cut off after a scroll` }).toBeLessThan(1);
        });
        // The case this window is for: at least one bubble reached past the lower edge before the scroll.
        if (lang === 'de') expect(under).toBeGreaterThan(0);
      });
    }
  });
}

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

  test('the mark has a pointer cursor, and a pointerdown elsewhere closes the bubble without a click', async ({ page }) => {
    // iOS Safari sends a click to a document listener only for a target with cursor: pointer or a handler of its
    // own; it sends pointerdown for every tap.
    await open(page, '#v=1');
    await expect(mark(page, 'f-esc')).toHaveCSS('cursor', 'pointer');
    await mark(page, 'f-esc').tap();
    await expect(page.locator('#hb-f-esc')).toBeVisible();
    await page.locator('#h-choose').dispatchEvent('pointerdown', { bubbles: true, pointerType: 'touch' });
    await expect(page.locator('#hb-f-esc')).toBeHidden();
    // A pointerdown on the mark of the open bubble leaves it to the click that follows.
    await mark(page, 'f-esc').tap();
    await mark(page, 'f-esc').dispatchEvent('pointerdown', { bubbles: true, pointerType: 'touch' });
    await expect(page.locator('#hb-f-esc')).toBeVisible();
    await expect(page.locator('#f-esc')).not.toBeChecked();
  });
});
