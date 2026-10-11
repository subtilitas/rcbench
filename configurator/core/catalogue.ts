// The single data file of the bench configurator.
//
// Every part, pin, pad, wire, function, rule, check, stage, safety note and
// data string of the page is defined here and nowhere else. The page holds
// chrome strings only. tools/check.ts validates what the compiler cannot:
// ids, wire ends, text rules, source anchors and the facts that are read
// from the repository at the release tag.
//
// Each record names its source in `src`:
//   repo   a file and line at tag v0.16.0
//   guide  a section of the build guide of the release
//   photo  a print read from a photo of the part; such a fact also has a
//          check entry of kind "unknown"
//   page   a proposal or wording of this page; never a hardware fact

import type {
  Catalogue, Check, Cond, Fn, Pad, Part, Pin, PinDefault, PowerSource, Rule, Src, Stage, Text, Wire,
} from '../contract.ts';

const TAG = 'v0.16.0';

function t(en: string, de: string): Text {
  return { en, de };
}

/** A string that reads the same in both languages: a print on a part, a pin name. */
function same(s: string): Text {
  return { en: s, de: s };
}

/** A text that a line of the repository holds at the release tag. */
export type AnchorText = { path: string; line: number; has: string };

const anchorTexts: AnchorText[] = [];

/**
 * A line of the repository at the release tag. `has` is a text of that line
 * which states the fact; `tools/check.ts` compares it with the tagged file.
 */
function repo(path: string, line: number, has: string): Src {
  anchorTexts.push(Object.freeze({ path, line, has }));
  return { kind: 'repo', path, line, tag: TAG };
}

function guide(section: string): Src {
  return { kind: 'guide', section };
}

function photo(file: string): Src {
  return { kind: 'photo', file };
}

const PAGE: Src = { kind: 'page' };

/** A statement of the project owner, by its date. */
function owner(date: string): Src {
  return { kind: 'owner', date };
}

const OWNER = owner('2026-10-11');

function pin(id: string, label: Text, src: Src, confirmed = true): Pin {
  return { id, label, confirmed, src };
}

const OUT_BIND = 'shared/outputs/out_bind.c';
const SETTINGS = 'shared/settings/settings.c';
const BOARD_PINS = 'firmware/panel/components/board/include/board_pins.h';
const IOMCU_PINS = 'firmware/iomcu/include/iomcu_pins.h';

// ---------------------------------------------------------------------------
// Parts. Catalogue order is display order.
//
// The servo and the ESC with its motor are test objects: a visitor connects
// and tests them, the bench needs neither. Every other part is hardware of
// the bench, the supplies and the motor simulator included.
// ---------------------------------------------------------------------------

const parts: Part[] = [
  {
    id: 'panel',
    name: t('Waveshare ESP32-S3-Touch-LCD-7 panel', 'Waveshare-ESP32-S3-Touch-LCD-7-Panel'),
    short: same('Panel'),
    role: t('Touch screen, STOP button, heartbeat source.', 'Touchscreen, STOP-Taste, Quelle des Heartbeat.'),
    locked: true,
    selectable: false,
    release: 'released',
    slot: { column: 'L', order: 1, sub: same('ESP32-S3-Touch-LCD-7') },
    pins: [
      // Prints as photo 01 of the owner shows them: the CAN socket reads H and
      // L, the 3-pin socket J8 reads "Sensor AD" with the pins AD, GND, 3V3.
      pin('CANH', same('CAN H'), OWNER),
      pin('CANL', same('CAN L'), OWNER),
      pin('J8G', same('J8 GND'), repo(BOARD_PINS, 189, 'GPIO6 is on J8')),
      pin('J8S', t('J8 GPIO6 (print AD)', 'J8 GPIO6 (Aufdruck AD)'), repo(BOARD_PINS, 189, 'GPIO6 is on J8')),
    ],
    pictures: [
      {
        kind: 'photo', file: '01-panel-back', width: 480, height: 297,
        alt: t(
          'Back of the panel on its metal plate: the board with the print ESP32-S3-Touch-LCD-7, 2 USB-C sockets, the ESP32-S3 module and the white sockets for I2C, CAN and RS-485.',
          'Rückseite des Panels auf seiner Metallplatte: die Platine mit dem Aufdruck ESP32-S3-Touch-LCD-7, 2 USB-C-Buchsen, das ESP32-S3-Modul und die weißen Buchsen für I2C, CAN und RS-485.',
        ),
      },
    ],
    src: guide('parts'),
  },
  {
    id: 'copro',
    name: t('Waveshare RP2350-CAN board', 'Waveshare-RP2350-CAN-Platine'),
    short: t('Coprocessor', 'Koprozessor'),
    role: t(
      'Coprocessor: drives the outputs, reads the sensors, sets the supply.',
      'Koprozessor: treibt die Ausgänge, liest die Sensoren, stellt die Versorgung.',
    ),
    locked: true,
    selectable: false,
    release: 'released',
    // Drawn in the centre from `pads`, not as a column module.
    slot: { column: 'none', order: 0, sub: same('RP2350-CAN') },
    pins: [],
    variantName: {
      option: 'connection',
      names: {
        osoyoo: t('Osoyoo screw-terminal breakout board', 'Osoyoo-Schraubklemmen-Breakout-Platine'),
      },
    },
    pictures: [
      {
        kind: 'photo', file: '05-rp2350-can-breakout', width: 480, height: 436, only: { connection: 'osoyoo' },
        alt: t(
          'RP2350-CAN board plugged into the Osoyoo breakout board: green screw terminals on both long sides, the pin names GP0 to GP22 printed next to them, 2 jumper wires in the 3-pole terminal of the RP2350-CAN board.',
          'RP2350-CAN-Platine, gesteckt in die Osoyoo-Breakout-Platine: grüne Schraubklemmen an beiden Längsseiten, daneben aufgedruckt die Pin-Namen GP0 bis GP22, 2 Jumperkabel in der 3-poligen Klemme der RP2350-CAN-Platine.',
        ),
      },
      {
        kind: 'photo', file: '08-rp2350-can-b', width: 480, height: 233,
        alt: t(
          'RP2350-CAN board from above: pin headers on both long sides, a green 3-pole screw terminal with 2 jumper wires at the left end, the buttons BOOT and RESET, a USB-C socket at the right end.',
          'RP2350-CAN-Platine von oben: Stiftleisten an beiden Längsseiten, eine grüne 3-polige Schraubklemme mit 2 Jumperkabeln am linken Ende, die Taster BOOT und RESET, eine USB-C-Buchse am rechten Ende.',
        ),
      },
    ],
    src: guide('parts'),
  },
  {
    id: 'usb',
    name: t(
      '2 USB-C cables with data lines, USB-C charger of at least 2 A',
      '2 USB-C-Kabel mit Datenleitungen, USB-C-Ladegerät mit mindestens 2 A',
    ),
    short: t('USB-C cables', 'USB-C-Kabel'),
    role: t(
      'One 5 V supply per board. The panel on the charger with a short cable.',
      'Je Platine eine 5-V-Versorgung. Das Panel am Ladegerät mit kurzem Kabel.',
    ),
    locked: true,
    selectable: false,
    release: 'released',
    slot: { column: 'none', order: 1 },
    pins: [],
    src: guide('also-needed'),
  },
  {
    id: 'wire',
    name: t('Hook-up wire, multimeter', 'Schaltdraht, Multimeter'),
    short: t('Hook-up wire', 'Schaltdraht'),
    role: t(
      'A twisted pair for CAN, jumper leads for the heartbeat and the UART.',
      'Ein verdrilltes Paar für CAN, Steckbrücken für Heartbeat und UART.',
    ),
    locked: true,
    selectable: false,
    release: 'released',
    slot: { column: 'none', order: 2 },
    pins: [],
    src: guide('also-needed'),
  },
  {
    id: 'servo',
    name: t('Servo (yours)', 'Servo (eigenes)'),
    short: same('Servo'),
    role: t('Device under test.', 'Prüfling.'),
    locked: false,
    selectable: false,
    testObject: true,
    release: 'released',
    slot: { column: 'L', order: 4 },
    pins: [
      pin('SIG', t('signal', 'Signal'), guide('wires')),
      pin('POS', same('+'), guide('wires')),
      pin('NEG', same('−'), guide('wires')),
    ],
    src: guide('also-needed'),
  },
  {
    id: 'ssup',
    name: t('Servo supply (yours)', 'Servoversorgung (eigene)'),
    short: t('Servo supply', 'Servoversorgung'),
    role: t(
      'A 2S LiPo battery or another supply that matches the servo. Voltage within the rating of the servo.',
      'Ein 2S-LiPo-Akku oder eine andere Versorgung, die zum Servo passt. Spannung innerhalb der Nennwerte des Servos.',
    ),
    locked: false,
    selectable: false,
    release: 'released',
    slot: { column: 'L', order: 9 },
    pins: [
      pin('POS', same('+'), guide('stage-1')),
      pin('NEG', same('−'), guide('stage-1')),
    ],
    src: guide('stage-1'),
  },
  {
    id: 'esc',
    name: t('ESC and motor (yours)', 'ESC und Motor (eigene)'),
    short: same('ESC'),
    role: t(
      'Device under test. ESC: electronic speed controller.',
      'Prüfling. ESC: elektronischer Drehzahlsteller.',
    ),
    locked: false,
    selectable: false,
    testObject: true,
    release: 'released',
    slot: { column: 'L', order: 5 },
    // Signal lead, power leads and phase wires form 3 groups.
    pins: [
      pin('SIG', t('signal', 'Signal'), guide('wires')),
      pin('SG', t('signal ground', 'Signalmasse'), guide('wires')),
      pin('BEC', t('centre wire (BEC +)', 'Mittelader (BEC +)'), guide('wires')),
      pin('POS', same('+'), guide('wires')),
      pin('NEG', same('−'), guide('wires')),
      pin('A', t('phase A', 'Phase A'), guide('wires')),
      pin('B', t('phase B', 'Phase B'), guide('wires')),
      pin('C', t('phase C', 'Phase C'), guide('wires')),
    ],
    src: guide('also-needed'),
  },
  {
    id: 'batt',
    name: t('ESC battery (yours)', 'ESC-Akku (eigener)'),
    short: t('ESC battery', 'ESC-Akku'),
    role: t('Source A of the ESC.', 'Quelle A des ESC.'),
    locked: false,
    selectable: false,
    release: 'released',
    slot: { column: 'L', order: 10 },
    pins: [
      pin('POS', same('+'), guide('wires')),
      pin('NEG', same('−'), guide('wires')),
    ],
    src: guide('wires'),
  },
  {
    id: 'pdmini',
    name: t(
      'WeAct Studio PD Power Mini V1 Buck module',
      'WeAct-Studio-PD-Power-Mini-V1-Buck-Modul',
    ),
    short: same('PD mini'),
    role: t(
      'Supply set from the bench: 1 to 20 V, 0.05 to 3 A (vendor values, not measured).',
      'Vom Prüfstand gestellte Versorgung: 1 bis 20 V, 0,05 bis 3 A (Herstellerangaben, nicht gemessen).',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'L', order: 6, sub: t('UART through the T-plug', 'UART über den T-Stecker') },
    // The serial lines D+ and D− and their ground leave the USB-C socket
    // through the T-plug, the part `usbtap`.
    pins: [
      pin('USB', t('USB-C in', 'USB-C Eingang'), guide('wires')),
      pin('OP', same('OUT +'), guide('wires')),
      pin('ON', same('OUT −'), guide('wires')),
    ],
    pictures: [
      {
        kind: 'photo', file: '02-pd-mini', width: 480, height: 279,
        alt: t(
          'PD mini board, top side: USB-C socket, display, 4 buttons, a green 4-pole screw terminal and a 4-pin header. The left third of the photo is out of focus.',
          'PD-mini-Platine, Oberseite: USB-C-Buchse, Display, 4 Taster, eine grüne 4-polige Schraubklemme und eine 4-polige Stiftleiste. Das linke Drittel des Fotos ist unscharf.',
        ),
      },
    ],
    src: guide('parts'),
  },
  {
    id: 'charger',
    name: t('USB-C PD charger', 'USB-C-PD-Ladegerät'),
    short: t('PD charger', 'PD-Ladegerät'),
    role: t('Feeds the PD mini. PD: USB Power Delivery.', 'Speist den PD mini. PD: USB Power Delivery.'),
    locked: false,
    selectable: false,
    release: 'released',
    slot: { column: 'L', order: 8 },
    pins: [pin('USB', same('USB-C'), guide('wires'))],
    src: guide('also-needed'),
  },
  {
    id: 'ina3221',
    name: t('DAOKAI INA3221 board, 3 channels', 'DAOKAI-INA3221-Platine, 3 Kanäle'),
    short: same('INA3221'),
    role: t(
      'Servo current, 0 to 26 V, up to 1.64 A per channel. I2C 0x40 with the A0 bridge to GND. Header of 8 holes, from the square hole: VS, GND, SCL, SDA, PV, CRI, WAR, TC.',
      'Servostrom, 0 bis 26 V, bis 1,64 A je Kanal. I2C 0x40 mit A0-Brücke nach GND. Leiste mit 8 Löchern, ab dem quadratischen Loch: VS, GND, SCL, SDA, PV, CRI, WAR, TC.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: {
      column: 'R',
      order: 14,
      sub: same('VS GND SCL SDA PV CRI WAR TC'),
    },
    // The board as the owner's photos of 2026-10-11 show it. Header of 8
    // holes, from the square hole: VS, GND, SCL, SDA, PV, CRI, WAR, TC. A0 is
    // no header pin: 4 solder bridges set it to GND, SDA, SCL or VS.
    pins: [
      pin('VS', same('VS'), OWNER),
      pin('GND', same('GND'), OWNER),
      pin('SCL', same('SCL'), OWNER),
      pin('SDA', same('SDA'), OWNER),
      pin('A0', t('A0 solder bridges', 'A0-Lötbrücken'), OWNER),
      // The back of the board prints VIN1+ and VIN1- at the 2 pads of CH1.
      // VIN1+ is the supply side of the shunt, VIN1- the side of the servo.
      pin('C1A', same('VIN1+'), OWNER),
      pin('C1B', same('VIN1-'), OWNER),
      // Both sides print GND, GND, VPU. What the pads do is not confirmed.
      pin('LP', t('GND, GND, VPU pads', 'Pads GND, GND, VPU'), OWNER, false),
    ],
    pictures: [
      {
        kind: 'photo', file: '16-ina3221-front', width: 480, height: 403,
        alt: t(
          'INA3221 board, component side: 8-hole header, A0 solder bridges GND, SDA, SCL, VS, 3 shunts R100 above the pad pairs CH1, CH2, CH3, 3 large pads GND, GND, VPU.',
          'INA3221-Platine, Bauteilseite: Leiste mit 8 Löchern, A0-Lötbrücken GND, SDA, SCL, VS, 3 Shunts R100 über den Pad-Paaren CH1, CH2, CH3, 3 große Pads GND, GND, VPU.',
        ),
      },
      {
        kind: 'photo', file: '17-ina3221-back', width: 480, height: 404,
        alt: t(
          'INA3221 board, back: pads VIN1-, VIN1+, VIN2-, VIN2+, VIN3-, VIN3+; header from the square hole VS, GND, SCL, SDA, PV, CRI, WAR, TC; large pads GND, GND, VPU.',
          'INA3221-Platine, Rückseite: Pads VIN1-, VIN1+, VIN2-, VIN2+, VIN3-, VIN3+; Leiste ab dem quadratischen Loch VS, GND, SCL, SDA, PV, CRI, WAR, TC; große Pads GND, GND, VPU.',
        ),
      },
    ],
    src: guide('parts'),
  },
  {
    id: 'as5600',
    name: t('AS5600 board with diametric magnet', 'AS5600-Platine mit diametralem Magnet'),
    short: same('AS5600'),
    role: t(
      'Angle of the servo horn, 12 bit. I2C 0x36, fixed.',
      'Winkel des Servohorns, 12 Bit. I2C 0x36, fest.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'R', order: 15 },
    pins: [
      pin('VCC', same('VCC'), guide('stage-4')),
      pin('OUT', same('OUT'), repo('docs/Building.md', 108, '| OUT | not connected')),
      pin('GND', same('GND'), guide('stage-4')),
      // docs/Building.md names this pin PGO and ties it to GND. The print on
      // the board is not confirmed.
      pin('GPO', same('GPO'), repo('docs/Building.md', 107, '| PGO | GND |'), false),
      pin('SDA', same('SDA'), guide('stage-4')),
      pin('SCL', same('SCL'), guide('stage-4')),
      pin('DIR', same('DIR'), guide('stage-4')),
    ],
    pictures: [
      {
        kind: 'photo', file: '10-as5600-b', width: 480, height: 407,
        alt: t(
          'AS5600 board, white, with the pad names VCC, OUT, GND, GPO, SDA, SCL and DIR. A yellow 7-pin header strip and a round magnet lie next to it.',
          'AS5600-Platine, weiß, mit den Pad-Namen VCC, OUT, GND, GPO, SDA, SCL und DIR. Daneben liegen eine gelbe 7-polige Stiftleiste und ein runder Magnet.',
        ),
      },
    ],
    src: guide('parts'),
  },
  {
    id: 'pull',
    name: t('2 resistors, 2.2 kΩ', '2 Widerstände, 2,2 kΩ'),
    short: t('2 × 2.2 kΩ', '2 × 2,2 kΩ'),
    role: t('Pull-ups of the sensor bus.', 'Pull-ups des Sensorbusses.'),
    locked: false,
    selectable: false,
    release: 'released',
    slot: { column: 'R', order: 16, sub: t('one per bus line', 'je Busleitung einer') },
    pins: [
      pin('SDA', t('R1, to SDA', 'R1, nach SDA'), guide('wires')),
      pin('SCL', t('R2, to SCL', 'R2, nach SCL'), guide('wires')),
      pin('V', t('R1 and R2, to 3V3', 'R1 und R2, nach 3V3'), guide('wires')),
    ],
    src: guide('wires'),
  },
  {
    id: 'ina228',
    name: t('INA228 battery current sensor', 'INA228-Akkustromsensor'),
    short: same('INA228'),
    role: t('ESC supply voltage and current.', 'Versorgungsspannung und Strom des ESC.'),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'R', order: 12 },
    // Pin names are those of the MATEK board. The header of the module with
    // XT90 plugs prints L A V G 5: L is SCL, A is SDA, V is the 3.3 V supply
    // of the chip, G is GND. Hole 5 has no pin. The supply pin differs by
    // board; the check entry `ina228-xt90-pads` states the print.
    pins: [
      { ...pin('V5', same('5V'), guide('wires')), only: { ina228Board: 'matek' } },
      { ...pin('V3', same('V'), OWNER), only: { ina228Board: 'xt90' } },
      pin('G', same('G'), guide('wires')),
      pin('SDA', same('SDA'), guide('wires')),
      pin('SCL', same('SCL'), guide('wires')),
      pin('BAT', same('BAT+'), guide('wires')),
      pin('ESC', same('ESC+'), guide('wires')),
      // The minus poles of the 2 plugs. The shunt lies between them.
      { ...pin('BATN', same('BAT−'), OWNER), only: { ina228Board: 'xt90' } },
      { ...pin('ESCN', same('ESC−'), OWNER), only: { ina228Board: 'xt90' } },
    ],
    variantName: {
      option: 'ina228Board',
      names: {
        matek: t('MATEK I2C-INA-BM board', 'MATEK-I2C-INA-BM-Platine'),
        xt90: t(
          'INA228 module with XT90 plugs (print: BIFRC V1.1)',
          'INA228-Modul mit XT90-Steckern (Aufdruck: BIFRC V1.1)',
        ),
      },
    },
    pictures: [
      {
        kind: 'photo', file: '15-current-sensor-top', width: 480, height: 306, only: { ina228Board: 'xt90' },
        alt: t(
          'Current sensor board with the print BIFRC V1.1, from above: 2 yellow power plugs, the shunt between them, a 5-pin header with the print L, A, V, G, 5.',
          'Stromsensor-Platine mit dem Aufdruck BIFRC V1.1, von oben: 2 gelbe Leistungsstecker, dazwischen der Shunt, eine 5-polige Stiftleiste mit dem Aufdruck L, A, V, G, 5.',
        ),
      },
      {
        kind: 'photo', file: '14-current-sensor-oblique', width: 480, height: 356, only: { ina228Board: 'xt90' },
        alt: t(
          'The same current sensor board in its black frame, seen from an angle.',
          'Dieselbe Stromsensor-Platine in ihrem schwarzen Rahmen, schräg gesehen.',
        ),
      },
    ],
    src: guide('parts'),
  },
  {
    id: 'sd',
    name: t('microSD card, FAT', 'microSD-Karte, FAT'),
    short: t('microSD card', 'microSD-Karte'),
    role: t(
      'In the card slot of the panel. Tested card sizes: not stated.',
      'Im Kartenschacht des Panels. Getestete Kartengrößen: nicht angegeben.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: {
      column: 'L',
      order: 2,
      sub: t('in the slot of the panel, no wires', 'im Schacht des Panels, keine Drähte'),
    },
    pins: [],
    src: guide('stage-5'),
  },
  {
    id: 'estop',
    name: t(
      'Emergency stop button, latching, 1 NO and 1 NC contact',
      'Not-Aus-Taster, rastend, 1 Schließer (NO) und 1 Öffner (NC)',
    ),
    short: t('Emergency stop', 'Not-Aus'),
    role: t(
      'Its NC (normally closed) contact opens the heartbeat wire. It switches off no supply.',
      'Sein Öffner (NC) unterbricht den Heartbeat-Draht. Er schaltet keine Versorgung ab.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'L', order: 3, sub: t('NC contact', 'Öffner (NC)') },
    // 3 tabs in a row: C, NO, NC, named from the wiring diagram of the
    // seller, not from a measurement. The 2 outer tabs C and NC form the NC
    // contact. The middle tab NO has no pin here and takes no wire.
    pins: [
      pin('NC1', t('C, outer tab', 'C, äußere Fahne'), OWNER),
      pin('NC2', t('NC, outer tab', 'NC, äußere Fahne'), OWNER),
    ],
    pictures: [
      {
        kind: 'photo', file: '11-estop-front', width: 480, height: 446,
        alt: t(
          'Red mushroom push button with the print STOP and 2 arrows, seen from the front.',
          'Roter Pilztaster mit dem Aufdruck STOP und 2 Pfeilen, von vorn gesehen.',
        ),
      },
      {
        kind: 'photo', file: '12-estop-side', width: 480, height: 341,
        alt: t(
          'The same push button from the side: the switch body with 3 solder tabs.',
          'Derselbe Taster von der Seite: der Schaltkörper mit 3 Lötfahnen.',
        ),
      },
    ],
    src: guide('parts'),
  },
  {
    id: 'sim',
    name: t(
      'Motor simulator, self-built: 3 resistors, 470 Ω, 2 W',
      'Motorsimulator, selbst gebaut: 3 Widerstände, 470 Ω, 2 W',
    ),
    short: t('Motor simulator', 'Motorsimulator'),
    role: t(
      'Stands in for the motor on the phase wires of the ESC.',
      'Ersetzt den Motor an den Phasenleitungen des ESC.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'L', order: 11, sub: t('no ground wire', 'kein Massedraht') },
    pins: [
      pin('A', t('corner A', 'Ecke A'), guide('wires')),
      pin('B', t('corner B', 'Ecke B'), guide('wires')),
      pin('C', t('corner C', 'Ecke C'), guide('wires')),
    ],
    src: guide('parts'),
  },
  {
    id: 'tap',
    name: t(
      'Phase tap: resistor 4.7 kΩ 0.25 W, zener diode 3.0 V',
      'Phasenabgriff: Widerstand 4,7 kΩ 0,25 W, Z-Diode 3,0 V',
    ),
    short: t('Phase tap', 'Phasenabgriff'),
    role: t(
      'Reads pitch and length of the beeps of the ESC on phase A.',
      'Liest Tonhöhe und Länge der ESC-Töne an Phase A.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'R', order: 13, sub: t('4.7 kΩ, 0.25 W', '4,7 kΩ, 0,25 W') },
    pins: [
      pin('IN', t('resistor, end 1', 'Widerstand, Ende 1'), guide('tap')),
      pin('OUT', t('resistor, end 2', 'Widerstand, Ende 2'), guide('tap')),
    ],
    src: guide('parts'),
  },
  {
    id: 'usbtap',
    name: t(
      'USB-C tap with 3-wire lead, the T-plug of the PD mini',
      'USB-C-Abgriff mit 3-adriger Leitung, der T-Stecker des PD mini',
    ),
    short: t('T-plug', 'T-Stecker'),
    role: t(
      'Sits between the USB-C PD charger and the PD mini: the charger on the socket IN, a USB-C cable from the socket OUT to the PD mini. Brings GND, D+ and D− to the coprocessor.',
      'Sitzt zwischen USB-C-PD-Ladegerät und PD mini: das Ladegerät an der Buchse IN, ein USB-C-Kabel von der Buchse OUT zum PD mini. Führt GND, D+ und D− zum Koprozessor.',
    ),
    between: ['charger', 'pdmini'],
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'L', order: 7, sub: t('UART of the PD mini', 'UART des PD mini') },
    pins: [
      // Both ends of the power path are USB-C sockets with the print IN and OUT.
      pin('OUT', t('OUT, USB-C socket', 'OUT, USB-C-Buchse'), OWNER),
      pin('IN', t('IN, USB-C socket', 'IN, USB-C-Buchse'), OWNER),
      pin('G', same('G'), OWNER),
      // D− is the receive line of the PD mini, D+ its transmit line.
      pin('DP', same('D+'), OWNER),
      pin('DM', same('D−'), OWNER),
      // No wire.
      pin('V', same('V'), OWNER),
    ],
    pictures: [
      {
        kind: 'photo', file: '04-usb-c-tap-closeup', width: 480, height: 530,
        alt: t(
          'USB-C tap, close-up: the sockets IN and OUT and 4 pads with the print G, D+, D-, V. A black wire on G, a red wire on D+, a white wire on D-, no wire on V.',
          'USB-C-Abgriff, Nahaufnahme: die Buchsen IN und OUT und 4 Pads mit dem Aufdruck G, D+, D-, V. Ein schwarzes Kabel an G, ein rotes an D+, ein weißes an D-, kein Kabel an V.',
        ),
      },
    ],
    src: OWNER,
  },
];

// ---------------------------------------------------------------------------
// Pads of the coprocessor. [pad, name, gpio, line in out_bind.c at v0.16.0]
// ---------------------------------------------------------------------------

const CAN_RESERVED: Text = t(
  'Used by the CAN controller of the board.',
  'Vom CAN-Controller der Platine belegt.',
);

/** Terminal prints of the Osoyoo breakout board that differ from the pad name. */
const BREAKOUT_LABELS: Record<number, string> = {
  31: '26 A0',
  32: '27 A1',
  34: '28 A2',
  35: 'ADC.REF',
  37: '3V3.EN',
};

const PAD_ROWS: Array<[number, string, number | null, number]> = [
  [1, 'GP0', 0, 33], [2, 'GP1', 1, 34], [3, 'GND', null, 86], [4, 'GP2', 2, 35],
  [5, 'GP3', 3, 36], [6, 'GP4', 4, 37], [7, 'GP5', 5, 38], [8, 'GND', null, 87],
  [9, 'GP6', 6, 39], [10, 'GP7', 7, 40], [11, 'GP8', 8, 41], [12, 'GP9', 9, 42],
  [13, 'GND', null, 88], [14, 'GP10', 10, 43], [15, 'GP11', 11, 44], [16, 'GP12', 12, 45],
  [17, 'GP13', 13, 46], [18, 'GND', null, 89], [19, 'GP14', 14, 47], [20, 'GP15', 15, 48],
  [21, 'GP16', 16, 49], [22, 'GP17', 17, 50], [23, 'GND', null, 90], [24, 'GP18', 18, 51],
  [25, 'GP19', 19, 52], [26, 'GP20', 20, 53], [27, 'GP21', 21, 54], [28, 'GND', null, 91],
  [29, 'GP22', 22, 55], [30, 'RUN', null, 92], [31, 'GP26', 26, 56], [32, 'GP27', 27, 57],
  [33, 'GND', null, 93], [34, 'GP28', 28, 58], [35, 'VREF', null, 94], [36, '3V3', null, 95],
  [37, '3V3_EN', null, 96], [38, 'GND', null, 97], [39, 'VSYS', null, 98], [40, 'VBUS', null, 99],
];

const pads: Pad[] = PAD_ROWS.map(([n, name, gpio, line]): Pad => {
  const pad: Pad = {
    n,
    name,
    gpio,
    breakoutLabel: BREAKOUT_LABELS[n] ?? name,
    // The row of the firmware table opens with the GPIO and the pad, or with
    // the pad and its kind.
    src: repo(OUT_BIND, line, gpio !== null ? `{ ${gpio}, ${n},` : `{ ${n}, LINK_PAD_`),
  };
  // GP8 to GP12 are the interrupt and the SPI bus of the CAN controller.
  if (gpio !== null && gpio >= 8 && gpio <= 12) pad.reserved = CAN_RESERVED;
  return pad;
});

// ---------------------------------------------------------------------------
// Wires. A wire keeps its number in every selection.
//
// Nets: all GND pads are one net `gnd`. The servo's minus is on that net
// (wire 8), so the wires 10 and 21 carry it too. `pd_out` is the PD mini's
// OUT +, shared by the servo and the ESC when both run from it.
// ---------------------------------------------------------------------------

const V33_EN = '3.3 V';
const V33_DE = '3,3 V';

const PROPOSED = t('Pin proposed by this page.', 'Pin ist ein Vorschlag dieser Seite.');
const STATED = t(
  'Pin from an owner statement of 2026-10-11.',
  'Pin aus einer Angabe des Projektinhabers vom 2026-10-11.',
);
const EXAMPLE = t(
  "Pin from the build guide's example.",
  'Pin aus dem Beispiel der Bauanleitung.',
);

const HB = t(`heartbeat, ${V33_EN}`, `Heartbeat, ${V33_DE}`);
const GND = same('GND');
const SDA = t(`I2C SDA, ${V33_EN}`, `I2C SDA, ${V33_DE}`);
const SCL = t(`I2C SCL, ${V33_EN}`, `I2C SCL, ${V33_DE}`);
const SUPPLY_33 = t(`supply, ${V33_EN}`, `Versorgung, ${V33_DE}`);
const SERVO_POWER = t('servo power', 'Servoversorgung');
const ESC_POWER = t('ESC power', 'ESC-Versorgung');
const NONE = t('none', 'keines');
const NOTHING = t('nothing', 'nichts');
const SHUNT_SERVO = t(
  'The shunt sits in the plus wire of the servo.',
  'Der Shunt sitzt in der Plusleitung des Servos.',
);
// Holds for both INA228 boards: the shunt of the MATEK board is in this line,
// the module with XT90 plugs passes it from plug to plug.
const SENSOR_ESC = t(
  'The plus line of the ESC runs through the sensor board.',
  'Die Plusleitung des ESC läuft durch das Sensor-Board.',
);
const SENSOR_ESC_NEG = t(
  'The shunt of this module sits in the minus line of the ESC.',
  'Der Shunt dieses Moduls sitzt in der Minusleitung des ESC.',
);
/** The INA228 module with XT90 plugs is in the setup: its shunt is in the minus line. */
const XT90_SHUNT: Cond = { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'xt90' }] };
/** A servo without the PD mini: its plus comes from the visitor's supply or from the bench. */
const SERVO_NO_PD: Cond[] = [{ has: 'servo' }, { not: { has: 'pdmini' } }];
const SERVO_EXTERNAL: Cond = { all: [...SERVO_NO_PD, { opt: 'servoPower', is: 'external' }] };
/** The bench itself feeds the servo: the 5 V pin, or the V pin of a strip of the Osoyoo breakout board. */
const SERVO_BENCH: Cond = { all: [...SERVO_NO_PD, { opt: 'servoPower', is: 'bench' }] };
const SERVO_5V_PIN: Cond = { all: [SERVO_BENCH, { opt: 'connection', is: 'pads' }] };
const SERVO_STRIP: Cond = { all: [SERVO_BENCH, { opt: 'connection', is: 'osoyoo' }] };
const SERVO_BENCH_NOTE = t(
  'For a small servo. The servo draws its current from the supply that also powers the logic.',
  'Für ein kleines Servo. Das Servo zieht seinen Strom aus der Versorgung, die auch die Logik speist.',
);
const STRIP_V = t(
  'V pin of the S V G strip at GP2 of the Osoyoo breakout board',
  'V-Pin der S-V-G-Leiste an GP2 der Osoyoo-Breakout-Platine',
);

const wires: Wire[] = [
  {
    n: 1, kind: 'wire', a: 'panel.CANH', b: 'can.H', net: 'canh', cls: 'canh',
    signal: same('CAN H'),
    note: t(
      'Twist CAN H and CAN L together. Each branch under 30 cm, the bus under 5 m.',
      'CAN H und CAN L verdrillen. Jeder Abzweig unter 30 cm, der Bus unter 5 m.',
    ),
    when: null, stage: 1, route: { kind: 'L', lane: 0 }, src: guide('wires'),
  },
  {
    n: 2, kind: 'wire', a: 'panel.CANL', b: 'can.L', net: 'canl', cls: 'canl',
    signal: same('CAN L'),
    note: t(
      '120 Ω termination on at both ends. 1 Mbit/s, set by the firmware.',
      '120-Ω-Abschluss an beiden Enden eingeschaltet. 1 Mbit/s, von der Firmware gesetzt.',
    ),
    when: null, stage: 1, route: { kind: 'L', lane: 1 }, src: guide('wires'),
  },
  {
    n: 3, kind: 'wire', a: 'panel.J8G', b: 'pad.3', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: t('Common ground.', 'Gemeinsame Masse.'),
    when: null, stage: 1, route: { kind: 'L', lane: 2 }, src: guide('wires'),
  },
  {
    n: 4, kind: 'wire', a: 'panel.J8S', b: 'pad.5', net: 'hb', cls: 'hb',
    signal: HB,
    note: t(
      'The interlock. The coprocessor refuses to arm without it.',
      'Die Verriegelung. Ohne ihn schaltet der Koprozessor nicht scharf.',
    ),
    when: { not: { has: 'estop' } }, stage: 1, route: { kind: 'L', lane: 3 }, pin: 'fixed',
    src: guide('wires'),
  },
  {
    n: 5, kind: 'wire', a: 'panel.J8S', b: 'estop.NC1', net: 'hb', cls: 'hb',
    signal: HB,
    note: t(
      'In place of wire 4. Use the 2 outer tabs C and NC. The middle tab NO stays unconnected.',
      'Anstelle von Draht 4. Die 2 äußeren Fahnen C und NC verwenden. Die mittlere Fahne NO bleibt unbeschaltet.',
    ),
    when: { has: 'estop' }, stage: 5, route: { kind: 'L', lane: 3 }, src: guide('wires'),
  },
  {
    n: 6, kind: 'wire', a: 'estop.NC2', b: 'pad.5', net: 'hb_in', cls: 'hb',
    signal: HB,
    note: t(
      'Optional: 10 kΩ from GP3 to GND on a long lead.',
      'Optional: 10 kΩ von GP3 nach GND bei langer Leitung.',
    ),
    when: { has: 'estop' }, stage: 5, route: { kind: 'L', lane: 4 }, pin: 'fixed',
    src: guide('wires'),
  },
  {
    n: 7, kind: 'wire', a: 'pad.4', b: 'servo.SIG', net: 'servo_sig', cls: 'out',
    signal: t(`SERVO PWM, ${V33_EN}`, `SERVO PWM, ${V33_DE}`),
    note: t(
      `${PROPOSED.en} Bind it under SETUP → OUTPUTS as SERVO PWM.`,
      `${PROPOSED.de} Unter SETUP → OUTPUTS als SERVO PWM binden.`,
    ),
    when: { has: 'servo' }, stage: 1, route: { kind: 'L', lane: 5 }, pin: 'proposal',
    src: PAGE,
  },
  {
    n: 8, kind: 'wire', a: 'pad.8', b: 'servo.NEG', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: t(
      'Servo − also goes to coprocessor GND.',
      'Servo − geht auch an GND des Koprozessors.',
    ),
    when: { has: 'servo' }, stage: 1, route: { kind: 'L', lane: 6 }, src: guide('wires'),
  },
  {
    n: 9, kind: 'wire', a: 'ssup.POS', b: 'servo.POS', net: 'servo_v', cls: 'power',
    signal: SERVO_POWER,
    note: t(
      'Within the rated voltage of the servo.',
      'Innerhalb der Nennspannung des Servos.',
    ),
    when: { all: [SERVO_EXTERNAL, { not: { has: 'ina3221' } }] },
    stage: 1, route: { kind: 'O', lane: 0 }, src: guide('stage-1'),
  },
  {
    n: 10, kind: 'wire', a: 'ssup.NEG', b: 'servo.NEG', net: 'gnd', cls: 'power',
    signal: SERVO_POWER,
    note: null,
    when: SERVO_EXTERNAL,
    stage: 1, route: { kind: 'O', lane: 1 }, src: guide('stage-1'),
  },
  {
    n: 11, kind: 'wire', a: 'pad.7', b: 'esc.SIG', net: 'esc_sig', cls: 'out',
    signal: t(`MOTOR PWM or DShot, ${V33_EN}`, `MOTOR PWM oder DShot, ${V33_DE}`),
    note: EXAMPLE,
    when: { has: 'esc' }, stage: 1, route: { kind: 'L', lane: 7 }, pin: 'example',
    src: guide('wires'),
  },
  {
    n: 12, kind: 'wire', a: 'pad.13', b: 'esc.SG', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: t(
      'Ground wire of the signal lead of the ESC.',
      'Massedraht des ESC-Signalkabels.',
    ),
    when: { has: 'esc' }, stage: 1, route: { kind: 'L', lane: 8 }, src: guide('wires'),
  },
  {
    n: 13, kind: 'stub', a: 'esc.BEC', b: null, to: NOTHING, net: 'esc_bec', cls: 'nc',
    signal: NONE,
    note: t('Connected nowhere.', 'Wird nirgends angeschlossen.'),
    when: { has: 'esc' }, stage: 1, route: { kind: 'S' }, src: guide('wires'),
  },
  {
    n: 14, kind: 'wire', a: 'batt.POS', b: 'esc.POS', net: 'batt_pos', cls: 'power',
    signal: ESC_POWER,
    note: null,
    when: {
      all: [{ has: 'esc' }, { opt: 'escPower', is: 'battery' }, { not: { has: 'ina228' } }],
    },
    stage: 1, route: { kind: 'O', lane: 2 }, src: guide('wires'),
  },
  {
    n: 15, kind: 'wire', a: 'batt.NEG', b: 'esc.NEG', net: 'esc_neg', cls: 'power',
    signal: ESC_POWER,
    note: null,
    when: { all: [{ has: 'esc' }, { opt: 'escPower', is: 'battery' }, { not: XT90_SHUNT }] },
    stage: 1, route: { kind: 'O', lane: 3 }, src: guide('wires'),
  },
  {
    n: 16, kind: 'wire', a: 'pad.10', b: 'usbtap.DM', net: 'pd_tx', cls: 'uart',
    signal: t(`UART TX, ${V33_EN}`, `UART TX, ${V33_DE}`),
    note: t(
      `Named from the coprocessor: its TX goes to D− of the T-plug. ${STATED.en} Optional 1 kΩ in series.`,
      `Vom Koprozessor aus benannt: sein TX geht an D− des T-Steckers. ${STATED.de} Optional 1 kΩ in Reihe.`,
    ),
    when: { has: 'pdmini' }, stage: 2, route: { kind: 'L', lane: 9 }, pin: 'proposal',
    src: OWNER,
  },
  {
    n: 17, kind: 'wire', a: 'usbtap.DP', b: 'pad.9', net: 'pd_rx', cls: 'uart',
    signal: t(`UART RX, ${V33_EN}`, `UART RX, ${V33_DE}`),
    note: t(
      `${STATED.en} Optional 1 kΩ in series.`,
      `${STATED.de} Optional 1 kΩ in Reihe.`,
    ),
    when: { has: 'pdmini' }, stage: 2, route: { kind: 'L', lane: 10 }, pin: 'proposal',
    src: OWNER,
  },
  {
    n: 18, kind: 'wire', a: 'pad.18', b: 'usbtap.G', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: null,
    when: { has: 'pdmini' }, stage: 2, route: { kind: 'L', lane: 11 }, src: OWNER,
  },
  {
    n: 19, kind: 'wire', a: 'charger.USB', b: 'usbtap.IN', net: 'pd_in', cls: 'power',
    signal: t('PD in', 'PD-Eingang'),
    note: t(
      'USB-C cable of the charger, plugged into the socket IN.',
      'USB-C-Kabel des Ladegeräts, in die Buchse IN gesteckt.',
    ),
    when: { has: 'pdmini' }, stage: 2, route: { kind: 'O', lane: 4 }, src: OWNER,
  },
  {
    n: 20, kind: 'wire', a: 'pdmini.OP', b: 'servo.POS', net: 'pd_out', cls: 'power',
    signal: t('servo power, 1 to 20 V', 'Servoversorgung, 1 bis 20 V'),
    note: null,
    when: { all: [{ has: 'servo' }, { has: 'pdmini' }, { not: { has: 'ina3221' } }] },
    stage: 2, route: { kind: 'O', lane: 0 }, src: guide('wires'),
  },
  {
    n: 21, kind: 'wire', a: 'pdmini.ON', b: 'servo.NEG', net: 'gnd', cls: 'power',
    signal: SERVO_POWER,
    note: null,
    when: { all: [{ has: 'servo' }, { has: 'pdmini' }] },
    stage: 2, route: { kind: 'O', lane: 1 }, src: guide('wires'),
  },
  {
    n: 22, kind: 'wire', a: 'pdmini.OP', b: 'esc.POS', net: 'pd_out', cls: 'power',
    signal: t('ESC power, 1 to 20 V', 'ESC-Versorgung, 1 bis 20 V'),
    note: t('Up to 3 A.', 'Bis 3 A.'),
    when: { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }, { not: { has: 'ina228' } }] },
    stage: 2, route: { kind: 'O', lane: 2 }, src: guide('wires'),
  },
  {
    n: 23, kind: 'wire', a: 'pdmini.ON', b: 'esc.NEG', net: 'gnd', cls: 'power',
    signal: ESC_POWER,
    note: null,
    when: { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }, { not: XT90_SHUNT }] },
    stage: 2, route: { kind: 'O', lane: 3 }, src: guide('wires'),
  },
  {
    n: 24, kind: 'wire', a: 'pad.36', b: 'ina3221.VS', net: '3v3', cls: 'logic',
    signal: SUPPLY_33,
    note: null,
    when: { has: 'ina3221' }, stage: 3, route: { kind: 'R', lane: 5 }, src: guide('wires'),
  },
  {
    n: 25, kind: 'wire', a: 'pad.23', b: 'ina3221.GND', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: null,
    when: { has: 'ina3221' }, stage: 3, route: { kind: 'R', lane: 6 }, src: guide('wires'),
  },
  {
    n: 26, kind: 'wire', a: 'pad.21', b: 'ina3221.SDA', net: 'sda', cls: 'sda',
    signal: SDA,
    note: t(
      'One bus for all sensors, 400 kHz. No output on GP16 or GP17.',
      'Ein Bus für alle Sensoren, 400 kHz. Kein Ausgang auf GP16 oder GP17.',
    ),
    when: { has: 'ina3221' }, stage: 3, route: { kind: 'R', lane: 7 }, pin: 'default',
    src: repo('docs/FirstRun.md', 289, '| SDA | GP16 (pad 21) |'),
  },
  {
    n: 27, kind: 'wire', a: 'pad.22', b: 'ina3221.SCL', net: 'scl', cls: 'scl',
    signal: SCL,
    note: null,
    when: { has: 'ina3221' }, stage: 3, route: { kind: 'R', lane: 8 }, pin: 'default',
    src: repo('docs/FirstRun.md', 290, '| SCL | GP17 (pad 22) |'),
  },
  {
    n: 28, kind: 'wire', a: 'pdmini.OP', b: 'ina3221.C1A', net: 'pd_out', cls: 'power',
    signal: t('servo power, 1 to 20 V', 'Servoversorgung, 1 bis 20 V'),
    note: SHUNT_SERVO,
    when: { all: [{ has: 'servo' }, { has: 'pdmini' }, { has: 'ina3221' }] },
    stage: 3, route: { kind: 'X', left: 12, right: 16, channel: 0 }, src: guide('wires'),
  },
  {
    n: 29, kind: 'wire', a: 'ssup.POS', b: 'ina3221.C1A', net: 'servo_v', cls: 'power',
    signal: t('servo power, up to 26 V', 'Servoversorgung, bis 26 V'),
    note: SHUNT_SERVO,
    when: { all: [SERVO_EXTERNAL, { has: 'ina3221' }] },
    stage: 3, route: { kind: 'X', left: 12, right: 16, channel: 0 }, src: guide('wires'),
  },
  {
    n: 30, kind: 'wire', a: 'servo.POS', b: 'ina3221.C1B', net: 'servo_shunt', cls: 'power',
    signal: SERVO_POWER,
    note: t(
      'The back of the board prints VIN1+ and VIN1- at the 2 pads of CH1. The supply goes to VIN1+, the servo to VIN1-.',
      'Die Rückseite der Platine nennt die 2 Pads von CH1 VIN1+ und VIN1-. Die Versorgung geht an VIN1+, das Servo an VIN1-.',
    ),
    when: { all: [{ has: 'servo' }, { has: 'ina3221' }] },
    stage: 3, route: { kind: 'X', left: 13, right: 17, channel: 1 }, src: OWNER,
  },
  {
    n: 31, kind: 'stub', a: 'ina3221.A0', b: null,
    to: t('GND, solder bridge on the board', 'GND, Lötbrücke auf der Platine'),
    net: 'ina3221_a0', cls: 'nc',
    signal: t('address strap', 'Adressbrücke'),
    note: t('Address 0x40. No wire.', 'Adresse 0x40. Kein Draht.'),
    when: { has: 'ina3221' }, stage: 3, route: { kind: 'S' }, src: guide('wires'),
  },
  {
    n: 32, kind: 'stub', a: 'ina3221.LP', b: null, to: NOTHING, net: 'ina3221_lp', cls: 'nc',
    signal: NONE,
    note: t(
      'Not connected. Both sides of the board print GND, GND, VPU. What the pads do is not confirmed.',
      'Nicht angeschlossen. Beide Seiten der Platine nennen GND, GND, VPU. Die Funktion der Pads ist nicht bestätigt.',
    ),
    when: { has: 'ina3221' }, stage: 3, route: { kind: 'S' }, src: guide('wires'),
  },
  {
    n: 33, kind: 'wire', a: 'pad.36', b: 'as5600.VCC', net: '3v3', cls: 'logic',
    signal: SUPPLY_33,
    note: t('This board on 3.3 V: not tested.', 'Diese Platine an 3,3 V: nicht getestet.'),
    when: { has: 'as5600' }, stage: 4, route: { kind: 'R', lane: 9 }, src: guide('stage-4'),
  },
  {
    n: 34, kind: 'wire', a: 'pad.23', b: 'as5600.GND', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: null,
    when: { has: 'as5600' }, stage: 4, route: { kind: 'R', lane: 10 }, src: guide('stage-4'),
  },
  {
    n: 35, kind: 'wire', a: 'pad.21', b: 'as5600.SDA', net: 'sda', cls: 'sda',
    signal: SDA,
    note: t('Address 0x36, fixed.', 'Adresse 0x36, fest.'),
    when: { has: 'as5600' }, stage: 4, route: { kind: 'R', lane: 11 }, pin: 'default',
    src: guide('stage-4'),
  },
  {
    n: 36, kind: 'wire', a: 'pad.22', b: 'as5600.SCL', net: 'scl', cls: 'scl',
    signal: SCL,
    note: null,
    when: { has: 'as5600' }, stage: 4, route: { kind: 'R', lane: 12 }, pin: 'default',
    src: guide('stage-4'),
  },
  {
    n: 37, kind: 'stub', a: 'as5600.DIR', b: null,
    to: t('GND or VCC, on the board', 'GND oder VCC, auf der Platine'),
    net: 'as5600_dir', cls: 'nc',
    signal: t('direction strap', 'Richtungsbrücke'),
    note: t(
      'Never leave DIR open. No wire to the coprocessor.',
      'DIR nie offen lassen. Kein Draht zum Koprozessor.',
    ),
    when: { has: 'as5600' }, stage: 4, route: { kind: 'S' }, src: guide('stage-4'),
  },
  {
    n: 38, kind: 'wire', a: 'pad.21', b: 'pull.SDA', net: 'sda', cls: 'sda',
    signal: same('I2C SDA'),
    note: t('2.2 kΩ from SDA to 3V3.', '2,2 kΩ von SDA nach 3V3.'),
    when: { has: 'pull' }, stage: 4, route: { kind: 'R', lane: 13 }, pin: 'default',
    src: guide('wires'),
  },
  {
    n: 39, kind: 'wire', a: 'pad.22', b: 'pull.SCL', net: 'scl', cls: 'scl',
    signal: same('I2C SCL'),
    note: t('2.2 kΩ from SCL to 3V3.', '2,2 kΩ von SCL nach 3V3.'),
    when: { has: 'pull' }, stage: 4, route: { kind: 'R', lane: 14 }, pin: 'default',
    src: guide('wires'),
  },
  {
    n: 40, kind: 'wire', a: 'pad.36', b: 'pull.V', net: '3v3', cls: 'logic',
    signal: SUPPLY_33,
    note: null,
    when: { has: 'pull' }, stage: 4, route: { kind: 'R', lane: 15 }, src: guide('wires'),
  },
  {
    n: 41, kind: 'wire', a: 'pad.40', b: 'ina228.V5', net: 'vbus', cls: 'logic',
    signal: t('supply, 5 V', 'Versorgung, 5 V'),
    note: t('The MATEK board needs 4 to 9 V.', 'Die MATEK-Platine braucht 4 bis 9 V.'),
    when: { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'matek' }] },
    stage: 5, route: { kind: 'R', lane: 0 }, src: guide('wires'),
  },
  {
    n: 42, kind: 'wire', a: 'pad.38', b: 'ina228.G', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: null,
    when: { has: 'ina228' },
    stage: 5, route: { kind: 'R', lane: 1 }, src: guide('wires'),
  },
  {
    n: 43, kind: 'wire', a: 'pad.21', b: 'ina228.SDA', net: 'sda', cls: 'sda',
    signal: SDA,
    note: null,
    when: { has: 'ina228' }, stage: 5, route: { kind: 'R', lane: 2 }, pin: 'default',
    src: guide('wires'),
  },
  {
    n: 44, kind: 'wire', a: 'pad.22', b: 'ina228.SCL', net: 'scl', cls: 'scl',
    signal: SCL,
    note: null,
    when: { has: 'ina228' }, stage: 5, route: { kind: 'R', lane: 3 }, pin: 'default',
    src: guide('wires'),
  },
  {
    n: 45, kind: 'wire', a: 'batt.POS', b: 'ina228.BAT', net: 'batt_pos', cls: 'power',
    signal: ESC_POWER,
    note: SENSOR_ESC,
    when: { all: [{ has: 'esc' }, { has: 'ina228' }, { opt: 'escPower', is: 'battery' }] },
    stage: 5, route: { kind: 'X', left: 14, right: 18, channel: 2 }, src: guide('wires'),
  },
  {
    n: 46, kind: 'wire', a: 'pdmini.OP', b: 'ina228.BAT', net: 'pd_out', cls: 'power',
    signal: t('ESC power, 1 to 20 V', 'ESC-Versorgung, 1 bis 20 V'),
    note: SENSOR_ESC,
    when: { all: [{ has: 'esc' }, { has: 'ina228' }, { opt: 'escPower', is: 'pd' }] },
    stage: 5, route: { kind: 'X', left: 14, right: 18, channel: 2 }, src: guide('wires'),
  },
  {
    n: 47, kind: 'wire', a: 'esc.POS', b: 'ina228.ESC', net: 'esc_shunt', cls: 'power',
    signal: ESC_POWER,
    note: null,
    when: { all: [{ has: 'esc' }, { has: 'ina228' }] },
    stage: 5, route: { kind: 'X', left: 15, right: 19, channel: 3 }, src: guide('wires'),
  },
  {
    n: 48, kind: 'wire', a: 'esc.A', b: 'sim.A', net: 'phase_a', cls: 'phase',
    signal: t('motor phase', 'Motorphase'),
    note: t(
      'Any phase wire to any corner. The tap sits on corner A.',
      'Jede Phasenleitung an jede Ecke. Der Abgriff sitzt an Ecke A.',
    ),
    when: { all: [{ has: 'esc' }, { has: 'sim' }] },
    stage: 5, route: { kind: 'O', lane: 5 }, src: guide('wires'),
  },
  {
    n: 49, kind: 'wire', a: 'esc.B', b: 'sim.B', net: 'phase_b', cls: 'phase',
    signal: t('motor phase', 'Motorphase'),
    note: null,
    when: { all: [{ has: 'esc' }, { has: 'sim' }] },
    stage: 5, route: { kind: 'O', lane: 6 }, src: guide('wires'),
  },
  {
    n: 50, kind: 'wire', a: 'esc.C', b: 'sim.C', net: 'phase_c', cls: 'phase',
    signal: t('motor phase', 'Motorphase'),
    note: null,
    when: { all: [{ has: 'esc' }, { has: 'sim' }] },
    stage: 5, route: { kind: 'O', lane: 7 }, src: guide('wires'),
  },
  {
    n: 51, kind: 'wire', a: 'sim.A', b: 'tap.IN', net: 'phase_a', cls: 'phase',
    signal: t('phase A, up to 25.2 V', 'Phase A, bis 25,2 V'),
    note: t('The resistor sits at the corner.', 'Der Widerstand sitzt an der Ecke.'),
    when: { all: [{ has: 'sim' }, { has: 'tap' }] },
    stage: 5, route: { kind: 'X', left: 16, right: 20, channel: 4 }, src: guide('wires'),
  },
  {
    n: 52, kind: 'wire', a: 'pad.29', b: 'tap.OUT', net: 'tap', cls: 'phase',
    signal: t('tap signal, 3.22 V or less', 'Abgriffsignal, höchstens 3,22 V'),
    note: t(
      'Under 30 cm. No output on GP22 while the tap is on.',
      'Unter 30 cm. Kein Ausgang auf GP22, solange der Abgriff eingeschaltet ist.',
    ),
    when: { has: 'tap' }, stage: 5, route: { kind: 'R', lane: 4 }, pin: 'default',
    src: repo(SETTINGS, 190, 'GP22 is pad 29'),
  },
  {
    n: 53, kind: 'component', a: 'pad.29', b: 'pad.28', net: 'tap', netB: 'gnd', cls: 'phase',
    signal: t('zener clamp', 'Z-Dioden-Klemme'),
    note: t(
      '3.0 V zener diode across the two adjacent pads, band (cathode) to GP22.',
      '3,0-V-Z-Diode über die zwei benachbarten Pads, Ring (Kathode) an GP22.',
    ),
    when: { has: 'tap' }, stage: 5, route: { kind: 'Z' }, pin: 'default',
    src: guide('wires'),
  },
  {
    n: 54, kind: 'stub', a: 'as5600.GPO', b: null,
    to: t('GND, on the board', 'GND, auf der Platine'),
    net: 'gnd', cls: 'gnd',
    signal: t('GPO strap', 'GPO-Brücke'),
    note: t(
      'docs/Building.md names the pin PGO. No wire to the coprocessor.',
      'docs/Building.md nennt den Pin PGO. Kein Draht zum Koprozessor.',
    ),
    when: { has: 'as5600' }, stage: 4, route: { kind: 'S' }, src: repo('docs/Building.md', 107, '| PGO | GND |'),
  },
  {
    n: 55, kind: 'wire', a: 'pad.36', b: 'ina228.V3', net: '3v3', cls: 'logic',
    signal: SUPPLY_33,
    note: t(
      'The pin with the print V. Hole 5 of the header has no pin and takes no wire.',
      'Der Pin mit dem Aufdruck V. Loch 5 der Leiste hat keinen Pin und bekommt keinen Draht.',
    ),
    when: { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'xt90' }] },
    stage: 5, route: { kind: 'R', lane: 0 }, src: OWNER,
  },
  // The module with XT90 plugs carries its shunt in the minus line. The minus
  // of the ESC runs through the module; the wires 15 and 23 are not drawn.
  {
    n: 56, kind: 'wire', a: 'batt.NEG', b: 'ina228.BATN', net: 'batt_neg', cls: 'power',
    signal: ESC_POWER,
    note: SENSOR_ESC_NEG,
    when: { all: [{ has: 'esc' }, XT90_SHUNT, { opt: 'escPower', is: 'battery' }] },
    stage: 5, route: { kind: 'X', left: 17, right: 21, channel: 5 }, src: OWNER,
  },
  {
    n: 57, kind: 'wire', a: 'pdmini.ON', b: 'ina228.BATN', net: 'gnd', cls: 'power',
    signal: ESC_POWER,
    note: SENSOR_ESC_NEG,
    when: { all: [{ has: 'esc' }, XT90_SHUNT, { opt: 'escPower', is: 'pd' }] },
    stage: 5, route: { kind: 'X', left: 17, right: 21, channel: 5 }, src: OWNER,
  },
  {
    n: 58, kind: 'wire', a: 'esc.NEG', b: 'ina228.ESCN', net: 'esc_neg', cls: 'power',
    signal: ESC_POWER,
    note: null,
    when: { all: [{ has: 'esc' }, XT90_SHUNT] },
    stage: 5, route: { kind: 'X', left: 18, right: 22, channel: 6 }, src: OWNER,
  },
  // The servo fed by the bench itself. Wires 59 and 60: the 5 V pin of the
  // coprocessor board. Wires 61 and 62: the V pin of the strip at the servo
  // pin of the Osoyoo breakout board; the strip is no pad of the coprocessor,
  // so the wire ends in the drawing as a stub with its target in words.
  {
    n: 59, kind: 'wire', a: 'servo.POS', b: 'pad.40', net: 'vbus', cls: 'power',
    signal: t('servo power, 5 V from USB', 'Servoversorgung, 5 V von USB'),
    note: SERVO_BENCH_NOTE,
    when: { all: [SERVO_5V_PIN, { not: { has: 'ina3221' } }] },
    stage: 1, route: { kind: 'X', left: 12, right: 16, channel: 0 }, src: OWNER,
  },
  {
    n: 60, kind: 'wire', a: 'pad.40', b: 'ina3221.C1A', net: 'vbus', cls: 'power',
    signal: t('servo power, 5 V from USB', 'Servoversorgung, 5 V von USB'),
    note: SERVO_BENCH_NOTE,
    when: { all: [SERVO_5V_PIN, { has: 'ina3221' }] },
    stage: 3, route: { kind: 'R', lane: 16 }, src: OWNER,
  },
  {
    n: 61, kind: 'stub', a: 'servo.POS', b: null, to: STRIP_V, net: 'strip_v', cls: 'power',
    signal: t('servo power, 5 V by the jumper', 'Servoversorgung, 5 V je nach Jumper'),
    note: SERVO_BENCH_NOTE,
    when: { all: [SERVO_STRIP, { not: { has: 'ina3221' } }] },
    stage: 1, route: { kind: 'S' }, src: OWNER,
  },
  {
    n: 62, kind: 'stub', a: 'ina3221.C1A', b: null, to: STRIP_V, net: 'strip_v', cls: 'power',
    signal: t('servo power, 5 V by the jumper', 'Servoversorgung, 5 V je nach Jumper'),
    note: SERVO_BENCH_NOTE,
    when: { all: [SERVO_STRIP, { has: 'ina3221' }] },
    stage: 3, route: { kind: 'S' }, src: OWNER,
  },
  // The second half of the power path of the PD mini. Wire 19 is the first:
  // charger to the socket IN of the T-plug.
  {
    n: 63, kind: 'wire', a: 'usbtap.OUT', b: 'pdmini.USB', net: 'pd_in', cls: 'power',
    signal: t('PD in', 'PD-Eingang'),
    note: t(
      'USB-C cable, plugged into the socket OUT and into the USB-C socket of the PD mini. The current rating the cable needs is not stated. Output of the PD mini is at most input less 0.5 V.',
      'USB-C-Kabel, in die Buchse OUT und in die USB-C-Buchse des PD mini gesteckt. Die nötige Strombelastbarkeit des Kabels ist nicht angegeben. Ausgang des PD mini höchstens Eingang minus 0,5 V.',
    ),
    when: { has: 'pdmini' }, stage: 2, route: { kind: 'O', lane: 4 }, src: OWNER,
  },
];

// ---------------------------------------------------------------------------
// Functions. `src` is the statement of the function in the docs of the release.
//
// A function with `needs: []` runs on the panel and the coprocessor alone. It
// is enabled in every setup and the page offers no tick for it.
// ---------------------------------------------------------------------------

const functions: Fn[] = [
  {
    id: 'link', group: 'bench',
    name: t('CAN link at 1 Mbit/s with an echo self-test at every start', 'CAN-Verbindung mit 1 Mbit/s und Echo-Selbsttest bei jedem Start'),
    needs: [], src: repo('STATUS.md', 110, 'CAN drivers (TWAI on the panel, XL2515 on the coprocessor) and the echo self-test'),
  },
  {
    id: 'interlock', group: 'bench',
    name: t('ARM by a 2 s hold, DISARM and the heartbeat interlock', 'ARM durch 2 s Halten, DISARM und Heartbeat-Verriegelung'),
    // Arming works on the bring-up bench. The decision to arm is tested on
    // the host only; the check entry `interlock-state` carries both.
    needs: [], src: repo('STATUS.md', 113, 'The decision to arm is `shared/safety/safety_gate.c`'),
  },
  {
    id: 'stop', group: 'bench',
    name: t('STOP command over the link', 'STOP-Befehl über die Verbindung'),
    needs: [], src: repo('README.md', 36, 'and a STOP command over the link'),
  },
  {
    id: 'bind', group: 'bench',
    name: t('Bind protocols to pads on the OUTPUTS screen', 'Protokolle auf dem OUTPUTS-Bildschirm an Pads binden'),
    needs: [], src: repo('STATUS.md', 117, 'Ticking or unticking a pin writes the binding and reads it back'),
  },
  {
    id: 'signals', group: 'bench',
    name: t('Send PWM and DShot signals on bound pads', 'PWM- und DShot-Signale auf gebundenen Pads ausgeben'),
    needs: [], src: repo('STATUS.md', 114, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
  },
  {
    id: 'sweep', group: 'bench',
    name: t(
      'Servo signal: sweep (square, sine, triangle) and frame rates other than 50 Hz',
      'Servosignal: Sweep (Rechteck, Sinus, Dreieck) und andere Frame-Raten als 50 Hz',
    ),
    needs: [],
    src: repo('STATUS.md', 105, 'a sweep (square, sine or triangle, run by the coprocessor)'),
  },
  {
    id: 'ppm', group: 'bench',
    name: t('Send a PPM signal with 8 channels on 1 pad', 'PPM-Signal mit 8 Kanälen auf 1 Pad ausgeben'),
    needs: [], src: repo('STATUS.md', 114, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
  },
  {
    id: 'settings', group: 'bench',
    name: t('Settings on the SETUP screen, kept in the flash of the panel', 'Einstellungen auf dem SETUP-Bildschirm, im Flash des Panels gespeichert'),
    needs: [],
    src: repo('STATUS.md', 99, 'The panel loads and saves the values in NVS'),
  },
  {
    id: 'language', group: 'bench',
    name: t('User interface in English or German', 'Bedienoberfläche auf Englisch oder Deutsch'),
    // The check entry `language-state` carries the state.
    needs: [], src: repo('STATUS.md', 98, 'English and German, chosen by SETUP'),
  },
  {
    id: 'profiles', group: 'bench',
    name: t('Browse and search the ESC profiles: 72 families, 451 models', 'ESC-Profile durchsuchen: 72 Familien, 451 Modelle'),
    // No statement of a run of the list on a panel. The check entry
    // `profiles-state` carries it.
    needs: [], src: repo('STATUS.md', 126, '72 families, 451 models, from 153 manuals'),
  },
  {
    id: 'servo', group: 'servo',
    name: t('Drive a servo', 'Servo ansteuern'),
    needs: [], drives: 'servo', src: repo('STATUS.md', 114, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
  },
  {
    id: 'servotest', group: 'servo',
    name: t('Automatic servo test', 'Automatischer Servotest'),
    // docs/Servo.md names three runs of 0.13.0; docs/Screens.md line 753
    // states the opposite. The check entry `servotest-state` carries it.
    // The test reads the current of the PD mini (docs/Servo.md, line 92).
    needs: ['pdmini'], drives: 'servo', src: repo('docs/Servo.md', 92, 'the current of the supply that feeds the servo: the PD mini'),
  },
  {
    id: 'servocur', group: 'servo',
    name: t('Servo current from 1 ms samples', 'Servostrom aus 1-ms-Abtastwerten'),
    needs: ['ina3221'], drives: 'servo', src: repo('STATUS.md', 119, 'Current monitors (INA228, INA3221)'),
  },
  {
    id: 'angle', group: 'servo',
    name: t('Angle of the servo horn', 'Winkel des Servohorns'),
    needs: ['as5600'], drives: 'servo', src: repo('STATUS.md', 121, 'Output encoder (AS5600)'),
  },
  {
    id: 'esc', group: 'esc',
    name: t('Drive an ESC and motor', 'ESC und Motor ansteuern'),
    needs: [], drives: 'esc', src: repo('STATUS.md', 114, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
  },
  {
    id: 'escpow', group: 'esc',
    name: t('ESC voltage, current, energy', 'Spannung, Strom, Energie des ESC'),
    needs: ['ina228'], drives: 'esc', src: repo('STATUS.md', 119, 'Current monitors (INA228, INA3221)'),
  },
  {
    id: 'stick', group: 'esc',
    name: t('ESC STICK programming', 'ESC-STICK-Programmierung'),
    // The bench powers the ESC from the PD mini and counts the beeps from the
    // supply current (docs/StickProgramming.md, line 6).
    needs: ['pdmini'], drives: 'esc', forces: { escPower: 'pd' },
    src: repo('STATUS.md', 127, 'the ESC STICK class on PROGRAMMER runs 24 of the 72 profiles'),
  },
  {
    id: 'tap', group: 'esc',
    name: t('Read the beeps of the ESC (phase tap)', 'ESC-Töne lesen (Phasenabgriff)'),
    needs: ['sim', 'tap'], drives: 'esc', src: repo('STATUS.md', 122, 'Phase tap (ESC tones)'),
  },
  {
    id: 'supply', group: 'bench',
    name: t('Supply set from the bench, 1 to 20 V', 'Versorgung vom Prüfstand gestellt, 1 bis 20 V'),
    needs: ['pdmini'], src: repo('STATUS.md', 107, 'the coprocessor runs it on a PIO UART on the pins the SUPPLY page'),
  },
  {
    id: 'logs', group: 'bench',
    name: t('Run logs on a card', 'Laufprotokolle auf Karte'),
    needs: ['sd'], src: repo('STATUS.md', 101, 'Logger | built: a run is written while armed'),
  },
  {
    id: 'estop', group: 'bench',
    name: t('Emergency stop button', 'Not-Aus-Taster'),
    // The docs of the release hold no statement of a run with the button.
    needs: ['estop'], src: guide('stage-5'),
  },
];

// ---------------------------------------------------------------------------
// Rules that add a part. Evaluated in array order, once. A rule reads only
// parts that earlier steps add, so one pass is complete.
// ---------------------------------------------------------------------------

const rules: Rule[] = [
  {
    id: 'esc-pd',
    when: { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }] },
    adds: 'pdmini',
    because: t('ESC power B', 'ESC-Versorgung B'),
    src: guide('wires'),
  },
  {
    id: 'pd-charger',
    when: { has: 'pdmini' },
    adds: 'charger',
    because: same('PD mini'),
    src: guide('also-needed'),
  },
  {
    id: 'pd-tplug',
    when: { has: 'pdmini' },
    adds: 'usbtap',
    because: t('UART of the PD mini', 'UART des PD mini'),
    src: OWNER,
  },
  {
    id: 'servo-supply',
    when: SERVO_EXTERNAL,
    adds: 'ssup',
    because: same('Servo'),
    src: guide('stage-1'),
  },
  {
    id: 'esc-battery',
    when: { all: [{ has: 'esc' }, { opt: 'escPower', is: 'battery' }] },
    adds: 'batt',
    because: same('ESC'),
    src: guide('wires'),
  },
  {
    id: 'bus-pullups',
    when: { all: [{ any: [{ has: 'ina228' }, { has: 'as5600' }] }, { not: { has: 'ina3221' } }] },
    adds: 'pull',
    because: t('sensor bus without the INA3221', 'Sensorbus ohne den INA3221'),
    src: guide('wires'),
  },
];

// ---------------------------------------------------------------------------
// Checks. Kind 'unknown' states what is not known and what the page does.
// ---------------------------------------------------------------------------

const checks: Check[] = [
  {
    id: 'can-60', kind: 'measure',
    text: t(
      'CAN H to CAN L, everything unpowered: about 60 Ω. A reading of 120 Ω means one terminator is missing.',
      'CAN H gegen CAN L, alles stromlos: etwa 60 Ω. 120 Ω heißt: Ein Abschluss fehlt.',
    ),
    when: null, src: guide('notes'),
  },
  {
    id: 'panel-terminals', kind: 'note',
    text: t(
      'Panel: the CAN socket has the print H and L. The 3-pin socket J8 has the print Sensor AD with the pins AD, GND and 3V3. The heartbeat (GPIO6) is the pin AD. The pin 3V3 takes no wire.',
      'Panel: Die CAN-Buchse trägt den Aufdruck H und L. Die 3-polige Buchse J8 trägt den Aufdruck Sensor AD mit den Pins AD, GND und 3V3. Der Heartbeat (GPIO6) ist der Pin AD. Der Pin 3V3 bekommt keinen Draht.',
    ),
    when: null, src: OWNER,
  },
  {
    id: 'same-release', kind: 'note',
    text: t(
      'Both boards run the same release. With different protocol versions the panel shows "protocol mismatch -- will not arm".',
      'Beide Platinen laufen mit demselben Release. Bei verschiedenen Protokollversionen zeigt das Panel "protocol mismatch -- will not arm".',
    ),
    when: null, src: repo('docs/Link.md', 79, 'protocol mismatch -- will not arm'),
  },
  {
    id: 'servo-pin', kind: 'note',
    text: t(
      'GP2 for the servo is a proposal of this page. The example of the build guide is GP0. GP0 and GP16 share a compare register, and GP16 carries the sensor bus. Whether the firmware refuses that pair: not stated.',
      'GP2 für das Servo ist ein Vorschlag dieser Seite. Das Beispiel der Bauanleitung ist GP0. GP0 und GP16 teilen sich ein Vergleichsregister, und GP16 trägt den Sensorbus. Ob die Firmware dieses Paar ablehnt: nicht angegeben.',
    ),
    when: { has: 'servo' }, src: repo('docs/DShot.md', 110, 'Six of those pairs are both free on the header the coprocessor offers: GP0 and'),
  },
  {
    id: 'pd-pins', kind: 'note',
    text: t(
      'GP7 (PD mini TX) and GP6 (PD mini RX) come from an owner statement of 2026-10-11. The firmware default is −1, not wired. Set both under SETUP → INTERFACES.',
      'GP7 (PD mini TX) und GP6 (PD mini RX) stammen aus einer Angabe des Projektinhabers vom 2026-10-11. Die Voreinstellung der Firmware ist −1, nicht verdrahtet. Beide unter SETUP → INTERFACES einstellen.',
    ),
    when: { has: 'pdmini' }, src: OWNER,
  },
  {
    id: 'pd-shared', kind: 'unknown',
    text: t(
      'Servo and ESC on the one PD mini output at the same time: not stated in the build guide.',
      'Servo und ESC gleichzeitig an dem einen Ausgang des PD mini: in der Bauanleitung nicht angegeben.',
    ),
    when: { all: [{ has: 'servo' }, { has: 'esc' }, { opt: 'escPower', is: 'pd' }] },
    src: PAGE,
  },
  {
    id: 'ina3221-a0', kind: 'note',
    text: t(
      'INA3221 as delivered: all A0 bridges open, the address changes between 0x40 and 0x41. Set the A0 bridge to GND.',
      'INA3221 im Lieferzustand: alle A0-Brücken offen, die Adresse wechselt zwischen 0x40 und 0x41. Die A0-Brücke nach GND setzen.',
    ),
    when: { has: 'ina3221' }, src: guide('wires'),
  },
  {
    id: 'ina3221-pullups', kind: 'unknown',
    text: t(
      'Pull-ups with the INA3221 on the bus: the sources disagree. The build guide lists none, because the INA3221 board carries 10 kΩ pull-ups. docs/FirstRun.md lists 2.2 kΩ from SDA and from SCL to 3V3 (pad 36) as well. The drawing follows the build guide. No measurement of the bus at 400 kHz is stated.',
      'Pull-ups mit dem INA3221 am Bus: Die Quellen widersprechen sich. Die Bauanleitung nennt keine, weil die INA3221-Platine 10-kΩ-Pull-ups trägt. docs/FirstRun.md nennt zusätzlich je 2,2 kΩ von SDA und SCL nach 3V3 (Pad 36). Die Zeichnung folgt der Bauanleitung. Eine Messung des Busses bei 400 kHz ist nicht angegeben.',
    ),
    when: { has: 'ina3221' }, src: repo('docs/FirstRun.md', 291, '| Pull-ups | 2.2 kΩ from SDA and from SCL | 3V3 (pad 36) |'),
  },
  {
    id: 'matek-pullup', kind: 'measure',
    text: t(
      'MATEK board: the pull-up rail is not known. With the board powered and the coprocessor not connected, SDA and SCL read 3.3 V or less to GND.',
      'MATEK-Platine: Die Pull-up-Schiene ist nicht bekannt. Platine versorgt, Koprozessor noch nicht verbunden: SDA und SCL liegen bei höchstens 3,3 V gegen GND.',
    ),
    when: { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'matek' }] },
    src: guide('wires'),
  },
  {
    id: 'ina228-xt90', kind: 'note',
    text: t(
      'INA228 module with XT90 plugs: GND, SDA and SCL on the sensor bus as on the MATEK I2C-INA-BM board, and the same I2C address. The supply is 3.3 V from 3V3 (pad 36), not 5 V from VBUS (pad 40). This comes from an owner statement of 2026-10-11.',
      'INA228-Modul mit XT90-Steckern: GND, SDA und SCL am Sensorbus wie bei der MATEK-I2C-INA-BM-Platine, dieselbe I2C-Adresse. Die Versorgung ist 3,3 V von 3V3 (Pad 36), nicht 5 V von VBUS (Pad 40). Das stammt aus einer Angabe des Projektinhabers vom 2026-10-11.',
    ),
    when: { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'xt90' }] },
    src: OWNER,
  },
  {
    id: 'ina228-xt90-pads', kind: 'note',
    text: t(
      'INA228 module with XT90 plugs: the header has 5 holes with the print L A V G 5. L is SCL, A is SDA, V is the 3.3 V supply of the INA228, G is GND. Hole 5 is the 5 V output of a regulator that is not fitted: it has no pin and takes no wire. Seen with the header at the bottom, the left plug (shrouded, male pins) is the battery side and the right plug (female) is the ESC side. The upper rail is plus, the lower rail with the shunt is minus, and G connects to the ESC end of the shunt. Plus and minus of the ESC both run through the 2 plugs: no wire joins the minus of the source and the minus of the ESC past the module.',
      'INA228-Modul mit XT90-Steckern: Die Leiste hat 5 Löcher mit dem Aufdruck L A V G 5. L ist SCL, A ist SDA, V ist die 3,3-V-Versorgung des INA228, G ist GND. Loch 5 ist der 5-V-Ausgang eines nicht bestückten Reglers: Es hat keinen Pin und bekommt keinen Draht. Mit der Leiste unten gesehen ist der linke Stecker (mit Kragen, Stifte) die Akkuseite und der rechte Stecker (Buchsen) die ESC-Seite. Die obere Schiene ist Plus, die untere Schiene mit dem Shunt ist Minus, und G liegt am ESC-Ende des Shunts. Plus und Minus des ESC laufen beide durch die 2 Stecker: Kein Draht verbindet das Minus der Quelle und das Minus des ESC am Modul vorbei.',
    ),
    when: { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'xt90' }] },
    src: OWNER,
  },
  {
    id: 'ina228-xt90-pd', kind: 'unknown',
    text: t(
      'INA228 module with XT90 plugs and ESC power B: G of the module is the ESC end of the shunt and is wired to GND of the coprocessor. A connection between the minus output of the PD mini and GND of the coprocessor is a second path beside the shunt. The share of the ESC current that the sensor reads in this setup is not measured.',
      'INA228-Modul mit XT90-Steckern und ESC-Versorgung B: G des Moduls ist das ESC-Ende des Shunts und liegt an GND des Koprozessors. Eine Verbindung zwischen dem Minus-Ausgang des PD mini und GND des Koprozessors ist ein zweiter Pfad neben dem Shunt. Der Anteil des ESC-Stroms, den der Sensor in diesem Aufbau misst, ist nicht gemessen.',
    ),
    when: { all: [{ has: 'esc' }, XT90_SHUNT, { opt: 'escPower', is: 'pd' }] },
    src: OWNER,
  },
  {
    id: 'as5600-board', kind: 'unknown',
    text: t(
      'AS5600 board: pull-ups on the board are not known. The print of the pin GPO, named PGO in Building.md, is not confirmed. It is tied to GND.',
      'AS5600-Platine: Die Pull-ups auf der Platine sind nicht bekannt. Der Aufdruck des Pins GPO, in Building.md PGO genannt, ist nicht bestätigt. Er liegt an GND.',
    ),
    when: { has: 'as5600' }, src: repo('docs/Building.md', 107, '| PGO | GND |'),
  },
  {
    id: 'tap-ground', kind: 'measure',
    text: t(
      'Phase tap: everything unpowered, ESC power negative to coprocessor GND (pad 28) reads near 0 Ω. With an opto-isolated ESC input the tap reads nothing.',
      'Phasenabgriff: alles stromlos, Minus der ESC-Versorgung gegen GND des Koprozessors (Pad 28) liest nahe 0 Ω. Bei einem ESC-Eingang mit Optokoppler liest der Abgriff nichts.',
    ),
    when: { has: 'tap' }, src: guide('tap'),
  },
  {
    id: 'sim-ground', kind: 'note',
    text: t(
      'No ground wire from the simulator to the coprocessor. It runs parallel to the ground of the ESC and can carry motor current.',
      'Kein Massedraht vom Simulator zum Koprozessor. Er liegt parallel zur Masse des ESC und kann Motorstrom führen.',
    ),
    when: { has: 'sim' }, src: guide('tap'),
  },
  {
    id: 'estop-tabs', kind: 'measure',
    text: t(
      'Emergency stop: 3 tabs in a row, C, NO, NC. The names come from the wiring diagram of the seller, not from a measurement. The 2 outer tabs C and NC conduct while the button is released and open when it is pressed. The middle tab NO stays unconnected. Before use, C to NC reads near 0 Ω with the button released and open with the button pressed. No run with the button is stated in the docs.',
      'Not-Aus: 3 Lötfahnen in einer Reihe, C, NO, NC. Die Namen stammen aus dem Anschlussbild des Verkäufers, nicht aus einer Messung. Die 2 äußeren Fahnen C und NC leiten, solange der Taster nicht gedrückt ist, und öffnen, wenn er gedrückt wird. Die mittlere Fahne NO bleibt unbeschaltet. Vor dem Einsatz liest C gegen NC nahe 0 Ω bei nicht gedrücktem Taster und offen bei gedrücktem Taster. Die Dokumente nennen keinen Lauf mit dem Taster.',
    ),
    when: { has: 'estop' }, src: OWNER,
  },
  {
    id: 'usbtap-v', kind: 'note',
    text: t(
      'T-plug: the V pad carries no wire. Without the PD mini the T-plug has no wire.',
      'T-Stecker: Das Pad V bekommt keinen Draht. Ohne den PD mini hat der T-Stecker keinen Draht.',
    ),
    when: { has: 'usbtap' }, src: OWNER,
  },
  {
    id: 'osoyoo-labels', kind: 'unknown',
    text: t(
      'Osoyoo breakout board: the terminal labels are read from one photo and partly legible. The board is not in the build guide.',
      'Osoyoo-Breakout-Platine: Die Klemmenbeschriftung stammt von einem Foto und ist teilweise lesbar. Die Platine steht nicht in der Bauanleitung.',
    ),
    when: { opt: 'connection', is: 'osoyoo' }, src: photo('05'),
  },
  {
    id: 'osoyoo-terminals', kind: 'note',
    text: t(
      'Osoyoo breakout board: every IO has a strip of 3 pins. From the terminal inward: S, V, G. The DC-DC converter of the board feeds the V pins; a jumper sets them to 3.3 V or 5 V. An ESC lead goes to S and G only: the V pin in the middle stays free.',
      'Osoyoo-Breakout-Platine: Jeder IO hat eine Leiste mit 3 Pins. Von der Klemme nach innen: S, V, G. Der DC-DC-Wandler der Platine speist die V-Pins; ein Jumper stellt sie auf 3,3 V oder 5 V. Ein ESC-Kabel geht nur an S und G: Der V-Pin in der Mitte bleibt frei.',
    ),
    when: { opt: 'connection', is: 'osoyoo' }, src: OWNER,
  },
  {
    id: 'strip-esc', kind: 'note',
    text: t(
      'Osoyoo breakout board: the ESC lead uses the S and G pins of the strip at GP5 only. The V pin in the middle carries 3.3 V or 5 V and stays free: the plus wire of the ESC lead is connected nowhere.',
      'Osoyoo-Breakout-Platine: Das ESC-Kabel nutzt nur die Pins S und G der Leiste an GP5. Der V-Pin in der Mitte führt 3,3 V oder 5 V und bleibt frei: Die Plusader des ESC-Kabels wird nirgends angeschlossen.',
    ),
    when: { all: [{ has: 'esc' }, { opt: 'connection', is: 'osoyoo' }] }, src: OWNER,
  },
  {
    id: 'servo-external', kind: 'note',
    text: t(
      'Servo supply: a 2S LiPo battery or another external supply that matches the servo. A small servo can run from the bench itself: variant "Servo power", B.',
      'Servoversorgung: ein 2S-LiPo-Akku oder eine andere externe Versorgung, die zum Servo passt. Ein kleines Servo kann vom Prüfstand selbst laufen: Variante „Servoversorgung“, B.',
    ),
    when: SERVO_EXTERNAL, src: OWNER,
  },
  {
    id: 'servo-bench-supply', kind: 'note',
    text: t(
      'The bench itself feeds the servo. The servo then draws its current from the supply that also powers the logic. This is prone to problems. For anything but a small servo use a 2S LiPo battery or another external supply that matches the servo.',
      'Der Prüfstand selbst speist das Servo. Das Servo zieht seinen Strom dann aus der Versorgung, die auch die Logik speist. Das ist störanfällig. Für alles außer einem kleinen Servo einen 2S-LiPo-Akku oder eine andere externe Versorgung verwenden, die zum Servo passt.',
    ),
    when: SERVO_BENCH, src: OWNER,
  },
  {
    id: 'servo-5v-pin', kind: 'unknown',
    text: t(
      'Current limit of the 5 V pin of the coprocessor board (VBUS, pad 40): not stated. How much current a small servo is: not stated.',
      'Stromgrenze des 5-V-Pins der Koprozessor-Platine (VBUS, Pad 40): nicht angegeben. Wie viel Strom ein kleines Servo ist: nicht angegeben.',
    ),
    when: SERVO_5V_PIN, src: OWNER,
  },
  {
    id: 'strip-jumper', kind: 'measure',
    text: t(
      'Osoyoo breakout board: set the jumper to 5 V for the servo. Measure the V pin of the strip at GP2 to GND before the servo is plugged in: about 5 V.',
      'Osoyoo-Breakout-Platine: Den Jumper für das Servo auf 5 V setzen. Vor dem Einstecken des Servos den V-Pin der Leiste an GP2 gegen GND messen: etwa 5 V.',
    ),
    when: SERVO_STRIP, src: OWNER,
  },
  {
    id: 'strip-plug', kind: 'note',
    text: t(
      'Osoyoo breakout board: the servo lead plugs onto the strip at GP2. S carries the signal of wire 7, V the plus of wire 61 and G the GND of wire 8. The plug replaces these 3 wires.',
      'Osoyoo-Breakout-Platine: Das Servokabel steckt auf der Leiste an GP2. S führt das Signal von Draht 7, V das Plus von Draht 61 und G das GND von Draht 8. Der Stecker ersetzt diese 3 Drähte.',
    ),
    when: { all: [SERVO_STRIP, { not: { has: 'ina3221' } }] }, src: OWNER,
  },
  {
    id: 'strip-supply', kind: 'unknown',
    text: t(
      'Osoyoo breakout board: what feeds the DC-DC converter and its input range are not stated. Its output current is not stated. Whether the jumper sets the whole board or a group of strips is not stated. The page treats the V pins as fed from the supply of the logic.',
      'Osoyoo-Breakout-Platine: Was den DC-DC-Wandler speist und sein Eingangsbereich sind nicht angegeben. Sein Ausgangsstrom ist nicht angegeben. Ob der Jumper die ganze Platine oder eine Gruppe von Leisten stellt, ist nicht angegeben. Die Seite behandelt die V-Pins als aus der Versorgung der Logik gespeist.',
    ),
    when: SERVO_STRIP, src: OWNER,
  },
  {
    id: 'esc-programming-power', kind: 'unknown',
    text: t(
      'ESC programming: an owner statement of 2026-10-11 says it needs no power source of the bench. docs/StickProgramming.md of release 0.16.0 powers the ESC from the PD mini and counts the beeps from the supply current. The page follows the document: ESC STICK programming needs the PD mini. What powers an ESC during other programming: not stated.',
      'ESC-Programmierung: Eine Angabe des Projektinhabers vom 2026-10-11 nennt keine Stromquelle des Prüfstands als nötig. docs/StickProgramming.md von Release 0.16.0 versorgt den ESC aus dem PD mini und zählt die Töne am Versorgungsstrom. Die Seite folgt dem Dokument: Die ESC-STICK-Programmierung braucht den PD mini. Was einen ESC bei anderer Programmierung versorgt: nicht angegeben.',
    ),
    when: { all: [{ has: 'esc' }, { has: 'pdmini' }, { opt: 'escPower', is: 'pd' }] },
    src: repo('docs/StickProgramming.md', 6, 'PD mini'),
  },
];

// ---------------------------------------------------------------------------
// Stages, safety notes, the list of items in no release. Build guide wording.
// ---------------------------------------------------------------------------

const stages: Stage[] = [
  { n: 1, title: t('Stage 1: panel and coprocessor', 'Stufe 1: Panel und Koprozessor'), src: guide('stage-1') },
  {
    n: 2,
    title: t(
      'Stage 2: PD mini, the supply the bench controls',
      'Stufe 2: PD mini, die vom Prüfstand gesteuerte Versorgung',
    ),
    src: guide('stage-2'),
  },
  {
    n: 3,
    title: t(
      'Stage 3: INA3221, servo current from 1 ms samples',
      'Stufe 3: INA3221, Servostrom aus 1-ms-Abtastwerten',
    ),
    src: guide('stage-3'),
  },
  {
    n: 4,
    title: t(
      "Stage 4: AS5600, the angle of the servo's horn",
      'Stufe 4: AS5600, der Winkel des Servohorns',
    ),
    src: guide('stage-4'),
  },
  { n: 5, title: t('Stage 5: the remaining parts', 'Stufe 5: die übrigen Teile'), src: guide('stage-5') },
];

const safety: Catalogue['safety'] = [
  {
    text: t(
      '<b>No propeller.</b> The motor screwed down, or the motor simulator (stage 5) in place of the motor.',
      '<b>Kein Propeller.</b> Motor festschrauben oder statt des Motors den Motorsimulator (Stufe 5) anschließen.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      "<b>ESC power from two sources only:</b> A, the ESC's own battery, or B, the PD mini output within its limits: 1 to 20 V, up to 3 A, and at most its input voltage less 0.5 V. A set point over the input puts the module into ERR until it is power cycled.",
      '<b>Versorgung des ESC nur aus zwei Quellen:</b> A, dem eigenen Akku des ESC, oder B, dem Ausgang des PD mini innerhalb seiner Grenzen: 1 bis 20 V, bis 3 A und höchstens Eingangsspannung minus 0,5 V. Ein Sollwert über der Eingangsspannung versetzt das Modul in den Zustand ERR, bis es aus- und wieder eingeschaltet wird.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>The panel and the coprocessor never power the ESC.</b> Keep their 5 V and 3.3 V pins away from the ESC.',
      '<b>Panel und Koprozessor versorgen den ESC nie.</b> Ihre 5-V- und 3,3-V-Pins bleiben vom ESC getrennt.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>ESC lead: only signal and ground</b> go to the coprocessor. The centre wire (BEC +) is connected nowhere.',
      '<b>ESC-Kabel: nur Signal und Masse</b> zum Koprozessor. Die Mittelader (BEC +) wird nirgends angeschlossen.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>STOP</b> disarms, switches the PD mini output off, and latches until the next ARM. Arming is a 2 s hold on ARM; disarming is a press; leaving a bench screen disarms.',
      '<b>STOP</b> entschärft, schaltet den Ausgang des PD mini ab und rastet bis zum nächsten ARM ein. Scharf schalten: ARM 2 s halten. Entschärfen: kurz tippen. Auch das Verlassen einer Prüfstandsseite entschärft.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>The heartbeat wire is the interlock.</b> The monostable that should back it in hardware is on no board yet, so the firmware at both ends is the whole interlock. A coprocessor that hangs while armed is not stopped by anything.',
      '<b>Der Heartbeat-Draht ist die Verriegelung.</b> Das Monoflop, das ihn in Hardware absichern soll, sitzt noch auf keiner Platine; die Verriegelung ist allein die Firmware beider Platinen. Hängt der Koprozessor im scharfen Zustand, stoppt ihn nichts.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      'An armed bench drives every bound pin. A pin bound as SERVO PWM rests at 1500 µs, which an ESC can read as about half throttle. Bind an ESC as MOTOR PWM or DShot only.',
      'Ein scharfer Prüfstand treibt jeden gebundenen Pin. Ein als SERVO PWM gebundener Pin ruht bei 1500 µs, für einen ESC etwa Halbgas. Einen ESC deshalb nur als MOTOR PWM oder DShot binden.',
    ),
    src: guide('safety'),
  },
];

const notInRelease: Catalogue['notInRelease'] = [
  {
    id: 'kst',
    text: t(
      'KST and Chaservo servo programming port: on main, in no release.',
      'Programmieranschluss für KST- und Chaservo-Servos: auf main, in keinem Release.',
    ),
    release: 'main',
    src: repo('STATUS.md', 492, 'KST servo programming'),
  },
  {
    id: 'knob',
    text: t(
      'Rotary knob: the docs disagree. The page follows the build guide: no knob is read on this panel.',
      'Drehknopf: Die Dokumente widersprechen sich. Die Seite folgt der Bauanleitung: An diesem Panel wird kein Knopf gelesen.',
    ),
    release: 'planned',
    src: guide('stage-5'),
  },
  {
    id: 'mt6701',
    text: t(
      'MT6701 magnetic encoder board: not read by 0.16.0.',
      'MT6701-Magnetencoder-Platine: wird von 0.16.0 nicht gelesen.',
    ),
    release: 'planned',
    src: guide('parts'),
  },
  {
    id: 'receivers',
    text: t(
      'Receiver buses (S.BUS, CRSF and others): no receiver is read.',
      'Empfängerbusse (S.BUS, CRSF und andere): Es wird kein Empfänger gelesen.',
    ),
    release: 'planned',
    src: guide('stage-5'),
  },
  {
    id: 'escserial',
    text: t(
      'Serial ESC programming (BLHeli_S, AM32 and others): no protocol on a wire.',
      'Serielle ESC-Programmierung (BLHeli_S, AM32 und andere): kein Protokoll auf einem Draht.',
    ),
    release: 'planned',
    src: repo('STATUS.md', 530, 'BLHeli_S and AM32, Hitec on the servo side | screen built; no protocol on a wire'),
  },
];

// ---------------------------------------------------------------------------
// Pins. The visitor does not choose pins; the data states one per signal.
//
// allowed: the values the firmware offers. 'catalogue' is any GPIO of the
// output catalogue in out_bind.c that is not reserved.
// ---------------------------------------------------------------------------

const pinDefaults: PinDefault[] = [
  { signal: 'heartbeat', gpio: 3, pad: 5, choice: 'fixed', role: 'input', allowed: [3], src: repo(IOMCU_PINS, 43, '#define IOMCU_HEARTBEAT_PIN 3') },
  {
    signal: 'sda', gpio: 16, pad: 21, choice: 'default', role: 'bus',
    // settings_sense_sda_valid(): −1 and the SDA pins whose pair is free.
    allowed: [-1, 0, 4, 6, 14, 16, 18, 20, 26],
    src: repo(SETTINGS, 160, '"sns_sda", "Sensor SDA"'),
  },
  {
    signal: 'scl', gpio: 17, pad: 22, choice: 'default', role: 'bus',
    // SCL follows SDA: the GPIO after it.
    allowed: [-1, 1, 5, 7, 15, 17, 19, 21, 27],
    src: repo(SETTINGS, 163, '"sns_scl", "Sensor SCL"'),
  },
  { signal: 'tap', gpio: 22, pad: 29, choice: 'default', role: 'input', allowed: 'catalogue', src: repo(SETTINGS, 190, '"tone_pin", "Tap pin"') },
  { signal: 'esc', gpio: 5, pad: 7, choice: 'example', role: 'output', allowed: 'catalogue', src: guide('wires') },
  { signal: 'servo', gpio: 2, pad: 4, choice: 'proposal', role: 'output', allowed: 'catalogue', src: PAGE },
  { signal: 'pd_tx', gpio: 7, pad: 10, choice: 'proposal', role: 'uart', allowed: 'catalogue', src: OWNER },
  { signal: 'pd_rx', gpio: 6, pad: 9, choice: 'proposal', role: 'uart', allowed: 'catalogue', src: OWNER },
];

// ---------------------------------------------------------------------------
// Power sources: what feeds a test object and the logic, with advantages,
// drawbacks and unknowns. `label` is the short name; `text` stands on its
// own. Every other line is one sentence.
//
// The numbers of the PD mini are vendor values. No source states a current
// for the 5 V pin or for the V pins of the Osoyoo breakout board.
// ---------------------------------------------------------------------------

const VENDOR = t('Vendor values, not measured.', 'Herstellerangaben, nicht gemessen.');
const OWN_SUPPLY_TEXT = t(
  'GND joins the GND of the bench. The signal comes from the bench. Plus goes to the servo only, through channel 1 of the INA3221 when it is fitted.',
  'GND liegt an GND des Prüfstands. Das Signal kommt vom Prüfstand. Plus geht nur an das Servo, über Kanal 1 des INA3221, wenn er eingebaut ist.',
);
const NOT_SET = t('The bench does not set it.', 'Der Prüfstand stellt sie nicht.');
const LOGIC_DRAW = t(
  'It draws from the supply that also powers the logic.',
  'Sie zieht aus der Versorgung, die auch die Logik speist.',
);
const LOGIC_PEAK = t(
  'A current peak of the servo can drop the logic voltage and reset the boards.',
  'Eine Stromspitze des Servos kann die Logikspannung einbrechen lassen und die Platinen zurücksetzen.',
);
const ESC_SIGNAL_ONLY = t(
  'The bench gives the ESC signal and ground only.',
  'Der Prüfstand gibt dem ESC nur Signal und Masse.',
);

const powerSources: PowerSource[] = [
  {
    id: 'usb', feeds: 'logic',
    label: t('USB, 5 V, one supply per board', 'USB, 5 V, je Platine eine Versorgung'),
    text: t(
      'The panel and the coprocessor each run from their own USB-C cable. The panel is on a charger of at least 2 A with a short cable.',
      'Panel und Koprozessor laufen je an einem eigenen USB-C-Kabel. Das Panel hängt mit kurzem Kabel an einem Ladegerät mit mindestens 2 A.',
    ),
    volts: { min: 5, max: 5 }, amps: null, basis: null,
    pros: [t('Each board has its own supply.', 'Jede Platine hat ihre eigene Versorgung.')],
    cons: [t(
      'A servo on the 5 V pin or on a V pin of the Osoyoo breakout board draws from it.',
      'Ein Servo am 5-V-Pin oder an einem V-Pin der Osoyoo-Breakout-Platine zieht daraus.',
    )],
    unknowns: [t('Current the coprocessor board draws: not stated.', 'Strom, den die Koprozessor-Platine zieht: nicht angegeben.')],
    when: null, used: { has: 'usb' }, src: guide('also-needed'),
  },
  {
    id: 'copro-5v', feeds: 'servo',
    label: t('5 V pin of the coprocessor board (VBUS, pad 40)', '5-V-Pin der Koprozessor-Platine (VBUS, Pad 40)'),
    text: t(
      'For a small servo. The plus of the servo goes to VBUS, the 5 V of the USB cable of the coprocessor. Signal and GND come from the pads of the coprocessor.',
      'Für ein kleines Servo. Das Plus des Servos geht an VBUS, die 5 V des USB-Kabels des Koprozessors. Signal und GND kommen von den Pads des Koprozessors.',
    ),
    volts: { min: 5, max: 5 }, amps: null, basis: null,
    pros: [t('It needs no further part.', 'Sie braucht kein weiteres Teil.')],
    cons: [
      LOGIC_DRAW,
      LOGIC_PEAK,
      t('The voltage is fixed at 5 V.', 'Die Spannung liegt fest bei 5 V.'),
      t('The bench does not limit the current.', 'Der Prüfstand begrenzt den Strom nicht.'),
    ],
    unknowns: [t('Current limit of the 5 V pin: not stated.', 'Stromgrenze des 5-V-Pins: nicht angegeben.')],
    when: { opt: 'connection', is: 'pads' }, used: SERVO_5V_PIN, src: OWNER,
  },
  {
    id: 'strip-v', feeds: 'servo',
    label: t('V pin of the S V G strip of the Osoyoo breakout board', 'V-Pin der S-V-G-Leiste der Osoyoo-Breakout-Platine'),
    text: t(
      'For a small servo. Every IO of the board has a strip with the pins S, V and G. The DC-DC converter of the board feeds the V pins; a jumper sets 3.3 V or 5 V. The servo lead plugs onto the strip of its pin.',
      'Für ein kleines Servo. Jeder IO der Platine hat eine Leiste mit den Pins S, V und G. Der DC-DC-Wandler der Platine speist die V-Pins; ein Jumper stellt 3,3 V oder 5 V. Das Servokabel steckt auf der Leiste seines Pins.',
    ),
    volts: { min: 3.3, max: 5, steps: [3.3, 5] }, amps: null, basis: null,
    pros: [
      t('The servo plugs straight in.', 'Das Servo wird direkt eingesteckt.'),
      t('It needs no soldering and no further part.', 'Sie braucht kein Löten und kein weiteres Teil.'),
    ],
    cons: [
      t('The jumper needs a check before a servo is plugged in.', 'Der Jumper muss vor dem Einstecken eines Servos geprüft werden.'),
      t(
        'The page treats it as drawing from the supply that also powers the logic, because the feed of the converter is not stated.',
        'Die Seite behandelt sie als Last an der Versorgung, die auch die Logik speist, weil die Speisung des Wandlers nicht angegeben ist.',
      ),
      LOGIC_PEAK,
      t('The bench does not limit the current.', 'Der Prüfstand begrenzt den Strom nicht.'),
    ],
    unknowns: [
      t('What feeds the DC-DC converter, and its input range: not stated.', 'Was den DC-DC-Wandler speist, und sein Eingangsbereich: nicht angegeben.'),
      t('Output current of the converter: not stated.', 'Ausgangsstrom des Wandlers: nicht angegeben.'),
      t('Whether the jumper sets the whole board or a group of strips: not stated.', 'Ob der Jumper die ganze Platine oder eine Gruppe von Leisten stellt: nicht angegeben.'),
    ],
    when: { opt: 'connection', is: 'osoyoo' }, used: SERVO_STRIP, src: OWNER,
  },
  {
    id: 'pd-servo', feeds: 'servo',
    label: t('PD mini from a USB-C PD charger', 'PD mini an einem USB-C-PD-Ladegerät'),
    text: t(
      'The bench sets the output of the PD mini over a UART. The output is at most the input voltage less 0.5 V. PD: USB Power Delivery.',
      'Der Prüfstand stellt den Ausgang des PD mini über einen UART. Der Ausgang liegt höchstens bei Eingangsspannung minus 0,5 V. PD: USB Power Delivery.',
    ),
    volts: { min: 1, max: 20 }, amps: { min: 0.05, max: 3 }, basis: VENDOR,
    pros: [
      t('The bench sets the voltage and the current limit.', 'Der Prüfstand stellt Spannung und Stromgrenze.'),
      t('It is separate from the supply of the logic.', 'Sie ist von der Versorgung der Logik getrennt.'),
      t('The automatic servo test reads its current.', 'Der automatische Servotest liest ihren Strom.'),
    ],
    cons: [
      t('The current ends at 3 A.', 'Der Strom endet bei 3 A.'),
      t('A wrong voltage setting can exceed the rating of the servo.', 'Eine falsch gestellte Spannung kann die Nennwerte des Servos überschreiten.'),
      t('It needs a USB-C PD charger, the T-plug and 3 UART wires.', 'Sie braucht ein USB-C-PD-Ladegerät, den T-Stecker und 3 UART-Drähte.'),
    ],
    unknowns: [],
    when: null, used: { all: [{ has: 'servo' }, { has: 'pdmini' }] }, src: guide('wires'),
  },
  {
    id: 'lipo-2s', feeds: 'servo',
    label: t('2S LiPo battery', '2S-LiPo-Akku'),
    text: t(
      `For a power servo that is rated for it. ${OWN_SUPPLY_TEXT.en}`,
      `Für ein starkes Servo, das dafür ausgelegt ist. ${OWN_SUPPLY_TEXT.de}`,
    ),
    volts: { min: 6, max: 8.4 }, amps: null, basis: null,
    pros: [t('It delivers the stall current of a servo.', 'Er liefert den Blockierstrom eines Servos.')],
    cons: [
      t('It has no current limit: a fuse is advised.', 'Er hat keine Strombegrenzung: Eine Sicherung ist ratsam.'),
      t('It fits only a servo rated for 8.4 V.', 'Er passt nur zu einem Servo, das für 8,4 V ausgelegt ist.'),
      t('The voltage changes with the state of charge.', 'Die Spannung ändert sich mit dem Ladezustand.'),
      NOT_SET,
    ],
    unknowns: [],
    when: null, used: SERVO_EXTERNAL, choice: 'servo-supply', src: OWNER,
  },
  {
    id: 'own-supply', feeds: 'servo',
    label: t('External supply that matches the servo', 'Externe Versorgung, die zum Servo passt'),
    text: t(
      `A bench supply or a BEC. BEC: battery eliminator circuit. ${OWN_SUPPLY_TEXT.en}`,
      `Ein Labornetzteil oder ein BEC. BEC: Battery Eliminator Circuit. ${OWN_SUPPLY_TEXT.de}`,
    ),
    volts: null, amps: null, basis: null,
    pros: [t('Voltage and current limit are chosen for the servo.', 'Spannung und Stromgrenze werden für das Servo gewählt.')],
    cons: [t('It is a further device.', 'Sie ist ein weiteres Gerät.'), NOT_SET],
    unknowns: [],
    when: null, used: SERVO_EXTERNAL, choice: 'servo-supply', src: OWNER,
  },
  {
    id: 'esc-battery', feeds: 'esc',
    label: t("A: the ESC's own battery", 'A: eigener Akku des ESC'),
    text: t(
      `${ESC_SIGNAL_ONLY.en} The battery feeds the ESC, through the INA228 when it is fitted.`,
      `${ESC_SIGNAL_ONLY.de} Der Akku speist den ESC, über den INA228, wenn er eingebaut ist.`,
    ),
    volts: null, amps: null, basis: null,
    pros: [t('It delivers the current of a running motor.', 'Er liefert den Strom eines laufenden Motors.')],
    cons: [
      t('The bench sets no voltage and no current limit.', 'Der Prüfstand stellt keine Spannung und keine Stromgrenze.'),
      t('STOP does not switch it off.', 'STOP schaltet ihn nicht ab.'),
    ],
    unknowns: [],
    when: null, used: { all: [{ has: 'esc' }, { opt: 'escPower', is: 'battery' }] }, src: guide('safety'),
  },
  {
    id: 'pd-esc', feeds: 'esc',
    label: t('B: PD mini from a USB-C PD charger', 'B: PD mini an einem USB-C-PD-Ladegerät'),
    text: t(
      `${ESC_SIGNAL_ONLY.en} The PD mini feeds the ESC. Its output is at most the input voltage less 0.5 V.`,
      `${ESC_SIGNAL_ONLY.de} Der PD mini speist den ESC. Sein Ausgang liegt höchstens bei Eingangsspannung minus 0,5 V.`,
    ),
    volts: { min: 1, max: 20 }, amps: { min: 0.05, max: 3 }, basis: VENDOR,
    pros: [
      t('The bench sets the voltage and the current limit.', 'Der Prüfstand stellt Spannung und Stromgrenze.'),
      t('STOP switches its output off.', 'STOP schaltet seinen Ausgang ab.'),
      t('ESC STICK programming counts the beeps from its current.', 'Die ESC-STICK-Programmierung zählt die Töne an seinem Strom.'),
    ],
    cons: [
      t('The current ends at 3 A.', 'Der Strom endet bei 3 A.'),
    ],
    unknowns: [t(
      'Servo and ESC on the one output at the same time: not stated.',
      'Servo und ESC gleichzeitig an dem einen Ausgang: nicht angegeben.',
    )],
    when: null, used: { all: [{ has: 'esc' }, { opt: 'escPower', is: 'pd' }] }, src: guide('safety'),
  },
];

/** The text each repository anchor of this file names, in the order of the anchors. */
export const ANCHOR_TEXTS: ReadonlyArray<AnchorText> = Object.freeze(anchorTexts);

/** Freezes a value and everything it holds. */
function deepFreeze<T>(value: T): T {
  if (typeof value === 'object' && value !== null && !Object.isFrozen(value)) {
    Object.freeze(value);
    for (const item of Object.values(value)) deepFreeze(item);
  }
  return value;
}

// Frozen: a setup holds records of the catalogue by reference, so a write to
// a setup or to the catalogue throws instead of changing later setups.
export const CATALOGUE: Catalogue = deepFreeze({
  release: '0.16.0',
  hashVersion: 1,
  parts,
  pads,
  wires,
  functions,
  rules,
  checks,
  stages,
  safety,
  notInRelease,
  // The heading of the functions that need no part.
  baseFunction: t(
    'No further hardware: panel and coprocessor alone',
    'Ohne weitere Hardware: Panel und Koprozessor allein',
  ),
  pinDefaults,
  powerSources,
});
