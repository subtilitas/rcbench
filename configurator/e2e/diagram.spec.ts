// The wiring picture of the built page under `vite preview`: phone and desktop
// width, English and German, light and dark, keyboard, pointer and print.

import { mkdirSync } from 'node:fs';
import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { STRINGS } from '../src/i18n.ts';

const SIZES = [{ width: 360, height: 800 }, { width: 1280, height: 800 }];
const FUNCTIONS = CATALOGUE.functions.map((f) => f.id);
const OWNED = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const BASE = '#v=1';
const MID = '#v=1&p=estop,ina3221,as5600';
const FULL = `#v=1&f=${FUNCTIONS.join(',')}&p=${OWNED.join(',')}&o=osoyoo,xt90,pd`;
const FIT = 'translate(0 0) scale(1)';
/** Folder of the screenshots. The workflow keeps it as an artifact; git ignores it. */
const SHOTS = 'test-results/diagram-shots';

const wiresOf = (owned: string[]): number[] =>
  resolve(CATALOGUE, { functions: [], owned, options: { ...DEFAULT_OPTIONS } }).wires.map((w) => w.n);

type Watch = { foreign: string[]; errors: string[]; failed: string[]; drawings: string[] };

function watch(page: Page): Watch {
  const seen: Watch = { foreign: [], errors: [], failed: [], drawings: [] };
  page.on('request', (r) => {
    const url = new URL(r.url());
    if (url.protocol !== 'data:' && url.origin !== 'http://localhost:4173') seen.foreign.push(r.url());
  });
  page.on('response', (r) => {
    const path = new URL(r.url()).pathname;
    if (r.status() >= 400) seen.failed.push(`${r.status()} ${path}`);
    else if (path.includes('/drawings/')) seen.drawings.push(path);
  });
  page.on('pageerror', (e) => seen.errors.push(String(e)));
  page.on('console', (m) => {
    if (m.type() === 'error') seen.errors.push(m.text());
  });
  return seen;
}

async function open(page: Page, hash: string): Promise<void> {
  await page.goto('about:blank');
  await page.goto(`./${hash}`);
  await page.locator('svg.picture').waitFor();
  await page.waitForLoadState('networkidle');
}

const transform = (page: Page): Promise<string | null> => page.locator('svg.picture g.vp').getAttribute('transform');

/** Scale and offset of the view, read from the transform of the picture. */
async function viewOf(page: Page): Promise<{ k: number; x: number; y: number }> {
  const m = /translate\((-?[\d.]+) (-?[\d.]+)\) scale\(([\d.]+)\)/.exec((await transform(page)) ?? '');
  expect(m, 'the picture carries a view').not.toBeNull();
  return { x: Number(m![1]), y: Number(m![2]), k: Number(m![3]) };
}

async function fits(page: Page): Promise<void> {
  const [scroll, width] = await page.evaluate(() => [document.documentElement.scrollWidth, window.innerWidth]);
  expect(scroll).toBeLessThanOrEqual(width as number);
}

for (const size of SIZES) {
  test.describe(`${size.width} px`, () => {
    test.use({ viewport: size });

    for (const lang of ['en', 'de'] as const) {
      for (const scheme of ['light', 'dark'] as const) {
        test(`${lang}, ${scheme}: the picture is the first view, fits the page and loads its drawings`, async ({ page }) => {
          const seen = watch(page);
          await page.emulateMedia({ colorScheme: scheme });
          await open(page, `${FULL}&l=${lang}`.replace('&l=en', ''));
          const col = lang === 'en' ? 0 : 1;
          await expect(page.locator('html')).toHaveAttribute('lang', lang);
          await expect(page.locator('#v-pic')).toHaveAttribute('aria-pressed', 'true');
          await expect(page.locator('#v-pic')).toHaveText(STRINGS.vPic[col]);
          await expect(page.locator('#view-pic')).toBeVisible();
          await expect(page.locator('#view-sch')).toBeHidden();
          await fits(page);

          // The box holds the whole picture and is no wider than the page.
          const box = (await page.locator('#dia').boundingBox())!;
          expect(box.x).toBeGreaterThanOrEqual(0);
          expect(box.x + box.width).toBeLessThanOrEqual(size.width);
          expect(box.height).toBeLessThanOrEqual(size.height * 0.8 + 2);
          expect(await transform(page)).toBe(FIT);

          // 1 drawing per part, from the folder of the language, each loaded.
          const hrefs = await page.locator('svg.picture image').evaluateAll((els) => els.map((el) => el.getAttribute('href') ?? ''));
          expect(hrefs).toHaveLength(16);
          for (const href of hrefs) expect(href.startsWith('drawings/de/'), href).toBe(lang === 'de');
          expect(new Set(seen.drawings)).toEqual(new Set(hrefs.map((href) => `/rcbench/${href}`)));

          // Name and description: a title, a description and the caption of the table.
          const svg = page.locator('svg.picture');
          await expect(svg).toHaveAttribute('aria-labelledby', 'dia-t');
          await expect(svg).toHaveAttribute('aria-describedby', 'dia-d wires-cap');
          for (const id of ['dia-t', 'dia-d', 'wires-cap']) expect((await page.locator(`#${id}`).textContent())!.length).toBeGreaterThan(10);
          await expect(page.locator('#dia')).toHaveAttribute('aria-label', STRINGS.diaBox[col]);

          // Each wire carries its number, and each wire of the table but wire 31 is in the picture.
          const drawn = await page.locator('svg.picture g.dw').evaluateAll((els) => els.map((el) => ({
            n: el.getAttribute('data-wire'),
            numbers: [...el.querySelectorAll('text.nt')].map((t) => t.textContent),
            label: el.getAttribute('aria-label') ?? '',
          })));
          const rows = await page.locator('table.wires button.wn').evaluateAll((els) => els.map((el) => el.getAttribute('data-wire')));
          expect(drawn.map((w) => w.n)).toEqual(rows.filter((n) => n !== '31'));
          for (const w of drawn) {
            expect(w.numbers.length, `wire ${w.n}`).toBeGreaterThanOrEqual(1);
            expect(new Set(w.numbers)).toEqual(new Set([w.n]));
            expect(w.label).toContain(`${w.n}:`);
          }
          await expect(page.locator('#dia-hint')).toContainText(STRINGS.diaUndrawn[col].replace('{n}', '31'));

          // Load wires are 7 px wide, the others 3 px, and the legend names both widths and both supply colours.
          const widths = await page.locator('svg.picture g.dw').evaluateAll((els) => els.map((el) => ({
            thick: el.classList.contains('thick'),
            px: getComputedStyle(el.querySelector('path.core')!).strokeWidth,
            part: el.classList.contains('k-component'),
          })));
          expect(widths.filter((w) => w.thick).length).toBeGreaterThanOrEqual(6);
          for (const w of widths) expect(w.px).toBe(w.part ? '2px' : w.thick ? '7px' : '3px');
          const items = page.locator('#dia-legend li');
          await expect(items).toHaveCount(4);
          await expect(items.nth(0)).toHaveText(STRINGS.dlPower[col]);
          await expect(items.nth(1)).toHaveText(STRINGS.dlSignal[col]);
          expect(await items.locator('svg path').evaluateAll((els) => els.map((el) => el.getAttribute('stroke-width')))).toContain('7');

          // The colours of the page follow the theme.
          const ink = await page.locator('svg.picture text.cap').first().evaluate((el) => getComputedStyle(el).fill);
          const surface = await page.locator('#dia').evaluate((el) => getComputedStyle(el).backgroundColor);
          expect(ink).not.toBe(surface);
          expect(seen.foreign).toEqual([]);
          expect(seen.errors).toEqual([]);
          expect(seen.failed).toEqual([]);
        });
      }
    }

    test('the keyboard zooms, moves and fits the picture', async ({ page }) => {
      await open(page, MID);
      await page.locator('#v-pic').focus();
      // View switch, 7 controls, then the box.
      for (const id of ['v-sch', 'out', 'in', 'fit', 'left', 'up', 'down', 'right']) {
        await page.keyboard.press('Tab');
        const at = await page.evaluate(() => (document.activeElement as HTMLElement).dataset.dia ?? document.activeElement!.id);
        expect(at).toBe(id);
      }
      await page.keyboard.press('Tab');
      await expect(page.locator('#dia')).toBeFocused();
      // The scroll that brings the focused box into view has ended when 2 readings agree.
      let top = -1;
      await expect.poll(async () => {
        const last = top;
        top = await page.evaluate(() => window.scrollY);
        return top === last;
      }).toBe(true);

      await page.keyboard.press('+');
      expect((await viewOf(page)).k).toBe(1.5);
      await page.keyboard.press('=');
      const zoomed = await viewOf(page);
      expect(zoomed.k).toBe(2.25);
      await page.keyboard.press('ArrowRight');
      const right = await viewOf(page);
      expect(right.x).toBeLessThan(zoomed.x);
      expect(right.y).toBe(zoomed.y);
      await page.keyboard.press('ArrowDown');
      expect((await viewOf(page)).y).toBeLessThan(right.y);
      // The arrow keys move the picture, not the page.
      expect(await page.evaluate(() => window.scrollY)).toBe(top);
      await page.keyboard.press('-');
      expect((await viewOf(page)).k).toBe(1.5);
      await page.keyboard.press('0');
      expect(await transform(page)).toBe(FIT);

      // At the fit the picture has no room to move, and zooming out stops there.
      await page.keyboard.press('ArrowLeft');
      await page.keyboard.press('-');
      expect(await transform(page)).toBe(FIT);
      // Zooming in stops at the largest scale.
      for (let i = 0; i < 12; i++) await page.keyboard.press('+');
      const most = (await viewOf(page)).k;
      await page.keyboard.press('+');
      expect((await viewOf(page)).k).toBe(most);
      expect(most).toBeGreaterThanOrEqual(2);
      // The picture covers the box at every edge.
      for (let i = 0; i < 30; i++) await page.keyboard.press('ArrowRight');
      const edge = await viewOf(page);
      const width = await page.locator('svg.picture').evaluate((el: SVGSVGElement) => el.viewBox.baseVal.width);
      expect(edge.x).toBeCloseTo(width - width * edge.k, 0);
      await fits(page);
    });

    test('the buttons zoom, move and fit the picture', async ({ page }) => {
      await open(page, MID);
      const tool = (id: string) => page.locator(`button.tool[data-dia=${id}]`);
      for (const id of ['out', 'in', 'fit', 'left', 'up', 'down', 'right']) {
        const box = (await tool(id).boundingBox())!;
        expect(Math.min(box.width, box.height), id).toBeGreaterThanOrEqual(44);
        expect(((await tool(id).getAttribute('aria-label')) ?? (await tool(id).textContent()))!.length).toBeGreaterThan(2);
      }
      await tool('in').click();
      await tool('in').click();
      const a = await viewOf(page);
      expect(a.k).toBe(2.25);
      await tool('right').click();
      await tool('down').click();
      const b = await viewOf(page);
      expect(b.x).toBeLessThan(a.x);
      expect(b.y).toBeLessThan(a.y);
      await tool('left').click();
      await tool('up').click();
      expect(await viewOf(page)).toEqual(a);
      await tool('out').click();
      expect((await viewOf(page)).k).toBe(1.5);
      await tool('fit').click();
      expect(await transform(page)).toBe(FIT);
      // The view survives a change of the setup and of the language.
      await tool('in').click();
      await page.locator('.lang button[data-lang=de]').click();
      await expect(page.locator('html')).toHaveAttribute('lang', 'de');
      expect((await viewOf(page)).k).toBe(1.5);
      await fits(page);
    });

    test('a table row marks its wire and a wire marks its row, by keyboard', async ({ page }) => {
      const seen = watch(page);
      await open(page, MID);
      const numbers = wiresOf(['estop', 'ina3221', 'as5600']);
      const wire = (n: number) => page.locator(`svg.picture g.dw[data-wire="${n}"]`);
      const button = (n: number) => page.locator(`table.wires button.wn[data-wire="${n}"]`);
      const row = (n: number) => page.locator('table.wires tr', { has: page.locator(`button.wn[data-wire="${n}"]`) });

      // From the table: focus shows the wire, Enter marks it.
      await button(5).focus();
      await expect(wire(5)).toHaveClass(/\bon\b/);
      await expect(page.locator('svg.picture')).toHaveClass(/has-on/);
      await expect(button(5)).toHaveAttribute('aria-pressed', 'false');
      await page.keyboard.press('Enter');
      for (const el of [button(5), wire(5)]) await expect(el).toHaveAttribute('aria-pressed', 'true');
      await expect(wire(5)).toHaveClass(/marked/);
      await expect(row(5)).toHaveClass(/marked/);
      // The mark stays when the focus leaves, and 1 wire is marked at a time.
      await button(6).focus();
      await expect(wire(5)).toHaveClass(/marked/);
      await page.keyboard.press('Space');
      await expect(wire(6)).toHaveAttribute('aria-pressed', 'true');
      await expect(wire(5)).toHaveAttribute('aria-pressed', 'false');
      await expect(page.locator('[data-wire][aria-pressed=true]')).toHaveCount(2);

      // From the picture: Tab from the box reaches the marked wire, arrows walk the wires.
      await page.locator('#dia').focus();
      await page.keyboard.press('Tab');
      await expect(wire(6)).toBeFocused();
      await page.keyboard.press('Home');
      await expect(wire(numbers[0]!)).toBeFocused();
      await page.keyboard.press('ArrowUp');
      await expect(wire(numbers[0]!)).toBeFocused();
      await page.keyboard.press('ArrowDown');
      await expect(wire(numbers[1]!)).toBeFocused();
      await expect(row(numbers[1]!)).toHaveClass(/\bon\b/);
      await page.keyboard.press('Enter');
      await expect(button(numbers[1]!)).toHaveAttribute('aria-pressed', 'true');
      await expect(row(numbers[1]!)).toHaveClass(/marked/);
      await expect(wire(6)).toHaveAttribute('aria-pressed', 'false');
      // Enter on the marked wire takes the mark off.
      await page.keyboard.press('Enter');
      await expect(page.locator('[data-wire][aria-pressed=true]')).toHaveCount(0);
      const last = numbers.filter((n) => n !== 31).at(-1)!;
      await page.keyboard.press('End');
      await expect(wire(last)).toBeFocused();
      await page.keyboard.press('ArrowRight');
      await expect(wire(last)).toBeFocused();
      // 1 wire is in the tab order.
      expect(await page.locator('svg.picture g.dw[tabindex="0"]').count()).toBe(1);
      await page.keyboard.press('Escape');
      await expect(page.locator('#dia')).toBeFocused();
      await expect(page.locator('svg.picture')).not.toHaveClass(/has-on/);

      // A zoomed view moves to a wire that takes the focus outside of it.
      for (let i = 0; i < 4; i++) await page.keyboard.press('+');
      await page.keyboard.press('Tab');
      await page.keyboard.press('End');
      const centre = (await wire(last).getAttribute('data-c'))!.split(' ').map(Number);
      const v = await viewOf(page);
      const size = await page.locator('svg.picture').evaluate((el: SVGSVGElement) => [el.viewBox.baseVal.width, el.viewBox.baseVal.height]);
      for (const i of [0, 1]) {
        const at = centre[i]! * v.k + (i === 0 ? v.x : v.y);
        expect(at).toBeGreaterThanOrEqual(0);
        expect(at).toBeLessThanOrEqual(size[i]!);
      }

      // A mark leaves with its wire and stays over a change that keeps the wire.
      await page.keyboard.press('Enter');
      await expect(button(last)).toHaveAttribute('aria-pressed', 'true');
      await page.locator('.lang button[data-lang=de]').click();
      await expect(page.locator('html')).toHaveAttribute('lang', 'de');
      await expect(wire(last)).toHaveAttribute('aria-pressed', 'true');
      await page.goto(`./${BASE}`);
      await expect(page.locator('svg.picture g.dw')).toHaveCount(4);
      await expect(page.locator('[data-wire][aria-pressed=true]')).toHaveCount(0);
      await expect(page.locator('svg.picture')).not.toHaveClass(/has-on/);
      expect(seen.errors).toEqual([]);
    });

    test('the switch shows the schematic and comes back to the picture with its view', async ({ page }) => {
      await open(page, MID);
      await page.locator('#dia').focus();
      await page.keyboard.press('+');
      await page.locator('#v-pic').focus();
      await page.keyboard.press('Tab');
      await page.keyboard.press('Enter');
      await expect(page.locator('#v-sch')).toBeFocused();
      await expect(page.locator('#v-sch')).toHaveAttribute('aria-pressed', 'true');
      await expect(page.locator('#view-pic')).toBeHidden();
      await expect(page.locator('svg.drawing')).toBeVisible();
      await fits(page);
      // The choice holds over a change of the setup.
      await page.goto(`./${FULL}`);
      await expect(page.locator('svg.drawing')).toBeVisible();
      await expect(page.locator('#view-pic')).toBeHidden();
      await page.locator('#v-pic').click();
      await expect(page.locator('#v-pic')).toBeFocused();
      await expect(page.locator('svg.picture')).toBeVisible();
      await expect(page.locator('svg.drawing')).toBeHidden();
      expect((await viewOf(page)).k).toBe(1.5);
      // The view is no part of the link.
      expect(page.url()).not.toMatch(/pic|sch/);
      await fits(page);
    });

    test('print shows the picture in the fit view on its own page, and the schematic', async ({ page }) => {
      await open(page, MID);
      await page.locator('#dia').focus();
      await page.keyboard.press('+');
      await page.keyboard.press('ArrowRight');
      await page.locator('table.wires button.wn').first().focus();
      await page.keyboard.press('Enter');
      await page.emulateMedia({ media: 'print' });
      await expect(page.locator('#view-pic')).toBeVisible();
      await expect(page.locator('#view-sch')).toBeVisible();
      for (const sel of ['.tools', '.views', '#dia-hint']) await expect(page.locator(sel)).toBeHidden();
      await expect(page.locator('#dia-legend')).toBeVisible();
      const style = await page.locator('svg.picture g.vp').evaluate((el) => getComputedStyle(el).transform);
      expect(style).toBe('none');
      const breaks = await page.locator('#view-pic').evaluate((el) => [getComputedStyle(el).breakBefore, getComputedStyle(el).breakAfter]);
      expect(breaks).toEqual(['page', 'page']);
      // No wire is dimmed on paper.
      const dim = await page.locator('svg.picture g.dw').evaluateAll((els) => els.filter((el) => getComputedStyle(el).opacity !== '1').length);
      expect(dim).toBe(0);
      const box = (await page.locator('svg.picture').boundingBox())!;
      expect(box.width).toBeLessThanOrEqual(size.width);
      await fits(page);
    });

    test('reduced motion: a wire changes without a transition', async ({ page }) => {
      await page.emulateMedia({ reducedMotion: 'reduce' });
      await open(page, BASE);
      const durations = await page.locator('svg.picture g.dw').evaluateAll((els) => els.map((el) => getComputedStyle(el).transitionDuration));
      expect(new Set(durations)).toEqual(new Set(['0s']));
    });
  });
}

test('a drag moves the zoomed picture and does not mark the wire under it', async ({ page }) => {
  await page.setViewportSize(SIZES[1]!);
  await open(page, MID);
  await page.locator('button.tool[data-dia=in]').click();
  const before = await viewOf(page);
  const box = (await page.locator('#dia').boundingBox())!;
  await page.locator('#dia').scrollIntoViewIfNeeded();
  const at = (await page.locator('#dia').boundingBox())!;
  await page.mouse.move(at.x + at.width / 2, at.y + at.height / 2);
  await page.mouse.down();
  await page.mouse.move(at.x + at.width / 2 - 80, at.y + at.height / 2 - 40, { steps: 4 });
  await page.mouse.up();
  const after = await viewOf(page);
  expect(after.k).toBe(before.k);
  expect(after.x).toBeLessThan(before.x);
  expect(after.y).toBeLessThan(before.y);
  expect(box.width).toBeGreaterThan(0);
  await expect(page.locator('[data-wire][aria-pressed=true]')).toHaveCount(0);

  // A click on a wire marks it and its row; the pointer over a wire shows it.
  await page.locator('button.tool[data-dia=fit]').click();
  const hit = page.locator('svg.picture g.dw[data-wire="5"] path.hit');
  // The point of the wire in px of the window, after the page has scrolled it to the middle.
  const spot = await hit.evaluate((el: SVGPathElement) => {
    const p = el.getPointAtLength(el.getTotalLength() * 0.3);
    const at = (): { x: number; y: number } => {
      const m = el.getScreenCTM()!;
      return { x: p.x * m.a + p.y * m.c + m.e, y: p.x * m.b + p.y * m.d + m.f };
    };
    window.scrollBy(0, at().y - window.innerHeight / 2);
    return at();
  });
  expect(spot.y).toBeGreaterThan(0);
  expect(spot.y).toBeLessThan(SIZES[1]!.height);
  await page.mouse.move(spot.x, spot.y);
  await expect(page.locator('svg.picture g.dw[data-wire="5"]')).toHaveClass(/\bon\b/);
  await page.mouse.click(spot.x, spot.y);
  await expect(page.locator('table.wires button.wn[data-wire="5"]')).toHaveAttribute('aria-pressed', 'true');
  // Ctrl and the wheel zoom; the wheel alone scrolls the page.
  await page.keyboard.down('Control');
  await page.mouse.wheel(0, -100);
  await page.keyboard.up('Control');
  await expect.poll(async () => (await viewOf(page)).k).toBeGreaterThan(1);
});

test.describe('touch', () => {
  test.use({ viewport: SIZES[0], hasTouch: true, isMobile: true });

  test('2 fingers zoom the picture inside its box and the page keeps its width', async ({ page }) => {
    await open(page, MID);
    expect(await page.locator('#dia').evaluate((el) => getComputedStyle(el).touchAction)).toBe('pan-y');
    await page.locator('#dia').scrollIntoViewIfNeeded();
    const box = (await page.locator('#dia').boundingBox())!;
    const cx = box.x + box.width / 2;
    const cy = box.y + box.height / 2;
    const cdp = await page.context().newCDPSession(page);
    const points = (d: number) => [{ x: cx - d, y: cy, id: 0 }, { x: cx + d, y: cy, id: 1 }];
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: points(20) });
    for (const d of [30, 40, 50, 60]) await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: points(d) });
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await expect.poll(async () => (await viewOf(page)).k).toBeGreaterThan(1.5);
    await fits(page);
    const after = (await page.locator('#dia').boundingBox())!;
    expect(after.width).toBe(box.width);
    // The fit button brings the whole picture back.
    await page.locator('button.tool[data-dia=fit]').tap();
    expect(await transform(page)).toBe(FIT);
  });
});

test.describe('screenshots', () => {
  mkdirSync(SHOTS, { recursive: true });
  const setups = [['base', BASE], ['sensors', MID], ['full', FULL]] as const;
  for (const [name, hash] of setups) {
    for (const size of SIZES) {
      test(`${name} at ${size.width} px`, async ({ page }) => {
        await page.setViewportSize(size);
        const seen = watch(page);
        for (const scheme of ['light', 'dark'] as const) {
          await page.emulateMedia({ colorScheme: scheme });
          await open(page, hash);
          await page.locator('#dia').screenshot({ path: `${SHOTS}/${name}-${size.width}-${scheme}.png` });
        }
        await open(page, `${hash}&l=de`);
        await page.locator('#dia').screenshot({ path: `${SHOTS}/${name}-${size.width}-dark-de.png` });
        expect(seen.errors).toEqual([]);
        expect(seen.failed).toEqual([]);
      });
    }
  }
});
