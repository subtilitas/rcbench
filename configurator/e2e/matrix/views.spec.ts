// Each setup in each cell of the matrix, in both views of the wiring: the
// page opens on the wiring picture with every part of the setup and every
// wire that has a wire point on a part drawing, and the schematic behind the
// switch shows every wire, fits the viewport, stays quiet, has no violation
// and takes the keyboard.

import { expect, test } from '@playwright/test';

import ANCHORS from '../../drawings/anchors.json' with { type: 'json' };
import type { Anchors } from '../../drawings/generate.ts';
import { CATALOGUE, resolve } from '../../src/core.ts';
import { diagramOf } from '../../src/diagram/route.ts';
import { sceneOf } from '../../src/diagram/scene.ts';
import { axeOf } from '../lib/axe.ts';
import { contrastOf, contrastProblems } from '../lib/contrast.ts';
import { keyboardWalk, walkProblems } from '../lib/keyboard.ts';
import { SEL, showSchematic } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';
import { SETUPS, cellOf, openCase } from './setups.ts';

for (const setup of SETUPS) {
  const { name, selection } = setup;
  const resolved = resolve(CATALOGUE, selection);
  const picture = diagramOf(sceneOf(resolved, ANCHORS as unknown as Anchors));

  test(`${name}: the page opens on the wiring picture with every part and every wire that has a wire point`, async ({ page }, info) => {
    const { lang } = cellOf(info);
    const seen = watchPage(page);
    await openCase(page, setup);
    await expect(page.locator(SEL.view('pic'))).toHaveAttribute('aria-pressed', 'true');
    await expect(page.locator(SEL.view('sch'))).toHaveAttribute('aria-pressed', 'false');
    await expect(page.locator(SEL.picture)).toBeVisible();
    await expect(page.locator(SEL.drawing)).toBeHidden();

    // 1 drawing per part, in the language of the page, and each one loads.
    const images = page.locator(`${SEL.picture} image`);
    await expect(images).toHaveCount(picture.parts.length);
    const hrefs = await images.evaluateAll((list) => list.map((i) => i.getAttribute('href') ?? ''));
    expect([...hrefs].sort()).toEqual(picture.parts.map((p) => `drawings/${lang === 'de' ? 'de/' : ''}${p.file}`).sort());
    const loaded = await page.evaluate(async (list) => Promise.all(list.map(async (href) => (await fetch(href)).status)), hrefs);
    expect(loaded).toEqual(hrefs.map(() => 200));

    // 1 wire group per wire with a wire point at both ends; the others are named below the picture.
    const drawn = await page.locator(SEL.pictureWire).evaluateAll((list) => list.map((g) => Number(g.getAttribute('data-wire'))));
    expect([...drawn].sort((a, b) => a - b)).toEqual(picture.wires.map((w) => w.n).sort((a, b) => a - b));
    expect(drawn.length + picture.undrawn.length).toBe(resolved.wires.length);
    for (const n of picture.undrawn) await expect(page.locator('#dia-hint')).toContainText(String(n));
    // Each wire group has a name, and 1 of them is the stop of the Tab key.
    for (const wire of await page.locator(SEL.pictureWire).all()) await expect(wire).toHaveAttribute('aria-label', /\S/);
    await expect(page.locator(`${SEL.pictureWire}[tabindex="0"]`)).toHaveCount(drawn.length > 0 ? 1 : 0);

    await expectNoHorizontalScroll(page);
    expectQuiet(seen);
  });

  test(`${name}: the schematic shows every wire, fits the viewport, stays quiet and has no violation`, async ({ page }) => {
    const seen = watchPage(page);
    await openCase(page, setup);
    await showSchematic(page);
    await expect(page.locator(SEL.view('pic'))).toHaveAttribute('aria-pressed', 'false');
    await expect(page.locator(SEL.drawing)).toHaveAttribute('aria-label', /\S/);
    await expect(page.locator(SEL.wire)).toHaveCount(resolved.wires.length);
    await expectNoHorizontalScroll(page);

    const report = await axeOf(page);
    expect(report.violations).toEqual([]);
    const contrast = await contrastOf(page);
    expect(contrastProblems(contrast)).toEqual([]);
    for (const kind of ['svg-text', 'wire']) expect(contrast.pairs.some((p) => p.kind === kind), kind).toBe(true);
    expectQuiet(seen);
  });

  test(`${name}: the Tab key reaches the schematic and no wire of the hidden picture`, async ({ page }) => {
    await openCase(page, setup);
    await page.locator(SEL.view('sch')).focus();
    await page.keyboard.press('Enter');
    await expect(page.locator(SEL.drawing)).toBeVisible();
    const walk = await keyboardWalk(page);
    expect(walkProblems(walk)).toEqual([]);
    const ids = walk.stops.map((s) => s.what);
    for (const what of [`button${SEL.view('pic')}`, `button${SEL.view('sch')}`, `div${SEL.pan}`]) expect(ids, `${what} is no stop`).toContain(what);
    expect(ids).not.toContain(`div${SEL.dia}`);
    expect(ids).not.toContain('g');
  });
}
