// Strings that only the PDF export shows, in English and German. Strings the
// page shows as well come from `src/i18n.ts`.

import type { Lang, SignalClass } from '../../contract.ts';

const P = {
  docTitle: [
    'rcbench setup',
    'rcbench-Aufbau',
  ],
  release: ['Firmware release of the catalogue: {r}', 'Firmware-Release des Katalogs: {r}'],
  date: ['Date: {d}', 'Datum: {d}'],
  link: ['Link to this setup: ', 'Link zu diesem Aufbau: '],
  quantity: [
    'Each entry is needed once. A number in the item name specifies the quantity.',
    'Jeder Eintrag wird einmal benötigt. Eine Zahl in der Bezeichnung gibt die Stückzahl an.',
  ],
  noParts: ['No parts.', 'Keine Teile.'],
  buyHint: [
    'Parts selected under "Parts you own" are omitted. Rows marked "Have it" appear under "You have". Manufacturer pages and web searches are linked.',
    'Teile, die du unter „Vorhandene Teile“ ausgewählt hast, werden ausgelassen. Zeilen mit „Habe ich“ stehen unter „Bereits vorhanden“. Herstellerseiten und Websuchen sind verlinkt.',
  ],
  notEnabled: ['Not enabled', 'Nicht aktiviert'],
  allEnabled: ['Every function of the catalogue is enabled.', 'Jede Funktion des Katalogs ist aktiviert.'],
  noneEnabled: ['No function is enabled.', 'Keine Funktion ist aktiviert.'],
  missing: ['Missing: ', 'Fehlt: '],
  needsVariant: ['Needs the variant: ', 'Braucht die Variante: '],
  notConnected: ['Not connected: ', 'Nicht angeschlossen: '],
  thNet: ['Net', 'Netz'],
  thLine: ['Line', 'Linie'],
  noChecks: ['No checks.', 'Keine Prüfungen.'],
  page: ['Page {x} of {y}', 'Seite {x} von {y}'],
} as const satisfies Record<string, readonly [string, string]>;

export type PdfKey = keyof typeof P;

export const PDF_STRINGS: Record<PdfKey, readonly [string, string]> = P;

/** Colour and pattern of the line of each signal class, in words. */
export const LINE_WORDS: Record<SignalClass, readonly [string, string]> = {
  canh: ['orange, solid', 'orange, durchgezogen'],
  canl: ['blue, dashed', 'blau, gestrichelt'],
  gnd: ['grey, solid, thin', 'grau, durchgezogen, dünn'],
  hb: ['magenta, dash-dot', 'magenta, Strich-Punkt'],
  out: ['violet, long dashes', 'violett, lange Striche'],
  uart: ['teal, dotted', 'petrol, gepunktet'],
  sda: ['green, short dashes', 'grün, kurze Striche'],
  scl: ['brown, dash-dot-dot', 'braun, Strich-Punkt-Punkt'],
  logic: ['pink, solid', 'pink, durchgezogen'],
  power: ['red, solid, thick', 'rot, durchgezogen, dick'],
  phase: ['black, dotted, wide gaps', 'schwarz, gepunktet, weite Lücken'],
  nc: ['grey, short dashes, thin', 'grau, kurze Striche, dünn'],
};

export function p(lang: Lang, key: PdfKey, vars: Record<string, string | number> = {}): string {
  let s: string = P[key][lang === 'de' ? 1 : 0];
  for (const name of Object.keys(vars)) s = s.split(`{${name}}`).join(String(vars[name]));
  return s;
}

export const lineWords = (lang: Lang, cls: SignalClass): string => LINE_WORDS[cls][lang === 'de' ? 1 : 0];
