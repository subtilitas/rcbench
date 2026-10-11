// Chrome strings of the page in English and German. Data strings (part names,
// roles, notes, checks, safety, stages) come from the catalogue.

import type { Lang, SignalClass, Text } from '../contract.ts';

export const RELEASE = '0.16.0';

const S = {
  title: ['rcbench bench configurator', 'rcbench Prüfstand-Konfigurator'],
  rel: [`For release ${RELEASE} of rcbench`, `Für Release ${RELEASE} von rcbench`],
  langGroup: ['Language / Sprache', 'Language / Sprache'],
  sections: ['Sections', 'Abschnitte'],
  choose: ['What should the bench do?', 'Was soll der Prüfstand tun?'],
  chooseHint: [
    'The functions are grouped by the parts they need. A function is active when the setup holds all of its parts.',
    'Die Funktionen sind nach den Teilen gruppiert, die sie brauchen. Eine Funktion ist aktiv, wenn der Aufbau alle ihre Teile enthält.',
  ],
  gBase: ['Runs on the base setup', 'Läuft auf dem Grundaufbau'],
  gNeeds: ['Needs: ', 'Braucht: '],
  gIn: ['In this setup', 'Im Aufbau'],
  gVariant: ['{p}: one of {x}.', '{p}: wahlweise {x}.'],
  gAlso: ['Also needs: {x}.', 'Braucht außerdem: {x}.'],
  gOneOf: ['Also needs, by variant: {x}.', 'Braucht außerdem, je nach Variante: {x}.'],
  or: [', or ', ' oder '],
  noPart: ['no further part', 'kein weiteres Teil'],
  adds: ['Adds: ', 'Kommt dazu: '],
  stOwn: ['Selected by you.', 'Von dir gewählt.'],
  stInc: ['Included with these parts.', 'Mit diesen Teilen enthalten.'],
  stBase: ['Included with the base setup.', 'Im Grundaufbau enthalten.'],
  stVar: ['Not active: needs {v}.', 'Nicht aktiv: braucht {v}.'],
  dsOn: [
    '{f} is no longer in your selection. It stays active.',
    '{f} ist nicht mehr in deiner Auswahl. Die Funktion bleibt aktiv.',
  ],
  dsOff: ['{f} is off.', '{f} ist aus.'],
  dsBase: ['The base setup runs it.', 'Der Grundaufbau führt sie aus.'],
  dsStay: [
    'Parts that stay: {x}. Another function or a part you own needs them.',
    'Teile, die bleiben: {x}. Eine andere Funktion oder ein vorhandenes Teil braucht sie.',
  ],
  outParts: ['Parts that leave with it: {x}.', 'Teile, die mit entfallen: {x}.'],
  inParts: ['Parts that come in: {x}.', 'Teile, die dazukommen: {x}.'],
  offFns: ['Functions that go off: {x}.', 'Funktionen, die entfallen: {x}.'],
  onFns: ['Functions that come on: {x}.', 'Funktionen, die dazukommen: {x}.'],
  rm: ['Remove', 'Entfernen'],
  rmName: ['Remove {p}', 'Entfernen: {p}'],
  rmAsk: ['Remove {p}?', '{p} entfernen?'],
  rmOff: ['Functions that go off:', 'Funktionen, die entfallen:'],
  rmNone: ['No function goes off.', 'Keine Funktion entfällt.'],
  rmVar: ['Variant that changes: {x}.', 'Variante, die wechselt: {x}.'],
  rmKept: [
    'Parts that stay, ticked under "Parts you own": {x}.',
    'Teile, die bleiben, angekreuzt unter „Teile, die vorhanden sind“: {x}.',
  ],
  rmYes: ['Remove part and these functions', 'Teil und diese Funktionen entfernen'],
  rmYesPlain: ['Remove part', 'Teil entfernen'],
  rmNo: ['Keep', 'Behalten'],
  rmDone: ['{p} removed.', '{p} entfernt.'],
  own: ['Parts you own', 'Teile, die vorhanden sind'],
  ownN: ['{n} ticked', '{n} angekreuzt'],
  ownHint: [
    'A tick adds a part. Removing a tick removes the part. The page asks first when a function or another part changes with it.',
    'Ein Haken fügt ein Teil hinzu. Ohne Haken entfällt das Teil. Die Seite fragt vorher, wenn sich damit eine Funktion oder ein anderes Teil ändert.',
  ],
  variants: ['Variants', 'Varianten'],
  vConn: ['Connections on the coprocessor', 'Anschlüsse am Koprozessor'],
  vPads: ['Solder to the pads', 'An die Pads löten'],
  vOso: ['Osoyoo screw-terminal breakout board', 'Osoyoo-Schraubklemmen-Breakout-Platine'],
  vIna: ['INA228 board', 'INA228-Platine'],
  vMatek: ['MATEK I2C-INA-BM board', 'MATEK-I2C-INA-BM-Platine'],
  vXt: ['INA228 module with XT90 plugs (print: BIFRC V1.1)', 'INA228-Modul mit XT90-Steckern (Aufdruck: BIFRC V1.1)'],
  vEscp: ['ESC power', 'ESC-Versorgung'],
  vBatt: ["A: the ESC's own battery", 'A: eigener Akku des ESC'],
  vPd: ['B: PD mini output, 1 to 20 V, up to 3 A', 'B: Ausgang des PD mini, 1 bis 20 V, bis 3 A'],
  optLocked: ['Not available: {f} needs the other variant.', 'Nicht wählbar: {f} braucht die andere Variante.'],
  nir: ['Not in a release', 'Nicht in einem Release'],
  result: ['Your bench', 'Dein Prüfstand'],
  sum: ['{p} parts, {w} wires, {f} functions.', '{p} Teile, {w} Drähte, {f} Funktionen.'],
  sum1: ['{p} parts, {w} wires, 1 function.', '{p} Teile, {w} Drähte, 1 Funktion.'],
  copy: ['Copy link', 'Link kopieren'],
  copied: ['Link copied.', 'Link kopiert.'],
  print: ['Print', 'Drucken'],
  pdf: ['Save as PDF', 'Als PDF speichern'],
  pdfBusy: ['The PDF is being made.', 'Das PDF wird erstellt.'],
  pdfDone: ['The PDF is made.', 'Das PDF ist erstellt.'],
  pdfSave: ['Save {x}', '{x} speichern'],
  pdfFail: ['The PDF was not made. The setup on this page is unchanged. Try again, or use Print.', 'Das PDF wurde nicht erstellt. Der Aufbau auf dieser Seite ist unverändert. Erneut versuchen oder Drucken verwenden.'],
  reset: ['Start over', 'Neu beginnen'],
  safety: ['Safety', 'Sicherheit'],
  parts: ['Parts', 'Teile'],
  always: ['Always needed', 'Immer nötig'],
  neededBy: ['Needed by: ', 'Nötig für: '],
  youOwn: ['You own it', 'Vorhanden'],
  lockRule: ['It stays while the part that needs it is in the setup.', 'Es bleibt, solange das Teil, das es braucht, im Aufbau ist.'],
  does: ['This setup does', 'Dieser Aufbau kann'],
  wiring: ['Wiring', 'Verdrahtung'],
  legend: ['Line patterns', 'Linienmuster'],
  panHint: [
    'The drawing is wider than this screen. Drag it sideways. The table below lists every wire.',
    'Die Zeichnung ist breiter als der Bildschirm. Seitlich ziehen. Die Tabelle darunter nennt jeden Draht.',
  ],
  drawAlt: [
    'Wiring drawing, {w} wires. The table below carries the same content.',
    'Verdrahtungszeichnung, {w} Drähte. Die Tabelle darunter hat denselben Inhalt.',
  ],
  padCap: [
    'Coprocessor pads: 1 to 20 down the left side, 40 to 21 down the right side. The numbers are those printed on the module. Line colours are a legend; they are not the colours of your leads.',
    'Pads des Koprozessors: 1 bis 20 links von oben nach unten, 40 bis 21 rechts von oben nach unten. Die Nummern stehen auf dem Modul. Die Linienfarben sind eine Legende, nicht die Farben der Leitungen.',
  ],
  tabCap: [
    'Pin-to-pin table, {w} wires. A wire keeps its number in every selection, so numbers are skipped.',
    'Pin-zu-Pin-Tabelle, {w} Drähte. Ein Draht behält seine Nummer in jeder Auswahl; deshalb fehlen Nummern.',
  ],
  thN: ['No.', 'Nr.'],
  thFrom: ['From', 'Von'],
  thTo: ['To', 'Nach'],
  thSig: ['Signal', 'Signal'],
  thNote: ['Note', 'Hinweis'],
  checks: ['Check before you connect', 'Vor dem Anschließen prüfen'],
  kM: ['Measure', 'Messen'],
  kU: ['Unknown', 'Unbekannt'],
  kN: ['Note', 'Hinweis'],
  order: ['Build order', 'Reihenfolge'],
  wiresN: ['Wires: ', 'Drähte: '],
  noWires: ['No wires.', 'Keine Drähte.'],
  stageParts: ['Parts: ', 'Teile: '],
  bChoose: ['Choose', 'Auswahl'],
  bWiring: ['Wiring', 'Verdrahtung'],
  bSub: ['{p} parts, {w} wires', '{p} Teile, {w} Drähte'],
  badV: [
    'This link was made by another version of the page. The page shows the basic bench instead.',
    'Dieser Link stammt von einer anderen Version der Seite. Die Seite zeigt stattdessen den Grundaufbau.',
  ],
  badId: [
    'This link names items the page does not know: {x}. They are left out.',
    'Dieser Link nennt Einträge, die die Seite nicht kennt: {x}. Sie fehlen im Aufbau.',
  ],
  nrh: ['Not run on hardware', 'Nicht auf Hardware gelaufen'],
  foot: [
    `Product and company names belong to their respective owners. They are used to describe compatibility. There is no affiliation. Pins and numbers come from the rcbench repository and its build guide at release ${RELEASE}. This page sends no data and loads nothing from other servers.`,
    `Produkt- und Firmennamen gehören ihren jeweiligen Inhabern. Sie dienen der Beschreibung der Kompatibilität. Es besteht keine Verbindung zu den Herstellern. Pins und Zahlen stammen aus dem rcbench-Repository und seiner Bauanleitung zum Release ${RELEASE}. Diese Seite sendet keine Daten und lädt nichts von anderen Servern.`,
  ],
  printHead: [`rcbench bench configurator, release ${RELEASE}`, `rcbench Prüfstand-Konfigurator, Release ${RELEASE}`],
  copro: ['Coprocessor', 'Koprozessor'],
  coproSub: ['RP2350-CAN', 'RP2350-CAN'],
  coproSubOso: ['on Osoyoo breakout', 'auf Osoyoo-Breakout'],
  inaSubMatek: ['MATEK I2C-INA-BM', 'MATEK I2C-INA-BM'],
  inaSubXt: ['XT90, header: L A V G 5', 'XT90, Leiste: L A V G 5'],
  pad: ['pad', 'Pad'],
  term: ['terminal', 'Klemme'],
  between: ['between {a} and {b}', 'zwischen {a} und {b}'],
  screw: ['screw terminal', 'Schraubklemme'],
  nc: ['not connected', 'nicht angeschlossen'],
  linkField: ['Link', 'Link'],
  resolverError: [
    'The page could not work out this selection. Start over.',
    'Die Seite konnte diese Auswahl nicht auflösen. Neu beginnen.',
  ],
  noscript: [
    'This page needs JavaScript. The build guide is on the releases page of the rcbench repository.',
    'Diese Seite braucht JavaScript. Die Bauanleitung liegt auf der Release-Seite des rcbench-Repositorys.',
  ],
  padUnconfirmed: ['pad not confirmed', 'Pad nicht bestätigt'],
  legCanh: ['CAN H', 'CAN H'],
  legCanl: ['CAN L', 'CAN L'],
  legGnd: ['GND', 'GND'],
  legHb: ['heartbeat', 'Heartbeat'],
  legOut: ['output signal', 'Ausgangssignal'],
  legUart: ['UART', 'UART'],
  legSda: ['I2C SDA', 'I2C SDA'],
  legScl: ['I2C SCL', 'I2C SCL'],
  legLogic: ['3.3 V or 5 V supply', 'Versorgung 3,3 V oder 5 V'],
  legPower: ['servo or ESC power', 'Servo- oder ESC-Versorgung'],
  legPhase: ['motor phase, tap', 'Motorphase, Abgriff'],
  legNc: ['no wire: open or strap on the board', 'kein Draht: offen oder Brücke auf der Platine'],
} as const satisfies Record<string, readonly [string, string]>;

export type Key = keyof typeof S;

export const STRINGS: Record<Key, readonly [string, string]> = S;

export const LEGEND: ReadonlyArray<readonly [SignalClass, Key]> = [
  ['canh', 'legCanh'], ['canl', 'legCanl'], ['gnd', 'legGnd'], ['hb', 'legHb'], ['out', 'legOut'], ['uart', 'legUart'],
  ['sda', 'legSda'], ['scl', 'legScl'], ['logic', 'legLogic'], ['power', 'legPower'], ['phase', 'legPhase'], ['nc', 'legNc'],
];

/** Chrome string `key` in `lang`, with `{name}` placeholders filled from `vars`. */
export function t(lang: Lang, key: Key, vars: Record<string, string | number> = {}): string {
  let s: string = S[key][lang === 'de' ? 1 : 0];
  for (const name of Object.keys(vars)) s = s.split(`{${name}}`).join(String(vars[name]));
  return s;
}

/** Data string in `lang`. */
export function tx(lang: Lang, text: Text): string {
  return lang === 'de' ? text.de : text.en;
}

/** Start language without a hash key: German for a German browser, else English. */
export function browserLang(language: string | undefined): Lang {
  return typeof language === 'string' && language.toLowerCase().startsWith('de') ? 'de' : 'en';
}

const ESCAPES: Record<string, string> = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

/** Escape for HTML text and double-quoted attribute values. */
export function esc(value: string | number): string {
  return String(value).replace(/[&<>"']/g, (c) => ESCAPES[c] as string);
}

/**
 * Safety text: every character escaped, then `<b>` and `</b>` restored. No
 * other markup of the data reaches the page.
 */
export function safeBold(value: string): string {
  return esc(value).replace(/&lt;(\/?)b&gt;/g, '<$1b>');
}
