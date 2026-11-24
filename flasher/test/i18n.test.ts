import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ALL_KEYS, browserLang, hasKey, nextKey, strings, t } from '../src/i18n.ts';
import { ADV_KEYS, advStrings, tAdv } from '../src/i18n_adv.ts';
import { BOARDS } from '../src/boards.ts';
import { INST_KEYS, instStrings, tInst } from '../src/installed_read.ts';

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

test('every string of the board check has an English and a German text with the same placeholders', () => {
  for (const key of INST_KEYS) {
    const [en, de] = instStrings(key);
    assert.ok(en.length > 0 && de.length > 0, key);
    assert.deepEqual(vars(en), vars(de), key);
    assert.doesNotMatch(en + de, /!/, `${key} carries an exclamation mark`);
  }
  for (const b of BOARDS) if (b.chip !== 'rp2350') assert.ok(INST_KEYS.includes(`n_${b.id}` as never), b.id);
  assert.match(tInst('de', 'u_unread', { x: 'timeout' }), /timeout/);
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

test('Done: the Dial names its kept binding after a write that kept the NVS, and asks to bind after one that did not', () => {
  assert.equal(nextKey('dial', false), 'nx_dial');
  assert.equal(nextKey('dial', true), 'nk_dial');
  assert.match(strings('nx_dial')[0], /shows NOT BOUND/);
  assert.match(strings('nk_dial')[0], /keeps its settings and its binding/);
  assert.match(strings('nk_dial')[1], /behält seine Einstellungen und seine Bindung/);
  // A node is bound by its address on the panel: the same line either way, naming both cases.
  for (const id of ['node-display', 'node-pwr', 'node-bal']) {
    assert.equal(nextKey(id, true), `nx_${id}`, id);
    assert.equal(nextKey(id, false), `nx_${id}`, id);
    assert.match(strings(`nx_${id}` as never)[0], /stays bound.*never bound: bind it/, id);
  }
  for (const id of ['panel', 'iomcu', 'iomcu-rbmod', 'c6']) {
    assert.equal(nextKey(id, true), `nx_${id}`, id);
    assert.equal(nextKey(id, false), `nx_${id}`, id);
  }
  assert.equal(nextKey('core2350b', true), null);
});

test('German device names follow the panel: Auswuchtknoten, Leistungsknoten, C6-Funk', () => {
  for (const key of ALL_KEYS) assert.doesNotMatch(strings(key)[1], /Balance-Knoten|Funkchip/, key);
  assert.equal(t('de', 'b_node-bal'), 'Auswuchtknoten');
  assert.equal(t('de', 'b_node-pwr'), 'Leistungsknoten');
});
