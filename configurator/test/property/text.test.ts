// Writing rules on every text the page holds outside the catalogue: the
// chrome strings and the static shell.
//
//  1. Every chrome string has an English and a German text, trimmed.
//  2. No text holds a banned word or an exclamation mark.
//  3. The shell states both languages where JavaScript is off.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { BANNED, bannedIn } from '../../core/test/property/words.ts';
import { STRINGS } from '../../src/i18n.ts';
import type { Key } from '../../src/i18n.ts';

const SHELL = readFileSync(new URL('../../index.html', import.meta.url), 'utf8');
const keys = Object.keys(STRINGS) as Key[];

/** Keys whose text ends with a space, because a list follows it. */
const LEADS = (key: Key): boolean => /: $/.test(STRINGS[key][0]) && /: $/.test(STRINGS[key][1]);

test('1 and 2: every chrome string in both languages', () => {
  assert.ok(keys.length > 60);
  for (const key of keys) {
    const pair = STRINGS[key];
    assert.equal(pair.length, 2, key);
    for (const text of pair) {
      assert.equal(typeof text, 'string', key);
      assert.ok(text.trim().length > 0, `${key} is empty`);
      assert.equal(text, LEADS(key) ? `${text.trim()} ` : text.trim(), `${key} starts or ends with a space`);
      assert.ok(!text.includes('!'), `${key} holds an exclamation mark`);
      assert.deepEqual(bannedIn(text), [], key);
      assert.ok(!/\s{2,}/.test(text), `${key} holds two spaces in a row`);
    }
  }
});

test('2 and 3: the text of the shell', () => {
  const body = SHELL.slice(SHELL.indexOf('<body>'));
  const text = body.replace(/<[^>]+>/g, '\n');
  assert.ok(!text.includes('!'), 'the shell holds an exclamation mark');
  assert.deepEqual(bannedIn(text), []);
  const noscript = /<noscript>(.*?)<\/noscript>/s.exec(SHELL)?.[1] ?? '';
  assert.match(noscript, /<p[^>]* lang="en">[^<]+<\/p>/);
  assert.match(noscript, /<p[^>]* lang="de">[^<]+<\/p>/);
  assert.match(SHELL, /<html lang="en">/);
});

test('the shell loads nothing from another origin', () => {
  for (const m of SHELL.matchAll(/\s(?:src|href|action|srcset)="([^"]*)"/g)) {
    const value = m[1] as string;
    assert.ok(value.startsWith('/src/') || value === 'data:,', `the shell loads ${value}`);
  }
  assert.ok(!/https?:\/\//.test(SHELL), 'the shell names an address');
  assert.match(SHELL, /<meta name="referrer" content="no-referrer">/);
});

test('the word list finds whole words in any letter case, and nothing inside a word', () => {
  for (const word of BANNED) {
    assert.deepEqual(bannedIn(`It is ${word}.`), [word]);
    assert.deepEqual(bannedIn(word.toUpperCase()), [word]);
    assert.deepEqual(bannedIn(`x${word}`), []);
    assert.deepEqual(bannedIn(`${word}x9`), []);
  }
  assert.deepEqual(bannedIn('Adjust the justification of the Vereinfachung'), []);
  assert.deepEqual(bannedIn('just, simply: easy'), ['simply', 'just', 'easy']);
});
