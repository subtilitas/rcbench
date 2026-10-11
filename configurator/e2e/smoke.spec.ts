// Smoke test of the built page under `vite preview`, at phone and desktop width.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { STRINGS } from '../src/i18n.ts';

const SIZES = [{ width: 360, height: 800 }, { width: 1280, height: 800 }];
const FUNCTIONS = CATALOGUE.functions.map((f) => f.id);
const OWNED = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const BASE_WIRES = resolve(CATALOGUE, { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } }).wires.length;
const FULL = `#v=1&f=${FUNCTIONS.join(',')}&p=${OWNED.join(',')}&o=osoyoo,xt90,pd`;

type Watch = { foreign: string[]; errors: string[] };

/** Records every request to another origin, every page error and every console error. */
function watch(page: Page): Watch {
  const seen: Watch = { foreign: [], errors: [] };
  page.on('request', (r) => {
    const url = new URL(r.url());
    if (url.protocol !== 'data:' && url.origin !== 'http://localhost:4173') seen.foreign.push(r.url());
  });
  page.on('pageerror', (e) => seen.errors.push(String(e)));
  page.on('console', (m) => {
    if (m.type() === 'error') seen.errors.push(m.text());
  });
  return seen;
}

/** Loads the page with a hash. A hash-only navigation does not reload, so the page starts from blank. */
async function open(page: Page, hash: string): Promise<void> {
  await page.goto('about:blank');
  await page.goto(`./${hash}`);
  await page.locator('svg.drawing').waitFor();
}

async function fits(page: Page): Promise<void> {
  const [scroll, width] = await page.evaluate(() => [document.documentElement.scrollWidth, window.innerWidth]);
  expect(scroll).toBeLessThanOrEqual(width as number);
}

/** Every text of the drawing lies inside the canvas, and every module text inside a module box. */
async function drawingTexts(page: Page): Promise<string[]> {
  return page.evaluate(() => {
    const svg = document.querySelector<SVGSVGElement>('svg.drawing');
    if (!svg) return ['no drawing'];
    const out: string[] = [];
    const view = svg.viewBox.baseVal;
    const boxes = [...svg.querySelectorAll<SVGRectElement>('rect.box')].map((r) => r.getBBox());
    for (const text of svg.querySelectorAll<SVGTextElement>('text')) {
      const b = text.getBBox();
      const label = text.textContent ?? '';
      if (b.x < 0 || b.y < 0 || b.x + b.width > view.width || b.y + b.height > view.height) out.push(`outside the canvas: ${label}`);
      if (text.closest('.pill')) continue;
      const inside = boxes.some((r) => b.x >= r.x && b.x + b.width <= r.x + r.width && b.y >= r.y && b.y + b.height <= r.y + r.height);
      if (!inside) out.push(`outside its module: ${label}`);
    }
    return out;
  });
}

for (const size of SIZES) {
  test.describe(`${size.width} px`, () => {
    test.use({ viewport: size, locale: 'en-GB' });

    test('the base setup is on screen without a tap', async ({ page }) => {
      const seen = watch(page);
      await open(page, '');
      await expect(page.locator('html')).toHaveAttribute('lang', 'en');
      await expect(page.locator('#h-choose')).toHaveText('What should the bench do?');
      expect(BASE_WIRES).toBeGreaterThan(0);
      await expect(page.locator('table.wires tbody tr')).toHaveCount(BASE_WIRES);
      await expect(page.locator('svg.drawing path.w:not(.x)')).toHaveCount(BASE_WIRES);
      await expect(page.locator('.safety li')).toHaveCount(CATALOGUE.safety.length);
      await expect(page.locator('input:checked[type=checkbox]')).toHaveCount(0);
      expect(new URL(page.url()).hash).toBe('');
      await fits(page);
      expect(await drawingTexts(page)).toEqual([]);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('the smallest and the full selection fit the viewport', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=1&f=servo');
      await expect(page.locator('#f-servo')).toBeChecked();
      await fits(page);
      expect(await drawingTexts(page)).toEqual([]);

      for (const lang of ['en', 'de']) {
        await open(page, `${FULL}&l=${lang}`);
        await expect(page.locator('html')).toHaveAttribute('lang', lang);
        for (const id of FUNCTIONS) await expect(page.locator(`#f-${id}`)).toBeChecked();
        await expect(page.locator('.msg')).toHaveCount(0);
        const rows = await page.locator('table.wires tbody tr').count();
        expect(rows).toBeGreaterThan(30);
        await expect(page.locator('svg.drawing path.w:not(.x)')).toHaveCount(rows);
        await fits(page);
        expect(await drawingTexts(page)).toEqual([]);
      }
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('a tick writes the hash and the hash restores the tick', async ({ page }) => {
      const seen = watch(page);
      await open(page, '');
      await page.locator('#f-servo').check();
      await expect(page).toHaveURL(/#v=1&f=servo$/);
      await expect(page.locator('#f-servo')).toBeFocused();
      await expect(page.locator('#live')).toHaveText(/^\d+ parts, \d+ wires, 1 function\.$/);
      const rows = await page.locator('table.wires tbody tr').count();
      expect(rows).toBeGreaterThan(BASE_WIRES);

      await page.locator('#f-esc').check();
      await page.locator('#o-escPower-pd').check();
      await expect(page).toHaveURL(/#v=1&f=servo,esc&o=pd$/);
      const url = page.url();
      const html = await page.locator('#result').innerHTML();

      await page.goto('about:blank');
      await page.goto(url);
      await page.locator('svg.drawing').waitFor();
      await expect(page.locator('#f-servo')).toBeChecked();
      await expect(page.locator('#f-esc')).toBeChecked();
      await expect(page.locator('#o-escPower-pd')).toBeChecked();
      expect(page.url()).toBe(url);
      expect(await page.locator('#result').innerHTML()).toBe(html);

      // A changed hash on the open page applies without a reload.
      await page.evaluate(() => { location.hash = '#v=1&f=estop'; });
      await expect(page.locator('#f-estop')).toBeChecked();
      await expect(page.locator('#f-servo')).not.toBeChecked();

      await page.locator('#b-reset').click();
      await expect(page.locator('input:checked[type=checkbox]')).toHaveCount(0);
      await expect(page).toHaveURL(/#v=1$/);
      await expect(page.locator('table.wires tbody tr')).toHaveCount(BASE_WIRES);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('the language switch keeps the selection and the focus', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=1&f=servo');
      const de = page.locator('.lang button[data-lang=de]');
      const en = page.locator('.lang button[data-lang=en]');
      await expect(en).toHaveAttribute('aria-pressed', 'true');
      await de.click();
      await expect(page.locator('html')).toHaveAttribute('lang', 'de');
      await expect(page.locator('#h-choose')).toHaveText('Was soll der Prüfstand tun?');
      await expect(de).toHaveAttribute('aria-pressed', 'true');
      await expect(en).toHaveAttribute('aria-pressed', 'false');
      await expect(de).toBeFocused();
      await expect(page).toHaveURL(/#v=1&f=servo&l=de$/);
      await expect(page.locator('#f-servo')).toBeChecked();
      await fits(page);
      expect(await drawingTexts(page)).toEqual([]);
      await en.click();
      await expect(page.locator('html')).toHaveAttribute('lang', 'en');
      await expect(page).toHaveURL(/#v=1&f=servo$/);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('the keyboard reaches every control', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=1&f=servo,esc');
      await page.locator('#own-summary').focus();
      await page.keyboard.press('Enter');
      await expect(page.locator('#own')).toHaveAttribute('open', '');

      const wanted = await page.evaluate(() => {
        const out = new Set<string>();
        for (const el of document.querySelectorAll<HTMLElement>('button, a[href], input, summary, [tabindex="0"]')) {
          if (el.getClientRects().length === 0) continue;
          // Tab reaches one radio per group.
          out.add(el instanceof HTMLInputElement && el.type === 'radio' ? `radio:${el.name}` : el.id || el.dataset.lang || el.dataset.jump || el.tagName);
        }
        return [...out].sort();
      });
      expect(wanted.length).toBeGreaterThan(20);

      await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
      await page.locator('body').click({ position: { x: 1, y: 1 } });
      await page.evaluate(() => window.scrollTo(0, 0));
      const reached = new Set<string>();
      for (let i = 0; i < wanted.length + 20; i++) {
        await page.keyboard.press('Tab');
        const key = await page.evaluate(() => {
          const el = document.activeElement as HTMLElement | null;
          if (!el || el === document.body) return null;
          return el instanceof HTMLInputElement && el.type === 'radio' ? `radio:${el.name}` : el.id || el.dataset.lang || el.dataset.jump || el.tagName;
        });
        if (key === null) break;
        reached.add(key);
      }
      expect([...reached].sort()).toEqual(wanted);

      // Space ticks a function, focus stays on it.
      await page.locator('#f-estop').focus();
      await page.keyboard.press('Space');
      await expect(page.locator('#f-estop')).toBeChecked();
      await expect(page.locator('#f-estop')).toBeFocused();
      await expect(page.locator('#own')).toHaveAttribute('open', '');
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('a locked control stays focusable and keeps its state', async ({ page }) => {
      await open(page, '#v=1&f=stick');
      const battery = page.locator('#o-escPower-battery');
      await expect(page.locator('#o-escPower-pd')).toBeChecked();
      await expect(battery).toHaveAttribute('aria-disabled', 'true');
      await battery.focus();
      await expect(battery).toBeFocused();
      await battery.click({ force: true });
      await expect(battery).not.toBeChecked();
      await expect(page.locator('#o-escPower-pd')).toBeChecked();
      await expect(page).toHaveURL(/#v=1&f=stick$/);
    });

    test('a link from another version and an unknown id give a notice', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=2&f=servo');
      await expect(page.locator('.msg.err')).toHaveCount(1);
      await expect(page.locator('#f-servo')).not.toBeChecked();
      await expect(page.locator('.msg.err')).toHaveText(STRINGS.badV[0]);
      await page.locator('.lang button[data-lang=de]').click();
      await expect(page.locator('.msg.err')).toHaveText(STRINGS.badV[1]);
      await open(page, '#v=1&f=servo,warp&p=<img src=x onerror=alert(1)>');
      await expect(page.locator('.msg')).toHaveCount(1);
      await expect(page.locator('.msg')).toContainText('warp');
      const english = await page.locator('.msg').textContent();
      await page.locator('.lang button[data-lang=de]').click();
      await expect(page.locator('.msg')).toHaveCount(1);
      await expect(page.locator('.msg')).toContainText('warp');
      await expect(page.locator('.msg')).not.toHaveText(english ?? '');
      await page.locator('.lang button[data-lang=en]').click();
      await expect(page.locator('.msg')).toHaveText(english ?? '');
      // The markup of the link is text: the only images are the pictures of the parts.
      await expect(page.locator('img:not([src^="pictures/"])')).toHaveCount(0);
      await expect(page.locator('img[onerror]')).toHaveCount(0);
      await expect(page.locator('#f-servo')).toBeChecked();
      await page.locator('#f-esc').check();
      await expect(page.locator('.msg')).toHaveCount(0);
      await open(page, `#v=1&f=${'servo,'.repeat(5000)}`);
      await expect(page.locator('.msg.err')).toHaveCount(1);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('Copy link falls back to a selectable field', async ({ page, context }) => {
      await open(page, '#v=1&f=servo');
      await page.evaluate(() => {
        Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText: () => Promise.reject(new Error('denied')) } });
      });
      await page.locator('#b-copy').click();
      await expect(page.locator('#linkfield')).toBeVisible();
      await expect(page.locator('#linkfield')).toHaveValue(page.url());
      await expect(page.locator('#linkfield')).toBeFocused();
      await fits(page);

      await context.grantPermissions(['clipboard-read', 'clipboard-write']);
      await open(page, '#v=1&f=servo');
      await page.locator('#b-copy').click();
      await expect(page.locator('#live')).toHaveText('Link copied.');
      expect(await page.evaluate(() => navigator.clipboard.readText())).toBe(page.url());
    });

    test('rows and buttons are 44 px tall and the theme follows the system', async ({ page }) => {
      await open(page, FULL);
      const small = await page.evaluate(() => {
        const out: string[] = [];
        for (const el of document.querySelectorAll<HTMLElement>('.opt, button, summary, .bar a')) {
          const r = el.getBoundingClientRect();
          if (r.height > 0 && (r.height < 44 || r.width < 44)) out.push(`${el.id || el.textContent}: ${r.width} x ${r.height}`);
        }
        return out;
      });
      expect(small).toEqual([]);
      const background = (): Promise<string> => page.evaluate(() => getComputedStyle(document.body).backgroundColor);
      await page.emulateMedia({ colorScheme: 'light' });
      expect(await background()).toBe('rgb(244, 245, 247)');
      await page.emulateMedia({ colorScheme: 'dark' });
      expect(await background()).toBe('rgb(14, 17, 22)');
      await page.emulateMedia({ media: 'print', colorScheme: 'dark' });
      expect(await background()).toBe('rgb(255, 255, 255)');
      await expect(page.locator('#choose')).toBeHidden();
      await expect(page.locator('#b-print')).toBeHidden();
      await expect(page.locator('.printonly')).toBeVisible();
      await expect(page.locator('.printonly')).toContainText(page.url());
    });
  });
}

test.describe('pictures', () => {
  const pictureRequests = (page: Page): string[] => {
    const seen: string[] = [];
    page.on('request', (r) => {
      if (r.resourceType() === 'image' || /\/pictures\//.test(r.url())) seen.push(new URL(r.url()).pathname);
    });
    return seen;
  };
  // FULL sets the Osoyoo board and the XT90 module, so every picture shows.
  const ALL = FULL;

  // The browser fetches a lazy image some distance before it scrolls into view.
  // Chromium uses 1250 px on a fast connection and 2500 px on a slow one.
  const LAZY_REACH = 3000;

  test('at 360 px no picture is in the first screen and none far below it loads before a scroll', async ({ page }) => {
    await page.setViewportSize(SIZES[0]);
    const seen = pictureRequests(page);
    const foreign = watch(page);
    await open(page, ALL);
    await page.waitForLoadState('networkidle');
    expect(await page.evaluate(() => window.scrollY)).toBe(0);
    const images = await page.evaluate(() => [...document.querySelectorAll<HTMLImageElement>('.parts img')].map((img) => ({
      path: new URL(img.src).pathname,
      top: img.getBoundingClientRect().top,
      lazy: img.loading === 'lazy' && img.decoding === 'async',
    })));
    expect(images).toHaveLength(11);
    for (const image of images) {
      expect(image.lazy, image.path).toBe(true);
      // Above the fold: the first screen of 360 x 800 px holds no picture.
      expect(image.top, image.path).toBeGreaterThan(SIZES[0].height);
    }
    const far = images.filter((image) => image.top > SIZES[0].height + LAZY_REACH).map((image) => image.path);
    expect(far.length).toBeGreaterThan(0);
    expect(seen.filter((path) => far.includes(path))).toEqual([]);
    // The full file loads on a tap on the thumb only.
    expect(seen.filter((path) => !path.endsWith('-thumb.webp') && !path.endsWith('.svg'))).toEqual([]);

    const last = page.locator('.parts img').last();
    await last.scrollIntoViewIfNeeded();
    await expect.poll(() => last.evaluate((img: HTMLImageElement) => img.complete && img.naturalWidth > 0)).toBe(true);
    expect(seen).toContain(far[far.length - 1]);
    expect(seen.filter((path) => !path.endsWith('-thumb.webp') && !path.endsWith('.svg'))).toEqual([]);
    await fits(page);
    expect(foreign).toEqual({ foreign: [], errors: [] });
  });

  for (const size of SIZES) {
    test(`${size.width} px: every picture of the full selection loads, fits and links to its full file`, async ({ page }) => {
      await page.setViewportSize(size);
      const foreign = watch(page);
      for (const lang of ['en', 'de']) {
        await open(page, `${ALL}&l=${lang}`);
        const cards = await page.evaluate(async () => {
          const out: Array<{ src: string; href: string; alt: string; ok: boolean; ratio: number; shown: number; card: number }> = [];
          for (const img of document.querySelectorAll<HTMLImageElement>('.parts img')) {
            img.loading = 'eager';
            await img.decode();
            const box = img.getBoundingClientRect();
            out.push({
              src: img.getAttribute('src') ?? '',
              href: img.closest('a')?.getAttribute('href') ?? '',
              alt: img.alt,
              ok: img.naturalWidth === Number(img.getAttribute('width')) && img.naturalHeight === Number(img.getAttribute('height')),
              ratio: (box.width / box.height) / (img.naturalWidth / img.naturalHeight),
              shown: box.width,
              card: img.closest('li')!.getBoundingClientRect().width,
            });
          }
          return out;
        });
        // Panel 1, coprocessor 2, PD mini 1, INA3221 1, AS5600 1, INA228 2, emergency stop 2, T-plug 1.
        expect(cards.map((c) => c.src)).toHaveLength(11);
        for (const c of cards) {
          expect(c.ok, c.src).toBe(true);
          expect(c.alt.length, c.src).toBeGreaterThan(20);
          expect(Math.abs(c.ratio - 1), c.src).toBeLessThan(0.02);
          expect(c.shown, c.src).toBeLessThanOrEqual(Math.min(320, c.card));
          expect(c.href, c.src).toBe(c.src.replace('-thumb.webp', '.webp'));
        }
        const drawing = page.locator('.parts img[src$="ina3221.svg"]');
        await expect(drawing).toHaveAttribute('alt', lang === 'en'
          ? 'Illustration of the INA3221 board: header A0, VS, SCL, SDA, GND; 3 channels with pads 1 and 2; 3 large pads.'
          : 'Illustration der INA3221-Platine: Leiste A0, VS, SCL, SDA, GND; 3 Kanäle mit den Pads 1 und 2; 3 große Pads.');
        await expect(page.locator('.parts .ph, .parts figcaption')).toHaveCount(0);
        await expect(page.locator('.parts li:not(.pic) img')).toHaveCount(0);
        await fits(page);
      }
      // The MATEK board has no picture.
      await open(page, `#v=1&f=${FUNCTIONS.join(',')}`);
      await expect(page.locator('.parts img[src*="current-sensor"]')).toHaveCount(0);
      await expect(page.locator('.parts img[src*="breakout"]')).toHaveCount(0);

      // Print: a thumb is at most 40 mm wide, 151.2 px at 96 px per inch.
      await open(page, ALL);
      await page.emulateMedia({ media: 'print' });
      const widest = await page.evaluate(() => Math.max(...[...document.querySelectorAll('.parts img')].map((img) => img.getBoundingClientRect().width)));
      expect(widest).toBeGreaterThan(100);
      expect(widest).toBeLessThanOrEqual(151.2);
      expect(foreign).toEqual({ foreign: [], errors: [] });
    });
  }

  test('a tap on a thumb opens the full file from the same origin', async ({ page }) => {
    await page.setViewportSize(SIZES[0]);
    await open(page, '');
    const link = page.locator('.parts li.pic a').first();
    await expect(link).toHaveAttribute('href', 'pictures/01-panel-back.webp');
    const response = await page.request.get(new URL('pictures/01-panel-back.webp', page.url()).href);
    expect(response.status()).toBe(200);
    expect(response.headers()['content-type']).toBe('image/webp');
    await link.focus();
    await page.keyboard.press('Enter');
    await page.waitForURL('**/rcbench/pictures/01-panel-back.webp');
  });
});

test.describe('German browser', () => {
  test.use({ viewport: SIZES[0], locale: 'de-DE' });

  test('the page starts in German and writes l=en for English', async ({ page }) => {
    await open(page, '');
    await expect(page.locator('html')).toHaveAttribute('lang', 'de');
    await expect(page.locator('#h-choose')).toHaveText('Was soll der Prüfstand tun?');
    await expect(page).toHaveTitle(STRINGS.title[1]);
    await expect(page.locator('h1')).toHaveText(STRINGS.title[1]);
    await page.locator('.lang button[data-lang=en]').click();
    await expect(page.locator('html')).toHaveAttribute('lang', 'en');
    await expect(page).toHaveTitle(STRINGS.title[0]);
    await expect(page.locator('h1')).toHaveText(STRINGS.title[0]);
    await expect(page).toHaveURL(/#v=1&l=en$/);
  });
});

test('the bottom bar jumps without changing the hash, and the pan box opens on the panel wires', async ({ page }) => {
  await page.setViewportSize(SIZES[0] as { width: number; height: number });
  await open(page, '#v=1&f=servo');
  await expect(page.locator('#panhint')).toBeVisible();
  const pan = await page.evaluate(() => {
    const el = document.getElementById('pan') as HTMLElement;
    return { left: el.scrollLeft, width: el.clientWidth };
  });
  expect(pan.left).toBeGreaterThan(100);
  expect(pan.left + pan.width).toBeGreaterThan(432);
  await page.locator('#bar a[data-jump=result]').click();
  await expect.poll(() => page.evaluate(() => window.scrollY)).toBeGreaterThan(300);
  await expect(page).toHaveURL(/#v=1&f=servo$/);
  await page.locator('#bar a[data-jump=choose]').click();
  await expect.poll(() => page.evaluate(() => window.scrollY)).toBeLessThan(120);
  await page.setViewportSize(SIZES[1] as { width: number; height: number });
  await expect(page.locator('#bar')).toBeHidden();
  await expect(page.locator('#panhint')).toBeHidden();
});
