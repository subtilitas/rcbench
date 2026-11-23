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
import { board, errText, esc, log, st, tr, ui } from './state.ts';
import type { Img } from './state.ts';
import { keepFor, sourcesFor } from './tasks.ts';

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
async function localSig(bytes: Uint8Array): Promise<Img['sig']> {
  const sig = st.localSig;
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
      const sig = await localSig(bytes);
      if (!isLoad(seq)) return;
      log(`${file.name}: ${bytes.length} bytes, SHA-256 ${sha}, signature ${sig.state}`);
      st.image = { bytes, name: file.name, tag: null, sha256: sha, format: v.ok ? v.format : 'bin', entry: null, sig };
    }
  } catch (e) {
    if (!isLoad(seq)) return;
    st.imageNote = { kind: 'bad', text: errText(e) };
  }
  st.loading = false;
  ui.render();
}

/** A .sig file picked beside a local file: kept for the next local image, and the image in hand checked again. */
async function loadSigFile(file: File): Promise<void> {
  // A file longer than a signature is read to one byte past the longest: still refused by size.
  st.localSig = { name: file.name, bytes: new Uint8Array(await file.slice(0, SIG_MAX + 1).arrayBuffer()) };
  const img = st.image;
  if (!img || img.entry) { ui.render(); return; }
  const sig = await localSig(img.bytes);
  if (st.image !== img) return;
  log(`${file.name}: ${sig.state}`);
  st.image = { ...img, sig };
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
