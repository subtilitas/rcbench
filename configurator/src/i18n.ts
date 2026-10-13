// Chrome strings of the page in English and German. Data strings (part names,
// roles, notes, checks, safety, stages) come from the catalogue.

import { VERSION } from '../release.ts';
import type { Lang, SignalClass, Text } from '../contract.ts';

export const RELEASE = VERSION;

const S = {
  title: ['rcbench bench configurator', 'rcbench Prüfstand-Konfigurator'],
  rel: [`For release ${RELEASE} of rcbench`, `Für Release ${RELEASE} von rcbench`],
  langGroup: ['Language / Sprache', 'Language / Sprache'],
  sections: ['Sections', 'Abschnitte'],
  choose: ['What should the bench do?', 'Was soll der Prüfstand tun?'],
  chooseHint: [
    'The functions are grouped by the parts of the bench they need. A servo or an ESC is no part of the bench: it is what the bench tests. A function is active when the setup holds all of its parts and its test object is connected.',
    'Die Funktionen sind nach den Teilen des Prüfstands gruppiert, die sie brauchen. Ein Servo oder ein ESC ist kein Teil des Prüfstands: Er ist das, was der Prüfstand testet. Eine Funktion ist aktiv, wenn der Aufbau alle ihre Teile enthält und ihr Prüfling angeschlossen ist.',
  ],
  gBase: ['Runs on the base setup', 'Läuft auf dem Grundaufbau'],
  gNeeds: ['Needs: ', 'Braucht: '],
  gIn: ['In this setup', 'Im Aufbau'],
  gInOne: ['In this setup: one of these', 'Im Aufbau: eine davon'],
  gVariant: ['{p}: one of {x}.', '{p}: wahlweise {x}.'],
  gAlso: ['Also needs: {x}.', 'Braucht außerdem: {x}.'],
  gOneOf: ['Also needs, by variant: {x}.', 'Braucht außerdem, je nach Variante: {x}.'],
  or: [', or ', ' oder '],
  noPart: ['no further part', 'kein weiteres Teil'],
  baseHint: [
    'These functions are part of every setup. They take no tick.',
    'Diese Funktionen gehören zu jedem Aufbau. Sie werden nicht angekreuzt.',
  ],
  baseTick: [
    'These functions need no further part of the bench. A tick connects the test object.',
    'Diese Funktionen brauchen kein weiteres Teil des Prüfstands. Ein Haken schließt den Prüfling an.',
  ],
  withParts: ['With the parts and test objects of this setup', 'Mit den Teilen und Prüflingen dieses Aufbaus'],
  adds: ['Adds: ', 'Kommt dazu: '],
  stOwn: ['Selected by you.', 'Von dir gewählt.'],
  stInc: ['Included with these parts.', 'Enthalten bei diesen Teilen.'],
  stIncBy: ['Included with these parts: ', 'Enthalten bei diesen Teilen: '],
  stBase: ['Included with the base setup.', 'Im Grundaufbau enthalten.'],
  stConn: ['Active: {x} is connected.', 'Aktiv: {x} ist angeschlossen.'],
  stObj: ['Connects: {x}.', 'Schließt an: {x}.'],
  stVar: ['Not active: needs {v}.', 'Nicht aktiv: braucht {v}.'],
  hNone: ['Needs no further part of the bench.', 'Braucht kein weiteres Teil des Prüfstands.'],
  hVar: ['Variant: {x}.', 'Variante: {x}.'],
  hWithout: ['only without {x}', 'nur ohne {x}'],
  dsOn: [
    '{f} is no longer in your selection. It stays active.',
    '{f} ist nicht mehr in deiner Auswahl. Die Funktion bleibt aktiv.',
  ],
  dsOff: ['{f} is off.', '{f} ist aus.'],
  dsBase: ['The base setup runs it.', 'Der Grundaufbau führt sie aus.'],
  dsConn: ['{x} stays connected.', '{x} bleibt angeschlossen.'],
  outObjects: ['No longer connected: {x}.', 'Nicht mehr angeschlossen: {x}.'],
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
  rmPlain: ['No function goes off and no other part leaves.', 'Keine Funktion entfällt und kein anderes Teil entfällt.'],
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
    'A tick adds a part. Removing a tick removes the part. The page asks first.',
    'Ein Haken fügt ein Teil hinzu. Ohne Haken entfällt das Teil. Die Seite fragt vorher.',
  ],
  variants: ['Variants', 'Varianten'],
  vConn: ['Connections on the coprocessor', 'Anschlüsse am Koprozessor'],
  vPads: ['Solder to the pads', 'An die Pads löten'],
  vOso: ['Osoyoo screw-terminal breakout board', 'Osoyoo-Schraubklemmen-Breakout-Platine'],
  vIna: ['INA228 board', 'INA228-Platine'],
  vMatek: ['MATEK I2C-INA-BM board', 'MATEK-I2C-INA-BM-Platine'],
  vXt: ['INA228 module with XT90 plugs (print: BIFRC V1.1)', 'INA228-Modul mit XT90-Steckern (Aufdruck: BIFRC V1.1)'],
  vServo: ['Servo power', 'Servoversorgung'],
  vSExt: [
    'A: your own supply: a 2S LiPo battery or another supply that matches the servo',
    'A: eigene Versorgung: ein 2S-LiPo-Akku oder eine andere Versorgung, die zum Servo passt',
  ],
  vSBench: [
    'B: from the bench, for a small servo: the 5 V pin, or the V pin of the Osoyoo breakout board',
    'B: vom Prüfstand, für ein kleines Servo: der 5-V-Pin oder der V-Pin der Osoyoo-Breakout-Platine',
  ],
  vPort: ['Servo signal on', 'Servosignal an'],
  vPortOut: ['the standard outputs, GP2', 'den Standardausgängen, GP2'],
  vPortProg: ['the programming port, GP13', 'dem Programmierport, GP13'],
  vEscp: ['ESC power', 'ESC-Versorgung'],
  vBatt: ["A: the ESC's own battery", 'A: eigener Akku des ESC'],
  vPd: ['B: PD mini output, 1 to 20 V, up to 3 A', 'B: Ausgang des PD mini, 1 bis 20 V, bis 3 A'],
  optLocked: ['Not available: {f} needs the other variant.', 'Nicht wählbar: {f} braucht die andere Variante.'],
  nir: ['Not in a release', 'Nicht in einem Release'],
  modelled: ['Modelled part: no product chosen', 'Modelliertes Teil: kein Produkt gewählt'],
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
  tested: ['What you connect and test', 'Was du anschließt und testest'],
  testedHint: [
    'A test object is no part of the bench. The bench runs without it.',
    'Ein Prüfling ist kein Teil des Prüfstands. Der Prüfstand läuft ohne ihn.',
  ],
  testedNone: [
    'No test object is connected. A tick at a function that drives a servo or an ESC connects one.',
    'Kein Prüfling ist angeschlossen. Ein Haken an einer Funktion, die ein Servo oder einen ESC ansteuert, schließt einen an.',
  ],
  usedBy: ['Used by: ', 'Genutzt von: '],
  buy: ['Parts to buy', 'Teile zum Kaufen'],
  buyNote: [
    'Parts list as of release v{r}. Makers change products without notice. Check every part against its check line before ordering. No warranty.',
    'Teileliste mit Stand Release v{r}. Hersteller ändern Produkte ohne Ankündigung. Jedes Teil vor der Bestellung mit seiner Prüfzeile abgleichen. Keine Gewähr.',
  ],
  buyHint: [
    'A part with a tick of your own under "Parts you own" is left out; a part that a function adds stays. A link opens the page of the maker or a web search in a new tab; the page loads nothing from either before a click.',
    'Ein Teil mit eigenem Haken unter „Teile, die vorhanden sind“ fehlt hier; ein Teil, das eine Funktion hinzufügt, bleibt. Ein Link öffnet die Seite des Herstellers oder eine Websuche in einem neuen Tab; vor einem Klick lädt die Seite von dort nichts.',
  ],
  buyNone: ['Nothing to buy: every part of this setup has a tick of your own.', 'Nichts zu kaufen: Jedes Teil dieses Aufbaus hat einen eigenen Haken.'],
  buyCheck: ['Check: ', 'Prüfen: '],
  buyAny: ['Any maker', 'Beliebiger Hersteller'],
  buyPage: ['Maker page', 'Herstellerseite'],
  buySearch: ['Web search', 'Websuche'],
  buyTextHead: ['rcbench parts to buy, release v{r}', 'rcbench Teile zum Kaufen, Release v{r}'],
  bring: ['You bring', 'Bringst du mit'],
  bringHint: [
    'These parts follow the servo or the ESC you test. The page names no product for them.',
    'Diese Teile richten sich nach dem Servo oder ESC, das du testest. Die Seite nennt dafür kein Produkt.',
  ],
  copyText: ['Copy as text', 'Als Text kopieren'],
  copyCsv: ['Copy as CSV', 'Als CSV kopieren'],
  copiedText: ['Text copied.', 'Text kopiert.'],
  copiedCsv: ['CSV copied.', 'CSV kopiert.'],
  buyField: ['Parts list', 'Teileliste'],
  csvQty: ['Quantity', 'Anzahl'],
  csvMaker: ['Maker', 'Hersteller'],
  csvOrder: ['Ordering name', 'Bestellbezeichnung'],
  csvCheck: ['Check', 'Prüfen'],
  csvPage: ['Maker page', 'Herstellerseite'],
  csvSearch: ['Search link', 'Suchlink'],
  power: ['Power sources', 'Stromquellen'],
  powerHint: [
    'Each source with its advantages, drawbacks and unknowns. "In this setup" marks the source the wires of this setup use.',
    'Jede Quelle mit ihren Vorteilen, Nachteilen und Unbekannten. „Im Aufbau“ markiert die Quelle, die die Drähte dieses Aufbaus nutzen.',
  ],
  pwLogic: ['Logic of the bench', 'Logik des Prüfstands'],
  pwFor: ['For the test object: {x}', 'Für den Prüfling: {x}'],
  pwPro: ['Advantage', 'Vorteil'],
  pwCon: ['Drawback', 'Nachteil'],
  pwTo: ['{a} to {b}', '{a} bis {b}'],
  pwUpTo: ['up to {a}', 'bis {a}'],
  pwOr: ['or', 'oder'],
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
  viewGroup: ['View of the wiring', 'Ansicht der Verdrahtung'],
  vPic: ['Picture', 'Bild'],
  vSch: ['Schematic', 'Schema'],
  diaTitle: ['Wiring picture: {p} parts, {w} wires', 'Verdrahtungsbild: {p} Teile, {w} Drähte'],
  diaDesc: [
    'Each wire runs from pin to pin in its colour and carries its number of the table at both ends. The table below carries the same content.',
    'Jeder Draht läuft in seiner Farbe von Pin zu Pin und trägt an beiden Enden seine Nummer aus der Tabelle. Die Tabelle darunter hat denselben Inhalt.',
  ],
  diaUndrawn: ['Not in the picture, see the table: wire {n}.', 'Nicht im Bild, siehe Tabelle: Draht {n}.'],
  diaHint: [
    'Zoom: the buttons, 2 fingers, or Ctrl and the mouse wheel. Move: drag. Keys in the picture: arrows move, + and - zoom, 0 fits. A wire number in the table marks its wire. Wire colours are a legend; they are not the colours of your leads.',
    'Zoom: die Tasten, 2 Finger oder Strg und das Mausrad. Verschieben: ziehen. Tasten im Bild: Pfeile verschieben, + und - zoomen, 0 passt ein. Eine Drahtnummer in der Tabelle markiert ihren Draht. Die Drahtfarben sind eine Legende; sie sind nicht die Farben deiner Leitungen.',
  ],
  diaBox: [
    'Wiring picture. Arrow keys move, plus and minus zoom, 0 fits.',
    'Verdrahtungsbild. Pfeiltasten verschieben, Plus und Minus zoomen, 0 passt ein.',
  ],
  diaTools: ['Picture controls', 'Bildsteuerung'],
  dlTitle: ['Legend of the wiring picture', 'Legende des Verdrahtungsbilds'],
  dlPower: ['power line: carries the current of the ESC or of the servo', 'Leistungsleitung: führt den Strom des ESC oder des Servos'],
  dlSignal: ['signal, and the supply of a board', 'Signal und Versorgung einer Platine'],
  dlCable: ['USB-C cable between 2 sockets', 'USB-C-Kabel zwischen 2 Buchsen'],
  dlPlus: ['red: supply plus', 'Rot: Versorgung Plus'],
  dlMinus: ['black: ground and supply minus', 'Schwarz: GND und Versorgung Minus'],
  diaWire: ['Wire {n}: {a} to {b}', 'Draht {n}: {a} nach {b}'],
  diaZener: ['3.0 V zener diode, band to {a}', '3,0-V-Z-Diode, Ring an {a}'],
  diaComp: ['{s}, band to {a}', '{s}, Ring an {a}'],
  wireMark: ['Wire {n}: mark in the picture', 'Draht {n}: im Bild markieren'],
  zIn: ['Zoom in', 'Vergrößern'],
  zOut: ['Zoom out', 'Verkleinern'],
  zFit: ['Fit', 'Einpassen'],
  mLeft: ['Move left', 'Nach links'],
  mRight: ['Move right', 'Nach rechts'],
  mUp: ['Move up', 'Nach oben'],
  mDown: ['Move down', 'Nach unten'],
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
  devState: [
    'rcbench is under development, and its functions are not verified on hardware.',
    'rcbench ist in Entwicklung, und seine Funktionen sind nicht auf Hardware verifiziert.',
  ],
  foot: [
    `Product and company names belong to their respective owners. They are used to describe compatibility. There is no affiliation. Pins and numbers come from the rcbench repository and its build guide at release ${RELEASE}. This page sends no data and loads nothing from other servers.`,
    `Produkt- und Firmennamen gehören ihren jeweiligen Inhabern. Sie dienen der Beschreibung der Kompatibilität. Es besteht keine Verbindung zu den Herstellern. Pins und Zahlen stammen aus dem rcbench-Repository und seiner Bauanleitung zum Release ${RELEASE}. Diese Seite sendet keine Daten und lädt nichts von anderen Servern.`,
  ],
  footNotice: ['Trademark notice', 'Markenhinweis'],
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
  simHead: ['Heat per resistor', 'Wärme je Widerstand'],
  simCells: ['LiPo pack', 'LiPo-Akku'],
  simOhms: ['Resistance', 'Widerstand'],
  simWatts: ['Rating', 'Belastbarkeit'],
  simWiring: ['Wiring', 'Schaltung'],
  simDelta: ['Delta', 'Dreieck'],
  simStar: ['Star', 'Stern'],
  simHot1: ['Hottest resistor: {a} mA, {w} W, {p} % of the rating.', 'Heißester Widerstand: {a} mA, {w} W, {p} % der Belastbarkeit.'],
  simHot2: ['Hottest 2 resistors: {a} mA, {w} W each, {p} % of the rating.', 'Heißeste 2 Widerstände: je {a} mA, {w} W, {p} % der Belastbarkeit.'],
  simRest2: ['Other 2 resistors: {a} mA, {w} W each.', 'Andere 2 Widerstände: je {a} mA, {w} W.'],
  simRest1: ['Third resistor: no current.', 'Dritter Widerstand: kein Strom.'],
  simOver: [
    'Above the rating of {r}. Choose a higher rating or a higher resistance.',
    'Über der Belastbarkeit von {r}. Eine höhere Belastbarkeit oder einen höheren Widerstand wählen.',
  ],
  simHint: [
    'At {p} % of the rating. The rating holds only up to the ambient temperature that the datasheet of the resistor states, and a resistor near its rating runs hot. Check the derating curve of the datasheet.',
    'Bei {p} % der Belastbarkeit. Die Belastbarkeit gilt nur bis zu der Umgebungstemperatur, die das Datenblatt des Widerstands nennt, und ein Widerstand nahe seiner Belastbarkeit wird heiß. Die Derating-Kurve im Datenblatt prüfen.',
  ],
  simBasis: [
    'Worst case: one commutation step held at {v} ({n}S at 4.2 V per cell). A sensorless ESC cannot commutate on resistors and can hold a step. The ESC also limits the current; this is not modelled.',
    'Schlimmster Fall: ein Kommutierungsschritt, gehalten bei {v} ({n}S zu 4,2 V je Zelle). Ein sensorloser ESC kann an Widerständen nicht kommutieren und kann einen Schritt halten. Auch der ESC begrenzt den Strom; das ist nicht modelliert.',
  ],
  simStarHow: [
    'Star: one end of each resistor goes to A, B or C; the other 3 ends are joined. The drawing shows delta.',
    'Stern: ein Ende jedes Widerstands an A, B oder C; die anderen 3 Enden sind verbunden. Die Zeichnung zeigt das Dreieck.',
  ],
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
