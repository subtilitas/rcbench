// Validates the catalogue: everything the compiler cannot check.
//
//   node tools/check.ts            static checks and source anchors; exit 1 on any error
//   node tools/check.ts --strict   a missing repository or release tag is an error
//   node tools/check.ts --wires    prints the wire table as Markdown and nothing else
//
// The tool writes no file. Every check is an exported function that returns
// findings, so the tests run the same code on changed copies of the data.

import { execFileSync } from 'node:child_process';
import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

import type {
  Catalogue, Cond, OptionKey, Options, Pad, PinDefault, Src, Wire,
} from '../../contract.ts';
import { TAG } from '../../release.ts';
import { ANCHOR_TEXTS, CATALOGUE } from '../catalogue.ts';
import type { AnchorText } from '../catalogue.ts';
import { ROW_ID, brought, buyRows } from '../buy.ts';
import { optionsIn, partsIn, requiresOption } from '../cond.ts';
import { DEFAULT_OPTIONS, OPTION_KEYS, OPTION_VALUES, resolve } from '../resolve.ts';
import { PICTURE_DIR, checkPictures, pictureFolder } from './pictures.ts';
import type { PictureFolder } from './pictures.ts';
import { checkReleaseSources } from './release-sources.ts';

export type Finding = { level: 'error' | 'warn'; id: string; msg: string };

/** The repository at the release tag. `read` returns null for a path the tag does not hold. */
export type Tree = {
  read: (path: string) => string | null;
  /** The newest release tag of the repository, or null when it has none. */
  newestTag: string | null;
};

export const RELEASE_TAG = TAG;

/** Section ids of the build guide. The generator is not in the repository; only the id is checked. */
export const GUIDE_SECTIONS: ReadonlyArray<string> = [
  'parts', 'also-needed', 'wires', 'notes', 'safety',
  'stage-1', 'stage-2', 'stage-3', 'stage-4', 'stage-5', 'tap',
];

/** The rule for the address of a `doc` source: https, a host, a path without white space. */
export const DOC_URL = /^https:\/\/[^\s/]+\/\S*$/;

/** Dates of the owner statements a source may name. */
export const OWNER_STATEMENTS: ReadonlyArray<string> = ['2026-10-11'];

export const BANNED_WORDS: ReadonlyArray<string> = [
  'powerful', 'seamless', 'simply', 'just', 'easy', 'robust', 'mächtig', 'nahtlos', 'einfach',
];

/** Compare-register partners of the PWM slices, from docs/DShot.md lines 110 to 112. */
export const COMPARE_PARTNERS: ReadonlyArray<readonly [number, number]> = [
  [0, 16], [1, 17], [2, 18], [4, 20], [5, 21], [6, 22],
];

export const LANE_MAX = { L: 18, R: 22, O: 7, channel: 6 } as const;

const PART_ID = /^[a-z0-9]+$/;
const PIN_ID = /^[A-Z0-9]+$/;
const ITEM_ID = /^[a-z0-9]+(-[a-z0-9]+)*$/;

function err(id: string, msg: string): Finding {
  return { level: 'error', id, msg };
}

function warn(id: string, msg: string): Finding {
  return { level: 'warn', id, msg };
}

function duplicates(ids: ReadonlyArray<string | number>): Array<string | number> {
  const seen = new Set<string | number>();
  const twice = new Set<string | number>();
  for (const id of ids) {
    if (seen.has(id)) twice.add(id);
    seen.add(id);
  }
  return [...twice];
}

function isOptionValue(key: string, value: unknown): boolean {
  if (!(OPTION_KEYS as ReadonlyArray<string>).includes(key)) return false;
  return (OPTION_VALUES[key as OptionKey] as ReadonlyArray<unknown>).includes(value);
}

/** The pad number of an end reference `pad.N`, or null. */
function padOf(end: string | null): number | null {
  if (end === null) return null;
  const m = /^pad\.(\d+)$/.exec(end);
  return m ? Number(m[1]) : null;
}

function endsOf(wire: Wire): string[] {
  return wire.b === null ? [wire.a] : [wire.a, wire.b];
}

// ---------------------------------------------------------------------------
// Invariant 1: ids
// ---------------------------------------------------------------------------

export function checkIds(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  const kinds: Array<[string, string[], RegExp]> = [
    ['part', c.parts.map((p) => p.id), PART_ID],
    ['function', c.functions.map((f) => f.id), PART_ID],
    ['rule', c.rules.map((r) => r.id), ITEM_ID],
    ['check', c.checks.map((k) => k.id), ITEM_ID],
    ['notInRelease', c.notInRelease.map((n) => n.id), ITEM_ID],
  ];
  for (const [kind, ids, pattern] of kinds) {
    for (const id of duplicates(ids)) out.push(err(`${kind}.${id}`, `id is used twice`));
    for (const id of ids) {
      if (!pattern.test(id)) out.push(err(`${kind}.${id}`, `id does not match ${pattern}`));
    }
  }
  for (const part of c.parts) {
    const ids = part.pins.map((p) => p.id);
    for (const id of duplicates(ids)) out.push(err(`part.${part.id}.${id}`, 'pin id is used twice'));
    for (const id of ids) {
      if (!PIN_ID.test(id)) out.push(err(`part.${part.id}.${id}`, `pin id does not match ${PIN_ID}`));
    }
  }
  let last = 0;
  for (const wire of c.wires) {
    if (!Number.isInteger(wire.n) || wire.n <= last) {
      out.push(err(`wire.${wire.n}`, 'wire numbers are not unique and ascending'));
    }
    last = wire.n;
  }
  for (const signal of duplicates(c.pinDefaults.map((d) => d.signal))) {
    out.push(err(`pin.${signal}`, 'signal is listed twice'));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Invariants 2, 4 and 9: wire ends
// ---------------------------------------------------------------------------

export function checkEnds(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  const parts = new Map(c.parts.map((p) => [p.id, p]));
  const pads = new Map(c.pads.map((p) => [p.n, p]));

  for (const wire of c.wires) {
    const id = `wire.${wire.n}`;
    if (wire.kind === 'stub') {
      if (wire.b !== null) out.push(err(id, 'a stub has no second end'));
      if (!wire.to) out.push(err(id, 'a stub names its target in `to`'));
    } else {
      if (wire.b === null) out.push(err(id, `a ${wire.kind} has two ends`));
      if (wire.to) out.push(err(id, 'only a stub has `to`'));
    }
    if (wire.kind === 'component' && !wire.netB) out.push(err(id, 'a component names `netB`'));
    if (wire.kind !== 'component' && wire.netB) out.push(err(id, 'only a component has `netB`'));

    for (const end of endsOf(wire)) {
      if (end === 'can.H' || end === 'can.L') continue;
      const n = padOf(end);
      if (n !== null) {
        const pad = pads.get(n);
        if (!pad) out.push(err(id, `end ${end} is not a pad 1 to 40`));
        else if (pad.reserved) out.push(err(id, `end ${end} is a reserved pad (${pad.name})`));
        continue;
      }
      const [partId, pinId, ...rest] = end.split('.');
      const part = parts.get(partId ?? '');
      const pin = part?.pins.find((p) => p.id === pinId);
      if (!part || !pin || rest.length > 0) {
        out.push(err(id, `end ${end} is not a declared pin`));
        continue;
      }
      // A pin that exists only under an option value: the wire's condition
      // has to require that value.
      for (const key of OPTION_KEYS) {
        const value = pin.only?.[key];
        if (value !== undefined && !requiresOption(wire.when, key, value)) {
          out.push(err(id, `end ${end} exists only with ${key}=${value}; the condition does not require it`));
        }
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Invariant 3: references
// ---------------------------------------------------------------------------

export function checkRefs(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  const parts = new Map(c.parts.map((p) => [p.id, p]));

  const cond = (id: string, when: Cond | null): void => {
    for (const partId of partsIn(when)) {
      if (!parts.has(partId)) out.push(err(id, `condition names the unknown part ${partId}`));
    }
    for (const { opt, is } of optionsIn(when)) {
      if (!isOptionValue(opt, is)) out.push(err(id, `condition names ${opt}=${is}, which is not a value of that option`));
    }
  };
  const optionMap = (id: string, map: Partial<Record<string, unknown>> | undefined): void => {
    for (const [key, value] of Object.entries(map ?? {})) {
      if (!isOptionValue(key, value)) out.push(err(id, `${key}=${String(value)} is not a value of an option`));
    }
  };

  for (const wire of c.wires) cond(`wire.${wire.n}`, wire.when);
  for (const check of c.checks) cond(`check.${check.id}`, check.when);
  for (const rule of c.rules) {
    cond(`rule.${rule.id}`, rule.when);
    const added = parts.get(rule.adds);
    if (!added) out.push(err(`rule.${rule.id}`, `adds the unknown part ${rule.adds}`));
    else if (added.locked) out.push(err(`rule.${rule.id}`, `adds the locked part ${rule.adds}`));
    else if (added.testObject === true) out.push(err(`rule.${rule.id}`, `adds the test object ${rule.adds}`));
  }

  // Power sources: each feeds the logic or a test object; each test object has one.
  for (const id of duplicates(c.powerSources.map((s) => s.id))) out.push(err(`power.${id}`, 'the id is used twice'));
  for (const source of c.powerSources) {
    const id = `power.${source.id}`;
    if (source.choice !== undefined) {
      const same = c.powerSources.filter((s) => s.choice === source.choice);
      if (same.length < 2) out.push(err(id, `the choice ${source.choice} holds 1 source`));
      if (same.some((s) => s.feeds !== source.feeds)) out.push(err(id, `the choice ${source.choice} feeds more than 1 thing`));
    }
    cond(id, source.when);
    cond(id, source.used);
    if (source.feeds !== 'logic' && parts.get(source.feeds)?.testObject !== true) {
      out.push(err(id, `feeds ${source.feeds}, which is neither the logic nor a test object`));
    }
    if (source.pros.length === 0) out.push(err(id, 'no advantage'));
    if (source.cons.length === 0) out.push(err(id, 'no drawback'));
    if (source.volts && (source.volts.min > source.volts.max || source.volts.min <= 0)) out.push(err(id, 'the voltage range is not ordered'));
    if (source.amps && ((source.amps.min ?? 0) > source.amps.max || source.amps.max <= 0)) out.push(err(id, 'the current range is not ordered'));
    if ((source.volts === null && source.amps === null) !== (source.basis === null) && source.basis !== null) {
      out.push(err(id, 'a basis without a number'));
    }
  }
  for (const part of c.parts) {
    if (part.testObject === true && !c.powerSources.some((s) => s.feeds === part.id)) {
      out.push(err(`part.${part.id}`, 'a test object without a power source'));
    }
  }
  if (c.parts.length > 0 && !c.powerSources.some((s) => s.feeds === 'logic')) out.push(err('power', 'no power source of the logic'));

  const forced = new Map<string, string>();
  for (const fn of c.functions) {
    const id = `function.${fn.id}`;
    // A function that needs no part and drives no test object is in every setup: it has no option to force.
    if (fn.needs.length === 0 && fn.drives === undefined && fn.forces !== undefined) out.push(err(id, 'needs no part, drives no test object and forces an option'));
    for (const partId of fn.needs) {
      if (!parts.has(partId)) out.push(err(id, `needs the unknown part ${partId}`));
      else if (parts.get(partId)?.testObject === true) out.push(err(id, `needs the test object ${partId}; a test object belongs in \`drives\``));
    }
    if (fn.drives !== undefined && parts.get(fn.drives)?.testObject !== true) {
      out.push(err(id, `drives ${fn.drives}, which is no test object`));
    }
    optionMap(id, fn.forces);
    for (const [key, value] of Object.entries(fn.forces ?? {})) {
      const other = forced.get(key);
      if (other !== undefined && other !== value) {
        out.push(err(id, `forces ${key}=${String(value)}; another function forces ${key}=${other}`));
      }
      forced.set(key, String(value));
    }
  }

  for (const part of c.parts) {
    const id = `part.${part.id}`;
    if (part.release !== 'released') out.push(err(id, 'only released parts are in `parts`'));
    if (part.locked && part.selectable) out.push(err(id, 'a locked part is not selectable'));
    if (part.testObject === true && (part.locked || part.selectable)) out.push(err(id, 'a test object is neither locked nor selectable'));
    if (part.testObject === true && !c.functions.some((f) => f.drives === part.id && f.needs.every((p) => parts.get(p)?.locked === true))) {
      out.push(err(id, 'a test object without a function that drives it on the base setup'));
    }
    for (const picture of part.pictures ?? []) optionMap(`${id}.picture.${picture.file}`, picture.only);
    for (const pin of part.pins) optionMap(`${id}.${pin.id}`, pin.only);
    if (part.variantName) {
      for (const value of Object.keys(part.variantName.names)) {
        if (!isOptionValue(part.variantName.option, value)) {
          out.push(err(id, `variant name for ${part.variantName.option}=${value}, which is not a value of that option`));
        }
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Invariant 5, first half: bilingual text
// ---------------------------------------------------------------------------

function isAllowedChar(cp: number): boolean {
  return (
    (cp >= 0x20 && cp <= 0x7e) || // Basic Latin, printable
    (cp >= 0xa0 && cp <= 0xff) || // Latin-1 Supplement: umlauts, µ, ×
    (cp >= 0x2000 && cp <= 0x206f) || // General Punctuation
    cp === 0x3a9 || // Ω
    cp === 0x3bc || // μ
    cp === 0x2192 || // →
    cp === 0x2212 // −
  );
}

const TAG_PATTERN = /<\/?[A-Za-z][^>]*>/g;

/** Findings for one string of one language. `allowBold`: `<b>` and `</b>` are allowed. */
export function checkString(id: string, value: unknown, allowBold: boolean): Finding[] {
  const out: Finding[] = [];
  if (typeof value !== 'string' || value.trim() === '') {
    return [err(id, 'string is missing or empty')];
  }
  if (value !== value.trim()) out.push(err(id, 'string starts or ends with white space'));
  if (value.includes('!')) out.push(err(id, 'string holds an exclamation mark'));
  for (const word of BANNED_WORDS) {
    const pattern = new RegExp(`(?<![\\p{L}\\p{N}])${word}(?![\\p{L}\\p{N}])`, 'iu');
    if (pattern.test(value)) out.push(err(id, `string holds the word "${word}"`));
  }
  for (const ch of value) {
    const cp = ch.codePointAt(0) ?? 0;
    if (!isAllowedChar(cp)) {
      out.push(err(id, `string holds the character U+${cp.toString(16).toUpperCase().padStart(4, '0')}`));
      break;
    }
  }
  const tags = value.match(TAG_PATTERN) ?? [];
  if (allowBold) {
    const other = tags.filter((tag) => tag !== '<b>' && tag !== '</b>');
    if (other.length > 0) out.push(err(id, `string holds the tag ${other[0]}`));
    const open = tags.filter((tag) => tag === '<b>').length;
    const close = tags.filter((tag) => tag === '</b>').length;
    if (open !== close) out.push(err(id, 'string holds an unbalanced <b>'));
  } else if (tags.length > 0) {
    out.push(err(id, `string holds the tag ${tags[0]}`));
  }
  return out;
}

function isTextLike(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
    && ('en' in value || 'de' in value);
}

function labelOf(value: unknown, index: number): string {
  if (typeof value === 'object' && value !== null) {
    const item = value as Record<string, unknown>;
    for (const key of ['id', 'n', 'signal']) {
      if (typeof item[key] === 'string' || typeof item[key] === 'number') return String(item[key]);
    }
  }
  return String(index);
}

/** Every bilingual text of the catalogue with a path that names it. */
export function collectTexts(c: Catalogue): Array<{ id: string; text: Record<string, unknown> }> {
  const out: Array<{ id: string; text: Record<string, unknown> }> = [];
  const walk = (value: unknown, path: string): void => {
    if (isTextLike(value)) {
      out.push({ id: path, text: value });
      return;
    }
    if (Array.isArray(value)) {
      value.forEach((item, index) => walk(item, `${path}.${labelOf(item, index)}`));
      return;
    }
    if (typeof value === 'object' && value !== null) {
      for (const [key, item] of Object.entries(value)) {
        if (key === 'src') continue;
        walk(item, path === '' ? key : `${path}.${key}`);
      }
    }
  };
  walk(c, '');
  return out;
}

export function checkTexts(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  for (const { id, text } of collectTexts(c)) {
    const allowBold = id.startsWith('safety.');
    out.push(...checkString(`${id}.en`, text['en'], allowBold));
    out.push(...checkString(`${id}.de`, text['de'], allowBold));
    const extra = Object.keys(text).filter((key) => key !== 'en' && key !== 'de');
    if (extra.length > 0) out.push(err(id, `text holds the key ${extra[0]}`));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Invariant 5, second half: source anchors
// ---------------------------------------------------------------------------

type Anchor = { id: string; src: Src | undefined; pageAllowed: boolean };

/** Every item that carries `src`. `page` is allowed on proposal wires, rules, checks and proposed pins. */
export function collectAnchors(c: Catalogue): Anchor[] {
  const out: Anchor[] = [];
  for (const part of c.parts) {
    out.push({ id: `part.${part.id}`, src: part.src, pageAllowed: false });
    for (const pin of part.pins) {
      out.push({ id: `part.${part.id}.${pin.id}`, src: pin.src, pageAllowed: false });
    }
  }
  for (const pad of c.pads) out.push({ id: `pad.${pad.n}`, src: pad.src, pageAllowed: false });
  for (const wire of c.wires) {
    out.push({ id: `wire.${wire.n}`, src: wire.src, pageAllowed: wire.pin === 'proposal' });
  }
  for (const fn of c.functions) out.push({ id: `function.${fn.id}`, src: fn.src, pageAllowed: false });
  for (const rule of c.rules) out.push({ id: `rule.${rule.id}`, src: rule.src, pageAllowed: true });
  for (const check of c.checks) out.push({ id: `check.${check.id}`, src: check.src, pageAllowed: true });
  for (const source of c.powerSources) out.push({ id: `power.${source.id}`, src: source.src, pageAllowed: false });
  for (const stage of c.stages) out.push({ id: `stage.${stage.n}`, src: stage.src, pageAllowed: false });
  c.safety.forEach((note, i) => out.push({ id: `safety.${i}`, src: note.src, pageAllowed: false }));
  for (const item of c.notInRelease) {
    out.push({ id: `notInRelease.${item.id}`, src: item.src, pageAllowed: false });
  }
  for (const pin of c.pinDefaults) {
    out.push({ id: `pin.${pin.signal}`, src: pin.src, pageAllowed: pin.choice === 'proposal' });
  }
  return out;
}

function lineCount(content: string): number {
  if (content === '') return 0;
  const lines = content.split('\n').length;
  return content.endsWith('\n') ? lines - 1 : lines;
}

export type SrcResult = { findings: Finding[]; repoAnchors: number; repoChecked: number };

/** The shortest text an anchor may name, in characters. */
export const ANCHOR_TEXT_MIN = 4;

/** A line or a text with each run of white space as one space. */
function squash(s: string): string {
  return s.replace(/\s+/g, ' ').trim();
}

/**
 * Checks every `src`. With `tree` null the repository anchors are counted, not read.
 * `texts` names for each repository anchor a text its line holds at the tag;
 * a line that is present and holds another row of the document is an error.
 */
export function checkSrc(
  c: Catalogue, tree: Tree | null, texts: ReadonlyArray<AnchorText> = ANCHOR_TEXTS,
): SrcResult {
  const findings: Finding[] = [];
  let repoAnchors = 0;
  let repoChecked = 0;

  const unknownPhotos = new Set<string>();
  for (const check of c.checks) {
    if (check.kind === 'unknown' && check.src?.kind === 'photo') unknownPhotos.add(check.src.file);
  }

  for (const { id, src, pageAllowed } of collectAnchors(c)) {
    if (typeof src !== 'object' || src === null) {
      findings.push(err(id, 'item has no src'));
      continue;
    }
    switch (src.kind) {
      case 'repo': {
        repoAnchors += 1;
        if (src.tag !== RELEASE_TAG) findings.push(err(id, `src names the tag ${String(src.tag)}, not ${RELEASE_TAG}`));
        if (typeof src.path !== 'string' || src.path === '' || src.path.startsWith('/')) {
          findings.push(err(id, 'src has no repository path'));
          break;
        }
        if (!Number.isInteger(src.line) || src.line < 1) {
          findings.push(err(id, `src ${src.path} has no line number`));
          break;
        }
        const named = texts.filter((x) => x.path === src.path && x.line === src.line);
        const unnamed = named.length === 0
          || named.some((x) => typeof x.has !== 'string' || squash(x.has).length < ANCHOR_TEXT_MIN);
        const noText = `src ${src.path}:${src.line} names no text of ${ANCHOR_TEXT_MIN} characters or more that the line holds`;
        if (tree === null) {
          if (unnamed) findings.push(err(id, noText));
          break;
        }
        repoChecked += 1;
        const content = tree.read(src.path);
        if (content === null) {
          findings.push(err(id, `src ${src.path} does not exist at ${RELEASE_TAG}`));
        } else if (lineCount(content) < src.line) {
          findings.push(err(id, `src ${src.path}:${src.line} is past the end of the file (${lineCount(content)} lines)`));
        } else if ((content.split('\n')[src.line - 1] ?? '').trim() === '') {
          findings.push(err(id, `src ${src.path}:${src.line} is an empty line`));
        } else if (unnamed) {
          findings.push(err(id, noText));
        } else {
          const held = squash(content.split('\n')[src.line - 1] ?? '');
          for (const text of named) {
            if (!held.includes(squash(text.has))) {
              findings.push(err(id, `src ${src.path}:${src.line} does not hold "${text.has}" at ${RELEASE_TAG}`));
            }
          }
        }
        break;
      }
      case 'guide':
        if (!GUIDE_SECTIONS.includes(src.section)) {
          findings.push(err(id, `src names the build guide section "${String(src.section)}", which is not in the list`));
        }
        break;
      case 'photo':
        if (typeof src.file !== 'string' || !/^[0-9]{2}$/.test(src.file)) {
          findings.push(err(id, 'src names no photo number'));
        } else if (!unknownPhotos.has(src.file)) {
          findings.push(err(id, `src is photo ${src.file}; no check of kind "unknown" names that photo`));
        }
        break;
      case 'page':
        if (!pageAllowed) findings.push(err(id, 'src "page" is not allowed on this item'));
        break;
      case 'owner':
        if (!OWNER_STATEMENTS.includes(src.date)) {
          findings.push(err(id, `src names the owner statement "${String(src.date)}", which is not in the list`));
        }
        break;
      case 'doc':
        if (typeof src.title !== 'string' || src.title === '') findings.push(err(id, 'src "doc" has no title'));
        if (typeof src.url !== 'string' || !DOC_URL.test(src.url)) findings.push(err(id, 'src "doc" has no https address'));
        if (typeof src.ref !== 'string' || src.ref === '') findings.push(err(id, 'src "doc" names no page, net or table'));
        break;
      default:
        findings.push(err(id, `src has the unknown kind "${String((src as { kind?: unknown }).kind)}"`));
    }
  }
  return { findings, repoAnchors, repoChecked };
}

// ---------------------------------------------------------------------------
// Invariant 6: counts
// ---------------------------------------------------------------------------

export function checkCounts(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  if (c.safety.length !== 7) out.push(err('safety', `${c.safety.length} entries, not 7`));
  if (c.stages.length !== 5) out.push(err('stages', `${c.stages.length} entries, not 5`));
  c.stages.forEach((stage, i) => {
    if (stage.n !== i + 1) out.push(err(`stage.${stage.n}`, 'stages are not numbered 1 to 5 in order'));
  });
  if (c.pads.length !== 40) out.push(err('pads', `${c.pads.length} entries, not 40`));
  c.pads.forEach((pad, i) => {
    if (pad.n !== i + 1) out.push(err(`pad.${pad.n}`, 'pads are not numbered 1 to 40 in order'));
  });
  for (const gpio of duplicates(c.pads.filter((p) => p.gpio !== null).map((p) => p.gpio as number))) {
    out.push(err(`pads`, `GP${gpio} is on two pads`));
  }
  if (c.hashVersion !== 1) out.push(err('hashVersion', 'is not 1'));
  if (!/^[0-9]+\.[0-9]+\.[0-9]+$/.test(c.release)) out.push(err('release', 'is not a version number'));
  else if (`v${c.release}` !== RELEASE_TAG) out.push(err('release', `is ${c.release}; the anchors name ${RELEASE_TAG}`));
  return out;
}

// ---------------------------------------------------------------------------
// Invariant 7: every function resolves on its own
// ---------------------------------------------------------------------------

export function checkFunctions(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  for (const fn of c.functions) {
    const setup = resolve(c, { functions: [fn.id], owned: [], options: { ...DEFAULT_OPTIONS } });
    const own = setup.functions.find((f) => f.fn.id === fn.id);
    if (!own || !own.chosen || !own.enabled || own.missing.length > 0) {
      out.push(err(`function.${fn.id}`, `chosen alone it is not enabled; missing: ${own?.missing.join(', ') ?? 'the function'}`));
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Invariant 8: pins
// ---------------------------------------------------------------------------

function partnerOf(gpio: number): number | null {
  for (const [a, b] of COMPARE_PARTNERS) {
    if (gpio === a) return b;
    if (gpio === b) return a;
  }
  return null;
}

/** PWM slice of a GPIO. */
export function sliceOf(gpio: number): number {
  return Math.floor(gpio / 2) % 8;
}

/** The GPIOs an output may use: every GPIO pad that is not reserved and is not the heartbeat input. */
export function outputCatalogue(c: Catalogue): number[] {
  const heartbeat = c.pinDefaults.find((d) => d.signal === 'heartbeat')?.gpio;
  return c.pads
    .filter((p): p is Pad & { gpio: number } => p.gpio !== null && !p.reserved && p.gpio !== heartbeat)
    .map((p) => p.gpio);
}

/** The SDA values the firmware offers: −1 and each even GPIO whose pair SDA, SDA + 1 is free. */
export function sdaAllowed(c: Catalogue): number[] {
  const free = new Set(outputCatalogue(c));
  return [-1, ...[...free].filter((g) => g % 2 === 0 && free.has(g + 1)).sort((a, b) => a - b)];
}

function allowedSet(c: Catalogue, pin: PinDefault): number[] {
  return pin.allowed === 'catalogue' ? outputCatalogue(c) : pin.allowed;
}

export function checkPins(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  const pads = new Map(c.pads.map((p) => [p.n, p]));
  const bySignal = new Map(c.pinDefaults.map((d) => [d.signal, d]));

  for (const pin of c.pinDefaults) {
    const id = `pin.${pin.signal}`;
    const pad = pads.get(pin.pad);
    if (!pad || pad.gpio !== pin.gpio) {
      out.push(err(id, `pad ${pin.pad} is not GP${pin.gpio}`));
      continue;
    }
    // Rule 1: allowed in the firmware, and not on a reserved pad.
    if (!allowedSet(c, pin).includes(pin.gpio)) out.push(err(id, `GP${pin.gpio} is not in the allowed set`));
    if (pad.reserved) out.push(err(id, `GP${pin.gpio} is on a reserved pad`));
  }

  // Rule 2: no two signals share a GPIO.
  for (const gpio of duplicates(c.pinDefaults.map((d) => d.gpio))) {
    out.push(err('pins', `GP${gpio} carries two signals`));
  }

  // Rule 3: SCL is SDA + 1, and the two lists are the ones the firmware offers.
  const sda = bySignal.get('sda');
  const scl = bySignal.get('scl');
  if (!sda || !scl) {
    out.push(err('pins', 'sda or scl is missing'));
  } else {
    if (scl.gpio !== sda.gpio + 1) out.push(err('pin.scl', `GP${scl.gpio} is not SDA + 1`));
    const offered = sdaAllowed(c);
    if (sda.allowed === 'catalogue' || sda.allowed.join() !== offered.join()) {
      out.push(err('pin.sda', `allowed set is not ${offered.join(', ')}`));
    }
    const follows = offered.map((g) => (g < 0 ? g : g + 1));
    if (scl.allowed === 'catalogue' || scl.allowed.join() !== follows.join()) {
      out.push(err('pin.scl', `allowed set is not ${follows.join(', ')}`));
    }
  }

  // Rule 4: no two outputs share a PWM slice.
  const outputs = c.pinDefaults.filter((d) => d.role === 'output');
  for (const slice of duplicates(outputs.map((d) => sliceOf(d.gpio)))) {
    out.push(err('pins', `two outputs share PWM slice ${slice}`));
  }

  // Rule 5: no output on a GPIO whose compare-register partner carries a signal.
  const used = new Map(c.pinDefaults.map((d) => [d.gpio, d.signal]));
  for (const pin of outputs) {
    const partner = partnerOf(pin.gpio);
    if (partner !== null && used.has(partner)) {
      out.push(err(`pin.${pin.signal}`, `GP${pin.gpio} shares a compare register with GP${partner} (${used.get(partner)})`));
    }
  }

  // Rule 6: the tap pin is not GP26 to GP29.
  const tap = bySignal.get('tap');
  if (!tap) out.push(err('pins', 'tap is missing'));
  else if (tap.gpio >= 26 && tap.gpio <= 29) out.push(err('pin.tap', `GP${tap.gpio} is an analogue pin`));

  // Rule 7: the heartbeat pin is GP3.
  const heartbeat = bySignal.get('heartbeat');
  if (!heartbeat) out.push(err('pins', 'heartbeat is missing'));
  else if (heartbeat.gpio !== 3 || heartbeat.choice !== 'fixed') out.push(err('pin.heartbeat', 'is not fixed on GP3'));

  // Wires and the pin table agree: a wire on a GPIO pad names the choice of
  // that pad's signal, and a wire on no GPIO pad names none.
  const byPad = new Map(c.pinDefaults.map((d) => [d.pad, d]));
  for (const wire of c.wires) {
    const id = `wire.${wire.n}`;
    const gpioEnds = endsOf(wire)
      .map((end) => padOf(end))
      .filter((n): n is number => n !== null && pads.get(n)?.gpio !== null && pads.get(n) !== undefined);
    if (gpioEnds.length === 0) {
      if (wire.pin !== undefined) out.push(err(id, 'names a pin choice and ends on no GPIO pad'));
      if (wire.src?.kind === 'page') out.push(err(id, 'src "page" on a wire without a proposed pin'));
      continue;
    }
    for (const n of gpioEnds) {
      const pin = byPad.get(n);
      if (!pin) out.push(err(id, `pad ${n} carries no signal of the pin table`));
      else if (wire.pin !== pin.choice) out.push(err(id, `pin choice is ${String(wire.pin)}; pad ${n} (${pin.signal}) is ${pin.choice}`));
    }
    if (wire.pin === 'proposal') {
      // A pin that is no firmware default comes from this page or from an
      // owner statement. The wire and a check entry say which.
      const stated = wire.src?.kind === 'owner';
      const sentence = stated ? 'Pin from an owner statement of ' : 'Pin proposed by this page.';
      const word = stated ? /owner statement/ : /propos/;
      if (!wire.note || !wire.note.en.includes(sentence)) {
        out.push(err(id, `a pin that is no firmware default has the note "${sentence}"`));
      }
      for (const n of gpioEnds) {
        const name = pads.get(n)?.name ?? '';
        const named = c.checks.some((k) => new RegExp(`\\b${name}\\b`).test(k.text.en) && word.test(k.text.en));
        if (!named) {
          out.push(err(id, `no check entry states that ${name} is ${stated ? 'from an owner statement' : 'a proposal'}`));
        }
      }
    }
    if (wire.pin === 'example' && (!wire.note || !wire.note.en.includes("Pin from the build guide's example."))) {
      out.push(err(id, 'an example pin has the note "Pin from the build guide\'s example."'));
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Invariant 10: slots and lanes
// ---------------------------------------------------------------------------

export function checkRoutes(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  for (const column of ['L', 'R', 'none'] as const) {
    const orders = c.parts.filter((p) => p.slot.column === column).map((p) => p.slot.order);
    for (const order of duplicates(orders)) {
      out.push(err(`slot.${column}.${order}`, 'slot order is used twice in the column'));
    }
  }
  const inRange = (value: number, max: number): boolean => Number.isInteger(value) && value >= 0 && value <= max;
  for (const wire of c.wires) {
    const id = `wire.${wire.n}`;
    const route = wire.route;
    if (route.kind === 'L' || route.kind === 'R' || route.kind === 'O') {
      if (!inRange(route.lane, LANE_MAX[route.kind])) out.push(err(id, `lane ${route.lane} is outside 0 to ${LANE_MAX[route.kind]}`));
    } else if (route.kind === 'X') {
      if (!inRange(route.left, LANE_MAX.L)) out.push(err(id, `left lane ${route.left} is outside 0 to ${LANE_MAX.L}`));
      if (!inRange(route.right, LANE_MAX.R)) out.push(err(id, `right lane ${route.right} is outside 0 to ${LANE_MAX.R}`));
      if (!inRange(route.channel, LANE_MAX.channel)) out.push(err(id, `channel ${route.channel} is outside 0 to ${LANE_MAX.channel}`));
    }
    if ((route.kind === 'S') !== (wire.kind === 'stub')) out.push(err(id, 'route S and kind stub go together'));
    if ((route.kind === 'Z') !== (wire.kind === 'component')) out.push(err(id, 'route Z and kind component go together'));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Facts read from the repository at the release tag
// ---------------------------------------------------------------------------

const OUT_BIND = 'shared/outputs/out_bind.c';
const SETTINGS = 'shared/settings/settings.c';
const IOMCU_PINS = 'firmware/iomcu/include/iomcu_pins.h';

export type FirmwarePin = { gpio: number; pad: number; reserved: boolean; line: number };
export type FirmwarePad = { pad: number; ground: boolean; name: string | null; line: number };

/** The rows of `k_pico_pins` in out_bind.c. */
export function parseFirmwarePins(content: string): FirmwarePin[] {
  const out: FirmwarePin[] = [];
  const rows = /^\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(true|false)\s*,/;
  content.split('\n').forEach((text, i) => {
    const m = rows.exec(text);
    if (m) out.push({ gpio: Number(m[1]), pad: Number(m[2]), reserved: m[3] === 'true', line: i + 1 });
  });
  return out;
}

/** The rows of `k_pico_pads` in out_bind.c: the pads that are no GPIO. */
export function parseFirmwarePads(content: string): FirmwarePad[] {
  const out: FirmwarePad[] = [];
  const rows = /^\s*\{\s*(\d+)\s*,\s*LINK_PAD_(\w+)\s*,\s*\d+\s*\}\s*,?\s*(?:\/\*\s*([A-Za-z0-9_]+))?/;
  content.split('\n').forEach((text, i) => {
    const m = rows.exec(text);
    if (m) out.push({ pad: Number(m[1]), ground: m[2] === 'GROUND', name: m[3] ?? null, line: i + 1 });
  });
  return out;
}

/** Default value and line of one integer setting in settings.c, or null. */
export function parseSettingDefault(content: string, key: string): { def: number; line: number } | null {
  const lines = content.split('\n');
  const start = lines.findIndex((text) => text.includes(`[${key}]`));
  if (start < 0) return null;
  for (let i = start; i < Math.min(start + 4, lines.length); i += 1) {
    const m = /SET_TYPE_\w+\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,/.exec(lines[i] ?? '');
    if (m) return { def: Number(m[4]), line: start + 1 };
  }
  return null;
}

const SETTING_OF: Partial<Record<PinDefault['signal'], string>> = {
  sda: 'SET_SENSE_SDA',
  scl: 'SET_SENSE_SCL',
  tap: 'SET_TONE_PIN',
  pd_tx: 'SET_PDMINI_TX',
  pd_rx: 'SET_PDMINI_RX',
};

/** Compares the pads and pins of the catalogue with the firmware sources at the release tag. */
export function checkTree(c: Catalogue, tree: Tree): Finding[] {
  const out: Finding[] = [];

  if (tree.newestTag !== null && tree.newestTag !== `v${c.release}`) {
    out.push(warn('release', `the data describes ${c.release}; the newest release tag is ${tree.newestTag}`));
  }

  const heartbeat = c.pinDefaults.find((d) => d.signal === 'heartbeat');

  const outBind = tree.read(OUT_BIND);
  if (outBind === null) {
    out.push(err('pads', `${OUT_BIND} does not exist at ${RELEASE_TAG}`));
  } else {
    const fwPins = new Map(parseFirmwarePins(outBind).map((p) => [p.pad, p]));
    const fwPads = new Map(parseFirmwarePads(outBind).map((p) => [p.pad, p]));
    if (fwPins.size + fwPads.size !== c.pads.length) {
      out.push(err('pads', `${OUT_BIND} lists ${fwPins.size + fwPads.size} pads, the data ${c.pads.length}`));
    }
    for (const pad of c.pads) {
      const id = `pad.${pad.n}`;
      const anchored = pad.src?.kind === 'repo' && pad.src.path === OUT_BIND ? pad.src.line : null;
      if (pad.gpio !== null) {
        const row = fwPins.get(pad.n);
        if (!row) {
          out.push(err(id, `${OUT_BIND} has no GPIO on this pad`));
          continue;
        }
        if (row.gpio !== pad.gpio) out.push(err(id, `is GP${pad.gpio} in the data and GP${row.gpio} in ${OUT_BIND}`));
        if (pad.name !== `GP${pad.gpio}`) out.push(err(id, `name ${pad.name} is not GP${pad.gpio}`));
        // The firmware reserves the pads the data marks reserved, and the heartbeat input.
        const expected = pad.reserved !== undefined || pad.gpio === heartbeat?.gpio;
        if (row.reserved !== expected) {
          out.push(err(id, `${OUT_BIND} ${row.reserved ? 'reserves' : 'does not reserve'} GP${pad.gpio}; the data says otherwise`));
        }
        if (anchored !== row.line) out.push(err(id, `src is not the row of this pad, ${OUT_BIND}:${row.line}`));
      } else {
        const row = fwPads.get(pad.n);
        if (!row) {
          out.push(err(id, `${OUT_BIND} lists this pad as a GPIO or not at all`));
          continue;
        }
        if (row.ground !== (pad.name === 'GND')) out.push(err(id, `ground in one of the data and ${OUT_BIND}, not in the other`));
        if (row.name !== null && row.name !== pad.name) out.push(err(id, `name is ${pad.name} in the data and ${row.name} in ${OUT_BIND}`));
        if (anchored !== row.line) out.push(err(id, `src is not the row of this pad, ${OUT_BIND}:${row.line}`));
      }
    }
  }

  const iomcu = tree.read(IOMCU_PINS);
  const hb = iomcu === null ? null : /#define\s+IOMCU_HEARTBEAT_PIN\s+(\d+)/.exec(iomcu);
  if (!hb) out.push(err('pin.heartbeat', `${IOMCU_PINS} defines no IOMCU_HEARTBEAT_PIN at ${RELEASE_TAG}`));
  else if (heartbeat && Number(hb[1]) !== heartbeat.gpio) {
    out.push(err('pin.heartbeat', `is GP${heartbeat.gpio} in the data and GP${hb[1]} in ${IOMCU_PINS}`));
  }

  const settings = tree.read(SETTINGS);
  if (settings === null) {
    out.push(err('pins', `${SETTINGS} does not exist at ${RELEASE_TAG}`));
  } else {
    for (const pin of c.pinDefaults) {
      const key = SETTING_OF[pin.signal];
      if (key === undefined) continue;
      const id = `pin.${pin.signal}`;
      const found = parseSettingDefault(settings, key);
      if (found === null) {
        out.push(err(id, `${SETTINGS} has no ${key}`));
        continue;
      }
      if (pin.choice === 'default' && found.def !== pin.gpio) {
        out.push(err(id, `default is GP${pin.gpio} in the data and ${found.def} in ${SETTINGS}`));
      }
      if (pin.choice === 'proposal' && found.def !== -1) {
        out.push(err(id, `is a proposal in the data; ${SETTINGS} has the default ${found.def}`));
      }
      if (pin.src?.kind === 'repo' && pin.src.path === SETTINGS
        && (pin.src.line < found.line || pin.src.line > found.line + 2)) {
        out.push(err(id, `src is not inside ${key}, ${SETTINGS}:${found.line}`));
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// The parts list: rows to buy and rows the visitor brings
// ---------------------------------------------------------------------------

/** Every set of options: the product of the values of each option. */
export function allOptions(): Options[] {
  let out: Array<Record<string, string>> = [{}];
  for (const key of OPTION_KEYS) {
    out = out.flatMap((o) => (OPTION_VALUES[key] as ReadonlyArray<string>).map((value) => ({ ...o, [key]: value })));
  }
  return out as Options[];
}

/** A maker page: https, an address that parses, no tracking parameter, no fragment. */
export function pageProblem(page: string): string | null {
  let url: URL;
  try {
    url = new URL(page);
  } catch {
    return 'does not parse as a URL';
  }
  if (url.protocol !== 'https:') return 'is not https';
  if (url.hash !== '') return 'has a fragment';
  if ([...url.searchParams.keys()].some((key) => /^(utm_|fbclid$|gclid$|ref$|tag$|aff)/i.test(key))) return 'has a tracking parameter';
  return null;
}

export function checkBuy(c: Catalogue): Finding[] {
  const out: Finding[] = [];
  const sets = allOptions();
  for (const part of c.parts) {
    const id = `part.${part.id}`;
    if (part.yours === true && (part.locked || part.selectable)) out.push(err(id, 'a part the visitor brings is neither locked nor selectable'));
    if (sets.some((o) => buyRows(part, o).length === 0)) out.push(err(id, 'a set of options gives no row of the parts list'));
    if (part.variantName) {
      const { option, names } = part.variantName;
      for (const value of Object.keys(names)) {
        const shown = sets.filter((o) => o[option] === value);
        if (shown.some((o) => !buyRows(part, o).some((row) => row.only?.[option] === value))) {
          out.push(err(id, `the variant ${option}=${value} has no row of its own in the parts list`));
        }
      }
    }
    if (part.buy.filter((row) => row.id === undefined).length > 1) out.push(err(id, 'more than 1 row of the parts list has no id'));
    const rowIds = part.buy.flatMap((row) => (row.id === undefined ? [] : [row.id]));
    for (const rowId of new Set(rowIds.filter((x, i) => rowIds.indexOf(x) !== i))) out.push(err(id, `the row id ${rowId} occurs twice`));
    part.buy.forEach((row, index) => {
      const at = `${id}.buy.${index}`;
      if (row.id !== undefined && !ROW_ID.test(row.id)) out.push(err(at, `the row id ${row.id} is not 1 to 12 lowercase letters and digits`));
      for (const [key, value] of Object.entries(row.only ?? {})) {
        if (!isOptionValue(key, value)) out.push(err(at, `${key}=${String(value)} is not a value of an option`));
      }
      if (row.maker !== null) {
        out.push(...checkString(`${at}.maker`, row.maker, false));
        if (brought(part)) out.push(err(at, 'a part the visitor brings names no maker'));
      }
      if (row.page !== undefined) {
        const problem = pageProblem(row.page);
        if (problem !== null) out.push(err(at, `the page ${row.page} ${problem}`));
        if (row.maker === null) out.push(err(at, 'a page without a maker'));
      }
      if (row.qty !== undefined && !(Number.isInteger(row.qty) && row.qty >= 1 && row.qty <= 99)) {
        out.push(err(at, `quantity ${String(row.qty)} is not a whole number from 1 to 99`));
      }
    });
  }
  return out;
}

// ---------------------------------------------------------------------------
// All checks, the wire table, the command line
// ---------------------------------------------------------------------------

export type Report = { findings: Finding[]; repoAnchors: number; repoChecked: number };

/** A catalogue that is not the shape the checks walk gives one finding, not an exception. */
function guarded(name: string, run: () => Finding[]): Finding[] {
  try {
    return run();
  } catch (cause) {
    return [err(name, `check stopped: ${cause instanceof Error ? cause.message : String(cause)}`)];
  }
}

export function checkAll(
  c: Catalogue, tree: Tree | null, strict = false, texts: ReadonlyArray<AnchorText> = ANCHOR_TEXTS,
  pictures: PictureFolder | null = null,
): Report {
  const findings: Finding[] = [
    ...guarded('ids', () => checkIds(c)),
    ...guarded('ends', () => checkEnds(c)),
    ...guarded('refs', () => checkRefs(c)),
    ...guarded('texts', () => checkTexts(c)),
    ...guarded('counts', () => checkCounts(c)),
    ...guarded('functions', () => checkFunctions(c)),
    ...guarded('pins', () => checkPins(c)),
    ...guarded('routes', () => checkRoutes(c)),
    ...guarded('buy', () => checkBuy(c)),
  ];
  if (pictures !== null) findings.push(...guarded('pictures', () => checkPictures(c, pictures)));
  let repoAnchors = 0;
  let repoChecked = 0;
  findings.push(...guarded('src', () => {
    const result = checkSrc(c, tree, texts);
    repoAnchors = result.repoAnchors;
    repoChecked = result.repoChecked;
    return result.findings;
  }));
  if (tree !== null) {
    findings.push(...guarded('tree', () => checkTree(c, tree)));
  } else if (strict) {
    findings.push(err('repository', `git or the tag ${RELEASE_TAG} is absent; ${repoAnchors} repository anchors are not checked`));
  }
  return { findings, repoAnchors, repoChecked };
}

function condText(cond: Cond | null, nested = false): string {
  if (cond === null) return 'always';
  if ('has' in cond) return cond.has;
  if ('opt' in cond) return `${cond.opt}=${cond.is}`;
  if ('not' in cond) return `not ${condText(cond.not, true)}`;
  const word = 'all' in cond ? ' and ' : ' or ';
  const list = 'all' in cond ? cond.all : cond.any;
  const text = list.map((item) => condText(item, true)).join(word);
  return nested ? `(${text})` : text;
}

function laneText(wire: Wire): string {
  const route = wire.route;
  if (route.kind === 'L' || route.kind === 'R' || route.kind === 'O') return String(route.lane);
  if (route.kind === 'X') return `L${route.left}, R${route.right}, C${route.channel}`;
  return '';
}

/** The wire table as Markdown, in the column order of the specification. */
export function wireTable(c: Catalogue): string {
  const rows = [
    '| No. | From | To | Class | Route | Lane | Stage | Present when |',
    '|---|---|---|---|---|---|---|---|',
  ];
  for (const wire of c.wires) {
    const to = wire.b ?? `(${wire.to?.en ?? ''})`;
    rows.push(
      `| ${wire.n} | ${wire.a} | ${to} | ${wire.cls} | ${wire.route.kind} | ${laneText(wire)} | ${wire.stage} | ${condText(wire.when)} |`,
    );
  }
  return `${rows.join('\n')}\n`;
}

/** The repository that holds this file, read at the release tag. Null when git or the tag is absent. */
export function gitTree(dir: string): Tree | null {
  const git = (args: string[]): string | null => {
    try {
      return execFileSync('git', ['-C', dir, ...args], {
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'ignore'],
        maxBuffer: 64 * 1024 * 1024,
      });
    } catch {
      return null;
    }
  };
  if (git(['rev-parse', '--quiet', '--verify', `refs/tags/${RELEASE_TAG}^{commit}`]) === null) return null;
  const cache = new Map<string, string | null>();
  const tags = git(['tag', '--list', 'v[0-9]*', '--sort=-v:refname']);
  return {
    read: (path) => {
      if (!cache.has(path)) cache.set(path, git(['show', `${RELEASE_TAG}:${path}`]));
      return cache.get(path) ?? null;
    },
    newestTag: tags?.split('\n').find((tag) => /^v[0-9]+\.[0-9]+\.[0-9]+$/.test(tag)) ?? null,
  };
}

function main(argv: string[]): number {
  const known = ['--wires', '--strict'];
  const unknownArg = argv.find((arg) => !known.includes(arg));
  if (unknownArg !== undefined) {
    process.stderr.write(`check: unknown argument ${unknownArg}\nusage: node tools/check.ts [--strict] [--wires]\n`);
    return 2;
  }
  if (argv.includes('--wires')) {
    process.stdout.write(wireTable(CATALOGUE));
    return 0;
  }
  const tree = gitTree(dirname(fileURLToPath(import.meta.url)));
  const folder = pictureFolder(PICTURE_DIR);
  const report = checkAll(CATALOGUE, tree, argv.includes('--strict'), ANCHOR_TEXTS, folder);
  report.findings.push(...checkReleaseSources());
  for (const finding of report.findings) {
    process.stdout.write(`${finding.level} ${finding.id}: ${finding.msg}\n`);
  }
  const errors = report.findings.filter((f) => f.level === 'error').length;
  const warnings = report.findings.length - errors;
  const c = CATALOGUE;
  process.stdout.write(
    `check: ${c.parts.length} parts, ${c.pads.length} pads, ${c.wires.length} wires, `
    + `${c.functions.length} functions, ${c.rules.length} rules, ${c.checks.length} checks, `
    + `${collectTexts(c).length} texts\n`,
  );
  process.stdout.write(`check: ${folder.names.length} files in the picture folder\n`);
  if (tree === null) {
    process.stdout.write(`check: skipped ${report.repoAnchors} repository anchors; git or the tag ${RELEASE_TAG} is absent\n`);
  } else {
    process.stdout.write(`check: ${report.repoChecked} repository anchors read at ${RELEASE_TAG}\n`);
  }
  process.stdout.write(`check: ${errors} errors, ${warnings} warnings\n`);
  return errors > 0 ? 1 : 0;
}

if (process.argv[1] !== undefined && fileURLToPath(import.meta.url) === process.argv[1]) {
  process.exitCode = main(process.argv.slice(2));
}
