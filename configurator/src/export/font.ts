// Reader for a TrueType font file: the metrics and the character map the PDF
// writer needs. It reads the tables head, hhea, hmtx, maxp, cmap, OS/2, post
// and name. Glyph outlines stay in the file, which the PDF embeds whole.

export type Font = {
  /** PostScript name, name record 6. */
  name: string;
  bytes: Uint8Array;
  unitsPerEm: number;
  ascent: number;
  descent: number;
  capHeight: number;
  italicAngle: number;
  box: [number, number, number, number];
  /** Code point to glyph index. */
  cmap: Map<number, number>;
  /** Advance width per glyph index in font units. */
  advance: number[];
};

/** A font with the glyphs a document uses, and the code point each stands for. */
export type FontUse = { font: Font; used: Map<number, number> };

export type Fonts = { regular: FontUse; bold: FontUse };

export class FontError extends Error {}

function tables(view: DataView): Map<string, number> {
  const out = new Map<string, number>();
  const count = view.getUint16(4);
  for (let i = 0; i < count; i++) {
    const at = 12 + i * 16;
    const tag = String.fromCharCode(view.getUint8(at), view.getUint8(at + 1), view.getUint8(at + 2), view.getUint8(at + 3));
    const offset = view.getUint32(at + 8);
    // The file is embedded whole: a table that ends after the file makes a font a reader refuses.
    if (offset + view.getUint32(at + 12) > view.byteLength) throw new FontError(`The font file ends inside the table ${tag}.`);
    out.set(tag, offset);
  }
  return out;
}

function readCmap(view: DataView, at: number): Map<number, number> {
  const count = view.getUint16(at + 2);
  let best = -1;
  let rank = 0;
  for (let i = 0; i < count; i++) {
    const platform = view.getUint16(at + 4 + i * 8);
    const encoding = view.getUint16(at + 6 + i * 8);
    const offset = at + view.getUint32(at + 8 + i * 8);
    const format = view.getUint16(offset);
    const unicode = platform === 0 || (platform === 3 && (encoding === 1 || encoding === 10));
    const score = !unicode ? 0 : format === 12 ? 2 : format === 4 ? 1 : 0;
    if (score > rank) {
      rank = score;
      best = offset;
    }
  }
  if (best < 0) throw new FontError('The font has no Unicode character map of format 4 or 12.');
  const map = new Map<number, number>();
  if (rank === 2) {
    const groups = view.getUint32(best + 12);
    for (let g = 0; g < groups; g++) {
      const first = view.getUint32(best + 16 + g * 12);
      const last = view.getUint32(best + 20 + g * 12);
      const glyph = view.getUint32(best + 24 + g * 12);
      for (let c = first; c <= last; c++) map.set(c, glyph + c - first);
    }
    return map;
  }
  const segs = view.getUint16(best + 6) / 2;
  const ends = best + 14;
  const starts = ends + segs * 2 + 2;
  const deltas = starts + segs * 2;
  const ranges = deltas + segs * 2;
  for (let s = 0; s < segs; s++) {
    const end = view.getUint16(ends + s * 2);
    const start = view.getUint16(starts + s * 2);
    const delta = view.getUint16(deltas + s * 2);
    const range = view.getUint16(ranges + s * 2);
    for (let c = start; c <= end && c !== 0xffff; c++) {
      let glyph: number;
      if (range === 0) glyph = (c + delta) & 0xffff;
      else {
        const raw = view.getUint16(ranges + s * 2 + range + (c - start) * 2);
        glyph = raw === 0 ? 0 : (raw + delta) & 0xffff;
      }
      if (glyph !== 0) map.set(c, glyph);
    }
  }
  return map;
}

function readName(view: DataView, at: number): string {
  const count = view.getUint16(at + 2);
  const strings = at + view.getUint16(at + 4);
  for (let i = 0; i < count; i++) {
    const rec = at + 6 + i * 12;
    if (view.getUint16(rec + 6) !== 6 || view.getUint16(rec) !== 3) continue;
    const length = view.getUint16(rec + 8);
    const offset = strings + view.getUint16(rec + 10);
    let name = '';
    for (let k = 0; k < length; k += 2) name += String.fromCharCode(view.getUint16(offset + k));
    return name.replace(/[^A-Za-z0-9-]/g, '');
  }
  return '';
}

/** Reads a TrueType file. A file that ends inside a table is an error, like a file of another kind. */
export function parseFont(bytes: Uint8Array): Font {
  try {
    return readFont(bytes);
  } catch (error) {
    if (error instanceof RangeError) throw new FontError('The font file ends inside a table.');
    throw error;
  }
}

function readFont(bytes: Uint8Array): Font {
  if (bytes.length < 12) throw new FontError('The font file is shorter than its header.');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0) !== 0x00010000) throw new FontError('The font file is not a TrueType file with glyf outlines.');
  const table = tables(view);
  const at = (tag: string): number => {
    const offset = table.get(tag);
    if (offset === undefined) throw new FontError(`The font has no ${tag} table.`);
    return offset;
  };
  const head = at('head');
  const hhea = at('hhea');
  const hmtx = at('hmtx');
  const glyphs = view.getUint16(at('maxp') + 4);
  const metrics = view.getUint16(hhea + 34);
  const advance: number[] = [];
  for (let g = 0; g < glyphs; g++) advance.push(view.getUint16(hmtx + Math.min(g, metrics - 1) * 4));
  const os2 = table.get('OS/2');
  const ascent = view.getInt16(hhea + 4);
  const capHeight = os2 !== undefined && view.getUint16(os2) >= 2 ? view.getInt16(os2 + 88) : ascent;
  const post = table.get('post');
  return {
    name: readName(view, at('name')) || 'Font',
    bytes,
    unitsPerEm: view.getUint16(head + 18),
    ascent,
    descent: view.getInt16(hhea + 6),
    capHeight,
    italicAngle: post === undefined ? 0 : view.getInt32(post + 4) / 65536,
    box: [view.getInt16(head + 36), view.getInt16(head + 38), view.getInt16(head + 40), view.getInt16(head + 42)],
    cmap: readCmap(view, at('cmap')),
    advance,
  };
}

export const use = (font: Font): FontUse => ({ font, used: new Map() });

/** Code points of a text. A line feed and a tab count as a space. */
export function codePoints(text: string): number[] {
  const out: number[] = [];
  for (const ch of text) {
    const cp = ch.codePointAt(0) as number;
    out.push(cp === 0x0a || cp === 0x09 || cp === 0x0d ? 0x20 : cp);
  }
  return out;
}

/** Characters of a text the font has no glyph for. */
export function missing(font: Font, text: string): string[] {
  return [...new Set(codePoints(text).filter((cp) => !font.cmap.has(cp)).map((cp) => String.fromCodePoint(cp)))];
}

/** Advance of one code point in font units. A character without a glyph is an error. */
export function advance(font: Font, cp: number): number {
  const glyph = font.cmap.get(cp);
  if (glyph === undefined) throw new FontError(`No glyph for U+${cp.toString(16).toUpperCase().padStart(4, '0')} in ${font.name}.`);
  return font.advance[glyph] as number;
}

/** Width of a text in units of the font size. A character without a glyph is an error. */
export function width(font: Font, text: string): number {
  let sum = 0;
  for (const cp of codePoints(text)) sum += advance(font, cp);
  return sum / font.unitsPerEm;
}

/**
 * The text as the hexadecimal string of a PDF text operator: 2 bytes per glyph
 * index. Records each glyph with the first code point that asked for it.
 */
export function encode(fontUse: FontUse, text: string): string {
  let hex = '';
  for (const cp of codePoints(text)) {
    const glyph = fontUse.font.cmap.get(cp);
    if (glyph === undefined) throw new FontError(`No glyph for U+${cp.toString(16).toUpperCase().padStart(4, '0')} in ${fontUse.font.name}.`);
    if (!fontUse.used.has(glyph)) fontUse.used.set(glyph, cp);
    hex += glyph.toString(16).padStart(4, '0');
  }
  return hex.toUpperCase();
}
