// The page-break plan of one laid-out document, checked against the rules of
// `src/export/layout.ts`, and the sweep that runs the check over every
// distinct setup in both languages. A finding is one line of text.
//
// The sweep takes about 8ms per document. 4 test files share it, one part
// each, so the test runner works on them in parallel.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { MARGIN, PAGE, layoutDoc } from '../../src/export/layout.ts';
import type { Laid } from '../../src/export/layout.ts';
import { documentModel } from '../../src/export/model.ts';
import type { Doc } from '../../src/export/model.ts';
import { describe } from '../geometry.ts';
import { LANGS, NOW, SAMPLES, css, fonts, urlOf, viewOf } from './helpers.ts';

export const SHARDS = 4;
const EPS = 0.01;

export function planFindings(doc: Doc, laid: Laid): string[] {
  const out: string[] = [];
  const top = PAGE[1] - MARGIN;
  if (laid.pages.length !== laid.plan.length) out.push('the plan and the pages differ in count');
  if (!(laid.floor > MARGIN)) out.push('the footer has no height');

  laid.plan.forEach((page, index) => {
    const at = `page ${index + 1}`;
    if (page.length === 0) out.push(`${at} is empty`);
    let edge = top;
    for (const b of page) {
      if (b.y > edge + EPS) out.push(`${at}: a ${b.kind} overlaps the block above it`);
      if (b.y - b.h < laid.floor - EPS) out.push(`${at}: a ${b.kind} reaches into the footer`);
      if (!(b.h > 0)) out.push(`${at}: a ${b.kind} has no height`);
      edge = b.y - b.h;
    }
    if (page[0] && Math.abs(page[0].y - top) > EPS) out.push(`${at} does not start at the top margin`);
    const last = page[page.length - 1];
    if (last && (last.kind === 'heading' || last.kind === 'head')) out.push(`${at} ends with a ${last.kind}`);
    const rows = page.filter((b) => b.kind === 'row');
    const heads = page.filter((b) => b.kind === 'head');
    if (rows.length > 0) {
      if (heads.length !== 1) out.push(`${at} carries ${heads.length} table headers for ${rows.length} rows`);
      else if (page.indexOf(heads[0] as (typeof page)[number]) !== page.indexOf(rows[0] as (typeof page)[number]) - 1) out.push(`${at}: the table header is not right above the first row`);
    } else if (heads.length > 0) out.push(`${at} carries a table header without a row`);
  });

  const placed = laid.plan.flat();
  const table = doc.sections.flatMap((s) => s.blocks).find((b) => b.kind === 'table');
  const wanted = table?.kind === 'table' ? table.rows.map((r) => r.n) : [];
  const rows = placed.filter((b) => b.kind === 'row').map((b) => b.ref);
  if (rows.join() !== wanted.join()) out.push('the placed rows are not every row once, in order');

  const safety = doc.sections.flatMap((s) => s.blocks).find((b) => b.kind === 'safety');
  const notes = placed.filter((b) => b.kind === 'safety' && b.ref !== undefined).map((b) => b.ref);
  if (notes.join() !== (safety?.kind === 'safety' ? safety.notes.map((_, i) => i) : []).join()) out.push('the placed safety notes are not every note once, in order');

  const items = doc.sections.flatMap((s) => s.blocks).reduce((sum, b) => sum + (b.kind === 'list' ? b.items.length : 0), 0);
  if (placed.filter((b) => b.kind === 'item').length !== items) out.push('the placed list entries are not every entry once');

  // The order of the sections holds across the pages.
  const order = doc.sections.map((s) => s.id);
  let reached = 0;
  for (const b of placed) {
    const i = order.indexOf(b.section);
    if (i < reached) out.push(`a ${b.kind} of ${b.section} follows a later section`);
    reached = Math.max(reached, i);
  }

  const drawings = placed.filter((b) => b.kind === 'drawing');
  if (drawings.length !== 1 || !laid.drawing) out.push(`the document holds ${drawings.length} drawings`);
  else {
    const d = laid.drawing;
    if (!(laid.drawingScale > 0) || d.width * laid.drawingScale > PAGE[0] - 2 * MARGIN + EPS) out.push('the drawing is wider than the text');
    const inside = (x1: number, x2: number, y: number): boolean =>
      d.boxes.some((r) => x1 >= r.x && x2 <= r.x + r.w && y >= r.y && y <= r.y + r.h);
    for (const text of d.texts) {
      if (text.x1 < 0 || text.x2 > d.width || text.y - text.size < 0 || text.y > d.height) out.push(`drawing text outside the canvas: ${text.text}`);
      else if (!inside(text.x1, text.x2, text.y)) out.push(`drawing text outside its box in the embedded font: ${text.text}`);
    }
  }
  return out;
}

/** Declares the test of one part of the sweep. */
export function sweepPart(part: number): void {
  test(`the page-break plan holds for every setup of the sweep in both languages, part ${part + 1} of ${SHARDS}`, () => {
    const sheet = css();
    const glyphs = fonts();
    const findings: string[] = [];
    let documents = 0;
    let most = 0;
    for (let i = part; i < SAMPLES.length; i += SHARDS) {
      const sample = SAMPLES[i] as (typeof SAMPLES)[number];
      for (const lang of LANGS) {
        const view = viewOf(sample.selection, sample.setup, lang);
        const doc = documentModel(view, urlOf(view), NOW);
        const laid = layoutDoc(doc, sheet, glyphs);
        for (const f of planFindings(doc, laid)) findings.push(`${describe(sample.selection)} ${lang}: ${f}`);
        documents++;
        most = Math.max(most, laid.pages.length);
      }
    }
    assert.deepEqual(findings.slice(0, 10), []);
    assert.equal(documents, 2 * Math.ceil((SAMPLES.length - part) / SHARDS));
    assert.ok(most >= 3 && most <= 14, `the longest document has ${most} pages`);
  });
}
