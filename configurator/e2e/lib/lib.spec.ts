// The helpers of this folder on documents with a known answer: a colour pair
// a fraction under its minimum is a finding, a pair at the minimum is none,
// and a focus outline counts with the colour that shows through it.

import { expect, test } from '@playwright/test';

import { contrastOf, contrastProblems } from './contrast.ts';
import { keyboardWalk, walkProblems } from './keyboard.ts';

const doc = (body: string): string => `<!doctype html><html lang="en"><body style="background:#fff;margin:20px">${body}</body></html>`;

test('text at 4.49999:1 on white is a finding, text at 4.5:1 and above is none', async ({ page }) => {
  await page.setContent(doc('<p style="color:rgb(108,123,103)">under</p>'));
  const under = await contrastOf(page);
  expect(under.pairs).toHaveLength(1);
  expect(under.pairs[0]?.ratio).toBeGreaterThan(4.4999);
  expect(under.pairs[0]?.ratio).toBeLessThan(4.5);
  expect(contrastProblems(under)).toHaveLength(1);
  expect(contrastProblems(under)[0]).toContain('the minimum is 4.5:1');

  await page.setContent(doc('<p style="color:rgb(108,123,102)">over</p>'));
  const over = await contrastOf(page);
  expect(over.pairs[0]?.ratio).toBeGreaterThanOrEqual(4.5);
  expect(contrastProblems(over)).toEqual([]);
});

test('large text at 2.99995:1 on white is a finding', async ({ page }) => {
  await page.setContent(doc('<p style="color:rgb(158,147,139);font-size:24px">under</p>'));
  const under = await contrastOf(page);
  expect(under.pairs[0]?.need).toBe(3);
  expect(under.pairs[0]?.ratio).toBeGreaterThan(2.9999);
  expect(under.pairs[0]?.ratio).toBeLessThan(3);
  expect(contrastProblems(under)).toHaveLength(1);
});

test('text with an alpha value counts with the background that shows through', async ({ page }) => {
  await page.setContent(doc('<p style="color:rgba(0,0,0,0.3)">faint</p>'));
  const faint = await contrastOf(page);
  expect(faint.pairs[0]?.fg).toBe('179,179,179');
  expect(contrastProblems(faint)).toHaveLength(1);
});

const button = (outline: string): string => doc(`<style>button:focus-visible{outline:3px solid ${outline};outline-offset:2px}</style><main><button id="b">Go</button></main>`);

test('an opaque black focus outline on white is no finding', async ({ page }) => {
  await page.setContent(button('rgb(0,0,0)'));
  const walk = await keyboardWalk(page);
  expect(walk.stops.map((s) => s.what)).toEqual(['button#b']);
  expect(walk.stops[0]?.outlineContrast).toBeCloseTo(21, 5);
  expect(walkProblems(walk)).toEqual([]);
});

test('a black focus outline at 5 % opacity on white is a finding', async ({ page }) => {
  await page.setContent(button('rgba(0,0,0,0.05)'));
  const walk = await keyboardWalk(page);
  // 5 % black over white is 242,242,242: 1.12:1 against white.
  expect(walk.stops[0]?.outlineContrast).toBeGreaterThan(1.1);
  expect(walk.stops[0]?.outlineContrast).toBeLessThan(1.15);
  expect(walkProblems(walk)).toEqual(['button#b has a focus outline with a contrast of 1.117:1']);
});

test('a focus outline at 2.99:1 is a finding', async ({ page }) => {
  // 148,148,148 on white is 3.03:1, 149,149,149 is 2.99:1.
  await page.setContent(button('rgb(148,148,148)'));
  expect(walkProblems(await keyboardWalk(page))).toEqual([]);
  await page.setContent(button('rgb(149,149,149)'));
  const walk = await keyboardWalk(page);
  expect(walk.stops[0]?.outlineContrast).toBeLessThan(3);
  expect(walkProblems(walk)).toHaveLength(1);
});
