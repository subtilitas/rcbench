// Chrome strings of the page in English and German. Data strings (part names,
// roles, notes, checks, safety, stages) come from the catalogue.

import { VERSION } from '../release.ts';
import type { Lang, SignalClass, Text } from '../contract.ts';

export const RELEASE = VERSION;

const S = {
  title: [
    'rcbench configurator',
    'rcbench-Konfigurator',
  ],
  rel: [`For release ${RELEASE} of rcbench`, `Für Release ${RELEASE} von rcbench`],
  langGroup: ['Language / Sprache', 'Language / Sprache'],
  sections: ['Sections', 'Abschnitte'],
  choose: ['What should the bench do?', 'Was soll der Prüfstand tun?'],
  chooseHint: [
    'Functions are grouped by the bench parts they need. The servo or ESC is the device under test. A function is available when the setup includes all required parts and its test device.',
    'Die Funktionen sind nach den benötigten Teilen des Prüfstands gruppiert. Das Servo oder der ESC ist der Prüfling. Eine Funktion ist verfügbar, wenn alle benötigten Teile und ihr Prüfling im Aufbau enthalten sind.',
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
    'These functions are included in every setup. No selection is needed.',
    'Diese Funktionen gehören zu jedem Aufbau. Du musst sie nicht auswählen.',
  ],
  baseTick: [
    'These functions need no additional bench parts. Select a function to include its test device in the setup.',
    'Diese Funktionen brauchen keine zusätzlichen Teile des Prüfstands. Wähle eine Funktion aus, um ihren Prüfling in den Aufbau aufzunehmen.',
  ],
  withParts: ['With the parts and test objects of this setup', 'Mit den Teilen und Prüflingen dieses Aufbaus'],
  adds: ['Adds: ', 'Kommt dazu: '],
  stOwn: ['Selected by you.', 'Von dir gewählt.'],
  stInc: [
    'Included with these parts.',
    'Mit diesen Teilen enthalten.',
  ],
  stIncBy: [
    'Included with these parts: ',
    'Mit diesen Teilen enthalten: ',
  ],
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
    'Parts kept: {x}. Another function or a part you own still needs them.',
    'Diese Teile bleiben: {x}. Eine andere Funktion oder ein vorhandenes Teil benötigt sie weiterhin.',
  ],
  outParts: [
    'Parts removed: {x}.',
    'Entfernte Teile: {x}.',
  ],
  inParts: [
    'Parts added: {x}.',
    'Hinzugefügte Teile: {x}.',
  ],
  offFns: [
    'Functions disabled: {x}.',
    'Deaktivierte Funktionen: {x}.',
  ],
  onFns: [
    'Functions enabled: {x}.',
    'Aktivierte Funktionen: {x}.',
  ],
  rm: ['Remove', 'Entfernen'],
  rmName: ['Remove {p}', 'Entfernen: {p}'],
  rmAsk: ['Remove {p}?', '{p} entfernen?'],
  rmOff: [
    'Functions disabled:',
    'Deaktivierte Funktionen:',
  ],
  rmNone: [
    'All functions remain available.',
    'Alle Funktionen bleiben verfügbar.',
  ],
  rmPlain: [
    'All functions and other parts remain in the setup.',
    'Alle Funktionen und übrigen Teile bleiben im Aufbau.',
  ],
  rmVar: ['Variant that changes: {x}.', 'Variante, die wechselt: {x}.'],
  rmKept: [
    'Parts kept because you selected them under "Parts you own": {x}.',
    'Diese Teile bleiben, weil du sie unter „Vorhandene Teile“ ausgewählt hast: {x}.',
  ],
  rmYes: ['Remove part and these functions', 'Teil und diese Funktionen entfernen'],
  rmYesPlain: ['Remove part', 'Teil entfernen'],
  rmNo: ['Keep', 'Behalten'],
  rmDone: ['{p} removed.', '{p} entfernt.'],
  own: [
    'Parts you own',
    'Vorhandene Teile',
  ],
  ownN: ['{n} ticked', '{n} angekreuzt'],
  ownHint: [
    'Select a part to add it. Clear its checkbox to remove it; the page asks for confirmation first.',
    'Wähle ein Teil aus, um es hinzuzufügen. Entferne den Haken, um es aus dem Aufbau zu nehmen. Die Seite fragt vorher nach.',
  ],
  variants: ['Variants', 'Varianten'],
  vConn: ['Connections on the IOmcu', 'Anschlüsse am IOmcu'],
  vPads: ['Solder to the pads', 'An die Pads löten'],
  vOso: ['Osoyoo screw-terminal breakout board', 'Osoyoo-Schraubklemmen-Breakout-Platine'],
  vIna: ['INA228 board', 'INA228-Platine'],
  vMatek: ['MATEK I2C-INA-BM board', 'MATEK-I2C-INA-BM-Platine'],
  vXt: ['INA228 module with XT90 plugs (print: BIFRC V1.1)', 'INA228-Modul mit XT90-Steckern (Aufdruck: BIFRC V1.1)'],
  vServo: ['Servo power', 'Servoversorgung'],
  vSExt: [
    'A: an external supply: a 2S LiPo battery or another source suitable for the servo',
    'A: externe Versorgung: ein 2S-LiPo-Akku oder eine andere geeignete Spannungsquelle für das Servo',
  ],
  vSBench: [
    'B: bench power for a small servo: the 5V pin or the V pin of the Osoyoo breakout board',
    'B: Prüfstandsversorgung für ein kleines Servo: der 5V-Pin oder der V-Pin der Osoyoo-Breakout-Platine',
  ],
  vPort: ['Servo signal on', 'Servosignal an'],
  vPortOut: ['the standard outputs, GPIO 2', 'den Standardausgängen, GPIO 2'],
  vPortProg: ['the programming port, GPIO 13', 'dem Programmierport, GPIO 13'],
  vEscp: ['ESC power', 'ESC-Versorgung'],
  vBatt: ["A: the ESC's own battery", 'A: eigener Akku des ESC'],
  vPd: [
    'B: PD mini output, 1 to 20V, up to 3A',
    'B: Ausgang des PD mini, 1 bis 20V, bis 3A',
  ],
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
  pdfBusy: [
    'Creating the PDF.',
    'PDF wird erstellt.',
  ],
  pdfDone: [
    'PDF ready.',
    'PDF ist fertig.',
  ],
  pdfSave: ['Save {x}', '{x} speichern'],
  pdfFail: [
    'The PDF could not be created. Your setup is unchanged. Try again or select Print.',
    'Das PDF konnte nicht erstellt werden. Dein Aufbau ist unverändert. Versuche es erneut oder wähle Drucken.',
  ],
  reset: ['Start over', 'Neu beginnen'],
  safety: ['Safety', 'Sicherheit'],
  parts: ['Parts', 'Teile'],
  tested: [
    'Devices to connect and test',
    'Prüflinge anschließen und testen',
  ],
  testedHint: [
    'These are the devices you test. The bench itself can run without them.',
    'Diese Geräte prüfst du mit dem Prüfstand. Der Prüfstand selbst kann auch ohne sie laufen.',
  ],
  testedNone: [
    'No test device is included. Select a function that drives a servo or ESC to add one.',
    'Es ist kein Prüfling ausgewählt. Wähle eine Funktion aus, die ein Servo oder einen ESC ansteuert, um einen hinzuzufügen.',
  ],
  usedBy: ['Used by: ', 'Genutzt von: '],
  buy: ['Parts to buy', 'Teile zum Kaufen'],
  buyNote: [
    'Parts list for release v{r}. Manufacturers may change products without notice. Before ordering, compare each part with its listed requirements. No warranty.',
    'Teileliste für Release v{r}. Hersteller können Produkte ohne Ankündigung ändern. Vergleiche vor der Bestellung jedes Teil mit den aufgeführten Anforderungen. Keine Gewähr.',
  ],
  buyHint: [
    'Parts selected under "Parts you own" are omitted. Select "Have it" to move a row to "You have"; the setup link saves this choice. Manufacturer and search links open in a new tab. The page contacts those sites only when you open a link.',
    'Teile, die du unter „Vorhandene Teile“ ausgewählt hast, werden hier ausgelassen. Mit „Habe ich“ verschiebst du eine Zeile nach „Bereits vorhanden“. Der Link zum Aufbau speichert diese Auswahl. Hersteller- und Suchlinks öffnen sich in einem neuen Tab. Erst beim Öffnen eines Links wird die jeweilige Seite aufgerufen.',
  ],
  buyNone: ['Nothing to buy: you have every part of this setup.', 'Nichts zu kaufen: Jedes Teil dieses Aufbaus ist vorhanden.'],
  haveIt: ['Have it', 'Habe ich'],
  haveName: ['Have it: {x}', 'Habe ich: {x}'],
  have: [
    'You have',
    'Bereits vorhanden',
  ],
  haveOn: [
    'Moved to "You have".',
    'Nach „Bereits vorhanden“ verschoben.',
  ],
  haveOff: ['Back under "Parts to buy".', 'Zurück unter „Teile zum Kaufen“.'],
  buyCheck: ['Check: ', 'Prüfen: '],
  buyAny: ['Any maker', 'Beliebiger Hersteller'],
  buyPage: ['Maker page', 'Herstellerseite'],
  buySearch: ['Web search', 'Websuche'],
  buyTextHead: ['rcbench parts to buy, release v{r}', 'rcbench Teile zum Kaufen, Release v{r}'],
  bring: [
    'Parts you supply',
    'Von dir bereitzustellende Teile',
  ],
  bringHint: [
    'Choose these parts to suit the servo or ESC you plan to test. The page does not specify a product.',
    'Wähle diese Teile passend zu dem Servo oder ESC, das du prüfen möchtest. Die Seite gibt dafür kein bestimmtes Produkt vor.',
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
    'Each power source lists its advantages, drawbacks and unknowns. "In this setup" marks the sources used by the selected wiring.',
    'Zu jeder Spannungsquelle sind Vorteile, Nachteile und offene Fragen aufgeführt. „Im Aufbau“ kennzeichnet die Quellen, die in der gewählten Verdrahtung verwendet werden.',
  ],
  pwLogic: [
    'Bench electronics',
    'Prüfstandselektronik',
  ],
  pwFor: ['For the test object: {x}', 'Für den Prüfling: {x}'],
  pwPro: ['Advantage', 'Vorteil'],
  pwCon: ['Drawback', 'Nachteil'],
  pwTo: ['{a} to {b}', '{a} bis {b}'],
  pwUpTo: ['up to {a}', 'bis {a}'],
  pwOr: ['or', 'oder'],
  always: ['Always needed', 'Immer nötig'],
  neededBy: ['Needed by: ', 'Nötig für: '],
  youOwn: ['You own it', 'Vorhanden'],
  lockRule: [
    'This part stays while another selected part needs it.',
    'Dieses Teil bleibt, solange ein anderes ausgewähltes Teil es benötigt.',
  ],
  does: ['This setup does', 'Dieser Aufbau kann'],
  wiring: ['Wiring', 'Verdrahtung'],
  legend: ['Line patterns', 'Linienmuster'],
  panHint: [
    'The drawing is wider than the screen. Drag sideways to see the rest. Every wire is also listed in the table below.',
    'Die Zeichnung ist breiter als der Bildschirm. Ziehe sie seitlich, um den Rest zu sehen. Die Tabelle darunter enthält ebenfalls alle Leitungen.',
  ],
  drawAlt: [
    'Wiring drawing with {w} wires. The table below contains the same information.',
    'Verdrahtungszeichnung mit {w} Leitungen. Die Tabelle darunter enthält dieselben Angaben.',
  ],
  viewGroup: ['View of the wiring', 'Ansicht der Verdrahtung'],
  vPic: ['Picture', 'Bild'],
  vSch: ['Schematic', 'Schema'],
  diaTitle: ['Wiring picture: {p} parts, {w} wires', 'Verdrahtungsbild: {p} Teile, {w} Drähte'],
  diaDesc: [
    'Each wire is drawn from pin to pin in its assigned colour. Both ends show its table number. The table below contains the same information.',
    'Jede Leitung ist in ihrer zugeordneten Farbe von Pin zu Pin eingezeichnet. An beiden Enden steht ihre Nummer aus der Tabelle. Die Tabelle darunter enthält dieselben Angaben.',
  ],
  diaUndrawn: ['Not in the picture, see the table: wire {n}.', 'Nicht im Bild, siehe Tabelle: Draht {n}.'],
  diaHint: [
    'Zoom with the buttons, two fingers, or Ctrl and the mouse wheel. Drag to move the picture. Keyboard: arrow keys move, + and - zoom, and 0 fits the view. Select a wire number in the table to highlight it. The colours identify wires in the drawing; your leads may have different colours.',
    'Zoome mit den Tasten, zwei Fingern oder Strg und dem Mausrad. Ziehe das Bild, um es zu verschieben. Tastatur: Pfeiltasten verschieben, + und - zoomen, 0 passt die Ansicht ein. Wähle eine Leitungsnummer in der Tabelle, um die Leitung hervorzuheben. Die Farben kennzeichnen die Leitungen in der Zeichnung; deine Leitungen können andere Farben haben.',
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
  diaZener: [
    '3.0V Zener diode, band towards {a}',
    '3,0V-Z-Diode, Ring zu {a}',
  ],
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
    'IOmcu pads: 1 to 20 down the left side, 40 to 21 down the right side. The numbers are those printed on the module. Line colours are a legend; they are not the colours of your leads.',
    'Pads des IOmcu: 1 bis 20 links von oben nach unten, 40 bis 21 rechts von oben nach unten. Die Nummern stehen auf dem Modul. Die Linienfarben sind eine Legende, nicht die Farben der Leitungen.',
  ],
  tabCap: [
    'Pin-to-pin table with {w} wires. Wire numbers stay the same across setups, so the sequence can have gaps.',
    'Pin-zu-Pin-Tabelle mit {w} Leitungen. Die Nummern bleiben in allen Aufbauten gleich. Deshalb kann die Folge Lücken haben.',
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
  footHelp: ['Panel help', 'Panel-Hilfe'],
  printHead: [`rcbench bench configurator, release ${RELEASE}`, `rcbench Prüfstand-Konfigurator, Release ${RELEASE}`],
  copro: ['IOmcu', 'IOmcu'],
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
    'This selection could not be resolved. Start over.',
    'Diese Auswahl konnte nicht aufgelöst werden. Beginne neu.',
  ],
  noscript: [
    'This page requires JavaScript. You can find the build guide on the rcbench repository releases page.',
    'Diese Seite benötigt JavaScript. Die Bauanleitung findest du auf der Release-Seite des rcbench-Repositorys.',
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
  legLogic: [
    '3.3V or 5V supply',
    'Versorgung mit 3,3V oder 5V',
  ],
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
  simHot1: [
    'Hottest resistor: {a}mA, {w}W, {p}% of its power rating.',
    'Heißester Widerstand: {a}mA, {w}W, {p}% seiner Belastbarkeit.',
  ],
  simHot2: [
    'Hottest 2 resistors: {a}mA and {w}W each, {p}% of their power rating.',
    'Heißeste 2 Widerstände: je {a}mA und {w}W, {p}% ihrer Belastbarkeit.',
  ],
  simRest2: [
    'Other 2 resistors: {a}mA and {w}W each.',
    'Andere 2 Widerstände: je {a}mA und {w}W.',
  ],
  simRest1: ['Third resistor: no current.', 'Dritter Widerstand: kein Strom.'],
  simOver: [
    'Above the power rating of {r}. Choose a higher power rating or resistance.',
    'Über der Belastbarkeit von {r}. Wähle eine höhere Belastbarkeit oder einen höheren Widerstand.',
  ],
  simHint: [
    'At {p}% of the power rating. This rating applies only up to the ambient temperature specified in the resistor datasheet. A resistor running near its rating gets hot. Check the derating curve.',
    'Bei {p}% der Belastbarkeit. Dieser Wert gilt nur bis zu der Umgebungstemperatur, die im Datenblatt des Widerstands angegeben ist. Ein Widerstand nahe seiner Belastbarkeit wird heiß. Prüfe die Derating-Kurve.',
  ],
  simBasis: [
    'Worst case: one commutation step held at {v} ({n}S at 4.2V per cell). A sensorless ESC cannot commutate with a resistive load and may hold one step. Current limiting by the ESC is not included in this model.',
    'Schlimmster Fall: ein Kommutierungsschritt wird bei {v} gehalten ({n}S mit 4,2V je Zelle). Ein sensorloser ESC kann an Lastwiderständen nicht kommutieren und kann einen Schritt halten. Die Strombegrenzung des ESC ist in diesem Modell nicht berücksichtigt.',
  ],
  simStarHow: [
    'Star: connect one end of each resistor to A, B or C, then join the other 3 ends. The drawing shows a delta connection.',
    'Stern: Verbinde jeweils ein Ende der Widerstände mit A, B oder C und die anderen 3 Enden miteinander. Die Zeichnung zeigt eine Dreieckschaltung.',
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
