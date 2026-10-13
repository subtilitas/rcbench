// Page layout of the PDF export: the document model as A4 pages of PDF content
// operators, with the page-break plan.
//
// - A4 portrait, 595.28 x 841.89 pt, margins 42.52 pt (15 mm).
// - Every block is placed whole: a list entry, a safety note, a table row and
//   the drawing never continue on the next page.
// - A heading stays with the block after it.
// - The table header row repeats on every page that carries table rows.
// - The drawing is scaled to the text width and to at most one page height.
// - Every page ends with the footer: page number and count, then the notice.
//
// A block taller than a page raises LayoutError.

import { colour } from './css.ts';
import type { Css } from './css.ts';
import { advance, codePoints, encode, width } from './font.ts';
import type { Fonts } from './font.ts';
import { lineSample } from './diagram.ts';
import type { Block, Doc, Item, Row, Run, SectionId } from './model.ts';
import { num } from './pdf.ts';
import type { Link, PageSpec } from './pdf.ts';
import { paintSvg } from './svg.ts';
import type { Painted } from './svg.ts';

export class LayoutError extends Error {}

export const PAGE: readonly [number, number] = [595.28, 841.89];
export const MARGIN = 42.52;
const LEFT = MARGIN;
const WIDTH = PAGE[0] - 2 * MARGIN;
const TOP = PAGE[1] - MARGIN;

/** Column widths of the pin-to-pin table in pt. The sum is the text width. */
export const COLUMNS: readonly number[] = [26, 106, 106, 86, 52, WIDTH - 376];
const PAD = 4;
/** Scale of a line sample: 38 x 12 px become 22.8 x 7.2 pt. */
const SAMPLE = 0.6;
const SAMPLE_W = 38 * SAMPLE;

export type Entry = { section: SectionId; kind: 'title' | 'heading' | 'text' | 'link' | 'safety' | 'item' | 'legend' | 'drawing' | 'head' | 'row'; ref?: number };
/** A placed block: `y` is its top edge in pt from the bottom of the page, `h` its height. */
export type Placed = Entry & { y: number; h: number };
export type Laid = {
  pages: PageSpec[];
  /** Per page, the placed blocks from top to bottom. */
  plan: Placed[][];
  /** Lowest y a block may reach: the top edge of the footer. */
  floor: number;
  /** Scale of the drawing: pt per px. */
  drawingScale: number;
  /** The painted drawing, in px: its size, its texts and its boxes. */
  drawing: Painted | null;
};

type RGB = [number, number, number];
type Out = { ops: string; links: Link[] };
type Box = {
  h: number;
  /** Space above, dropped at the top of a page. */
  gap: number;
  /** The box stays on the page of the box after it. */
  keep: boolean;
  /** Table header that precedes the box when no row of the table stands above it on the page. */
  head?: Box;
  entry: Entry;
  draw: (out: Out, top: number) => void;
};
type Piece = { text: string; bold: boolean };
type Line = { pieces: Piece[]; width: number };
type Type = { size: number; lead: number };

const TITLE: Type = { size: 18, lead: 24 };
const H2: Type = { size: 13, lead: 18 };
const H3: Type = { size: 10.5, lead: 15 };
const BODY: Type = { size: 9.5, lead: 13 };
const SMALL: Type = { size: 8.5, lead: 11.5 };
const CELL: Type = { size: 8, lead: 10.5 };
const FOOT: Type = { size: 6.5, lead: 8.5 };

/** Characters a line may break after, besides the space. */
const BREAKS = '-/';
/** The same for ids and addresses, which hold no space. */
const BREAKS_ID = '-/_&,=?#.';

/**
 * Breaks runs into lines of at most `max` pt. A line breaks after a space or a
 * break character; a word wider than the line breaks between characters. A
 * no-break space never breaks.
 */
export function wrap(fonts: Fonts, runs: Run[], size: number, max: number, breaks = BREAKS): Line[] {
  type Ch = { ch: string; bold: boolean; w: number };
  const chars: Ch[] = [];
  for (const run of runs) {
    const font = (run.bold ? fonts.bold : fonts.regular).font;
    for (const cp of codePoints(run.text)) {
      const ch = String.fromCodePoint(cp);
      chars.push({ ch, bold: run.bold === true, w: (advance(font, cp) / font.unitsPerEm) * size });
    }
  }
  const lines: Line[] = [];
  let cur: Ch[] = [];
  let curW = 0;
  const flush = (): void => {
    while (cur.length > 0 && (cur[cur.length - 1] as Ch).ch === ' ') cur.pop();
    const pieces: Piece[] = [];
    let total = 0;
    for (const c of cur) {
      const last = pieces[pieces.length - 1];
      if (last && last.bold === c.bold) last.text += c.ch;
      else pieces.push({ text: c.ch, bold: c.bold });
      total += c.w;
    }
    lines.push({ pieces, width: total });
    cur = [];
    curW = 0;
  };
  const add = (c: Ch): void => {
    if (cur.length === 0 && c.ch === ' ') return;
    cur.push(c);
    curW += c.w;
  };
  let token: Ch[] = [];
  const place = (): void => {
    if (token.length === 0) return;
    let solid = token.length;
    while (solid > 0 && (token[solid - 1] as Ch).ch === ' ') solid--;
    const w = token.slice(0, solid).reduce((sum, c) => sum + c.w, 0);
    if (cur.length > 0 && curW + w > max) flush();
    if (w > max) {
      for (const c of token) {
        if (cur.length > 0 && c.ch !== ' ' && curW + c.w > max) flush();
        add(c);
      }
    } else token.forEach(add);
    token = [];
  };
  for (const c of chars) {
    token.push(c);
    if (c.ch === ' ' || breaks.includes(c.ch)) place();
  }
  place();
  if (cur.length > 0 || lines.length === 0) flush();
  return lines;
}

export function layoutDoc(doc: Doc, css: Css, fonts: Fonts): Laid {
  const token = (name: string): RGB => {
    const value = css.tokens[name];
    const rgb = value === undefined ? null : colour(value);
    if (!rgb) throw new LayoutError(`The style sheet has no colour for ${name}.`);
    return rgb;
  };
  const INK = token('--ink');
  const MUTED = token('--muted');
  const HAIR = token('--hair');
  const GREY = token('--grey-bg');
  const rgb = (c: RGB): string => c.map(num).join(' ');
  const capHeight = fonts.regular.font.capHeight / fonts.regular.font.unitsPerEm;

  /** One line of text. `top` is the top edge of the line box. */
  const lineOps = (line: Line, x: number, top: number, type: Type, ink: RGB): string => {
    if (line.pieces.length === 0) return '';
    const base = top - type.lead / 2 - (capHeight * type.size) / 2;
    let out = `${rgb(ink)} rg\nBT\n`;
    let at = x;
    for (const piece of line.pieces) {
      const font = piece.bold ? fonts.bold : fonts.regular;
      out += `/${piece.bold ? 'F2' : 'F1'} ${num(type.size)} Tf\n1 0 0 1 ${num(at)} ${num(base)} Tm\n<${encode(font, piece.text)}> Tj\n`;
      at += width(font.font, piece.text) * type.size;
    }
    return `${out}ET\n`;
  };
  const linesOps = (lines: Line[], x: number, top: number, type: Type, ink: RGB): string =>
    lines.map((line, i) => lineOps(line, x, top - i * type.lead, type, ink)).join('');
  const rule = (x1: number, x2: number, y: number, w: number, ink: RGB): string =>
    `${rgb(ink)} RG\n${num(w)} w\n${num(x1)} ${num(y)} m\n${num(x2)} ${num(y)} l\nS\n`;
  const vertical = (x: number, y1: number, y2: number, w: number, ink: RGB): string =>
    `${rgb(ink)} RG\n${num(w)} w\n${num(x)} ${num(y1)} m\n${num(x)} ${num(y2)} l\nS\n`;
  /** SVG markup at `x`, `top`, scaled by `scale` pt per px. */
  const svgOps = (ops: string, x: number, top: number, scale: number): string =>
    `q\n${num(scale)} 0 0 ${num(-scale)} ${num(x)} ${num(top)} cm\n${ops}Q\n`;

  // Footer: the same on every page, apart from the page number.
  const footLines = wrap(fonts, [{ text: doc.footer }], FOOT.size, WIDTH);
  const footHeight = (footLines.length + 1) * FOOT.lead + 6;
  const floor = MARGIN + footHeight;
  const room = TOP - floor;

  const flow: Box[] = [];
  let section: SectionId = 'title';
  const push = (kind: Entry['kind'], h: number, gap: number, keep: boolean, draw: Box['draw'], ref?: number): Box => {
    const box: Box = { h, gap, keep, entry: ref === undefined ? { section, kind } : { section, kind, ref }, draw };
    flow.push(box);
    return box;
  };
  const text = (kind: Entry['kind'], runs: Run[], type: Type, ink: RGB, gap: number, keep = false): void => {
    const lines = wrap(fonts, runs, type.size, WIDTH);
    push(kind, lines.length * type.lead, gap, keep, (out, top) => {
      out.ops += linesOps(lines, LEFT, top, type, ink);
    });
  };

  /** First line: runs, then each tag in square brackets. */
  const itemRuns = (item: Item): Run[] => [...item.runs, ...item.tags.map((tag) => ({ text: ` [${tag}]` }))];
  const list = (items: Item[], bullet: boolean): void => {
    const indent = bullet ? 11 : 0;
    items.forEach((item, index) => {
      const first = wrap(fonts, itemRuns(item), BODY.size, WIDTH - indent);
      // A line that is a link breaks inside its address as well, and each of its lines is underlined and clickable.
      const subs = item.lines.map((sub) => ({
        lines: wrap(fonts, [{ text: sub.text }], SMALL.size, WIDTH - indent, sub.url === undefined ? BREAKS : BREAKS_ID),
        ink: sub.muted ? MUTED : INK,
        url: sub.url,
      }));
      const h = first.length * BODY.lead + subs.reduce((sum, s) => sum + s.lines.length * SMALL.lead, 0);
      push('item', h, index === 0 ? 3 : bullet ? 2 : 6, false, (out, top) => {
        if (bullet) out.ops += lineOps({ pieces: [{ text: '•', bold: false }], width: 0 }, LEFT + 1, top, BODY, INK);
        out.ops += linesOps(first, LEFT + indent, top, BODY, INK);
        let y = top - first.length * BODY.lead;
        for (const sub of subs) {
          out.ops += linesOps(sub.lines, LEFT + indent, y, SMALL, sub.ink);
          if (sub.url !== undefined) {
            const url = sub.url;
            sub.lines.forEach((line, i) => {
              const at = y - i * SMALL.lead;
              out.ops += rule(LEFT + indent, LEFT + indent + line.width, at - SMALL.lead + 2, 0.4, sub.ink);
              out.links.push({ x1: LEFT + indent, y1: at - SMALL.lead, x2: LEFT + indent + line.width, y2: at, url });
            });
          }
          y -= sub.lines.length * SMALL.lead;
        }
      });
    });
  };

  const safety = (title: string, notes: Run[][]): void => {
    const inset = 10;
    const indent = 11;
    const border = 1.5;
    const sides = (out: Out, top: number, h: number): void => {
      out.ops += vertical(LEFT + border / 2, top, top - h, border, INK) + vertical(LEFT + WIDTH - border / 2, top, top - h, border, INK);
    };
    const titleH = 8 + H2.lead;
    push('safety', titleH, 12, true, (out, top) => {
      sides(out, top, titleH);
      out.ops += rule(LEFT, LEFT + WIDTH, top - border / 2, border, INK);
      out.ops += lineOps({ pieces: [{ text: title, bold: true }], width: 0 }, LEFT + inset, top - 8, H2, INK);
    });
    notes.forEach((note, index) => {
      const lines = wrap(fonts, note, BODY.size, WIDTH - 2 * inset - indent);
      const last = index === notes.length - 1;
      const h = lines.length * BODY.lead + (last ? 9 : 4);
      push('safety', h, 0, false, (out, top) => {
        sides(out, top, h);
        out.ops += lineOps({ pieces: [{ text: '•', bold: false }], width: 0 }, LEFT + inset + 1, top, BODY, INK);
        out.ops += linesOps(lines, LEFT + inset + indent, top, BODY, INK);
        if (last) out.ops += rule(LEFT, LEFT + WIDTH, top - h + border / 2, border, INK);
      }, index);
    });
    if (notes.length === 0) {
      push('safety', 6, 0, false, (out, top) => {
        sides(out, top, 6);
        out.ops += rule(LEFT, LEFT + WIDTH, top - 6 + border / 2, border, INK);
      });
    }
  };

  const samples = new Map<string, string>();
  const sampleOps = (cls: Row['cls']): string => {
    let ops = samples.get(cls);
    if (ops === undefined) {
      ops = paintSvg(lineSample(cls), css, fonts).ops;
      samples.set(cls, ops);
    }
    return ops;
  };

  const legend = (label: string, entries: Extract<Block, { kind: 'legend' }>['entries']): void => {
    const half = WIDTH / 2;
    const textW = half - SAMPLE_W - 6 - 8;
    const cells = entries.map((e) => ({ cls: e.cls, lines: wrap(fonts, [{ text: `${e.label}: ${e.line}` }], SMALL.size, textW) }));
    const rows: Array<{ h: number; cells: typeof cells }> = [];
    for (let i = 0; i < cells.length; i += 2) {
      const pair = cells.slice(i, i + 2);
      rows.push({ h: Math.max(...pair.map((c) => c.lines.length)) * SMALL.lead + 1, cells: pair });
    }
    const labelH = SMALL.lead + 2;
    const h = labelH + rows.reduce((sum, r) => sum + r.h, 0);
    push('legend', h, 3, false, (out, top) => {
      out.ops += lineOps({ pieces: [{ text: label, bold: true }], width: 0 }, LEFT, top, SMALL, INK);
      let y = top - labelH;
      for (const row of rows) {
        row.cells.forEach((cell, col) => {
          const x = LEFT + col * half;
          out.ops += svgOps(sampleOps(cell.cls), x, y - (SMALL.lead - 12 * SAMPLE) / 2, SAMPLE);
          out.ops += linesOps(cell.lines, x + SAMPLE_W + 6, y, SMALL, INK);
        });
        y -= row.h;
      }
    });
  };

  let drawingScale = 0;
  let drawn: Painted | null = null;
  const drawing = (svg: string): void => {
    const painted = paintSvg(svg, css, fonts);
    const scale = Math.min(WIDTH / painted.width, room / painted.height);
    drawingScale = scale;
    drawn = painted;
    push('drawing', painted.height * scale, 6, false, (out, top) => {
      out.ops += svgOps(painted.ops, LEFT + (WIDTH - painted.width * scale) / 2, top, scale);
    });
  };

  const table = (block: Extract<Block, { kind: 'table' }>): void => {
    const x: number[] = [];
    COLUMNS.reduce((at, w) => {
      x.push(at);
      return at + w;
    }, LEFT);
    const col = (i: number): number => (COLUMNS[i] as number) - 2 * PAD;
    const headLines = block.head.map((label, i) => wrap(fonts, [{ text: label, bold: true }], CELL.size, col(i)));
    const headH = Math.max(...headLines.map((l) => l.length)) * CELL.lead + 2 * PAD;
    const saved = section;
    const head: Box = {
      h: headH, gap: 0, keep: true, entry: { section: saved, kind: 'head' },
      draw: (out, top) => {
        out.ops += `${rgb(GREY)} rg\n${num(LEFT)} ${num(top - headH)} ${num(WIDTH)} ${num(headH)} re\nf\n`;
        headLines.forEach((lines, i) => {
          out.ops += linesOps(lines, (x[i] as number) + PAD, top - PAD, CELL, INK);
        });
        out.ops += rule(LEFT, LEFT + WIDTH, top - headH, 0.5, INK);
      },
    };
    const caption = wrap(fonts, [{ text: block.caption }], SMALL.size, WIDTH);
    push('text', caption.length * SMALL.lead + 3, 8, true, (out, top) => {
      out.ops += linesOps(caption, LEFT, top, SMALL, MUTED);
    });
    for (const row of block.rows) {
      const cells: Line[][] = [
        wrap(fonts, [{ text: String(row.n), bold: true }], CELL.size, col(0)),
        wrap(fonts, [{ text: row.from }], CELL.size, col(1)),
        wrap(fonts, [{ text: row.to, bold: row.toBold }], CELL.size, col(2)),
        wrap(fonts, [{ text: row.signal }], CELL.size, col(3)),
        wrap(fonts, [{ text: row.net }], CELL.size, col(4), BREAKS_ID),
        wrap(fonts, [{ text: row.line }], CELL.size, col(5) - SAMPLE_W - 4),
      ];
      const noteX = (x[1] as number) + PAD;
      const note = row.note === '' ? [] : wrap(fonts, [{ text: `${block.noteLabel}: `, bold: true }, { text: row.note }], CELL.size, LEFT + WIDTH - PAD - noteX);
      const bodyH = Math.max(...cells.map((l) => l.length)) * CELL.lead;
      const h = PAD + bodyH + (note.length > 0 ? note.length * CELL.lead + 1 : 0) + PAD;
      const box = push('row', h, 0, false, (out, top) => {
        cells.forEach((lines, i) => {
          const shift = i === 5 ? SAMPLE_W + 4 : 0;
          out.ops += linesOps(lines, (x[i] as number) + PAD + shift, top - PAD, CELL, INK);
        });
        out.ops += svgOps(sampleOps(row.cls), (x[5] as number) + PAD, top - PAD - (CELL.lead - 12 * SAMPLE) / 2, SAMPLE);
        if (note.length > 0) out.ops += linesOps(note, noteX, top - PAD - bodyH - 1, CELL, MUTED);
        out.ops += rule(LEFT, LEFT + WIDTH, top - h, 0.5, HAIR);
      }, row.n);
      box.head = head;
    }
  };

  for (const part of doc.sections) {
    section = part.id;
    for (const block of part.blocks) {
      switch (block.kind) {
        case 'title':
          text('title', [{ text: block.text, bold: true }], TITLE, INK, 0);
          break;
        case 'heading':
          text('heading', [{ text: block.text, bold: true }], block.level === 2 ? H2 : H3, INK, block.level === 2 ? 16 : 10, true);
          break;
        case 'para':
          text('text', block.runs, block.muted ? SMALL : BODY, block.muted ? MUTED : INK, 3);
          break;
        case 'link': {
          const labelW = width(fonts.regular.font, block.label) * BODY.size;
          const lines = wrap(fonts, [{ text: block.url }], BODY.size, WIDTH - labelW, BREAKS_ID);
          const url = block.url;
          push('link', lines.length * BODY.lead, 3, false, (out, top) => {
            out.ops += lineOps({ pieces: [{ text: block.label, bold: false }], width: labelW }, LEFT, top, BODY, INK);
            lines.forEach((line, i) => {
              const y = top - i * BODY.lead;
              out.ops += lineOps(line, LEFT + labelW, y, BODY, INK);
              // The underline marks the address as a link without colour.
              out.ops += rule(LEFT + labelW, LEFT + labelW + line.width, y - BODY.lead + 2, 0.4, INK);
              out.links.push({ x1: LEFT + labelW, y1: y - BODY.lead, x2: LEFT + labelW + line.width, y2: y, url });
            });
          });
          break;
        }
        case 'safety':
          safety(block.title, block.notes);
          break;
        case 'list':
          list(block.items, block.bullet);
          break;
        case 'legend':
          legend(block.label, block.entries);
          break;
        case 'drawing':
          drawing(block.svg);
          break;
        case 'table':
          table(block);
          break;
      }
    }
  }

  // Pagination.
  const plan: Placed[][] = [[]];
  const outs: Out[] = [{ ops: '', links: [] }];
  let y = TOP;
  /** True when `box` needs its table header above it at this point of the page. */
  const wantsHead = (box: Box): boolean => {
    if (!box.head) return false;
    const page = plan[plan.length - 1] as Placed[];
    const above = page[page.length - 1];
    return !above || (above.kind !== 'row' && above.kind !== 'head') || above.section !== box.entry.section;
  };
  /** Height the box needs, with its header and with the boxes it stays with. */
  const need = (index: number, first: boolean): number => {
    const box = flow[index] as Box;
    const own = box.h + (first ? (box.head && wantsHead(box) ? box.head.h : 0) : box.gap + (box.head ? box.head.h : 0));
    const next = flow[index + 1];
    return box.keep && next ? own + need(index + 1, false) : own;
  };
  const place = (box: Box): void => {
    const page = plan[plan.length - 1] as Placed[];
    page.push({ ...box.entry, y, h: box.h });
    box.draw(outs[outs.length - 1] as Out, y);
    y -= box.h;
  };
  flow.forEach((box, index) => {
    const tallest = box.h + (box.head ? box.head.h : 0);
    if (tallest > room) throw new LayoutError(`A ${box.entry.kind} of the document is ${Math.ceil(tallest)} pt tall. A page holds ${Math.floor(room)} pt.`);
    let empty = (plan[plan.length - 1] as Placed[]).length === 0;
    if (!empty && y - box.gap - need(index, true) < floor) {
      plan.push([]);
      outs.push({ ops: '', links: [] });
      y = TOP;
      empty = true;
    }
    if (!empty) y -= box.gap;
    if (box.head && wantsHead(box)) place(box.head);
    place(box);
  });

  const pages: PageSpec[] = outs.map((out, i) => {
    const label = doc.pageLabel.split('{x}').join(String(i + 1)).split('{y}').join(String(outs.length));
    const labelLine = wrap(fonts, [{ text: label, bold: true }], FOOT.size, WIDTH)[0] as Line;
    const top = floor - 6;
    const foot = rule(LEFT, LEFT + WIDTH, floor - 3, 0.5, HAIR)
      + lineOps(labelLine, LEFT + WIDTH - labelLine.width, top, FOOT, INK)
      + linesOps(footLines, LEFT, top - FOOT.lead, FOOT, MUTED);
    return { ops: out.ops + foot, links: out.links };
  });
  return { pages, plan, floor, drawingScale, drawing: drawn };
}
