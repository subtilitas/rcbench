// Shared parts of the export tests: the files the export reads, the sweep of
// setups, and a reader of the produced PDF built on pdf.js.

import { readFileSync } from 'node:fs';
import { getDocument, VerbosityLevel } from 'pdfjs-dist/legacy/build/pdf.mjs';
import type { Catalogue, Lang, Selection, Setup } from '../../contract.ts';
import { CATALOGUE, DEFAULT_OPTIONS, resolve } from '../../src/core.ts';
import { buildPdf } from '../../src/export/build.ts';
import type { Exported, ExportInput } from '../../src/export/build.ts';
import { parseCss } from '../../src/export/css.ts';
import type { Css } from '../../src/export/css.ts';
import { parseFont, use } from '../../src/export/font.ts';
import type { Fonts } from '../../src/export/font.ts';
import { optionsOf } from '../../src/names.ts';
import type { View } from '../../src/view/chooser.ts';
import { sweep } from '../geometry.ts';
import type { Sample } from '../geometry.ts';

const bytes = (path: string): Uint8Array => new Uint8Array(readFileSync(new URL(path, import.meta.url)));

export const LANGS: readonly Lang[] = ['en', 'de'];
export const CSS_TEXT = readFileSync(new URL('../../src/style.css', import.meta.url), 'utf8');
export const FONT_BYTES = {
  regular: bytes('../../src/export/font/Inter-Regular-subset.ttf'),
  bold: bytes('../../src/export/font/Inter-Bold-subset.ttf'),
};
export const URL_BASE = 'https://example.org/rcbench/';
/** Noon, so the local calendar date is the same in every time zone from UTC-11 to UTC+11. */
export const NOW = new Date('2026-03-04T12:00:00Z');

export const css = (): Css => parseCss(CSS_TEXT);
/** Fresh fonts: a document records the glyphs it uses in them. */
export const fonts = (): Fonts => ({ regular: use(parseFont(FONT_BYTES.regular)), bold: use(parseFont(FONT_BYTES.bold)) });

/** Every distinct setup of the sweep, ordered by wire count, then part count. */
export const SAMPLES: Sample[] = sweep({ CATALOGUE, resolve, DEFAULT_OPTIONS })
  .sort((a, b) => a.setup.wires.length - b.setup.wires.length || a.setup.parts.length - b.setup.parts.length);
export const SMALLEST = SAMPLES[0] as Sample;
export const LARGEST = SAMPLES[SAMPLES.length - 1] as Sample;
export const EMPTY: Selection = { functions: [], owned: [], options: { ...DEFAULT_OPTIONS } };

export function viewOf(selection: Selection, setup: Setup, lang: Lang, catalogue: Catalogue = CATALOGUE): View {
  return { lang, catalogue, setup, options: optionsOf(setup), selection };
}

export const urlOf = (view: View): string => `${URL_BASE}#v=1&l=${view.lang}&n=${view.setup.wires.length}`;

export function build(view: View, more: Partial<ExportInput> = {}): Promise<Exported> {
  return buildPdf({ view, url: urlOf(view), now: NOW, css: CSS_TEXT, fonts: FONT_BYTES, ...more });
}

export type Read = {
  pages: number;
  /** Text of each page, items joined by a space. */
  text: string[];
  /** Text items of each page in the order of the content stream, without blank items. */
  items: string[][];
  title: string;
  creator: string;
  lang: string;
  /** Target of every link annotation. */
  links: string[];
};

/**
 * Parses the bytes with pdf.js. Throws when they are no PDF. The text comes
 * back as the file maps it: pdf.js would otherwise fold the micro sign U+00B5
 * into the Greek letter U+03BC.
 */
export async function readPdf(data: Uint8Array): Promise<Read> {
  const task = getDocument({ data: data.slice(), verbosity: VerbosityLevel.ERRORS, useSystemFonts: false, disableFontFace: true });
  const pdf = await task.promise;
  try {
    const text: string[] = [];
    const items: string[][] = [];
    const links: string[] = [];
    for (let n = 1; n <= pdf.numPages; n++) {
      const page = await pdf.getPage(n);
      const content = await page.getTextContent({ disableNormalization: true });
      const strs = content.items.map((item) => ('str' in item ? item.str : ''));
      text.push(strs.join(' '));
      items.push(strs.filter((s) => s.trim() !== ''));
      for (const a of await page.getAnnotations()) {
        if (a.subtype === 'Link') links.push(String(a.url ?? a.unsafeUrl ?? ''));
      }
    }
    const meta = await pdf.getMetadata();
    const info = meta.info as { Title?: string; Creator?: string; Language?: string };
    return { pages: pdf.numPages, text, items, title: info.Title ?? '', creator: info.Creator ?? '', lang: info.Language ?? '', links };
  } finally {
    await task.destroy();
  }
}

/** Text without any white space: line breaks of the layout do not change it. */
export const squeeze = (text: string): string => text.replace(/[\s ]+/g, '');

/** The bytes as a string of one character per byte. */
export const latin = (data: Uint8Array): string => Buffer.from(data).toString('latin1');
