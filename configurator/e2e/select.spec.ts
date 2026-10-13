// The chooser of the built page under `vite preview`: groups by needed bench
// hardware, the words for the state of a function, the test objects under
// their own heading, and the removal of a part by keyboard.

import { expect, test } from '@playwright/test';
import { pageOrigin } from './lib/watch.ts';
import type { Page } from '@playwright/test';
import type { Lang, OptionKey } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, groups, resolve, alwaysOn } from '../src/core.ts';
import { STRINGS, t } from '../src/i18n.ts';
import { variantShort } from '../src/view/chooser.ts';

const SIZES = [{ width: 360, height: 800 }, { width: 1280, height: 800 }];
const LANGS: Lang[] = ['en', 'de'];
const GROUPS = groups(CATALOGUE);
/** The functions the base setup runs. They take no tick and are in no link. */
const ON_BASE = CATALOGUE.functions.filter((f) => alwaysOn(CATALOGUE, f)).map((f) => f.id);
/** The functions with a tick. */
const FUNCTIONS = CATALOGUE.functions.filter((f) => !alwaysOn(CATALOGUE, f)).map((f) => f.id);
/** The devices a visitor connects and tests. They are no hardware of the bench. */
const OBJECTS = CATALOGUE.parts.filter((p) => p.testObject).map((p) => p.id);
/** The functions with a tick that need no hardware beyond the base setup: each connects a test object. */
const PLAIN = CATALOGUE.functions.filter((f) => f.needs.length === 0 && f.drives !== undefined).map((f) => f.id);
const enabledCount = (functions: string[], owned: string[] = []): number =>
  resolve(CATALOGUE, { functions, owned, options: { ...DEFAULT_OPTIONS } }).functions.filter((f) => f.enabled).length;
const escapeRe = (s: string): string => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
/** The end of the summary line for a selection: "13 functions." */
const sumEnd = (functions: string[], owned: string[] = []): RegExp => {
  const f = enabledCount(functions, owned);
  return new RegExp(`\\d+ parts, \\d+ wires, ${escapeRe(t('en', f === 1 ? 'sum1' : 'sum', { p: 0, w: 0, f }).split(', ').pop() as string)}$`);
};
const OWNED = CATALOGUE.parts.filter((p) => p.selectable).map((p) => p.id);
const FULL = `#v=1&f=${FUNCTIONS.join(',')}&p=${OWNED.join(',')}&o=osoyoo,xt90,pd`;

const col = (lang: Lang): 0 | 1 => (lang === 'en' ? 0 : 1);
const short = (id: string, lang: Lang = 'en'): string => CATALOGUE.parts.find((p) => p.id === id)!.short[lang];
const fnName = (id: string, lang: Lang = 'en'): string => CATALOGUE.functions.find((f) => f.id === id)!.name[lang];
const groupId = (parts: string[]): string => (parts.length === 0 ? 'g-base' : `g-${parts.join('-')}`);

type Watch = { foreign: string[]; errors: string[] };

/** Records every request to another origin, every page error and every console error. */
function watch(page: Page): Watch {
  const seen: Watch = { foreign: [], errors: [] };
  page.on('request', (r) => {
    const url = new URL(r.url());
    if (url.protocol !== 'data:' && url.origin !== pageOrigin()) seen.foreign.push(r.url());
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
  // The schematic is in the page behind the view switch; the picture is the first view.
  await page.locator('svg.drawing').waitFor({ state: 'attached' });
}

async function fits(page: Page): Promise<void> {
  const [scroll, width] = await page.evaluate(() => [document.documentElement.scrollWidth, window.innerWidth]);
  expect(scroll).toBeLessThanOrEqual(width as number);
}

const focused = (page: Page): Promise<string> => page.evaluate(() => document.activeElement?.id ?? '');

/** Moves the focus to the control `id` with the Tab key alone, from the top of the page. */
async function tabTo(page: Page, id: string): Promise<void> {
  await page.evaluate(() => {
    (document.activeElement as HTMLElement | null)?.blur();
    window.getSelection()?.removeAllRanges();
    window.scrollTo(0, 0);
  });
  for (let i = 0; i < 400; i++) {
    await page.keyboard.press('Tab');
    if (await focused(page) === id) return;
  }
  throw new Error(`Tab does not reach #${id}`);
}

/** Opens "Parts you own" with the keyboard. */
async function openOwned(page: Page): Promise<void> {
  await tabTo(page, 'own-summary');
  await page.keyboard.press('Enter');
  await expect(page.locator('#own')).toHaveAttribute('open', '');
}

/** WCAG contrast ratio of the text colour of `selector` against the nearest opaque background. */
async function contrast(page: Page, selector: string): Promise<number> {
  return page.evaluate((sel) => {
    const rgb = (value: string): number[] => (value.match(/[\d.]+/g) ?? []).map(Number);
    const luminance = (c: number[]): number => {
      const [r, g, b] = c.slice(0, 3).map((v) => {
        const s = v / 255;
        return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
      }) as [number, number, number];
      return 0.2126 * r + 0.7152 * g + 0.0722 * b;
    };
    const el = document.querySelector<HTMLElement>(sel);
    if (!el) return 0;
    let back: number[] = [255, 255, 255];
    for (let node: HTMLElement | null = el; node; node = node.parentElement) {
      const c = rgb(getComputedStyle(node).backgroundColor);
      if (c.length === 3 || (c.length === 4 && c[3] === 1)) {
        back = c;
        break;
      }
    }
    const [a, b] = [luminance(rgb(getComputedStyle(el).color)), luminance(back)];
    return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
  }, selector);
}

for (const size of SIZES) {
  test.describe(`${size.width} px`, () => {
    test.use({ viewport: size, locale: 'en-GB' });

    test('the functions stand in groups by the bench hardware they need, in English and German', async ({ page }) => {
      const seen = watch(page);
      expect(GROUPS.flatMap((g) => g.functions).sort()).toEqual([...ON_BASE, ...FUNCTIONS].sort());
      expect([...(GROUPS[0]?.functions ?? [])].sort()).toEqual([...ON_BASE, ...PLAIN].sort());
      expect(PLAIN).toEqual(['servo', 'servoprog', 'esc']);
      expect(OBJECTS).toEqual(['servo', 'esc']);
      // No group is keyed by a test object.
      expect(GROUPS.flatMap((g) => g.parts).filter((id) => OBJECTS.includes(id))).toEqual([]);
      for (const lang of LANGS) {
        await open(page, lang === 'en' ? '' : '#v=1&l=de');
        await expect(page.locator('html')).toHaveAttribute('lang', lang);
        await expect(page.locator('fieldset.grp')).toHaveCount(GROUPS.length);
        expect(await page.locator('fieldset.grp').evaluateAll((list) => list.map((el) => el.id))).toEqual(GROUPS.map((g) => groupId(g.parts)));
        for (const group of GROUPS) {
          const box = page.locator(`#${groupId(group.parts)}`);
          const heading = group.parts.length === 0
            ? STRINGS.gBase[col(lang)]
            : STRINGS.gNeeds[col(lang)] + group.parts.map((id) => short(id, lang)).join(' + ');
          await expect(box.locator('legend')).toHaveText(heading);
          await expect(box.locator('legend')).toBeVisible();
          // No heading names a servo or an ESC as needed.
          const legend = await box.locator('legend').innerText();
          for (const id of OBJECTS) expect(legend.split(/[^\p{L}\p{N}]+/u), `${legend}: ${id}`).not.toContain(short(id, lang));
          // The group of the base setup holds a list without a control and the 3 ticks that connect a
          // test object; every other group holds one tick per function.
          const ticks = await box.locator('input[data-f]').evaluateAll((list) => list.map((el) => (el as HTMLElement).dataset.f));
          const rows = await box.locator('ul.base > li').evaluateAll((list) => list.map((el) => el.id.replace(/^b-/, '')));
          expect([ticks, rows]).toEqual(group.parts.length === 0
            ? [group.functions.filter((id) => !ON_BASE.includes(id)), group.functions.filter((id) => ON_BASE.includes(id))]
            : [group.functions, []]);
          // The group is a named group for assistive technology.
          await expect(page.getByRole('group', { name: heading, exact: true })).toHaveCount(1);
        }
        // A part that is one of 2 boards, and parts that depend on a variant, are stated in words.
        const escpow = page.locator('#g-ina228');
        await expect(escpow).toContainText(lang === 'en' ? /: one of .*MATEK.*, or .*XT90/ : /: wahlweise .*MATEK.* oder .*XT90/);
        await expect(escpow).toContainText(
          t(lang, 'gOneOf', { x: `${short('batt', lang)}${STRINGS.or[col(lang)]}${['pdmini', 'charger', 'usbtap'].map((id) => short(id, lang)).join(' + ')}` }),
        );
        await expect(page.locator('#g-pdmini')).toContainText(t(lang, 'gAlso', { x: ['charger', 'usbtap'].map((id) => short(id, lang)).join(', ') }));
        // The 3 ticks of the base group stand under their own sentence.
        await expect(page.locator('#basetick')).toHaveText(STRINGS.baseTick[col(lang)]);
        await expect(page.locator('#basetick ~ .row input[data-f]')).toHaveCount(PLAIN.length);
        // A function that forces an option names the option a tick sets; the others name the test object.
        for (const id of PLAIN) {
          const fn = CATALOGUE.functions.find((f) => f.id === id)!;
          const forced = Object.entries(fn.forces ?? {}) as Array<[OptionKey, string]>;
          await expect(page.locator(`label:has(#f-${id}) small`)).toHaveText(forced.length > 0
            ? t(lang, 'stVar', { v: forced.map(([key, value]) => variantShort(lang, key, value)).join(', ') })
            : t(lang, 'stObj', { x: short(fn.drives as string, lang) }));
        }
        expect(PLAIN.filter((id) => CATALOGUE.functions.find((f) => f.id === id)!.forces !== undefined)).toEqual(['servoprog']);
        // What is in no release keeps its own greyed list.
        await expect(page.locator('.nir h3')).toHaveText(STRINGS.nir[col(lang)]);
        await expect(page.locator('.nir li')).toHaveCount(CATALOGUE.notInRelease.length);
        await expect(page.locator('.nir input')).toHaveCount(0);
        await fits(page);
      }
      // The full selection in German carries the longest texts.
      await open(page, `${FULL}&l=de`);
      await fits(page);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('the list of the base setup holds no control, and the keyboard passes it', async ({ page }) => {
      // 6 page loads and a Tab walk per language: 13 s alone, up to 30 s on a CI runner beside 3 other workers.
      test.slow();
      const seen = watch(page);
      expect(ON_BASE.length).toBeGreaterThan(0);
      // The ids of every function, with and without a tick, in the link: the widest case.
      const all = CATALOGUE.functions.map((f) => f.id);
      for (const lang of LANGS) {
        for (const hash of [`#v=1&l=${lang}`, `#v=1&f=${all.join(',')}&l=${lang}`, `${FULL}&l=${lang}`]) {
          await open(page, hash);
          await expect(page.locator('html')).toHaveAttribute('lang', lang);
          const box = page.locator('#g-base');
          expect(await page.locator('fieldset.grp').first().getAttribute('id')).toBe('g-base');
          await expect(box.locator('legend')).toHaveText(STRINGS.gBase[col(lang)]);
          await expect(box.locator('legend .tag')).toHaveCount(0);
          await expect(box.locator('> .hint')).toHaveText([STRINGS.baseHint[col(lang)], STRINGS.baseTick[col(lang)]]);
          // The list holds no control and nothing that takes the focus, in any setup. The group holds the 3 ticks alone.
          await expect(box.locator('ul.base').locator('input, button, select, textarea, a, summary, [tabindex], [contenteditable]')).toHaveCount(0);
          expect(await box.locator('input, button, select, textarea, a, summary, [tabindex], [contenteditable]').evaluateAll((list) => list.map((el) => el.id)))
            .toEqual(PLAIN.map((id) => `f-${id}`));
          await expect(box.locator('ul.base > li')).toHaveCount(ON_BASE.length);
          for (const f of CATALOGUE.functions.filter((x) => ON_BASE.includes(x.id))) {
            const row = page.locator(`#b-${f.id}`);
            await expect(row.locator('b')).toHaveText(f.name[lang]);
            await expect(row.locator('.tag')).toHaveCount(0);
            // Always on, and never "selected by you".
            await expect(row.locator('small')).toHaveText(STRINGS.stBase[col(lang)]);
            await expect(row).toBeVisible();
            await expect(page.locator(`#f-${f.id}`)).toHaveCount(0);
            await expect(page.locator('#does-base li', { hasText: f.name[lang] })).toHaveCount(1);
          }
          await expect(box.locator('ul.base')).not.toContainText(STRINGS.stOwn[col(lang)]);
          // A link is not rewritten when it opens, and none of the ticks is one of these functions.
          expect(new URL(page.url()).hash).toBe(hash);
          expect(await page.locator('input[data-f]:checked').evaluateAll((list) => list.map((el) => (el as HTMLElement).dataset.f).sort()))
            .toEqual(hash.startsWith('#v=1&l=') ? [] : [...FUNCTIONS].sort());
          await expect(page.locator('.msg')).toHaveCount(0);
          await fits(page);
          // A row is as wide as its group and its text is not cut.
          const cut = await box.locator('ul.base > li').evaluateAll((list) => list.filter((el) => el.scrollWidth > el.clientWidth + 1).map((el) => el.id));
          expect(cut).toEqual([]);
        }

        // Tab walks the chooser in document order: it skips no control and stops on nothing in the list of the base setup.
        await open(page, `#v=1&f=servotest&l=${lang}`);
        const wanted = await page.evaluate(() => {
          const out: string[] = [];
          for (const el of document.querySelectorAll<HTMLElement>('#choose input, #choose button, #choose summary, #choose a[href], #choose [tabindex]')) {
            if (el.getClientRects().length === 0) continue;
            // The content of a closed details element takes no focus; its summary does.
            const closed = el.closest('details:not([open])');
            if (closed && !(el.tagName === 'SUMMARY' && el.parentElement === closed)) continue;
            const key = el instanceof HTMLInputElement && el.type === 'radio' ? `radio:${el.name}` : el.id;
            if (!out.includes(key)) out.push(key);
          }
          return out;
        });
        expect(wanted.length).toBeGreaterThan(FUNCTIONS.length);
        expect(wanted.slice(0, PLAIN.length + 1)).toEqual([...PLAIN, GROUPS[1]?.functions[0]].map((id) => `f-${id}`));
        await tabTo(page, wanted[0] as string);
        const walked: string[] = [];
        for (let i = 0; i < wanted.length + 5; i++) {
          const at = await page.evaluate(() => {
            const el = document.activeElement as HTMLElement | null;
            if (!el || !el.closest('#choose')) return null;
            return { key: el instanceof HTMLInputElement && el.type === 'radio' ? `radio:${el.name}` : el.id, inBase: el.closest('ul.base') !== null };
          });
          if (at === null) break;
          expect(at.inBase, at.key).toBe(false);
          walked.push(at.key);
          await page.keyboard.press('Tab');
        }
        expect(walked).toEqual(wanted);
        // Backwards from the first tick the focus leaves the chooser: nothing in the list takes it.
        await tabTo(page, wanted[0] as string);
        await page.keyboard.press('Shift+Tab');
        expect(await page.evaluate(() => document.activeElement?.closest('#g-base') === null && document.activeElement?.closest('#choose') === null)).toBe(true);
        // A click on a row changes nothing.
        const before = [new URL(page.url()).hash, await page.locator('#result').innerHTML()];
        await page.locator(`#b-${ON_BASE[0]}`).click();
        expect([new URL(page.url()).hash, await page.locator('#result').innerHTML()]).toEqual(before);
      }

      // A part that leaves takes no function of the base setup with it.
      await open(page, `#v=1&f=${all.join(',')}`);
      await page.locator('#rm-pdmini').click();
      await expect(page.locator('#rmwarn')).toBeVisible();
      for (const f of CATALOGUE.functions.filter((x) => ON_BASE.includes(x.id))) await expect(page.locator('#rmwarn')).not.toContainText(f.name.en);
      await page.locator('#rm-yes').click();
      await expect(page.locator('#rm-pdmini')).toHaveCount(0);
      // The link the page writes holds none of their ids.
      const stored = new URLSearchParams(new URL(page.url()).hash.slice(1)).get('f')?.split(',') ?? [];
      expect(stored.length).toBeGreaterThan(0);
      expect(stored.filter((id) => !FUNCTIONS.includes(id))).toEqual([]);
      await expect(page.locator('#g-base ul.base > li')).toHaveCount(ON_BASE.length);
      await expect(page.locator('#does-base li')).toHaveCount(ON_BASE.length);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('a function is "selected by you", "included with these parts" or active by its test object, and the link stores the own selection alone', async ({ page }) => {
      const seen = watch(page);
      for (const lang of LANGS) {
        const tail = lang === 'en' ? '' : '&l=de';
        await open(page, `#v=1&f=servotest${tail}`);
        const label = (id: string) => page.locator(`label:has(#f-${id})`);
        await expect(page.locator('#f-servotest')).toBeChecked();
        await expect(label('servotest')).toContainText(STRINGS.stOwn[col(lang)]);
        // The servo is connected and the PD mini is in the setup: 2 more functions are active without a tick.
        const words: Array<[string, string]> = [['servo', t(lang, 'stConn', { x: short('servo', lang) })], ['supply', STRINGS.stInc[col(lang)]]];
        for (const [id, state] of words) {
          await expect(page.locator(`#f-${id}`)).not.toBeChecked();
          await expect(label(id).locator('small')).toHaveText(state);
          await expect(label(id)).toHaveAttribute('data-state', 'inc');
          await expect(page.getByRole('checkbox', { name: new RegExp(escapeRe(state)) }).and(page.locator(`#f-${id}`))).toHaveCount(1);
        }
        // The ESC is not connected: its tick connects it and adds no part of the bench by name.
        await expect(label('esc').locator('small')).toHaveText(t(lang, 'stObj', { x: short('esc', lang) }));
        await expect(label('esc')).toHaveAttribute('data-state', 'off');
        await expect(label('angle')).toContainText(STRINGS.adds[col(lang)] + short('as5600', lang));
        await expect(page.locator('#g-pdmini legend .tag')).toHaveText(STRINGS.gIn[col(lang)]);
        for (const id of ['g-base', 'g-ina228', 'g-ina3221']) await expect(page.locator(`#${id} legend .tag`)).toHaveCount(0);
        // The link stores the tick of the visitor and nothing of what comes with the parts.
        expect(new URL(page.url()).hash).toBe(`#v=1&f=servotest${tail}`);
        // "This setup does" names all 3 with the same words.
        const does = page.locator('#does-parts');
        await expect(does.locator('li', { hasText: fnName('servotest', lang) })).toContainText(STRINGS.stOwn[col(lang)]);
        await expect(does.locator('li', { hasText: fnName('supply', lang) })).toContainText(STRINGS.stInc[col(lang)]);
        await expect(does.locator('small')).toHaveCount(3);
        await fits(page);
      }

      // A tick on an included function makes it the visitor's own.
      await open(page, '#v=1&f=servotest');
      await page.locator('#f-servo').focus();
      await page.keyboard.press('Space');
      await expect(page).toHaveURL(/#v=1&f=servo,servotest$/);
      await expect(page.locator('label:has(#f-servo)')).toContainText('Selected by you.');
      await expect(page.locator('#note')).toHaveCount(0);
      // Without the tick it stays active, and the page states that its test object stays connected.
      await page.keyboard.press('Space');
      await expect(page).toHaveURL(/#v=1&f=servotest$/);
      await expect(page.locator('#f-servo')).toBeFocused();
      await expect(page.locator('#f-servo')).not.toBeChecked();
      await expect(page.locator('label:has(#f-servo)')).toContainText('Active: Servo is connected.');
      const stays = `${t('en', 'dsOn', { f: fnName('servo') })} ${t('en', 'dsConn', { x: short('servo') })}`;
      await expect(page.locator('#note')).toHaveText(stays);
      await expect(page.locator('#note')).toHaveAttribute('role', 'status');
      await expect(page.locator('#live')).toContainText(stays);
      await expect(page.locator('#live')).toHaveText(sumEnd(['servotest']));
      expect(enabledCount(['servotest'])).toBe(ON_BASE.length + 3);
      await fits(page);

      // The last function that needs the PD mini leaves the selection: the page names what leaves.
      await page.locator('#f-servotest').focus();
      await page.keyboard.press('Space');
      await expect(page).toHaveURL(/#v=1$/);
      await expect(page.locator('#note')).toContainText(t('en', 'dsOff', { f: fnName('servotest') }));
      await expect(page.locator('#note')).toContainText(t('en', 'outObjects', { x: short('servo') }));
      await expect(page.locator('#note')).toContainText(t('en', 'outParts', { x: ['pdmini', 'charger', 'usbtap'].map((id) => short(id)).join(', ') }));
      await expect(page.locator('#note')).toContainText(t('en', 'offFns', { x: [fnName('servo'), fnName('supply')].join(', ') }));
      await expect(page.locator('input:checked[type=checkbox]')).toHaveCount(0);
      // The next change clears the note.
      await page.locator('#f-estop').check();
      await expect(page.locator('#note')).toHaveCount(0);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('a part leaves by keyboard alone after a question that names the functions that go off', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=1&f=servotest');
      // Every part of the bench beyond the base setup has a remove control; a base part and a test object have none.
      const setup = resolve(CATALOGUE, { functions: ['servotest'], owned: [], options: { ...DEFAULT_OPTIONS } });
      expect(setup.parts.filter((r) => r.part.testObject).map((r) => r.part.id)).toEqual(['servo']);
      for (const r of setup.parts) await expect(page.locator(`#rm-${r.part.id}`)).toHaveCount(r.part.locked || r.part.testObject ? 0 : 1);
      await expect(page.locator('button.rm')).toHaveCount(setup.parts.filter((r) => !r.part.locked && !r.part.testObject).length);
      await expect(page.locator('#tested button, #tested input')).toHaveCount(0);
      await expect(page.getByRole('button', { name: 'Remove PD mini', exact: true })).toHaveCount(1);

      await tabTo(page, 'rm-pdmini');
      await expect(page.locator('#rm-pdmini')).toHaveAttribute('aria-expanded', 'false');
      await page.keyboard.press('Enter');
      const warning = page.getByRole('alertdialog', { name: 'Remove PD mini?' });
      await expect(warning).toBeVisible();
      await expect(page.locator('#rmwarn')).toHaveCount(1);
      await expect(page.locator('#rm-pdmini')).toHaveAttribute('aria-expanded', 'true');
      // The focus is on the answer that changes nothing.
      await expect(page.locator('#rm-no')).toBeFocused();
      await expect(page.locator('#rm-no')).toHaveText('Keep');
      await expect(page.locator('#rm-yes')).toHaveText('Remove part and these functions');
      // The functions that go off are named, with the state each has.
      await expect(warning.locator('li')).toHaveCount(2);
      await expect(warning.locator('li').nth(0)).toHaveText(`${fnName('servotest')} Selected by you.`);
      await expect(warning.locator('li').nth(1)).toHaveText(`${fnName('supply')} Included with these parts.`);
      await expect(warning).toContainText(`Parts that leave with it: ${short('charger')}, ${short('usbtap')}.`);
      await expect(warning).toContainText(`Parts that come in: ${short('ssup')}.`);
      // The servo is no part that stays or leaves: the question names it nowhere as a part.
      await expect(warning).not.toContainText('Parts you own');
      // The question stands under its control and inside the viewport.
      await expect(warning).toBeInViewport();
      expect(await page.evaluate(() => {
        const control = document.getElementById('rm-pdmini') as HTMLElement;
        return control.nextElementSibling?.id;
      })).toBe('rmwarn');
      await fits(page);
      expect(new URL(page.url()).hash).toBe('#v=1&f=servotest');

      // Escape keeps the part and returns the focus to the remove control.
      await page.keyboard.press('Escape');
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page.locator('#rm-pdmini')).toBeFocused();
      await expect(page.locator('#rm-pdmini')).toHaveAttribute('aria-expanded', 'false');
      expect(new URL(page.url()).hash).toBe('#v=1&f=servotest');

      // "Keep" does the same.
      await page.keyboard.press('Enter');
      await expect(page.locator('#rm-no')).toBeFocused();
      await page.keyboard.press('Enter');
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page.locator('#rm-pdmini')).toBeFocused();
      await expect(page.locator('#f-servotest')).toBeChecked();

      // The question is no trap: Tab leaves it, the question stays, and Escape still closes it.
      await page.keyboard.press('Space');
      await expect(page.locator('#rm-no')).toBeFocused();
      await page.keyboard.press('Tab');
      expect(await page.evaluate(() => document.getElementById('rmwarn')?.contains(document.activeElement))).toBe(false);
      await expect(page.locator('#rmwarn')).toHaveCount(1);
      await page.keyboard.press('Escape');
      await expect(page.locator('#rm-pdmini')).toBeFocused();

      // The remove control itself closes the open question.
      await page.keyboard.press('Enter');
      await expect(page.locator('#rm-no')).toBeFocused();
      await page.keyboard.press('Shift+Tab');
      await expect(page.locator('#rm-yes')).toBeFocused();
      await page.keyboard.press('Shift+Tab');
      await expect(page.locator('#rm-pdmini')).toBeFocused();
      await page.keyboard.press('Enter');
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page.locator('#rm-pdmini')).toBeFocused();

      // "Remove part and these functions" removes both and names the result.
      await page.keyboard.press('Enter');
      await page.keyboard.press('Shift+Tab');
      await expect(page.locator('#rm-yes')).toBeFocused();
      await page.keyboard.press('Enter');
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page.locator('#rm-pdmini')).toHaveCount(0);
      // The servo stays connected: the tick of "Drive a servo" holds it.
      await expect(page).toHaveURL(/#v=1&f=servo$/);
      await expect(page.locator('#h-parts')).toBeFocused();
      await expect(page.locator('#live')).toHaveText(new RegExp(`^PD mini removed\\. ${sumEnd(['servo']).source}`));
      expect(enabledCount(['servo'])).toBe(ON_BASE.length + 1);
      await expect(page.locator('#f-servotest')).not.toBeChecked();
      await expect(page.locator('label:has(#f-servotest)')).toContainText(`Adds: ${short('pdmini')}`);
      await expect(page.locator('#f-servo')).toBeChecked();
      await expect(page.locator('label:has(#f-servo)')).toContainText('Selected by you.');
      await expect(page.locator('#tested > li')).toHaveCount(1);
      await expect(page.locator('#rm-ssup')).toHaveCount(1);
      // The link of the result opens the same setup.
      const html = await page.locator('#result').innerHTML();
      await open(page, new URL(page.url()).hash);
      expect(await page.locator('#result').innerHTML()).toBe(html);

      // Adding the part back restores the functions.
      await openOwned(page);
      await page.locator('#p-pdmini').focus();
      await page.keyboard.press('Space');
      await expect(page).toHaveURL(/#v=1&f=servo&p=pdmini$/);
      for (const id of ['servotest', 'supply']) await expect(page.locator(`label:has(#f-${id})`)).toContainText('Included with these parts.');
      await expect(page.locator('#live')).toHaveText(sumEnd(['servo'], ['pdmini']));
      expect(enabledCount(['servo'], ['pdmini'])).toBe(ON_BASE.length + 3);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('a part that nothing else follows asks as well, and leaves by the confirming answer alone', async ({ page }) => {
      const seen = watch(page);
      for (const lang of LANGS) {
        const tail = lang === 'en' ? '' : '&l=de';
        const hash = `#v=1&p=sd,usbtap${tail}`;
        await open(page, hash);
        await tabTo(page, 'rm-usbtap');
        await page.keyboard.press('Enter');
        const warning = page.getByRole('alertdialog', { name: t(lang, 'rmAsk', { p: short('usbtap', lang) }) });
        await expect(warning).toBeVisible();
        await expect(warning).toBeInViewport();
        await expect(page.locator('#rmwarn-d')).toHaveText(STRINGS.rmPlain[col(lang)]);
        await expect(page.locator('#rm-yes')).toHaveText(STRINGS.rmYesPlain[col(lang)]);
        await expect(page.locator('#rm-no')).toHaveText(STRINGS.rmNo[col(lang)]);
        await expect(page.locator('#rm-no')).toBeFocused();
        // Nothing left the setup by the request alone.
        expect(new URL(page.url()).hash).toBe(hash);
        await expect(page.locator('#rm-usbtap')).toHaveCount(1);
        await fits(page);
        // Escape and "Keep" keep the part and return the focus to the control.
        await page.keyboard.press('Escape');
        await expect(page.locator('#rmwarn')).toHaveCount(0);
        await expect(page.locator('#rm-usbtap')).toBeFocused();
        expect(new URL(page.url()).hash).toBe(hash);
        await page.keyboard.press('Enter');
        await expect(page.locator('#rm-no')).toBeFocused();
        await page.keyboard.press('Enter');
        await expect(page.locator('#rmwarn')).toHaveCount(0);
        await expect(page.locator('#rm-usbtap')).toBeFocused();
        expect(new URL(page.url()).hash).toBe(hash);
        // The confirming answer removes the part.
        await page.keyboard.press('Space');
        await expect(page.locator('#rm-no')).toBeFocused();
        await page.keyboard.press('Shift+Tab');
        await expect(page.locator('#rm-yes')).toBeFocused();
        expect(new URL(page.url()).hash).toBe(hash);
        await page.keyboard.press('Enter');
        await expect(page.locator('#rmwarn')).toHaveCount(0);
        await expect(page.locator('#rm-usbtap')).toHaveCount(0);
        expect(new URL(page.url()).hash).toBe(`#v=1&p=sd${tail}`);
        await expect(page.locator('#h-parts')).toBeFocused();
        await expect(page.locator('#live')).toContainText(t(lang, 'rmDone', { p: short('usbtap', lang) }));
      }

      // The battery of the ESC leaves by the other variant of the ESC power.
      await open(page, '#v=1&f=esc');
      await tabTo(page, 'rm-batt');
      await page.keyboard.press('Enter');
      const warning = page.locator('#rmwarn');
      await expect(warning).toContainText('No function goes off.');
      await expect(warning).toContainText('Variant that changes: ESC power B.');
      await expect(warning).toContainText(`Parts that come in: ${['pdmini', 'charger', 'usbtap'].map((id) => short(id)).join(', ')}.`);
      await expect(warning).toContainText(`Functions that come on: ${fnName('stick')}, ${fnName('supply')}.`);
      await expect(page.locator('#rm-yes')).toHaveText('Remove part');
      await page.keyboard.press('Shift+Tab');
      await page.keyboard.press('Enter');
      await expect(page).toHaveURL(/#v=1&f=esc&o=pd$/);
      await expect(page.locator('#o-escPower-pd')).toBeChecked();
      await expect(page.locator('#rm-batt')).toHaveCount(0);

      // The DC supply of the Osoyoo breakout board leaves by servo power A, and the servo supply comes in.
      await open(page, '#v=1&f=servo&o=osoyoo,servo5v');
      await tabTo(page, 'rm-dcsup');
      await page.keyboard.press('Enter');
      const dc = page.getByRole('alertdialog', { name: `Remove ${short('dcsup')}?` });
      await expect(dc).toBeVisible();
      await expect(dc).toContainText('No function goes off.');
      await expect(dc).toContainText('Variant that changes: Servo power A.');
      await expect(dc).toContainText(`Parts that come in: ${short('ssup')}.`);
      await expect(page.locator('#rm-yes')).toHaveText('Remove part');
      await page.keyboard.press('Shift+Tab');
      await page.keyboard.press('Enter');
      await expect(page).toHaveURL(/#v=1&f=servo&o=osoyoo$/);
      await expect(page.locator('#o-servoPower-external')).toBeChecked();
      await expect(page.locator('#rm-dcsup')).toHaveCount(0);
      await expect(page.locator('#rm-ssup')).toHaveCount(1);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('"Parts you own" removes a part by its tick, with the same question', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=1&f=servotest&p=usbtap,sd,tap');
      await openOwned(page);
      // Every selectable part of the setup is ticked, also one a function brings; none is locked.
      for (const id of ['pdmini', 'usbtap', 'sd', 'tap']) await expect(page.locator(`#p-${id}`)).toBeChecked();
      // A servo or an ESC is no part to own: the list holds no row for it.
      for (const id of OBJECTS) await expect(page.locator(`#p-${id}`)).toHaveCount(0);
      expect(await page.locator('#own input[data-p]').evaluateAll((list) => list.map((el) => (el as HTMLElement).dataset.p))).toEqual(OWNED);
      await expect(page.locator('#own input[aria-disabled]')).toHaveCount(0);
      await expect(page.locator('label:has(#p-pdmini)')).toContainText(`Needed by: ${fnName('servotest')}`);

      // A part nothing follows asks as well; the tick stays until the confirming answer.
      await page.locator('#p-tap').focus();
      await page.keyboard.press('Space');
      await expect(page.locator('#rmwarn-d')).toHaveText(STRINGS.rmPlain[0]);
      await expect(page.locator('#rm-no')).toBeFocused();
      await expect(page.locator('#p-tap')).toBeChecked();
      await expect(page).toHaveURL(/#v=1&f=servotest&p=usbtap,sd,tap$/);
      await page.keyboard.press('Escape');
      await expect(page.locator('#p-tap')).toBeFocused();
      await expect(page.locator('#p-tap')).toBeChecked();
      await page.keyboard.press('Space');
      await page.keyboard.press('Shift+Tab');
      await expect(page.locator('#rm-yes')).toBeFocused();
      await page.keyboard.press('Enter');
      await expect(page).toHaveURL(/#v=1&f=servotest&p=sd,usbtap$/);
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page.locator('#p-tap')).toBeFocused();
      await expect(page.locator('#p-tap')).not.toBeChecked();

      // The card slot runs the log function: the page asks under the row.
      await page.locator('#p-sd').focus();
      await page.keyboard.press('Space');
      const warning = page.getByRole('alertdialog', { name: `Remove ${short('sd')}?` });
      await expect(warning).toBeVisible();
      await expect(warning).toBeInViewport();
      await expect(page.locator('#rm-no')).toBeFocused();
      await expect(page.locator('#p-sd')).toBeChecked();
      await expect(warning.locator('li')).toHaveText([`${fnName('logs')} Included with these parts.`]);
      expect(await page.evaluate(() => document.getElementById('rmwarn')?.previousElementSibling?.querySelector('input')?.id)).toBe('p-sd');
      // The parts list shows no second question.
      await expect(page.locator('#result #rmwarn')).toHaveCount(0);
      await fits(page);
      await page.keyboard.press('Escape');
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page.locator('#p-sd')).toBeFocused();
      await expect(page.locator('#p-sd')).toBeChecked();
      await expect(page).toHaveURL(/#v=1&f=servotest&p=sd,usbtap$/);

      await page.keyboard.press('Space');
      await page.keyboard.press('Shift+Tab');
      await expect(page.locator('#rm-yes')).toBeFocused();
      await page.keyboard.press('Enter');
      await expect(page).toHaveURL(/#v=1&f=servotest&p=usbtap$/);
      await expect(page.locator('#p-sd')).toBeFocused();
      await expect(page.locator('#p-sd')).not.toBeChecked();
      await expect(page.locator('#own')).toHaveAttribute('open', '');

      // A part a function brings: the tick opens the question, and "Keep" leaves it ticked.
      await page.locator('#p-pdmini').focus();
      await page.keyboard.press('Space');
      await expect(page.locator('#rmwarn li').first()).toHaveText(`${fnName('servotest')} Selected by you.`);
      await page.keyboard.press('Enter');
      await expect(page.locator('#p-pdmini')).toBeFocused();
      await expect(page.locator('#p-pdmini')).toBeChecked();
      await expect(page.locator('#f-servotest')).toBeChecked();

      // A question that is open closes when the selection changes elsewhere.
      await page.keyboard.press('Space');
      await expect(page.locator('#rmwarn')).toHaveCount(1);
      await page.locator('#f-estop').check();
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page).toHaveURL(/#v=1&f=servotest,estop&p=usbtap$/);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('the question fits in German, in both themes, with 44 px answers, and print leaves it out', async ({ page }) => {
      await open(page, `${FULL}&l=de`);
      await tabTo(page, 'rm-pdmini');
      await page.keyboard.press('Enter');
      const warning = page.getByRole('alertdialog', { name: t('de', 'rmAsk', { p: short('pdmini', 'de') }) });
      await expect(warning).toBeVisible();
      await expect(page.locator('#rm-yes')).toHaveText(STRINGS.rmYes[1]);
      await expect(page.locator('#rm-no')).toHaveText(STRINGS.rmNo[1]);
      await expect(page.locator('#rm-no')).toBeFocused();
      await fits(page);
      const small = await page.evaluate(() => {
        const out: string[] = [];
        for (const el of document.querySelectorAll<HTMLElement>('.opt, button, summary, .bar a')) {
          const r = el.getBoundingClientRect();
          if (r.height > 0 && (r.height < 44 || r.width < 44)) out.push(`${el.id || el.textContent}: ${r.width} x ${r.height}`);
        }
        return out;
      });
      expect(small).toEqual([]);
      for (const colorScheme of ['light', 'dark'] as const) {
        await page.emulateMedia({ colorScheme });
        for (const selector of ['#rmwarn-h', '#rmwarn li', '#rmwarn li small', '#rm-yes', '#rm-no', 'label[data-state="own"] small', 'fieldset.grp > legend', 'fieldset.grp > legend .tag', 'fieldset.grp > .hint']) {
          expect(await contrast(page, selector), `${selector} in ${colorScheme}`).toBeGreaterThanOrEqual(4.5);
        }
        // The frame of the question against the page.
        const frame = await page.evaluate(() => getComputedStyle(document.getElementById('rmwarn') as HTMLElement).borderTopWidth);
        expect(frame).toBe('2px');
      }
      await page.emulateMedia({ media: 'print' });
      await expect(page.locator('#rmwarn')).toBeHidden();
      await expect(page.locator('#rm-pdmini')).toBeHidden();
      await expect(page.locator('#choose')).toBeHidden();
      await expect(page.locator('#h-parts')).toBeVisible();
      // The printed list of functions keeps the words for the state.
      await expect(page.locator('#does-parts small').first()).toBeVisible();
      await expect(page.locator('#does-parts small').first()).toHaveText(STRINGS.stOwn[1]);
      await expect(page.locator('#does-base li')).toHaveCount(ON_BASE.length);
    });

    test('a servo and an ESC stand under their own heading, never as a part the bench needs', async ({ page }) => {
      const seen = watch(page);
      for (const lang of LANGS) {
        const tail = lang === 'en' ? '' : '&l=de';
        const cases: Array<[string, string[], string[]]> = [
          ['#v=1', [], []], ['#v=1&f=servo', ['servo'], ['servo']], ['#v=1&f=esc', ['esc'], ['esc']], ['#v=1&f=angle', ['angle'], ['servo']],
          ['#v=1&f=servo,esc', ['servo', 'esc'], ['servo', 'esc']], [FULL, FUNCTIONS, ['servo', 'esc']],
        ];
        for (const [hash, functions, objects] of cases) {
          await open(page, `${hash}${tail}`);
          const what = `${hash}${tail}`;
          await expect(page.locator('#h-tested')).toHaveText(STRINGS.tested[col(lang)]);
          await expect(page.locator('#h-tested')).toBeVisible();
          await expect(page.locator('#h-power')).toHaveText(STRINGS.power[col(lang)]);
          await expect(page.locator('#tested > li')).toHaveCount(objects.length);
          if (objects.length === 0) await expect(page.locator('#result')).toContainText(STRINGS.testedNone[col(lang)]);
          for (const id of objects) {
            const part = CATALOGUE.parts.find((x) => x.id === id)!;
            const row = page.locator('#tested > li', { hasText: part.name[lang] });
            await expect(row, what).toHaveCount(1);
            await expect(row).toContainText(STRINGS.usedBy[col(lang)].trim());
            // The parts list of the bench holds no row for it, and nothing removes it as a part.
            await expect(page.locator('#result ul.parts:not([id]) > li', { hasText: part.name[lang] }), what).toHaveCount(0);
            await expect(page.locator(`#rm-${id}, #p-${id}`), what).toHaveCount(0);
            await expect(page.locator(`#power-${id} > li`).first(), what).toBeVisible();
          }
          await expect(page.locator('#tested button, #tested input')).toHaveCount(0);
          // The power sources: the logic in every setup, and each connected test object.
          expect(await page.locator('#result ul.power').evaluateAll((list) => list.map((el) => el.id)), what).toEqual(['power-logic', ...objects.map((id) => `power-${id}`)]);
          // No heading of a group names a servo or an ESC, in any setup.
          for (const legend of await page.locator('fieldset.grp > legend').allInnerTexts()) {
            for (const id of OBJECTS) expect(legend.split(/[^\p{L}\p{N}]+/u), `${what}: ${legend}`).not.toContain(short(id, lang));
          }
          // The summary counts the parts of the bench.
          const setup = resolve(CATALOGUE, { functions, owned: hash === FULL ? OWNED : [], options: { ...DEFAULT_OPTIONS, ...(hash === FULL ? { connection: 'osoyoo', ina228Board: 'xt90', escPower: 'pd' } : {}) } });
          const bench = setup.parts.filter((r) => !r.part.testObject).length;
          await expect(page.locator('#result ul.parts:not([id]) > li'), what).toHaveCount(bench);
          await expect(page.locator('.sum p'), what).toContainText(new RegExp(`^${bench} `));
          // The state of the project stands once, in the footer; no row carries a state of its own.
          await expect(page.locator('#dev-state')).toHaveCount(1);
          await expect(page.locator('footer #dev-state')).toHaveText(STRINGS.devState[col(lang)]);
          await expect(page.locator('#dev-state')).toBeVisible();
          expect((await page.locator('body').innerText()).split(STRINGS.devState[col(lang)]).length - 1, what).toBe(1);
          await expect(page.locator('body')).not.toContainText(/not run on hardware|nicht auf Hardware gelaufen/i);
          await fits(page);
        }
      }

      // The servo from the bench itself: the note stands in the result, and without that variant it does not.
      const note = CATALOGUE.checks.find((c) => c.id === 'servo-bench-supply')!;
      await open(page, '#v=1&f=servo');
      await expect(page.locator('#result')).not.toContainText(note.text.en);
      await open(page, '#v=1&f=servo&o=servo5v');
      await expect(page.locator('#result')).toContainText(note.text.en);
      await expect(page.locator('#pw-copro-5v .tag')).toHaveText(STRINGS.gIn[0]);
      await fits(page);
      await open(page, '#v=1&f=servo&o=servo5v&l=de');
      await expect(page.locator('#result')).toContainText(note.text.de);
      await fits(page);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('a link in the format of release 0.16.0 opens the same setup and is not rewritten', async ({ page }) => {
      const seen = watch(page);
      const links: Array<[string, string[], string[], Partial<typeof DEFAULT_OPTIONS>]> = [
        ['#v=1&f=servo', ['servo'], [], {}],
        // A servo or an ESC among the owned parts of a link is the tick of its function.
        ['#v=1&p=servo', ['servo'], [], {}],
        ['#v=1&f=angle&p=servo,sd', ['servo', 'angle'], ['sd'], {}],
        ['#v=1&p=esc,pdmini&o=pd&l=de', ['esc'], ['pdmini'], { escPower: 'pd' }],
        ['#v=1&f=servo,esc&o=pd', ['servo', 'esc'], [], { escPower: 'pd' }],
        ['#v=1&f=servotest,logs&p=estop&o=osoyoo', ['servotest', 'logs'], ['estop'], { connection: 'osoyoo' }],
        ['#v=1&p=pdmini,sd', [], ['pdmini', 'sd'], {}],
        ['#v=1&f=escpow,stick&o=xt90,pd&l=de', ['escpow', 'stick'], [], { ina228Board: 'xt90', escPower: 'pd' }],
        [FULL, FUNCTIONS, OWNED, { connection: 'osoyoo', ina228Board: 'xt90', escPower: 'pd' }],
      ];
      for (const [hash, functions, owned, options] of links) {
        const setup = resolve(CATALOGUE, { functions, owned, options: { ...DEFAULT_OPTIONS, ...options } });
        await open(page, hash);
        expect(new URL(page.url()).hash, hash).toBe(hash);
        await expect(page.locator('.msg')).toHaveCount(0);
        // The ticks are the functions of the link, no more.
        expect(await page.locator('input[data-f]:checked').evaluateAll((list) => list.map((el) => (el as HTMLElement).dataset.f).sort()), hash).toEqual([...functions].sort());
        await expect(page.locator('#result ul.parts:not([id]) > li')).toHaveCount(setup.parts.filter((r) => !r.part.testObject).length);
        await expect(page.locator('#tested > li')).toHaveCount(setup.parts.filter((r) => r.part.testObject).length);
        await expect(page.locator('table.wires tbody tr')).toHaveCount(setup.wires.length);
        // Every enabled function is listed once: under the base setup or under the parts.
        await expect(page.locator('#does-base li')).toHaveCount(ON_BASE.length);
        await expect(page.locator('#does-base li, #does-parts li')).toHaveCount(setup.functions.filter((f) => f.enabled).length);
        for (const f of setup.functions) {
          if (ON_BASE.includes(f.fn.id)) {
            await expect(page.locator(`#f-${f.fn.id}`), `${hash}: ${f.fn.id}`).toHaveCount(0);
            await expect(page.locator(`#b-${f.fn.id} small`), `${hash}: ${f.fn.id}`).toHaveText(STRINGS.stBase[hash.endsWith('l=de') ? 1 : 0]);
          } else {
            await expect(page.locator(`label:has(#f-${f.fn.id})`), `${hash}: ${f.fn.id}`).toHaveAttribute('data-state', f.chosen ? 'own' : f.enabled ? 'inc' : 'off');
          }
        }
        await fits(page);
      }
      expect(seen).toEqual({ foreign: [], errors: [] });
    });
  });
}

test('a link that changes while a question is open closes the question', async ({ page }) => {
  await page.setViewportSize(SIZES[0] as { width: number; height: number });
  await open(page, '#v=1&f=servotest');
  await page.locator('#rm-pdmini').click();
  await expect(page.locator('#rmwarn')).toHaveCount(1);
  await page.evaluate(() => { location.hash = '#v=1&f=estop'; });
  await expect(page.locator('#f-estop')).toBeChecked();
  await expect(page.locator('#rmwarn')).toHaveCount(0);
  // "Start over" closes it as well.
  await page.locator('#rm-estop').click();
  await expect(page.locator('#rmwarn')).toHaveCount(1);
  await page.locator('#b-reset').click();
  await expect(page.locator('#rmwarn')).toHaveCount(0);
  await expect(page).toHaveURL(/#v=1$/);
  // The language switch keeps an open question and translates it.
  await open(page, '#v=1&f=servotest');
  await page.locator('#rm-pdmini').click();
  await page.locator('.lang button[data-lang=de]').click();
  await expect(page.getByRole('alertdialog', { name: t('de', 'rmAsk', { p: short('pdmini', 'de') }) })).toBeVisible();
  await page.locator('#rm-no').click();
  await expect(page.locator('#rm-pdmini')).toBeFocused();
});
