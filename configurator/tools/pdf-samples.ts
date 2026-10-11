// Writes 2 sample files of the PDF export: the setup with the fewest wires in
// English and the setup with the most wires in German, out of every distinct
// setup of the sweep.
//
//   node tools/pdf-samples.ts <folder> [base URL of the page]
//
// The date of the documents is the date of the run.

import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import type { Lang } from '../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../src/core.ts';
import { buildPdf } from '../src/export/build.ts';
import { formatHash } from '../src/hash.ts';
import { optionsOf } from '../src/names.ts';
import { sweep } from '../test/geometry.ts';
import type { Sample } from '../test/geometry.ts';

const folder = process.argv[2];
const base = process.argv[3] ?? 'https://subtilitas.github.io/rcbench/';
if (!folder) {
  console.error('usage: node tools/pdf-samples.ts <folder> [base URL of the page]');
  process.exit(1);
}

const read = (path: string): Uint8Array => new Uint8Array(readFileSync(new URL(path, import.meta.url)));
const css = readFileSync(new URL('../src/style.css', import.meta.url), 'utf8');
const fonts = { regular: read('../src/export/font/Inter-Regular-subset.ttf'), bold: read('../src/export/font/Inter-Bold-subset.ttf') };

const samples = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS });
const byWires = [...samples].sort((a, b) => a.setup.wires.length - b.setup.wires.length || a.setup.parts.length - b.setup.parts.length);
const picks: Array<[Sample, Lang]> = [[byWires[0] as Sample, 'en'], [byWires[byWires.length - 1] as Sample, 'de']];

mkdirSync(folder, { recursive: true });
for (const [sample, lang] of picks) {
  const { selection, setup } = sample;
  const url = `${base}#${formatHash(selection, lang, 'en', CATALOGUE)}`;
  const view = { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
  const made = await buildPdf({ view, url, now: new Date(), css, fonts });
  const path = join(folder, made.fileName);
  writeFileSync(path, made.bytes);
  console.log(`${path}: ${made.bytes.length} bytes, ${made.laid.pages.length} pages, ${setup.parts.length} parts, ${setup.wires.length} wires`);
}
