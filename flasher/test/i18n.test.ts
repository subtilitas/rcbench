import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ALL_KEYS, browserLang, hasKey, strings, t } from '../src/i18n.ts';
import { ADV_KEYS, advStrings, tAdv } from '../src/i18n_adv.ts';
import { BOARDS } from '../src/boards.ts';

const vars = (s: string): string[] => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]!).sort();

test('every string has an English and a German text with the same placeholders', () => {
  for (const key of ALL_KEYS) {
    const [en, de] = strings(key);
    assert.ok(en.length > 0 && de.length > 0, key);
    assert.deepEqual(vars(en), vars(de), key);
    assert.doesNotMatch(en + de, /!/, `${key} carries an exclamation mark`);
  }
});

test('every Advanced string has an English and a German text with the same placeholders', () => {
  for (const key of ADV_KEYS) {
    const [en, de] = advStrings(key);
    assert.ok(en.length > 0 && de.length > 0, key);
    assert.deepEqual(vars(en), vars(de), key);
    assert.doesNotMatch(en + de, /!/, `${key} carries an exclamation mark`);
  }
  assert.match(tAdv('de', 'sigMismatch', { file: 'a.sig' }), /a\.sig/);
});

test('every board has a name in both languages', () => {
  for (const b of BOARDS) assert.ok(hasKey(`b_${b.id}`), b.id);
});

test('the start language follows the browser', () => {
  assert.equal(browserLang(['de-DE', 'en']), 'de');
  assert.equal(browserLang(['en-GB', 'de']), 'en');
  assert.equal(browserLang(['fr']), 'en');
  assert.equal(browserLang([]), 'en');
});

test('placeholders are filled, unknown ones stay', () => {
  assert.equal(t('en', 'noImage', { tag: 'v0.23.15' }), 'Release v0.23.15 carries no image for this board.');
  assert.match(t('de', 'connected', {}), /\{x\}/);
});
