// One render function per step of the wizard, in simple mode: Task, Board,
// Prepare, Connect, Check, Write, Done. Each returns the HTML of the step's
// section; main.ts puts it in place and wires the buttons by id and data
// attributes.

import { boardById, chipLabel, linkOf } from './boards.ts';
import type { Board } from './boards.ts';
import { hasKey } from './i18n.ts';
import type { Key } from './i18n.ts';
import { hex } from './partitions.ts';
import type { PlanVerdict } from './partitions.ts';
import { boardHasRelease, espPlan, phasesOf, progressView } from './actions.ts';
import { board, esc, fmtBytes, hasSerial, hasUsb, st, tr } from './state.ts';
import type { Note } from './state.ts';
import { boardUsable, boardsFor, tasksFor, tileState } from './tasks.ts';
import type { TaskId } from './tasks.ts';
import { checkGate, sigStateOf, stepNumber } from './wizard.ts';
import type { Gate, Step } from './wizard.ts';

const NOTICE_URL = 'https://github.com/subtilitas/rcbench/blob/main/NOTICE';
const LINKS = { serial: hasSerial, usb: hasUsb };
const API = { serial: 'Web Serial', picoboot: 'WebUSB' } as const;

const btn = (id: string, key: Key, on: boolean, cls = 'act', vars?: Record<string, string>): string =>
  `<button type="button" class="${cls}" id="${id}"${on ? '' : ' disabled'}>${esc(tr(key, vars))}</button>`;

export function renderNote(n: Note | null, id: string): string {
  if (!n) return '';
  return `<div class="note ${n.kind}" id="${id}" role="${n.kind === 'bad' ? 'alert' : 'status'}"><p>${esc(n.text)}</p>`
    + `${n.hint ? `<p>${esc(n.hint)}</p>` : ''}</div>`;
}

const boardName = (b: Board): string => tr(`b_${b.id}` as Key);

const TITLE: Record<Step, Key> = {
  task: 'sTask', board: 'sBoard', prepare: 'sPrepare', connect: 'sConnect', check: 'sCheck', write: 'sWrite', done: 'sDone',
};

/** The heading of the current step: "Step n of m: <title>", the bare title on Task. */
export function stepTitle(): string {
  const w = st.wiz;
  if (w.step === 'task') return tr('sTask');
  const { n, m } = stepNumber(w);
  return tr('stepOf', { n, m, title: tr(TITLE[w.step]) });
}

/** BACK and CANCEL, inert while a connect or a write runs. */
function bar(): string {
  const on = !st.busy;
  return `<div class="bar">${btn('back', 'back', on)}${btn('cancel', 'cancel', on)}</div>`;
}

// ---- Task ----

const TASK_CHIP: Record<TaskId, string> = {
  panel: 'ESP32-P4', iomcu: 'RP2350', wireless: 'ESP32-S3/-C6', c6: 'ESP32-C6', file: '',
};

function renderTask(): string {
  const none = !hasSerial && !hasUsb;
  const tiles = tasksFor(st.wiz.advanced).map((x) => {
    const s = tileState(x.id, st.wiz.advanced, LINKS);
    const why = none ? '' : s.usable ? '' : tr('unsupported', { api: s.missing.map((l) => API[l]).join(', ') });
    const sub = hasKey(`tsub_${x.id}`) ? tr(`tsub_${x.id}` as Key) : '';
    return `<button type="button" class="tile" data-task="${x.id}"${s.usable && !none ? '' : ' disabled'}>`
      + `<span><b>${esc(tr(`task_${x.id}` as Key))}</b>${sub ? `<small>${esc(sub)}</small>` : ''}`
      + `${why ? `<small class="why">${esc(why)}</small>` : ''}</span><span class="chip">${TASK_CHIP[x.id]}</span></button>`;
  }).join('');
  return `<div class="tiles" id="tiles">${tiles}</div><p class="muted small">${esc(tr('tasksNote'))}</p>`;
}

// ---- Board ----

function renderBoard(): string {
  const task = st.wiz.task;
  if (!task) return '';
  const tiles = boardsFor(task, st.wiz.advanced).map((b) => {
    const why = !boardUsable(b, LINKS) ? tr('unsupported', { api: API[linkOf(b.chip)] })
      : !boardHasRelease(b.id) ? tr('noRelease') : '';
    return `<button type="button" class="tile" data-board="${b.id}"${why ? ' disabled' : ''}>`
      + `<span><b>${esc(boardName(b))}</b>${why ? `<small class="why">${esc(why)}</small>` : ''}</span>`
      + `<span class="chip">${chipLabel(b.chip)}</span></button>`;
  }).join('');
  return `<div class="tiles" id="boards">${tiles}</div>${bar()}`;
}

// ---- Prepare (C6 radio) ----

// Header H4 with the adapter's wires and IO9 tied to GND. The order of the
// pins is not recorded: the drawing names the signals, the board's
// silkscreen gives the order.
const H4_SVG = '<svg class="h4" viewBox="0 0 220 96" role="img" aria-hidden="true">'
  + '<rect x="4" y="4" width="92" height="88" rx="4" fill="none" stroke="currentColor" stroke-width="2"/>'
  + '<text x="50" y="18" font-size="10" text-anchor="middle" fill="currentColor">H4</text>'
  + ['TXD0', 'RXD0', 'GND', 'IO9'].map((s, i) => `<circle cx="84" cy="${32 + i * 17}" r="4" fill="currentColor"/>`
    + `<text x="74" y="${36 + i * 17}" font-size="10" text-anchor="end" fill="currentColor">${s}</text>`).join('')
  + '<rect x="150" y="20" width="66" height="60" rx="4" fill="none" stroke="currentColor" stroke-width="2"/>'
  + ['RX', 'TX', 'GND'].map((s, i) => `<text x="190" y="${36 + i * 17}" font-size="10" fill="currentColor">${s}</text>`
    + `<path d="M88 ${32 + i * 17}H150" stroke="currentColor" stroke-width="2"/>`).join('')
  + '<path d="M84 83h20V66H88" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="3 2"/>'
  + '</svg>';

function renderPrepare(): string {
  const steps = (['c6_1', 'c6_2', 'c6_3'] as const).map((k) => `<li>${esc(tr(k))}</li>`).join('');
  return `<ol class="prep" id="prepare">${steps}</ol><div class="manual">${H4_SVG}<p class="small">${esc(tr('c6Pins'))}</p></div>`
    + `${btn('next', 'next', true, 'act primary')}${bar()}`;
}

// ---- Connect ----

// A board with its BOOT button held while the cable goes in.
const BOOT_SVG = '<svg class="boot" viewBox="0 0 120 56" role="img" aria-hidden="true">'
  + '<rect x="2" y="8" width="84" height="40" rx="4" fill="none" stroke="currentColor" stroke-width="2"/>'
  + '<rect x="86" y="20" width="14" height="16" fill="none" stroke="currentColor" stroke-width="2"/>'
  + '<path d="M100 28h18" stroke="currentColor" stroke-width="3"/>'
  + '<circle cx="30" cy="28" r="7" fill="currentColor"/><text x="44" y="44" font-size="9" text-anchor="middle" fill="currentColor">BOOT</text>'
  + '<path d="M30 2v12" stroke="currentColor" stroke-width="2"/><path d="M26 10l4 5 4-5" fill="none" stroke="currentColor" stroke-width="2"/>'
  + '</svg>';

function renderConnect(b: Board): string {
  const on = !st.busy;
  let html = '';
  if (st.conn) {
    html += `<p class="note ok" id="conn-state" role="status">${esc(tr('connected', { x: st.connLabel }))}</p>`;
    html += btn('next', 'next', on, 'act primary') + btn('disconnect', 'disconnect', on);
    return html + bar();
  }
  const line = `cn_${b.id}`;
  if (hasKey(line)) html += `<p id="conn-lines">${esc(tr(line))}</p>`;
  if (linkOf(b.chip) === 'serial') {
    if (b.id !== 'c6') html += `<p class="muted">${esc(tr('connectEspHint'))}</p>`;
    html += btn('connect-esp', 'connectEsp', on, 'act primary');
  } else {
    html += `<p class="muted">${esc(tr('bootselHint'))}</p>`;
    html += btn('bootsel', 'bootselBtn', on && hasSerial);
    html += renderNote(st.bootselNote, 'bootsel-note');
    html += `<div class="manual">${BOOT_SVG}<p>${esc(tr('bootselManual'))}</p></div>`;
    html += btn('connect-pico', 'connectPico', on, 'act primary');
  }
  html += renderNote(st.connNote, 'conn-note');
  if (st.chipRefused) html += btn('other-board', 'chooseOther', on);
  return html + bar();
}

// ---- Check ----

function releaseLabel(tag: string): string {
  const r = st.releases?.find((x) => x.tag === tag);
  return r ? `${r.tag}, ${tr(r.kind)}, ${r.published.slice(0, 10)}` : tag;
}

/** "Keep settings" in simple mode: always on, shown as the list of what stays on the board. */
function renderKeep(v: PlanVerdict): string {
  if (!v.ok) {
    return `<div class="note bad" id="keep-note" role="alert"><p>${esc(tr(`keep_${v.reason}` as Key, { x: v.detail }))}</p>`
      + `<p>${esc(tr('keepRefused'))}</p></div>`;
  }
  const { plan } = v;
  // Simple mode writes with "Keep settings" on: the plan always names its table.
  const table = hex(plan.table ?? 0);
  if (!plan.kept.length) return `<p class="note info" id="keep-note">${esc(tr('keepNone', { x: table }))}</p>`;
  const items = plan.kept.map((k) => `<li>${esc(tr('keepItem', { name: k.name, sub: k.subtype, offset: hex(k.offset), n: fmtBytes(k.size) }))}</li>`).join('');
  return `<div class="note info" id="keep-note"><p>${esc(tr('keepList', { x: table }))}</p>`
    + `<ul id="keep-list">${items}</ul><p>${esc(tr('keepOta'))}</p></div>`;
}

/** The gate of WRITE from the page's state. */
export function gate(): Gate {
  const plan = espPlan();
  return checkGate({
    advanced: false,
    conn: !!st.conn,
    busy: st.busy,
    releasesFailed: !!st.releasesError,
    loading: st.loading || (st.releases === null && !st.releasesError),
    imageOk: !!st.image,
    keepRefused: !!plan && !plan.ok,
    sig: st.image ? sigStateOf('release', false, st.image.sig.state) : null,
  });
}

function renderCheck(b: Board): string {
  const img = st.image;
  const rows: [Key, string, string?][] = [['rowBoard', boardName(b)], ['rowConn', st.connLabel || '—']];
  if (img) {
    rows.push(
      ['imgTag', releaseLabel(img.tag)],
      ['imgFile', img.name],
      ['imgSize', `${tr('bytes', { n: fmtBytes(img.bytes.length) })} (${(img.bytes.length / 1048576).toFixed(2)}MB)`],
      ['imgSha', img.sha256, 'mono'],
      ['imgCheck', tr('imgOk')],
    );
    for (const e of img.entry.erase ?? []) rows.push(['imgErase', tr('imgEraseB', { x: `0x${e.offset.toString(16)}+0x${e.size.toString(16)}` })]);
  }
  let html = `<dl class="img" id="image">${rows.map(([k, v, cls]) => `<dt>${esc(tr(k))}</dt><dd${cls ? ` class="${cls}"` : ''}>${esc(v)}</dd>`).join('')}`;
  if (img?.sig.state === 'signed') {
    html += `<dt>${esc(tr('rowSig'))}</dt><dd id="sig-state" data-state="signed">${esc(tr('sigSigned', { file: img.sig.file }))}</dd>`;
  }
  html += '</dl>';
  if (img && img.sig.state !== 'signed') {
    const bad = img.sig.state === 'mismatch';
    html += `<div class="note ${bad ? 'bad' : 'info'}" id="sig-state" data-state="${img.sig.state}" role="${bad ? 'alert' : 'status'}">`
      + `<p>${esc(tr(bad ? 'sigMismatch' : 'sigNone', { file: img.sig.file }))}</p></div>`;
  }
  const plan = espPlan();
  if (plan) html += renderKeep(plan);
  if (st.releasesError) html += renderNote({ kind: 'bad', text: tr('noReleases', { x: st.releasesError }) }, 'releases-note');
  else if (!img && !st.imageNote) html += `<p class="muted" id="image-wait">${esc(tr('loading'))}</p>`;
  html += renderNote(st.imageNote, 'image-note');
  if (st.imageNote?.retry) html += btn('retry', 'retry', !st.loading);
  const g = gate();
  html += `<p class="muted">${esc(tr('writeHint'))}</p>`;
  html += `<button type="button" class="act primary" id="write" data-block="${g.block ?? ''}"${g.write ? '' : ' disabled'}>${esc(tr('write'))}</button>`;
  return html + bar();
}

// ---- Write ----

function renderWrite(): string {
  const p = st.progress;
  const phases = phasesOf(st.conn?.kind === 'pico' || board()?.chip === 'rp2350' ? 'pico' : 'esp');
  const at = p ? phases.indexOf(p.step) : -1;
  const failed = st.outcome === 'failed';
  const failAt = failed && st.failedStep && phases.includes(st.failedStep) ? phases.indexOf(st.failedStep) : at;
  const stateOf = (i: number): string => {
    if (failed) return i < failAt ? 'done' : i === failAt ? 'failed' : 'todo';
    if (!st.writing) return 'done';
    return i < at ? 'done' : i === at ? 'now' : 'todo';
  };
  const items = phases.map((ph, i) => {
    const state = stateOf(i);
    return `<li data-state="${state}">${esc(hasKey(`step_${ph}`) ? tr(`step_${ph}` as Key) : ph)}</li>`;
  }).join('');
  const view = progressView();
  let html = `<p>${esc(tr('writeKeep'))}</p><ol class="phases" id="phases">${items}</ol>`;
  html += `<div class="prog" id="prog-wrap"${st.writing ? '' : ' hidden'}><progress id="prog" max="100" value="${view.pct}"></progress>`
    + `<p id="prog-text" aria-live="polite">${esc(view.text)}</p></div>`;
  html += renderNote(st.result, 'result');
  if (failed) html += btn('connect-again', 'connectAgain', !st.busy, 'act primary');
  return html + bar();
}

// ---- Done ----

function renderDone(b: Board): string {
  const next = `nx_${b.id}`;
  return renderNote(st.result, 'result')
    + (hasKey(next) ? `<p class="note info" id="next">${esc(tr(next))}</p>` : '')
    + `<div class="bar">${btn('again', 'writeAnother', true, 'act', { board: boardName(b) })}${btn('over', 'startOver', true, 'act primary')}</div>`;
}

/** The body of the current step. */
export function renderStep(): string {
  const w = st.wiz;
  const b = w.board ? boardById(w.board) : undefined;
  switch (w.step) {
    case 'task': return renderTask();
    case 'board': return renderBoard();
    case 'prepare': return renderPrepare();
    case 'connect': return b ? renderConnect(b) : '';
    case 'check': return b ? renderCheck(b) : '';
    case 'write': return renderWrite();
    case 'done': return b ? renderDone(b) : '';
  }
}

export function renderFooter(): string {
  return `<p>${esc(tr('footDev'))}</p><p>${esc(tr('footNoData'))}</p>`
    + `<p><a id="cfglink" href="../">${esc(tr('footConfigurator'))}</a> · `
    + `<a id="liclink" href="licenses.txt">${esc(tr('footLicences'))}</a></p>`
    + `<p>${esc(tr('footNames'))} <a id="noticelink" href="${NOTICE_URL}" rel="noopener noreferrer">${esc(tr('footNotice'))}</a></p>`;
}
