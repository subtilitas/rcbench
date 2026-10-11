// Each entry of the hash corpus in the page: the page shows the setup the
// entry stores and the wires of wires.json, writes the stored hash on the
// first change, and neither throws nor logs. An entry that names an id the
// catalogue has added since the corpus started is exempt from the stored
// setup, alone.

import { expect, test } from '@playwright/test';

import { hashOf as textOf, namesLater, readCorpus, readWires } from '../../test/corpus/corpus.ts';
import { SEL, openHash, readSetup } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';

const corpus = readCorpus();
const wires = readWires();

for (const entry of corpus.entries) {
  test(`${entry.name}: ${entry.why}`, async ({ page }) => {
    const seen = watchPage(page);
    const want = entry.expect;
    await openHash(page, textOf(entry));
    // An entry that names an id of a later catalogue shows another setup than the stored one.
    if (namesLater(entry, corpus)) {
      expect((await readSetup(page)).lang).toBe(want.lang);
      expectQuiet(seen);
      return;
    }
    await expect(page.locator(SEL.row)).toHaveCount((wires[entry.name] ?? []).length);
    const shown = await readSetup(page);

    expect(shown.lang).toBe(want.lang);
    expect(shown.functions.filter((id) => corpus.ids.functions.includes(id))).toEqual(want.functions);
    expect(shown.parts.filter((id) => corpus.ids.parts.includes(id))).toEqual(want.parts);
    expect(shown.options).toEqual(expect.objectContaining(Object.fromEntries(Object.entries(want.options).filter(([key]) => key in shown.options))));
    expect(shown.notices.map((n) => n.kind)).toEqual(want.notices);
    if (want.reported !== '') expect(shown.notices.map((n) => n.text).join(' ')).toContain(want.reported);
    expect(shown.wires).toEqual([...(wires[entry.name] ?? [])].sort((a, b) => a - b));
    expect(shown.drawn).toBe(shown.wires.length);
    await expectNoHorizontalScroll(page);

    // The first change writes the hash: here the language, there and back.
    const there = want.lang === 'en' ? 'de' : 'en';
    await page.locator(SEL.lang(there)).click();
    await expect(page.locator('html')).toHaveAttribute('lang', there);
    await page.locator(SEL.lang(want.lang)).click();
    await expect(page.locator('html')).toHaveAttribute('lang', want.lang);
    expect((await readSetup(page)).hash).toBe(want.canonical);
    expectQuiet(seen);
  });
}
