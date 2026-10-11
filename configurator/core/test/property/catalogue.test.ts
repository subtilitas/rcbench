// Integrity of the catalogue, by a walk over the whole object.
//
// The walk knows the shapes `Text` and `Src` and nothing else, so a record a
// later change adds is covered without a change here. tools/check.ts states
// the same rules record by record; this file is independent of it.
//
//  1. Every id is unique in its collection.
//  2. Every reference resolves: part ids, pin ids, pads, stages, option values.
//  3. Every text has English and German, and nothing else.
//  4. No text holds a banned word or an exclamation mark.
//  5. Every source anchor is well-formed for its kind.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Catalogue, Cond, Options, Src } from '../../../contract.ts';
import { CATALOGUE } from '../../catalogue.ts';
import { OPTION_KEYS, OPTION_VALUES } from '../../resolve.ts';
import { BANNED, bannedIn } from './words.ts';

/** The ids the hash codec carries: 1 to 32 characters of a to z and 0 to 9. */
const HASH_ID = /^[a-z0-9]{1,32}$/;

type Found<T> = { value: T; path: string };

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** Every object below `root` that has an `en` or a `de` key. */
function texts(root: unknown): Array<Found<Record<string, unknown>>> {
  const out: Array<Found<Record<string, unknown>>> = [];
  const walk = (value: unknown, path: string): void => {
    if (Array.isArray(value)) {
      value.forEach((item, i) => walk(item, `${path}[${i}]`));
    } else if (isRecord(value)) {
      if ('en' in value || 'de' in value) out.push({ value, path });
      else for (const [key, item] of Object.entries(value)) walk(item, `${path}.${key}`);
    }
  };
  walk(root, 'catalogue');
  return out;
}

/** Every value below `root` under a key `src`, and every record that lacks one. */
function sources(root: unknown): { found: Array<Found<unknown>>; records: number } {
  const found: Array<Found<unknown>> = [];
  let records = 0;
  const walk = (value: unknown, path: string): void => {
    if (Array.isArray(value)) {
      value.forEach((item, i) => walk(item, `${path}[${i}]`));
    } else if (isRecord(value)) {
      if ('src' in value) {
        found.push({ value: value.src, path: `${path}.src` });
        records += 1;
      }
      for (const [key, item] of Object.entries(value)) if (key !== 'src') walk(item, `${path}.${key}`);
    }
  };
  walk(root, 'catalogue');
  return { found, records };
}

function duplicates<T>(list: ReadonlyArray<T>): T[] {
  const seen = new Set<T>();
  const twice = new Set<T>();
  for (const item of list) (seen.has(item) ? twice : seen).add(item);
  return [...twice];
}

function conds(cond: Cond | null): Cond[] {
  if (cond === null) return [];
  if ('not' in cond) return [cond, ...conds(cond.not)];
  if ('all' in cond) return [cond, ...cond.all.flatMap(conds)];
  if ('any' in cond) return [cond, ...cond.any.flatMap(conds)];
  return [cond];
}

function textProblems(text: Record<string, unknown>): string[] {
  const out: string[] = [];
  const keys = Object.keys(text).sort();
  if (keys.join(',') !== 'de,en') out.push(`has the keys ${keys.join(', ')}`);
  for (const lang of ['en', 'de']) {
    const s = text[lang];
    if (typeof s !== 'string') { out.push(`${lang} is no string`); continue; }
    if (s.length === 0) out.push(`${lang} is empty`);
    if (s !== s.trim()) out.push(`${lang} starts or ends with a space`);
    if (/\s{2,}/.test(s)) out.push(`${lang} holds two spaces in a row`);
    if (s.includes('!')) out.push(`${lang} holds an exclamation mark`);
    for (const word of bannedIn(s)) out.push(`${lang} holds "${word}"`);
  }
  return out;
}

function sourceProblems(src: unknown, release: string): string[] {
  if (!isRecord(src)) return ['is no object'];
  const keys = Object.keys(src).sort().join(',');
  const s = src as Src;
  switch (s.kind) {
    case 'repo': {
      const out: string[] = [];
      if (keys !== 'kind,line,path,tag') out.push(`has the keys ${keys}`);
      if (typeof s.path !== 'string' || !/^[A-Za-z0-9_][A-Za-z0-9_.\-/]*\.[A-Za-z0-9]+$/.test(s.path)) out.push(`path "${String(s.path)}" is no file path`);
      else if (s.path.includes('..') || s.path.includes('//') || s.path.startsWith('/')) out.push(`path "${s.path}" is not relative to the repository`);
      if (!Number.isInteger(s.line) || s.line < 1) out.push(`line ${String(s.line)} is no line number`);
      if (s.tag !== `v${release}`) out.push(`tag ${String(s.tag)} is not v${release}`);
      return out;
    }
    case 'guide':
      return [
        ...(keys !== 'kind,section' ? [`has the keys ${keys}`] : []),
        ...(typeof s.section !== 'string' || !/^[a-z0-9]+(-[a-z0-9]+)*$/.test(s.section) ? [`section "${String(s.section)}" is no section name`] : []),
      ];
    case 'photo':
      return [
        ...(keys !== 'file,kind' ? [`has the keys ${keys}`] : []),
        ...(typeof s.file !== 'string' || !/^[0-9]{2}$/.test(s.file) ? [`file "${String(s.file)}" is no photo number`] : []),
      ];
    case 'owner': {
      const out = keys !== 'date,kind' ? [`has the keys ${keys}`] : [];
      const ok = typeof s.date === 'string' && /^[0-9]{4}-[0-9]{2}-[0-9]{2}$/.test(s.date)
        && new Date(`${s.date}T00:00:00Z`).toISOString().slice(0, 10) === s.date;
      if (!ok) out.push(`date "${String(s.date)}" is no calendar date`);
      return out;
    }
    case 'page':
      return keys !== 'kind' ? [`has the keys ${keys}`] : [];
    default:
      return [`kind "${String((src as { kind?: unknown }).kind)}" is no source kind`];
  }
}

function optionProblems(only: Partial<Options> | undefined): string[] {
  if (only === undefined) return [];
  const out: string[] = [];
  for (const [key, value] of Object.entries(only)) {
    if (!(OPTION_KEYS as ReadonlyArray<string>).includes(key)) out.push(`"${key}" is no option`);
    else if (!(OPTION_VALUES[key as keyof Options] as ReadonlyArray<string>).includes(value)) out.push(`"${value}" is no value of ${key}`);
  }
  if (Object.keys(only).length === 0) out.push('names no option');
  return out;
}

/** What the reference rules find in a catalogue. Empty for a sound one. */
function referenceProblems(c: Catalogue): string[] {
  const out: string[] = [];
  const parts = new Map(c.parts.map((p) => [p.id, p]));
  const pads = new Map(c.pads.map((p) => [p.n, p]));
  const stages = new Set(c.stages.map((s) => s.n));

  const cond = (when: Cond | null, at: string): void => {
    for (const leaf of conds(when)) {
      if ('has' in leaf && !parts.has(leaf.has)) out.push(`${at}: the condition names the part ${leaf.has}`);
      if ('opt' in leaf) {
        if (!(OPTION_KEYS as ReadonlyArray<string>).includes(leaf.opt)) out.push(`${at}: the condition names the option ${leaf.opt}`);
        else if (!(OPTION_VALUES[leaf.opt] as ReadonlyArray<string>).includes(leaf.is)) out.push(`${at}: the condition names ${leaf.opt} = ${leaf.is}`);
      }
      if ('all' in leaf && leaf.all.length === 0) out.push(`${at}: the condition holds an empty "all"`);
      if ('any' in leaf && leaf.any.length === 0) out.push(`${at}: the condition holds an empty "any"`);
    }
  };
  const end = (ref: string, at: string): void => {
    const dot = ref.indexOf('.');
    const head = ref.slice(0, dot);
    const tail = ref.slice(dot + 1);
    if (dot < 1 || tail.length === 0) out.push(`${at}: "${ref}" is no end`);
    else if (head === 'can') { if (tail !== 'H' && tail !== 'L') out.push(`${at}: "${ref}" is no CAN terminal`); }
    else if (head === 'pad') { if (!/^[1-9][0-9]*$/.test(tail) || !pads.has(Number(tail))) out.push(`${at}: "${ref}" is no pad`); }
    else if (!parts.has(head)) out.push(`${at}: "${ref}" names no part`);
    else if (!parts.get(head)?.pins.some((p) => p.id === tail)) out.push(`${at}: "${ref}" names no pin`);
  };

  for (const fn of c.functions) {
    for (const id of fn.needs) if (!parts.has(id)) out.push(`function ${fn.id} needs the part ${id}`);
    for (const p of optionProblems(fn.forces)) out.push(`function ${fn.id} forces: ${p}`);
    // A function that needs no part is in every setup: it has no option to force.
    if (fn.needs.length === 0 && Object.keys(fn.forces ?? {}).length > 0) out.push(`function ${fn.id} needs no part and forces an option`);
  }
  for (const rule of c.rules) {
    if (!parts.has(rule.adds)) out.push(`rule ${rule.id} adds the part ${rule.adds}`);
    cond(rule.when, `rule ${rule.id}`);
  }
  for (const check of c.checks) cond(check.when, `check ${check.id}`);
  for (const wire of c.wires) {
    end(wire.a, `wire ${wire.n}`);
    if (wire.b !== null) end(wire.b, `wire ${wire.n}`);
    cond(wire.when, `wire ${wire.n}`);
    if (!stages.has(wire.stage)) out.push(`wire ${wire.n} names the stage ${wire.stage}`);
  }
  for (const part of c.parts) {
    for (const pin of part.pins) for (const p of optionProblems(pin.only)) out.push(`pin ${part.id}.${pin.id}: ${p}`);
    for (const picture of part.pictures ?? []) for (const p of optionProblems(picture.only)) out.push(`picture ${picture.file}: ${p}`);
    if (part.variantName !== undefined) {
      const { option, names } = part.variantName;
      if (!(OPTION_KEYS as ReadonlyArray<string>).includes(option)) out.push(`part ${part.id} names the option ${option}`);
      else {
        for (const value of Object.keys(names)) {
          if (!(OPTION_VALUES[option] as ReadonlyArray<string>).includes(value)) out.push(`part ${part.id} names ${option} = ${value}`);
        }
      }
    }
    if (part.locked && part.selectable) out.push(`part ${part.id} is locked and can be ticked`);
  }
  for (const d of c.pinDefaults) {
    const pad = pads.get(d.pad);
    if (!pad) out.push(`pin default ${d.signal} names the pad ${d.pad}`);
    else if (pad.gpio !== d.gpio) out.push(`pin default ${d.signal} names GPIO ${d.gpio} on pad ${d.pad}, which is GPIO ${String(pad.gpio)}`);
    if (d.allowed !== 'catalogue' && !d.allowed.includes(d.gpio)) out.push(`pin default ${d.signal} is outside its allowed GPIOs`);
  }
  const taken = new Set([...c.parts.map((p) => p.id), ...c.functions.map((f) => f.id)]);
  for (const n of c.notInRelease) if (taken.has(n.id)) out.push(`"${n.id}" is in no release and is a part or a function`);
  return out;
}

/** The ids that occur twice, per collection. Empty for a sound catalogue. */
function idProblems(c: Catalogue): string[] {
  const out: string[] = [];
  const report = (name: string, list: ReadonlyArray<string | number>): void => {
    for (const id of duplicates(list)) out.push(`${name} ${id} occurs twice`);
  };
  report('part', c.parts.map((p) => p.id));
  for (const part of c.parts) report(`pin of ${part.id}`, part.pins.map((p) => p.id));
  report('picture', c.parts.flatMap((p) => (p.pictures ?? []).map((x) => x.file)));
  report('pad', c.pads.map((p) => p.n));
  report('GPIO', c.pads.flatMap((p) => (p.gpio === null ? [] : [p.gpio])));
  report('wire', c.wires.map((w) => w.n));
  report('function', c.functions.map((f) => f.id));
  report('rule', c.rules.map((r) => r.id));
  report('check', c.checks.map((x) => x.id));
  report('stage', c.stages.map((s) => s.n));
  report('"not in a release" item', c.notInRelease.map((n) => n.id));
  report('pin default', c.pinDefaults.map((d) => d.signal));
  return out;
}

// ---------------------------------------------------------------------------

test('1: every id is unique in its collection', () => {
  assert.deepEqual(idProblems(CATALOGUE), []);
});

test('1: the ids the URL carries fit the hash codec', () => {
  for (const fn of CATALOGUE.functions) assert.match(fn.id, HASH_ID, `function ${fn.id}`);
  for (const part of CATALOGUE.parts) assert.match(part.id, HASH_ID, `part ${part.id}`);
  for (const n of CATALOGUE.notInRelease) assert.match(n.id, HASH_ID, `"not in a release" item ${n.id}`);
  for (const key of OPTION_KEYS) {
    for (const value of OPTION_VALUES[key]) assert.match(value, HASH_ID, `${key} = ${value}`);
  }
  // An option token names its key: no value belongs to two options.
  assert.deepEqual(duplicates(OPTION_KEYS.flatMap((key) => [...OPTION_VALUES[key]] as string[])), []);
});

test('2: every reference resolves', () => {
  assert.deepEqual(referenceProblems(CATALOGUE), []);
});

test('2: every part is reachable, and every rule can hold', () => {
  const needed = new Set(CATALOGUE.functions.flatMap((f) => f.needs));
  const added = new Set(CATALOGUE.rules.map((r) => r.adds));
  const driven = new Set(CATALOGUE.functions.flatMap((f) => (f.drives === undefined ? [] : [f.drives])));
  for (const part of CATALOGUE.parts) {
    assert.ok(part.locked || part.selectable || needed.has(part.id) || added.has(part.id) || driven.has(part.id),
      `nothing brings the part ${part.id} into a setup`);
    // A test object comes with a function that drives it, and by no other way.
    assert.equal(driven.has(part.id), part.testObject === true, `${part.id}: driven by a function, and a test object or not`);
    if (part.testObject === true) {
      assert.ok(!part.locked && !part.selectable && !needed.has(part.id) && !added.has(part.id), `the test object ${part.id} is bench hardware as well`);
    }
  }
  // A wire of a pin or pad end belongs to a part that can be in a setup with it.
  for (const wire of CATALOGUE.wires) {
    assert.ok(wire.signal.en.length > 0, `wire ${wire.n}`);
    assert.equal(wire.kind === 'stub', wire.b === null, `wire ${wire.n}: a stub has one end, a wire two`);
  }
});

test('3 and 4: every text has English and German, no banned word and no exclamation mark', () => {
  const found = texts(CATALOGUE);
  assert.ok(found.length >= 250, `the walk finds ${found.length} texts`);
  const problems = found.flatMap(({ value, path }) => textProblems(value).map((p) => `${path} ${p}`));
  assert.deepEqual(problems, []);
});

test('3: the release and the hash version are set', () => {
  assert.match(CATALOGUE.release, /^[0-9]+\.[0-9]+\.[0-9]+$/);
  assert.equal(CATALOGUE.hashVersion, 1);
});

test('5: every record names a source, and every source anchor is well-formed', () => {
  const { found } = sources(CATALOGUE);
  const problems = found.flatMap(({ value, path }) => sourceProblems(value, CATALOGUE.release).map((p) => `${path} ${p}`));
  assert.deepEqual(problems, []);

  const records = CATALOGUE.parts.length + CATALOGUE.parts.reduce((n, p) => n + p.pins.length, 0)
    + CATALOGUE.pads.length + CATALOGUE.wires.length + CATALOGUE.functions.length + CATALOGUE.rules.length
    + CATALOGUE.checks.length + CATALOGUE.stages.length + CATALOGUE.safety.length
    + CATALOGUE.notInRelease.length + CATALOGUE.pinDefaults.length + CATALOGUE.powerSources.length;
  assert.equal(found.length, records, 'a record has no source, or a source sits where no record is');
});

test('the rules report a catalogue that breaks them', () => {
  const part = CATALOGUE.parts[0];
  const wire = CATALOGUE.wires[0];
  const fn = CATALOGUE.functions[0];
  const rule = CATALOGUE.rules[0];
  assert.ok(part && wire && fn && rule);

  assert.ok(idProblems({ ...CATALOGUE, parts: [...CATALOGUE.parts, part] }).includes(`part ${part.id} occurs twice`));
  assert.deepEqual(idProblems({ ...CATALOGUE, wires: [...CATALOGUE.wires, wire] }), [`wire ${wire.n} occurs twice`]);
  assert.deepEqual(idProblems({ ...CATALOGUE, functions: [...CATALOGUE.functions, fn] }), [`function ${fn.id} occurs twice`]);

  assert.deepEqual(referenceProblems({ ...CATALOGUE, functions: [{ ...fn, needs: ['nopart'] }] }), ['function ' + fn.id + ' needs the part nopart']);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, rules: [{ ...rule, adds: 'nopart' }] }), [`rule ${rule.id} adds the part nopart`]);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, functions: [{ ...fn, needs: [], forces: { escPower: 'pd' } }] }), [`function ${fn.id} needs no part and forces an option`]);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, functions: [{ ...fn, needs: [] }] }), []);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, rules: [{ ...rule, when: { not: { has: 'nopart' } } }] }), [`rule ${rule.id}: the condition names the part nopart`]);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, rules: [{ ...rule, when: { opt: 'escPower', is: 'mains' } }] }), [`rule ${rule.id}: the condition names escPower = mains`]);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, wires: [{ ...wire, a: 'pad.99' }] }), [`wire ${wire.n}: "pad.99" is no pad`]);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, wires: [{ ...wire, a: 'panel.NOPIN' }] }), [`wire ${wire.n}: "panel.NOPIN" names no pin`]);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, wires: [{ ...wire, a: 'nopart.X' }] }), [`wire ${wire.n}: "nopart.X" names no part`]);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, wires: [{ ...wire, a: 'can.X' }] }), [`wire ${wire.n}: "can.X" is no CAN terminal`]);
  const first = CATALOGUE.notInRelease[0];
  assert.ok(first);
  assert.deepEqual(referenceProblems({ ...CATALOGUE, notInRelease: [{ ...first, id: part.id }] }), [`"${part.id}" is in no release and is a part or a function`]);

  assert.deepEqual(textProblems({ en: 'A wire', de: 'Ein Draht' }), []);
  assert.deepEqual(textProblems({ en: 'A wire' }), ['has the keys en', 'de is no string']);
  assert.deepEqual(textProblems({ en: 'A wire', de: '' }), ['de is empty']);
  assert.deepEqual(textProblems({ en: 'A wire', de: 'Ein Draht', fr: 'Un fil' }), ['has the keys de, en, fr']);
  assert.deepEqual(textProblems({ en: 'Solder it!', de: 'Löten' }), ['en holds an exclamation mark']);
  assert.deepEqual(textProblems({ en: 'Just solder', de: 'Einfach löten' }), ['en holds "just"', 'de holds "einfach"']);
  assert.deepEqual(textProblems({ en: 'Adjust the justification', de: 'Vereinfacht' }), []);
  for (const word of BANNED) assert.equal(textProblems({ en: `It is ${word}.`, de: 'x' }).length, 1, word);

  const release = CATALOGUE.release;
  assert.deepEqual(sourceProblems({ kind: 'repo', path: 'shared/pins.h', line: 12, tag: `v${release}` }, release), []);
  assert.deepEqual(sourceProblems({ kind: 'repo', path: '/etc/passwd', line: 12, tag: `v${release}` }, release), ['path "/etc/passwd" is no file path']);
  assert.deepEqual(sourceProblems({ kind: 'repo', path: 'a/../b.h', line: 12, tag: `v${release}` }, release), ['path "a/../b.h" is not relative to the repository']);
  assert.deepEqual(sourceProblems({ kind: 'repo', path: 'shared/pins.h', line: 0, tag: `v${release}` }, release), ['line 0 is no line number']);
  assert.deepEqual(sourceProblems({ kind: 'repo', path: 'shared/pins.h', line: 1.5, tag: `v${release}` }, release), ['line 1.5 is no line number']);
  assert.deepEqual(sourceProblems({ kind: 'repo', path: 'shared/pins.h', line: 12, tag: 'main' }, release), [`tag main is not v${release}`]);
  assert.deepEqual(sourceProblems({ kind: 'guide', section: 'Stage 1' }, release), ['section "Stage 1" is no section name']);
  assert.deepEqual(sourceProblems({ kind: 'photo', file: '5' }, release), ['file "5" is no photo number']);
  assert.deepEqual(sourceProblems({ kind: 'owner', date: '2026-02-30' }, release), ['date "2026-02-30" is no calendar date']);
  assert.deepEqual(sourceProblems({ kind: 'owner', date: '11.10.2026' }, release), ['date "11.10.2026" is no calendar date']);
  assert.deepEqual(sourceProblems({ kind: 'page', line: 3 }, release), ['has the keys kind,line']);
  assert.deepEqual(sourceProblems({ kind: 'forum' }, release), ['kind "forum" is no source kind']);
  assert.deepEqual(sourceProblems(null, release), ['is no object']);
});
