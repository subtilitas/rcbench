// The tasks of the wizard's first screen, the boards each task writes, and
// the links a task needs. Pure: no browser code.

import { BOARDS, boardById, linkOf } from './boards.ts';
import type { Board, Link } from './boards.ts';

export type TaskId = 'panel' | 'iomcu' | 'wireless' | 'c6' | 'file';

export interface Task {
  id: TaskId;
  /** Board ids of `BOARDS`, in the order the board step lists them. */
  boards: readonly string[];
  /** True for a task that shows only with the Advanced switch on. */
  advancedOnly: boolean;
  /** True for a task with a Prepare step before Connect. */
  prepare: boolean;
}

/**
 * The tasks in the order of the first screen. The C6 radio repair is the
 * last task of simple mode; "Write a file of my own" follows it in Advanced.
 * A planned board (no image in any release) stays in a task's list and
 * shows in Advanced only (see `boardsFor`).
 */
export const TASKS: readonly Task[] = [
  { id: 'panel', boards: ['panel'], advancedOnly: false, prepare: false },
  { id: 'iomcu', boards: ['iomcu', 'iomcu-rbmod', 'core2350b'], advancedOnly: false, prepare: false },
  { id: 'wireless', boards: ['node-display', 'dial', 'dial-k518', 'node-pwr', 'node-bal'], advancedOnly: false, prepare: false },
  { id: 'c6', boards: ['c6'], advancedOnly: false, prepare: true },
  { id: 'file', boards: BOARDS.map((b) => b.id), advancedOnly: true, prepare: false },
];

/** The task of an id, or undefined for any other string. */
export const taskById = (id: string): Task | undefined => TASKS.find((x) => x.id === id);

/** The tasks of the first screen: simple mode leaves out the Advanced-only ones. */
export function tasksFor(advanced: boolean): Task[] {
  return TASKS.filter((x) => advanced || !x.advancedOnly);
}

/**
 * The boards a task offers. A planned board, such as the Core2350B, shows
 * in Advanced only, where it takes a local file only (`sourcesFor`).
 */
export function boardsFor(task: TaskId, advanced: boolean): Board[] {
  const x = taskById(task);
  if (!x || (x.advancedOnly && !advanced)) return [];
  return x.boards.map((id) => boardById(id)!).filter((b) => advanced || !b.planned);
}

/** True when the task offers one board only, so the wizard skips the board step. */
export const singleBoard = (task: TaskId): boolean => taskById(task)?.boards.length === 1;

export type Source = 'release' | 'file';

/**
 * The image sources of a board. Simple mode writes release images only.
 * Advanced adds the local file; a planned board takes the local file only.
 */
export function sourcesFor(board: Board, advanced: boolean): Source[] {
  if (!advanced) return board.planned ? [] : ['release'];
  return board.planned ? ['file'] : ['release', 'file'];
}

/**
 * The source the page uses: the wanted one when the board and the mode
 * offer it, else the first offered one; null when the board takes none.
 * "Write a file of my own" preselects the local file.
 */
export function sourceFor(board: Board, advanced: boolean, wanted: Source): Source | null {
  const offered = sourcesFor(board, advanced);
  return offered.includes(wanted) ? wanted : offered[0] ?? null;
}

/** "Keep settings": always on in simple mode, the visitor's choice in Advanced. */
export const keepFor = (advanced: boolean, wanted: boolean): boolean => !advanced || wanted;

/** The links the browser offers: Web Serial and WebUSB. */
export interface Links {
  serial: boolean;
  usb: boolean;
}

/** True when the browser has the link the board's chip needs. */
export function boardUsable(board: Board, links: Links): boolean {
  return linkOf(board.chip) === 'picoboot' ? links.usb : links.serial;
}

/** The links a task's boards need, without repeats, in the order serial, picoboot. */
export function taskLinks(task: TaskId, advanced: boolean): Link[] {
  const need = new Set(boardsFor(task, advanced).map((b) => linkOf(b.chip)));
  return (['serial', 'picoboot'] as const).filter((l) => need.has(l));
}

/** A tile of the first screen: usable, or greyed with the link it lacks. */
export type TileState = { usable: true } | { usable: false; missing: Link[] };

/** A task is usable when the browser has the link of at least one of its boards. */
export function tileState(task: TaskId, advanced: boolean, links: Links): TileState {
  const need = taskLinks(task, advanced);
  const missing = need.filter((l) => (l === 'picoboot' ? !links.usb : !links.serial));
  return missing.length < need.length ? { usable: true } : { usable: false, missing };
}
