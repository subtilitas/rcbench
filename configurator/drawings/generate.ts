// Builds the part drawings and their anchor table from the catalogue.
// Pure: the same catalogue gives the same bytes. No file, no clock, no random number.

import type { Catalogue, Lang, Part, Text } from '../contract.ts';
import { DEFAULT_OPTIONS } from '../core/resolve.ts';
import { Dwg } from './lib.ts';
import type { Side } from './lib.ts';
import { INA3221_MAP, LIST } from './parts.ts';
import type { Entry } from './parts.ts';

export const LANGS: ReadonlyArray<Lang> = ['en', 'de'];

/** One pin of a drawing: the wire point in px of the viewBox, the side a wire leaves on, the label text. */
export type AnchorPin = { x: number; y: number; side: Side; labelId: string; label: Text };

export type DrawingRecord = {
  /** Part id of the catalogue. null: the part is not in the catalogue and is not wired. */
  part: string | null;
  variant: string | null;
  viewBox: [number, number, number, number];
  /** Where the pin ids and labels come from. */
  names: 'board print' | 'catalogue';
  pins: Record<string, AnchorPin>;
  /** Catalogue pin id to pin ids of the drawing, for a drawing that carries the print of the board. */
  catalogueMap?: Record<string, string[] | null>;
};

export type Anchors = {
  /** Release of the catalogue the table is built from. */
  release: string;
  files: Record<string, DrawingRecord>;
};

export type Output = {
  anchors: Anchors;
  /** Path below the output folder to file content: `<file>.svg` in English, `de/<file>.svg` in German. */
  svgs: Record<string, string>;
};

/** Path of a drawing below the output folder. */
export function pathOf(file: string, lang: Lang): string {
  return lang === 'en' ? file : `de/${file}`;
}

/** File name of the drawing of a part, or of one of its 2 boards. */
export function fileOf(part: string, variant: string | null): string {
  return variant === null ? `${part}.svg` : `${part}--${variant}.svg`;
}

/**
 * Pins of a part as the catalogue names them for one drawing: pin id to label.
 * A pin whose `only` names another board than the drawing shows is left out.
 * The coprocessor carries its 40 pads and the 2 poles of the CAN terminal.
 */
export function cataloguePins(catalogue: Catalogue, part: Part, variant: string | null): Record<string, Text> {
  const out: Record<string, Text> = {};
  if (part.id === 'copro') {
    for (const pad of catalogue.pads) {
      const s = variant === 'osoyoo' ? pad.breakoutLabel : pad.name;
      out[`pad.${pad.n}`] = { en: s, de: s };
    }
    for (const wire of catalogue.wires) {
      for (const end of [wire.a, wire.b]) {
        if (end !== null && end.startsWith('can.') && !(end in out)) out[end] = { ...wire.signal };
      }
    }
    return out;
  }
  const key = part.variantName?.option;
  const shown = key === undefined ? undefined : variant ?? DEFAULT_OPTIONS[key];
  for (const pin of part.pins) {
    const other = key !== undefined && pin.only?.[key] !== undefined && pin.only[key] !== shown;
    if (!other) out[pin.id] = { ...pin.label };
  }
  return out;
}

const same = (m: Record<string, string>): Record<string, Text> =>
  Object.fromEntries(Object.entries(m).map(([id, s]) => [id, { en: s, de: s }]));

function partOf(catalogue: Catalogue, entry: Entry): Part | null {
  if (entry.part === null) return null;
  const part = catalogue.parts.find((p) => p.id === entry.part);
  if (!part) throw new Error(`${entry.file}: part ${entry.part} is not in the catalogue`);
  return part;
}

/** Title of a drawing: the name of the part, with the name of the board for a part with 2 boards. */
function titleOf(part: Part | null, entry: Entry, lang: Lang): string {
  if (!part) {
    if (!entry.name) throw new Error(`${entry.file}: a drawing without a part needs a name`);
    return entry.name[lang];
  }
  const board = entry.variant === undefined ? undefined : part.variantName?.names[entry.variant];
  return board ? `${part.name[lang]}, ${board[lang]}` : part.name[lang];
}

function labelsOf(catalogue: Catalogue, part: Part | null, entry: Entry): Record<string, Text> {
  if (entry.native) return same(entry.native);
  if (!part) throw new Error(`${entry.file}: a drawing without a part needs the print of its board`);
  return cataloguePins(catalogue, part, entry.variant ?? null);
}

/** Every drawing in both languages, and the anchor table. Throws when a pin of the catalogue is not drawn. */
export function generate(catalogue: Catalogue): Output {
  const files: Array<[string, DrawingRecord]> = [];
  const svgs: Record<string, string> = {};
  for (const entry of LIST) {
    const part = partOf(catalogue, entry);
    const labels = labelsOf(catalogue, part, entry);
    const file = `${entry.file}.svg`;
    const record: DrawingRecord = {
      part: entry.part,
      variant: entry.variant ?? null,
      viewBox: [0, 0, entry.w, entry.h],
      names: entry.native ? 'board print' : 'catalogue',
      pins: {},
    };
    if (entry.file === 'ina3221') record.catalogueMap = INA3221_MAP;
    for (const lang of LANGS) {
      const d = new Dwg({
        file: entry.file, pfx: entry.pfx, w: entry.w, h: entry.h,
        title: titleOf(part, entry, lang), desc: entry.desc[lang], lang, labels,
      });
      entry.draw(d);
      const missing = Object.keys(labels).filter((id) => !d.seen.has(id));
      if (missing.length > 0) throw new Error(`${entry.file}: pins not drawn: ${missing.join(', ')}`);
      svgs[pathOf(file, lang)] = d.svg();
      for (const [id, a] of Object.entries(d.anchors)) {
        const pin = record.pins[id] ?? { x: a.x, y: a.y, side: a.side, labelId: a.labelId, label: { en: '', de: '' } };
        pin.label[lang] = a.label;
        record.pins[id] = pin;
      }
    }
    files.push([file, record]);
  }
  files.sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
  return { anchors: { release: catalogue.release, files: Object.fromEntries(files) }, svgs };
}

/** The anchor table as the text of `anchors.json`. */
export function anchorsText(anchors: Anchors): string {
  return `${JSON.stringify(anchors, null, 1)}\n`;
}
