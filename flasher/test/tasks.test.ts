import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BOARDS, boardById } from '../src/boards.ts';
import { TASKS, boardUsable, boardsFor, keepFor, sourceFor, sourcesFor, taskLinks, tasksFor, tileState } from '../src/tasks.ts';

const ids = (xs: readonly { id: string }[]): string[] => xs.map((x) => x.id);
const board = (id: string) => boardById(id)!;

test('simple mode lists four tasks with the C6 radio repair last; Advanced adds the own file after it', () => {
  assert.deepEqual(ids(tasksFor(false)), ['panel', 'iomcu', 'wireless', 'c6']);
  assert.deepEqual(ids(tasksFor(true)), ['panel', 'iomcu', 'wireless', 'c6', 'file']);
  assert.deepEqual(TASKS.filter((x) => x.prepare).map((x) => x.id), ['c6']);
});

test('every board of BOARDS that a release carries is in exactly one simple task', () => {
  for (const b of BOARDS) {
    const holders = tasksFor(false).filter((x) => boardsFor(x.id, false).some((y) => y.id === b.id));
    assert.equal(holders.length, b.planned ? 0 : 1, b.id);
  }
});

test('every board a task names is in BOARDS, and every board of BOARDS is in a task', () => {
  for (const x of TASKS) for (const id of x.boards) assert.ok(boardById(id), `${x.id}: ${id}`);
  const named = new Set(TASKS.filter((x) => x.id !== 'file').flatMap((x) => x.boards));
  for (const b of BOARDS) assert.ok(named.has(b.id), `${b.id} is in no task of the first screen`);
});

test('the boards of each task, simple and Advanced', () => {
  assert.deepEqual(ids(boardsFor('panel', false)), ['panel']);
  assert.deepEqual(ids(boardsFor('iomcu', false)), ['iomcu', 'iomcu-rbmod']);
  assert.deepEqual(ids(boardsFor('wireless', false)), ['node-display', 'dial', 'node-pwr', 'node-bal']);
  assert.deepEqual(ids(boardsFor('c6', false)), ['c6']);
  assert.deepEqual(ids(boardsFor('file', false)), []);
  assert.deepEqual(ids(boardsFor('iomcu', true)), ['iomcu', 'iomcu-rbmod', 'core2350b']);
  assert.deepEqual(ids(boardsFor('file', true)), ids(BOARDS));
  assert.equal(boardsFor('file', true).length, 9);
});

test('the Core2350B shows in Advanced only and takes a local file only', () => {
  for (const x of TASKS) assert.ok(!ids(boardsFor(x.id, false)).includes('core2350b'), x.id);
  assert.ok(ids(boardsFor('iomcu', true)).includes('core2350b'));
  assert.ok(ids(boardsFor('file', true)).includes('core2350b'));
  assert.ok(!ids(boardsFor('wireless', true)).includes('core2350b'));
  assert.deepEqual(sourcesFor(board('core2350b'), false), []);
  assert.deepEqual(sourcesFor(board('core2350b'), true), ['file']);
  assert.equal(sourceFor(board('core2350b'), true, 'release'), 'file');
  assert.equal(sourceFor(board('core2350b'), false, 'release'), null);
});

test('simple mode writes release images only with "Keep settings" on; Advanced offers both and the visitor\'s choice', () => {
  for (const b of BOARDS.filter((x) => !x.planned)) {
    assert.deepEqual(sourcesFor(b, false), ['release'], b.id);
    assert.equal(sourceFor(b, false, 'file'), 'release', b.id);
    assert.deepEqual(sourcesFor(b, true), ['release', 'file'], b.id);
    assert.equal(sourceFor(b, true, 'file'), 'file', b.id);
  }
  assert.equal(keepFor(false, false), true);
  assert.equal(keepFor(false, true), true);
  assert.equal(keepFor(true, false), false);
  assert.equal(keepFor(true, true), true);
});

test('a task needs the links of its boards; a tile greys out only when the browser lacks all of them', () => {
  const all = { serial: true, usb: true };
  const noUsb = { serial: true, usb: false };
  const noSerial = { serial: false, usb: true };
  const none = { serial: false, usb: false };
  assert.deepEqual(taskLinks('panel', false), ['serial']);
  assert.deepEqual(taskLinks('iomcu', false), ['picoboot']);
  assert.deepEqual(taskLinks('wireless', false), ['serial']);
  assert.deepEqual(taskLinks('c6', false), ['serial']);
  assert.deepEqual(taskLinks('file', true), ['serial', 'picoboot']);
  for (const x of tasksFor(true)) assert.deepEqual(tileState(x.id, true, all), { usable: true }, x.id);
  assert.deepEqual(tileState('iomcu', false, noUsb), { usable: false, missing: ['picoboot'] });
  assert.deepEqual(tileState('panel', false, noUsb), { usable: true });
  assert.deepEqual(tileState('panel', false, noSerial), { usable: false, missing: ['serial'] });
  assert.deepEqual(tileState('file', true, noUsb), { usable: true });
  assert.deepEqual(tileState('file', true, none), { usable: false, missing: ['serial', 'picoboot'] });
  for (const x of tasksFor(false)) assert.equal(tileState(x.id, false, none).usable, false, x.id);
  assert.equal(boardUsable(board('iomcu'), noUsb), false);
  assert.equal(boardUsable(board('dial'), noUsb), true);
  assert.equal(boardUsable(board('dial'), noSerial), false);
});
