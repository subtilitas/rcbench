// PDF 1.7 writer for the setup export: pages of content operators, 2 embedded
// TrueType fonts addressed by glyph index (Type 0, Identity-H) with a ToUnicode
// map, link annotations, document title and language. The file holds no
// script, no form, no attachment and no reference to another file.
//
// Streams are deflated by the CompressionStream of the platform. Without it
// the streams are stored as they are and the file is larger.

import type { FontUse, Fonts } from './font.ts';

export type Link = { x1: number; y1: number; x2: number; y2: number; url: string };
export type PageSpec = { ops: string; links: Link[] };
export type Meta = {
  title: string;
  /** BCP 47 tag of the document language. */
  lang: string;
  creator: string;
  date: Date;
};

/** A number as PDF writes it: at most 3 decimals, no exponent. */
export function num(value: number): string {
  if (!Number.isFinite(value)) throw new Error('A coordinate of the PDF is not a finite number.');
  const rounded = Math.round(value * 1000) / 1000;
  return Object.is(rounded, -0) ? '0' : String(rounded);
}

function ascii(text: string): Uint8Array {
  const out = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    if (code > 0xff) throw new Error('A PDF object holds a character outside Latin-1.');
    out[i] = code;
  }
  return out;
}

/** A text string object: UTF-16BE with byte order mark, hexadecimal. */
export function textString(text: string): string {
  let hex = 'FEFF';
  for (let i = 0; i < text.length; i++) hex += text.charCodeAt(i).toString(16).padStart(4, '0').toUpperCase();
  return `<${hex}>`;
}

/** A literal string of ASCII characters. Other characters are an error. */
export function literal(text: string): string {
  if (/[^\x20-\x7e]/.test(text)) throw new Error('A literal string of the PDF holds a character outside ASCII.');
  return `(${text.replace(/[\\()]/g, (c) => `\\${c}`)})`;
}

/** The address of a link as ASCII. Characters outside ASCII are percent-encoded. */
export function uri(url: string): string {
  return /[^\x21-\x7e]/.test(url) ? encodeURI(url) : url;
}

export async function deflate(data: Uint8Array): Promise<Uint8Array | null> {
  if (typeof CompressionStream === 'undefined') return null;
  const stream = new Blob([data as Uint8Array<ArrayBuffer>]).stream().pipeThrough(new CompressionStream('deflate'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/** FNV-1a, 32 bit, over bytes. */
export function fnv(data: Uint8Array, seed = 0x811c9dc5): number {
  let h = seed >>> 0;
  for (let i = 0; i < data.length; i++) {
    h ^= data[i] as number;
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h;
}

const hex8 = (value: number): string => value.toString(16).padStart(8, '0');

/** 6 capital letters that mark an embedded font as a subset. */
function subsetTag(fontUse: FontUse): string {
  let h = fnv(ascii([...fontUse.used.keys()].sort((a, b) => a - b).join(',')));
  let tag = '';
  for (let i = 0; i < 6; i++) {
    tag += String.fromCharCode(65 + (h % 26));
    h = Math.floor(h / 26);
  }
  return tag;
}

function toUnicode(fontUse: FontUse): string {
  const pairs = [...fontUse.used.entries()].sort((a, b) => a[0] - b[0]);
  let body = '';
  for (let i = 0; i < pairs.length; i += 100) {
    const block = pairs.slice(i, i + 100);
    body += `${block.length} beginbfchar\n`;
    for (const [glyph, cp] of block) {
      const s = String.fromCodePoint(cp);
      let target = '';
      for (let k = 0; k < s.length; k++) target += s.charCodeAt(k).toString(16).padStart(4, '0');
      body += `<${glyph.toString(16).padStart(4, '0')}> <${target}>\n`.toUpperCase();
    }
    body += 'endbfchar\n';
  }
  return '/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n'
    + '/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n'
    + '/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n'
    + '1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n'
    + body
    + 'endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n';
}

function widths(fontUse: FontUse): string {
  const { font } = fontUse;
  const glyphs = [...fontUse.used.keys()].sort((a, b) => a - b);
  const groups: string[] = [];
  let run: number[] = [];
  let first = -1;
  const flush = (): void => {
    if (run.length > 0) groups.push(`${first} [${run.join(' ')}]`);
    run = [];
  };
  let previous = -2;
  for (const glyph of glyphs) {
    if (glyph !== previous + 1) {
      flush();
      first = glyph;
    }
    run.push(Math.round(((font.advance[glyph] as number) * 1000) / font.unitsPerEm));
    previous = glyph;
  }
  flush();
  return groups.join(' ');
}

class Writer {
  private readonly chunks: Uint8Array[] = [];
  private readonly offsets: number[] = [0];
  private length = 0;
  private readonly compress: boolean;

  constructor(compress: boolean) {
    this.compress = compress;
    this.push(ascii('%PDF-1.7\n%\xe2\xe3\xcf\xd3\n'));
  }

  private push(chunk: Uint8Array): void {
    this.chunks.push(chunk);
    this.length += chunk.length;
  }

  /** Reserves an object number. */
  reserve(): number {
    this.offsets.push(-1);
    return this.offsets.length - 1;
  }

  object(id: number, dict: string): void {
    this.offsets[id] = this.length;
    this.push(ascii(`${id} 0 obj\n${dict}\nendobj\n`));
  }

  async stream(id: number, entries: string, data: Uint8Array): Promise<void> {
    const packed = this.compress ? await deflate(data) : null;
    const body = packed ?? data;
    this.offsets[id] = this.length;
    this.push(ascii(`${id} 0 obj\n<< ${entries}${packed ? ' /Filter /FlateDecode' : ''} /Length ${body.length} >>\nstream\n`));
    this.push(body);
    this.push(ascii('\nendstream\nendobj\n'));
  }

  finish(root: number, info: number): Uint8Array {
    const body = new Uint8Array(this.length);
    let at = 0;
    for (const chunk of this.chunks) {
      body.set(chunk, at);
      at += chunk.length;
    }
    const id = hex8(fnv(body)) + hex8(fnv(body, 0x9747b28c)) + hex8(fnv(body, 0x01234567)) + hex8(fnv(body, 0x89abcdef));
    let xref = `xref\n0 ${this.offsets.length}\n0000000000 65535 f \n`;
    for (let i = 1; i < this.offsets.length; i++) {
      const offset = this.offsets[i] as number;
      if (offset < 0) throw new Error(`Object ${i} of the PDF has no content.`);
      xref += `${String(offset).padStart(10, '0')} 00000 n \n`;
    }
    const tail = ascii(`${xref}trailer\n<< /Size ${this.offsets.length} /Root ${root} 0 R /Info ${info} 0 R /ID [<${id}> <${id}>] >>\n`
      + `startxref\n${this.length}\n%%EOF\n`);
    const out = new Uint8Array(body.length + tail.length);
    out.set(body, 0);
    out.set(tail, body.length);
    return out;
  }
}

async function fontObjects(w: Writer, id: number, fontUse: FontUse, bold: boolean): Promise<void> {
  const { font } = fontUse;
  const scale = (v: number): number => Math.round((v * 1000) / font.unitsPerEm);
  const name = `/${subsetTag(fontUse)}+${font.name}`;
  const cid = w.reserve();
  const descriptor = w.reserve();
  const file = w.reserve();
  const map = w.reserve();
  w.object(id, `<< /Type /Font /Subtype /Type0 /BaseFont ${name} /Encoding /Identity-H /DescendantFonts [${cid} 0 R] /ToUnicode ${map} 0 R >>`);
  w.object(cid, `<< /Type /Font /Subtype /CIDFontType2 /BaseFont ${name} /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >>`
    + ` /FontDescriptor ${descriptor} 0 R /DW 1000 /W [${widths(fontUse)}] /CIDToGIDMap /Identity >>`);
  w.object(descriptor, `<< /Type /FontDescriptor /FontName ${name} /Flags 4 /FontBBox [${font.box.map(scale).join(' ')}]`
    + ` /ItalicAngle ${num(font.italicAngle)} /Ascent ${scale(font.ascent)} /Descent ${scale(font.descent)} /CapHeight ${scale(font.capHeight)}`
    + ` /StemV ${bold ? 140 : 80} /FontFile2 ${file} 0 R >>`);
  await w.stream(file, `/Length1 ${font.bytes.length}`, font.bytes);
  await w.stream(map, '', ascii(toUnicode(fontUse)));
}

function pdfDate(date: Date): string {
  const p = (v: number, n = 2): string => String(v).padStart(n, '0');
  return `D:${p(date.getUTCFullYear(), 4)}${p(date.getUTCMonth() + 1)}${p(date.getUTCDate())}`
    + `${p(date.getUTCHours())}${p(date.getUTCMinutes())}${p(date.getUTCSeconds())}Z`;
}

/**
 * The PDF file. Content operators name the regular font /F1 and the bold font
 * /F2. `fonts` carries the glyphs the operators use.
 */
export async function writePdf(
  pages: PageSpec[], fonts: Fonts, meta: Meta, size: readonly [number, number], compress = true,
): Promise<Uint8Array> {
  if (pages.length === 0) throw new Error('The PDF has no page.');
  const w = new Writer(compress);
  const root = w.reserve();
  const tree = w.reserve();
  const info = w.reserve();
  const f1 = w.reserve();
  const f2 = w.reserve();
  await fontObjects(w, f1, fonts.regular, false);
  await fontObjects(w, f2, fonts.bold, true);

  const kids: number[] = [];
  for (const page of pages) {
    const id = w.reserve();
    const content = w.reserve();
    kids.push(id);
    const annots: number[] = [];
    for (const link of page.links) {
      const a = w.reserve();
      annots.push(a);
      w.object(a, `<< /Type /Annot /Subtype /Link /Rect [${num(link.x1)} ${num(link.y1)} ${num(link.x2)} ${num(link.y2)}] /Border [0 0 0]`
        + ` /A << /Type /Action /S /URI /URI ${literal(uri(link.url))} >> >>`);
    }
    await w.stream(content, '', ascii(page.ops));
    w.object(id, `<< /Type /Page /Parent ${tree} 0 R /Contents ${content} 0 R`
      + `${annots.length > 0 ? ` /Annots [${annots.map((a) => `${a} 0 R`).join(' ')}]` : ''} >>`);
  }
  w.object(tree, `<< /Type /Pages /Kids [${kids.map((k) => `${k} 0 R`).join(' ')}] /Count ${kids.length}`
    + ` /MediaBox [0 0 ${num(size[0])} ${num(size[1])}] /Resources << /Font << /F1 ${f1} 0 R /F2 ${f2} 0 R >> >> >>`);
  w.object(root, `<< /Type /Catalog /Pages ${tree} 0 R /Lang ${literal(meta.lang)} /ViewerPreferences << /DisplayDocTitle true >> >>`);
  const date = pdfDate(meta.date);
  w.object(info, `<< /Title ${textString(meta.title)} /Creator ${textString(meta.creator)} /Producer ${textString(meta.creator)}`
    + ` /CreationDate (${date}) /ModDate (${date}) >>`);
  return w.finish(root, info);
}
