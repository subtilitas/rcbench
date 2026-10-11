// Each setup in each cell of the matrix: a part beyond the base setup leaves
// by its remove control or by its tick under "Parts you own". The resolver
// states what a removal does; the page asks first where more than the part
// changes.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

import type { Lang, Selection } from '../../contract.ts';
import { CATALOGUE, removalEffect, removalIsPlain } from '../../src/core.ts';
import type { Removal } from '../../src/core.ts';
import { t } from '../../src/i18n.ts';
import { axeOf } from '../lib/axe.ts';
import { contrastOf, contrastProblems } from '../lib/contrast.ts';
import { keyboardWalk, walkProblems } from '../lib/keyboard.ts';
import { SEL, expectSetup, expectedOf, hashOf, openSetup } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';
import { SETUPS, cellOf } from './setups.ts';

const other = (lang: Lang): Lang => (lang === 'en' ? 'de' : 'en');
const effectOf = (selection: Selection, id: string): Removal => removalEffect(CATALOGUE, selection, id);
const shortName = (id: string, lang: Lang): string => CATALOGUE.parts.find((p) => p.id === id)?.short[lang] ?? id;

/** Asserts that the question of a removal is open with the content the resolver states, and that "Keep" has the focus. */
async function expectQuestion(page: Page, effect: Removal, lang: Lang): Promise<void> {
  const question = page.locator(SEL.question);
  await expect(question).toBeVisible();
  await expect(page.getByRole('alertdialog', { name: t(lang, 'rmAsk', { p: shortName(effect.part, lang) }) })).toHaveCount(1);
  const held = effect.refused === 'held';
  await expect(question.locator('li')).toHaveCount(held ? 0 : effect.functionsOff.length);
  await expect(page.locator(SEL.yes)).toHaveCount(held ? 0 : 1);
  await expect(page.locator(SEL.no)).toBeFocused();
  await expect(page.locator(SEL.no)).toBeInViewport();
}

for (const { name, selection } of SETUPS) {
  const removable = expectedOf(selection).removable;
  const asking = removable.find((id) => !removalIsPlain(effectOf(selection, id)));
  const owned = expectedOf(selection).owned[0];

  test(`${name}: every part beyond the base setup leaves by its remove control`, async ({ page }, info) => {
    const { lang } = cellOf(info);
    const seen = watchPage(page);
    await openSetup(page, selection);
    await expectSetup(page, selection);
    // A part of the base setup has no remove control.
    for (const part of CATALOGUE.parts.filter((p) => p.locked)) await expect(page.locator(SEL.remove(part.id))).toHaveCount(0);

    for (const id of removable) {
      const effect = effectOf(selection, id);
      await openSetup(page, selection);
      const control = page.locator(SEL.remove(id));
      await expect(control).toHaveAccessibleName(t(lang, 'rmName', { p: shortName(id, lang) }));
      await expect(control).toHaveAttribute('aria-expanded', 'false');
      await control.focus();
      await page.keyboard.press('Enter');

      if (removalIsPlain(effect)) await expect(page.locator(SEL.question)).toHaveCount(0);
      else {
        await expectQuestion(page, effect, lang);
        await expect(control).toHaveAttribute('aria-expanded', 'true');
        if (effect.refused === 'held') {
          // A rule holds the part: the one answer keeps it.
          await page.keyboard.press('Enter');
          await expect(page.locator(SEL.question)).toHaveCount(0);
          await expect(control).toBeFocused();
          await expectSetup(page, selection);
          continue;
        }
        await page.locator(SEL.yes).focus();
        await page.keyboard.press('Enter');
        await expect(page.locator(SEL.question)).toHaveCount(0);
      }
      const after = await expectSetup(page, effect.selection);
      expect(after.parts, id).not.toContain(id);
      expect(after.hash, id).toBe(hashOf(effect.selection));
      await expect(page.locator(SEL.partsHead)).toBeFocused();
      await expect(page.locator(SEL.live)).toContainText(t(lang, 'rmDone', { p: shortName(id, lang) }));
    }
    expectQuiet(seen);
  });

  if (asking !== undefined) {
    test(`${name}: the question before a removal fits the page and closes without a change`, async ({ page }, info) => {
      const { lang } = cellOf(info);
      const effect = effectOf(selection, asking);
      const seen = watchPage(page);
      await openSetup(page, selection);
      const before = await expectSetup(page, selection);
      const control = page.locator(SEL.remove(asking));
      await control.click();
      await expectQuestion(page, effect, lang);
      await expectNoHorizontalScroll(page);
      expect((await axeOf(page)).violations).toEqual([]);
      expect(contrastProblems(await contrastOf(page))).toEqual([]);

      // The other language asks the same question. The setup stays.
      await page.locator(SEL.lang(other(lang))).click();
      await expect(page.getByRole('alertdialog', { name: t(other(lang), 'rmAsk', { p: shortName(asking, other(lang)) }) })).toHaveCount(1);
      await expect(page.locator(`${SEL.question} li`)).toHaveCount(effect.refused === 'held' ? 0 : effect.functionsOff.length);
      await expectSetup(page, selection);
      await page.locator(SEL.lang(lang)).click();
      await expect(page.locator(SEL.question)).toBeVisible();

      // Paper shows no question.
      await page.emulateMedia({ media: 'print' });
      await expect(page.locator(SEL.question)).toBeHidden();
      await page.emulateMedia({ media: 'screen' });

      // Escape keeps the part and returns the focus to the control that asked.
      await page.locator(SEL.no).focus();
      await page.keyboard.press('Escape');
      await expect(page.locator(SEL.question)).toHaveCount(0);
      await expect(control).toBeFocused();
      await expect(control).toHaveAttribute('aria-expanded', 'false');
      expect(await expectSetup(page, selection)).toEqual(before);

      // An open question is not part of the link: a reload shows the setup without it.
      await control.click();
      await expect(page.locator(SEL.question)).toBeVisible();
      await page.reload();
      await page.locator(SEL.drawing).waitFor();
      await expect(page.locator(SEL.question)).toHaveCount(0);
      expect(await expectSetup(page, selection)).toEqual(before);

      // The answers are stops of the Tab key, directly after the control that asked.
      await page.locator(SEL.remove(asking)).click();
      await expect(page.locator(SEL.question)).toBeVisible();
      const walk = await keyboardWalk(page);
      expect(walkProblems(walk)).toEqual([]);
      const stops = walk.stops.map((s) => s.what);
      const at = stops.indexOf(`button${SEL.remove(asking)}`);
      expect(at).toBeGreaterThanOrEqual(0);
      const answers = effect.refused === 'held' ? [`button${SEL.no}`] : [`button${SEL.yes}`, `button${SEL.no}`];
      expect(stops.slice(at + 1, at + 1 + answers.length)).toEqual(answers);
      expectQuiet(seen);
    });
  }

  if (owned !== undefined) {
    test(`${name}: a tick that comes off under "Parts you own" removes the part`, async ({ page }, info) => {
      const { lang } = cellOf(info);
      const effect = effectOf(selection, owned);
      const seen = watchPage(page);
      await openSetup(page, selection);
      await expectSetup(page, selection);
      await page.locator(SEL.ownSummary).click();
      await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
      const tick = page.locator(SEL.owned(owned));
      await expect(tick).toBeChecked();
      await tick.focus();
      await page.keyboard.press('Space');

      if (!removalIsPlain(effect)) {
        // The question stands under the row, and the part stays until an answer.
        await expectQuestion(page, effect, lang);
        await expect(page.locator(`label:has(${SEL.owned(owned)}) + ${SEL.question}`)).toHaveCount(1);
        await expect(tick).toBeChecked();
        await expectNoHorizontalScroll(page);
        await page.keyboard.press('Enter');
        await expect(page.locator(SEL.question)).toHaveCount(0);
        await expect(tick).toBeFocused();
        await expect(tick).toBeChecked();
        await expectSetup(page, selection);
        if (effect.refused === 'held') {
          expectQuiet(seen);
          return;
        }
        await page.keyboard.press('Space');
        await expectQuestion(page, effect, lang);
        await page.locator(SEL.yes).focus();
        await page.keyboard.press('Enter');
        await expect(page.locator(SEL.question)).toHaveCount(0);
      }
      const after = await expectSetup(page, effect.selection);
      expect(after.hash).toBe(hashOf(effect.selection));
      await expect(tick).toBeFocused();
      await expect(tick).not.toBeChecked();
      await expect(page.locator(SEL.own)).toHaveAttribute('open', '');
      expectQuiet(seen);
    });
  }
}
