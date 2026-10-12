// The state the browser keeps between visits, against the built page under
// `vite preview`: the selection and the language come back on a reload and
// on a visit without a hash, a change of language keeps the setup, a link
// wins over the stored state, and a store that is blocked, full or holds
// another shape leaves the page at work.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import type { Lang, Selection } from '../contract.ts';
import { CATALOGUE, removalEffect } from '../src/core.ts';
import { formatHash } from '../src/hash.ts';
import { STORE_KEY, STORE_VERSION } from '../src/store.ts';
import { SEL, expectSetup, hashOf, openHash, readSetup, selectionOf, stateOf } from './lib/setup.ts';
import { expectQuiet, watchPage } from './lib/watch.ts';

const other = (lang: Lang): Lang => (lang === 'en' ? 'de' : 'en');
/** The stored text of a selection: its hash without '#' and without the language key. */
const setupOf = (selection: Selection): string => formatHash(selection, 'en', 'en', CATALOGUE);
const BASE = selectionOf();
/** 2 test objects, 1 function that brings parts, 1 owned part and 1 variant away from its default. */
const BUILT = selectionOf({ functions: ['servo', 'esc', 'supply'], owned: ['ina228'], options: { connection: 'osoyoo' } });

/** What the browser holds under the key of the page, parsed, or null. */
async function stored(page: Page): Promise<unknown> {
  return page.evaluate((key) => {
    const raw = localStorage.getItem(key);
    return raw === null ? null : JSON.parse(raw) as unknown;
  }, STORE_KEY);
}

/** Puts a raw value under the key of the page. The page is on its origin. */
async function put(page: Page, raw: string): Promise<void> {
  await page.evaluate(([key, value]) => localStorage.setItem(key as string, value as string), [STORE_KEY, raw]);
}

/** A visit by the address without a hash: the README link, a bookmark of the page. */
async function visit(page: Page): Promise<void> {
  await openHash(page, '');
}

/** Ticks `BUILT` on the page with the controls a visitor uses. */
async function build(page: Page): Promise<void> {
  for (const id of BUILT.functions) await page.locator(SEL.fn(id)).check();
  await page.locator(SEL.ownSummary).click();
  for (const id of BUILT.owned) await page.locator(SEL.owned(id)).check();
  await page.locator(SEL.option('connection', BUILT.options.connection)).check();
}

for (const lang of ['en', 'de'] as Lang[]) {
  test.describe(`browser language ${lang}`, () => {
    test.use({ locale: lang === 'de' ? 'de-DE' : 'en-GB', viewport: { width: 1280, height: 800 } });

    test('a built setup comes back on a reload, on a visit without a hash and in a second tab', async ({ page, context }) => {
      const seen = watchPage(page);
      await visit(page);
      expect(await stored(page)).toBeNull();
      await build(page);
      const built = await expectSetup(page, BUILT);
      expect(built.hash).toBe(hashOf(BUILT));
      expect(await stored(page)).toEqual({ v: STORE_VERSION, lang: null, setup: setupOf(BUILT) });

      await page.reload();
      await page.locator(SEL.picture).waitFor();
      expect(await expectSetup(page, BUILT)).toEqual(built);

      await visit(page);
      // The address names the setup again: the link to copy and the PDF read it there.
      expect(await expectSetup(page, BUILT)).toEqual(built);

      const second = await context.newPage();
      await second.goto('./');
      await second.locator(SEL.picture).waitFor();
      expect(await expectSetup(second, BUILT)).toEqual(built);
      await second.close();
      expectQuiet(seen);
    });

    test('3 changes of language and back keep the setup, and the language comes back with it', async ({ page }) => {
      const seen = watchPage(page);
      await visit(page);
      await build(page);
      const built = await expectSetup(page, BUILT);
      expect(built.lang).toBe(lang);

      for (let round = 0; round < 3; round += 1) {
        await page.locator(SEL.lang(other(lang))).click();
        await expect(page.locator('html')).toHaveAttribute('lang', other(lang));
        const switched = await expectSetup(page, BUILT);
        expect(stateOf(switched)).toEqual(stateOf(built));
        expect(switched.notices).toEqual([]);
        expect(switched.hash).toBe(hashOf(BUILT, other(lang)));
        // The stored selection is the same text in both languages.
        expect(await stored(page)).toEqual({ v: STORE_VERSION, lang: other(lang), setup: setupOf(BUILT) });

        // The other language holds over a reload and a visit without a hash.
        await page.reload();
        await page.locator(SEL.picture).waitFor();
        expect(await expectSetup(page, BUILT)).toEqual(switched);
        await visit(page);
        expect(await expectSetup(page, BUILT)).toEqual(switched);

        await page.locator(SEL.lang(lang)).click();
        await expect(page.locator('html')).toHaveAttribute('lang', lang);
        expect(await expectSetup(page, BUILT)).toEqual(built);
        expect(await stored(page)).toEqual({ v: STORE_VERSION, lang, setup: setupOf(BUILT) });
        await page.reload();
        await page.locator(SEL.picture).waitFor();
        expect(await expectSetup(page, BUILT)).toEqual(built);
        await visit(page);
        expect(await expectSetup(page, BUILT)).toEqual(built);
      }
      expectQuiet(seen);
    });

    test('a change in the other language is stored and shown in the first', async ({ page }) => {
      await visit(page);
      await page.locator(SEL.lang(other(lang))).click();
      await build(page);
      const built = await expectSetup(page, BUILT);
      expect(built.lang).toBe(other(lang));
      await page.locator(SEL.lang(lang)).click();
      await visit(page);
      const back = await expectSetup(page, BUILT);
      expect(back.lang).toBe(lang);
      expect(stateOf(back)).toEqual(stateOf(built));
      expect(back.hash).toBe(hashOf(BUILT));
    });

    test('a link wins over the stored setup and stores nothing until a change', async ({ page }) => {
      const seen = watchPage(page);
      const linked = selectionOf({ functions: ['angle'], options: { escPower: 'pd' } });
      await visit(page);
      await build(page);
      const kept = { v: STORE_VERSION, lang: null, setup: setupOf(BUILT) };

      await openHash(page, hashOf(linked));
      expect((await expectSetup(page, linked)).hash).toBe(hashOf(linked));
      expect(await stored(page)).toEqual(kept);
      // A link to the base setup is a link as well.
      await openHash(page, hashOf(BASE));
      expect((await expectSetup(page, BASE)).hash).toBe(hashOf(BASE));
      // A link of another version gives the base setup and its notice.
      await openHash(page, '#v=2&f=servo');
      expect((await expectSetup(page, BASE)).notices.map((n) => n.kind)).toEqual(['err']);
      expect(await stored(page)).toEqual(kept);

      // The language of a link wins over the stored one and is not stored.
      await openHash(page, hashOf(linked, other(lang)));
      expect((await expectSetup(page, linked)).lang).toBe(other(lang));
      expect(await stored(page)).toEqual(kept);
      await visit(page);
      expect((await expectSetup(page, BUILT)).lang).toBe(lang);

      // A change on the page of a link stores the setup of the page.
      await openHash(page, hashOf(linked));
      await page.locator(SEL.fn('servo')).check();
      const changed = { ...linked, functions: ['servo', ...linked.functions] };
      await expectSetup(page, changed);
      expect(await stored(page)).toEqual({ ...kept, setup: setupOf(changed) });
      await visit(page);
      await expectSetup(page, changed);
      expectQuiet(seen);
    });

    test('a hash with the language alone shows the stored setup in that language', async ({ page }) => {
      await visit(page);
      await build(page);
      await openHash(page, `#l=${other(lang)}`);
      const shown = await expectSetup(page, BUILT);
      expect(shown.lang).toBe(other(lang));
      expect(shown.hash).toBe(hashOf(BUILT, other(lang)));
      expect(await stored(page)).toEqual({ v: STORE_VERSION, lang: null, setup: setupOf(BUILT) });
    });

    test('a link without a language opens in the stored language and keeps its address', async ({ page }) => {
      await visit(page);
      await page.locator(SEL.lang(other(lang))).click();
      expect(await stored(page)).toEqual({ v: STORE_VERSION, lang: other(lang), setup: null });
      const link = '#v=1&f=servo,warp';
      await openHash(page, link);
      const shown = await expectSetup(page, selectionOf({ functions: ['servo'] }));
      expect({ lang: shown.lang, hash: shown.hash, notices: shown.notices.map((n) => n.kind) }).toEqual({ lang: other(lang), hash: link, notices: ['note'] });
      // Without a link the address names the stored language.
      await visit(page);
      const bare = await expectSetup(page, BASE);
      expect({ lang: bare.lang, hash: bare.hash }).toEqual({ lang: other(lang), hash: hashOf(BASE, other(lang)) });
    });

    test('the reset and the removal of a part are stored', async ({ page }) => {
      await visit(page);
      await build(page);
      const effect = removalEffect(CATALOGUE, BUILT, 'ina228');
      expect(effect.refused).toBeNull();
      await page.locator(SEL.remove('ina228')).click();
      await page.locator(SEL.yes).click();
      await expectSetup(page, effect.selection);
      await visit(page);
      await expectSetup(page, effect.selection);

      await page.locator(SEL.reset).click();
      await expectSetup(page, BASE);
      expect(await stored(page)).toEqual({ v: STORE_VERSION, lang: null, setup: setupOf(BASE) });
      await visit(page);
      const base = await expectSetup(page, BASE);
      // The base setup is the page without a hash.
      expect(base.hash).toBe('');
    });

    test('a stored value of another shape gives the base setup in the browser language', async ({ page }) => {
      const seen = watchPage(page);
      await visit(page);
      for (const raw of [
        '', '{', 'null', '[]', '"v=1&f=servo"',
        JSON.stringify({ v: 0, lang: other(lang), setup: setupOf(BUILT) }),
        JSON.stringify({ v: 2, lang: other(lang), setup: setupOf(BUILT) }),
        JSON.stringify({ lang: other(lang), functions: BUILT.functions, owned: BUILT.owned, options: BUILT.options }),
        JSON.stringify({ v: 1, lang: 'fr', setup: 'v=2&f=servo' }),
        JSON.stringify({ v: 1, lang: 7, setup: { f: 'servo' } }),
        JSON.stringify({ v: 1, lang: null, setup: `v=1&f=${'a'.repeat(2001)}` }),
        `{"v":1,"lang":"${other(lang)}","setup":"${setupOf(BUILT)}","pad":"${'x'.repeat(4096)}"}`,
      ]) {
        await put(page, raw);
        await visit(page);
        const shown = await expectSetup(page, BASE);
        expect({ lang: shown.lang, hash: shown.hash, notices: shown.notices }, raw.slice(0, 60)).toEqual({ lang, hash: '', notices: [] });
        // The page works on: a tick is stored in the shape of this version.
        await page.locator(SEL.fn('servo')).check();
        await expectSetup(page, selectionOf({ functions: ['servo'] }));
        expect(await stored(page)).toEqual({ v: STORE_VERSION, lang: null, setup: setupOf(selectionOf({ functions: ['servo'] })) });
      }
      expectQuiet(seen);
    });

    test('a stored setup with ids the catalogue does not know shows the ids it knows', async ({ page }) => {
      const seen = watchPage(page);
      await visit(page);
      // An id of another catalogue, a function every setup runs, a part with no tick, an unknown variant.
      const base = CATALOGUE.functions.find((f) => f.needs.length === 0 && f.drives === undefined)!.id;
      await put(page, JSON.stringify({ v: 1, lang: other(lang), setup: `v=1&f=servo,gone,${base},supply&p=nopart,ina228&o=osoyoo,notoken` }));
      await visit(page);
      const want = selectionOf({ functions: ['servo', 'supply'], owned: ['ina228'], options: { connection: 'osoyoo' } });
      const shown = await expectSetup(page, want);
      expect({ lang: shown.lang, hash: shown.hash, notices: shown.notices }).toEqual({ lang: other(lang), hash: hashOf(want, other(lang)), notices: [] });

      // No id of the catalogue at all: the base setup, in the stored language.
      await put(page, JSON.stringify({ v: 1, lang: other(lang), setup: 'v=1&f=gone&p=nopart' }));
      await visit(page);
      const none = await expectSetup(page, BASE);
      expect({ lang: none.lang, hash: none.hash, notices: none.notices }).toEqual({ lang: other(lang), hash: hashOf(BASE, other(lang)), notices: [] });
      expectQuiet(seen);
    });

    for (const fault of ['blocked', 'full'] as const) {
      test(`a ${fault} store leaves the page at work, with the setup in the hash`, async ({ page }) => {
        const seen = watchPage(page);
        await page.addInitScript((kind) => {
          const refuse = (): never => { throw new DOMException('refused', kind === 'full' ? 'QuotaExceededError' : 'SecurityError'); };
          if (kind === 'blocked') Object.defineProperty(window, 'localStorage', { configurable: true, get: refuse });
          else Storage.prototype.setItem = refuse;
        }, fault);
        await visit(page);
        await build(page);
        const built = await expectSetup(page, BUILT);
        expect(built.hash).toBe(hashOf(BUILT));
        await page.locator(SEL.lang(other(lang))).click();
        const switched = await expectSetup(page, BUILT);
        expect(switched.lang).toBe(other(lang));
        expect(switched.hash).toBe(hashOf(BUILT, other(lang)));
        await page.reload();
        await page.locator(SEL.picture).waitFor();
        expect(await expectSetup(page, BUILT)).toEqual(switched);
        // Nothing is stored: a visit without a hash gives the base setup in the browser language.
        await visit(page);
        const base = await readSetup(page);
        await expectSetup(page, BASE);
        expect({ lang: base.lang, hash: base.hash }).toEqual({ lang, hash: '' });
        expectQuiet(seen);
      });
    }
  });
}
