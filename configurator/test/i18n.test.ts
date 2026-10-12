import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { test } from 'node:test';
import { LEGEND, RELEASE, STRINGS, browserLang, esc, safeBold, t } from '../src/i18n.ts';
import type { Key } from '../src/i18n.ts';

const keys = Object.keys(STRINGS) as Key[];
const root = new URL('..', import.meta.url);
const read = (path: string): string => readFileSync(new URL(path, root), 'utf8');

function sources(dir: string): string[] {
  const out: string[] = [];
  for (const entry of readdirSync(new URL(dir, root), { withFileTypes: true })) {
    if (entry.isDirectory()) out.push(...sources(`${dir}${entry.name}/`));
    else if (entry.name.endsWith('.ts')) out.push(`${dir}${entry.name}`);
  }
  return out;
}

test('every key has an English and a German text', () => {
  assert.ok(keys.length > 60);
  for (const key of keys) {
    const pair = STRINGS[key];
    assert.equal(pair.length, 2, key);
    assert.ok(pair[0].trim().length > 0, `${key} has no English text`);
    assert.ok(pair[1].trim().length > 0, `${key} has no German text`);
  }
});

test('both languages of a key carry the same placeholders', () => {
  const holes = (s: string): string[] => (s.match(/\{[a-z]+\}/g) ?? []).sort();
  for (const key of keys) assert.deepEqual(holes(STRINGS[key][1]), holes(STRINGS[key][0]), key);
});

test('t fills placeholders and picks the language', () => {
  assert.equal(t('en', 'sum', { p: 7, w: 9, f: 2 }), '7 parts, 9 wires, 2 functions.');
  assert.equal(t('de', 'sum', { p: 7, w: 9, f: 2 }), '7 Teile, 9 Drähte, 2 Funktionen.');
});

test('header, notice and print head name the release of package.json', () => {
  const pkg = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8')) as { version: string };
  assert.equal(RELEASE, pkg.version);
  for (const lang of ['en', 'de'] as const) {
    for (const key of ['rel', 'foot', 'printHead'] as const) {
      assert.ok(t(lang, key).includes(pkg.version), `${key} (${lang}) names ${pkg.version}`);
    }
  }
});

test('every key is used by the page', () => {
  const text = sources('src/').filter((f) => f !== 'src/i18n.ts').map(read).join('\n');
  const fixed = new Set<Key>([...LEGEND.map((l) => l[1]), 'noscript', 'langGroup']);
  for (const key of keys) {
    if (fixed.has(key)) continue;
    assert.ok(text.includes(`'${key}'`), `${key} is used nowhere`);
  }
});

test('the static texts of index.html equal the string table', () => {
  const html = read('index.html');
  assert.ok(html.includes(`<title>${STRINGS.title[0]}</title>`));
  assert.ok(html.includes(`<h1 id="title">${STRINGS.title[0]}</h1>`));
  assert.ok(html.includes(`lang="en">${STRINGS.noscript[0]}<`));
  assert.ok(html.includes(`lang="de">${STRINGS.noscript[1]}<`));
  assert.ok(html.includes(`aria-label="${STRINGS.langGroup[0]}"`));
  assert.equal(STRINGS.langGroup[0], STRINGS.langGroup[1]);
});

test('the legend has one entry and one line style per signal class', () => {
  const classes = LEGEND.map((l) => l[0]);
  assert.equal(new Set(classes).size, 12);
  const css = read('src/style.css');
  for (const cls of classes) assert.ok(css.includes(`.c-${cls} {`), `no style for ${cls}`);
});

test('chrome strings hold the writing rules', () => {
  const banned = /!|\b(powerful|seamless|robust|blazing|comprehensive|simply|just|easy|nahtlos|mächtig)\b/i;
  for (const key of keys) {
    for (const s of STRINGS[key]) assert.ok(!banned.test(s), `${key}: ${s}`);
    assert.ok(!/\d\.\d+ ?(V|A|mA|Ω|kΩ|mm|cm|m|ms|s|px)\b/.test(STRINGS[key][1]), `${key}: German value has a decimal point`);
  }
});

test('the start language follows the browser', () => {
  assert.equal(browserLang('de'), 'de');
  assert.equal(browserLang('de-AT'), 'de');
  assert.equal(browserLang('DE-ch'), 'de');
  assert.equal(browserLang('en-GB'), 'en');
  assert.equal(browserLang('fr'), 'en');
  assert.equal(browserLang(''), 'en');
  assert.equal(browserLang(undefined), 'en');
});

test('data text reaches the page escaped', () => {
  assert.equal(esc(`<a href="x">&'`), '&lt;a href=&quot;x&quot;&gt;&amp;&#39;');
  assert.equal(safeBold('<b>STOP</b> <i>x</i> <b onclick=1>'), '<b>STOP</b> &lt;i&gt;x&lt;/i&gt; &lt;b onclick=1&gt;');
});
