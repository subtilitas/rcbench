// The PDF button of the result view. This module is part of the first load.
// It holds no export code: the first click imports `setup-pdf.ts`, which the
// build emits as a separate chunk.
//
// One export runs at a time. A click during a running export does nothing.
// The finished file starts a download and stays reachable as a link in the
// status line, because a browser may refuse a download that no tap started.

import type { Lang } from '../../contract.ts';
import { esc, t } from '../i18n.ts';
import type { View } from '../view/chooser.ts';

type State =
  | { key: string; kind: 'busy' }
  | { key: string; kind: 'done'; href: string; name: string }
  | { key: string; kind: 'fail' };

let state: State | null = null;
/** Language and link of the last render. */
let shown = '';
let shownLang: Lang = 'en';

const keyOf = (lang: Lang, url: string): string => `${lang} ${url}`;

/** Markup of the status line. `syncPdf` fills it. */
export const PDF_STATUS = '<p class="pdfmsg" id="pdfmsg" role="status"></p>';

function drop(): void {
  if (state?.kind === 'done') URL.revokeObjectURL(state.href);
  state = null;
}

function paint(): void {
  const button = document.getElementById('b-pdf');
  const msg = document.getElementById('pdfmsg');
  if (!button || !msg) return;
  if (state?.kind === 'busy') button.setAttribute('aria-disabled', 'true');
  else button.removeAttribute('aria-disabled');
  let html = '';
  if (state?.kind === 'busy') html = esc(t(shownLang, 'pdfBusy'));
  else if (state?.kind === 'fail') html = esc(t(shownLang, 'pdfFail'));
  else if (state?.kind === 'done') {
    html = `${esc(t(shownLang, 'pdfDone'))} <a id="pdflink" href="${esc(state.href)}" download="${esc(state.name)}">`
      + `${esc(t(shownLang, 'pdfSave', { x: state.name }))}</a>`;
  }
  if (msg.innerHTML !== html) msg.innerHTML = html;
  msg.classList.toggle('err', state?.kind === 'fail');
}

/**
 * Called after every render. A finished file or an error message belongs to
 * one setup in one language; it goes when either changes.
 */
export function syncPdf(lang: Lang, url: string): void {
  shown = keyOf(lang, url);
  shownLang = lang;
  if (state && state.kind !== 'busy' && state.key !== shown) drop();
  paint();
}

/** Makes the PDF of the view and starts its download. Resolves when the export has ended. */
export async function exportPdf(view: View, url: string): Promise<void> {
  if (state?.kind === 'busy') return;
  drop();
  const key = keyOf(view.lang, url);
  state = { key, kind: 'busy' };
  paint();
  try {
    const { setupPdf } = await import('./setup-pdf.ts');
    const made = await setupPdf(view, url, new Date());
    state = { key, kind: 'done', href: URL.createObjectURL(made.blob), name: made.fileName };
  } catch (error) {
    console.warn('PDF export:', error);
    state = { key, kind: 'fail' };
  }
  // The setup or the language changed during the export: the result is of no use.
  if (state.key !== shown) drop();
  paint();
  if (state?.kind === 'done') document.getElementById('pdflink')?.click();
}
