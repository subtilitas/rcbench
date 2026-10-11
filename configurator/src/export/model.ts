// The document model of the PDF export: a resolved setup as ordered sections
// of blocks. Pure: no document, no fonts, no page geometry. Every fact comes
// from the catalogue and the setup; chrome strings come from the string tables.
//
// Section order: title, safety, parts, tested, power, functions, wiring,
// order, nir.

import type { Check, Lang, OptionKey, SignalClass } from '../../contract.ts';
import { formatHash } from '../hash.ts';
import { LEGEND, t, tx } from '../i18n.ts';
import type { Key } from '../i18n.ts';
import { benchParts, endName, powerGroups, powerNumbers, targetName, testObjectsOf } from '../names.ts';
import type { NameCtx } from '../names.ts';
import {
  VARIANTS, powerLines, powerTitle, reasonText, runsOnBase, usedByText,
} from '../view/chooser.ts';
import type { View } from '../view/chooser.ts';
import { wiringDiagram } from './diagram.ts';
import type { Diagram } from './diagram.ts';
import { lineWords, p } from './text.ts';

export type Run = { text: string; bold?: boolean };
export type Sub = { text: string; muted: boolean };
/** One list entry: a first line with tags in brackets, then lines in small type. */
export type Item = { runs: Run[]; tags: string[]; lines: Sub[] };
export type Row = {
  n: number;
  from: string;
  to: string;
  /** The second end is no pin: a stub's text. */
  toBold: boolean;
  signal: string;
  net: string;
  cls: SignalClass;
  /** Colour and pattern of the line in words. */
  line: string;
  note: string;
};
export type LegendEntry = { cls: SignalClass; label: string; line: string };
export type Block =
  | { kind: 'title'; text: string }
  | { kind: 'heading'; text: string; level: 2 | 3 }
  | { kind: 'para'; runs: Run[]; muted: boolean }
  | { kind: 'link'; label: string; url: string }
  | { kind: 'safety'; title: string; notes: Run[][] }
  | { kind: 'list'; items: Item[]; bullet: boolean }
  | { kind: 'legend'; label: string; entries: LegendEntry[] }
  | { kind: 'drawing'; svg: string }
  | { kind: 'table'; caption: string; head: string[]; noteLabel: string; rows: Row[] };

export type SectionId = 'title' | 'safety' | 'parts' | 'tested' | 'power' | 'functions' | 'wiring' | 'order' | 'nir';
export type Section = { id: SectionId; blocks: Block[] };
export type Doc = {
  lang: Lang;
  title: string;
  fileName: string;
  url: string;
  footer: string;
  /** "Page {x} of {y}" in the language of the document. */
  pageLabel: string;
  sections: Section[];
};

export const SECTION_ORDER: readonly SectionId[] = [
  'title', 'safety', 'parts', 'tested', 'power', 'functions', 'wiring', 'order', 'nir',
];

const KIND: Record<Check['kind'], Key> = { measure: 'kM', unknown: 'kU', note: 'kN' };

/** Safety text as runs: `<b>` and `</b>` switch bold, every other character is text. */
export function boldRuns(text: string): Run[] {
  const out: Run[] = [];
  let bold = false;
  for (const part of text.split(/(<\/?b>)/)) {
    if (part === '<b>') bold = true;
    else if (part === '</b>') bold = false;
    else if (part !== '') out.push(bold ? { text: part, bold: true } : { text: part });
  }
  return out;
}

/** FNV-1a, 32 bit, over the UTF-16 code units of a string, as 8 hexadecimal digits. */
export function shortHash(text: string): string {
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h.toString(16).padStart(8, '0');
}

/** `rcbench-setup-<hash>-<lang>.pdf`. The hash names the selection, not the language. */
export function fileName(view: View): string {
  return `rcbench-setup-${shortHash(formatHash(view.selection, view.lang, view.lang, view.catalogue))}-${view.lang}.pdf`;
}

/** Local calendar date as YYYY-MM-DD. */
export function isoDate(date: Date): string {
  const two = (v: number): string => String(v).padStart(2, '0');
  return `${String(date.getFullYear()).padStart(4, '0')}-${two(date.getMonth() + 1)}-${two(date.getDate())}`;
}

const para = (text: string, muted = false): Block => ({ kind: 'para', runs: [{ text }], muted });
const heading = (text: string, level: 2 | 3 = 2): Block => ({ kind: 'heading', text, level });

function variantText(lang: Lang, key: OptionKey, value: string): string {
  const variant = VARIANTS.find((v) => v.key === key);
  const label = variant?.values.find((v) => v[0] === value);
  return variant && label ? `${t(lang, variant.label)}: ${t(lang, label[1])}` : `${key}: ${value}`;
}

function titleSection(view: View, url: string, now: Date): Section {
  const { lang, catalogue, setup } = view;
  const f = setup.functions.filter((x) => x.enabled).length;
  const variants: Item[] = setup.options.filter((o) => o.visible).map((o) => ({
    runs: [{ text: variantText(lang, o.key, o.value) }],
    tags: [],
    lines: [],
  }));
  const blocks: Block[] = [
    { kind: 'title', text: p(lang, 'docTitle') },
    para(p(lang, 'release', { r: catalogue.release })),
    para(t(lang, 'devState')),
    para(p(lang, 'date', { d: isoDate(now) })),
    { kind: 'link', label: p(lang, 'link'), url },
    para(t(lang, f === 1 ? 'sum1' : 'sum', { p: benchParts(setup).length, w: setup.wires.length, f })),
  ];
  if (variants.length > 0) blocks.push(heading(t(lang, 'variants'), 3), { kind: 'list', items: variants, bullet: true });
  return { id: 'title', blocks };
}

/** One part or test object: name, tags, role, then the line `why`. */
function partItem(view: View, r: View['setup']['parts'][number], why: string): Item {
  const { lang, options } = view;
  const part = r.part;
  const variant = part.variantName ? part.variantName.names[options[part.variantName.option]] : undefined;
  const tags: string[] = [];
  if (part.release !== 'released') tags.push(t(lang, 'nir'));
  const lines: Sub[] = [];
  if (part.role) lines.push({ text: tx(lang, part.role), muted: true });
  if (why !== '') lines.push({ text: why, muted: false });
  return { runs: [{ text: tx(lang, part.name) + (variant ? `, ${tx(lang, variant)}` : ''), bold: true }], tags, lines };
}

/** The bench hardware of the setup. A test object is no part to list here. */
function partsSection(view: View): Section {
  const { lang, setup } = view;
  const items: Item[] = benchParts(setup).map((r) => partItem(view, r, reasonText(r, view)));
  const blocks: Block[] = [heading(t(lang, 'parts'))];
  if (items.length === 0) blocks.push(para(p(lang, 'noParts')));
  else blocks.push(para(p(lang, 'quantity'), true), { kind: 'list', items, bullet: false });
  return { id: 'parts', blocks };
}

/** The test objects of the setup, each with the functions that use it. */
function testedSection(view: View): Section {
  const { lang, setup } = view;
  const items: Item[] = testObjectsOf(setup).map((r) => partItem(view, r, usedByText(view, r)));
  return {
    id: 'tested',
    blocks: [
      heading(t(lang, 'tested')),
      para(t(lang, 'testedHint'), true),
      items.length === 0 ? para(t(lang, 'testedNone')) : { kind: 'list', items, bullet: false },
    ],
  };
}

/** The power sources of the logic and of each connected test object, as the catalogue states them. */
function powerSection(view: View): Section {
  const { lang, setup } = view;
  const blocks: Block[] = [heading(t(lang, 'power')), para(t(lang, 'powerHint'), true)];
  for (const group of powerGroups(setup)) {
    const items: Item[] = group.sources.map(({ source, used }) => {
      const numbers = powerNumbers(lang, source);
      const lines: Sub[] = [];
      if (numbers !== '') lines.push({ text: numbers, muted: true });
      lines.push({ text: tx(lang, source.text), muted: false });
      for (const line of powerLines(lang, source)) lines.push({ text: `${line.kind}: ${line.text}`, muted: false });
      return { runs: [{ text: tx(lang, source.label), bold: true }], tags: used ? [t(lang, source.choice === undefined ? 'gIn' : 'gInOne')] : [], lines };
    });
    blocks.push(heading(powerTitle(view, group.feeds), 3), { kind: 'list', items, bullet: false });
  }
  return { id: 'power', blocks };
}

function functionsSection(view: View): Section {
  const { lang, catalogue, setup, options } = view;
  // As on the page: the functions every setup runs under the heading of the
  // catalogue, then the functions the parts and test objects of the setup enable.
  const entry = (f: (typeof setup.functions)[number]): Item =>
    ({ runs: [{ text: tx(lang, f.fn.name) }], tags: [], lines: [] });
  const on = setup.functions.filter((f) => f.enabled);
  const base: Item[] = on.filter((f) => runsOnBase(catalogue, f)).map(entry);
  const withParts: Item[] = on.filter((f) => !runsOnBase(catalogue, f)).map(entry);
  const enabled: Block[] = [];
  if (base.length > 0) enabled.push(heading(tx(lang, catalogue.baseFunction), 3), { kind: 'list', items: base, bullet: true });
  if (withParts.length > 0) enabled.push(heading(t(lang, 'withParts'), 3), { kind: 'list', items: withParts, bullet: true });
  if (enabled.length === 0) enabled.push(para(p(lang, 'noneEnabled')));
  const off: Item[] = setup.functions.filter((f) => !f.enabled).map((f) => {
    const lines: Sub[] = [];
    if (f.missing.length > 0) {
      const names = f.missing.map((id) => {
        const part = catalogue.parts.find((x) => x.id === id);
        return part ? tx(lang, part.short) : id;
      });
      lines.push({ text: p(lang, 'missing') + names.join(', '), muted: false });
    }
    for (const [key, value] of Object.entries(f.fn.forces ?? {}) as Array<[OptionKey, string]>) {
      if (options[key] !== value) lines.push({ text: p(lang, 'needsVariant') + variantText(lang, key, value), muted: false });
    }
    const object = f.fn.drives === undefined ? undefined : catalogue.parts.find((x) => x.id === f.fn.drives);
    if (object && !setup.parts.some((r) => r.part.id === object.id)) {
      lines.push({ text: p(lang, 'notConnected') + tx(lang, object.short), muted: false });
    }
    return { runs: [{ text: tx(lang, f.fn.name) }], tags: [], lines };
  });
  return {
    id: 'functions',
    blocks: [
      heading(t(lang, 'does')),
      ...enabled,
      // A catalogue without a function has nothing to list on either side.
      ...(off.length > 0
        ? [heading(p(lang, 'notEnabled'), 3), { kind: 'list', items: off, bullet: true } as Block]
        : on.length > 0 ? [heading(p(lang, 'notEnabled'), 3), para(p(lang, 'allEnabled'))] : []),
    ],
  };
}

function wiringSection(view: View, diagram: Diagram): Section {
  const { lang, catalogue, setup, options } = view;
  const ctx: NameCtx = { lang, catalogue, options };
  const rows: Row[] = [...setup.wires].sort((a, b) => a.n - b.n).map((w) => ({
    n: w.n,
    from: endName(w.a, ctx),
    to: targetName(w, ctx),
    toBold: w.b === null,
    signal: tx(lang, w.signal),
    net: w.netB ? `${w.net} / ${w.netB}` : w.net,
    cls: w.cls,
    line: lineWords(lang, w.cls),
    note: w.note ? tx(lang, w.note) : '',
  }));
  const blocks: Block[] = [
    heading(t(lang, 'wiring')),
    { kind: 'legend', label: t(lang, 'legend'), entries: LEGEND.map(([cls, key]) => ({ cls, label: t(lang, key), line: lineWords(lang, cls) })) },
    { kind: 'drawing', svg: diagram(setup, catalogue, lang) },
    para(t(lang, 'padCap'), true),
  ];
  if (rows.length === 0) blocks.push(para(t(lang, 'noWires')));
  else {
    blocks.push({
      kind: 'table',
      caption: t(lang, 'tabCap', { w: rows.length }),
      head: [t(lang, 'thN'), t(lang, 'thFrom'), t(lang, 'thTo'), t(lang, 'thSig'), p(lang, 'thNet'), p(lang, 'thLine')],
      noteLabel: t(lang, 'thNote'),
      rows,
    });
  }
  return { id: 'wiring', blocks };
}

/** The stages as the page lists them, then every check of the setup with its kind. */
function orderSection(view: View): Section {
  const { lang, catalogue, setup } = view;
  const last = Math.max(...catalogue.stages.map((s) => s.n));
  const wired = new Set(setup.wires.flatMap((w) => [w.a, w.b ?? '']).map((ref) => ref.split('.')[0]));
  const loose = setup.parts.map((r) => r.part)
    .filter((part) => part.slot.column !== 'none' && part.id !== 'copro' && !wired.has(part.id))
    .map((part) => tx(lang, part.short));
  const stages: Item[] = [];
  for (const stage of catalogue.stages) {
    const numbers = setup.wires.filter((w) => w.stage === stage.n).map((w) => w.n).sort((a, b) => a - b);
    const names = stage.n === last ? loose : [];
    if (numbers.length === 0 && names.length === 0 && stage.n > 1) continue;
    const lines: Sub[] = [];
    if (numbers.length > 0) lines.push({ text: t(lang, 'wiresN') + numbers.join(', '), muted: false });
    if (names.length > 0) lines.push({ text: t(lang, 'stageParts') + names.join(', '), muted: false });
    if (lines.length === 0) lines.push({ text: t(lang, 'noWires'), muted: false });
    stages.push({ runs: [{ text: tx(lang, stage.title), bold: true }], tags: [], lines });
  }
  const checks: Item[] = setup.checks.map((c) => ({
    runs: [{ text: `${t(lang, KIND[c.kind])}: `, bold: true }, { text: tx(lang, c.text) }],
    tags: [],
    lines: [],
  }));
  return {
    id: 'order',
    blocks: [
      heading(t(lang, 'order')),
      { kind: 'list', items: stages, bullet: false },
      heading(t(lang, 'checks'), 3),
      checks.length > 0 ? { kind: 'list', items: checks, bullet: true } : para(p(lang, 'noChecks')),
    ],
  };
}

export function documentModel(view: View, url: string, now: Date, diagram: Diagram = wiringDiagram): Doc {
  const { lang, catalogue } = view;
  const sections: Section[] = [
    titleSection(view, url, now),
    { id: 'safety', blocks: [{ kind: 'safety', title: t(lang, 'safety'), notes: catalogue.safety.map((s) => boldRuns(tx(lang, s.text))) }] },
    partsSection(view),
    testedSection(view),
    powerSection(view),
    functionsSection(view),
    wiringSection(view, diagram),
    orderSection(view),
  ];
  if (catalogue.notInRelease.length > 0) {
    sections.push({
      id: 'nir',
      blocks: [
        heading(t(lang, 'nir')),
        { kind: 'list', items: catalogue.notInRelease.map((n) => ({ runs: [{ text: tx(lang, n.text) }], tags: [], lines: [] })), bullet: true },
      ],
    });
  }
  return {
    lang,
    title: `${p(lang, 'docTitle')}, ${fileName(view).slice('rcbench-setup-'.length, -'.pdf'.length)}`,
    fileName: fileName(view),
    url,
    footer: t(lang, 'foot'),
    pageLabel: p(lang, 'page'),
    sections,
  };
}

/** Every string of a document, for glyph and wording checks. */
export function strings(doc: Doc): string[] {
  const out: string[] = [doc.title, doc.footer, doc.pageLabel, doc.url];
  const item = (i: Item): void => {
    out.push(...i.runs.map((r) => r.text), ...i.tags, ...i.lines.map((l) => l.text));
  };
  for (const block of doc.sections.flatMap((s) => s.blocks)) {
    if (block.kind === 'title' || block.kind === 'heading') out.push(block.text);
    else if (block.kind === 'para') out.push(...block.runs.map((r) => r.text));
    else if (block.kind === 'link') out.push(block.label, block.url);
    else if (block.kind === 'safety') out.push(block.title, ...block.notes.flat().map((r) => r.text));
    else if (block.kind === 'list') block.items.forEach(item);
    else if (block.kind === 'legend') out.push(block.label, ...block.entries.flatMap((e) => [e.label, e.line]));
    else if (block.kind === 'table') {
      out.push(block.caption, block.noteLabel, ...block.head);
      for (const r of block.rows) out.push(String(r.n), r.from, r.to, r.signal, r.net, r.line, r.note);
    }
  }
  return out;
}
