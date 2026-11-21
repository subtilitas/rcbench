// State, events and rendering of the flasher page.

import './style.css';
import { BOARDS, BOOTLOADER_OFFSET, boardById, checkImage, chipLabel, chipMatches, linkOf } from './boards.ts';
import type { Board } from './boards.ts';
import { IOMCU_PORT_FILTER, pickBootrom, touch1200 } from './bootsel.ts';
import { sha256Hex } from './digest.ts';
import { connectEsp } from './esp.ts';
import type { EspSession } from './esp.ts';
import { browserLang, hasKey, t } from './i18n.ts';
import type { Key, Lang } from './i18n.ts';
import { defaultRelease, readIndex, readManifest } from './manifest.ts';
import type { ImageEntry, Manifest, ReleaseEntry } from './manifest.ts';
import { hex, planWrite } from './partitions.ts';
import type { PlanVerdict } from './partitions.ts';
import { Picoboot } from './picoboot.ts';
import { StepError, planBin, planUf2, runPlan } from './rp2350.ts';
import { FAMILY, parseUf2 } from './uf2.ts';

const FW = `${import.meta.env.BASE_URL}fw/`;
const STORE_KEY = 'rcbench.flasher';
const NOTICE_URL = 'https://github.com/subtilitas/rcbench/blob/main/NOTICE';

interface Img {
  bytes: Uint8Array;
  name: string;
  tag: string | null;
  sha256: string;
  format: 'bin' | 'uf2';
  entry: ImageEntry | null;
}

type Conn = { kind: 'esp'; session: EspSession } | { kind: 'pico'; pb: Picoboot };

interface Note { kind: 'ok' | 'bad' | 'info'; text: string; hint?: string }

const hasSerial = 'serial' in navigator;
const hasUsb = 'usb' in navigator;

const st = {
  lang: 'en' as Lang,
  releases: null as ReleaseEntry[] | null,
  releasesError: null as string | null,
  board: BOARDS[0] as Board,
  source: 'release' as 'release' | 'file',
  tag: null as string | null,
  /** The release the visitor picked: a change of board keeps it when it carries the board. */
  chosenTag: null as string | null,
  manifests: new Map<string, Manifest>(),
  image: null as Img | null,
  imageNote: null as Note | null,
  conn: null as Conn | null,
  connLabel: '',
  connNote: null as Note | null,
  bootselNote: null as Note | null,
  busy: false,
  /** "Keep settings": an ESP write leaves the NVS partitions of the image's table as they are. On at the start and on a new board. */
  keep: true,
  result: null as Note | null,
  log: [] as string[],
  /** The last progress of the write in hand: rendered again after a re-render, a language switch for one. */
  progress: null as { step: string; done: number; total: number; t0: number } | null,
};

const $ = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
const esc = (s: string): string => s.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
const tr = (key: Key, vars?: Record<string, string | number>): string => t(st.lang, key, vars);
const fmtBytes = (n: number): string => n.toLocaleString(st.lang === 'de' ? 'de-DE' : 'en-GB');
const errText = (e: unknown): string => (e instanceof Error ? e.message : String(e));

function log(line: string): void {
  const s = line.replace(/\s+$/, '');
  if (!s) return;
  st.log.push(s);
  if (st.log.length > 400) st.log.shift();
  const pre = document.getElementById('log');
  if (pre) pre.textContent = st.log.join('\n');
}

function readStoredLang(): Lang | null {
  try {
    const v = JSON.parse(localStorage.getItem(STORE_KEY) ?? 'null') as unknown;
    if (v && typeof v === 'object' && 'lang' in v && (v.lang === 'en' || v.lang === 'de')) return v.lang;
  } catch { /* a blocked or damaged store leaves the browser language */ }
  return null;
}

function storeLang(lang: Lang): void {
  try { localStorage.setItem(STORE_KEY, JSON.stringify({ v: 1, lang })); } catch { /* not kept */ }
}

const boardUsable = (b: Board): boolean => !b.planned && (linkOf(b.chip) === 'serial' ? hasSerial : hasUsb);

// ---- Write plan ----

let planCache: { image: Img; keep: boolean; verdict: PlanVerdict } | null = null;

/** The write plan of the ESP image in hand, or null for an RP2350 board or no image. */
function espPlan(): PlanVerdict | null {
  const img = st.image;
  const chip = st.board.chip;
  if (!img || chip === 'rp2350') return null;
  if (planCache?.image !== img || planCache.keep !== st.keep) {
    planCache = { image: img, keep: st.keep, verdict: planWrite(img.bytes, BOOTLOADER_OFFSET[chip], st.keep) };
  }
  return planCache.verdict;
}

const keptText = (v: PlanVerdict | null): string => (v?.ok ? v.plan.kept.map((k) => `${k.name} ${hex(k.offset)}+${hex(k.size)}`).join(', ') : '');

// ---- Rendering ----

function renderBoards(): string {
  const groups: [Board['group'], Key][] = [['panel', 'gPanel'], ['iomcu', 'gIomcu'], ['wireless', 'gWireless']];
  return groups.map(([g, key]) => {
    const rows = BOARDS.filter((b) => b.group === g).map((b) => {
      const name = tr(`b_${b.id}` as Key);
      const why = b.planned ? tr('planned')
        : !boardUsable(b) ? tr('unsupported', { api: linkOf(b.chip) === 'serial' ? 'Web Serial' : 'WebUSB' }) : '';
      return `<label class="opt${why ? ' off' : ''}"><input type="radio" name="board" value="${b.id}"`
        + `${b.id === st.board.id ? ' checked' : ''}${why ? ' disabled' : ''}${st.busy ? ' disabled' : ''}>`
        + `<span><b>${esc(name)}</b> <small>${chipLabel(b.chip)}${why ? `, ${esc(why)}` : ''}</small></span></label>`;
    }).join('');
    return `<fieldset><legend>${esc(tr(key))}</legend>${rows}</fieldset>`;
  }).join('');
}

function releaseLabel(r: ReleaseEntry): string {
  return `${r.tag}, ${tr(r.kind)}, ${r.published.slice(0, 10)}`;
}

function renderNote(n: Note | null, id: string): string {
  if (!n) return '';
  return `<div class="note ${n.kind}" id="${id}" role="${n.kind === 'bad' ? 'alert' : 'status'}"><p>${esc(n.text)}</p>`
    + `${n.hint ? `<p>${esc(n.hint)}</p>` : ''}</div>`;
}

function renderFirmware(): string {
  const dis = st.busy ? ' disabled' : '';
  let html = '<div class="src">';
  if (st.releases === null && !st.releasesError) html += `<p class="muted">${esc(tr('loading'))}</p>`;
  if (st.releasesError) html += `<p class="muted">${esc(tr('noReleases', { x: st.releasesError }))}</p>`;
  const list = releasesFor(st.board);
  html += `<label class="opt"><input type="radio" name="src" value="release"${st.source === 'release' ? ' checked' : ''}`
    + `${list.length ? '' : ' disabled'}${dis}><span>${esc(tr('srcRelease'))}</span></label>`;
  html += `<select id="tag" aria-label="${esc(tr('srcRelease'))}"${st.source !== 'release' || !list.length ? ' disabled' : ''}${dis}>`;
  for (const r of list) html += `<option value="${r.tag}"${r.tag === st.tag ? ' selected' : ''}>${esc(releaseLabel(r))}</option>`;
  html += '</select>';
  html += `<label class="opt"><input type="radio" name="src" value="file"${st.source === 'file' ? ' checked' : ''}${dis}>`
    + `<span>${esc(tr('srcFile'))}</span></label>`;
  html += `<input type="file" id="file" accept=".bin,.uf2"${st.source !== 'file' ? ' disabled' : ''}${dis}>`;
  html += `<p class="muted small">${esc(tr('fileHint'))}</p></div>`;
  const img = st.image;
  if (img) {
    const rows: [Key, string][] = [
      ['imgTag', img.tag ?? '—'],
      ['imgFile', img.name],
      ['imgSize', `${tr('bytes', { n: fmtBytes(img.bytes.length) })} (${(img.bytes.length / 1048576).toFixed(2)}MB)`],
      ['imgSha', img.sha256],
      ['imgCheck', img.entry ? tr('imgOk') : tr('imgLocal')],
    ];
    for (const e of img.entry?.erase ?? []) {
      rows.push(['imgErase', tr('imgEraseB', { x: `0x${e.offset.toString(16)}+0x${e.size.toString(16)}` })]);
    }
    html += `<dl class="img" id="image">${rows.map(([k, v]) => `<dt>${esc(tr(k))}</dt><dd${k === 'imgSha' ? ' class="mono"' : ''}>${esc(v)}</dd>`).join('')}</dl>`;
    if (st.board.chip === 'rp2350' && img.format === 'bin') html += `<p class="note info">${esc(tr('binWarn'))}</p>`;
  }
  html += renderNote(st.imageNote, 'image-note');
  return html;
}

function renderConnect(): string {
  const dis = st.busy ? ' disabled' : '';
  let html = '';
  if (st.conn) {
    html += `<p class="note ok" id="conn-state" role="status">${esc(tr('connected', { x: st.connLabel }))}</p>`;
    html += `<button type="button" class="act" id="disconnect"${dis}>${esc(tr('disconnect'))}</button>`;
  } else if (linkOf(st.board.chip) === 'serial') {
    html += `<p class="muted">${esc(tr('connectEspHint'))}</p>`;
    html += `<button type="button" class="act" id="connect-esp"${dis}>${esc(tr('connectEsp'))}</button>`;
  } else {
    html += `<p class="muted">${esc(tr('bootselHint'))}</p>`;
    html += `<button type="button" class="act" id="bootsel"${dis || (hasSerial ? '' : ' disabled')}>${esc(tr('bootselBtn'))}</button>`;
    html += renderNote(st.bootselNote, 'bootsel-note');
    html += `<div class="manual">${BOOT_SVG}<p>${esc(tr('bootselManual'))}</p></div>`;
    html += `<button type="button" class="act" id="connect-pico"${dis}>${esc(tr('connectPico'))}</button>`;
  }
  html += renderNote(st.connNote, 'conn-note');
  return html;
}

/** Percentage and text of the progress in hand, in the language of the page. */
function progressView(): { pct: number; text: string } {
  const p = st.progress;
  if (!p) return { pct: 0, text: '' };
  const pct = p.total > 0 ? Math.floor((100 * p.done) / p.total) : 0;
  return {
    pct,
    text: tr('progress', {
      step: hasKey(`step_${p.step}`) ? tr(`step_${p.step}` as Key) : p.step,
      pct, done: fmtBytes(p.done), total: fmtBytes(p.total), s: ((performance.now() - p.t0) / 1000).toFixed(1),
    }),
  };
}

function renderKeep(v: PlanVerdict): string {
  const dis = st.busy ? ' disabled' : '';
  let html = `<label class="opt"><input type="checkbox" id="keep"${st.keep ? ' checked' : ''}${dis}>`
    + `<span><b>${esc(tr('keep'))}</b> <small>${esc(tr('keepSub'))}</small></span></label>`;
  if (!v.ok) {
    return html + `<div class="note bad" id="keep-note" role="alert"><p>${esc(tr(`keep_${v.reason}` as Key, { x: v.detail }))}</p>`
      + `<p>${esc(tr('keepRefused'))}</p></div>`;
  }
  const { plan } = v;
  if (plan.table === null) return html + `<p class="note info" id="keep-note">${esc(tr('keepOff'))}</p>`;
  if (!plan.kept.length) return html + `<p class="note info" id="keep-note">${esc(tr('keepNone', { x: hex(plan.table) }))}</p>`;
  const items = plan.kept.map((k) => `<li>${esc(tr('keepItem', { name: k.name, sub: k.subtype, offset: hex(k.offset), n: fmtBytes(k.size) }))}</li>`).join('');
  return html + `<div class="note info" id="keep-note"><p>${esc(tr('keepList', { x: hex(plan.table) }))}</p>`
    + `<ul id="keep-list">${items}</ul><p>${esc(tr('keepOta'))}</p></div>`;
}

function renderWrite(): string {
  const plan = espPlan();
  const ready = !!st.image && !!st.conn && !st.busy && (plan === null || plan.ok);
  let html = `<p class="muted">${esc(tr('writeHint'))}</p>`;
  if (plan) html += renderKeep(plan);
  html += `<button type="button" class="act primary" id="write"${ready ? '' : ' disabled'}>${esc(tr('write'))}</button>`;
  if (!st.image) html += ` <span class="muted small">${esc(tr('needImage'))}</span>`;
  else if (!st.conn) html += ` <span class="muted small">${esc(tr('needConnect'))}</span>`;
  const p = progressView();
  html += `<div class="prog" id="prog-wrap"${st.busy ? '' : ' hidden'}><progress id="prog" max="100" value="${p.pct}"></progress>`
    + `<p id="prog-text" aria-live="polite">${esc(p.text)}</p></div>`;
  html += renderNote(st.result, 'result');
  return html;
}

function renderFooter(): string {
  return `<p>${esc(tr('footDev'))}</p><p>${esc(tr('footNoData'))}</p>`
    + `<p><a id="cfglink" href="../">${esc(tr('footConfigurator'))}</a> · `
    + `<a id="liclink" href="licenses.txt">${esc(tr('footLicences'))}</a></p>`
    + `<p>${esc(tr('footNames'))} <a id="noticelink" href="${NOTICE_URL}" rel="noopener noreferrer">${esc(tr('footNotice'))}</a></p>`;
}

function render(): void {
  const focus = document.activeElement?.id;
  document.documentElement.lang = st.lang;
  document.title = tr('title');
  $('title').textContent = tr('title');
  $('sub').textContent = tr('sub');
  for (const b of document.querySelectorAll<HTMLButtonElement>('.lang button')) {
    b.setAttribute('aria-pressed', String(b.dataset.lang === st.lang));
  }
  const notices: string[] = [];
  if (!hasSerial && !hasUsb) notices.push(tr('noBrowser'));
  else {
    if (!hasSerial) notices.push(tr('noSerial'));
    if (!hasUsb) notices.push(tr('noUsb'));
  }
  $('notice').innerHTML = notices.map((n) => `<p>${esc(n)}</p>`).join('');
  $('notice').hidden = notices.length === 0;
  $('flow').hidden = !hasSerial && !hasUsb;
  $('h-board').textContent = tr('hBoard');
  $('h-firmware').textContent = tr('hFirmware');
  $('h-connect').textContent = tr('hConnect');
  $('h-write').textContent = tr('hWrite');
  $('boards').innerHTML = renderBoards();
  $('firmware').innerHTML = renderFirmware();
  $('connect').innerHTML = renderConnect();
  $('writer').innerHTML = renderWrite();
  $('log-sum').textContent = tr('log');
  $('log').textContent = st.log.join('\n');
  $('foot').innerHTML = renderFooter();
  if (focus) document.getElementById(focus)?.focus();
}

// A board with its BOOT button held while the cable goes in.
const BOOT_SVG = '<svg class="boot" viewBox="0 0 120 56" role="img" aria-hidden="true">'
  + '<rect x="2" y="8" width="84" height="40" rx="4" fill="none" stroke="currentColor" stroke-width="2"/>'
  + '<rect x="86" y="20" width="14" height="16" fill="none" stroke="currentColor" stroke-width="2"/>'
  + '<path d="M100 28h18" stroke="currentColor" stroke-width="3"/>'
  + '<circle cx="30" cy="28" r="7" fill="currentColor"/><text x="44" y="44" font-size="9" text-anchor="middle" fill="currentColor">BOOT</text>'
  + '<path d="M30 2v12" stroke="currentColor" stroke-width="2"/><path d="M26 10l4 5 4-5" fill="none" stroke="currentColor" stroke-width="2"/>'
  + '</svg>';

// ---- Image ----

async function fetchJson(url: string): Promise<unknown> {
  const r = await fetch(url, { cache: 'no-cache' });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}

async function manifestFor(tag: string): Promise<Manifest> {
  const known = st.manifests.get(tag);
  if (known) return known;
  const m = readManifest(await fetchJson(`${FW}${tag}/manifest.json`), tag);
  st.manifests.set(tag, m);
  return m;
}

function imageVerdict(bytes: Uint8Array, name: string): Note | null {
  const v = checkImage(st.board.chip, bytes, name);
  if (v.ok) return null;
  return { kind: 'bad', text: tr(`bad_${v.reason}` as Key, { x: v.detail }) };
}

let loadSeq = 0;

async function loadReleaseImage(): Promise<void> {
  const seq = ++loadSeq;
  st.image = null;
  st.imageNote = null;
  const tag = st.tag;
  if (st.source !== 'release' || st.board.planned) { render(); return; }
  if (!tag) {
    if (st.releases?.length) st.imageNote = { kind: 'info', text: tr('noRelease') };
    render();
    return;
  }
  render();
  try {
    const m = await manifestFor(tag);
    const entry = m.images.find((i) => i.board === st.board.id);
    if (seq !== loadSeq) return;
    if (!entry) { st.imageNote = { kind: 'info', text: tr('noImage', { tag }) }; render(); return; }
    st.imageNote = { kind: 'info', text: tr('fetching', { file: entry.file }) };
    render();
    let bytes: Uint8Array;
    try {
      const r = await fetch(`${FW}${tag}/${entry.file}`);
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      bytes = new Uint8Array(await r.arrayBuffer());
    } catch (e) {
      if (seq !== loadSeq) return;
      st.imageNote = { kind: 'bad', text: tr('fetchBad', { file: entry.file, x: errText(e) }) };
      render();
      return;
    }
    const sha = await sha256Hex(bytes);
    if (seq !== loadSeq) return;
    if (sha !== entry.sha256 || bytes.length !== entry.size) {
      st.imageNote = { kind: 'bad', text: tr('hashBad', { got: sha, want: entry.sha256 }) };
    } else {
      st.imageNote = imageVerdict(bytes, entry.file);
      if (!st.imageNote) st.image = { bytes, name: entry.file, tag, sha256: sha, format: entry.format, entry };
    }
  } catch (e) {
    if (seq !== loadSeq) return;
    st.imageNote = { kind: 'bad', text: tr('noReleases', { x: errText(e) }) };
  }
  render();
}

async function loadFile(file: File): Promise<void> {
  const seq = ++loadSeq;
  st.image = null;
  const bytes = new Uint8Array(await file.arrayBuffer());
  const sha = await sha256Hex(bytes);
  if (seq !== loadSeq) return;
  st.imageNote = imageVerdict(bytes, file.name);
  if (!st.imageNote) {
    const format = checkImage(st.board.chip, bytes, file.name);
    st.image = { bytes, name: file.name, tag: null, sha256: sha, format: format.ok ? format.format : 'bin', entry: null };
  }
  render();
}

// ---- Connection ----

async function dropConn(): Promise<void> {
  const c = st.conn;
  st.conn = null;
  st.connLabel = '';
  if (c?.kind === 'esp') await c.session.close();
  if (c?.kind === 'pico') await c.pb.close();
}

function failNote(e: unknown): Note {
  const step = e instanceof StepError ? e.step : 'connect';
  const stepName = hasKey(`step_${step}`) ? tr(`step_${step}` as Key) : step;
  const pico = linkOf(st.board.chip) === 'picoboot';
  const hintKey: Key = step === 'connect' ? (pico ? 'try_connect_pico' : 'try_connect_serial')
    : step === 'bootsel' ? 'try_bootsel'
      : step === 'verify' ? 'try_verify'
        : step === 'reset' || step === 'reboot' ? 'try_reset'
          : step === 'exclusive' || step === 'partition' ? 'try_rom' : 'try_transfer';
  const cause = e instanceof StepError ? e.cause : e;
  return { kind: 'bad', text: tr('failed', { step: stepName, x: errText(cause) }), hint: tr(hintKey) };
}

async function connectSerial(): Promise<void> {
  let port: SerialPort;
  try { port = await navigator.serial.requestPort(); } catch { return; }
  st.busy = true;
  st.connNote = null;
  st.result = null;
  render();
  try {
    const session = await connectEsp(port, log);
    if (!chipMatches(st.board.chip, session.chip)) {
      await session.close();
      st.connNote = { kind: 'bad', text: tr('chipBad', { want: chipLabel(st.board.chip), got: session.chip ? chipLabel(session.chip) : `${tr('chipUnknown')} (${session.description})` }) };
    } else {
      st.conn = { kind: 'esp', session };
      st.connLabel = session.description;
    }
  } catch (e) {
    st.connNote = failNote(e);
  }
  st.busy = false;
  render();
}

async function enterBootsel(): Promise<void> {
  let port: SerialPort;
  try { port = await navigator.serial.requestPort({ filters: IOMCU_PORT_FILTER }); } catch { return; }
  st.busy = true;
  render();
  try {
    await touch1200(port);
    st.bootselNote = { kind: 'ok', text: tr('bootselDone') };
  } catch (e) {
    st.bootselNote = failNote(new StepError('bootsel', e));
  }
  st.busy = false;
  render();
}

async function connectUsb(): Promise<void> {
  st.connNote = null;
  st.result = null;
  try {
    const dev = await pickBootrom(navigator.usb);
    if (!dev) { st.connNote = failNote(new StepError('connect', new Error(tr('noBootrom')))); render(); return; }
    st.busy = true;
    render();
    const pb = await Picoboot.open(dev);
    st.conn = { kind: 'pico', pb };
    st.connLabel = `${tr('picoName')}, ${dev.productName ?? ''}`.replace(/, $/, '');
  } catch (e) {
    st.connNote = failNote(e instanceof StepError ? e : new StepError('connect', e));
  }
  st.busy = false;
  render();
}

// ---- Write ----

async function write(): Promise<void> {
  const img = st.image;
  const conn = st.conn;
  if (!img || !conn || st.busy) return;
  st.busy = true;
  st.result = null;
  render();
  const t0 = performance.now();
  // The elements are looked up on every call: a render during the write, a
  // language switch for one, replaces them.
  const show = (step: string, done: number, total: number): void => {
    st.progress = { step, done, total, t0 };
    const view = progressView();
    const bar = document.getElementById('prog') as HTMLProgressElement | null;
    const text = document.getElementById('prog-text');
    if (bar) bar.value = view.pct;
    if (text) text.textContent = view.text;
  };
  try {
    if (conn.kind === 'esp') {
      const plan = espPlan();
      // A refused plan never reaches this point: WRITE stays disabled for it.
      if (!plan?.ok) throw new StepError('write', new Error(plan ? plan.reason : 'no plan'));
      const chunks = plan.plan.ranges.map((r) => ({ address: r.offset, data: img.bytes.subarray(r.offset, r.offset + r.size) }));
      const total = chunks.reduce((n, c) => n + c.data.length, 0);
      for (const c of chunks) log(`write 0x${c.address.toString(16)}+0x${c.data.length.toString(16)}`);
      await conn.session.write(chunks, (done) => show('write', done, total));
      show('verify', 0, total);
      await conn.session.verify(chunks);
      show('verify', total, total);
      show('reset', 0, 1);
      await conn.session.reset();
    } else {
      let plan;
      if (img.format === 'uf2') {
        const uf2 = parseUf2(img.bytes);
        let target = null;
        if (uf2.family !== FAMILY.ABSOLUTE) {
          try { target = await conn.pb.targetPartition(uf2.family); } catch (e) { throw new StepError('partition', e); }
        }
        try { plan = planUf2(uf2, target, img.entry?.erase ?? []); } catch (e) { throw new StepError('partition', e); }
      } else {
        plan = planBin(img.bytes);
      }
      await runPlan(conn.pb, plan, (p) => show(p.step, p.done, p.total));
      await conn.pb.close();
    }
    st.conn = null;
    st.connLabel = '';
    const kept = keptText(conn.kind === 'esp' ? espPlan() : null);
    st.result = { kind: 'ok', text: tr('done', { file: img.name, s: ((performance.now() - t0) / 1000).toFixed(1) }) + (kept ? ` ${tr('doneKept', { x: kept })}` : '') };
  } catch (e) {
    st.result = failNote(e);
    await dropConn();
  }
  st.busy = false;
  st.progress = null;
  render();
}

// ---- Events ----

function onChange(ev: Event): void {
  const el = ev.target as HTMLInputElement;
  if (el.name === 'board') {
    const b = boardById(el.value);
    if (!b || st.busy) return;
    st.board = b;
    st.keep = true;
    pickTag();
    st.connNote = null;
    st.bootselNote = null;
    st.result = null;
    // A local file is held to the chip of the board it was chosen for: a new board drops it.
    st.image = null;
    st.imageNote = null;
    void dropConn().then(() => loadReleaseImage());
  } else if (el.name === 'src') {
    st.source = el.value === 'file' ? 'file' : 'release';
    st.image = null;
    st.imageNote = null;
    void loadReleaseImage();
  } else if (el.id === 'tag') {
    st.tag = (el as unknown as HTMLSelectElement).value;
    st.chosenTag = st.tag;
    void loadReleaseImage();
  } else if (el.id === 'keep') {
    if (st.busy) return;
    st.keep = el.checked;
    st.result = null;
    render();
  } else if (el.id === 'file' && el.files?.[0]) {
    void loadFile(el.files[0]);
  }
}

function onClick(ev: Event): void {
  const el = (ev.target as HTMLElement).closest('button');
  if (!el) return;
  if (el.dataset.lang === 'en' || el.dataset.lang === 'de') {
    st.lang = el.dataset.lang;
    storeLang(st.lang);
    render();
    return;
  }
  if (el.id === 'connect-esp') void connectSerial();
  else if (el.id === 'bootsel') void enterBootsel();
  else if (el.id === 'connect-pico') void connectUsb();
  else if (el.id === 'disconnect') void dropConn().then(render);
  else if (el.id === 'write') void write();
}

/** Releases whose manifest carries an image for the board, newest first. */
function releasesFor(board: Board): ReleaseEntry[] {
  return (st.releases ?? []).filter((r) => st.manifests.get(r.tag)?.images.some((i) => i.board === board.id));
}

/** The newest stable release with an image for the board, else the newest with one. */
function pickTag(): void {
  const list = releasesFor(st.board);
  st.tag = list.some((r) => r.tag === st.chosenTag) ? st.chosenTag : defaultRelease(list)?.tag ?? null;
}

async function start(): Promise<void> {
  st.lang = readStoredLang() ?? browserLang(navigator.languages ?? [navigator.language]);
  document.addEventListener('change', onChange);
  document.addEventListener('click', onClick);
  render();
  if (!hasSerial && !hasUsb) return;
  try {
    const releases = readIndex(await fetchJson(`${FW}index.json`));
    const loaded = await Promise.allSettled(releases.map((r) => manifestFor(r.tag)));
    loaded.forEach((x, i) => { if (x.status === 'rejected') log(`${releases[i]!.tag}: ${errText(x.reason)}`); });
    st.releases = releases.filter((r) => st.manifests.has(r.tag));
    pickTag();
  } catch (e) {
    st.releasesError = errText(e);
  }
  if (!st.releases?.length) st.source = 'file';
  // A local file the visitor picked while the releases loaded stays: the
  // release image is loaded only while the source is still the release.
  if (st.source === 'release') await loadReleaseImage();
  else render();
}

void start();
