import { test } from 'node:test';
import assert from 'node:assert/strict';
import { boardsFor, sourceFor, tasksFor } from '../src/tasks.ts';
import { BOARDS } from '../src/boards.ts';
import type { TaskId } from '../src/tasks.ts';
import {
  back, cancel, canEnter, sigStateOf, chooseBoard, chooseTask, checkGate, clamp, connectAgain, fromHash, next, setAdvanced,
  start, startOver, stepNumber, stepsOf, toHash, writeAnother,
} from '../src/wizard.ts';
import type { CheckFacts, Facts, Step, Wiz } from '../src/wizard.ts';

const LINKS = { serial: true, usb: true };
const idle: Facts = { links: LINKS, conn: false, busy: false, result: null };
const connected: Facts = { ...idle, conn: true };
const writing: Facts = { ...idle, conn: true, busy: true };
const written: Facts = { ...idle, result: 'ok' };
const failed: Facts = { ...idle, result: 'failed' };

/** Taps through a task to Done and returns every state on the way. */
function walk(task: TaskId, board: string | null, advanced = false): Wiz[] {
  const out: Wiz[] = [];
  let w = chooseTask(start(advanced), task);
  out.push(w);
  if (w.step === 'prepare') out.push((w = next(w, idle)));
  if (w.step === 'board') out.push((w = chooseBoard(w, board!)));
  out.push((w = next(w, connected)));
  out.push((w = next(w, writing)));
  out.push((w = next(w, written)));
  return out;
}

const STEPS: Record<TaskId, Step[]> = {
  panel: ['task', 'connect', 'check', 'write', 'done'],
  iomcu: ['task', 'board', 'connect', 'check', 'write', 'done'],
  wireless: ['task', 'board', 'connect', 'check', 'write', 'done'],
  c6: ['task', 'prepare', 'connect', 'check', 'write', 'done'],
  file: ['task', 'board', 'connect', 'check', 'write', 'done'],
};

test('the steps of every task', () => {
  for (const [task, steps] of Object.entries(STEPS)) assert.deepEqual(stepsOf(task as TaskId), steps, task);
  assert.deepEqual(stepsOf(null), ['task']);
});

test('every task and board walks Task to Done in the order of its steps', () => {
  for (const advanced of [false, true]) {
    for (const x of tasksFor(advanced)) {
      for (const b of boardsFor(x.id, advanced)) {
        const states = walk(x.id, b.id, advanced);
        assert.deepEqual(['task', ...states.map((s) => s.step)], STEPS[x.id], `${x.id} ${b.id}`);
        const done = states.at(-1)!;
        assert.deepEqual(done, { task: x.id, board: b.id, step: 'done', advanced }, `${x.id} ${b.id}`);
        assert.deepEqual(stepNumber(done), { n: STEPS[x.id].length, m: STEPS[x.id].length });
      }
    }
  }
});

test('a one-board task takes its board with the task tap', () => {
  assert.deepEqual(chooseTask(start(), 'panel'), { task: 'panel', board: 'panel', step: 'connect', advanced: false });
  assert.deepEqual(chooseTask(start(), 'c6'), { task: 'c6', board: 'c6', step: 'prepare', advanced: false });
  assert.deepEqual(chooseTask(start(), 'iomcu'), { task: 'iomcu', board: null, step: 'board', advanced: false });
  assert.deepEqual(stepNumber(chooseTask(start(), 'iomcu')), { n: 2, m: 6 });
});

test('the own file is an Advanced task; the Core2350B is chosen in Advanced only', () => {
  assert.deepEqual(chooseTask(start(), 'file'), start());
  assert.equal(chooseTask(start(true), 'file').step, 'board');
  const simple = chooseTask(start(), 'iomcu');
  assert.deepEqual(chooseBoard(simple, 'core2350b'), simple);
  assert.equal(chooseBoard(chooseTask(start(true), 'iomcu'), 'core2350b').step, 'connect');
  assert.equal(chooseBoard(chooseTask(start(true), 'file'), 'core2350b').step, 'connect');
  assert.deepEqual(chooseBoard(simple, 'dial'), simple);
});

test('no move forward without its fact: a connection for Check, a write for Write, a good end for Done', () => {
  const c = chooseTask(start(), 'panel');
  assert.equal(next(c, idle).step, 'connect');
  const k = next(c, connected);
  assert.equal(k.step, 'check');
  assert.equal(next(k, connected).step, 'check');
  const wr = next(k, writing);
  assert.equal(wr.step, 'write');
  assert.equal(next(wr, writing).step, 'write');
  assert.equal(next(wr, failed).step, 'write');
  assert.equal(next(next(wr, written), written).step, 'done');
  const noUsb = { ...connected, links: { serial: true, usb: false } };
  assert.equal(next(chooseBoard(chooseTask(start(), 'iomcu'), 'iomcu'), noUsb).step, 'connect');
});

test('BACK from each step of each task', () => {
  for (const x of tasksFor(true)) {
    const b = boardsFor(x.id, true)[0]!.id;
    const states = walk(x.id, b, true);
    const at = (s: Step): Wiz => states.find((y) => y.step === s)!;
    const multi = STEPS[x.id].includes('board');
    // Board, Prepare and a one-board Connect lead to Task and drop everything.
    if (multi) {
      assert.deepEqual(back(at('board'), idle), { wiz: start(true), drop: { conn: true, image: true } }, x.id);
      assert.deepEqual(back(at('connect'), connected), { wiz: { ...at('connect'), board: null, step: 'board' }, drop: { conn: true, image: true } }, x.id);
    } else if (x.prepare) {
      assert.deepEqual(back(at('prepare'), idle), { wiz: start(true), drop: { conn: true, image: true } }, x.id);
      assert.deepEqual(back(at('connect'), connected), { wiz: at('prepare'), drop: { conn: true, image: false } }, x.id);
    } else {
      assert.deepEqual(back(at('connect'), connected), { wiz: start(true), drop: { conn: true, image: true } }, x.id);
    }
    // Check to Connect keeps the connection.
    assert.deepEqual(back(at('check'), connected), { wiz: { ...at('check'), step: 'connect' }, drop: { conn: false, image: false } }, x.id);
    // Write: inert while the write runs, Connect after a failure.
    assert.deepEqual(back(at('write'), writing), { wiz: at('write'), drop: { conn: false, image: false } }, x.id);
    assert.deepEqual(back(at('write'), failed), { wiz: { ...at('write'), step: 'connect' }, drop: { conn: true, image: false } }, x.id);
    // Done to Task.
    assert.deepEqual(back(at('done'), written), { wiz: start(true), drop: { conn: true, image: true } }, x.id);
  }
  assert.deepEqual(back(start(), idle), { wiz: start(), drop: { conn: false, image: false } });
});

test('CANCEL from each step leads to Task with Advanced kept, inert while a write runs', () => {
  for (const advanced of [false, true]) {
    for (const x of tasksFor(advanced)) {
      for (const w of walk(x.id, boardsFor(x.id, advanced)[0]!.id, advanced)) {
        assert.deepEqual(cancel(w, idle), { wiz: start(advanced), drop: { conn: true, image: true } }, `${x.id} ${w.step}`);
        if (w.step === 'write') assert.deepEqual(cancel(w, writing), { wiz: w, drop: { conn: false, image: false } });
      }
    }
  }
  assert.deepEqual(cancel(start(true), idle), { wiz: start(true), drop: { conn: false, image: false } });
});

test('CONNECT AGAIN after a failed write, WRITE ANOTHER and START OVER on Done', () => {
  const states = walk('wireless', 'node-pwr');
  const wr = states.find((s) => s.step === 'write')!;
  const done = states.at(-1)!;
  assert.deepEqual(connectAgain(wr, failed), { wiz: { ...wr, step: 'connect' }, drop: { conn: true, image: false } });
  assert.deepEqual(connectAgain(wr, writing).wiz, wr);
  assert.deepEqual(connectAgain(done, failed).wiz, done);
  const again = writeAnother(done);
  assert.deepEqual(again, { wiz: { task: 'wireless', board: 'node-pwr', step: 'connect', advanced: false }, drop: { conn: true, image: false } });
  assert.equal(next(again.wiz, connected).step, 'check');
  assert.deepEqual(writeAnother(wr).wiz, wr);
  assert.deepEqual(startOver(done), { wiz: start(), drop: { conn: true, image: true } });
  assert.deepEqual(startOver(wr).wiz, wr);
});

test('the Advanced switch acts on Task only and leaves nothing of a task behind', () => {
  assert.deepEqual(setAdvanced(start(), true), start(true));
  assert.deepEqual(setAdvanced(start(true), false), start(false));
  const c = chooseTask(start(true), 'panel');
  assert.deepEqual(setAdvanced(c, false), c);
});

test('the URL hash round-trips every state of every walk', () => {
  for (const advanced of [false, true]) {
    for (const x of tasksFor(advanced)) {
      for (const b of boardsFor(x.id, advanced)) {
        for (const w of walk(x.id, b.id, advanced)) assert.deepEqual(fromHash(`#${toHash(w)}`), w, toHash(w));
      }
    }
  }
  assert.equal(toHash(start()), '');
  assert.equal(toHash(start(true)), 'adv=1');
  assert.equal(toHash(chooseBoard(chooseTask(start(true), 'iomcu'), 'core2350b')), 't=iomcu&b=core2350b&s=connect&adv=1');
});

test('unknown or contradicting hash values lead to Task or to Board', () => {
  assert.deepEqual(fromHash(''), start());
  assert.deepEqual(fromHash('#adv=1'), start(true));
  assert.deepEqual(fromHash('#t=toaster&s=connect'), start());
  assert.deepEqual(fromHash('#t=panel&s=nowhere'), start());
  assert.deepEqual(fromHash('#t=panel&s=board'), start());
  assert.deepEqual(fromHash('#t=panel&s=task'), start());
  assert.deepEqual(fromHash('#t=file&b=dial&s=connect'), start());
  assert.deepEqual(fromHash('#t=iomcu&b=core2350b&s=connect'), { task: 'iomcu', board: null, step: 'board', advanced: false });
  assert.deepEqual(fromHash('#t=iomcu&b=dial&s=check'), { task: 'iomcu', board: null, step: 'board', advanced: false });
  assert.deepEqual(fromHash('#t=panel&b=dial&s=connect'), { task: 'panel', board: 'panel', step: 'connect', advanced: false });
  assert.deepEqual(fromHash('#t=iomcu&s=board&adv=1'), { task: 'iomcu', board: null, step: 'board', advanced: true });
});

test('a reload or a pasted link lands at most on Connect', () => {
  assert.deepEqual(clamp(fromHash('#t=panel&s=write'), idle), { task: 'panel', board: 'panel', step: 'connect', advanced: false });
  assert.deepEqual(clamp(fromHash('#t=panel&s=done'), idle).step, 'connect');
  assert.deepEqual(clamp(fromHash('#t=wireless&b=dial&s=check'), idle).step, 'connect');
  assert.deepEqual(clamp(fromHash('#t=c6&s=check'), idle).step, 'connect');
  assert.deepEqual(clamp(fromHash('#t=c6&s=prepare'), idle).step, 'prepare');
  assert.deepEqual(clamp(fromHash('#t=iomcu&s=board'), idle).step, 'board');
});

test('clamp keeps a state its facts hold, and walks back where a link is missing', () => {
  for (const w of walk('iomcu', 'iomcu-rbmod')) {
    const f = w.step === 'check' ? connected : w.step === 'write' ? writing : w.step === 'done' ? written : idle;
    assert.deepEqual(clamp(w, f), w, w.step);
  }
  const wr = walk('panel', null).find((s) => s.step === 'write')!;
  assert.deepEqual(clamp(wr, failed), wr);
  const noUsb = { ...idle, links: { serial: true, usb: false } };
  const c = chooseBoard(chooseTask(start(), 'iomcu'), 'iomcu');
  assert.deepEqual(clamp(c, noUsb), { ...c, board: null, step: 'board' });
  const noSerial = { ...idle, links: { serial: false, usb: true } };
  assert.deepEqual(clamp(chooseTask(start(), 'panel'), noSerial), start());
  assert.equal(canEnter('prepare', chooseTask(start(), 'panel'), idle), false);
  assert.equal(canEnter('board', chooseTask(start(), 'panel'), idle), false);
});

const ready: CheckFacts = {
  advanced: false, conn: true, busy: false, releasesFailed: false, loading: false, imageOk: true, keepRefused: false, sig: 'signed',
};

test('signature: a mismatch disables WRITE in simple mode and only warns in Advanced; no signature is a note in both', () => {
  assert.deepEqual(checkGate(ready), { write: true, block: null, notes: [] });
  assert.deepEqual(checkGate({ ...ready, advanced: true }), { write: true, block: null, notes: [] });
  assert.deepEqual(checkGate({ ...ready, sig: 'mismatch' }), { write: false, block: 'sig-mismatch', notes: ['sig-mismatch'] });
  assert.deepEqual(checkGate({ ...ready, sig: 'mismatch', advanced: true }), { write: true, block: null, notes: ['sig-mismatch'] });
  assert.deepEqual(checkGate({ ...ready, sig: 'none' }), { write: true, block: null, notes: ['sig-none'] });
  assert.deepEqual(checkGate({ ...ready, sig: 'none', advanced: true }), { write: true, block: null, notes: ['sig-none'] });
  assert.deepEqual(checkGate({ ...ready, sig: null }), { write: false, block: 'sig-pending', notes: [] });
  assert.deepEqual(checkGate({ ...ready, sig: null, advanced: true }), { write: false, block: 'sig-pending', notes: [] });
});

test('WRITE waits for the connection, the release list, the image and the keep plan, in that order', () => {
  assert.equal(checkGate({ ...ready, conn: false, busy: true }).block, 'conn');
  assert.equal(checkGate({ ...ready, busy: true }).block, 'busy');
  assert.deepEqual(checkGate({ ...ready, releasesFailed: true, loading: true }), { write: false, block: 'releases', notes: ['advanced-file'] });
  assert.deepEqual(checkGate({ ...ready, releasesFailed: true, advanced: true }).notes, []);
  assert.equal(checkGate({ ...ready, loading: true, imageOk: false }).block, 'loading');
  assert.equal(checkGate({ ...ready, imageOk: false, sig: null }).block, 'image');
  assert.deepEqual(checkGate({ ...ready, keepRefused: true }), { write: false, block: 'keep', notes: ['advanced-erase'] });
  assert.deepEqual(checkGate({ ...ready, keepRefused: true, advanced: true }), { write: false, block: 'keep', notes: [] });
  assert.equal(checkGate({ ...ready, keepRefused: true, sig: 'mismatch' }).block, 'keep');
});

test('a local file without a .sig file is unchecked: WRITE allowed in Advanced with its note, never pending', () => {
  const adv = { ...ready, advanced: true };
  assert.equal(sigStateOf('file', false, null), 'unchecked');
  assert.equal(sigStateOf('file', false, 'mismatch'), 'unchecked');
  assert.deepEqual(checkGate({ ...adv, sig: sigStateOf('file', false, null) }), { write: true, block: null, notes: ['sig-unchecked'] });
  // A .sig file picked beside the local file: the three verdicts as for a release image.
  assert.equal(sigStateOf('file', true, null), null);
  assert.deepEqual(checkGate({ ...adv, sig: sigStateOf('file', true, null) }).block, 'sig-pending');
  assert.deepEqual(checkGate({ ...adv, sig: sigStateOf('file', true, 'signed') }), { write: true, block: null, notes: [] });
  assert.deepEqual(checkGate({ ...adv, sig: sigStateOf('file', true, 'none') }), { write: true, block: null, notes: ['sig-none'] });
  assert.deepEqual(checkGate({ ...adv, sig: sigStateOf('file', true, 'mismatch') }), { write: true, block: null, notes: ['sig-mismatch'] });
  assert.deepEqual(checkGate({ ...ready, sig: sigStateOf('release', false, 'mismatch') }).block, 'sig-mismatch');
  assert.equal(sigStateOf('release', false, null), null);
});

test('simple mode never reaches unchecked: its source is the release, and the gate refuses it', () => {
  for (const b of BOARDS) {
    const source = sourceFor(b, false, 'file');
    if (source === null) continue;
    assert.equal(source, 'release', b.id);
    for (const picked of [false, true]) {
      for (const v of ['signed', 'none', 'mismatch', null] as const) assert.notEqual(sigStateOf(source, picked, v), 'unchecked', b.id);
    }
  }
  assert.deepEqual(checkGate({ ...ready, sig: 'unchecked' }), { write: false, block: 'source', notes: [] });
});
