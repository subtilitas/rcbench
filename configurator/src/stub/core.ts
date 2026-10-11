// Stand-in for the core (data file and resolver) until configurator/core
// exists. Reduced data, enough to draw every module, every route type, both
// stub targets, the Osoyoo labels and the XT90 variant. Every source anchor is
// { kind: 'page' }. This folder is test material: the Pages workflow refuses
// to build while it exists.

import type {
  Catalogue, Check, Cond, Core, Fn, Options, OptionKey, Pad, Part, Pin, Reason, ResolvedFunction,
  ResolvedOption, ResolvedPart, Route, Rule, Selection, Setup, SignalClass, Stage, Text, Wire,
} from '../../contract.ts';

const SRC = { kind: 'page' } as const;
const OWNER = { kind: 'owner', date: '2026-10-11' } as const;
const T = (en: string, de: string = en): Text => ({ en, de });

export const DEFAULT_OPTIONS: Options = { connection: 'pads', ina228Board: 'matek', escPower: 'battery' };

type PinSpec = [id: string, en: string, de?: string, only?: Partial<Options>, unconfirmed?: boolean];

function part(
  id: string, name: Text, short: Text, role: Text | null,
  flags: { locked?: boolean; selectable?: boolean; notRun?: boolean },
  slot: Part['slot'], pins: PinSpec[] = [], variantName?: Part['variantName'],
): Part {
  const out: Part = {
    id, name, short, role,
    locked: flags.locked === true,
    selectable: flags.selectable === true,
    release: 'released',
    hardware: flags.notRun ? 'not-run' : 'run',
    slot,
    pins: pins.map(([pid, en, de, only, unconfirmed]): Pin => {
      const pin: Pin = { id: pid, label: T(en, de ?? en), confirmed: unconfirmed !== true, src: SRC };
      if (only) pin.only = only;
      return pin;
    }),
    src: SRC,
  };
  if (variantName) out.variantName = variantName;
  return out;
}

const none = { column: 'none', order: 0 } as const;

const PARTS: Part[] = [
  part('panel', T('Waveshare ESP32-S3-Touch-LCD-7 panel', 'Waveshare-ESP32-S3-Touch-LCD-7-Panel'), T('Panel'),
    T('Touch screen, STOP button, heartbeat source.', 'Touchscreen, STOP-Taste, Quelle des Heartbeat.'),
    { locked: true }, { column: 'L', order: 1, sub: T('ESP32-S3-Touch-LCD-7') },
    [['CANH', 'CAN H'], ['CANL', 'CAN L'], ['J8G', 'J8 GND'], ['J8S', 'J8 GPIO6']]),
  part('copro', T('Waveshare RP2350-CAN board', 'Waveshare-RP2350-CAN-Platine'), T('Coprocessor', 'Koprozessor'),
    T('Coprocessor: drives the outputs, reads the sensors, sets the supply.', 'Koprozessor: treibt die Ausgänge, liest die Sensoren, stellt die Versorgung.'),
    { locked: true }, { column: 'none', order: 1 }, [],
    { option: 'connection', names: { osoyoo: T('Osoyoo screw-terminal breakout board', 'Osoyoo-Schraubklemmen-Breakout-Platine') } }),
  part('usb', T('2 USB-C cables with data lines, USB-C charger of at least 2 A', '2 USB-C-Kabel mit Datenleitungen, USB-C-Ladegerät mit mindestens 2 A'), T('USB-C cables', 'USB-C-Kabel'),
    T('One 5 V supply per board. The panel on the charger with a short cable.', 'Je Platine eine 5-V-Versorgung. Das Panel am Ladegerät mit kurzem Kabel.'),
    { locked: true }, none),
  part('wire', T('Hook-up wire, multimeter', 'Schaltdraht, Multimeter'), T('Hook-up wire', 'Schaltdraht'),
    T('A twisted pair for CAN, jumper leads for the rest.', 'Ein verdrilltes Paar für CAN, Steckbrücken für den Rest.'),
    { locked: true }, { column: 'none', order: 2 }),
  part('servo', T('Servo (yours)', 'Servo (eigenes)'), T('Servo'), T('Device under test.', 'Prüfling.'),
    { selectable: true }, { column: 'L', order: 4 },
    [['SIG', 'signal', 'Signal'], ['POS', '+'], ['NEG', '−']]),
  part('ssup', T('Servo supply (yours)', 'Servoversorgung (eigene)'), T('Servo supply', 'Servoversorgung'),
    T("Voltage as the servo's maker states.", 'Spannung nach Angabe des Servoherstellers.'),
    {}, { column: 'L', order: 8 }, [['POS', '+'], ['NEG', '−']]),
  part('esc', T('ESC and motor (yours)', 'ESC und Motor (eigene)'), T('ESC'),
    T('Device under test. ESC: electronic speed controller.', 'Prüfling. ESC: elektronischer Drehzahlsteller.'),
    { selectable: true }, { column: 'L', order: 5 },
    [['SIG', 'signal', 'Signal'], ['SG', 'signal ground', 'Signalmasse'], ['BEC', 'centre wire (BEC +)', 'Mittelader (BEC +)'],
      ['POS', '+'], ['NEG', '−'], ['A', 'phase A', 'Phase A'], ['B', 'phase B', 'Phase B'], ['C', 'phase C', 'Phase C']]),
  part('batt', T('ESC battery (yours)', 'ESC-Akku (eigener)'), T('ESC battery', 'ESC-Akku'), T('Source A of the ESC.', 'Quelle A des ESC.'),
    {}, { column: 'L', order: 9 }, [['POS', '+'], ['NEG', '−']]),
  part('pdmini', T('WeAct Studio PD Power Mini V1 Buck module', 'WeAct-Studio-PD-Power-Mini-V1-Buck-Modul'), T('PD mini'),
    T('Supply set from the bench: 1 to 20 V, 0.05 to 3 A (vendor values, not measured).', 'Vom Prüfstand gestellte Versorgung: 1 bis 20 V, 0,05 bis 3 A (Herstellerangaben, nicht gemessen).'),
    { selectable: true }, { column: 'L', order: 6, sub: T('UART through the T-plug', 'UART über den T-Stecker') },
    [['USB', 'USB-C in', 'USB-C Eingang'], ['OP', 'OUT +'], ['ON', 'OUT −']]),
  part('charger', T('USB-C PD charger', 'USB-C-PD-Ladegerät'), T('PD charger', 'PD-Ladegerät'),
    T('Feeds the PD mini. PD: USB Power Delivery.', 'Speist den PD mini. PD: USB Power Delivery.'),
    {}, { column: 'L', order: 7 }, [['USB', 'USB-C']]),
  part('ina3221', T('DAOKAI INA3221 board, 3 channels', 'DAOKAI-INA3221-Platine, 3 Kanäle'), T('INA3221'),
    T('Servo current, 0 to 26 V, up to 1.64 A per channel. I2C 0x40 with the A0 bridge to GND.', 'Servostrom, 0 bis 26 V, bis 1,64 A je Kanal. I2C 0x40 mit A0-Brücke nach GND.'),
    { selectable: true, notRun: true }, { column: 'R', order: 14, sub: T('header: A0 VS SCL SDA GND', 'Leiste: A0 VS SCL SDA GND') },
    [['A0', 'A0'], ['VS', 'VS'], ['SCL', 'SCL'], ['SDA', 'SDA'], ['GND', 'GND'], ['C1A', 'CH1, pad 1', 'CH1, Pad 1'], ['C1B', 'CH1, pad 2', 'CH1, Pad 2'], ['LP', '3 large pads', '3 große Pads']]),
  part('as5600', T('AS5600 board with diametric magnet', 'AS5600-Platine mit diametralem Magnet'), T('AS5600'),
    T('Angle of the servo horn, 12 bit. I2C 0x36, fixed.', 'Winkel des Servohorns, 12 Bit. I2C 0x36, fest.'),
    { selectable: true, notRun: true }, { column: 'R', order: 15 },
    [['VCC', 'VCC'], ['OUT', 'OUT'], ['GND', 'GND'], ['GPO', 'GPO', 'GPO', undefined, true], ['SDA', 'SDA'], ['SCL', 'SCL'], ['DIR', 'DIR']]),
  part('pull', T('2 resistors, 2.2 kΩ', '2 Widerstände, 2,2 kΩ'), T('2 × 2.2 kΩ', '2 × 2,2 kΩ'), T('Pull-ups of the sensor bus.', 'Pull-ups des Sensorbusses.'),
    {}, { column: 'R', order: 16, sub: T('one per bus line', 'je Busleitung einer') },
    [['SDA', 'R1, to SDA', 'R1, nach SDA'], ['SCL', 'R2, to SCL', 'R2, nach SCL'], ['V', 'R1 and R2, to 3V3', 'R1 und R2, nach 3V3']]),
  part('ina228', T('INA228 battery current sensor', 'INA228-Akkustromsensor'), T('INA228'),
    T('ESC supply voltage and current.', 'Versorgungsspannung und Strom des ESC.'),
    { selectable: true, notRun: true }, { column: 'R', order: 12 },
    [['V5', '5V'], ['G', 'G'], ['SDA', 'SDA'], ['SCL', 'SCL'], ['BAT', 'BAT+'], ['ESC', 'ESC+']],
    { option: 'ina228Board', names: {
      matek: T('MATEK I2C-INA-BM board', 'MATEK-I2C-INA-BM-Platine'),
      xt90: T('INA228 module with XT90 plugs (print: BIFRC V1.1)', 'INA228-Modul mit XT90-Steckern (Aufdruck: BIFRC V1.1)'),
    } }),
  part('sd', T('microSD card, FAT', 'microSD-Karte, FAT'), T('microSD card', 'microSD-Karte'),
    T("In the panel's slot. Tested card sizes: not stated.", 'Im Kartenschacht des Panels. Getestete Kartengrößen: nicht angegeben.'),
    { selectable: true }, { column: 'L', order: 2, sub: T("in the panel's slot, no wires", 'im Schacht des Panels, keine Drähte') }),
  part('estop', T('Emergency stop button, latching, 1 NC contact', 'Not-Halt-Taster, rastend, 1 Öffner (NC)'), T('Emergency stop', 'Not-Halt'),
    T('Its NC (normally closed) contact opens the heartbeat wire. It switches off no supply.', 'Sein Öffner (NC) unterbricht den Heartbeat-Draht. Er schaltet keine Versorgung ab.'),
    { selectable: true }, { column: 'L', order: 3, sub: T('NC contact', 'Öffner (NC)') },
    [['NC1', 'NC tab 1', 'NC-Fahne 1'], ['NC2', 'NC tab 2', 'NC-Fahne 2']]),
  part('sim', T('Motor simulator, self-built: 3 resistors, 470 Ω, 2 W', 'Motorsimulator, selbst gebaut: 3 Widerstände, 470 Ω, 2 W'), T('Motor simulator', 'Motorsimulator'),
    T("Stands in for the motor on the ESC's phase wires.", 'Ersetzt den Motor an den Phasenleitungen des ESC.'),
    { selectable: true }, { column: 'L', order: 10, sub: T('no ground wire', 'kein Massedraht') },
    [['A', 'corner A', 'Ecke A'], ['B', 'corner B', 'Ecke B'], ['C', 'corner C', 'Ecke C']]),
  part('tap', T('Phase tap: resistor 4.7 kΩ 0.25 W, zener diode 3.0 V', 'Phasenabgriff: Widerstand 4,7 kΩ 0,25 W, Z-Diode 3,0 V'), T('Phase tap', 'Phasenabgriff'),
    T("Reads pitch and length of the ESC's beeps on phase A.", 'Liest Tonhöhe und Länge der ESC-Töne an Phase A.'),
    { selectable: true, notRun: true }, { column: 'R', order: 13, sub: T('4.7 kΩ, 0.25 W', '4,7 kΩ, 0,25 W') },
    [['IN', 'resistor, end 1', 'Widerstand, Ende 1'], ['OUT', 'resistor, end 2', 'Widerstand, Ende 2']]),
  part('usbtap', T('USB-C tap with 3-wire lead, the T-plug of the PD mini', 'USB-C-Abgriff mit 3-adriger Leitung, der T-Stecker des PD mini'), T('T-plug', 'T-Stecker'),
    T('Sits between the USB-C PD charger and the PD mini. Brings GND, D+ and D− to the coprocessor.', 'Sitzt zwischen USB-C-PD-Ladegerät und PD mini. Führt GND, D+ und D− zum Koprozessor.'),
    { selectable: true, notRun: true }, { column: 'L', order: 11, sub: T('UART of the PD mini', 'UART des PD mini') },
    [['G', 'G'], ['DP', 'D+'], ['DM', 'D−'], ['V', 'V']]),
];

const PAD_NAMES = ['GP0', 'GP1', 'GND', 'GP2', 'GP3', 'GP4', 'GP5', 'GND', 'GP6', 'GP7', 'GP8', 'GP9', 'GND', 'GP10', 'GP11', 'GP12', 'GP13', 'GND', 'GP14', 'GP15',
  'GP16', 'GP17', 'GND', 'GP18', 'GP19', 'GP20', 'GP21', 'GND', 'GP22', 'RUN', 'GP26', 'GP27', 'GND', 'GP28', 'VREF', '3V3', '3V3_EN', 'GND', 'VSYS', 'VBUS'];
const OSOYOO: Record<number, string> = { 31: '26 A0', 32: '27 A1', 34: '28 A2', 35: 'ADC.REF', 37: '3V3.EN' };
const PADS: Pad[] = PAD_NAMES.map((name, i): Pad => {
  const gpio = /^GP\d+$/.test(name) ? Number(name.slice(2)) : null;
  const pad: Pad = { n: i + 1, name, gpio, breakoutLabel: OSOYOO[i + 1] ?? name, src: SRC };
  if (gpio !== null && gpio >= 8 && gpio <= 12) pad.reserved = T('CAN pins of the board.', 'CAN-Pins der Platine.');
  return pad;
});

const has = (id: string): Cond => ({ has: id });
const not = (c: Cond): Cond => ({ not: c });
const all = (...c: Cond[]): Cond => ({ all: c });
const opt = (key: OptionKey, is: string): Cond => ({ opt: key, is });
const lane = (kind: 'L' | 'R' | 'O', n: number): Route => ({ kind, lane: n });
const across = (left: number, right: number, channel: number): Route => ({ kind: 'X', left, right, channel });

function wire(
  n: number, a: string, b: string | Text, cls: SignalClass, route: Route, stage: Wire['stage'],
  when: Cond | null, signal: Text, note: Text | null, extra: Partial<Wire> = {},
): Wire {
  const stub = typeof b !== 'string';
  const w: Wire = {
    n, kind: stub ? 'stub' : 'wire', a, b: stub ? null : b, net: `n${n}`, cls, signal, note, when, stage, route, src: SRC, ...extra,
  };
  if (stub) w.to = b;
  return w;
}

const V33 = (en: string, de: string = en): Text => T(`${en}, 3.3 V`, `${de}, 3,3 V`);
const STATED = T('Pin from an owner statement of 2026-10-11.', 'Pin aus einer Angabe des Projektinhabers vom 2026-10-11.');
const SERVO_PD = all(has('servo'), has('pdmini'));
const SERVO_OWN = all(has('servo'), not(has('pdmini')));
const ESC_BATT = all(has('esc'), opt('escPower', 'battery'));
const ESC_PD = all(has('esc'), opt('escPower', 'pd'));

const WIRES: Wire[] = [
  wire(1, 'panel.CANH', 'can.H', 'canh', lane('L', 0), 1, null, T('CAN H'), T('Twist CAN H and CAN L together. Each branch under 30 cm.', 'CAN H und CAN L verdrillen. Jeder Abzweig unter 30 cm.')),
  wire(2, 'panel.CANL', 'can.L', 'canl', lane('L', 1), 1, null, T('CAN L'), T('120 Ω termination on at both ends.', '120-Ω-Abschluss an beiden Enden eingeschaltet.')),
  wire(3, 'panel.J8G', 'pad.3', 'gnd', lane('L', 2), 1, null, T('GND'), T('Common ground.', 'Gemeinsame Masse.')),
  wire(4, 'panel.J8S', 'pad.5', 'hb', lane('L', 3), 1, not(has('estop')), V33('heartbeat', 'Heartbeat'), T('The interlock. The coprocessor refuses to arm without it.', 'Die Verriegelung. Ohne ihn schaltet der Koprozessor nicht scharf.'), { pin: 'fixed' }),
  wire(5, 'panel.J8S', 'estop.NC1', 'hb', lane('L', 3), 5, has('estop'), V33('heartbeat', 'Heartbeat'), T('In place of wire 4. Use the NC pair, not NO.', 'Anstelle von Draht 4. Das NC-Paar verwenden, nicht NO.')),
  wire(6, 'estop.NC2', 'pad.5', 'hb', lane('L', 4), 5, has('estop'), V33('heartbeat', 'Heartbeat'), T('Optional: 10 kΩ from GP3 to GND on a long lead.', 'Optional: 10 kΩ von GP3 nach GND bei langer Leitung.'), { pin: 'fixed' }),
  wire(7, 'pad.4', 'servo.SIG', 'out', lane('L', 5), 1, has('servo'), V33('SERVO PWM'), T('Pin proposed by this page. Bind it under SETUP → OUTPUTS.', 'Pin ist ein Vorschlag dieser Seite. Unter SETUP → OUTPUTS binden.'), { pin: 'proposal' }),
  wire(8, 'pad.8', 'servo.NEG', 'gnd', lane('L', 6), 1, has('servo'), T('GND'), T('Servo − also goes to coprocessor GND.', 'Servo − geht auch an GND des Koprozessors.')),
  wire(9, 'ssup.POS', 'servo.POS', 'power', lane('O', 0), 1, all(SERVO_OWN, not(has('ina3221'))), T('servo power', 'Servoversorgung'), null),
  wire(10, 'ssup.NEG', 'servo.NEG', 'power', lane('O', 1), 1, SERVO_OWN, T('servo power', 'Servoversorgung'), null),
  wire(11, 'pad.7', 'esc.SIG', 'out', lane('L', 7), 1, has('esc'), V33('MOTOR PWM or DShot', 'MOTOR PWM oder DShot'), T("Pin from the build guide's example.", 'Pin aus dem Beispiel der Bauanleitung.'), { pin: 'example' }),
  wire(12, 'pad.13', 'esc.SG', 'gnd', lane('L', 8), 1, has('esc'), T('GND'), T("Ground wire of the ESC's signal lead.", 'Massedraht des ESC-Signalkabels.')),
  wire(13, 'esc.BEC', T('nothing', 'nichts'), 'nc', { kind: 'S' }, 1, has('esc'), T('none', 'keines'), T('Connected nowhere.', 'Wird nirgends angeschlossen.')),
  wire(14, 'batt.POS', 'esc.POS', 'power', lane('O', 2), 1, all(ESC_BATT, not(has('ina228'))), T('ESC power', 'ESC-Versorgung'), null),
  wire(15, 'batt.NEG', 'esc.NEG', 'power', lane('O', 3), 1, ESC_BATT, T('ESC power', 'ESC-Versorgung'), null),
  wire(16, 'pad.10', 'usbtap.DM', 'uart', lane('L', 9), 2, has('pdmini'), V33('UART TX'), T(`Named from the coprocessor: its TX goes to D− of the T-plug. ${STATED.en} Optional 1 kΩ in series.`, `Vom Koprozessor aus benannt: sein TX geht an D− des T-Steckers. ${STATED.de} Optional 1 kΩ in Reihe.`), { pin: 'proposal', src: OWNER }),
  wire(17, 'usbtap.DP', 'pad.9', 'uart', lane('L', 10), 2, has('pdmini'), V33('UART RX'), T(`${STATED.en} Optional 1 kΩ in series.`, `${STATED.de} Optional 1 kΩ in Reihe.`), { pin: 'proposal', src: OWNER }),
  wire(18, 'pad.18', 'usbtap.G', 'gnd', lane('L', 11), 2, has('pdmini'), T('GND'), null, { src: OWNER }),
  wire(19, 'charger.USB', 'pdmini.USB', 'power', lane('O', 4), 2, has('pdmini'), T('PD in', 'PD-Eingang'), T('Through the T-plug. Output is at most input less 0.5 V.', 'Über den T-Stecker. Ausgang höchstens Eingang minus 0,5 V.')),
  wire(20, 'pdmini.OP', 'servo.POS', 'power', lane('O', 0), 2, all(SERVO_PD, not(has('ina3221'))), T('servo power, 1 to 20 V', 'Servoversorgung, 1 bis 20 V'), null),
  wire(21, 'pdmini.ON', 'servo.NEG', 'power', lane('O', 1), 2, SERVO_PD, T('servo power', 'Servoversorgung'), null),
  wire(22, 'pdmini.OP', 'esc.POS', 'power', lane('O', 2), 2, all(ESC_PD, not(has('ina228'))), T('ESC power, 1 to 20 V', 'ESC-Versorgung, 1 bis 20 V'), T('Up to 3 A.', 'Bis 3 A.')),
  wire(23, 'pdmini.ON', 'esc.NEG', 'power', lane('O', 3), 2, ESC_PD, T('ESC power', 'ESC-Versorgung'), null),
  wire(24, 'pad.36', 'ina3221.VS', 'logic', lane('R', 5), 3, has('ina3221'), V33('supply', 'Versorgung'), null),
  wire(25, 'pad.23', 'ina3221.GND', 'gnd', lane('R', 6), 3, has('ina3221'), T('GND'), null),
  wire(26, 'pad.21', 'ina3221.SDA', 'sda', lane('R', 7), 3, has('ina3221'), V33('I2C SDA'), T('One bus for all sensors, 400 kHz. No output on GP16 or GP17.', 'Ein Bus für alle Sensoren, 400 kHz. Kein Ausgang auf GP16 oder GP17.'), { pin: 'default' }),
  wire(27, 'pad.22', 'ina3221.SCL', 'scl', lane('R', 8), 3, has('ina3221'), V33('I2C SCL'), null, { pin: 'default' }),
  wire(28, 'pdmini.OP', 'ina3221.C1A', 'power', across(12, 16, 0), 3, all(SERVO_PD, has('ina3221')), T('servo power, 1 to 20 V', 'Servoversorgung, 1 bis 20 V'), T("The shunt sits in the servo's plus wire.", 'Der Shunt sitzt in der Plusleitung des Servos.')),
  wire(29, 'ssup.POS', 'ina3221.C1A', 'power', across(12, 16, 0), 3, all(SERVO_OWN, has('ina3221')), T('servo power, up to 26 V', 'Servoversorgung, bis 26 V'), T("The shunt sits in the servo's plus wire.", 'Der Shunt sitzt in der Plusleitung des Servos.')),
  wire(30, 'servo.POS', 'ina3221.C1B', 'power', across(13, 17, 1), 3, all(has('servo'), has('ina3221')), T('servo power', 'Servoversorgung'), T('If the current reads negative, swap the two wires on CH1.', 'Liest der Strom negativ, die zwei Drähte an CH1 tauschen.')),
  wire(31, 'ina3221.A0', T('GND, solder bridge on the board', 'GND, Lötbrücke auf der Platine'), 'nc', { kind: 'S' }, 3, has('ina3221'), T('address strap', 'Adressbrücke'), T('Address 0x40. No wire.', 'Adresse 0x40. Kein Draht.')),
  wire(32, 'ina3221.LP', T('nothing', 'nichts'), 'nc', { kind: 'S' }, 3, has('ina3221'), T('none', 'keines'), T('Not connected. Silkscreen, schematic and listing picture disagree.', 'Nicht angeschlossen. Aufdruck, Schaltplan und Angebotsbild widersprechen sich.')),
  wire(33, 'pad.36', 'as5600.VCC', 'logic', lane('R', 9), 4, has('as5600'), V33('supply', 'Versorgung'), T('This board on 3.3 V: not tested.', 'Diese Platine an 3,3 V: nicht getestet.')),
  wire(34, 'pad.23', 'as5600.GND', 'gnd', lane('R', 10), 4, has('as5600'), T('GND'), null),
  wire(35, 'pad.21', 'as5600.SDA', 'sda', lane('R', 11), 4, has('as5600'), V33('I2C SDA'), T('Address 0x36, fixed.', 'Adresse 0x36, fest.'), { pin: 'default' }),
  wire(36, 'pad.22', 'as5600.SCL', 'scl', lane('R', 12), 4, has('as5600'), V33('I2C SCL'), null, { pin: 'default' }),
  wire(37, 'as5600.DIR', T('GND or VCC, on the board', 'GND oder VCC, auf der Platine'), 'nc', { kind: 'S' }, 4, has('as5600'), T('direction strap', 'Richtungsbrücke'), T('Never leave DIR open. No wire to the coprocessor.', 'DIR nie offen lassen. Kein Draht zum Koprozessor.')),
  wire(38, 'pad.21', 'pull.SDA', 'sda', lane('R', 13), 4, has('pull'), T('I2C SDA'), T('2.2 kΩ from SDA to 3V3.', '2,2 kΩ von SDA nach 3V3.'), { pin: 'default' }),
  wire(39, 'pad.22', 'pull.SCL', 'scl', lane('R', 14), 4, has('pull'), T('I2C SCL'), T('2.2 kΩ from SCL to 3V3.', '2,2 kΩ von SCL nach 3V3.'), { pin: 'default' }),
  wire(40, 'pad.36', 'pull.V', 'logic', lane('R', 15), 4, has('pull'), V33('supply', 'Versorgung'), null),
  wire(41, 'pad.40', 'ina228.V5', 'logic', lane('R', 0), 5, has('ina228'), T('supply, 5 V', 'Versorgung, 5 V'), T('The MATEK board needs 4 to 9 V.', 'Die MATEK-Platine braucht 4 bis 9 V.')),
  wire(42, 'pad.38', 'ina228.G', 'gnd', lane('R', 1), 5, has('ina228'), T('GND'), null),
  wire(43, 'pad.21', 'ina228.SDA', 'sda', lane('R', 2), 5, has('ina228'), V33('I2C SDA'), null, { pin: 'default' }),
  wire(44, 'pad.22', 'ina228.SCL', 'scl', lane('R', 3), 5, has('ina228'), V33('I2C SCL'), null, { pin: 'default' }),
  wire(45, 'batt.POS', 'ina228.BAT', 'power', across(14, 18, 2), 5, all(ESC_BATT, has('ina228')), T('ESC power', 'ESC-Versorgung'), T("The sensor sits in the ESC's plus line.", 'Der Sensor sitzt in der Plusleitung des ESC.')),
  wire(46, 'pdmini.OP', 'ina228.BAT', 'power', across(14, 18, 2), 5, all(ESC_PD, has('ina228')), T('ESC power, 1 to 20 V', 'ESC-Versorgung, 1 bis 20 V'), T("The sensor sits in the ESC's plus line.", 'Der Sensor sitzt in der Plusleitung des ESC.')),
  wire(47, 'esc.POS', 'ina228.ESC', 'power', across(15, 19, 3), 5, all(has('esc'), has('ina228')), T('ESC power', 'ESC-Versorgung'), null),
  wire(48, 'esc.A', 'sim.A', 'phase', lane('O', 5), 5, all(has('esc'), has('sim')), T('motor phase', 'Motorphase'), T('Any phase wire to any corner. The tap sits on corner A.', 'Jede Phasenleitung an jede Ecke. Der Abgriff sitzt an Ecke A.')),
  wire(49, 'esc.B', 'sim.B', 'phase', lane('O', 6), 5, all(has('esc'), has('sim')), T('motor phase', 'Motorphase'), null),
  wire(50, 'esc.C', 'sim.C', 'phase', lane('O', 7), 5, all(has('esc'), has('sim')), T('motor phase', 'Motorphase'), null),
  wire(51, 'sim.A', 'tap.IN', 'phase', across(16, 20, 4), 5, all(has('sim'), has('tap')), T('phase A, up to 25.2 V', 'Phase A, bis 25,2 V'), T('The resistor sits at the corner.', 'Der Widerstand sitzt an der Ecke.')),
  wire(52, 'pad.29', 'tap.OUT', 'phase', lane('R', 4), 5, has('tap'), T('tap signal, 3.22 V or less', 'Abgriffsignal, höchstens 3,22 V'), T('Under 30 cm. No output on GP22 while the tap is on.', 'Unter 30 cm. Kein Ausgang auf GP22, solange der Abgriff eingeschaltet ist.'), { pin: 'default' }),
  wire(53, 'pad.29', 'pad.28', 'phase', { kind: 'Z' }, 5, has('tap'), T('zener clamp', 'Z-Dioden-Klemme'), T('3.0 V zener diode across the two adjacent pads, band (cathode) to GP22.', '3,0-V-Z-Diode über die zwei benachbarten Pads, Ring (Kathode) an GP22.'), { kind: 'component', netB: 'gnd' }),
  wire(54, 'as5600.GPO', T('GND, on the board', 'GND, auf der Platine'), 'gnd', { kind: 'S' }, 4, has('as5600'), T('GPO strap', 'GPO-Brücke'), T('docs/Building.md names the pin PGO. No wire to the coprocessor.', 'docs/Building.md nennt den Pin PGO. Kein Draht zum Koprozessor.'), { net: 'gnd' }),
];

function fn(id: string, group: Fn['group'], name: Text, needs: string[], notRun = false, forces?: Partial<Options>): Fn {
  const f: Fn = { id, group, name, needs, hardware: notRun ? 'not-run' : 'run', src: SRC };
  if (forces) f.forces = forces;
  return f;
}

const FUNCTIONS: Fn[] = [
  fn('servo', 'servo', T('Drive a servo', 'Servo ansteuern'), ['servo']),
  fn('servotest', 'servo', T('Automatic servo test', 'Automatischer Servotest'), ['servo', 'pdmini']),
  fn('servocur', 'servo', T('Servo current from 1 ms samples', 'Servostrom aus 1-ms-Abtastwerten'), ['servo', 'ina3221'], true),
  fn('angle', 'servo', T('Angle of the servo horn', 'Winkel des Servohorns'), ['servo', 'as5600'], true),
  fn('esc', 'esc', T('Drive an ESC and motor', 'ESC und Motor ansteuern'), ['esc']),
  fn('escpow', 'esc', T('ESC voltage, current, energy', 'Spannung, Strom, Energie des ESC'), ['esc', 'ina228'], true),
  fn('stick', 'esc', T('ESC STICK programming', 'ESC-STICK-Programmierung'), ['esc', 'pdmini'], true, { escPower: 'pd' }),
  fn('tap', 'esc', T("Read the ESC's beeps (phase tap)", 'ESC-Töne lesen (Phasenabgriff)'), ['esc', 'sim', 'tap'], true),
  fn('supply', 'bench', T('Supply set from the bench, 1 to 20 V', 'Versorgung vom Prüfstand gestellt, 1 bis 20 V'), ['pdmini']),
  fn('logs', 'bench', T('Run logs on a card', 'Laufprotokolle auf Karte'), ['sd']),
  fn('estop', 'bench', T('Emergency stop button', 'Not-Halt-Taster'), ['estop']),
];

const RULES: Rule[] = [
  { id: 'pd-for-esc', when: ESC_PD, adds: 'pdmini', because: T('ESC power B', 'ESC-Versorgung B'), src: SRC },
  { id: 'pd-tplug', when: has('pdmini'), adds: 'usbtap', because: T('UART of the PD mini', 'UART des PD mini'), src: OWNER },
  { id: 'charger', when: has('pdmini'), adds: 'charger', because: T('PD mini'), src: SRC },
  { id: 'servo-supply', when: SERVO_OWN, adds: 'ssup', because: T('Servo'), src: SRC },
  { id: 'battery', when: ESC_BATT, adds: 'batt', because: T('ESC'), src: SRC },
  { id: 'pull-ups', when: all({ any: [has('ina228'), has('as5600')] }, not(has('ina3221'))), adds: 'pull', because: T('sensor bus without the INA3221', 'Sensorbus ohne INA3221'), src: SRC },
];

function check(id: string, kind: Check['kind'], when: Cond | null, en: string, de: string): Check {
  return { id, kind, text: T(en, de), when, src: SRC };
}

const CHECKS: Check[] = [
  check('can-ohm', 'measure', null, 'CAN H to CAN L, everything unpowered: about 60 Ω. A reading of 120 Ω means one terminator is missing.', 'CAN H gegen CAN L, alles stromlos: etwa 60 Ω. 120 Ω heißt: Ein Abschluss fehlt.'),
  check('panel-print', 'unknown', null, "Names of the panel's CAN terminals and the pin order of J8: not stated in the docs. Read the print on the board.", 'Namen der CAN-Klemmen des Panels und Pinfolge von J8: in den Dokumenten nicht angegeben. Den Aufdruck der Platine lesen.'),
  check('same-release', 'note', null, 'Both boards run the same release. With different releases the bench does not arm.', 'Beide Platinen laufen mit demselben Release. Bei verschiedenen Releases schaltet der Prüfstand nicht scharf.'),
  check('servo-pin', 'note', has('servo'), "GP2 for the servo is a proposal of this page. The build guide's example is GP0. GP0 and GP16 share a compare register, and GP16 carries the sensor bus. Whether the firmware refuses that pair: not stated.", 'GP2 für das Servo ist ein Vorschlag dieser Seite. Das Beispiel der Bauanleitung ist GP0. GP0 und GP16 teilen sich ein Vergleichsregister, und GP16 trägt den Sensorbus. Ob die Firmware dieses Paar ablehnt: nicht angegeben.'),
  check('pd-pins', 'note', has('pdmini'), 'GP7 (PD mini TX) and GP6 (PD mini RX) come from an owner statement of 2026-10-11. The firmware default is −1, not wired. Set both under SETUP → INTERFACES.', 'GP7 (PD mini TX) und GP6 (PD mini RX) stammen aus einer Angabe des Projektinhabers vom 2026-10-11. Die Voreinstellung der Firmware ist −1, nicht verdrahtet. Beide unter SETUP → INTERFACES einstellen.'),
  check('pd-shared', 'unknown', all(has('servo'), ESC_PD), 'Servo and ESC on the one PD mini output at the same time: not stated in the build guide.', 'Servo und ESC gleichzeitig an dem einen Ausgang des PD mini: in der Bauanleitung nicht angegeben.'),
  check('ina3221-a0', 'note', has('ina3221'), 'INA3221 as delivered: all A0 bridges open, the address changes between 0x40 and 0x41. Set the A0 bridge to GND.', 'INA3221 im Lieferzustand: alle A0-Brücken offen, die Adresse wechselt zwischen 0x40 und 0x41. Die A0-Brücke nach GND setzen.'),
  check('matek-rail', 'measure', all(has('ina228'), opt('ina228Board', 'matek')), 'MATEK board: the pull-up rail is not known. With the board powered and the coprocessor not connected, SDA and SCL read 3.3 V or less to GND.', 'MATEK-Platine: Die Pull-up-Schiene ist nicht bekannt. Platine versorgt, Koprozessor noch nicht verbunden: SDA und SCL liegen bei höchstens 3,3 V gegen GND.'),
  check('ina228-xt90', 'note', all(has('ina228'), opt('ina228Board', 'xt90')), 'INA228 module with XT90 plugs: wired as the MATEK I2C-INA-BM board, with supply, GND, SDA and SCL on the sensor bus and the same I2C address. This comes from an owner statement of 2026-10-11.', 'INA228-Modul mit XT90-Steckern: verdrahtet wie die MATEK-I2C-INA-BM-Platine, mit Versorgung, GND, SDA und SCL am Sensorbus und derselben I2C-Adresse. Das stammt aus einer Angabe des Projektinhabers vom 2026-10-11.'),
  check('ina228-xt90-pads', 'unknown', all(has('ina228'), opt('ina228Board', 'xt90')), 'INA228 module with XT90 plugs: the pad names printed on the board are not confirmed. The header print reads L A V G 5. The drawing uses the pad names of the MATEK board.', 'INA228-Modul mit XT90-Steckern: Die auf der Platine gedruckten Padnamen sind nicht bestätigt. Der Aufdruck der Leiste lautet L A V G 5. Die Zeichnung verwendet die Padnamen der MATEK-Platine.'),
  check('as5600-board', 'unknown', has('as5600'), 'AS5600 board: pull-ups on the board are not known. The print of the pin GPO, named PGO in Building.md, is not confirmed. It is tied to GND.', 'AS5600-Platine: Die Pull-ups auf der Platine sind nicht bekannt. Der Aufdruck des Pins GPO, in Building.md PGO genannt, ist nicht bestätigt. Er liegt an GND.'),
  check('tap-ground', 'measure', has('tap'), 'Phase tap: everything unpowered, ESC power negative to coprocessor GND reads near 0 Ω. With an opto-isolated ESC input the tap reads nothing.', 'Phasenabgriff: alles stromlos, Minus der ESC-Versorgung gegen GND des Koprozessors liest nahe 0 Ω. Bei einem ESC-Eingang mit Optokoppler liest der Abgriff nichts.'),
  check('usbtap-v', 'note', has('usbtap'), 'T-plug: the V pad carries no wire. Without the PD mini the T-plug has no wire.', 'T-Stecker: Das Pad V bekommt keinen Draht. Ohne den PD mini hat der T-Stecker keinen Draht.'),
  check('sim-ground', 'note', has('sim'), "No ground wire from the simulator to the coprocessor. It runs parallel to the ESC's own ground and can carry motor current.", 'Kein Massedraht vom Simulator zum Koprozessor. Er liegt parallel zur Masse des ESC und kann Motorstrom führen.'),
  check('estop-tabs', 'unknown', has('estop'), 'Emergency stop: the tab labels are not confirmed. The NC pair reads near 0 Ω while the button is released.', 'Not-Halt: Die Beschriftung der Lötfahnen ist nicht bestätigt. Das NC-Paar liest nahe 0 Ω, solange der Taster nicht gedrückt ist.'),
  check('osoyoo-labels', 'unknown', opt('connection', 'osoyoo'), 'Osoyoo breakout board: the terminal labels are read from one photo and partly legible. The board is not in the build guide and has not run on hardware.', 'Osoyoo-Breakout-Platine: Die Klemmenbeschriftung stammt von einem Foto und ist teilweise lesbar. Die Platine steht nicht in der Bauanleitung und ist nicht auf Hardware gelaufen.'),
  check('osoyoo-rows', 'note', opt('connection', 'osoyoo'), 'Use the screw terminals only. What the V pins of the G V S rows carry is not stated. Plug no servo lead and no ESC lead there.', 'Nur die Schraubklemmen verwenden. Was die V-Pins der G-V-S-Reihen führen, ist nicht angegeben. Dort kein Servokabel und kein ESC-Kabel stecken.'),
];

const STAGES: Stage[] = ([
  [1, 'Stage 1: panel and coprocessor', 'Stufe 1: Panel und Koprozessor'],
  [2, 'Stage 2: PD mini, the supply set from the bench', 'Stufe 2: PD mini, die vom Prüfstand gesteuerte Versorgung'],
  [3, 'Stage 3: INA3221, servo current from 1 ms samples', 'Stufe 3: INA3221, Servostrom aus 1-ms-Abtastwerten'],
  [4, "Stage 4: AS5600, the angle of the servo's horn", 'Stufe 4: AS5600, der Winkel des Servohorns'],
  [5, 'Stage 5: the remaining parts', 'Stufe 5: die übrigen Teile'],
] as const).map(([n, en, de]): Stage => ({ n, title: T(en, de), src: SRC }));

const SAFETY: Array<[string, string]> = [
  ['<b>No propeller.</b> The motor screwed down, or the motor simulator (stage 5) in place of the motor.', '<b>Kein Propeller.</b> Motor festschrauben oder statt des Motors den Motorsimulator (Stufe 5) anschließen.'],
  ["<b>ESC power from two sources only:</b> A, the ESC's own battery, or B, the PD mini output within its limits: 1 to 20 V, up to 3 A, and at most its input voltage less 0.5 V. A set point over the input puts the module into ERR until it is power cycled.", '<b>Versorgung des ESC nur aus zwei Quellen:</b> A, dem eigenen Akku des ESC, oder B, dem Ausgang des PD mini innerhalb seiner Grenzen: 1 bis 20 V, bis 3 A und höchstens Eingangsspannung minus 0,5 V. Ein Sollwert über der Eingangsspannung versetzt das Modul in den Zustand ERR, bis es aus- und wieder eingeschaltet wird.'],
  ['<b>The panel and the coprocessor never power the ESC.</b> Keep their 5 V and 3.3 V pins away from the ESC.', '<b>Panel und Koprozessor versorgen den ESC nie.</b> Ihre 5-V- und 3,3-V-Pins bleiben vom ESC getrennt.'],
  ['<b>ESC lead: only signal and ground</b> go to the coprocessor. The centre wire (BEC +) is connected nowhere.', '<b>ESC-Kabel: nur Signal und Masse</b> zum Koprozessor. Die Mittelader (BEC +) wird nirgends angeschlossen.'],
  ['<b>STOP</b> disarms, switches the PD mini output off, and latches until the next ARM. Arming is a 2 s hold on ARM; disarming is a press; leaving a bench screen disarms.', '<b>STOP</b> entschärft, schaltet den Ausgang des PD mini ab und rastet bis zum nächsten ARM ein. Scharf schalten: ARM 2 s halten. Entschärfen: kurz tippen. Auch das Verlassen einer Prüfstandsseite entschärft.'],
  ['<b>The heartbeat wire is the interlock.</b> The monostable that should back it in hardware is on no board yet, so the firmware at both ends is the whole interlock. A coprocessor that hangs while armed is not stopped by anything.', '<b>Der Heartbeat-Draht ist die Verriegelung.</b> Das Monoflop, das ihn in Hardware absichern soll, sitzt noch auf keiner Platine; die Verriegelung ist allein die Firmware beider Platinen. Hängt der Koprozessor im scharfen Zustand, stoppt ihn nichts.'],
  ['An armed bench drives every bound pin. A pin bound as SERVO PWM rests at 1500 µs, which an ESC can read as about half throttle. Bind an ESC as MOTOR PWM or DShot only.', 'Ein scharfer Prüfstand treibt jeden gebundenen Pin. Ein als SERVO PWM gebundener Pin ruht bei 1500 µs, für einen ESC etwa Halbgas. Einen ESC deshalb nur als MOTOR PWM oder DShot binden.'],
];

const NOT_IN_RELEASE: Array<[string, 'main' | 'planned', string, string]> = [
  ['kst', 'main', 'KST and Chaservo servo programming port: on main, in no release.', 'Programmieranschluss für KST- und Chaservo-Servos: auf main, in keinem Release.'],
  ['knob', 'planned', 'Rotary knob: the docs disagree. The page follows the build guide: no knob is read on this panel.', 'Drehknopf: Die Dokumente widersprechen sich. Die Seite folgt der Bauanleitung: An diesem Panel wird kein Knopf gelesen.'],
  ['mt6701', 'planned', 'MT6701 magnetic encoder board: not read by 0.16.0.', 'MT6701-Magnetencoder-Platine: wird von 0.16.0 nicht gelesen.'],
  ['receivers', 'planned', 'Receiver buses (S.BUS, CRSF and others): no receiver is read.', 'Empfängerbusse (S.BUS, CRSF und andere): Es wird kein Empfänger gelesen.'],
  ['serialesc', 'planned', 'Serial ESC programming (BLHeli_S, AM32 and others): no protocol on a wire.', 'Serielle ESC-Programmierung (BLHeli_S, AM32 und andere): kein Protokoll auf einem Draht.'],
];

export const CATALOGUE: Catalogue = {
  release: '0.16.0',
  hashVersion: 1,
  parts: PARTS,
  pads: PADS,
  wires: WIRES,
  functions: FUNCTIONS,
  rules: RULES,
  checks: CHECKS,
  stages: STAGES,
  safety: SAFETY.map(([en, de]) => ({ text: T(en, de), src: SRC })),
  notInRelease: NOT_IN_RELEASE.map(([id, release, en, de]) => ({ id, release, text: T(en, de), src: SRC })),
  baseFunction: T('User interface, CAN link, ARM, STOP and the heartbeat interlock', 'Bedienoberfläche, CAN-Verbindung, ARM, STOP und Heartbeat-Verriegelung'),
  pinDefaults: [],
};

const OPTION_VALUES: { [K in OptionKey]: readonly string[] } = {
  connection: ['pads', 'osoyoo'],
  ina228Board: ['matek', 'xt90'],
  escPower: ['battery', 'pd'],
};

function holds(c: Cond | null, present: Set<string>, options: Options): boolean {
  if (c === null) return true;
  if ('has' in c) return present.has(c.has);
  if ('opt' in c) return options[c.opt] === c.is;
  if ('not' in c) return !holds(c.not, present, options);
  if ('all' in c) return c.all.every((x) => holds(x, present, options));
  return c.any.some((x) => holds(x, present, options));
}

export const resolve: Core['resolve'] = (catalogue: Catalogue, selection: Selection): Setup => {
  const ignored: string[] = [];
  const fnById = new Map(catalogue.functions.map((f) => [f.id, f]));
  const partById = new Map(catalogue.parts.map((p) => [p.id, p]));

  const options: Options = { ...DEFAULT_OPTIONS };
  const given = (selection.options ?? {}) as Record<string, unknown>;
  for (const key of Object.keys(OPTION_VALUES) as OptionKey[]) {
    const value = given[key];
    if (value === undefined) continue;
    if (typeof value === 'string' && OPTION_VALUES[key].includes(value)) (options as Record<string, string>)[key] = value;
    else ignored.push(String(value));
  }

  const chosen = new Set<string>();
  for (const id of selection.functions ?? []) {
    if (fnById.has(id)) chosen.add(id);
    else if (!ignored.includes(id)) ignored.push(id);
  }
  const owned = new Set<string>();
  for (const id of selection.owned ?? []) {
    if (partById.get(id)?.selectable) owned.add(id);
    else if (!ignored.includes(id)) ignored.push(id);
  }

  const reasons = new Map<string, Reason[]>();
  const add = (id: string, reason: Reason): void => {
    const list = reasons.get(id) ?? [];
    list.push(reason);
    reasons.set(id, list);
  };
  for (const p of catalogue.parts) if (p.locked) add(p.id, { kind: 'always' });
  const forcedBy: Partial<Record<OptionKey, string>> = {};
  for (const f of catalogue.functions) {
    if (!chosen.has(f.id)) continue;
    for (const id of f.needs) add(id, { kind: 'function', id: f.id });
    for (const key of Object.keys(f.forces ?? {}) as OptionKey[]) {
      (options as Record<string, string>)[key] = f.forces![key] as string;
      forcedBy[key] = f.id;
    }
  }
  for (const p of catalogue.parts) if (owned.has(p.id)) add(p.id, { kind: 'owned' });
  for (const rule of catalogue.rules) {
    if (!holds(rule.when, new Set(reasons.keys()), options)) continue;
    add(rule.adds, rule.id === 'pd-for-esc'
      ? { kind: 'option', key: 'escPower', value: 'pd' }
      : { kind: 'rule', id: rule.id, because: rule.because });
  }

  const present = new Set(reasons.keys());
  const parts: ResolvedPart[] = catalogue.parts.filter((p) => present.has(p.id)).map((p) => {
    const addedBy = reasons.get(p.id)!;
    return { part: p, addedBy, derived: addedBy.some((r) => r.kind !== 'owned'), ownedTick: owned.has(p.id) };
  });
  const functions: ResolvedFunction[] = catalogue.functions.map((f) => {
    const missing = f.needs.filter((id) => !present.has(id));
    const forcesHold = (Object.keys(f.forces ?? {}) as OptionKey[]).every((k) => options[k] === f.forces![k]);
    return { fn: f, chosen: chosen.has(f.id), enabled: missing.length === 0 && forcesHold, missing };
  });
  const visible: Record<OptionKey, boolean> = { connection: true, ina228Board: present.has('ina228'), escPower: present.has('esc') };
  const resolvedOptions: ResolvedOption[] = (Object.keys(OPTION_VALUES) as OptionKey[]).map((key) => ({
    key, value: options[key], visible: visible[key], forcedBy: forcedBy[key] ?? null,
  }));
  const checks = catalogue.checks.filter((c) => holds(c.when, present, options));

  return {
    parts,
    functions,
    options: resolvedOptions,
    wires: catalogue.wires.filter((w) => holds(w.when, present, options)),
    checks,
    unknown: checks.filter((c) => c.kind === 'unknown').map((c) => ({ id: c.id, text: c.text })),
    conflicts: [],
    ignored,
  };
};
