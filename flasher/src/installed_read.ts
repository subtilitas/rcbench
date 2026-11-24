// The board check on Check: reads the head of the application the boot
// loader of an ESP32 board starts and shows whether it is the firmware of the
// chosen board. A warning only: WRITE stays as the gate of Check sets it.
// The page loads this module on the first connect (actions.ts
// `checkInstalled`), so its code and strings stay out of the first load.
//
// The reads, all through the flasher stub, none of them writes or erases:
// the partition table (0xC00 bytes at 0xC000 on the ESP32-P4, else at
// 0x8000, the other offset when the first holds none), the two otadata
// entries (32 bytes each), and the application's first 296 bytes. An
// RP2350 board is not read: the IOmcu images of every RP2350 board carry the
// one program name rcbench-iomcu.

import type { Board } from './boards.ts';
import type { EspSession } from './esp.ts';
import type { Key, Lang } from './i18n.ts';
import { checkEsp } from './installed.ts';
import type { AppHead, Reader, Verdict } from './installed.ts';
import { md5Hex } from './digest.ts';
import { errText, esc, log, st, tr, ui } from './state.ts';
import type { Conn } from './state.ts';

const I = {
  rowInst: ['Installed', 'Installiert'],
  reading: ['reading the firmware on the board', 'die Firmware auf dem Board wird gelesen'],
  match: ['{fw}: the firmware of this board', '{fw}: die Firmware dieses Boards'],
  other: ['{fw}: the firmware of another device: {other}', '{fw}: die Firmware eines anderen Geräts: {other}'],
  otherNote: [
    'This board runs the firmware of {other}, not of {board}. Check that the cable goes to the board you mean. WRITE stays possible.',
    'Dieses Board führt die Firmware von {other} aus, nicht von {board}. Prüfe, ob das Kabel am gemeinten Board steckt. SCHREIBEN bleibt möglich.',
  ],
  or: [' or ', ' oder '],
  u_blank: [
    'cannot tell: no partition table at 0x8000 or 0xC000 (a blank board or another layout)',
    'nicht erkennbar: keine Partitionstabelle bei 0x8000 oder 0xC000 (ein leeres Board oder eine andere Aufteilung)',
  ],
  'u_no-app': [
    'cannot tell: no application in {x}, the slot the boot loader starts',
    'nicht erkennbar: keine Anwendung in {x}, dem Slot, den der Bootloader startet',
  ],
  u_foreign: ['cannot tell: {fw} is no firmware of this page', 'nicht erkennbar: {fw} ist keine Firmware dieser Seite'],
  u_ambiguous: [
    'cannot tell: {fw} without a role record is the firmware of {other} (releases before v0.23.13)',
    'nicht erkennbar: {fw} ohne Rollenkennung ist die Firmware von {other} (Releases vor v0.23.13)',
  ],
  u_unread: ['cannot tell: reading the flash failed ({x})', 'nicht erkennbar: das Lesen des Flashs schlug fehl ({x})'],
  'u_no-stub': ['cannot tell: the flasher stub does not run, so the page reads no flash', 'nicht erkennbar: der Flasher-Stub läuft nicht, die Seite liest also keinen Flash'],
  u_rp2350: [
    'cannot tell: the IOmcu images of every RP2350 board carry the one name rcbench-iomcu, so the page does not read them',
    'nicht erkennbar: die IOmcu-Images aller RP2350-Boards tragen denselben Namen rcbench-iomcu, die Seite liest sie daher nicht',
  ],
  'n_panel': ['Panel', 'Panel'],
  'n_dial': ['M5Stack Dial', 'M5Stack Dial'],
  'n_node-display': ['Display node', 'Anzeigeknoten'],
  'n_node-pwr': ['Power node', 'Leistungsknoten'],
  'n_node-bal': ['Balance node', 'Auswuchtknoten'],
  'n_c6': ['C6 radio of the panel', 'C6-Funk des Panels'],
} as const satisfies Record<string, readonly [string, string]>;

export type InstKey = keyof typeof I;

/** The string of a key in a language, with each {name} replaced. */
export function tInst(lang: Lang, key: InstKey, vars: Record<string, string> = {}): string {
  const s: string = I[key][lang === 'de' ? 1 : 0];
  return s.replace(/\{(\w+)\}/g, (m, name: string) => (name in vars ? vars[name]! : m));
}

/** Every key with both strings, for the tests. */
export const INST_KEYS = Object.keys(I) as InstKey[];
export const instStrings = (key: InstKey): readonly [string, string] => I[key];

const ti = (key: InstKey, vars?: Record<string, string>): string => tInst(st.lang, key, vars);
const devName = (id: string): string => (`n_${id}` in I ? ti(`n_${id}` as InstKey) : id);
const names = (ids: readonly string[]): string => ids.map(devName).join(ti('or'));
const fwOf = (h: AppHead): string => `${h.project} ${h.version}`.trim();

/** The stub does not run: the page reads no flash (secure download mode, a chip without a stub). */
const NO_STUB = 'no stub';

/**
 * A flash read through the flasher stub, held to the MD5 digest the stub
 * sends after the data. The digest is read here, so that no later command
 * takes it for its answer.
 */
async function readFlash(s: EspSession, address: number, size: number): Promise<Uint8Array> {
  const l = s.loader;
  if (!l.IS_STUB) throw new Error(NO_STUB);
  const data = await l.readFlash(address, size);
  const digest = await l.transport.read(l.DEFAULT_TIMEOUT);
  const hex = Array.from(digest, (b) => b.toString(16).padStart(2, '0')).join('');
  if (data.length !== size || hex !== md5Hex(data)) throw new Error(`0x${address.toString(16)}+0x${size.toString(16)}: MD5 differs`);
  return data;
}

/** The connection is gone: the check stops before its next read. */
const GONE = 'connection gone';

/** The verdict for a connection: an ESP32 board is read, an RP2350 one is not. Logs reads, bytes and time. */
async function verdictOf(conn: Conn, b: Board): Promise<Verdict> {
  if (conn.kind === 'pico') return { kind: 'unknown', why: 'rp2350' };
  let reads = 0;
  let bytes = 0;
  const rd: Reader = (address, size) => {
    if (st.conn !== conn) return Promise.reject(new Error(GONE));
    reads++;
    bytes += size;
    return readFlash(conn.session, address, size);
  };
  const t0 = performance.now();
  try {
    return await checkEsp(rd, b.chip, b.id);
  } catch (e) {
    const x = errText(e);
    return { kind: 'unknown', why: x === NO_STUB ? 'no-stub' : 'unread', detail: x };
  } finally {
    log(`board check: ${reads} reads, ${bytes} bytes, ${Math.round(performance.now() - t0)}ms`);
  }
}

/**
 * The board check of one connection: "reading" on Check, then the verdict.
 * A check whose connection is gone (BACK, CANCEL, a new connect) reads
 * nothing more and leaves the page's state as it is.
 */
export async function run(conn: Conn, b: Board): Promise<void> {
  if (st.conn !== conn) return;
  st.installed = { state: 'reading' };
  ui.render();
  const verdict = await verdictOf(conn, b);
  if (st.conn !== conn) return;
  st.installed = { state: 'done', verdict };
  ui.render();
}

/** The text of a verdict for the row of Check. */
export function verdictText(v: Verdict): string {
  if (v.kind === 'match') return ti('match', { fw: fwOf(v.head) });
  if (v.kind === 'other') return ti('other', { fw: fwOf(v.head), other: names(v.boards) });
  return ti(`u_${v.why}`, { x: v.detail ?? '', fw: v.head ? fwOf(v.head) : '', other: names(v.boards ?? []) });
}

/** The row of Check, inside its list: reading, or the verdict. */
export function row(): string {
  const i = st.installed;
  if (!i) return '';
  const text = i.state === 'reading' ? ti('reading') : verdictText(i.verdict);
  const kind = i.state === 'reading' ? 'reading' : i.verdict.kind;
  return `<dt>${esc(ti('rowInst'))}</dt><dd id="installed" data-verdict="${kind}">${esc(text)}</dd>`;
}

/** The warning under the rows when the board runs another device's firmware. */
export function note(b: Board): string {
  const i = st.installed;
  if (i?.state !== 'done' || i.verdict.kind !== 'other') return '';
  const text = ti('otherNote', { other: names(i.verdict.boards), board: tr(`b_${b.id}` as Key) });
  return `<div class="note warn" id="installed-note" role="status"><p>${esc(text)}</p></div>`;
}
