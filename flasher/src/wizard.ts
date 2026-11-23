// The wizard's step machine: the steps of each task, the moves between them,
// the entry guard of each step, the URL hash, and the gate of WRITE. Pure:
// no browser code. The page applies a move's `drop` to its connection and
// image.

import { boardById } from './boards.ts';
import { boardUsable, boardsFor, singleBoard, taskById } from './tasks.ts';
import type { Links, TaskId } from './tasks.ts';

export type Step = 'task' | 'board' | 'prepare' | 'connect' | 'check' | 'write' | 'done';

export interface Wiz {
  task: TaskId | null;
  board: string | null;
  step: Step;
  advanced: boolean;
}

/** What the page holds outside the wizard state: the guards read it. */
export interface Facts {
  links: Links;
  /** A connection to the board is open. */
  conn: boolean;
  /** A write runs. */
  busy: boolean;
  /** The result of the last write: null before one. */
  result: 'ok' | 'failed' | null;
}

/** What a move asks the page to drop. */
export interface Drop {
  conn: boolean;
  /** The image, its notes, and "Keep settings" back to on. */
  image: boolean;
}

export interface Move {
  wiz: Wiz;
  drop: Drop;
}

const NONE: Drop = { conn: false, image: false };
const CONN: Drop = { conn: true, image: false };
const ALL: Drop = { conn: true, image: true };

/** The first screen, with the Advanced switch as given. */
export const start = (advanced = false): Wiz => ({ task: null, board: null, step: 'task', advanced });

/** The steps of a task, Task first and Done last; the board step only for a task of several boards. */
export function stepsOf(task: TaskId | null): Step[] {
  const x = task ? taskById(task) : undefined;
  if (!x) return ['task'];
  const mid: Step[] = x.prepare ? ['prepare'] : singleBoard(x.id) ? [] : ['board'];
  return ['task', ...mid, 'connect', 'check', 'write', 'done'];
}

/** "Step n of m" of the current step; n counts from 1. */
export function stepNumber(w: Wiz): { n: number; m: number } {
  const steps = stepsOf(w.task);
  return { n: steps.indexOf(w.step) + 1, m: steps.length };
}

/** True when the board is one the task offers in the current mode. */
function boardOffered(w: Wiz): boolean {
  return w.task !== null && w.board !== null && boardsFor(w.task, w.advanced).some((b) => b.id === w.board);
}

/** The entry guard of a step: true when the wizard may stand on it. */
export function canEnter(step: Step, w: Wiz, f: Facts): boolean {
  if (step === 'task') return true;
  if (!stepsOf(w.task).includes(step)) return false;
  switch (step) {
    case 'board':
    case 'prepare':
      return true;
    case 'connect':
      return boardOffered(w) && boardUsable(boardById(w.board!)!, f.links);
    case 'check':
      return canEnter('connect', w, f) && f.conn;
    case 'write':
      return canEnter('connect', w, f) && (f.busy || f.result === 'failed');
    case 'done':
      return canEnter('connect', w, f) && !f.busy && f.result === 'ok';
  }
}

/**
 * The step the wizard may stand on: the given step when every step before it
 * and the step itself pass their guards, else the last step that passes.
 * A reload or a pasted link lands at most on Connect, since the connection
 * is not in the URL. A step back to Board drops the board.
 */
export function clamp(w: Wiz, f: Facts): Wiz {
  const steps = stepsOf(w.task);
  const want = Math.max(0, steps.indexOf(w.step));
  let at = 0;
  for (let i = 1; i <= want; i++) {
    const s = steps[i]!;
    // Check, Write and Done hold the page's connection and write: a step
    // past them holds its own guard, so they are not passed through.
    if (i < want && (s === 'check' || s === 'write' || s === 'done')) continue;
    if (!canEnter(s, w, f)) break;
    at = i;
  }
  const step = steps[at]!;
  if (step === 'task') return start(w.advanced);
  return { ...w, step, board: step === 'board' ? null : w.board };
}

/** The Task tap: the task's first step after Task; a one-board task takes its board here. */
export function chooseTask(w: Wiz, task: TaskId): Wiz {
  const x = taskById(task);
  if (w.step !== 'task' || !x || (x.advancedOnly && !w.advanced)) return w;
  const board = singleBoard(task) ? x.boards[0]! : null;
  return { ...w, task, board, step: stepsOf(task)[1]! };
}

/** The board tap on Board: on to Connect when the task offers the board in the current mode. */
export function chooseBoard(w: Wiz, board: string): Wiz {
  if (w.step !== 'board' || !w.task || !boardsFor(w.task, w.advanced).some((b) => b.id === board)) return w;
  return { ...w, board, step: 'connect' };
}

/**
 * The forward move of the current step: NEXT on Prepare, a connection on
 * Connect, WRITE on Check, the end of a write on Write. Stays when the next
 * step's guard fails.
 */
export function next(w: Wiz, f: Facts): Wiz {
  const steps = stepsOf(w.task);
  const i = steps.indexOf(w.step);
  if (w.step === 'task' || w.step === 'board' || w.step === 'done' || i < 0) return w;
  const to = steps[i + 1]!;
  return canEnter(to, w, f) ? { ...w, step: to } : w;
}

/**
 * BACK, and the browser's back. Inert while a write runs. From Check the
 * connection stays; from Connect it goes; back to Task or to Board the image
 * goes too. From Write after a failure: Connect, the image kept. From Done:
 * Task.
 */
export function back(w: Wiz, f: Facts): Move {
  if (f.busy) return { wiz: w, drop: NONE };
  switch (w.step) {
    case 'task':
      return { wiz: w, drop: NONE };
    case 'check':
      return { wiz: { ...w, step: 'connect' }, drop: NONE };
    case 'write':
      return { wiz: { ...w, step: 'connect' }, drop: CONN };
    case 'done':
      return { wiz: start(w.advanced), drop: ALL };
    default: {
      const steps = stepsOf(w.task);
      const to = steps[steps.indexOf(w.step) - 1] ?? 'task';
      if (to === 'task') return { wiz: start(w.advanced), drop: ALL };
      if (to === 'board') return { wiz: { ...w, board: null, step: 'board' }, drop: ALL };
      return { wiz: { ...w, step: to }, drop: CONN };
    }
  }
}

/** CANCEL: Task, the connection and the image dropped, Advanced kept. Inert while a write runs. */
export function cancel(w: Wiz, f: Facts): Move {
  if (f.busy || w.step === 'task') return { wiz: w, drop: NONE };
  return { wiz: start(w.advanced), drop: ALL };
}

/** CONNECT AGAIN after a failed write: Connect with the image kept. */
export function connectAgain(w: Wiz, f: Facts): Move {
  if (w.step !== 'write' || f.busy || f.result !== 'failed') return { wiz: w, drop: NONE };
  return { wiz: { ...w, step: 'connect' }, drop: CONN };
}

/** WRITE ANOTHER on Done: Connect with the same board and image. */
export function writeAnother(w: Wiz): Move {
  if (w.step !== 'done') return { wiz: w, drop: NONE };
  return { wiz: { ...w, step: 'connect' }, drop: CONN };
}

/** START OVER on Done: Task. */
export function startOver(w: Wiz): Move {
  if (w.step !== 'done') return { wiz: w, drop: NONE };
  return { wiz: start(w.advanced), drop: ALL };
}

/**
 * The Advanced switch, on the first screen only. Switching it off on Task
 * leaves nothing of Advanced behind; the page resets source and "Keep
 * settings" with `sourceFor` and `keepFor`.
 */
export function setAdvanced(w: Wiz, on: boolean): Wiz {
  return w.step === 'task' ? start(on) : w;
}

const STEPS: readonly string[] = ['task', 'board', 'prepare', 'connect', 'check', 'write', 'done'];

/** The URL hash of a state: `t=<task>&b=<board>&s=<step>[&adv=1]`; empty on Task. */
export function toHash(w: Wiz): string {
  const p = new URLSearchParams();
  if (w.task && w.step !== 'task') {
    p.set('t', w.task);
    if (w.board) p.set('b', w.board);
    p.set('s', w.step);
  }
  if (w.advanced) p.set('adv', '1');
  return p.toString();
}

/**
 * A state from a URL hash, with or without the leading '#'. An unknown task
 * or step, or a board the task does not offer in that mode, gives Task (or
 * Board for a task of several boards). `clamp` then holds the step to the
 * facts.
 */
export function fromHash(hash: string): Wiz {
  const p = new URLSearchParams(hash.replace(/^#/, ''));
  const advanced = p.get('adv') === '1';
  const t = p.get('t') ?? '';
  const s = p.get('s') ?? '';
  const x = taskById(t);
  const task = x?.id ?? null;
  if (!task || !x || (x.advancedOnly && !advanced) || !STEPS.includes(s) || !stepsOf(task).includes(s as Step) || s === 'task') {
    return start(advanced);
  }
  const offered = boardsFor(task, advanced).map((b) => b.id);
  const b = p.get('b');
  const board = singleBoard(task) ? x.boards[0]! : b && offered.includes(b) ? b : null;
  const steps = stepsOf(task);
  if (board === null && steps.indexOf(s as Step) > steps.indexOf('board')) return { task, board, step: 'board', advanced };
  return { task, board, step: s as Step, advanced };
}

/**
 * The signature state of an image: signed by the release key, no signature,
 * a signature that does not match, or a local file without a picked .sig
 * file, whose signature is not checked. The first three match `SigShown` of
 * the signature check (flasher/src/signature.ts); `unchecked` matches its
 * local-file state `local`.
 */
export type SigState = 'signed' | 'none' | 'mismatch' | 'unchecked';

/**
 * The signature state of an image in hand. A release image takes the shown
 * verdict of its signature check. A local file without a picked .sig file is
 * `unchecked`; with one it takes the verdict of that check. Null while a
 * check runs (`pending`, or no verdict yet), for either source and whether
 * or not the page holds the picked .sig file yet.
 */
export function sigStateOf(source: 'release' | 'file', sigPicked: boolean, verdict: 'signed' | 'none' | 'mismatch' | 'pending' | null): SigState | null {
  // A check that runs holds WRITE whatever else the page holds, so the order
  // in which a .sig pick updates the page cannot open WRITE early.
  if (verdict === 'pending') return null;
  if (source === 'file' && !sigPicked) return 'unchecked';
  return verdict;
}

/** What Check holds before WRITE. */
export interface CheckFacts {
  advanced: boolean;
  conn: boolean;
  busy: boolean;
  /** The release list failed and the source is a release. */
  releasesFailed: boolean;
  /** The image download or the local file read runs. */
  loading: boolean;
  /** An image is in hand and passed its checks (size, SHA-256, chip). */
  imageOk: boolean;
  /** The write plan of "Keep settings" refuses the image. */
  keepRefused: boolean;
  /** The signature state (`sigStateOf`); null while the check runs or before an image. */
  sig: SigState | null;
}

export type Block = 'conn' | 'busy' | 'releases' | 'loading' | 'image' | 'source' | 'keep' | 'sig-pending' | 'sig-mismatch';

/** A note on Check: the hint to Advanced, or the signature note. */
export type CheckNote = 'advanced-file' | 'advanced-erase' | 'sig-none' | 'sig-mismatch' | 'sig-unchecked';

export interface Gate {
  /** True when WRITE is enabled. */
  write: boolean;
  /** The first reason WRITE is disabled; null when enabled. */
  block: Block | null;
  notes: CheckNote[];
}

/**
 * The gate of WRITE on Check. A signature that does not match disables WRITE
 * in simple mode; Advanced shows the same red note and allows WRITE. No
 * signature is an information note in both modes. A local file without a
 * picked .sig file (`unchecked`, Advanced only) is an information note and
 * allows WRITE. Simple mode writes release images only: `unchecked` there
 * disables WRITE (`source`).
 */
export function checkGate(c: CheckFacts): Gate {
  const notes: CheckNote[] = [];
  if (c.sig === 'none') notes.push('sig-none');
  if (c.sig === 'mismatch') notes.push('sig-mismatch');
  if (c.sig === 'unchecked' && c.advanced) notes.push('sig-unchecked');
  if (c.releasesFailed && !c.advanced) notes.push('advanced-file');
  if (c.keepRefused && !c.advanced) notes.push('advanced-erase');
  const block: Block | null = !c.conn ? 'conn'
    : c.busy ? 'busy'
      : c.releasesFailed ? 'releases'
        : c.loading ? 'loading'
          : !c.imageOk ? 'image'
            : c.sig === 'unchecked' && !c.advanced ? 'source'
            : c.keepRefused ? 'keep'
              : c.sig === null ? 'sig-pending'
                : c.sig === 'mismatch' && !c.advanced ? 'sig-mismatch'
                  : null;
  return { write: block === null, block, notes };
}
