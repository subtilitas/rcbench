// Advanced mode on Check: the release select, the local file and its .sig
// file, and the "Keep settings" checkbox. The page loads this module on the
// first switch to Advanced (actions.ts `loadAdvanced`), so simple mode does
// not carry it in its first load.

import { checkImage } from './boards.ts';
import type { Board } from './boards.ts';
import { sha256Hex } from './digest.ts';
import { imageVerdict, isLoad, newLoad, pickRelease, setSource } from './actions.ts';
import { tAdv } from './i18n_adv.ts';
import type { AdvKey } from './i18n_adv.ts';
import { RELEASE_KEY_PEM } from './release_key.ts';
import { releasesFor } from './release.ts';
import { SIG_MAX, sigShown, verifySignature } from './signature.ts';
import { otherSlots, planUf2 } from './rp2350.ts';
import { board, errText, esc, log, st, tr, ui } from './state.ts';
import type { Img } from './state.ts';
import { keepFor, sourcesFor } from './tasks.ts';
import { FAMILY, parseUf2 } from './uf2.ts';

export const ta = (key: AdvKey, vars?: Record<string, string | number>): string => tAdv(st.lang, key, vars);

const dis = (off: boolean): string => (off || st.busy ? ' disabled' : '');

/** Release select, local file and .sig file, above the rows of Check. */
export function renderSource(b: Board): string {
  const offered = sourcesFor(b, true);
  const list = st.releases ? releasesFor(b.id, st.releases, st.manifests) : [];
  const rel = st.source === 'release';
  let html = '<div class="src" id="source">';
  html += `<label class="opt"><input type="radio" name="src" value="release"${rel ? ' checked' : ''}`
    + `${dis(!offered.includes('release') || !list.length)}><span>${esc(ta('srcRelease'))}</span></label>`;
  html += `<select id="tag" aria-label="${esc(ta('srcRelease'))}"${dis(!rel || !list.length)}>`;
  for (const r of list) {
    html += `<option value="${esc(r.tag)}"${r.tag === st.tag ? ' selected' : ''}>${esc(`${r.tag}, ${tr(r.kind)}, ${r.published.slice(0, 10)}`)}</option>`;
  }
  html += '</select>';
  if (st.releases === null && !st.releasesError) html += `<p class="muted small">${esc(ta('srcLoading'))}</p>`;
  html += `<label class="opt"><input type="radio" name="src" value="file"${rel ? '' : ' checked'}${dis(false)}>`
    + `<span>${esc(ta('srcFile'))}</span></label>`;
  html += `<input type="file" id="file" accept=".bin,.uf2"${dis(rel)}>`;
  html += `<p class="muted small">${esc(ta('fileHint'))}</p>`;
  html += `<label class="sigpick small" for="sigfile">${esc(ta('sigFile'))}</label>`
    + `<input type="file" id="sigfile" accept=".sig"${dis(rel)}>`
    + `${st.localSig ? ` <span class="muted small" id="sig-picked">${esc(st.localSig.name)}</span>` : ''}`;
  return `${html}</div>`;
}

/** The "Keep settings" checkbox; its plan and notes follow it on Check. */
export function keepBox(): string {
  return `<label class="opt"><input type="checkbox" id="keep"${st.keep ? ' checked' : ''}${dis(false)}>`
    + `<span><b>${esc(ta('keep'))}</b> <small>${esc(ta('keepSub'))}</small></span></label>`;
}

/** The signature of a local image: checked only against a .sig file the visitor picked. */
async function localSig(bytes: Uint8Array, sig = st.localSig): Promise<Img['sig']> {
  if (!sig) return { state: 'unchecked', file: '' };
  return { state: sigShown(await verifySignature(RELEASE_KEY_PEM, bytes, sig.bytes)), file: sig.name };
}

/** Reads a local file, checks it against the board's chip and the picked .sig file. */
async function loadFile(file: File): Promise<void> {
  const b = board();
  if (!b) return;
  const seq = newLoad();
  st.image = null;
  st.imageNote = null;
  st.loading = true;
  ui.render();
  try {
    const bytes = new Uint8Array(await file.arrayBuffer());
    const sha = await sha256Hex(bytes);
    if (!isLoad(seq)) return;
    const bad = imageVerdict(bytes, file.name);
    if (bad) {
      st.imageNote = bad;
    } else {
      const v = checkImage(b.chip, bytes, file.name);
      // A .sig file picked while the check runs replaces the one it ran with:
      // the verdict is the one of the .sig file selected when the image lands.
      let sigUsed = st.localSig;
      let sig = await localSig(bytes, sigUsed);
      while (isLoad(seq) && st.localSig !== sigUsed) {
        sigUsed = st.localSig;
        sig = await localSig(bytes, sigUsed);
      }
      if (!isLoad(seq)) return;
      log(`${file.name}: ${bytes.length} bytes, SHA-256 ${sha}, signature ${sig.state}`);
      const format = v.ok ? v.format : 'bin';
      // A local install UF2 erases the other IOmcu slot as a release install does,
      // so the boot ROM starts the written image, not an older or newer one.
      let erase: Img['erase'] = [];
      if (b.chip === 'rp2350' && format === 'uf2') {
        const uf2 = parseUf2(bytes);
        if (uf2.family === FAMILY.ABSOLUTE) erase = otherSlots(planUf2(uf2, null).writes);
      }
      st.image = { bytes, name: file.name, tag: null, sha256: sha, format, entry: null, erase, sig };
    }
  } catch (e) {
    if (!isLoad(seq)) return;
    st.imageNote = { kind: 'bad', text: errText(e) };
  }
  st.loading = false;
  ui.render();
}

let sigSeq = 0;

/**
 * A .sig file picked beside a local file: kept for the next local image, and
 * the image in hand checked again. The image's signature shows `pending`
 * (WRITE disabled) from the pick until the verdict for this .sig file lands,
 * so no verdict of an earlier .sig file stays shown. A later pick wins: the
 * result of an earlier one that ends after it is dropped.
 */
async function loadSigFile(file: File): Promise<void> {
  const seq = ++sigSeq;
  const shown = st.image;
  if (shown && !shown.entry) {
    st.image = { ...shown, sig: { state: 'pending', file: file.name } };
    ui.render();
  }
  // A file longer than a signature is read to one byte past the longest: still refused by size.
  const bytes = new Uint8Array(await file.slice(0, SIG_MAX + 1).arrayBuffer());
  if (seq !== sigSeq) return;
  st.localSig = { name: file.name, bytes };
  const img = st.image;
  if (!img || img.entry) { ui.render(); return; }
  const sig = await localSig(img.bytes);
  // Only the latest pick lands, and only on the file it was checked against:
  // an earlier pick may have replaced st.image with its verdict meanwhile.
  const now = st.image;
  if (seq !== sigSeq || !now || now.bytes !== img.bytes) return;
  log(`${file.name}: ${sig.state}`);
  st.image = { ...now, sig };
  ui.render();
}

/** A change of an input of the source part or of the checkbox. */
export function onChange(el: HTMLInputElement): void {
  if (st.busy) return;
  if (el.name === 'src') setSource(el.value === 'file' ? 'file' : 'release');
  else if (el.id === 'tag') pickRelease(el.value);
  else if (el.id === 'keep') { st.keep = keepFor(st.wiz.advanced, el.checked); ui.render(); }
  else if (el.id === 'file' && el.files?.[0]) void loadFile(el.files[0]);
  else if (el.id === 'sigfile' && el.files?.[0]) void loadSigFile(el.files[0]);
}
