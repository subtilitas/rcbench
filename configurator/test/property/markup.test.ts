// Properties of the markup the page renders, over the selection space.
//
// Rendered: every subset of the functions and every subset of the parts you
// own, in both languages. The option combination changes from subset to
// subset, so each of the 8 occurs 256 times per space and language.
//
//  1. The markup is well-formed: every tag closes in order.
//  2. Every id occurs once, and every reference to an id resolves.
//  3. Every form control has a label, every image a text and a size, every
//     table header a scope.
//  4. No link and no image leaves the page folder.
//  5. The text holds no placeholder, no "undefined", no banned word and no
//     exclamation mark, and data text reaches the page escaped.
//  6. The summary states the count of bench parts, wires and functions of the
//     setup. A test object is no bench part and is not counted.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import type { Lang, Selection } from '../../contract.ts';
import { OPTION_SETS, enumerate, spaceOf } from '../../core/test/property/space.ts';
import { bannedIn } from '../../core/test/property/words.ts';
import { CATALOGUE, resolve } from '../../src/core.ts';
import { STRINGS } from '../../src/i18n.ts';
import { optionsOf } from '../../src/names.ts';
import { chooser } from '../../src/view/chooser.ts';
import type { View } from '../../src/view/chooser.ts';
import { NOTICE_URL, result, summary } from '../../src/view/result.ts';

const SPACE = spaceOf(CATALOGUE);
const LANGS: Lang[] = ['en', 'de'];
const URL_OF_TEST = 'https://example.invalid/rcbench/';
const SHELL = readFileSync(new URL('../../index.html', import.meta.url), 'utf8');

/** The ids of the static shell: the sections the rendered markup refers to. */
const SHELL_IDS = [...SHELL.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1] as string);

const VOID = new Set(['input', 'img', 'br', 'meta', 'link', 'hr']);

type Tag = { name: string; attrs: Map<string, string>; text: string; label?: Tag };
type Doc = { tags: Tag[]; text: string; problems: string[] };

/** The tags and the text of a fragment, with what is malformed in it. */
export function read(html: string): Doc {
  const tags: Tag[] = [];
  const problems: string[] = [];
  const open: Tag[] = [];
  let text = '';
  let at = 0;
  const token = /<(\/?)([a-zA-Z][a-zA-Z0-9]*)((?:\s+[a-zA-Z_:][-a-zA-Z0-9_:.]*(?:="[^"]*")?)*)\s*(\/?)>/y;
  while (at < html.length) {
    const lt = html.indexOf('<', at);
    const chunk = html.slice(at, lt < 0 ? html.length : lt);
    if (chunk.includes('>')) problems.push(`a bare ">" in "${chunk.slice(0, 40)}"`);
    text += chunk;
    for (const tag of open) tag.text += chunk;
    if (lt < 0) break;
    token.lastIndex = lt;
    const m = token.exec(html);
    if (!m) {
      problems.push(`no tag at "${html.slice(lt, lt + 40)}"`);
      at = lt + 1;
      continue;
    }
    at = token.lastIndex;
    const name = (m[2] as string);
    if (m[1] === '/') {
      const last = open.pop();
      if (!last || last.name !== name) problems.push(`</${name}> closes <${last ? last.name : 'nothing'}>`);
      text += ' ';
      continue;
    }
    const attrs = new Map<string, string>();
    for (const a of (m[3] as string).matchAll(/\s+([a-zA-Z_:][-a-zA-Z0-9_:.]*)(?:="([^"]*)")?/g)) {
      const key = a[1] as string;
      if (attrs.has(key)) problems.push(`<${name}> has ${key} twice`);
      attrs.set(key, a[2] ?? '');
    }
    const tag: Tag = { name, attrs, text: '' };
    const label = [...open].reverse().find((t) => t.name === 'label');
    if (label) tag.label = label;
    tags.push(tag);
    if (m[4] !== '/' && !VOID.has(name)) open.push(tag);
  }
  for (const tag of open) problems.push(`<${tag.name}> is not closed`);
  return { tags, text, problems };
}

function unescape(text: string): string {
  return text.replace(/&(lt|gt|quot|#39|amp);/g, (_, x: string) => ({ lt: '<', gt: '>', quot: '"', '#39': "'", amp: '&' })[x] as string);
}

function view(selection: Selection, lang: Lang): View {
  const setup = resolve(CATALOGUE, selection);
  return { lang, catalogue: CATALOGUE, setup, options: optionsOf(setup), selection };
}

/** What the rules find in the page of one view. Empty for a sound page. */
/** The radio groups of a document that have another count of checked radios than 1. */
function radioProblems(doc: Doc): string[] {
  const groups = new Map<string, number>();
  for (const tag of doc.tags) {
    if (tag.name !== 'input' || tag.attrs.get('type') !== 'radio') continue;
    const name = tag.attrs.get('name') ?? '';
    groups.set(name, (groups.get(name) ?? 0) + (tag.attrs.has('checked') ? 1 : 0));
  }
  return [...groups].filter(([, count]) => count !== 1).map(([name, count]) => `${count} radios of ${name} are checked`);
}

function problemsOf(v: View, ownOpen: boolean): string[] {
  const html = chooser(v, ownOpen) + result(v, URL_OF_TEST, []);
  const doc = read(html);
  const out = [...doc.problems];

  // 2
  const ids = doc.tags.flatMap((t) => (t.attrs.has('id') ? [t.attrs.get('id') as string] : []));
  const seen = new Set<string>();
  for (const id of ids) {
    if (id === '' || /\s/.test(id)) out.push(`the id "${id}" is empty or holds a space`);
    if (seen.has(id)) out.push(`the id ${id} occurs twice`);
    seen.add(id);
  }
  const all = new Set([...seen, ...SHELL_IDS]);
  for (const tag of doc.tags) {
    for (const key of ['for', 'aria-labelledby', 'aria-describedby', 'aria-controls']) {
      for (const id of (tag.attrs.get(key) ?? '').split(/\s+/).filter((x) => x !== '')) {
        if (!all.has(id)) out.push(`<${tag.name} ${key}="${id}"> names no element`);
      }
    }
    const href = tag.attrs.get('href');
    if (href?.startsWith('#') && !all.has(href.slice(1))) out.push(`the link ${href} names no element`);
  }

  // 3
  const labelled = new Set(doc.tags.filter((t) => t.name === 'label' && t.attrs.has('for')).map((t) => t.attrs.get('for')));
  for (const tag of doc.tags) {
    if (tag.name === 'input') {
      const id = tag.attrs.get('id');
      if (!id) out.push('an input has no id');
      // An input inside a label is labelled by the text of that label.
      if (!(tag.label && tag.label.text.trim() !== '') && !labelled.has(id) && !tag.attrs.get('aria-label')) out.push(`the input ${String(id)} has no label`);
      if (tag.attrs.get('type') === 'radio' && !tag.attrs.get('name')) out.push(`the radio ${String(id)} has no group`);
    }
    if (tag.name === 'button') {
      if (tag.attrs.get('type') !== 'button') out.push(`the button ${String(tag.attrs.get('id'))} has no type "button"`);
      if (tag.text.trim() === '' && !tag.attrs.get('aria-label')) out.push(`the button ${String(tag.attrs.get('id'))} has no name`);
    }
    if (tag.name === 'a' && tag.text.trim() === '' && !tag.attrs.get('aria-label')) {
      // A link around an image is named by the text of the image.
      const img = doc.tags[doc.tags.indexOf(tag) + 1];
      if (!img || img.name !== 'img' || !img.attrs.get('alt')) out.push(`the link ${String(tag.attrs.get('href'))} has no name`);
    }
    if (tag.name === 'img') {
      if (!tag.attrs.get('alt')) out.push(`the image ${String(tag.attrs.get('src'))} has no text`);
      for (const key of ['width', 'height']) {
        if (!/^[1-9][0-9]*$/.test(tag.attrs.get(key) ?? '')) out.push(`the image ${String(tag.attrs.get('src'))} has no ${key}`);
      }
    }
    if (tag.name === 'th' && !tag.attrs.get('scope')) out.push(`the table header "${tag.text}" has no scope`);
    if (tag.name === 'table' && !doc.tags.some((t) => t.name === 'caption' && t.text.trim() !== '')) out.push('the table has no caption');
    if (tag.attrs.get('role') === 'radiogroup' && !tag.attrs.get('aria-labelledby') && !tag.attrs.get('aria-label')) out.push('a radio group has no name');
    const tabindex = tag.attrs.get('tabindex');
    if (tabindex !== undefined && tabindex !== '0' && tabindex !== '-1') out.push(`<${tag.name}> has tabindex ${tabindex}`);
    for (const key of tag.attrs.keys()) {
      if (/^on/i.test(key) || key === 'style') out.push(`<${tag.name}> has the attribute ${key}`);
    }
  }
  out.push(...radioProblems(doc));
  const svg = doc.tags.find((t) => t.name === 'svg' && (t.attrs.get('class') ?? '').split(' ').includes('drawing'));
  if (!svg) out.push('the page has no drawing');
  else if (!svg.attrs.get('aria-label') && !svg.attrs.get('aria-labelledby') && svg.attrs.get('aria-hidden') !== 'true') out.push('the drawing has no name');

  // 4
  for (const tag of doc.tags) {
    for (const key of ['href', 'src']) {
      const value = tag.attrs.get(key);
      if (value === undefined || value.startsWith('#')) continue;
      // The link to the trademark notice leaves the page when the visitor follows it. The page loads nothing from it.
      if (key === 'href' && tag.name === 'a' && value === NOTICE_URL && tag.attrs.get('rel') === 'noopener noreferrer') continue;
      // A picture of a part, or a part drawing of the wiring picture in the language of the page.
      if (!/^pictures\/[a-z0-9-]+\.(webp|svg)$/.test(value) && !/^drawings\/(de\/)?[a-z0-9-]+\.svg$/.test(value)) out.push(`<${tag.name} ${key}="${value}"> leaves the page folder`);
    }
    if (tag.attrs.has('srcset') || tag.attrs.has('action') || tag.attrs.has('formaction')) out.push(`<${tag.name}> loads from an attribute outside the rule`);
    if (['script', 'iframe', 'object', 'embed', 'link', 'style', 'form', 'base', 'meta'].includes(tag.name)) out.push(`the page renders <${tag.name}>`);
  }

  // 5
  const visible = unescape(doc.text.split(URL_OF_TEST).join(' '));
  if (/\{[a-z]+\}/.test(visible)) out.push(`a placeholder is left: ${String(/\{[a-z]+\}/.exec(visible))}`);
  for (const word of ['undefined', 'NaN', '[object', 'null']) {
    if (new RegExp(`(?<![A-Za-z])${word.replace('[', '\\[')}(?![A-Za-z])`).test(visible)) out.push(`the text holds "${word}"`);
  }
  if (visible.includes('!')) out.push('the text holds an exclamation mark');
  for (const word of bannedIn(visible)) out.push(`the text holds "${word}"`);
  for (const tag of doc.tags) {
    for (const [key, value] of tag.attrs) {
      if (key === 'd' || key === 'viewBox') continue;
      const plain = unescape(value);
      if (/(?<![A-Za-z])(undefined|NaN)(?![A-Za-z])|\[object/.test(plain)) out.push(`<${tag.name} ${key}="${value}">`);
      if (key === 'alt' || key === 'aria-label') {
        if (plain.includes('!')) out.push(`${key} holds an exclamation mark`);
        for (const word of bannedIn(plain)) out.push(`${key} holds "${word}"`);
      }
    }
  }
  if (/&(?!(lt|gt|quot|#39|amp);)/.test(html)) out.push('an ampersand is not escaped');

  // 6
  const f = v.setup.functions.filter((x) => x.enabled).length;
  const expected = (f === 1 ? STRINGS.sum1 : STRINGS.sum)[v.lang === 'en' ? 0 : 1]
    .replace('{p}', String(v.setup.parts.filter((p) => p.part.testObject !== true).length)).replace('{w}', String(v.setup.wires.length)).replace('{f}', String(f));
  if (summary(v) !== expected) out.push(`the summary is "${summary(v)}", the setup gives "${expected}"`);
  if (!visible.includes(expected)) out.push(`the page does not show "${expected}"`);
  const rows = doc.tags.filter((t) => t.name === 'tr').length - 1;
  if (v.setup.wires.length > 0 && rows !== v.setup.wires.length) out.push(`the table has ${rows} rows for ${v.setup.wires.length} wires`);
  return out;
}

function walkSpace(ids: string[], key: 'functions' | 'owned'): number {
  let renders = 0;
  for (const c of enumerate(ids, key)) {
    if (c.option !== c.mask % OPTION_SETS.length) continue;
    for (const lang of LANGS) {
      assert.deepEqual(problemsOf(view(c.selection, lang), (c.mask & 1) === 1), [], `${lang}, ${c.where}`);
      renders += 1;
    }
  }
  return renders;
}

test('the reader of the test finds what is malformed', () => {
  assert.deepEqual(read('<p>a <b>b</b></p><br><input id="x" checked>').problems, []);
  assert.deepEqual(read('<p>a <b>b</p></b>').problems, ['</p> closes <b>', '</b> closes <p>']);
  assert.deepEqual(read('<p>a').problems, ['<p> is not closed']);
  assert.deepEqual(read('<p id="a" id="b"></p>').problems, ['<p> has id twice']);
  assert.deepEqual(read('<p>a < b</p>').problems, ['no tag at "< b</p>"']);
  assert.deepEqual(read('<p>a > b</p>').problems, ['a bare ">" in "a > b"']);
  assert.equal(read('<p>a <b>b</b> c</p>').tags[0]?.text, 'a b c');
  assert.equal(read('<svg><path d="M0 0"/><text>x</text></svg>').problems.length, 0);
});

test('1 to 6: the page of every subset of the functions', () => {
  assert.equal(walkSpace(SPACE.functions, 'functions'), 2 ** SPACE.functions.length * LANGS.length);
});

test('1 to 6: the page of every subset of the parts you own', () => {
  assert.equal(walkSpace(SPACE.selectable, 'owned'), 2 ** SPACE.selectable.length * LANGS.length);
});

test('1 to 6: the page of the full selection under every option combination, with a notice of each kind', () => {
  for (const options of OPTION_SETS) {
    for (const lang of LANGS) {
      const v = view({ functions: SPACE.functions, owned: SPACE.selectable, options: { ...options } }, lang);
      assert.deepEqual(problemsOf(v, true), [], `${lang}, ${JSON.stringify(options)}`);
      const html = result(v, URL_OF_TEST, [{ kind: 'err', text: 'a < b & "c"' }, { kind: 'note', text: "<img src=x onerror='1'>" }]);
      assert.deepEqual(read(html).problems, [], lang);
      assert.ok(!read(html).tags.some((t) => t.name === 'img' && t.attrs.get('src') === 'x'), 'a notice reaches the page as markup');
    }
  }
});

test('5: the address of the page reaches the page escaped', () => {
  const hostile = 'https://example.invalid/?a=1&b="><script>alert(1)</script><p x=\'';
  for (const lang of LANGS) {
    const v = view({ functions: [], owned: [], options: { ...(OPTION_SETS[0] as Selection['options']) } }, lang);
    const doc = read(result(v, hostile, []));
    assert.deepEqual(doc.problems, []);
    assert.ok(!doc.tags.some((t) => t.name === 'script'));
    assert.ok(unescape(doc.text).includes(hostile));
  }
});

test('the rules report a radio group with no checked radio and one with 2', () => {
  const radio = (name: string, checked: boolean): string => `<input type="radio" name="${name}"${checked ? ' checked' : ''}>`;
  assert.deepEqual(radioProblems(read(radio('a', true) + radio('a', false) + radio('b', false) + radio('b', true))), []);
  assert.deepEqual(radioProblems(read(radio('a', false) + radio('a', false) + radio('b', true))), ['0 radios of a are checked']);
  assert.deepEqual(radioProblems(read(radio('a', true) + radio('a', true))), ['2 radios of a are checked']);
  assert.deepEqual(radioProblems(read('<input type="radio">')), ['0 radios of  are checked']);
});

test('the rules report a page that breaks them', () => {
  const v = view({ functions: ['servo'], owned: [], options: { ...(OPTION_SETS[0] as Selection['options']) } }, 'en');
  assert.deepEqual(problemsOf(v, false), []);
  // A setup with a text that breaks the writing rules, and one with a missing name.
  const broken = (change: (part: View['setup']['parts'][number]['part']) => View['setup']['parts'][number]['part']): View => ({
    ...v,
    setup: { ...v.setup, parts: v.setup.parts.map((p) => (p.part.id === 'servo' ? { ...p, part: change(p.part) } : p)) },
  });
  assert.deepEqual(problemsOf(broken((p) => ({ ...p, name: { en: 'Just a servo!', de: 'x' } })), false),
    ['the text holds an exclamation mark', 'the text holds "just"']);
  assert.deepEqual(problemsOf(broken((p) => ({ ...p, name: { en: undefined as unknown as string, de: 'x' } })), false),
    ['the text holds "undefined"']);
  assert.deepEqual(problemsOf(broken((p) => ({ ...p, name: { en: 'Fill {n} in', de: 'x' } })), false),
    ['a placeholder is left: {n}']);
});
