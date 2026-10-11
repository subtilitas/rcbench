// The chooser of the built page under `vite preview`: groups by needed parts,
// the words for the state of a function, and the removal of a part by keyboard.

import { expect, test } from '@playwright/test';
import { pageOrigin } from './lib/watch.ts';
import type { Page } from '@playwright/test';
import type { Lang } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, groups, resolve } from '../src/core.ts';
import { STRINGS, t } from '../src/i18n.ts';

const SIZES = [{ width: 360, height: 800 }, { width: 1280, height: 800 }];
const LANGS: Lang[] = ['en', 'de'];
const GROUPS = groups(CATALOGUE);
const FUNCTIONS = CATALOGUE.functions.map((f) => f.id);
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
  await page.locator('svg.drawing').waitFor();
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

    test('the functions stand in groups by the parts they need, in English and German', async ({ page }) => {
      const seen = watch(page);
      expect(GROUPS.flatMap((g) => g.functions).sort()).toEqual([...FUNCTIONS].sort());
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
          expect(await box.locator('input[data-f]').evaluateAll((list) => list.map((el) => (el as HTMLElement).dataset.f))).toEqual(group.functions);
          // The group is a named group for assistive technology.
          await expect(page.getByRole('group', { name: heading, exact: true })).toHaveCount(1);
        }
        // A part that is one of 2 boards, and parts that depend on a variant, are stated in words.
        const escpow = page.locator('#g-esc-ina228');
        await expect(escpow).toContainText(lang === 'en' ? /: one of .*MATEK.*, or .*XT90/ : /: wahlweise .*MATEK.* oder .*XT90/);
        await expect(page.locator('#g-esc')).toContainText(
          t(lang, 'gOneOf', { x: `${short('batt', lang)}${STRINGS.or[col(lang)]}${['pdmini', 'charger', 'usbtap'].map((id) => short(id, lang)).join(' + ')}` }),
        );
        await expect(page.locator('#g-servo')).toContainText(t(lang, 'gAlso', { x: short('ssup', lang) }));
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

    test('a function is "selected by you" or "included with these parts", and the link stores the own selection alone', async ({ page }) => {
      const seen = watch(page);
      for (const lang of LANGS) {
        const tail = lang === 'en' ? '' : '&l=de';
        await open(page, `#v=1&f=servotest${tail}`);
        const label = (id: string) => page.locator(`label:has(#f-${id})`);
        await expect(page.locator('#f-servotest')).toBeChecked();
        await expect(label('servotest')).toContainText(STRINGS.stOwn[col(lang)]);
        // The servo and the PD mini are in the setup: 2 more functions are active without a tick.
        for (const id of ['servo', 'supply']) {
          await expect(page.locator(`#f-${id}`)).not.toBeChecked();
          await expect(label(id)).toContainText(STRINGS.stInc[col(lang)]);
          await expect(label(id)).toHaveAttribute('data-state', 'inc');
          await expect(page.getByRole('checkbox', { name: new RegExp(STRINGS.stInc[col(lang)].replace('.', '\\.')) }).and(page.locator(`#f-${id}`))).toHaveCount(1);
        }
        await expect(label('angle')).toContainText(STRINGS.adds[col(lang)] + short('as5600', lang));
        for (const id of ['g-servo', 'g-pdmini', 'g-servo-pdmini']) await expect(page.locator(`#${id} legend .tag`)).toHaveText(STRINGS.gIn[col(lang)]);
        await expect(page.locator('#g-esc legend .tag')).toHaveCount(0);
        // The link stores the tick of the visitor and nothing of what comes with the parts.
        expect(new URL(page.url()).hash).toBe(`#v=1&f=servotest${tail}`);
        // "This setup does" names all 3 with the same words.
        const does = page.locator('#result ul.plain').first();
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
      // Without the tick it stays active, and the page states why its part stays.
      await page.keyboard.press('Space');
      await expect(page).toHaveURL(/#v=1&f=servotest$/);
      await expect(page.locator('#f-servo')).toBeFocused();
      await expect(page.locator('#f-servo')).not.toBeChecked();
      await expect(page.locator('label:has(#f-servo)')).toContainText('Included with these parts.');
      const stays = `${t('en', 'dsOn', { f: fnName('servo') })} ${t('en', 'dsStay', { x: short('servo') })}`;
      await expect(page.locator('#note')).toHaveText(stays);
      await expect(page.locator('#note')).toHaveAttribute('role', 'status');
      await expect(page.locator('#live')).toContainText(stays);
      await expect(page.locator('#live')).toHaveText(/\d+ parts, \d+ wires, 3 functions\.$/);
      await fits(page);

      // The last function that needs the PD mini leaves the selection: the page names what leaves.
      await page.locator('#f-servotest').focus();
      await page.keyboard.press('Space');
      await expect(page).toHaveURL(/#v=1$/);
      await expect(page.locator('#note')).toContainText(t('en', 'dsOff', { f: fnName('servotest') }));
      await expect(page.locator('#note')).toContainText(t('en', 'outParts', { x: ['servo', 'pdmini', 'charger', 'usbtap'].map((id) => short(id)).join(', ') }));
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
      // Every part beyond the base setup has a remove control; a base part has none.
      const setup = resolve(CATALOGUE, { functions: ['servotest'], owned: [], options: { ...DEFAULT_OPTIONS } });
      for (const r of setup.parts) await expect(page.locator(`#rm-${r.part.id}`)).toHaveCount(r.part.locked ? 0 : 1);
      await expect(page.locator('button.rm')).toHaveCount(setup.parts.filter((r) => !r.part.locked).length);
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
      await expect(warning).toContainText(`Parts that stay, ticked under "Parts you own": ${short('servo')}.`);
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
      await expect(page).toHaveURL(/#v=1&p=servo$/);
      await expect(page.locator('#h-parts')).toBeFocused();
      await expect(page.locator('#live')).toHaveText(/^PD mini removed\. \d+ parts, \d+ wires, 1 function\.$/);
      await expect(page.locator('#f-servotest')).not.toBeChecked();
      await expect(page.locator('label:has(#f-servotest)')).toContainText(`Adds: ${short('pdmini')}`);
      await expect(page.locator('label:has(#f-servo)')).toContainText('Included with these parts.');
      await expect(page.locator('#rm-ssup')).toHaveCount(1);
      // The link of the result opens the same setup.
      const html = await page.locator('#result').innerHTML();
      await open(page, new URL(page.url()).hash);
      expect(await page.locator('#result').innerHTML()).toBe(html);

      // Adding the part back restores the functions.
      await openOwned(page);
      await page.locator('#p-pdmini').focus();
      await page.keyboard.press('Space');
      await expect(page).toHaveURL(/#v=1&p=servo,pdmini$/);
      for (const id of ['servo', 'servotest', 'supply']) await expect(page.locator(`label:has(#f-${id})`)).toContainText('Included with these parts.');
      await expect(page.locator('#live')).toHaveText(/3 functions\.$/);
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('a part that nothing else follows leaves at once, a part held by a variant names the variant', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=1&p=usbtap,sd');
      await tabTo(page, 'rm-usbtap');
      await page.keyboard.press('Enter');
      await expect(page.locator('#rmwarn')).toHaveCount(0);
      await expect(page).toHaveURL(/#v=1&p=sd$/);
      await expect(page.locator('#h-parts')).toBeFocused();
      await expect(page.locator('#live')).toHaveText(new RegExp(`^${short('usbtap')} removed\\. `));

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
      expect(seen).toEqual({ foreign: [], errors: [] });
    });

    test('"Parts you own" removes a part by its tick, with the same question', async ({ page }) => {
      const seen = watch(page);
      await open(page, '#v=1&f=servotest&p=usbtap,sd,tap');
      await openOwned(page);
      // Every selectable part of the setup is ticked, also one a function brings; none is locked.
      for (const id of ['servo', 'pdmini', 'usbtap', 'sd', 'tap']) await expect(page.locator(`#p-${id}`)).toBeChecked();
      await expect(page.locator('#p-esc')).not.toBeChecked();
      await expect(page.locator('#own input[aria-disabled]')).toHaveCount(0);
      await expect(page.locator('label:has(#p-pdmini)')).toContainText(`Needed by: ${fnName('servotest')}`);

      // A part nothing follows leaves at once; the focus stays on its row.
      await page.locator('#p-tap').focus();
      await page.keyboard.press('Space');
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
      await expect(page.locator('#result ul.plain').first().locator('small').first()).toBeVisible();
      await expect(page.locator('#result ul.plain').first().locator('small').first()).toHaveText(STRINGS.stOwn[1]);
    });

    test('a link in the format of release 0.16.0 opens the same setup and is not rewritten', async ({ page }) => {
      const seen = watch(page);
      const links: Array<[string, string[], string[], Partial<typeof DEFAULT_OPTIONS>]> = [
        ['#v=1&f=servo', ['servo'], [], {}],
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
        await expect(page.locator('.parts > li')).toHaveCount(setup.parts.length);
        await expect(page.locator('table.wires tbody tr')).toHaveCount(setup.wires.length);
        await expect(page.locator('#result ul.plain').first().locator('li')).toHaveCount(1 + setup.functions.filter((f) => f.enabled).length);
        for (const f of setup.functions) {
          await expect(page.locator(`label:has(#f-${f.fn.id})`), `${hash}: ${f.fn.id}`).toHaveAttribute('data-state', f.chosen ? 'own' : f.enabled ? 'inc' : 'off');
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
