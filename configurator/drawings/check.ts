// Checks of the part drawings against the catalogue. Every function is pure and
// returns findings; an empty list is a pass. The command line is `cli.ts`.

import type { Catalogue, Lang } from '../contract.ts';
import { DEFAULT_OPTIONS } from '../core/resolve.ts';
import { LANGS, anchorsText, cataloguePins, fileOf, generate, pathOf } from './generate.ts';
import type { Anchors, DrawingRecord } from './generate.ts';
import { BACKGROUNDS, MINUS_NETS, MIN_RATIO, PALETTE, PLUS_NETS, colourOf, endPoints, neighbours, wireRatio, withoutWirePoint } from './wires.ts';
import type { Colour } from './wires.ts';

export type FindingId =
  | 'svg' | 'anchor' | 'pin-without-anchor' | 'anchor-without-pin' | 'label' | 'map' | 'no-drawing'
  | 'release' | 'wire-colour' | 'wire-end' | 'contrast' | 'neighbour' | 'drift';

export type Finding = { id: FindingId; msg: string };

/** The committed files a check reads: the text of `anchors.json` and every SVG by its path. */
export type Files = { anchors: string; svgs: Record<string, string> };

const SIDES = ['N', 'E', 'S', 'W'];
const VOID = /\/\s*$/;

/** An `&` that starts no entity or character reference. */
const BARE_AMP = /&(?!(?:lt|gt|amp|quot|apos|#\d+|#x[0-9a-fA-F]+);)/;

/** A character outside the Char production of XML 1.0, or half of a surrogate pair. */
const ILLEGAL = /[\u0000-\u0008\u000B\u000C\u000E-\u001F\uFFFE\uFFFF]|[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/;
/** True for a code point of the Char production of XML 1.0. */
const xmlChar = (cp: number): boolean =>
  cp === 0x9 || cp === 0xA || cp === 0xD || (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
const SVG_NS = 'http://www.w3.org/2000/svg';
/** Elements that run code, load a file or carry a style sheet, by local name. */
const ACTIVE = new Set(['image', 'foreignobject', 'iframe', 'object', 'embed', 'audio', 'video', 'link', 'style']);

const unescape = (s: string): string =>
  s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&');

/**
 * Well-formedness of the subset of XML the generator writes: one root element,
 * every element closed in order, every attribute quoted, no stray `<` or `&`
 * in text or in an attribute value, no character XML 1.0 forbids, written
 * out or as a numeric reference.
 * Returns the first defect or null.
 */
export function malformed(svg: string): string | null {
  const illegal = ILLEGAL.exec(svg);
  if (illegal) return `character U+${illegal[0].charCodeAt(0).toString(16).toUpperCase().padStart(4, '0')} at offset ${illegal.index} is not a character of XML 1.0`;
  for (const m of svg.matchAll(/&#(x[0-9a-fA-F]+|\d+);/g)) {
    const cp = m[1]!.startsWith('x') ? Number.parseInt(m[1]!.slice(1), 16) : Number.parseInt(m[1]!, 10);
    if (!xmlChar(cp)) return `character reference ${m[0]} at offset ${m.index} names no character of XML 1.0`;
  }
  const stack: string[] = [];
  let roots = 0;
  let at = 0;
  const tag = /<(\/?)([A-Za-z][\w:.-]*)((?:\s+[\w:.-]+="[^"<]*")*)\s*(\/?)>/y;
  while (at < svg.length) {
    const lt = svg.indexOf('<', at);
    const text = svg.slice(at, lt < 0 ? svg.length : lt);
    if (BARE_AMP.test(text)) return `bare & at offset ${at}`;
    if (text.includes('>')) return `bare > at offset ${at}`;
    if (stack.length === 0 && text.trim() !== '') return `text outside the root element at offset ${at}`;
    if (lt < 0) break;
    tag.lastIndex = lt;
    const m = tag.exec(svg);
    if (!m) return `not a tag at offset ${lt}`;
    const [, close, name, attrs, selfClose] = m;
    if (BARE_AMP.test(attrs ?? '')) return `bare & in an attribute of <${name}> at offset ${lt}`;
    if (close) {
      if (attrs || selfClose) return `closing tag ${name} carries attributes at offset ${lt}`;
      const open = stack.pop();
      if (open !== name) return `</${name}> closes <${open ?? 'nothing'}> at offset ${lt}`;
    } else {
      const names = [...(attrs ?? '').matchAll(/\s+([\w:.-]+)=/g)].map((a) => a[1]);
      if (new Set(names).size !== names.length) return `attribute given twice in <${name}> at offset ${lt}`;
      if (stack.length === 0) roots += 1;
      if (roots > 1) return `second root element <${name}> at offset ${lt}`;
      if (!VOID.test(`${selfClose}`)) stack.push(name!);
    }
    at = tag.lastIndex;
  }
  if (stack.length > 0) return `<${stack[stack.length - 1]}> is not closed`;
  if (roots === 0) return 'no root element';
  return null;
}

/**
 * One SVG file: self-contained, well formed, an `svg` root in the SVG namespace,
 * ids prefixed, title, description, viewBox, font, and the language when given.
 */
export function checkSvg(where: string, svg: string, rec: DrawingRecord, lang?: Lang): Finding[] {
  const out: Finding[] = [];
  const fail = (msg: string): void => { out.push({ id: 'svg', msg: `${where}: ${msg}` }); };
  const bad = malformed(svg);
  if (bad !== null) fail(`not well formed: ${bad}`);
  // An element counts by its local name: a namespace prefix does not hide it.
  const locals = new Set([...svg.matchAll(/<(?:[A-Za-z][\w.-]*:)?([A-Za-z][\w.-]*)(?=[\s/>])/g)].map((m) => m[1]!.toLowerCase()));
  if (locals.has('script')) fail('contains a script element');
  if (/\son[a-z]+\s*=/i.test(svg)) fail('contains an event handler attribute');
  if ([...locals].some((name) => ACTIVE.has(name))) fail('contains an image, foreign content or style sheet element');
  if (/data:|javascript:|@import|base64/i.test(svg)) fail('contains a data, script or import reference');
  for (const m of svg.matchAll(/\b(?:xlink:href|href|src)\s*=\s*"([^"]*)"/g)) {
    if (!m[1]!.startsWith('#')) fail(`reference "${m[1]}" leaves the file`);
  }
  for (const m of svg.matchAll(/url\(\s*['"]?([^)'"]*)/gi)) {
    if (!m[1]!.startsWith('#')) fail(`url(${m[1]}) leaves the file`);
  }
  for (const m of svg.matchAll(/https?:\/\/[^\s"<]+/g)) {
    if (m[0] !== 'http://www.w3.org/2000/svg') fail(`external address ${m[0]}`);
  }
  // A base address moves every "#id" reference to another file.
  if (/\s(?:[\w.-]+:)?base\s*=/i.test(svg)) fail('contains a base address attribute');
  const first = /<[A-Za-z][^>]*>/.exec(svg)?.[0] ?? '';
  const rootName = /^<([^\s/>]+)/.exec(first)?.[1] ?? '';
  /** Attributes of the root element. */
  const root = new Map([...first.matchAll(/\s([\w:.-]+)="([^"]*)"/g)].map((m) => [m[1]!, m[2]!]));
  if (rootName !== 'svg' || root.get('xmlns') !== SVG_NS) fail(`the root element is not svg in the namespace ${SVG_NS}`);
  if (lang !== undefined && (root.get('lang') !== lang || root.get('xml:lang') !== lang)) fail(`the root element does not state the language ${lang} in lang and xml:lang`);
  const ids = [...svg.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]!);
  const set = new Set(ids);
  if (set.size !== ids.length) fail('an id is given twice');
  const pfx = ids[0]?.split('-')[0] ?? '';
  for (const id of set) if (!id.startsWith(`${pfx}-`)) fail(`id ${id} lacks the prefix ${pfx}-`);
  for (const m of svg.matchAll(/(?:url\(#|href="#)([^)"]+)/gi)) if (!set.has(m[1]!)) fail(`reference to missing id ${m[1]}`);
  const title = svg.match(/<title id="([^"]+)">([^<]*)<\/title>/);
  if (!title || title[2]!.trim().length < 3) fail('no title element');
  else if (root.get('aria-labelledby') !== title[1]) fail('the root element does not name the title');
  const desc = svg.match(/<desc id="([^"]+)">([^<]*)<\/desc>/);
  if (!desc || desc[2]!.trim().length < 10) fail('no desc element');
  else if (root.get('aria-describedby') !== desc[1]) fail('the root element does not name the description');
  const vb = /^0 0 ([\d.]+) ([\d.]+)$/.exec(root.get('viewBox') ?? '');
  if (!vb || Number(vb[1]) !== rec.viewBox[2] || Number(vb[2]) !== rec.viewBox[3]) fail('viewBox differs from anchors.json');
  // The label fit is measured for this stack: the root states it, and no element states another.
  if (!(root.get('font-family') ?? '').startsWith('system-ui')) fail('the root element does not state a font stack that starts with system-ui');
  if (/font-family="(?!system-ui)/.test(svg)) fail('font stack does not start with system-ui');
  return out;
}

/** Anchors of one drawing against its 2 files: inside the viewBox, a side, exactly 1 label text per language. */
export function checkRecord(file: string, rec: DrawingRecord, svgs: Partial<Record<Lang, string>>): Finding[] {
  const out: Finding[] = [];
  const fail = (msg: string): void => { out.push({ id: 'anchor', msg }); };
  const [, , w, h] = rec.viewBox;
  const labelIds = new Set<string>();
  for (const [id, a] of Object.entries(rec.pins)) {
    // A coordinate that is not a finite number reads as null from the JSON file and compares like 0.
    if (![a.x, a.y].every((v) => typeof v === 'number' && Number.isFinite(v))) fail(`${file}: anchor of ${id} (${a.x}, ${a.y}) is not a pair of finite numbers`);
    else if (!(a.x >= 0 && a.x <= w && a.y >= 0 && a.y <= h)) fail(`${file}: anchor of ${id} (${a.x}, ${a.y}) lies outside the viewBox ${w} x ${h}`);
    if (!SIDES.includes(a.side)) fail(`${file}: anchor of ${id} has no side N, E, S or W`);
    if (labelIds.has(a.labelId)) fail(`${file}: label id ${a.labelId} serves 2 pins`);
    labelIds.add(a.labelId);
    for (const lang of LANGS) {
      const svg = svgs[lang];
      if (svg === undefined) continue;
      const where = pathOf(file, lang);
      const found = [...svg.matchAll(/<text id="([^"]+)"[^>]*>([^<]*)<\/text>/g)].filter((m) => m[1] === a.labelId);
      if (found.length !== 1) { fail(`${where}: pin ${id} has ${found.length} label texts, 1 expected`); continue; }
      const text = unescape(found[0]![2]!);
      if (text.trim() === '') fail(`${where}: label of pin ${id} is empty`);
      if (text !== a.label[lang]) fail(`${where}: label of pin ${id} is "${text}", anchors.json has "${a.label[lang]}"`);
    }
  }
  for (const lang of LANGS) {
    const svg = svgs[lang];
    if (svg === undefined) continue;
    for (const m of svg.matchAll(/<text id="([^"]+-lbl-[^"]+)"/g)) {
      if (!labelIds.has(m[1]!)) fail(`${pathOf(file, lang)}: label ${m[1]} has no anchor`);
    }
  }
  return out;
}

/**
 * Pins and labels of the catalogue against the anchor table.
 *
 * - `pin-without-anchor`: a catalogue pin has no anchor in a drawing of its part.
 * - `anchor-without-pin`: an anchor names no catalogue pin. A drawing whose part is
 *   null is not in the catalogue and is skipped.
 * - `label`: a label differs from the catalogue in English or German.
 * - `map`: the map of a drawing that carries the print of its board is incomplete.
 * - `no-drawing`: a part, or one of the boards of a part, has no drawing. A part whose
 *   default board has no name of its own needs the drawing without a variant. A drawing
 *   of a board the part does not name is a finding as well: no setup selects it.
 * - `release`: the table is of another catalogue release.
 */
export function checkPins(catalogue: Catalogue, anchors: Anchors): Finding[] {
  const out: Finding[] = [];
  const fail = (id: FindingId, msg: string): void => { out.push({ id, msg }); };
  const drawn = new Set<string>();
  for (const [file, rec] of Object.entries(anchors.files)) {
    if (rec.part === null) continue;
    const part = catalogue.parts.find((p) => p.id === rec.part);
    if (!part) { fail('anchor-without-pin', `${file}: part ${rec.part} is not in the catalogue`); continue; }
    if (file !== fileOf(rec.part, rec.variant)) fail('no-drawing', `${file}: file name does not match part ${rec.part} and variant ${rec.variant}`);
    if (rec.variant !== null && !Object.hasOwn(part.variantName?.names ?? {}, rec.variant)) {
      fail('no-drawing', `${file}: part ${rec.part} names no board ${rec.variant}`);
      continue;
    }
    drawn.add(rec.part);
    const expected = cataloguePins(catalogue, part, rec.variant);
    if (rec.catalogueMap) {
      for (const id of Object.keys(expected)) {
        if (!(id in rec.catalogueMap)) { fail('pin-without-anchor', `${file}: catalogue pin ${id} is not in catalogueMap`); continue; }
        for (const n of rec.catalogueMap[id] ?? []) {
          if (!rec.pins[n]) fail('pin-without-anchor', `${file}: catalogue pin ${id} maps to ${n}, which has no anchor`);
        }
      }
      for (const id of Object.keys(rec.catalogueMap)) {
        if (!(id in expected)) fail('map', `${file}: catalogueMap names ${id}, which is not a catalogue pin`);
      }
      continue;
    }
    for (const [id, label] of Object.entries(expected)) {
      const a = rec.pins[id];
      if (!a) { fail('pin-without-anchor', `${file}: catalogue pin ${id} has no anchor`); continue; }
      for (const lang of LANGS) {
        if (a.label[lang] !== label[lang]) fail('label', `${pathOf(file, lang)}: pin ${id} is labelled "${a.label[lang]}", the catalogue has "${label[lang]}"`);
      }
    }
    for (const id of Object.keys(rec.pins)) {
      if (!(id in expected)) fail('anchor-without-pin', `${file}: anchor ${id} is not a catalogue pin`);
    }
  }
  for (const part of catalogue.parts) {
    if (!drawn.has(part.id)) fail('no-drawing', `part ${part.id}: no drawing`);
    if (!part.variantName) continue;
    const { option, names } = part.variantName;
    for (const v of Object.keys(names)) {
      if (!anchors.files[fileOf(part.id, v)]) fail('no-drawing', `part ${part.id}: no drawing for ${v}`);
    }
    const preset = DEFAULT_OPTIONS[option];
    if (!Object.hasOwn(names, preset) && drawn.has(part.id) && !anchors.files[fileOf(part.id, null)]) {
      fail('no-drawing', `part ${part.id}: no drawing for the default ${preset}`);
    }
  }
  if (anchors.release !== catalogue.release) fail('release', `anchors.json is of catalogue release ${anchors.release}, the catalogue is ${catalogue.release}`);
  return out;
}

/** Every colour of a palette against every background: at least 3:1, by core or casing. */
export function checkContrast(
  palette: Readonly<Record<string, Colour>> = PALETTE,
  backgrounds: Readonly<Record<string, string>> = BACKGROUNDS,
): Finding[] {
  const out: Finding[] = [];
  for (const [name, colour] of Object.entries(palette)) {
    for (const [bgName, bg] of Object.entries(backgrounds)) {
      const r = wireRatio(colour, bg);
      if (r < MIN_RATIO) out.push({ id: 'contrast', msg: `colour ${name}: ${r.toFixed(2)}:1 on ${bgName} ${bg}, below ${MIN_RATIO}:1` });
    }
  }
  return out;
}

/**
 * Wires of the catalogue against the anchor table.
 *
 * - `wire-colour`: a wire has no colour of the palette.
 * - `wire-end`: an end of a wire has no anchor. An end whose pin the drawing maps
 *   to null has no wire point on the board and is skipped.
 * - `neighbour`: 2 wires that end on neighbouring pins of one drawing share a colour.
 *   2 supply plus wires and 2 minus wires are exempt: their colour is fixed.
 */
export function checkWires(catalogue: Catalogue, anchors: Anchors): Finding[] {
  const out: Finding[] = [];
  const ends: Array<{ n: number; colour: string | null; rail: 'plus' | 'minus' | null; file: string; pin: string; end: string }> = [];
  for (const wire of catalogue.wires) {
    let colour: string | null = null;
    try {
      colour = colourOf(wire);
    } catch (e) {
      out.push({ id: 'wire-colour', msg: (e as Error).message });
    }
    if (colour !== null && !PALETTE[colour]) out.push({ id: 'wire-colour', msg: `wire ${wire.n}: colour ${colour} is not in the palette` });
    const rail = PLUS_NETS.has(wire.net) ? 'plus' : MINUS_NETS.has(wire.net) ? 'minus' : null;
    for (const end of [wire.a, wire.b]) {
      if (end === null) continue;
      const points = endPoints(end, anchors);
      if (points.length === 0 && !withoutWirePoint(end, anchors)) out.push({ id: 'wire-end', msg: `wire ${wire.n}: end ${end} has no anchor` });
      for (const p of points) ends.push({ n: wire.n, colour, rail, file: p.file, pin: p.pin, end });
    }
  }
  for (const [file, rec] of Object.entries(anchors.files)) {
    const near = neighbours(rec);
    const here = ends.filter((e) => e.file === file && e.colour !== null);
    for (const a of here) {
      for (const b of here) {
        if (a.n >= b.n || a.pin === b.pin || a.end === b.end || !near[a.pin]!.includes(b.pin)) continue;
        if (a.rail !== null && a.rail === b.rail) continue;
        if (a.colour === b.colour) out.push({ id: 'neighbour', msg: `${file}: wires ${a.n} and ${b.n} end on the neighbouring pins ${a.pin} and ${b.pin} and are both ${a.colour}` });
      }
    }
  }
  return out;
}

/** Committed files against the output of the generator: a missing file, a file too many, other bytes. */
export function checkDrift(expected: Files, actual: Files): Finding[] {
  const out: Finding[] = [];
  const fail = (msg: string): void => { out.push({ id: 'drift', msg: `${msg}: run "npm run drawings"` }); };
  if (actual.anchors !== expected.anchors) fail('anchors.json differs from the output of the generator');
  for (const [path, svg] of Object.entries(expected.svgs)) {
    if (!(path in actual.svgs)) fail(`${path} is missing`);
    else if (actual.svgs[path] !== svg) fail(`${path} differs from the output of the generator`);
  }
  for (const path of Object.keys(actual.svgs)) {
    if (!(path in expected.svgs)) fail(`${path} is not a drawing of the generator`);
  }
  return out;
}

/** The files the generator writes for a catalogue. */
export function filesOf(catalogue: Catalogue): Files {
  const { anchors, svgs } = generate(catalogue);
  return { anchors: anchorsText(anchors), svgs };
}

/** Every check over a set of committed files. `expected` is the output of the generator for the catalogue. */
export function checkAll(catalogue: Catalogue, files: Files, expected: Files = filesOf(catalogue)): Finding[] {
  const out: Finding[] = [];
  let anchors: Anchors;
  try {
    anchors = JSON.parse(files.anchors) as Anchors;
  } catch (e) {
    return [{ id: 'anchor', msg: `anchors.json is not JSON: ${(e as Error).message}` }];
  }
  for (const [file, rec] of Object.entries(anchors.files)) {
    const svgs: Partial<Record<Lang, string>> = {};
    for (const lang of LANGS) {
      const path = pathOf(file, lang);
      const svg = files.svgs[path];
      if (svg === undefined) { out.push({ id: 'svg', msg: `${path}: file is missing` }); continue; }
      svgs[lang] = svg;
      out.push(...checkSvg(path, svg, rec, lang));
    }
    out.push(...checkRecord(file, rec, svgs));
  }
  out.push(...checkPins(catalogue, anchors), ...checkContrast(), ...checkWires(catalogue, anchors), ...checkDrift(expected, files));
  return out;
}
