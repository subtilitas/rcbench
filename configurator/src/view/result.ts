// The result document: notices, summary, safety, parts, functions, wiring,
// checks, build order, footer. Returns markup; main.ts owns the document.

import type { Catalogue, Check, Lang } from '../../contract.ts';
import { LEGEND, esc, safeBold, t, tx } from '../i18n.ts';
import type { Key } from '../i18n.ts';
import { drawing, sample } from '../draw/svg.ts';
import { reasonText } from './chooser.ts';
import type { View } from './chooser.ts';
import { wiresTable } from './table.ts';

export type Notice = { kind: 'err' | 'note'; text: string };

const KIND: Record<Check['kind'], Key> = { measure: 'kM', unknown: 'kU', note: 'kN' };

/** "7 parts, 9 wires, 2 functions." */
export function summary(view: View): string {
  const f = view.setup.functions.filter((x) => x.enabled).length;
  return t(view.lang, f === 1 ? 'sum1' : 'sum', { p: view.setup.parts.length, w: view.setup.wires.length, f });
}

function head(lang: Lang, url: string, notices: Notice[]): string {
  return `<p class="printonly"><b>${esc(t(lang, 'printHead'))}</b><br>${esc(url)}</p>`
    + `<h2 id="h-result">${esc(t(lang, 'result'))}</h2>`
    + notices.map((n) => `<p class="msg ${n.kind}" role="alert">${esc(n.text)}</p>`).join('');
}

function safety(lang: Lang, catalogue: Catalogue): string {
  return `<div class="safety"><h3>${esc(t(lang, 'safety'))}</h3><ul>`
    + catalogue.safety.map((s) => `<li>${safeBold(tx(lang, s.text))}</li>`).join('')
    + '</ul></div>';
}

const button = (lang: Lang, id: string, key: Key): string =>
  `<button class="act" type="button" id="${id}">${esc(t(lang, key))}</button>`;

const footer = (lang: Lang): string => `<footer>${esc(t(lang, 'foot'))}</footer>`;

function parts(view: View): string {
  const { lang, setup, options } = view;
  let h = `<h3>${esc(t(lang, 'parts'))}</h3><ul class="parts">`;
  for (const r of setup.parts) {
    const p = r.part;
    let name = tx(lang, p.name);
    const variant = p.variantName ? p.variantName.names[options[p.variantName.option]] : undefined;
    if (variant) name += `, ${tx(lang, variant)}`;
    const notRun = p.hardware === 'not-run' || (p.id === 'copro' && options.connection === 'osoyoo');
    const photo = p.photo
      ? `<img class="ph" src="photos/${esc(p.photo.file)}" width="48" height="48" alt="${esc(tx(lang, p.photo.alt))}">`
      : '<span class="ph" aria-hidden="true"></span>';
    h += `<li>${photo}<div><b>${esc(name)}</b>${notRun ? ` <span class="tag">${esc(t(lang, 'nrh'))}</span>` : ''}`
      + `<small>${esc(p.role ? tx(lang, p.role) : '')}</small>`
      + `<span class="why">${esc(reasonText(r, view))}</span></div></li>`;
  }
  return `${h}</ul>`;
}

function does(view: View): string {
  const { lang, catalogue, setup } = view;
  const items = setup.functions.filter((f) => f.enabled).map((f) =>
    `<li>${esc(tx(lang, f.fn.name))}${f.fn.hardware === 'not-run' ? ` <span class="tag">${esc(t(lang, 'nrh'))}</span>` : ''}</li>`);
  return `<h3>${esc(t(lang, 'does'))}</h3><ul class="plain"><li>${esc(tx(lang, catalogue.baseFunction))}</li>${items.join('')}</ul>`;
}

function wiring(view: View): string {
  const { lang, catalogue, setup } = view;
  const legend = LEGEND.map(([cls, key]) => `<li>${sample(cls)}${esc(t(lang, key))}</li>`).join('');
  return `<h3 id="wiring">${esc(t(lang, 'wiring'))}</h3>`
    + `<ul class="legend" aria-label="${esc(t(lang, 'legend'))}">${legend}</ul>`
    + `<p class="hint pan-hint" id="panhint" hidden>${esc(t(lang, 'panHint'))}</p>`
    + `<div class="pan" id="pan" tabindex="0" role="group" aria-label="${esc(t(lang, 'wiring'))}">${drawing(setup, catalogue, lang)}</div>`
    + `<p class="hint">${esc(t(lang, 'padCap'))}</p>`
    + wiresTable(view);
}

function checks(view: View): string {
  const { lang, setup } = view;
  return `<h3>${esc(t(lang, 'checks'))}</h3><ul class="plain">`
    + setup.checks.map((c) => `<li><span class="kind">${esc(t(lang, KIND[c.kind]))}:</span> ${esc(tx(lang, c.text))}</li>`).join('')
    + '</ul>';
}

function order(view: View): string {
  const { lang, catalogue, setup } = view;
  const last = Math.max(...catalogue.stages.map((s) => s.n));
  // A drawn part without pins has no wire that could place it in a stage. It belongs to the last stage.
  const loose = setup.parts
    .map((r) => r.part)
    .filter((p) => p.slot.column !== 'none' && p.id !== 'copro' && p.pins.length === 0)
    .map((p) => tx(lang, p.short));
  let h = `<h3>${esc(t(lang, 'order'))}</h3>`;
  for (const stage of catalogue.stages) {
    const numbers = setup.wires.filter((w) => w.stage === stage.n).map((w) => w.n).sort((a, b) => a - b);
    const names = stage.n === last ? loose : [];
    if (numbers.length === 0 && names.length === 0 && stage.n > 1) continue;
    const lines: string[] = [];
    if (numbers.length > 0) lines.push(esc(t(lang, 'wiresN') + numbers.join(', ')));
    if (names.length > 0) lines.push(esc(t(lang, 'stageParts') + names.join(', ')));
    if (lines.length === 0) lines.push(esc(t(lang, 'noWires')));
    h += `<div class="stage"><b>${esc(tx(lang, stage.title))}</b><br>${lines.join('<br>')}</div>`;
  }
  return h;
}

export function result(view: View, url: string, notices: Notice[]): string {
  const { lang, catalogue } = view;
  return head(lang, url, notices)
    + `<div class="sum"><p>${esc(summary(view))}</p>${button(lang, 'b-copy', 'copy')}${button(lang, 'b-print', 'print')}${button(lang, 'b-reset', 'reset')}</div>`
    + `<p class="linkbox" id="linkbox" hidden><label for="linkfield">${esc(t(lang, 'linkField'))}</label> <input id="linkfield" type="text" readonly></p>`
    + safety(lang, catalogue)
    + parts(view)
    + does(view)
    + wiring(view)
    + checks(view)
    + order(view)
    + footer(lang);
}

/** The result section when the resolver throws: the error, "Start over", and the safety notes. */
export function resultError(lang: Lang, catalogue: Catalogue, url: string): string {
  return head(lang, url, [{ kind: 'err', text: t(lang, 'resolverError') }])
    + `<div class="sum">${button(lang, 'b-reset', 'reset')}</div>`
    + safety(lang, catalogue)
    + footer(lang);
}
