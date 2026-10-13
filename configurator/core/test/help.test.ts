// The help text of the functions with a tick: one entry per such function,
// none for another id, each under the text rules of the data.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { CATALOGUE } from '../catalogue.ts';
import { alwaysOn } from '../groups.ts';
import { FUNCTION_HELP } from '../help.ts';
import { checkAll, checkHelp } from '../tools/check.ts';

const TICKED = CATALOGUE.functions.filter((fn) => !alwaysOn(CATALOGUE, fn)).map((fn) => fn.id);

test('the help text covers every function with a tick and no other', () => {
  assert.equal(TICKED.length, 12);
  assert.deepEqual(Object.keys(FUNCTION_HELP).sort(), [...TICKED].sort());
  assert.deepEqual(checkHelp(CATALOGUE), []);
  assert.ok(Object.isFrozen(FUNCTION_HELP));
});

test('the check finds a missing entry, an entry of a function without a tick and a text that breaks a rule', () => {
  const { servo: _servo, ...missing } = FUNCTION_HELP;
  assert.deepEqual(checkHelp(CATALOGUE, missing).map((f) => f.id), ['help.servo']);
  assert.deepEqual(checkHelp(CATALOGUE, { ...FUNCTION_HELP, link: { en: 'Links.', de: 'Verbindet.' } }).map((f) => f.id), ['help.link']);
  assert.deepEqual(checkHelp(CATALOGUE, { ...FUNCTION_HELP, nosuch: { en: 'x', de: 'x' } }).map((f) => f.id), ['help.nosuch']);
  const bad = checkHelp(CATALOGUE, {
    ...FUNCTION_HELP,
    servo: { en: 'Drives a servo, easy.', de: '' },
    esc: { en: 'Drives an ESC!', de: 'Steuert einen ESC.' },
  });
  assert.deepEqual(bad.map((f) => f.id), ['help.servo.en', 'help.servo.de', 'help.esc.en']);
  assert.ok(bad.every((f) => f.level === 'error'));
});

test('the check of the data file runs the check of the help text', () => {
  const report = checkAll(CATALOGUE, null);
  assert.deepEqual(report.findings.filter((f) => f.id.startsWith('help')), []);
});
