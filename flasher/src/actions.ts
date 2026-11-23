// What the page does: the release list, the release image and its checks,
// the connection, and the write with its verify. Each action changes `st`
// and calls `ui.render`; a connection that fits and the end of a write call
// the wizard's moves in `ui`.

import { BOOTLOADER_OFFSET, checkImage, chipLabel, chipMatches, linkOf } from './boards.ts';
import { IOMCU_PORT_FILTER, pickBootrom, touch1200 } from './bootsel.ts';
import { sha256Hex } from './digest.ts';
import { connectEsp } from './esp.ts';
import { hasKey } from './i18n.ts';
import type { Key } from './i18n.ts';
import { readIndex, readManifest } from './manifest.ts';
import type { Manifest } from './manifest.ts';
import { hex, planWrite } from './partitions.ts';
import type { PlanVerdict } from './partitions.ts';
import { Picoboot } from './picoboot.ts';
import { releaseFor, visitRelease } from './release.ts';
import { RELEASE_KEY_PEM } from './release_key.ts';
import { StepError, planBin, planUf2, runPlan } from './rp2350.ts';
import { sigShown, verifySignature } from './signature.ts';
import { board, errText, fmtBytes, log, st, tr, ui } from './state.ts';
import type { AdvApi, Img, Note } from './state.ts';
import { sourceFor } from './tasks.ts';
import { FAMILY, parseUf2 } from './uf2.ts';

const FW = `${import.meta.env.BASE_URL}fw/`;

// ---- Releases and the image ----

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

/** Loads the release list and every manifest, and sets the release of the visit. */
export async function loadReleases(): Promise<void> {
  try {
    const releases = readIndex(await fetchJson(`${FW}index.json`));
    const loaded = await Promise.allSettled(releases.map((r) => manifestFor(r.tag)));
    loaded.forEach((x, i) => { if (x.status === 'rejected') log(`${releases[i]!.tag}: ${errText(x.reason)}`); });
    st.releases = releases.filter((r) => st.manifests.has(r.tag));
    st.visit = visitRelease(st.releases, st.manifests);
  } catch (e) {
    st.releasesError = errText(e);
  }
}

/** True when a release on the page carries an image for the board; true while the list loads. */
export function boardHasRelease(id: string): boolean {
  if (!st.releases) return true;
  return releaseFor(id, st.releases, st.manifests, { visit: st.visit, chosen: null, advanced: false }) !== null;
}

/** The release the page writes to the board in hand: the picked one in Advanced while it carries the board. */
function tagFor(id: string): string | null {
  return st.releases ? releaseFor(id, st.releases, st.manifests, { visit: st.visit, chosen: st.chosen, advanced: st.wiz.advanced }) : null;
}

/**
 * A new board: drops the image, a local file's .sig and its notes, sets the
 * source, and loads the release image when the source is the release.
 * "Write a file of my own", and in Advanced a board no release carries,
 * start on the local file; simple mode writes the release only.
 */
export function chooseImage(): void {
  dropImage();
  st.localSig = null;
  const b = board();
  if (!b) return;
  const file = st.wiz.task === 'file' || !boardHasRelease(b.id);
  st.source = sourceFor(b, st.wiz.advanced, file ? 'file' : 'release') ?? 'release';
  if (st.source === 'release') {
    st.tag = tagFor(b.id);
    void loadReleaseImage();
  } else ui.render();
}

/** The release list is in: the release image of the board in hand loads; a local file stays. */
export function releasesIn(): void {
  const b = board();
  if (b && st.source === 'release') chooseImage();
  else ui.render();
}

/** A release picked in the select of Advanced: it wins on every board it carries. */
export function pickRelease(tag: string): void {
  st.chosen = tag;
  const b = board();
  st.tag = b ? tagFor(b.id) : null;
  void loadReleaseImage();
}

/** A change of the source in Advanced: the image in hand goes, the release image loads for the release. */
export function setSource(source: 'release' | 'file'): void {
  dropImage();
  st.source = source;
  const b = board();
  if (source === 'release' && b) {
    st.tag = tagFor(b.id);
    void loadReleaseImage();
  } else ui.render();
}

/** Drops the image, its notes and the release in hand. */
export function dropImage(): void {
  ++loadSeq;
  st.image = null;
  st.imageNote = null;
  st.loading = false;
  st.tag = null;
  st.keep = true;
}

/** Advanced off: the release, "Keep settings" on, no picked release and no .sig file. */
export function leaveAdvanced(): void {
  dropImage();
  st.source = 'release';
  st.chosen = null;
  st.localSig = null;
}

/** Loads the Advanced code once; the wizard turns Advanced on only after it is in. */
export async function loadAdvanced(): Promise<AdvApi> {
  st.adv ??= await import('./advanced.ts');
  return st.adv;
}

/** A new load of the image in hand: the number a load holds to see whether it is still the current one. */
export const newLoad = (): number => ++loadSeq;
export const isLoad = (seq: number): boolean => seq === loadSeq;

export function imageVerdict(bytes: Uint8Array, name: string): Note | null {
  const b = board();
  if (!b) return null;
  const v = checkImage(b.chip, bytes, name);
  if (v.ok) return null;
  return { kind: 'bad', text: tr(`bad_${v.reason}` as Key, { x: v.detail }) };
}

let loadSeq = 0;

/**
 * Downloads the release image of the board in hand, checks its size and
 * SHA-256 against the manifest and its chip against the board, and checks
 * its release signature. Waits for the release list: the page calls it
 * again once the list is in.
 */
export async function loadReleaseImage(): Promise<void> {
  const seq = ++loadSeq;
  const b = board();
  st.image = null;
  st.imageNote = null;
  st.loading = false;
  if (!b || !st.releases || st.source !== 'release') { ui.render(); return; }
  const tag = st.tag;
  if (!tag) {
    st.imageNote = { kind: 'info', text: tr('noRelease') };
    ui.render();
    return;
  }
  st.loading = true;
  ui.render();
  const done = (note: Note | null): void => {
    if (seq !== loadSeq) return;
    st.imageNote = note;
    st.loading = false;
    ui.render();
  };
  try {
    const m = await manifestFor(tag);
    const entry = m.images.find((i) => i.board === b.id);
    if (seq !== loadSeq) return;
    if (!entry) { done({ kind: 'info', text: tr('noImage', { tag }) }); return; }
    st.imageNote = { kind: 'info', text: tr('fetching', { file: entry.file }) };
    ui.render();
    let bytes: Uint8Array;
    try {
      const r = await fetch(`${FW}${tag}/${entry.file}`);
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      bytes = new Uint8Array(await r.arrayBuffer());
    } catch (e) {
      done({ kind: 'bad', text: tr('fetchBad', { file: entry.file, x: errText(e) }), retry: true });
      return;
    }
    const sha = await sha256Hex(bytes);
    if (seq !== loadSeq) return;
    if (sha !== entry.sha256 || bytes.length !== entry.size) {
      done({ kind: 'bad', text: tr('hashBad', { got: sha, want: entry.sha256 }), retry: true });
      return;
    }
    const bad = imageVerdict(bytes, entry.file);
    if (bad) { done(bad); return; }
    const sigName = `${entry.file}.sig`;
    const state = sigShown(await verifySignature(RELEASE_KEY_PEM, bytes, await fetchSig(`${FW}${tag}/${sigName}`, sigName)));
    if (seq !== loadSeq) return;
    log(`${sigName}: ${state}`);
    st.image = { bytes, name: entry.file, tag, sha256: sha, format: entry.format, entry, erase: entry.erase ?? [], sig: { state, file: sigName } };
    done(null);
  } catch (e) {
    done({ kind: 'bad', text: tr('noReleases', { x: errText(e) }) });
  }
}

/**
 * The release signature beside a release image: null for a 404 or any other
 * failed download, which the page shows as "no signature".
 */
async function fetchSig(url: string, name: string): Promise<Uint8Array | null> {
  try {
    const r = await fetch(url);
    if (r.status === 404) return null;
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return new Uint8Array(await r.arrayBuffer());
  } catch (e) {
    log(`${name}: ${errText(e)}`);
    return null;
  }
}

// ---- Write plan ----

let planCache: { image: Img; keep: boolean; verdict: PlanVerdict } | null = null;

/** The write plan of the ESP image in hand, or null for an RP2350 board or no image. */
export function espPlan(): PlanVerdict | null {
  const img = st.image;
  const b = board();
  if (!img || !b || b.chip === 'rp2350') return null;
  if (planCache?.image !== img || planCache.keep !== st.keep) {
    planCache = { image: img, keep: st.keep, verdict: planWrite(img.bytes, BOOTLOADER_OFFSET[b.chip], st.keep) };
  }
  return planCache.verdict;
}

export const keptText = (v: PlanVerdict | null): string => (v?.ok ? v.plan.kept.map((k) => `${k.name} ${hex(k.offset)}+${hex(k.size)}`).join(', ') : '');

// ---- Connection ----

export async function dropConn(): Promise<void> {
  const c = st.conn;
  st.conn = null;
  st.connLabel = '';
  if (c?.kind === 'esp') await c.session.close();
  if (c?.kind === 'pico') await c.pb.close();
}

/** Drops the connection and the notes of Connect and Write. */
export function resetConnect(): void {
  void dropConn();
  st.connNote = null;
  st.bootselNote = null;
  st.chipRefused = false;
  st.result = null;
  st.outcome = null;
  st.failedStep = null;
  st.progress = null;
}

export function failNote(e: unknown): Note {
  const step = e instanceof StepError ? e.step : 'connect';
  const stepName = hasKey(`step_${step}`) ? tr(`step_${step}` as Key) : step;
  const pico = linkOf(board()?.chip ?? 'esp32p4') === 'picoboot';
  const hintKey: Key = step === 'connect' ? (pico ? 'try_connect_pico' : 'try_connect_serial')
    : step === 'bootsel' ? 'try_bootsel'
      : step === 'verify' ? 'try_verify'
        : step === 'reset' || step === 'reboot' ? 'try_reset'
          : step === 'exclusive' || step === 'partition' ? 'try_rom' : 'try_transfer';
  const cause = e instanceof StepError ? e.cause : e;
  return { kind: 'bad', text: tr('failed', { step: stepName, x: errText(cause) }), hint: tr(hintKey) };
}

export async function connectSerial(): Promise<void> {
  const b = board();
  if (!b) return;
  let port: SerialPort;
  try { port = await navigator.serial.requestPort(); } catch { return; }
  st.busy = true;
  st.connNote = null;
  st.chipRefused = false;
  ui.render();
  try {
    const session = await connectEsp(port, log);
    if (!chipMatches(b.chip, session.chip)) {
      await session.close();
      st.chipRefused = true;
      st.connNote = { kind: 'bad', text: tr('chipBad', { want: chipLabel(b.chip), got: session.chip ? chipLabel(session.chip) : `${tr('chipUnknown')} (${session.description})` }) };
    } else {
      st.conn = { kind: 'esp', session };
      st.connLabel = session.description;
    }
  } catch (e) {
    st.connNote = failNote(e);
  }
  st.busy = false;
  if (st.conn) ui.connected();
  else ui.render();
}

export async function enterBootsel(): Promise<void> {
  let port: SerialPort;
  try { port = await navigator.serial.requestPort({ filters: IOMCU_PORT_FILTER }); } catch { return; }
  st.busy = true;
  ui.render();
  try {
    await touch1200(port);
    st.bootselNote = { kind: 'ok', text: tr('bootselDone') };
  } catch (e) {
    st.bootselNote = failNote(new StepError('bootsel', e));
  }
  st.busy = false;
  ui.render();
}

export async function connectUsb(): Promise<void> {
  st.connNote = null;
  try {
    const dev = await pickBootrom(navigator.usb);
    if (!dev) { st.connNote = failNote(new StepError('connect', new Error(tr('noBootrom')))); ui.render(); return; }
    st.busy = true;
    ui.render();
    const pb = await Picoboot.open(dev);
    st.conn = { kind: 'pico', pb };
    st.connLabel = `${tr('picoName')}, ${dev.productName ?? ''}`.replace(/, $/, '');
  } catch (e) {
    st.connNote = failNote(e instanceof StepError ? e : new StepError('connect', e));
  }
  st.busy = false;
  if (st.conn) ui.connected();
  else ui.render();
}

// ---- Write ----

/** The phases of a write on the board in hand, in order. */
export function phasesOf(conn: 'esp' | 'pico'): string[] {
  return conn === 'esp' ? ['write', 'verify', 'reset'] : ['exclusive', 'erase', 'write', 'verify', 'reboot'];
}

/** Percentage and text of the progress in hand, in the language of the page. */
export function progressView(): { pct: number; text: string } {
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

/**
 * WRITE: writes the image, compares it with the flash, restarts the board.
 * A failure of the restart after a verify that passed counts as written,
 * with the hint to press RESET. Ends with `ui.written`.
 */
export async function write(): Promise<void> {
  const img = st.image;
  const conn = st.conn;
  if (!img || !conn || st.busy) return;
  st.busy = true;
  st.writing = true;
  st.result = null;
  st.outcome = null;
  st.failedStep = null;
  const t0 = performance.now();
  st.progress = { step: phasesOf(conn.kind)[0]!, done: 0, total: 0, t0 };
  ui.written();
  // The elements are looked up on every call: a render during the write, a
  // language switch for one, replaces them.
  const show = (step: string, done: number, total: number): void => {
    const moved = st.progress?.step !== step;
    st.progress = { step, done, total, t0 };
    if (moved) { ui.render(); return; }
    const view = progressView();
    const bar = document.getElementById('prog') as HTMLProgressElement | null;
    const text = document.getElementById('prog-text');
    if (bar) bar.value = view.pct;
    if (text) text.textContent = view.text;
  };
  let resetHint = '';
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
      try {
        await conn.session.reset();
      } catch (e) {
        // Written and verified: the board only missed its restart. The port is released all the same.
        log(errText(e));
        resetHint = tr('try_reset');
        await conn.session.close();
      }
    } else {
      let plan;
      if (img.format === 'uf2') {
        const uf2 = parseUf2(img.bytes);
        let target = null;
        if (uf2.family !== FAMILY.ABSOLUTE) {
          try { target = await conn.pb.targetPartition(uf2.family); } catch (e) { throw new StepError('partition', e); }
        }
        try { plan = planUf2(uf2, target, img.erase); } catch (e) { throw new StepError('partition', e); }
      } else {
        plan = planBin(img.bytes);
      }
      try {
        await runPlan(conn.pb, plan, (p) => show(p.step, p.done, p.total));
      } catch (e) {
        if (!(e instanceof StepError && e.step === 'reboot')) throw e;
        log(errText(e));
        resetHint = tr('try_reset');
      }
      await conn.pb.close().catch(() => undefined);
    }
    st.conn = null;
    st.connLabel = '';
    const kept = keptText(conn.kind === 'esp' ? espPlan() : null);
    st.outcome = 'ok';
    st.result = {
      kind: 'ok',
      text: tr('done', { file: img.name, s: ((performance.now() - t0) / 1000).toFixed(1) }) + (kept ? ` ${tr('doneKept', { x: kept })}` : ''),
      ...(resetHint ? { hint: resetHint } : {}),
    };
  } catch (e) {
    st.outcome = 'failed';
    st.failedStep = e instanceof StepError ? e.step : st.progress?.step ?? null;
    st.result = failNote(e);
    await dropConn();
  }
  st.busy = false;
  st.writing = false;
  ui.written();
}
