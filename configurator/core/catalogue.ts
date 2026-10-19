// The single data file of the bench configurator.
//
// Every part, pin, pad, wire, function, rule, check, stage, safety note and
// data string of the page is defined here and nowhere else. The page holds
// chrome strings only. tools/check.ts validates what the compiler cannot:
// ids, wire ends, text rules, source anchors and the facts that are read
// from the repository at the release tag.
//
// Each record names its source in `src`:
//   repo   a file and line at the release tag
//   guide  a section of the build guide of the release
//   photo  a print read from a photo of the part; such a fact also has a
//          check entry of kind "unknown"
//   page   a proposal or wording of this page; never a hardware fact

import { TAG, VERSION } from '../release.ts';
import type {
  Buy, Catalogue, Check, Cond, Fn, Pad, Part, Pin, PinDefault, PowerSource, Rule, Src, Stage, Text, Wire,
} from '../contract.ts';



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

/** A document of a maker or a part maker: its title, address and the place in it. */
function doc(title: string, url: string, ref: string): Src {
  return { kind: 'doc', title, url, ref };
}

/** Schematic of the Osoyoo breakout board, REV 1.0, file dated 2024-12-03. */
const OSOYOO_SCHEMATIC = doc(
  'Schematic of the OSOYOO IO Breakout Board for Pico-series, REV 1.0, file dated 2024-12-03',
  'https://osoyoo.com/driver/OSOYOO_Breakout_Board_for_Pico/Schematic_OSOYOO-IO-Expansion-Board-for-Pico-series_2024-12-03.pdf',
  'sheet 1: header H3 (pins 1 3.3V, 2 VCC, 3 VOUT_5V), connectors U15 and U17 (VCC), converter U9, regulator U1, MOSFETs Q1 and Q3',
);
/** Product page of the Osoyoo breakout board. */
const OSOYOO_PAGE = doc(
  'OSOYOO Breakout Board for Pico series, product page, published 2024-12-02',
  'https://osoyoo.com/2024/12/02/osoyoo-breakout-board-for-pico-series/',
  'sections TECH SPECS, DIAGRAM (V selector) and Note',
);
/** Product photos of the board on the product page: the prints on the board. */
const OSOYOO_PHOTOS = doc(
  'OSOYOO Breakout Board for Pico series, product photos',
  'https://osoyoo.com/2024/12/02/osoyoo-breakout-board-for-pico-series/',
  'photos of the board: print DC:7~16V at the DC jack, print 100 uF 16 V on the capacitors C1 and C2',
);
/** Datasheet of the hex inverter, as the product page links it. */
const SN74HC04_DATASHEET = doc(
  'Texas Instruments SN74HC04, SN54HC04, hex inverters',
  'https://osoyoo.com/driver/OSOYOO_Breakout_Board_for_Nano/sn74hc04.pdf',
  'electrical characteristics: input leakage up to 1 uA, input capacitance 3 to 10 pF',
);
/** Datasheet of the converter, as the product page links it. */
const XL1509_DATASHEET = doc(
  'XLSEMI XL1509, 2A 150KHz 40V Buck DC to DC Converter, Rev 2.0',
  'https://osoyoo.com/driver/OSOYOO_Breakout_Board_for_Nano/XL1509.pdf',
  'page 6, table XL1509-5.0 (4.8 to 5.2 V from 7 to 40 V in, 0.2 to 2 A); page 7, DC parameters (150 kHz, current limit 4 A)',
);

/** A statement of the project owner, by its date. */
function owner(date: string): Src {
  return { kind: 'owner', date };
}

const OWNER = owner('2026-10-11');

/**
 * A row of the parts list. `maker` null: a generic or modelled part, and
 * `order` is its spec. `page` is the product page of the maker.
 */
function buy(maker: string | null, order: Text, check: Text, more: Pick<Buy, 'id' | 'page' | 'qty' | 'only'> = {}): Buy {
  return { maker, order, check, ...more };
}

function pin(id: string, label: Text, src: Src, confirmed = true): Pin {
  return { id, label, confirmed, src };
}

const OUT_BIND = 'shared/outputs/out_bind.c';
const SETTINGS = 'shared/settings/settings.c';
const BOARD_PINS = 'firmware/panel/components/board/include/board_pins.h';
const IOMCU_PINS = 'firmware/iomcu/include/iomcu_pins.h';
const PROG_PORT = 'shared/outputs/include/prog_port.h';
const PROGRAMMER = 'docs/ServoProgrammer.md';

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
    buy: [
      buy('Waveshare', same('ESP32-S3-Touch-LCD-7'), t(
        'The touch version with 800 × 480 pixels. Not the ESP32-S3-LCD-7 without touch, not the ESP32-S3-Touch-LCD-7B with 1024 × 600.',
        'Die Touch-Version mit 800 × 480 Pixeln. Nicht das ESP32-S3-LCD-7 ohne Touch, nicht das ESP32-S3-Touch-LCD-7B mit 1024 × 600.',
      ), { page: 'https://www.waveshare.com/esp32-s3-touch-lcd-7.htm' }),
    ],
    src: guide('parts'),
  },
  {
    id: 'copro',
    name: t('Waveshare RP2350-CAN board', 'Waveshare-RP2350-CAN-Platine'),
    short: t('IOmcu', 'IOmcu'),
    role: t(
      'IOmcu: drives the outputs, reads the sensors, sets the supply.',
      'IOmcu: treibt die Ausgänge, liest die Sensoren, stellt die Versorgung.',
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
    buy: [
      buy('Waveshare', same('RP2350-CAN'), t(
        'The RP2350 board with the CAN controller XL2515 and a 3-pole CAN screw terminal. Not a Pico with a CAN add-on board.',
        'Die RP2350-Platine mit dem CAN-Controller XL2515 und einer 3-poligen CAN-Schraubklemme. Kein Pico mit CAN-Zusatzplatine.',
      ), { page: 'https://www.waveshare.com/rp2350-can.htm' }),
      buy('Osoyoo', same('Breakout Board for Pico series'), t(
        'The board with the jumper V Selector and a DC jack. It holds no microcontroller: the RP2350-CAN plugs in where a Pico goes. Not the boards for Nano or UNO.',
        'Die Platine mit dem Jumper V Selector und einer DC-Buchse. Sie trägt keinen Mikrocontroller: Die RP2350-CAN steckt dort, wo ein Pico sitzt. Nicht die Platinen für Nano oder UNO.',
      ), { id: 'osoyoo', page: 'https://osoyoo.com/2024/12/02/osoyoo-breakout-board-for-pico-series/', only: { connection: 'osoyoo' } }),
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
    buy: [
      buy(null, t('USB-C data cable', 'USB-C-Datenkabel'), t(
        'Not a charge-only cable: the firmware of each board is flashed over USB.',
        'Kein reines Ladekabel: Die Firmware jeder Platine wird über USB geflasht.',
      ), { id: 'cable', qty: 2 }),
      buy(null, t('USB-C charger, 5 V, 2 A', 'USB-C-Ladegerät, 5 V, 2 A'), t(
        'At least 2 A at 5 V. It feeds the panel through a short cable.',
        'Mindestens 2 A bei 5 V. Es speist das Panel über ein kurzes Kabel.',
      ), { id: 'charger' }),
    ],
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
    buy: [
      buy(null, t('Hook-up wire', 'Schaltdraht'), t(
        'Twisted as a pair for CAN H and CAN L.',
        'Als verdrilltes Paar für CAN H und CAN L.',
      ), { id: 'pair' }),
      buy(null, t('Jumper wires', 'Jumperkabel'), t(
        'For the heartbeat and the UART.',
        'Für Heartbeat und UART.',
      ), { id: 'jumper' }),
      buy(null, t('Digital multimeter', 'Digitalmultimeter'), t(
        'With a resistance range: the checks read about 60 Ω from CAN H to CAN L and near 0 Ω on ground paths.',
        'Mit Widerstandsbereich: Die Prüfungen messen etwa 60 Ω von CAN H nach CAN L und nahe 0 Ω auf Massepfaden.',
      ), { id: 'dmm' }),
    ],
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
    yours: true,
    buy: [
      buy(null, t('Servo under test', 'Servo als Prüfling'), t(
        'Its voltage rating matches the servo power of the setup, see Power sources.',
        'Seine Nennspannung passt zur Servoversorgung des Aufbaus, siehe Stromquellen.',
      )),
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
    yours: true,
    buy: [
      buy(null, t('2S LiPo battery or another supply that matches the servo', '2S-LiPo-Akku oder eine andere Versorgung, die zum Servo passt'), t(
        'Voltage within the rating of the servo. A 2S LiPo battery gives up to 8.4 V.',
        'Spannung innerhalb der Nennwerte des Servos. Ein 2S-LiPo-Akku liefert bis 8,4 V.',
      )),
    ],
    src: guide('stage-1'),
  },
  // The supply that position 5V of the jumper V Selector needs. No product is
  // chosen: the part states the range the board takes, so it is modelled.
  {
    id: 'dcsup',
    name: t(
      'DC supply for the Osoyoo breakout board, 7 to 12 V DC (yours)',
      'DC-Versorgung für die Osoyoo-Breakout-Platine, 7 bis 12 V DC (eigene)',
    ),
    short: t('Osoyoo DC supply', 'Osoyoo-DC-Versorgung'),
    role: t(
      'Feeds the XL1509 converter of the Osoyoo breakout board, which feeds the V pins while the jumper V Selector sits on 5V. 7 to 12 V DC (vendor recommendation; print 7 to 16 V, vendor limit 18 V, capacitors rated 16 V). Above 12 V the gate-source voltage of the SI2302 (Q3) exceeds its rating of ±12 V (datasheet linked on the product page) at every input: the gate sits at PWRIN through R7, with no clamp. The print of 16 V and the vendor limit of 18 V are above that. The supply connects at exactly 1 of these: the DC jack, the VIN terminal or the header H4 (pin 2 PWRIN, pin 1 VIN-). At the DC jack: plug 5.5 x 2.1 mm, centre positive. The product page names the socket DC005-2.1: the 2.1 mm pin follows from that name; the 5.5 mm outer diameter is the usual size of that socket family and is not stated by the vendor. The minus of the jack is GND of the board, with no reverse-polarity protection: the SI2302 (Q3) in the lead VIN- protects the VIN terminal and H4 only. The switch OFF/ON is on ON. Current it must deliver: not stated.',
      'Speist den XL1509-Wandler der Osoyoo-Breakout-Platine, der die V-Pins speist, solange der Jumper V Selector auf 5V sitzt. 7 bis 12 V DC (Herstellerempfehlung; Aufdruck 7 bis 16 V, Herstellergrenze 18 V, Kondensatoren mit 16 V). Über 12 V überschreitet die Gate-Source-Spannung des SI2302 (Q3) an jedem Eingang ihren Grenzwert von ±12 V (Datenblatt, verlinkt auf der Produktseite): Das Gate liegt über R7 ohne Begrenzung an PWRIN. Der Aufdruck von 16 V und die Herstellergrenze von 18 V liegen darüber. Die Versorgung wird an genau einem dieser Anschlüsse angeschlossen: an der DC-Buchse, an der VIN-Klemme oder am Pfostenverbinder H4 (Pin 2 PWRIN, Pin 1 VIN-). An der DC-Buchse: Stecker 5,5 x 2,1 mm, Mittelstift Plus. Die Produktseite nennt die Buchse DC005-2.1: Der Stift von 2,1 mm folgt aus diesem Namen; der Außendurchmesser von 5,5 mm ist das übliche Maß dieser Buchsenfamilie und vom Hersteller nicht angegeben. Das Minus der Buchse ist GND der Platine, ohne Verpolschutz: Der SI2302 (Q3) in der Leitung VIN- schützt nur die VIN-Klemme und H4. Der Schalter OFF/ON steht auf ON. Strom, den sie liefern muss: nicht angegeben.',
    ),
    locked: false,
    selectable: false,
    modelled: true,
    release: 'released',
    slot: { column: 'L', order: 12, sub: t('7 to 12 V DC', '7 bis 12 V DC') },
    pins: [
      pin('POS', same('+'), OSOYOO_SCHEMATIC),
      pin('NEG', same('−'), OSOYOO_SCHEMATIC),
    ],
    buy: [
      buy(null, t('DC power supply 7 to 12 V, plug 5.5 × 2.1 mm, centre positive', 'DC-Netzteil 7 bis 12 V, Stecker 5,5 × 2,1 mm, Mittelstift Plus'), t(
        'Not above 12 V. Centre positive: the DC jack has no reverse-polarity protection. Current it must deliver: not stated.',
        'Nicht über 12 V. Mittelstift Plus: Die DC-Buchse hat keinen Verpolschutz. Strom, den es liefern muss: nicht angegeben.',
      )),
    ],
    src: OSOYOO_PAGE,
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
    yours: true,
    buy: [
      buy(null, t('ESC and motor', 'ESC und Motor'), t(
        'Signal and ground go to the bench; the centre wire (BEC +) is connected nowhere.',
        'Signal und Masse gehen an den Prüfstand; die Mittelader (BEC +) wird nirgends angeschlossen.',
      )),
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
    yours: true,
    buy: [
      buy(null, t('Battery for the ESC', 'Akku für den ESC'), t(
        'Within the rating of the ESC. STOP does not switch it off.',
        'Innerhalb der Nennwerte des ESC. STOP schaltet ihn nicht ab.',
      )),
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
    buy: [
      buy('WeAct Studio', same('PD Power Mini V1 Buck'), t(
        'The Buck version: output 1 to 20 V, 0.05 to 3 A. The output stays at least 0.5 V below the PD voltage the module requests (PD Vset, 8 to 24 V, firmware V1.0.1.0 or later).',
        'Die Buck-Version: Ausgang 1 bis 20 V, 0,05 bis 3 A. Der Ausgang bleibt mindestens 0,5 V unter der PD-Spannung, die das Modul anfordert (PD Vset, 8 bis 24 V, ab Firmware V1.0.1.0).',
      ), { page: 'https://github.com/WeActStudio/WeActStudio.PDPowerMiniV1-Buck' }),
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
    buy: [
      buy(null, t('USB-C PD charger, 20 V', 'USB-C-PD-Ladegerät, 20 V'), t(
        'With a 20 V profile. The PD mini output stays at least 0.7 V below the charger voltage: a charger without a 20 V profile ends the range lower.',
        'Mit 20-V-Profil. Der Ausgang des PD mini bleibt mindestens 0,7 V unter der Spannung des Ladegeräts: Ohne 20-V-Profil endet der Bereich tiefer.',
      )),
    ],
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
    buy: [
      buy('DAOKAI', same('INA3221'), t(
        'The board with 3 channels, shunts R100 (0.1 Ω) and a header of 8 holes from VS to TC. A unit whose die ID reads 0x1408 instead of 0x3220 is refused.',
        'Die Platine mit 3 Kanälen, Shunts R100 (0,1 Ω) und einer Leiste mit 8 Löchern von VS bis TC. Ein Exemplar, dessen Die-ID 0x1408 statt 0x3220 liest, wird abgewiesen.',
      )),
    ],
    src: guide('parts'),
  },
  {
    id: 'as5600',
    name: t('AS5600 board with diametric magnet', 'AS5600-Platine mit diametralem Magnet'),
    short: same('AS5600'),
    role: t(
      'Angle of the servo horn, 12 bit. I2C 0x36, fixed.',
      'Winkel des Ruderhorns, 12 Bit. I2C 0x36, fest.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'R', order: 15 },
    pins: [
      pin('VCC', same('VCC'), guide('stage-4')),
      pin('OUT', same('OUT'), guide('stage-4')),
      pin('GND', same('GND'), guide('stage-4')),
      // Tied to GND (wire 54). The print on the board is not confirmed.
      pin('GPO', same('GPO'), guide('stage-4'), false),
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
    buy: [
      buy(null, t('AS5600 magnetic encoder', 'AS5600-Magnetencoder'), t(
        'A board with a diametrically magnetised magnet; an axial magnet gives no angle. The board has the pads VCC, OUT, GND, GPO, SDA, SCL, DIR.',
        'Eine Platine mit diametral magnetisiertem Magnet; ein axialer Magnet gibt keinen Winkel. Die Platine hat die Pads VCC, OUT, GND, GPO, SDA, SCL, DIR.',
      )),
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
    buy: [
      buy(null, t('Resistor 2.2 kΩ, 0.25 W', 'Widerstand 2,2 kΩ, 0,25 W'), t(
        'One from SDA and one from SCL to 3V3. 2.2 kΩ, not 22 kΩ.',
        'Einer von SDA und einer von SCL nach 3V3. 2,2 kΩ, nicht 22 kΩ.',
      ), { qty: 2 }),
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
    buy: [
      buy('Matek', same('I2C-INA-BM'), t(
        'The board carries an INA228 or an INA238. The bench reads the INA228 only and refuses an INA238 (ID 0x238x).',
        'Die Platine trägt einen INA228 oder einen INA238. Der Prüfstand liest nur den INA228 und weist einen INA238 ab (ID 0x238x).',
      ), { id: 'matek', page: 'https://www.mateksys.com/?portfolio=i2c-ina-bm', only: { ina228Board: 'matek' } }),
      buy(null, t('INA228 module, XT90', 'INA228-Modul, XT90'), t(
        'With 2 XT90 plugs, board print BIFRC V1.1, header print L A V G 5. Its supply is 3.3 V on V, not 5 V. The maker is not known.',
        'Mit 2 XT90-Steckern, Aufdruck der Platine BIFRC V1.1, Aufdruck der Leiste L A V G 5. Die Versorgung ist 3,3 V an V, nicht 5 V. Der Hersteller ist nicht bekannt.',
      ), { id: 'xt90', only: { ina228Board: 'xt90' } }),
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
    buy: [
      buy(null, t('microSD card', 'microSD-Karte'), t(
        'Formatted FAT. Tested card sizes: not stated.',
        'Formatiert mit FAT. Getestete Kartengrößen: nicht angegeben.',
      )),
    ],
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
      'Its NC (normally closed) contact opens the heartbeat wire. It breaks no power line; the IOmcu switches the bench to SAFE and the output of the PD mini off.',
      'Sein Öffner (NC) unterbricht den Heartbeat-Draht. Er trennt keine Versorgungsleitung; der IOmcu schaltet den Prüfstand auf SAFE und den Ausgang des PD mini aus.',
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
    buy: [
      buy(null, t('Emergency stop button, latching, 1NO 1NC', 'Not-Aus-Taster, rastend, 1NO 1NC'), t(
        '1 normally open (NO) and 1 normally closed (NC) contact. The NC contact opens the heartbeat wire. A button with an NO contact alone does not fit.',
        '1 Schließer (NO) und 1 Öffner (NC). Der Öffner unterbricht den Heartbeat-Draht. Ein Taster nur mit Schließer (NO) passt nicht.',
      )),
    ],
    src: guide('parts'),
  },
  {
    id: 'sim',
    name: t(
      'Motor simulator, self-built: 3 resistors in delta, 470 Ω, 2 W',
      'Motorsimulator, selbst gebaut: 3 Widerstände im Dreieck, 470 Ω, 2 W',
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
    buy: [
      buy(null, t('Resistor 470 Ω, 2 W', 'Widerstand 470 Ω, 2 W'), t(
        'Rated 2 W each, not less.',
        'Je 2 W belastbar, nicht weniger.',
      ), { qty: 3 }),
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
      'Liest Tonhöhe und Länge der ESC-Pieptöne an Phase A.',
    ),
    locked: false,
    selectable: true,
    release: 'released',
    slot: { column: 'R', order: 13, sub: t('4.7 kΩ, 0.25 W', '4,7 kΩ, 0,25 W') },
    pins: [
      pin('IN', t('resistor, end 1', 'Widerstand, Ende 1'), guide('tap')),
      pin('OUT', t('resistor, end 2', 'Widerstand, Ende 2'), guide('tap')),
    ],
    buy: [
      buy(null, t('Resistor 4.7 kΩ, 0.25 W', 'Widerstand 4,7 kΩ, 0,25 W'), t(
        'In series from phase A to the tap pin. 4.7 kΩ, not 47 kΩ.',
        'In Reihe von Phase A zum Abgriff-Pin. 4,7 kΩ, nicht 47 kΩ.',
      ), { id: 'resistor' }),
      buy(null, t('Zener diode 3.0 V', 'Z-Diode 3,0 V'), t(
        'Zener voltage 3.0 V; the type code on the body often reads 3V0.',
        'Z-Spannung 3,0 V; der Typcode auf dem Gehäuse lautet oft 3V0.',
      ), { id: 'zener' }),
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
      'Sits between the USB-C PD charger and the PD mini: the charger on the socket IN, a USB-C cable from the socket OUT to the PD mini. Brings GND, D+ and D− to the IOmcu.',
      'Sitzt zwischen USB-C-PD-Ladegerät und PD mini: das Ladegerät an der Buchse IN, ein USB-C-Kabel von der Buchse OUT zum PD mini. Führt GND, D+ und D− zum IOmcu.',
    ),
    between: ['charger', 'pdmini'],
    locked: false,
    selectable: false,
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
    buy: [
      buy(null, t('USB-C breakout board, female to female', 'USB-C-Breakout-Platine, Buchse auf Buchse'), t(
        '2 USB-C sockets, IN and OUT, and the pads G, D+, D−, V. A board without D+ and D− pads does not fit.',
        '2 USB-C-Buchsen, IN und OUT, und die Pads G, D+, D−, V. Eine Platine ohne Pads für D+ und D− passt nicht.',
      ), { id: 'board' }),
      buy(null, t('USB-C cable, USB-C at both ends', 'USB-C-Kabel, USB-C an beiden Enden'), t(
        'From the socket OUT to the PD mini. Rated 3 A or more.',
        'Von der Buchse OUT zum PD mini. Für 3 A oder mehr ausgelegt.',
      ), { id: 'cable' }),
    ],
    src: OWNER,
  },
];

// ---------------------------------------------------------------------------
// Pads of the IOmcu. [pad, name, gpio, line in out_bind.c at the release tag]
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
  [1, 'GP0', 0, 34], [2, 'GP1', 1, 35], [3, 'GND', null, 87], [4, 'GP2', 2, 36],
  [5, 'GP3', 3, 37], [6, 'GP4', 4, 38], [7, 'GP5', 5, 39], [8, 'GND', null, 88],
  [9, 'GP6', 6, 40], [10, 'GP7', 7, 41], [11, 'GP8', 8, 42], [12, 'GP9', 9, 43],
  [13, 'GND', null, 89], [14, 'GP10', 10, 44], [15, 'GP11', 11, 45], [16, 'GP12', 12, 46],
  [17, 'GP13', 13, 47], [18, 'GND', null, 90], [19, 'GP14', 14, 48], [20, 'GP15', 15, 49],
  [21, 'GP16', 16, 50], [22, 'GP17', 17, 51], [23, 'GND', null, 91], [24, 'GP18', 18, 52],
  [25, 'GP19', 19, 53], [26, 'GP20', 20, 54], [27, 'GP21', 21, 55], [28, 'GND', null, 92],
  [29, 'GP22', 22, 56], [30, 'RUN', null, 93], [31, 'GP26', 26, 57], [32, 'GP27', 27, 58],
  [33, 'GND', null, 94], [34, 'GP28', 28, 59], [35, 'VREF', null, 95], [36, '3V3', null, 96],
  [37, '3V3_EN', null, 97], [38, 'GND', null, 98], [39, 'VSYS', null, 99], [40, 'VBUS', null, 100],
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
/** The servo signal on a pad bound under OUTPUTS. */
const SERVO_OUTPUTS: Cond = { all: [{ has: 'servo' }, { opt: 'servoPort', is: 'outputs' }] };
/** The servo signal on the programming port: the function that programs a servo. */
const SERVO_PROG: Cond = { all: [{ has: 'servo' }, { opt: 'servoPort', is: 'prog' }] };
const SERVO_EXTERNAL: Cond = { all: [...SERVO_NO_PD, { opt: 'servoPower', is: 'external' }] };
/** The bench itself feeds the servo: the 5 V pin, or the V pin of a strip of the Osoyoo breakout board. */
const SERVO_BENCH: Cond = { all: [...SERVO_NO_PD, { opt: 'servoPower', is: 'bench' }] };
const SERVO_5V_PIN: Cond = { all: [SERVO_BENCH, { opt: 'connection', is: 'pads' }] };
const SERVO_STRIP: Cond = { all: [SERVO_BENCH, { opt: 'connection', is: 'osoyoo' }] };
const SERVO_BENCH_NOTE = t(
  'For a small servo. The servo draws its current from the supply that also powers the logic.',
  'Für ein kleines Servo. Das Servo zieht seinen Strom aus der Versorgung, die auch die Logik speist.',
);
const SERVO_STRIP_NOTE = t(
  'For a small servo. With the jumper V Selector on 5V the servo draws from the DC input of the board through the XL1509 converter, with the jumper on 3V3 from VSYS of the IOmcu board, the supply that also powers the logic.',
  'Für ein kleines Servo. Mit dem Jumper V Selector auf 5V zieht das Servo aus dem DC-Eingang der Platine über den XL1509-Wandler, mit dem Jumper auf 3V3 aus VSYS der IOmcu-Platine, der Versorgung, die auch die Logik speist.',
);
const STRIP_V = t(
  'V pin of the S V G strip at GP2 of the Osoyoo breakout board',
  'V-Pin der S-V-G-Leiste an GP2 der Osoyoo-Breakout-Platine',
);
const DC_IN_POS = t(
  'plus of the DC input of the Osoyoo breakout board: the centre pin of the DC jack, the VIN terminal or H4 pin 2 PWRIN, exactly 1 of them',
  'Plus des DC-Eingangs der Osoyoo-Breakout-Platine: Mittelstift der DC-Buchse, VIN-Klemme oder H4 Pin 2 PWRIN, genau einer davon',
);
const DC_IN_NEG = t(
  'minus of the same DC input of the Osoyoo breakout board: VIN- at the minus pole of the 2-pole VIN terminal or at H4 pin 1, GND at the outer contact of the DC jack',
  'Minus desselben DC-Eingangs der Osoyoo-Breakout-Platine: VIN- am Minuspol der 2-poligen VIN-Klemme oder an H4 Pin 1, GND am Außenkontakt der DC-Buchse',
);
const DC_IN_SIGNAL = t('DC input of the Osoyoo breakout board, 7 to 12 V', 'DC-Eingang der Osoyoo-Breakout-Platine, 7 bis 12 V');
const DC_IN_NOTE = t(
  'Exactly 1 input: the DC jack, the VIN terminal or the header H4, never 2 of them. DC jack: type DC005-2.1 (product page), plug 5.5 x 2.1 mm (2.1 mm from the socket name, 5.5 mm the usual size of that socket family, not stated by the vendor), centre positive, no reverse-polarity protection; the SI2302 protects the VIN terminal and H4 only. The switch OFF/ON on ON. USB does not feed the V pins.',
  'Genau 1 Eingang: DC-Buchse, VIN-Klemme oder Pfostenverbinder H4, nie 2 davon. DC-Buchse: Typ DC005-2.1 (Produktseite), Stecker 5,5 x 2,1 mm (2,1 mm aus dem Namen der Buchse, 5,5 mm das übliche Maß dieser Buchsenfamilie, vom Hersteller nicht angegeben), Mittelstift Plus, ohne Verpolschutz; der SI2302 schützt nur die VIN-Klemme und H4. Der Schalter OFF/ON auf ON. USB speist die V-Pins nicht.',
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
      'The interlock. The IOmcu refuses to arm without it.',
      'Die Verriegelung. Ohne ihn schaltet der IOmcu nicht auf ARMED.',
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
    when: SERVO_OUTPUTS, stage: 1, route: { kind: 'L', lane: 5 }, pin: 'proposal',
    src: PAGE,
  },
  {
    n: 8, kind: 'wire', a: 'pad.8', b: 'servo.NEG', net: 'gnd', cls: 'gnd',
    signal: GND,
    note: t(
      'Servo − also goes to IOmcu GND.',
      'Servo − geht auch an GND des IOmcu.',
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
      `Named from the IOmcu: its TX goes to D− of the T-plug. ${STATED.en} Optional 1 kΩ in series.`,
      `Vom IOmcu aus benannt: sein TX geht an D− des T-Steckers. ${STATED.de} Optional 1 kΩ in Reihe.`,
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
    src: repo('docs/FirstRun.md', 564, '| SDA | GP16 (pad 21) |'),
  },
  {
    n: 27, kind: 'wire', a: 'pad.22', b: 'ina3221.SCL', net: 'scl', cls: 'scl',
    signal: SCL,
    note: null,
    when: { has: 'ina3221' }, stage: 3, route: { kind: 'R', lane: 8 }, pin: 'default',
    src: repo('docs/FirstRun.md', 565, '| SCL | GP17 (pad 22) |'),
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
      'Never leave DIR open. No wire to the IOmcu.',
      'DIR nie offen lassen. Kein Draht zum IOmcu.',
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
    src: repo(SETTINGS, 192, 'GP22 is pad 29'),
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
      'No wire to the IOmcu.',
      'Kein Draht zum IOmcu.',
    ),
    when: { has: 'as5600' }, stage: 4, route: { kind: 'S' }, src: guide('stage-4'),
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
  // IOmcu board. Wires 61 and 62: the V pin of the strip at the servo
  // pin of the Osoyoo breakout board; the strip is no pad of the IOmcu,
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
    signal: t('servo power, 3.3 V or 5 V by the jumper', 'Servoversorgung, 3,3 V oder 5 V je nach Jumper'),
    note: SERVO_STRIP_NOTE,
    when: { all: [SERVO_STRIP, { not: { has: 'ina3221' } }] },
    stage: 1, route: { kind: 'S' }, src: OWNER,
  },
  {
    n: 62, kind: 'stub', a: 'ina3221.C1A', b: null, to: STRIP_V, net: 'strip_v', cls: 'power',
    signal: t('servo power, 3.3 V or 5 V by the jumper', 'Servoversorgung, 3,3 V oder 5 V je nach Jumper'),
    note: SERVO_STRIP_NOTE,
    when: { all: [SERVO_STRIP, { has: 'ina3221' }] },
    stage: 3, route: { kind: 'S' }, src: OWNER,
  },
  // The second half of the power path of the PD mini. Wire 19 is the first:
  // charger to the socket IN of the T-plug.
  {
    n: 63, kind: 'wire', a: 'usbtap.OUT', b: 'pdmini.USB', net: 'pd_in', cls: 'power',
    signal: t('PD in', 'PD-Eingang'),
    note: t(
      'USB-C cable, plugged into the socket OUT and into the USB-C socket of the PD mini. The current rating the cable needs is not stated. Output of the PD mini is at least 0.7 V below the input voltage.',
      'USB-C-Kabel, in die Buchse OUT und in die USB-C-Buchse des PD mini gesteckt. Die nötige Strombelastbarkeit des Kabels ist nicht angegeben. Ausgang des PD mini mindestens 0,7 V unter der Eingangsspannung.',
    ),
    when: { has: 'pdmini' }, stage: 2, route: { kind: 'O', lane: 4 }, src: OWNER,
  },
  // The servo on the programming port of the IOmcu, in place of wire 7.
  {
    n: 64, kind: 'wire', a: 'pad.17', b: 'servo.SIG', net: 'servo_sig', cls: 'out',
    signal: t(`servo signal: PWM and programming, ${V33_EN}`, `Servosignal: PWM und Programmierung, ${V33_DE}`),
    note: t(
      'In place of wire 7. The programming port, port 1: the pin of an IOmcu that has stored none. Set SERVO TESTER to PROG PORT on the PROG PORT entry of SETUP → OUTPUTS.',
      'Anstelle von Draht 7. Der Programmierport, Port 1: der Pin eines IOmcu, der keinen gespeichert hat. SERVO-TESTER auf dem Eintrag PROG-PORT von SETUP → OUTPUTS auf PROG-PORT stellen.',
    ),
    when: SERVO_PROG, stage: 1, route: { kind: 'L', lane: 5 }, pin: 'default',
    src: repo(PROG_PORT, 61, '#define PROG_PORT_DEFAULT_PIN 13u'),
  },
  // The DC supply of the Osoyoo breakout board, for the jumper on 5V. The DC
  // input is no pad of the IOmcu, so both leads end as stubs with their
  // target in words. The converter U9 joins this input to the V pins.
  {
    n: 65, kind: 'stub', a: 'dcsup.POS', b: null, to: DC_IN_POS, net: 'osoyoo_vin', cls: 'power',
    signal: DC_IN_SIGNAL,
    note: DC_IN_NOTE,
    when: SERVO_STRIP, stage: 1, route: { kind: 'S' }, src: OSOYOO_SCHEMATIC,
  },
  {
    n: 66, kind: 'stub', a: 'dcsup.NEG', b: null, to: DC_IN_NEG, net: 'osoyoo_vin_neg', cls: 'power',
    signal: DC_IN_SIGNAL,
    note: DC_IN_NOTE,
    when: SERVO_STRIP, stage: 1, route: { kind: 'S' }, src: OSOYOO_SCHEMATIC,
  },
];

// ---------------------------------------------------------------------------
// Functions. `src` is the statement of the function in the docs of the release.
//
// A function with `needs: []` runs on the panel and the IOmcu alone. It
// is enabled in every setup and the page offers no tick for it.
// ---------------------------------------------------------------------------

const functions: Fn[] = [
  {
    id: 'link', group: 'bench',
    name: t('CAN link at 1 Mbit/s with an echo self-test at every start', 'CAN-Verbindung mit 1 Mbit/s und Echo-Selbsttest bei jedem Start'),
    needs: [], src: repo('STATUS.md', 114, 'CAN drivers (TWAI on the panel, XL2515 on the IOmcu) and the echo self-test'),
  },
  {
    id: 'interlock', group: 'bench',
    name: t('ARM by a 2 s hold, DISARM and the heartbeat interlock', 'ARM durch 2 s Halten, DISARM und Heartbeat-Verriegelung'),
    // Arming works on the bring-up bench. The decision to arm is tested on
    // the host only.
    needs: [], src: repo('STATUS.md', 117, 'The decision to switch to ARMED is `shared/safety/safety_gate.c`'),
  },
  {
    id: 'stop', group: 'bench',
    name: t('STOP command over the link', 'STOP-Befehl über die Verbindung'),
    needs: [], src: repo('README.md', 37, 'and a STOP command over the link'),
  },
  {
    id: 'bind', group: 'bench',
    name: t('Bind protocols to pads on the OUTPUTS screen', 'Protokolle auf dem OUTPUTS-Bildschirm an Pads binden'),
    needs: [], src: repo('STATUS.md', 122, 'Ticking or unticking a pin writes the binding and reads it back'),
  },
  {
    id: 'signals', group: 'bench',
    name: t('Send PWM and DShot signals on bound pads', 'PWM- und DShot-Signale auf gebundenen Pads ausgeben'),
    needs: [], src: repo('STATUS.md', 119, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
  },
  {
    id: 'sweep', group: 'bench',
    name: t(
      'Servo signal: sweep (square, sine, triangle) and frame rates other than 50 Hz',
      'Servosignal: Sweep (Rechteck, Sinus, Dreieck) und eine andere Pulsrate als 50 Hz',
    ),
    needs: [],
    src: repo('STATUS.md', 107, 'a sweep (square, sine or triangle, run by the IOmcu)'),
  },
  {
    id: 'ppm', group: 'bench',
    name: t('Send a PPM signal with 8 channels on 1 pad', 'PPM-Signal mit 8 Kanälen auf 1 Pad ausgeben'),
    needs: [], src: repo('STATUS.md', 119, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
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
    needs: [], src: repo('STATUS.md', 98, 'English and German, chosen by SETUP'),
  },
  {
    id: 'profiles', group: 'bench',
    name: t('Browse and search the ESC profiles: 72 families, 451 models', 'ESC-Profile durchsuchen: 72 Familien, 451 Modelle'),
    // No statement of a run of the list on a panel.
    needs: [], src: repo('STATUS.md', 132, '72 families, 451 models, from 153 manuals'),
  },
  {
    id: 'wifi', group: 'bench',
    name: t('Panel update and card files in a browser over Wi-Fi', 'Panel-Update und Kartendateien im Browser über WLAN'),
    // The WI-FI screen. The upload works with no card; the file
    // list then says the card is missing (docs/Screens.md, "Wi-Fi").
    needs: [], src: repo('STATUS.md', 103, 'Panel firmware update and card files over Wi-Fi'),
  },
  {
    id: 'canupdate', group: 'bench',
    name: t('IOmcu firmware update over CAN from the panel', 'Firmware-Update des IOmcu über CAN vom Panel'),
    needs: [], src: repo('STATUS.md', 118, 'IOmcu firmware update over CAN'),
  },
  {
    id: 'servo', group: 'servo',
    name: t('Drive a servo', 'Servo ansteuern'),
    needs: [], drives: 'servo', src: repo('STATUS.md', 119, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
  },
  {
    id: 'servotest', group: 'servo',
    name: t('Automatic servo test', 'Automatischer Servotest'),
    // The test reads the current of the PD mini (docs/Servo.md, line 437).
    needs: ['pdmini'], drives: 'servo', src: repo('docs/Servo.md', 437, 'the current of the supply that feeds the servo: the PD mini'),
  },
  {
    id: 'servocur', group: 'servo',
    name: t('Servo current from 1 ms samples', 'Servostrom aus 1-ms-Abtastwerten'),
    needs: ['ina3221'], drives: 'servo', src: repo('STATUS.md', 124, 'Current monitors (INA228, INA3221)'),
  },
  {
    id: 'angle', group: 'servo',
    name: t('Angle of the servo horn', 'Winkel des Ruderhorns'),
    needs: ['as5600'], drives: 'servo', src: repo('STATUS.md', 126, 'Output encoder (AS5600)'),
  },
  {
    id: 'servoprog', group: 'servo',
    name: t('Program a KST or Chaservo servo', 'KST- oder Chaservo-Servo programmieren'),
    // The panel and the IOmcu suffice. The servo is on the programming
    // port. Its supply is the PD mini, where one is there, and CONNECT sets
    // it; else the supply the variant "Servo power" names. SERVO TESTER on
    // SETUP > OUTPUTS is PROG PORT, so the other servo functions drive the same pin.
    needs: [], drives: 'servo', forces: { servoPort: 'prog' },
    src: repo(PROGRAMMER, 42, "The servo is on the IOmcu's programming port, port 1: GP13, pad 17"),
  },
  {
    id: 'esc', group: 'esc',
    name: t('Drive an ESC and motor', 'ESC und Motor ansteuern'),
    needs: [], drives: 'esc', src: repo('STATUS.md', 119, 'Output drivers (PWM, PPM, DShot, bidirectional DShot)'),
  },
  {
    id: 'escpow', group: 'esc',
    name: t('ESC voltage, current, energy', 'Spannung, Strom, Energie des ESC'),
    needs: ['ina228'], drives: 'esc', src: repo('STATUS.md', 124, 'Current monitors (INA228, INA3221)'),
  },
  {
    id: 'stick', group: 'esc',
    name: t('ESC STICK programming', 'ESC-STICK-Programmierung'),
    // The bench powers the ESC from the PD mini and counts the beeps from the
    // supply current (docs/StickProgramming.md, line 6).
    needs: ['pdmini'], drives: 'esc', forces: { escPower: 'pd' },
    src: repo('STATUS.md', 133, 'ESC STICK on MOTOR & ESC > PROGRAM runs 24 of the 72 profiles'),
  },
  {
    id: 'tap', group: 'esc',
    name: t('Read the beeps of the ESC (phase tap)', 'ESC-Pieptöne lesen (Phasenabgriff)'),
    needs: ['sim', 'tap'], drives: 'esc', src: repo('STATUS.md', 127, 'Phase tap (ESC tones)'),
  },
  {
    id: 'supply', group: 'bench',
    name: t('Supply set from the bench, 1 to 20 V', 'Versorgung vom Prüfstand gestellt, 1 bis 20 V'),
    needs: ['pdmini'], src: repo('STATUS.md', 109, 'the IOmcu runs it on a PIO UART on the pins the SUPPLY page'),
  },
  {
    id: 'logs', group: 'bench',
    name: t('Run logs on a card', 'Laufprotokolle auf Karte'),
    needs: ['sd'], src: repo('STATUS.md', 101, 'Logger | built: a run is written while ARMED'),
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
    because: same('PD mini'),
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
  {
    // The servo on the V pin of a strip: the jumper V Selector goes to 5V,
    // which feeds the V pins only from the DC input.
    id: 'osoyoo-dc',
    when: SERVO_STRIP,
    adds: 'dcsup',
    because: t('servo on the V pin, jumper on 5V', 'Servo am V-Pin, Jumper auf 5V'),
    src: OSOYOO_SCHEMATIC,
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
      'Both boards run the same release. With different protocol versions the panel shows "Protocol versions do not match. ARMED is blocked."',
      'Beide Platinen laufen mit demselben Release. Bei verschiedenen Protokollversionen zeigt das Panel „Protokollversionen passen nicht zusammen. ARMED ist gesperrt.“',
    ),
    when: null, src: repo('docs/Link.md', 79, 'Protocol versions do not match. ARMED is blocked.'),
  },
  {
    id: 'servo-pin', kind: 'note',
    text: t(
      'GP2 for the servo is a proposal of this page. The example of the build guide is GP0. GP0 and GP16 share a compare register, and GP16 carries the sensor bus. Whether the firmware refuses that pair: not stated.',
      'GP2 für das Servo ist ein Vorschlag dieser Seite. Das Beispiel der Bauanleitung ist GP0. GP0 und GP16 teilen sich ein Vergleichsregister, und GP16 trägt den Sensorbus. Ob die Firmware dieses Paar ablehnt: nicht angegeben.',
    ),
    when: SERVO_OUTPUTS, src: repo('docs/DShot.md', 133, 'Six of those pairs are both free on the header the IOmcu offers: GP0 and'),
  },
  {
    id: 'pd-pins', kind: 'note',
    text: t(
      'GP7 (PD mini TX) and GP6 (PD mini RX) come from an owner statement of 2026-10-11. The firmware default is −1, not wired. Set both under SETUP → INTERFACES.',
      'GP7 (PD mini TX) und GP6 (PD mini RX) stammen aus einer Angabe des Projektinhabers vom 2026-10-11. Die Voreinstellung der Firmware ist −1, nicht verdrahtet. Beide unter SETUP → ANSCHLÜSSE einstellen.',
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
    when: { has: 'ina3221' }, src: repo('docs/FirstRun.md', 566, '| Pull-ups | 2.2 kΩ from SDA and from SCL | 3V3 (pad 36) |'),
  },
  {
    id: 'matek-pullup', kind: 'measure',
    text: t(
      'MATEK board: the pull-up rail is not known. With the board powered and the IOmcu not connected, SDA and SCL read 3.3 V or less to GND.',
      'MATEK-Platine: Die Pull-up-Schiene ist nicht bekannt. Platine versorgt, IOmcu noch nicht verbunden: SDA und SCL liegen bei höchstens 3,3 V gegen GND.',
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
      'INA228 module with XT90 plugs and ESC power B: G of the module is the ESC end of the shunt and is wired to GND of the IOmcu. A connection between the minus output of the PD mini and GND of the IOmcu is a second path beside the shunt. The share of the ESC current that the sensor reads in this setup is not measured.',
      'INA228-Modul mit XT90-Steckern und ESC-Versorgung B: G des Moduls ist das ESC-Ende des Shunts und liegt an GND des IOmcu. Eine Verbindung zwischen dem Minus-Ausgang des PD mini und GND des IOmcu ist ein zweiter Pfad neben dem Shunt. Der Anteil des ESC-Stroms, den der Sensor in diesem Aufbau misst, ist nicht gemessen.',
    ),
    when: { all: [{ has: 'esc' }, XT90_SHUNT, { opt: 'escPower', is: 'pd' }] },
    src: OWNER,
  },
  {
    id: 'as5600-board', kind: 'unknown',
    text: t(
      'AS5600 board: pull-ups on the board are not known. The print of the pin GPO is not confirmed. It is tied to GND.',
      'AS5600-Platine: Die Pull-ups auf der Platine sind nicht bekannt. Der Aufdruck des Pins GPO ist nicht bestätigt. Er liegt an GND.',
    ),
    when: { has: 'as5600' }, src: guide('stage-4'),
  },
  {
    id: 'tap-ground', kind: 'measure',
    text: t(
      'Phase tap: everything unpowered, ESC power negative to IOmcu GND (pad 28) reads near 0 Ω. With an opto-isolated ESC input the tap reads nothing.',
      'Phasenabgriff: alles stromlos, Minus der ESC-Versorgung gegen GND des IOmcu (Pad 28) liest nahe 0 Ω. Bei einem ESC-Eingang mit Optokoppler liest der Abgriff nichts.',
    ),
    when: { has: 'tap' }, src: guide('tap'),
  },
  {
    id: 'sim-ground', kind: 'note',
    text: t(
      'No ground wire from the simulator to the IOmcu. It runs parallel to the ground of the ESC and can carry motor current.',
      'Kein Massedraht vom Simulator zum IOmcu. Er liegt parallel zur Masse des ESC und kann Motorstrom führen.',
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
      'T-plug: the V pad carries no wire.',
      'T-Stecker: Das Pad V bekommt keinen Draht.',
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
      'Osoyoo breakout board: every IO has a strip of 3 pins. From the terminal inward: S, V, G. S joins the pin of the Pico board directly. All V pins of both sides are 1 net. The 3-pin jumper V Selector (print 5V and 3V3) feeds that net from the 5 V converter of the DC input or from the 3.3 V regulator. An ESC lead goes to S and G only: the V pin in the middle stays free.',
      'Osoyoo-Breakout-Platine: Jeder IO hat eine Leiste mit 3 Pins. Von der Klemme nach innen: S, V, G. S liegt direkt am Pin der Pico-Platine. Alle V-Pins beider Seiten sind 1 Netz. Der 3-polige Jumper V Selector (Aufdruck 5V und 3V3) speist dieses Netz aus dem 5-V-Wandler des DC-Eingangs oder aus dem 3,3-V-Regler. Ein ESC-Kabel geht nur an S und G: Der V-Pin in der Mitte bleibt frei.',
    ),
    when: { opt: 'connection', is: 'osoyoo' }, src: OSOYOO_SCHEMATIC,
  },
  {
    id: 'strip-esc', kind: 'note',
    text: t(
      'Osoyoo breakout board: the ESC lead uses the S and G pins of the strip at GP5 only. The V pin in the middle carries the voltage of the jumper V Selector and stays free: the plus wire of the ESC lead is connected nowhere.',
      'Osoyoo-Breakout-Platine: Das ESC-Kabel nutzt nur die Pins S und G der Leiste an GP5. Der V-Pin in der Mitte führt die Spannung des Jumpers V Selector und bleibt frei: Die Plusader des ESC-Kabels wird nirgends angeschlossen.',
    ),
    when: { all: [{ has: 'esc' }, { opt: 'connection', is: 'osoyoo' }] }, src: OWNER,
  },
  {
    id: 'strip-servo-plus', kind: 'note',
    text: t(
      'Osoyoo breakout board, servo supply other than the strip: the servo lead connects only its signal and GND to the board, at the S pin of the strip at GP2 (GP13 with the servo programmer) and a G pin. Its plus wire goes to the servo supply, the external supply or the PD mini, as the drawing shows; with the INA3221 board in the setup it goes to VIN1- of that board, and the supply to VIN1+. The V pin of the strip stays free. All 40 V pins are 1 net fed through the jumper V Selector: a supply on 1 V pin reaches every strip and the output of the converter or the regulator.',
      'Osoyoo-Breakout-Platine, Servoversorgung nicht über die Leiste: Das Servokabel verbindet nur Signal und GND mit der Platine, am S-Pin der Leiste an GP2 (GP13 mit dem Servo-Programmierer) und an einem G-Pin. Seine Plusader geht an die Servoversorgung, die externe Versorgung oder den PD mini, wie die Zeichnung zeigt; mit der INA3221-Platine im Aufbau geht sie an VIN1- dieser Platine und die Versorgung an VIN1+. Der V-Pin der Leiste bleibt frei. Alle 40 V-Pins sind 1 Netz, gespeist über den Jumper V Selector: Eine Versorgung an 1 V-Pin erreicht jede Leiste und den Ausgang des Wandlers oder des Reglers.',
    ),
    when: { all: [{ has: 'servo' }, { opt: 'connection', is: 'osoyoo' }, { any: [{ has: 'pdmini' }, { opt: 'servoPower', is: 'external' }] }] },
    src: OWNER,
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
    id: 'prog-target', kind: 'note',
    text: t(
      'Servo programmer: the servo is on the programming port, GP13 (pad 17), unless another pin is stored for port 1. On the PROG PORT entry of SETUP → OUTPUTS, set SERVO TESTER to PROG PORT: with OUTPUTS as target the programmer is locked. The sweep, CENTRE, RELEASE, the hold and the automatic test then drive the same pin when PORT PWM on that entry is ON; with PORT PWM OFF the port sends no pulse. A slot of OUTPUTS on GP13 leaves the port without a pin.',
      'Servo-Programmierer: Das Servo hängt am Programmierport, GP13 (Pad 17), sofern für Port 1 kein anderer Pin gespeichert ist. Auf dem Eintrag PROG-PORT von SETUP → OUTPUTS SERVO-TESTER auf PROG-PORT stellen: Mit OUTPUTS als Ziel ist der Programmierer gesperrt. Sweep, ZENTRIEREN, FREIGEBEN, das Halten und der automatische Test treiben dann denselben Pin, wenn PORT-PWM auf diesem Eintrag auf EIN steht; mit PORT-PWM AUS sendet der Port keinen Puls. Ein Slot von OUTPUTS auf GP13 lässt den Port ohne Pin.',
    ),
    when: SERVO_PROG, src: repo(PROGRAMMER, 82, '| SERVO TESTER | **OUTPUTS**, **PROG PORT** |'),
  },
  {
    id: 'prog-supply-pd', kind: 'note',
    text: t(
      'Servo programmer with the PD mini: CONNECT switches an output that is off on at the programming voltage, 4.8 to 6.0 V in steps of 0.1 V, up to 8.4 V with HV SERVO on, and at the current limit, 50 to 3000 mA. The defaults are 5.0 V and 1000 mA. An output that is already on runs at the lower of its voltage and the programming voltage and at the lower of the two current limits; CONNECT raises neither. An output that is on under 4.8 V is not programmed until it is raised. STOP switches the supply off.',
      'Servo-Programmierer mit dem PD mini: VERBINDEN schaltet einen ausgeschalteten Ausgang mit der Programmierspannung ein, 4,8 bis 6,0 V in Schritten von 0,1 V, bis 8,4 V mit HV-SERVO auf EIN, und mit der Stromgrenze, 50 bis 3000 mA. Voreingestellt sind 5,0 V und 1000 mA. Ein Ausgang, der schon an ist, läuft mit der kleineren der beiden Spannungen und der kleineren der beiden Stromgrenzen; VERBINDEN erhöht keinen der beiden Werte. Ein Ausgang, der unter 4,8 V an ist, wird erst programmiert, wenn er erhöht ist. STOP schaltet die Versorgung ab.',
    ),
    when: { all: [SERVO_PROG, { has: 'pdmini' }] },
    src: repo(PROGRAMMER, 205, '4.8 to 6.0 V in 0.1 V; up to 8.4 V with HV SERVO on'),
  },
  {
    id: 'prog-supply-own', kind: 'note',
    text: t(
      'Servo programmer without the PD mini: the programming voltage and the current limit are stored and not applied. The servo runs from the supply of the variant "Servo power". STOP switches no supply.',
      'Servo-Programmierer ohne den PD mini: Programmierspannung und Stromgrenze werden gespeichert und nicht angewendet. Das Servo läuft an der Versorgung der Variante „Servoversorgung“. STOP schaltet keine Versorgung.',
    ),
    when: { all: [SERVO_PROG, { not: { has: 'pdmini' } }] },
    src: repo(PROGRAMMER, 269, 'Without a PD mini the voltage and the current limit are stored and not'),
  },
  {
    id: 'prog-level', kind: 'unknown',
    text: t(
      "High level of the servo's reply on GP13: not measured, at most 5 V and typically 3.3 V. GP13 is rated 5.5 V with the IOmcu powered and 3.63 V without. Power the IOmcu before the servo's supply.",
      'Pegel der Antwort des Servos an GP13: nicht gemessen, höchstens 5 V und typisch 3,3 V. GP13 verträgt 5,5 V bei versorgtem IOmcu und 3,63 V ohne Versorgung. Den IOmcu vor der Versorgung des Servos einschalten.',
    ),
    when: SERVO_PROG, src: repo(PROGRAMMER, 185, 'The level is not measured. GP13 is rated 5.5 V while the'),
  },
  {
    id: 'servo-bench-supply', kind: 'note',
    text: t(
      'The bench itself feeds the servo. At the 5 V pin of the IOmcu board the servo then draws its current from the supply that also powers the logic. This is prone to problems. At the V pin of the Osoyoo breakout board the source depends on the jumper V Selector. For anything but a small servo use a 2S LiPo battery or another external supply that matches the servo.',
      'Der Prüfstand selbst speist das Servo. Am 5-V-Pin der IOmcu-Platine zieht das Servo seinen Strom dann aus der Versorgung, die auch die Logik speist. Das ist störanfällig. Am V-Pin der Osoyoo-Breakout-Platine hängt die Quelle vom Jumper V Selector ab. Für alles außer einem kleinen Servo einen 2S-LiPo-Akku oder eine andere externe Versorgung verwenden, die zum Servo passt.',
    ),
    when: SERVO_BENCH, src: OWNER,
  },
  {
    id: 'servo-5v-pin', kind: 'unknown',
    text: t(
      'Current limit of the 5 V pin of the IOmcu board (VBUS, pad 40): not stated. How much current a small servo is: not stated.',
      'Stromgrenze des 5-V-Pins der IOmcu-Platine (VBUS, Pad 40): nicht angegeben. Wie viel Strom ein kleines Servo ist: nicht angegeben.',
    ),
    when: SERVO_5V_PIN, src: OWNER,
  },
  {
    id: 'strip-jumper', kind: 'note',
    text: t(
      'Osoyoo breakout board, warning: the jumper V Selector sets the voltage of every V pin. Check it before a servo is plugged in. Position 5V gives 5 V, but only while a DC supply is connected and the OFF/ON switch is on ON; without it the V pins carry 0 V. Position 3V3 feeds the V pins from the 3.3 V regulator, which gives 3.3 V where the regulator is powered; whether USB alone powers it is not resolved. Set the jumper to 5V for the servo.',
      'Osoyoo-Breakout-Platine, Warnung: Der Jumper V Selector stellt die Spannung aller V-Pins. Vor dem Einstecken eines Servos prüfen. Stellung 5V gibt 5 V, aber nur, solange eine DC-Versorgung angeschlossen ist und der Schalter OFF/ON auf ON steht; ohne sie führen die V-Pins 0 V. Stellung 3V3 speist die V-Pins aus dem 3,3-V-Regler und gibt 3,3 V, wo der Regler versorgt ist; ob USB allein ihn versorgt, ist nicht geklärt. Den Jumper für das Servo auf 5V setzen.',
    ),
    when: SERVO_STRIP, src: OSOYOO_SCHEMATIC,
  },
  {
    id: 'strip-dc', kind: 'note',
    text: t(
      'Osoyoo breakout board, DC supply: connect exactly 1 supply: at the DC jack, at the VIN terminal or at the 2-pin header H4 (PWRIN and VIN-), never at 2 of them. The vendor states not to use the DC jack and the screw terminal together. The vendor recommends 7 to 12 V.',
      'Osoyoo-Breakout-Platine, DC-Versorgung: Genau 1 Versorgung anschließen: an der DC-Buchse, an der VIN-Klemme oder am 2-poligen Pfostenverbinder H4 (PWRIN und VIN-), nie an 2 davon. Der Hersteller gibt an, die DC-Buchse und die Schraubklemme nicht zusammen zu verwenden. Der Hersteller empfiehlt 7 bis 12 V.',
    ),
    when: SERVO_STRIP, src: OSOYOO_PAGE,
  },
  {
    id: 'strip-measure', kind: 'measure',
    text: t(
      'Osoyoo breakout board, output check: measure the V pin of the strip at GP2 to the G pin of the same strip before the servo is plugged in. Expected per position of the jumper V Selector: 5V with a DC supply connected and the switch on ON, the output of the converter, 5 V nominal (its range is in the converter entry); 5V without a DC supply or with the switch on OFF, 0 V; 3V3, 3.3 V where the regulator is powered (whether USB alone powers it is not resolved).',
      'Osoyoo-Breakout-Platine, Ausgangsprüfung: Vor dem Einstecken des Servos den V-Pin der Leiste an GP2 gegen den G-Pin derselben Leiste messen. Erwartet je Stellung des Jumpers V Selector: 5V mit angeschlossener DC-Versorgung und Schalter auf ON der Ausgang des Wandlers, 5 V nominal (sein Bereich steht im Wandler-Eintrag); 5V ohne DC-Versorgung oder mit Schalter auf OFF 0 V; 3V3 3,3 V, wo der Regler versorgt ist (ob USB allein ihn versorgt, ist nicht geklärt).',
    ),
    when: SERVO_STRIP, src: OSOYOO_SCHEMATIC,
  },
  {
    id: 'strip-feed', kind: 'note',
    text: t(
      'Osoyoo breakout board, feed of the V pins: the jumper V Selector is the 3-pin header H3. Pin 1 is the net 3.3V from the regulator U1, pin 2 is the net VCC, pin 3 is the net VOUT_5V from the converter U9. VCC is the V pin of all 40 strips. The regulator takes its input from VSYS of the Pico board. The converter takes its input from the DC jack, the VIN terminal or the 2-pin header H4 (pin 2 PWRIN, pin 1 VIN-) through the OFF/ON switch. No part joins VOUT_5V to the V pins when the jumper is on 3V3, and none joins VBUS to them when it is on 5V: with the jumper on 5V and no DC supply the V pins carry 0 V.',
      'Osoyoo-Breakout-Platine, Speisung der V-Pins: Der Jumper V Selector ist der 3-polige Pfostenverbinder H3. Pin 1 ist das Netz 3.3V vom Regler U1, Pin 2 ist das Netz VCC, Pin 3 ist das Netz VOUT_5V vom Wandler U9. VCC ist der V-Pin aller 40 Leisten. Der Regler bezieht seinen Eingang aus VSYS der Pico-Platine. Der Wandler bezieht seinen Eingang aus der DC-Buchse, der VIN-Klemme oder dem 2-poligen Pfostenverbinder H4 (Pin 2 PWRIN, Pin 1 VIN-) über den Schalter OFF/ON. Kein Bauteil verbindet VOUT_5V mit den V-Pins, wenn der Jumper auf 3V3 steht, und keines verbindet VBUS mit ihnen, wenn er auf 5V steht: Mit dem Jumper auf 5V und ohne DC-Versorgung führen die V-Pins 0 V.',
    ),
    when: SERVO_STRIP, src: OSOYOO_SCHEMATIC,
  },
  {
    id: 'strip-converter', kind: 'note',
    text: t(
      'Osoyoo breakout board, converter chip XL1509, datasheet values of the 5.0 V version: input 7 to 40 V, output 4.8 to 5.2 V at 0.2 to 2 A, switching frequency 150 kHz (127 to 173 kHz), switch current limit 4 A, efficiency 82 % typical at 12 V in and 2 A.',
      'Osoyoo-Breakout-Platine, Wandler-Chip XL1509, Datenblattwerte der 5,0-V-Version: Eingang 7 bis 40 V, Ausgang 4,8 bis 5,2 V bei 0,2 bis 2 A, Schaltfrequenz 150 kHz (127 bis 173 kHz), Schaltstromgrenze 4 A, Wirkungsgrad typisch 82 % bei 12 V Eingang und 2 A.',
    ),
    when: SERVO_STRIP, src: XL1509_DATASHEET,
  },
  {
    id: 'strip-input', kind: 'note',
    text: t(
      'Osoyoo breakout board, DC input: the input range is set by the board, not by the converter chip. The vendor recommends 7 to 12 V and limits the input voltage to 7 to 18 V. The vendor rates the output of the board at 5 V and 2 A, or 3.3 V and 500 mA.',
      'Osoyoo-Breakout-Platine, DC-Eingang: Der Eingangsbereich wird von der Platine bestimmt, nicht vom Wandler-Chip. Der Hersteller empfiehlt 7 bis 12 V und begrenzt die Eingangsspannung auf 7 bis 18 V. Der Hersteller gibt den Ausgang der Platine mit 5 V und 2 A oder 3,3 V und 500 mA an.',
    ),
    when: SERVO_STRIP, src: OSOYOO_PAGE,
  },
  {
    id: 'strip-print', kind: 'note',
    text: t(
      'Osoyoo breakout board, prints: the DC jack reads 7 to 16 V and the 2 electrolytic capacitors beside the converter are printed 100 uF 16 V. A supply above 16 V is outside the print and the capacitor rating.',
      'Osoyoo-Breakout-Platine, Aufdrucke: Die DC-Buchse trägt den Aufdruck 7 bis 16 V, und die 2 Elektrolytkondensatoren neben dem Wandler sind mit 100 uF 16 V bedruckt. Eine Versorgung über 16 V liegt außerhalb des Aufdrucks und der Kondensatorangabe.',
    ),
    when: SERVO_STRIP, src: OSOYOO_PHOTOS,
  },
  {
    id: 'strip-regulator', kind: 'note',
    text: t(
      'Osoyoo breakout board, 3.3 V position: the jumper cap sits on the 3V3 side as delivered (osoyoo.com page; osoyoo.store: not stated). The regulator is an ME6210A33M3G according to the vendor. The vendor rates it at 3.3 V and 500 mA. A servo on the V pin in this position draws from VSYS of the IOmcu board, the supply that also powers the logic, so a servo that draws more than 0.5 A is outside the rating of the regulator. For USB alone with no voltage on the V pins, the vendor names a workaround: a Dupont cable from the S pin of the 3.3 V row to a V pin. Do not use it for the servo. The S pin of the 3.3 V row is the 3V3 pin of the IOmcu board (pad 36), the 3.3 V supply of its logic; its current limit is not stated. With the cable in place and the jumper cap on 5V, the 5 V of the converter reaches that supply. A cable already in place comes off before the jumper cap goes back on any position.',
      'Osoyoo-Breakout-Platine, Stellung 3,3 V: Die Jumperkappe sitzt im Lieferzustand auf der Seite 3V3 (Seite osoyoo.com; osoyoo.store: nicht angegeben). Der Regler ist laut Hersteller ein ME6210A33M3G. Der Hersteller gibt ihn mit 3,3 V und 500 mA an. Ein Servo am V-Pin in dieser Stellung zieht aus VSYS der IOmcu-Platine, der Versorgung, die auch die Logik speist; ein Servo mit mehr als 0,5 A liegt damit außerhalb der Angabe des Reglers. Bei USB allein ohne Spannung an den V-Pins nennt der Hersteller als Abhilfe ein Dupont-Kabel vom S-Pin der 3,3-V-Reihe zu einem V-Pin. Für das Servo nicht verwenden. Der S-Pin der 3,3-V-Reihe ist der 3V3-Pin der IOmcu-Platine (Pad 36), die 3,3-V-Versorgung ihrer Logik; seine Stromgrenze ist nicht angegeben. Mit gestecktem Kabel und der Jumperkappe auf 5V erreichen die 5 V des Wandlers diese Versorgung. Ein schon gestecktes Kabel kommt ab, bevor die Jumperkappe wieder auf eine Stellung gesteckt wird.',
    ),
    when: SERVO_STRIP, src: OSOYOO_PAGE,
  },
  {
    id: 'strip-parts', kind: 'note',
    text: t(
      'Osoyoo breakout board, other chips: the 5 SN74HC04 hex inverters take the Pico pins as inputs and drive only the LEDs of the IO pins, with their supply at VSYS. They sit neither on the V net nor in the path from S to the terminal: S joins the Pico pin directly and nothing is level-shifted or inverted. The SI2302 (Q3) is an N-channel MOSFET in the lead VIN- of the VIN terminal and of H4 and protects the terminal and H4 only against reverse polarity. The minus of the DC jack is GND directly: the jack has no reverse-polarity protection. A second MOSFET Q1 joins VOUT_5V to VSYS, its gate at VBUS: it passes the converter to VSYS only while no USB voltage is present.',
      'Osoyoo-Breakout-Platine, weitere Chips: Die 5 Sechsfach-Inverter SN74HC04 nehmen die Pico-Pins als Eingänge und treiben nur die LEDs der IO-Pins, ihre Versorgung liegt an VSYS. Sie liegen weder am V-Netz noch im Weg von S zur Klemme: S liegt direkt am Pico-Pin, nichts wird im Pegel gewandelt oder invertiert. Der SI2302 (Q3) ist ein N-Kanal-MOSFET in der Leitung VIN- der VIN-Klemme und von H4 und schützt nur die Klemme und H4 gegen Verpolung. Das Minus der DC-Buchse liegt direkt an GND: Die Buchse hat keinen Verpolschutz. Ein zweiter MOSFET Q1 verbindet VOUT_5V mit VSYS, sein Gate liegt an VBUS: Er gibt den Wandler nur auf VSYS, solange keine USB-Spannung anliegt.',
    ),
    when: { opt: 'connection', is: 'osoyoo' }, src: OSOYOO_SCHEMATIC,
  },
  {
    id: 'strip-load', kind: 'note',
    text: t(
      'Osoyoo breakout board, load on a signal pin: each pin carries 1 inverter input of an SN74HC04 as load, leakage up to 1 uA and 3 to 10 pF (datasheet).',
      'Osoyoo-Breakout-Platine, Last an einem Signalpin: Jeder Pin trägt 1 Inverter-Eingang eines SN74HC04 als Last, Leckstrom bis 1 uA und 3 bis 10 pF (Datenblatt).',
    ),
    when: { opt: 'connection', is: 'osoyoo' }, src: SN74HC04_DATASHEET,
  },
  {
    id: 'strip-level', kind: 'note',
    text: t(
      'Osoyoo breakout board, signal level: the vendor states that the Pico accepts 3.3 V I/O only. S of a strip joins the Pico pin directly, so a signal above 3.3 V at S reaches the pin. In position 5V the V pin carries 5 V, and a device fed from it can drive S above 3.3 V.',
      'Osoyoo-Breakout-Platine, Signalpegel: Der Hersteller gibt an, dass der Pico nur 3,3-V-Ein- und Ausgänge verträgt. S einer Leiste liegt direkt am Pico-Pin, ein Signal über 3,3 V an S erreicht also den Pin. In Stellung 5V führt der V-Pin 5 V, und ein davon gespeistes Gerät kann S über 3,3 V treiben.',
    ),
    when: { opt: 'connection', is: 'osoyoo' }, src: OSOYOO_PAGE,
  },
  {
    id: 'strip-plug', kind: 'note',
    text: t(
      'Osoyoo breakout board: the servo lead plugs onto the strip at GP2. S carries the signal of wire 7, V the plus of wire 61 and G the GND of wire 8. The plug replaces these 3 wires.',
      'Osoyoo-Breakout-Platine: Das Servokabel steckt auf der Leiste an GP2. S führt das Signal von Draht 7, V das Plus von Draht 61 und G das GND von Draht 8. Der Stecker ersetzt diese 3 Drähte.',
    ),
    when: { all: [SERVO_STRIP, SERVO_OUTPUTS, { not: { has: 'ina3221' } }] }, src: OWNER,
  },
  {
    id: 'strip-prog', kind: 'note',
    text: t(
      'Osoyoo breakout board with the servo programmer: the servo lead does not plug onto the strip at GP2 as a whole. S of the lead goes to GP13 by wire 64, the plus and GND go where the drawing shows them. The S pin of the strip at GP2 stays free.',
      'Osoyoo-Breakout-Platine mit dem Servo-Programmierer: Das Servokabel steckt nicht als Ganzes auf der Leiste an GP2. S des Kabels geht über Draht 64 an GP13, Plus und GND gehen dorthin, wo die Zeichnung sie zeigt. Der S-Pin der Leiste an GP2 bleibt frei.',
    ),
    when: { all: [SERVO_STRIP, SERVO_PROG] }, src: OWNER,
  },
  {
    id: 'strip-supply', kind: 'unknown',
    text: t(
      'Osoyoo breakout board: the currents of 2 A at 5 V and 500 mA at 3.3 V are vendor values, not measured. The marking of the converter chip is not stated in the schematic; the feedback pin joins the output directly, which fits the fixed 5 V version XL1509-5.0. The part number of the MOSFET Q1 is not stated. With USB alone and the jumper on 3V3 the schematic feeds the 3.3 V regulator from VSYS, while the product page says the V pins carry no voltage with USB alone and a jumper cap on the selector; which holds is not measured. The stall current of the servo is not stated.',
      'Osoyoo-Breakout-Platine: Die Ströme von 2 A bei 5 V und 500 mA bei 3,3 V sind Herstellerangaben, nicht gemessen. Die Kennzeichnung des Wandler-Chips steht nicht im Schaltplan; der Feedback-Pin liegt direkt am Ausgang, was zur festen 5-V-Version XL1509-5.0 passt. Die Teilenummer des MOSFET Q1 ist nicht angegeben. Mit USB allein und dem Jumper auf 3V3 speist der Schaltplan den 3,3-V-Regler aus VSYS, während die Produktseite sagt, dass die V-Pins mit USB allein und einer Jumperkappe am Selector keine Spannung führen; was gilt, ist nicht gemessen. Der Blockierstrom des Servos ist nicht angegeben.',
    ),
    when: SERVO_STRIP, src: OSOYOO_PAGE,
  },
  {
    id: 'esc-programming-power', kind: 'unknown',
    text: t(
      `ESC programming: an owner statement of 2026-10-11 says it needs no power source of the bench. docs/StickProgramming.md of release ${VERSION} powers the ESC from the PD mini and counts the beeps from the supply current. The page follows the document: ESC STICK programming needs the PD mini. What powers an ESC during other programming: not stated.`,
      `ESC-Programmierung: Eine Angabe des Projektinhabers vom 2026-10-11 nennt keine Stromquelle des Prüfstands als nötig. docs/StickProgramming.md von Release ${VERSION} versorgt den ESC aus dem PD mini und zählt die Pieptöne am Versorgungsstrom. Die Seite folgt dem Dokument: Die ESC-STICK-Programmierung braucht den PD mini. Was einen ESC bei anderer Programmierung versorgt: nicht angegeben.`,
    ),
    when: { all: [{ has: 'esc' }, { has: 'pdmini' }, { opt: 'escPower', is: 'pd' }] },
    src: repo('docs/StickProgramming.md', 6, 'PD mini'),
  },
];

// ---------------------------------------------------------------------------
// Stages, safety notes, the list of items in no release. Build guide wording.
// ---------------------------------------------------------------------------

const stages: Stage[] = [
  { n: 1, title: t('Stage 1: panel and IOmcu', 'Stufe 1: Panel und IOmcu'), src: guide('stage-1') },
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
      'Stufe 4: AS5600, der Winkel des Ruderhorns',
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
      "<b>ESC power from two sources only:</b> A, the ESC's own battery, or B, the PD mini output within its limits: 1 to 20 V, up to 3 A, and at least 0.7 V below its input voltage. A set point over the input voltage puts the module into ERR until it is power cycled.",
      '<b>Versorgung des ESC nur aus zwei Quellen:</b> A, dem eigenen Akku des ESC, oder B, dem Ausgang des PD mini innerhalb seiner Grenzen: 1 bis 20 V, bis 3 A und mindestens 0,7 V unter der Eingangsspannung. Ein Sollwert über der Eingangsspannung versetzt das Modul in den Zustand ERR, bis es aus- und wieder eingeschaltet wird.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>The panel and the IOmcu never power the ESC.</b> Keep their 5 V and 3.3 V pins away from the ESC.',
      '<b>Panel und IOmcu versorgen den ESC nie.</b> Ihre 5-V- und 3,3-V-Pins bleiben vom ESC getrennt.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>ESC lead: only signal and ground</b> go to the IOmcu. The centre wire (BEC +) is connected nowhere.',
      '<b>ESC-Kabel: nur Signal und Masse</b> zum IOmcu. Die Mittelader (BEC +) wird nirgends angeschlossen.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>STOP</b> switches the bench to SAFE, switches the PD mini output off, and latches until the next ARM. Switching to ARMED takes a 2 s hold on ARM; switching to SAFE takes a press; leaving a bench screen switches to SAFE.',
      '<b>STOP</b> schaltet den Prüfstand auf SAFE, schaltet den Ausgang des PD mini ab und rastet bis zum nächsten ARM ein. Auf ARMED schalten: ARM 2 s halten. Auf SAFE schalten: kurz tippen. Auch das Verlassen einer Prüfstandsseite schaltet auf SAFE.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      '<b>The heartbeat wire is the interlock.</b> The monostable that should back it in hardware is on no board yet, so the firmware at both ends is the whole interlock. An IOmcu that hangs while ARMED is not stopped by anything.',
      '<b>Der Heartbeat-Draht ist die Verriegelung.</b> Das Monoflop, das ihn in Hardware absichern soll, sitzt noch auf keiner Platine; die Verriegelung ist allein die Firmware beider Platinen. Hängt der IOmcu in ARMED, stoppt ihn nichts.',
    ),
    src: guide('safety'),
  },
  {
    text: t(
      'An ARMED bench drives every bound pin. A pin bound as SERVO PWM rests at 1500 µs, which an ESC can read as about half throttle. Bind an ESC as MOTOR PWM or DShot only.',
      'Ein Prüfstand in ARMED treibt jeden gebundenen Pin. Ein als SERVO PWM gebundener Pin ruht bei 1500 µs, für einen ESC etwa Halbgas. Einen ESC deshalb nur als MOTOR PWM oder DShot binden.',
    ),
    src: guide('safety'),
  },
];

const notInRelease: Catalogue['notInRelease'] = [
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
      `MT6701 magnetic encoder board: not read by ${VERSION}.`,
      `MT6701-Magnetencoder-Platine: wird von ${VERSION} nicht gelesen.`,
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
    src: repo('STATUS.md', 624, 'ESCs with BLHeli_S and AM32 firmware, Hitec D-series servos | screen built; no protocol on a wire'),
  },
];

// ---------------------------------------------------------------------------
// Pins. The visitor does not choose pins; the data states one per signal.
//
// allowed: the values the firmware offers. 'catalogue' is any GPIO of the
// output catalogue in out_bind.c that is not reserved.
// ---------------------------------------------------------------------------

const pinDefaults: PinDefault[] = [
  { signal: 'heartbeat', gpio: 3, pad: 5, choice: 'fixed', role: 'input', allowed: [3], src: repo(IOMCU_PINS, 42, '#define IOMCU_HEARTBEAT_PIN 3') },
  {
    signal: 'sda', gpio: 16, pad: 21, choice: 'default', role: 'bus',
    // settings_sense_sda_valid(): −1 and the SDA pins whose pair is free.
    allowed: [-1, 0, 4, 6, 14, 16, 18, 20, 26],
    src: repo(SETTINGS, 162, '"sns_sda", "Sensor SDA"'),
  },
  {
    signal: 'scl', gpio: 17, pad: 22, choice: 'default', role: 'bus',
    // SCL follows SDA: the GPIO after it.
    allowed: [-1, 1, 5, 7, 15, 17, 19, 21, 27],
    src: repo(SETTINGS, 165, '"sns_scl", "Sensor SCL"'),
  },
  { signal: 'tap', gpio: 22, pad: 29, choice: 'default', role: 'input', allowed: 'catalogue', src: repo(SETTINGS, 192, '"tone_pin", "Tap pin"') },
  { signal: 'esc', gpio: 5, pad: 7, choice: 'example', role: 'output', allowed: 'catalogue', src: guide('wires') },
  { signal: 'servo', gpio: 2, pad: 4, choice: 'proposal', role: 'output', allowed: 'catalogue', src: PAGE },
  { signal: 'prog', gpio: 13, pad: 17, choice: 'default', role: 'output', allowed: 'catalogue', src: repo(PROG_PORT, 61, '#define PROG_PORT_DEFAULT_PIN 13u') },
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
      'The panel and the IOmcu each run from their own USB-C cable. The panel is on a charger of at least 2 A with a short cable.',
      'Panel und IOmcu laufen je an einem eigenen USB-C-Kabel. Das Panel hängt mit kurzem Kabel an einem Ladegerät mit mindestens 2 A.',
    ),
    volts: { min: 5, max: 5 }, amps: null, basis: null,
    pros: [t('Each board has its own supply.', 'Jede Platine hat ihre eigene Versorgung.')],
    cons: [t(
      'A servo on the 5 V pin, or on a V pin of the Osoyoo breakout board with the jumper on 3V3, draws from it.',
      'Ein Servo am 5-V-Pin, oder an einem V-Pin der Osoyoo-Breakout-Platine mit dem Jumper auf 3V3, zieht daraus.',
    )],
    unknowns: [t('Current the IOmcu board draws: not stated.', 'Strom, den die IOmcu-Platine zieht: nicht angegeben.')],
    when: null, used: { has: 'usb' }, src: guide('also-needed'),
  },
  {
    id: 'copro-5v', feeds: 'servo',
    label: t('5 V pin of the IOmcu board (VBUS, pad 40)', '5-V-Pin der IOmcu-Platine (VBUS, Pad 40)'),
    text: t(
      'For a small servo. The plus of the servo goes to VBUS, the 5 V of the USB cable of the IOmcu. Signal and GND come from the pads of the IOmcu.',
      'Für ein kleines Servo. Das Plus des Servos geht an VBUS, die 5 V des USB-Kabels des IOmcu. Signal und GND kommen von den Pads des IOmcu.',
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
      'For a small servo. Every IO of the board has a strip with the pins S, V and G. All V pins are 1 net. The jumper V Selector sets it: position 5V feeds it from the XL1509 converter of the DC input, position 3V3 from the 3.3 V regulator. The servo lead plugs onto the strip of its pin.',
      'Für ein kleines Servo. Jeder IO der Platine hat eine Leiste mit den Pins S, V und G. Alle V-Pins sind 1 Netz. Der Jumper V Selector stellt es: Stellung 5V speist es aus dem XL1509-Wandler des DC-Eingangs, Stellung 3V3 aus dem 3,3-V-Regler. Das Servokabel steckt auf der Leiste seines Pins.',
    ),
    volts: { min: 3.3, max: 5, steps: [3.3, 5] }, amps: null, basis: VENDOR,
    pros: [
      t('Without the servo programmer and without the INA3221 current sensor the servo plugs straight in.', 'Ohne den Servo-Programmierer und ohne den Stromsensor INA3221 wird das Servo direkt eingesteckt.'),
      t('It needs no soldering.', 'Sie braucht kein Löten.'),
      t(
        'In position 5V the servo draws from the DC input through a converter rated 2 A. With USB connected this is not the supply of the logic; without USB voltage the converter also feeds VSYS of the Pico board, and the servo and the logic share the 2 A.',
        'In Stellung 5V zieht das Servo aus dem DC-Eingang über einen Wandler mit 2 A. Mit angeschlossenem USB ist das nicht die Versorgung der Logik; ohne USB-Spannung speist der Wandler auch VSYS der Pico-Platine, und Servo und Logik teilen sich die 2 A.',
      ),
    ],
    cons: [
      t(
        'Warning: the jumper needs a check before a servo is plugged in. In position 3V3 the servo gets 3.3 V where the regulator is powered; whether USB alone powers it is not resolved.',
        'Warnung: Der Jumper muss vor dem Einstecken eines Servos geprüft werden. In Stellung 3V3 bekommt das Servo 3,3 V, wo der Regler versorgt ist; ob USB allein ihn versorgt, ist nicht geklärt.',
      ),
      t(
        'As delivered the jumper cap sits on the 3V3 side (osoyoo.com page), so position 5V is a change. Position 5V needs a DC supply of 7 to 12 V at the DC jack, the VIN terminal or the header H4, exactly 1 of the three (vendor recommendation; print 7 to 16 V, vendor limit 18 V, capacitors rated 16 V) and the switch on ON; USB does not feed it. Without it the V pins carry 0 V. The supply is in the parts list as a modelled part.',
        'Im Lieferzustand sitzt die Jumperkappe auf der Seite 3V3 (Seite osoyoo.com), Stellung 5V ist also eine Änderung. Stellung 5V braucht eine DC-Versorgung von 7 bis 12 V an der DC-Buchse, der VIN-Klemme oder dem Pfostenverbinder H4, an genau einem der drei Anschlüsse (Herstellerempfehlung; Aufdruck 7 bis 16 V, Herstellergrenze 18 V, Kondensatoren mit 16 V) und den Schalter auf ON; USB speist sie nicht. Ohne sie führen die V-Pins 0 V. Die Versorgung steht als modelliertes Teil in der Teileliste.',
      ),
      t(
        'The rating is 2 A at 5 V and 500 mA at 3.3 V (vendor values). A stalled servo draws more than that where its stall current is higher; the stall current of the servo is not stated here.',
        'Die Angabe lautet 2 A bei 5 V und 500 mA bei 3,3 V (Herstellerwerte). Ein blockiertes Servo zieht mehr, wenn sein Blockierstrom höher liegt; der Blockierstrom des Servos steht hier nicht.',
      ),
      t(
        'In position 3V3 the servo draws from VSYS of the IOmcu board, the supply that also powers the logic. A current peak can drop it and reset the boards.',
        'In Stellung 3V3 zieht das Servo aus VSYS der IOmcu-Platine, der Versorgung, die auch die Logik speist. Eine Stromspitze kann sie einbrechen lassen und die Platinen zurücksetzen.',
      ),
      t('The bench does not limit the current.', 'Der Prüfstand begrenzt den Strom nicht.'),
    ],
    unknowns: [
      t('Current of the board in a measurement: not measured; 2 A and 500 mA are vendor values.', 'Strom der Platine in einer Messung: nicht gemessen; 2 A und 500 mA sind Herstellerwerte.'),
      t('Whether position 3V3 works with USB alone: the schematic and the product page disagree, not measured.', 'Ob Stellung 3V3 mit USB allein arbeitet: Schaltplan und Produktseite widersprechen sich, nicht gemessen.'),
      t('Stall current of the servo: not stated.', 'Blockierstrom des Servos: nicht angegeben.'),
    ],
    when: { opt: 'connection', is: 'osoyoo' }, used: SERVO_STRIP, src: OSOYOO_SCHEMATIC,
  },
  {
    id: 'pd-servo', feeds: 'servo',
    label: t('PD mini from a USB-C PD charger', 'PD mini an einem USB-C-PD-Ladegerät'),
    text: t(
      'The bench sets the output of the PD mini over a UART. The output stays at least 0.7 V below the input voltage. PD: USB Power Delivery.',
      'Der Prüfstand stellt den Ausgang des PD mini über einen UART. Der Ausgang bleibt mindestens 0,7 V unter der Eingangsspannung. PD: USB Power Delivery.',
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
      `${ESC_SIGNAL_ONLY.en} The PD mini feeds the ESC. Its output stays at least 0.7 V below the input voltage.`,
      `${ESC_SIGNAL_ONLY.de} Der PD mini speist den ESC. Sein Ausgang bleibt mindestens 0,7 V unter der Eingangsspannung.`,
    ),
    volts: { min: 1, max: 20 }, amps: { min: 0.05, max: 3 }, basis: VENDOR,
    pros: [
      t('The bench sets the voltage and the current limit.', 'Der Prüfstand stellt Spannung und Stromgrenze.'),
      t('STOP switches its output off.', 'STOP schaltet seinen Ausgang ab.'),
      t('ESC STICK programming counts the beeps from its current.', 'Die ESC-STICK-Programmierung zählt die Pieptöne an seinem Strom.'),
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
  release: VERSION,
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
    'No further hardware: panel and IOmcu alone',
    'Ohne weitere Hardware: Panel und IOmcu allein',
  ),
  pinDefaults,
  powerSources,
});
