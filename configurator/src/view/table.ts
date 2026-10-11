// The pin-to-pin table: one row per wire of the setup, in ascending number.

import { esc, t, tx } from '../i18n.ts';
import { endName, targetName } from '../names.ts';
import type { NameCtx } from '../names.ts';
import { sample } from '../draw/svg.ts';
import { swatch } from '../diagram/picture.ts';
import { colourOf } from '../../drawings/wires.ts';
import type { View } from './chooser.ts';

export function wiresTable(view: View): string {
  const { lang, catalogue, setup, options } = view;
  const ctx: NameCtx = { lang, catalogue, options };
  const head = (['thN', 'thFrom', 'thTo', 'thSig', 'thNote'] as const)
    .map((k) => `<th scope="col">${esc(t(lang, k))}</th>`).join('');
  let rows = '';
  for (const w of [...setup.wires].sort((p, q) => p.n - q.n)) {
    const to = w.b === null ? `<b>${esc(targetName(w, ctx))}</b>` : esc(targetName(w, ctx));
    // The number is a button: it marks the wire in the wiring picture.
    rows += `<tr><td class="n"><button type="button" class="wn" data-wire="${w.n}" aria-pressed="false" aria-label="${esc(t(lang, 'wireMark', { n: w.n }))}"><b>${w.n}</b>${swatch(colourOf(w))}</button></td>`
      + `<td data-l="${esc(t(lang, 'thFrom'))}">${esc(endName(w.a, ctx))}</td>`
      + `<td data-l="${esc(t(lang, 'thTo'))}">${to}</td>`
      + `<td class="sig">${sample(w.cls)}${esc(tx(lang, w.signal))}</td>`
      + `<td class="note">${w.note ? esc(tx(lang, w.note)) : ''}</td></tr>`;
  }
  return `<table class="wires"><caption id="wires-cap">${esc(t(lang, 'tabCap', { w: setup.wires.length }))}</caption>`
    + `<thead><tr>${head}</tr></thead><tbody>${rows}</tbody></table>`;
}
