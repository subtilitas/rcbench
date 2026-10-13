// Each entry of the hash corpus in the page: the page shows the setup the
// entry stores and the wires of wires.json, writes the stored hash on the
// first change, and neither throws nor logs. An entry that names an id the
// catalogue has added since the corpus started is exempt from the stored
// setup, alone. An entry that names a part the catalogue no longer offers a tick
// for is held to no notice for it. A test object under the owned parts of an entry is the tick
// of the function that drives it alone: the page shows that tick and writes
// the function in place of the part.

import { expect, test } from '@playwright/test';

import { hashOf as textOf, namesLater, namesRetired, readCorpus, readWires } from '../../test/corpus/corpus.ts';
import type { Options } from '../../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, plainDrive } from '../../src/core.ts';
import { formatHash } from '../../src/hash.ts';
import { SEL, expectedOf, openHash, readSetup } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';

const corpus = readCorpus();
const wires = readWires();
const OBJECTS = CATALOGUE.parts.filter((p) => p.testObject === true).map((p) => p.id);

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
    // An entry that names a part without a tick today gets no tick for it and no notice for it.
    const retired = namesRetired(entry, corpus);
    if (retired.length > 0) {
      const gone = await readSetup(page);
      expect(gone.lang).toBe(want.lang);
      for (const id of retired) expect(gone.notices.map((n) => n.text).join(' ')).not.toContain(id);
      expectQuiet(seen);
      return;
    }
    await expect(page.locator(SEL.row)).toHaveCount((wires[entry.name] ?? []).length);
    const shown = await readSetup(page);

    // The stored selection as the page holds it: an option key the entry does not store has its default value.
    const named = want.owned.filter((id) => OBJECTS.includes(id));
    const ticks = new Set([...want.functions, ...named.map((id) => plainDrive(CATALOGUE, id) as string)]);
    const functions = CATALOGUE.functions.map((f) => f.id).filter((id) => ticks.has(id));
    const owned = want.owned.filter((id) => !OBJECTS.includes(id));
    const options: Options = { ...DEFAULT_OPTIONS, ...want.options };

    expect(shown.lang).toBe(want.lang);
    expect(shown.functions.filter((id) => corpus.ids.functions.includes(id))).toEqual(functions.filter((id) => corpus.ids.functions.includes(id)));
    expect(shown.parts.filter((id) => corpus.ids.parts.includes(id))).toEqual(want.parts.filter((id) => corpus.ids.parts.includes(id)));
    // Each variant group the resolver states as visible is on the page, with the stored value.
    expect(shown.options).toEqual(expectedOf({ functions, owned, options }).options);
    for (const [key, value] of Object.entries(shown.options)) expect(value, key).toBe(options[key as keyof Options]);
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
    expect((await readSetup(page)).hash).toBe(named.length === 0 ? want.canonical : `#${formatHash({ functions, owned, options }, want.lang, 'en', CATALOGUE)}`);
    expectQuiet(seen);
  });
}
