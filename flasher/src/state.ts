// The page's state, its types, and the helpers every part of the page uses:
// strings, escaping, the log, the stored language, the facts the wizard's
// guards read.

import { boardById } from './boards.ts';
import type { Board } from './boards.ts';
import type { EspSession } from './esp.ts';
import { t } from './i18n.ts';
import type { Key, Lang } from './i18n.ts';
import type { ImageEntry, Manifest, ReleaseEntry } from './manifest.ts';
import type { Picoboot } from './picoboot.ts';
import type { SigShown } from './signature.ts';
import { start } from './wizard.ts';
import type { Facts, Wiz } from './wizard.ts';

const STORE_KEY = 'rcbench.flasher';

export interface Img {
  bytes: Uint8Array;
  name: string;
  tag: string;
  sha256: string;
  format: 'bin' | 'uf2';
  entry: ImageEntry;
  /** The release signature: its shown state and the .sig file it comes from. */
  sig: { state: SigShown; file: string };
}

export type Conn = { kind: 'esp'; session: EspSession } | { kind: 'pico'; pb: Picoboot };

/** A note on a screen; `retry` adds RETRY for a download that failed or did not match. */
export interface Note { kind: 'ok' | 'bad' | 'info'; text: string; hint?: string; retry?: boolean }

export const hasSerial = 'serial' in navigator;
export const hasUsb = 'usb' in navigator;

export const st = {
  lang: 'en' as Lang,
  wiz: start() as Wiz,
  releases: null as ReleaseEntry[] | null,
  releasesError: null as string | null,
  /** The release of the visit (release.ts `visitRelease`): set once the list is in. */
  visit: null as string | null,
  manifests: new Map<string, Manifest>(),
  /** The release the page writes to the board in hand; null while the list loads or when no release carries the board. */
  tag: null as string | null,
  image: null as Img | null,
  /** A download or a check of the release image runs. */
  loading: false,
  imageNote: null as Note | null,
  conn: null as Conn | null,
  connLabel: '',
  connNote: null as Note | null,
  /** True when the last connect found a chip other than the board's. */
  chipRefused: false,
  bootselNote: null as Note | null,
  /** A connect, the BOOTSEL request or a write runs. */
  busy: false,
  /** A write runs: BACK, CANCEL, the language and the browser's back are inert. */
  writing: false,
  /** "Keep settings": always on in simple mode. */
  keep: true,
  /** The end of the last write: ok, failed, or null before one. */
  outcome: null as 'ok' | 'failed' | null,
  result: null as Note | null,
  /** The phase that failed, for the phase list on Write. */
  failedStep: null as string | null,
  log: [] as string[],
  /** The last progress of the write in hand: rendered again after a re-render, a language switch for one. */
  progress: null as { step: string; done: number; total: number; t0: number } | null,
};

/** The board of the wizard state; null before a board is chosen. */
export const board = (): Board | null => (st.wiz.board ? boardById(st.wiz.board) ?? null : null);

/** What the wizard's guards read from the page. */
export function facts(): Facts {
  return { links: { serial: hasSerial, usb: hasUsb }, conn: !!st.conn, busy: st.writing, result: st.outcome };
}

/** Hooks main.ts sets: a render of the current step, and the moves an action ends with. */
export const ui = {
  render: (): void => undefined,
  /** A connection whose chip matches the board: on to Check. */
  connected: (): void => undefined,
  /** The end of a write: Done on success, the error on Write otherwise. */
  written: (): void => undefined,
};

export const $ = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
export const esc = (s: string): string => s.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
export const tr = (key: Key, vars?: Record<string, string | number>): string => t(st.lang, key, vars);
export const fmtBytes = (n: number): string => n.toLocaleString(st.lang === 'de' ? 'de-DE' : 'en-GB');
export const errText = (e: unknown): string => (e instanceof Error ? e.message : String(e));

export function log(line: string): void {
  const s = line.replace(/\s+$/, '');
  if (!s) return;
  st.log.push(s);
  if (st.log.length > 400) st.log.shift();
  const pre = document.getElementById('log');
  if (pre) pre.textContent = st.log.join('\n');
}

export function readStoredLang(): Lang | null {
  try {
    const v = JSON.parse(localStorage.getItem(STORE_KEY) ?? 'null') as unknown;
    if (v && typeof v === 'object' && 'lang' in v && (v.lang === 'en' || v.lang === 'de')) return v.lang;
  } catch { /* a blocked or damaged store leaves the browser language */ }
  return null;
}

export function storeLang(lang: Lang): void {
  try { localStorage.setItem(STORE_KEY, JSON.stringify({ v: 1, lang })); } catch { /* not kept */ }
}
