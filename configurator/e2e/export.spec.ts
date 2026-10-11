// The PDF export of the built page under `vite preview`: the download at phone
// and desktop width in both languages, the separate chunk, and the failure paths.

import { readFileSync } from 'node:fs';
import { expect, test } from '@playwright/test';
import type { Download, Page } from '@playwright/test';
import { getDocument, VerbosityLevel } from 'pdfjs-dist/legacy/build/pdf.mjs';
import { CATALOGUE } from '../src/core.ts';
import { STRINGS } from '../src/i18n.ts';

const SIZES = [{ width: 360, height: 800 }, { width: 1280, height: 800 }];
const LANGS = ['en', 'de'] as const;
type Lang = (typeof LANGS)[number];
// The ticks of the page: a function that needs no part has no tick and is in no link.
const FUNCTIONS = CATALOGUE.functions.filter((f) => f.needs.length > 0).map((f) => f.id);
const OWNED = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const FULL = `#v=1&f=${FUNCTIONS.join(',')}&p=${OWNED.join(',')}&o=osoyoo,xt90,pd`;
const NAME = /^rcbench-setup-[0-9a-f]{8}-(en|de)\.pdf$/;
const CHUNK = /\/assets\/setup-pdf-[\w-]+\.js$/;
const say = (lang: Lang, key: keyof typeof STRINGS): string => (STRINGS[key] as readonly [string, string])[lang === 'de' ? 1 : 0];

type Watch = { foreign: string[]; errors: string[]; chunk: string[]; requests: string[] };

/** Records every request, the ones to another origin, the ones for the export chunk, and every error. */
function watch(page: Page): Watch {
  const seen: Watch = { foreign: [], errors: [], chunk: [], requests: [] };
  page.on('request', (r) => {
    const url = new URL(r.url());
    if (url.protocol === 'data:' || url.protocol === 'blob:') return;
    seen.requests.push(r.url());
    if (url.origin !== 'http://localhost:4173') seen.foreign.push(r.url());
    if (CHUNK.test(url.pathname)) seen.chunk.push(r.url());
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
  // The schematic is in the page behind the view switch; the picture is the first view.
  await page.locator('svg.drawing').waitFor({ state: 'attached' });
}

/** Moves the focus to the PDF button with the Tab key alone. */
async function tabToPdf(page: Page): Promise<void> {
  for (let i = 0; i < 400; i++) {
    await page.keyboard.press('Tab');
    if (await page.evaluate(() => document.activeElement?.id === 'b-pdf')) return;
  }
  throw new Error('The Tab key does not reach the PDF button.');
}

type Parsed = { bytes: Buffer; pages: number; text: string; lang: string; title: string };

async function parse(download: Download): Promise<Parsed> {
  const bytes = readFileSync(await download.path());
  const task = getDocument({ data: new Uint8Array(bytes), verbosity: VerbosityLevel.ERRORS, useSystemFonts: false, disableFontFace: true });
  const pdf = await task.promise;
  try {
    let text = '';
    for (let n = 1; n <= pdf.numPages; n++) {
      const content = await (await pdf.getPage(n)).getTextContent();
      text += `${content.items.map((item) => ('str' in item ? item.str : '')).join(' ')}\n`;
    }
    const info = (await pdf.getMetadata()).info as { Title?: string; Language?: string };
    return { bytes, pages: pdf.numPages, text, lang: info.Language ?? '', title: info.Title ?? '' };
  } finally {
    await task.destroy();
  }
}

/** Downloads that start within the wait after the action. */
async function downloadsOf(page: Page, act: () => Promise<void>, wait = 1500): Promise<Download[]> {
  const got: Download[] = [];
  const take = (d: Download): void => void got.push(d);
  page.on('download', take);
  await act();
  await expect.poll(() => got.length, { timeout: 15_000 }).toBeGreaterThan(0);
  await page.waitForTimeout(wait);
  page.off('download', take);
  return got;
}

for (const size of SIZES) {
  for (const lang of LANGS) {
    test.describe(`${size.width} px, ${lang}`, () => {
      test.use({ viewport: size, locale: lang === 'de' ? 'de-DE' : 'en-GB' });

      test('the keyboard alone downloads the PDF of the full selection, and the chunk loads on the first use', async ({ page }) => {
        const seen = watch(page);
        await open(page, `${FULL}&l=${lang}`);
        await expect(page.locator('html')).toHaveAttribute('lang', lang);
        const button = page.locator('#b-pdf');
        await expect(button).toHaveText(say(lang, 'pdf'));
        await expect(button).toHaveJSProperty('tagName', 'BUTTON');
        expect((await button.boundingBox())?.height).toBeGreaterThanOrEqual(44);
        await expect(page.locator('#pdfmsg')).toBeHidden();
        // The idle page has made every request of the first load.
        await page.waitForLoadState('networkidle');
        expect(seen.chunk).toEqual([]);
        const html = await (await page.request.get('./')).text();
        expect(html).not.toContain('setup-pdf');

        await tabToPdf(page);
        const before = seen.requests.length;
        const waiting = page.waitForEvent('download');
        await page.keyboard.press('Enter');
        const download = await waiting;
        expect(download.suggestedFilename()).toMatch(NAME);
        expect(download.suggestedFilename().endsWith(`-${lang}.pdf`)).toBe(true);

        const pdf = await parse(download);
        expect(pdf.bytes.length).toBeGreaterThan(20_000);
        expect(pdf.bytes.subarray(0, 8).toString('latin1')).toBe('%PDF-1.7');
        expect(pdf.bytes.subarray(-6).toString('latin1')).toBe('%%EOF\n');
        expect(pdf.pages).toBeGreaterThanOrEqual(4);
        expect(pdf.lang).toBe(lang);
        expect(pdf.title).toContain('rcbench');
        expect(pdf.text).toContain(lang === 'de' ? `Seite 1 von ${pdf.pages}` : `Page 1 of ${pdf.pages}`);
        // The share link of the page is in the document.
        expect(pdf.text.replace(/\s+/g, '')).toContain(new URL(page.url()).hash.replace(/\s+/g, ''));

        // The status line names the file and keeps it reachable; the focus stays on the button.
        const link = page.locator('#pdflink');
        await expect(page.locator('#pdfmsg')).toContainText(say(lang, 'pdfDone'));
        await expect(link).toHaveAttribute('download', download.suggestedFilename());
        await expect(link).toHaveText(say(lang, 'pdfSave').replace('{x}', download.suggestedFilename()));
        await expect(button).toBeFocused();
        await expect(button).not.toHaveAttribute('aria-disabled', 'true');

        // The export asks for its chunk and nothing else.
        expect(seen.chunk.length).toBe(1);
        // A part photo below the fold loads when the focus scrolls it into view; that is no request of the export.
        const other = seen.requests.slice(before).filter((url) => !CHUNK.test(new URL(url).pathname) && !new URL(url).pathname.startsWith('/rcbench/pictures/'));
        expect(other).toEqual([]);
        expect(seen.foreign).toEqual([]);
        expect(seen.errors).toEqual([]);
        const [scroll, width] = await page.evaluate(() => [document.documentElement.scrollWidth, window.innerWidth]);
        expect(scroll).toBeLessThanOrEqual(width as number);

        // The link of the status line gives the same file again, by keyboard.
        await page.keyboard.press('Tab');
        for (let i = 0; i < 20 && !(await page.evaluate(() => document.activeElement?.id === 'pdflink')); i++) await page.keyboard.press('Tab');
        await expect(link).toBeFocused();
        const again = page.waitForEvent('download');
        await page.keyboard.press('Enter');
        expect((await parse(await again)).bytes.equals(pdf.bytes)).toBe(true);
        expect(seen.chunk.length).toBe(1);
      });
    });
  }
}

test.describe('PDF export, failure paths and limits', () => {
  test.use({ viewport: SIZES[0], locale: 'en-GB' });

  test('the base setup without a tick exports', async ({ page }) => {
    const seen = watch(page);
    await open(page, '');
    const waiting = page.waitForEvent('download');
    await page.locator('#b-pdf').click();
    const pdf = await parse(await waiting);
    expect(pdf.pages).toBeGreaterThanOrEqual(2);
    expect(pdf.lang).toBe('en');
    expect(seen.errors).toEqual([]);
    expect(seen.foreign).toEqual([]);
  });

  test('the link in the PDF holds no id of a function without a tick', async ({ page }) => {
    const seen = watch(page);
    const base = CATALOGUE.functions.filter((f) => f.needs.length === 0);
    expect(base.length).toBeGreaterThan(0);
    // A link that names every function: the page keeps its address until a change.
    const opened = `#v=1&f=${CATALOGUE.functions.map((f) => f.id).join(',')}`;
    await open(page, opened);
    expect(new URL(page.url()).hash).toBe(opened);
    const waiting = page.waitForEvent('download');
    await page.locator('#b-pdf').click();
    const pdf = await parse(await waiting);
    const text = pdf.text.replace(/\s+/g, '');
    expect(text).toContain(`#v=1&f=${FUNCTIONS.join(',')}`);
    expect(text).not.toContain(opened);
    // The document lists each of these functions as enabled.
    for (const f of base) expect(pdf.text.replace(/\s+/g, ' ')).toContain(f.name.en);
    // The same setup without these ids in the link gives the same file name.
    const first = (await waiting).suggestedFilename();
    await open(page, `#v=1&f=${FUNCTIONS.join(',')}`);
    const again = page.waitForEvent('download');
    await page.locator('#b-pdf').click();
    expect((await again).suggestedFilename()).toBe(first);
    expect(seen.errors).toEqual([]);
    expect(seen.foreign).toEqual([]);
  });

  test('a double click and clicks during the export start 1 download', async ({ page }) => {
    const seen = watch(page);
    await open(page, FULL);
    // The chunk waits for the release, so every click falls into the running export.
    let release = (): void => {};
    const held = new Promise<void>((done) => {
      release = done;
    });
    await page.route(CHUNK, async (route) => {
      await held;
      await route.continue();
    });
    const button = page.locator('#b-pdf');
    const got = await downloadsOf(page, async () => {
      await button.dblclick();
      await expect(button).toHaveAttribute('aria-disabled', 'true');
      await expect(page.locator('#pdfmsg')).toHaveText(say('en', 'pdfBusy'));
      // The busy button stays focusable and takes the events; `force` skips the wait for an enabled button.
      await button.click({ force: true });
      await button.focus();
      await page.keyboard.press('Enter');
      await page.keyboard.press('Space');
      await expect(button).toBeFocused();
      await expect(button).toHaveAttribute('aria-disabled', 'true');
      release();
    });
    expect(got.length).toBe(1);
    expect(seen.chunk.length).toBe(1);
    await expect(button).not.toHaveAttribute('aria-disabled', 'true');
    // The next click after the export starts the next download.
    const next = page.waitForEvent('download');
    await button.click();
    expect((await next).suggestedFilename()).toMatch(NAME);
    expect(seen.errors).toEqual([]);
  });

  for (const lang of LANGS) {
    test(`${lang}: a chunk that does not load gives the message, and the page stays usable`, async ({ page }) => {
      await open(page, `${FULL}&l=${lang}`);
      await page.route(CHUNK, (route) => route.abort());
      const button = page.locator('#b-pdf');
      const msg = page.locator('#pdfmsg');
      let downloads = 0;
      page.on('download', () => downloads++);
      await button.click();
      await expect(msg).toHaveText(say(lang, 'pdfFail'));
      await expect(msg).toHaveClass(/\berr\b/);
      await expect(msg).toHaveAttribute('role', 'status');
      await expect(button).not.toHaveAttribute('aria-disabled', 'true');
      expect(downloads).toBe(0);
      // The message does not rest on colour alone: it has a border, and its text states the failure.
      expect(await msg.evaluate((el) => parseFloat(getComputedStyle(el).borderLeftWidth))).toBeGreaterThan(0);
      const [scroll, width] = await page.evaluate(() => [document.documentElement.scrollWidth, window.innerWidth]);
      expect(scroll).toBeLessThanOrEqual(width as number);

      // The page works on: a tick changes the hash, the drawing and clears the message.
      const hash = new URL(page.url()).hash;
      await page.locator('input[type=checkbox]:not([disabled])').first().click();
      await expect.poll(() => new URL(page.url()).hash).not.toBe(hash);
      await expect(page.locator('svg.drawing')).toBeAttached();
      await expect(msg).toBeHidden();

      // With the chunk reachable again the same button makes the file.
      await page.unroute(CHUNK);
      const waiting = page.waitForEvent('download');
      await button.click();
      expect((await waiting).suggestedFilename()).toMatch(NAME);
      await expect(msg).toContainText(say(lang, 'pdfDone'));
    });
  }

  test('offline: the second export works, a first export gives the message', async ({ page, context }) => {
    await open(page, FULL);
    const button = page.locator('#b-pdf');
    const first = page.waitForEvent('download');
    await button.click();
    await first;
    await context.setOffline(true);
    const second = page.waitForEvent('download');
    await button.click();
    const pdf = await parse(await second);
    expect(pdf.pages).toBeGreaterThanOrEqual(4);
    // Another setup, still offline.
    await page.locator('.lang button[data-lang=de]').click();
    const third = page.waitForEvent('download');
    await button.click();
    expect((await third).suggestedFilename()).toMatch(/-de\.pdf$/);

    // A page that never loaded the chunk has no export while offline.
    await context.setOffline(false);
    await open(page, FULL);
    await page.waitForLoadState('networkidle');
    await context.setOffline(true);
    await button.click();
    await expect(page.locator('#pdfmsg')).toHaveText(say('en', 'pdfFail'));
    await context.setOffline(false);
  });

  test('the finished file belongs to one setup in one language', async ({ page }) => {
    await open(page, FULL);
    const button = page.locator('#b-pdf');
    const msg = page.locator('#pdfmsg');
    const first = page.waitForEvent('download');
    await button.click();
    const en = (await first).suggestedFilename();
    await expect(page.locator('#pdflink')).toHaveCount(1);
    const href = await page.locator('#pdflink').getAttribute('href');
    expect(href).toMatch(/^blob:/);

    // Another language: the link goes and its file is released.
    await page.locator('.lang button[data-lang=de]').click();
    await expect(msg).toBeHidden();
    await expect(page.locator('#pdflink')).toHaveCount(0);
    expect(await page.evaluate((url) => fetch(url).then(() => true, () => false), href as string)).toBe(false);
    const second = page.waitForEvent('download');
    await button.click();
    const de = (await second).suggestedFilename();
    expect(de).toBe(en.replace('-en.pdf', '-de.pdf'));

    // Another setup: another name.
    await page.locator('.lang button[data-lang=en]').click();
    await page.locator('input[type=checkbox]:not([disabled])').first().click();
    await expect(msg).toBeHidden();
    const third = page.waitForEvent('download');
    await button.click();
    const other = (await third).suggestedFilename();
    expect(other).toMatch(NAME);
    expect(other).not.toBe(en);
  });

  test('the light and the dark scheme give the same bytes for the same setup and date', async ({ page }) => {
    await page.clock.setFixedTime(new Date('2026-03-04T12:00:00Z'));
    const files: Buffer[] = [];
    for (const colorScheme of ['light', 'dark'] as const) {
      await page.emulateMedia({ colorScheme });
      await open(page, FULL);
      const waiting = page.waitForEvent('download');
      await page.locator('#b-pdf').click();
      const pdf = await parse(await waiting);
      expect(pdf.text).toContain('2026-03-04');
      files.push(pdf.bytes);
      // The status line is readable in the scheme: its text differs from its background.
      const [ink, paper] = await page.locator('#pdfmsg').evaluate((el) => [getComputedStyle(el).color, getComputedStyle(document.body).backgroundColor]);
      expect(ink).not.toBe(paper);
    }
    expect((files[0] as Buffer).equals(files[1] as Buffer)).toBe(true);
  });

  test('the print view hides the button and the status line and keeps the drawing', async ({ page }) => {
    await open(page, FULL);
    const waiting = page.waitForEvent('download');
    await page.locator('#b-pdf').click();
    await waiting;
    await expect(page.locator('#pdfmsg')).toBeVisible();
    await page.emulateMedia({ media: 'print' });
    await expect(page.locator('#b-pdf')).toBeHidden();
    await expect(page.locator('#pdfmsg')).toBeHidden();
    await expect(page.locator('svg.drawing')).toBeVisible();
    await expect(page.locator('table.wires')).toBeVisible();
  });

  test('reduced motion: the export shows no animation', async ({ page }) => {
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await open(page, FULL);
    const waiting = page.waitForEvent('download');
    await page.locator('#b-pdf').click();
    await waiting;
    const moving = await page.evaluate(() => document.getAnimations().length);
    expect(moving).toBe(0);
  });
});
