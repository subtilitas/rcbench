// The Osoyoo strip data and the coverage tool: the facts, texts and sources of
// the Osoyoo strip setups, and the reader of the lcov report. The static
// checks and their broken copies are in catalogue.test.ts.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import type { Catalogue, Src } from '../../contract.ts';
import { CATALOGUE } from '../catalogue.ts';
import { DOC_URL, checkSrc } from '../tools/check.ts';
import { BEGIN, END, MEASURED, figure, parseLcov, percent, replaceFigure } from '../tools/coverage.ts';

// --- the coverage tool ------------------------------------------------------

test('coverage: the lcov report is read per file', () => {
  const report = ['TN:', 'SF:cond.ts', 'DA:1,1', 'LH:51', 'LF:51', 'end_of_record',
    'SF:/work/core/resolve.ts', 'LH:183', 'LF:184', 'end_of_record', 'SF:open.ts', 'LF:3'].join('\n');
  assert.deepEqual(parseLcov(report, '/work/core'), [
    { file: 'cond.ts', found: 51, hit: 51 },
    { file: 'resolve.ts', found: 184, hit: 183 },
  ]);
  assert.deepEqual(parseLcov('', '/work/core'), []);
});

test('coverage: the percentage rounds down, so 100.0 means every line', () => {
  assert.equal(percent(184, 184), '100.0');
  assert.equal(percent(1999, 2000), '99.9');
  assert.equal(percent(19999, 20000), '99.9');
  assert.equal(percent(1, 3), '33.3');
  assert.equal(percent(0, 10), '0.0');
  assert.equal(percent(0, 0), '0.0');
});

test('coverage: the figure names each measured file and fails without its record', () => {
  const block = figure([
    { file: 'resolve.ts', found: 184, hit: 183 },
    { file: 'cond.ts', found: 51, hit: 51 },
    { file: 'groups.ts', found: 200, hit: 199 },
    { file: 'sim.ts', found: 120, hit: 120 },
    { file: 'catalogue.ts', found: 9, hit: 9 },
  ]);
  assert.ok(block.startsWith(BEGIN) && block.endsWith(END));
  assert.match(block, /\| `resolve\.ts` \| 183 \| 184 \| 99\.4 % \|/);
  assert.match(block, /\| `cond\.ts` \| 51 \| 51 \| 100\.0 % \|/);
  assert.match(block, /\| `groups\.ts` \| 199 \| 200 \| 99\.5 % \|/);
  assert.doesNotMatch(block, /catalogue/);
  assert.throws(() => figure([{ file: 'cond.ts', found: 51, hit: 51 }]), /no record for resolve\.ts/);
  assert.deepEqual(MEASURED, ['resolve.ts', 'cond.ts', 'groups.ts', 'sim.ts']);
});

test('coverage: the block between the markers is replaced, and only there', () => {
  const readme = `# Title\n\n${BEGIN}\nold\n${END}\n\nRest.\n`;
  const next = replaceFigure(readme, `${BEGIN}\nnew\n${END}`);
  assert.equal(next, `# Title\n\n${BEGIN}\nnew\n${END}\n\nRest.\n`);
  assert.equal(replaceFigure(next ?? '', `${BEGIN}\nnew\n${END}`), next);
  assert.equal(replaceFigure('# Title\n', 'x'), null);
  assert.equal(replaceFigure(`${END}\n${BEGIN}\n`, 'x'), null);
  assert.equal(replaceFigure(`${BEGIN}\n`, 'x'), null);
});

test('the Osoyoo strip: no current is shown for both voltages, the DC input names its upper bound, each fact has its source', () => {
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  assert.equal(strip.amps, null);
  const cons = strip.cons.map((c) => c.en).join(' ');
  assert.match(cons, /7 to 12 V/);
  assert.match(cons, /16 V/);
  assert.match(cons, /The supply is in the parts list as a modelled part\./);
  assert.doesNotMatch(cons, /not in the parts list/);
  assert.doesNotMatch(strip.pros.map((c) => c.en).join(' '), /no further part/);
  const check = (id: string) => CATALOGUE.checks.find((c) => c.id === id)!;
  assert.equal(check('strip-converter').src.kind, 'doc');
  assert.match(check('strip-converter').text.en, /^((?!rates the board|printed 16 V).)*$/);
  assert.match(check('strip-print').text.en, /7 to 16 V/);
  assert.doesNotMatch(check('strip-input').text.en, /printed|print /);
  assert.doesNotMatch(check('strip-parts').text.en, /datasheet|uA/);
  assert.match(check('strip-load').text.en, /1 uA/);
  assert.notDeepEqual(check('strip-input').src, check('strip-converter').src);
  assert.notDeepEqual(check('strip-print').src, check('strip-input').src);
});

test('the Osoyoo strip: one DC input, no unresolved 3V3 claim, the plug claim excludes the current sensor, one source per check', () => {
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  const check = (id: string) => CATALOGUE.checks.find((c) => c.id === id)!;
  // Exactly one of the jack, the VIN terminal and the header H4: the check and the power source say so in both languages.
  assert.match(check('strip-dc').text.en, /exactly 1 supply: at the DC jack, at the VIN terminal or at the 2-pin header H4 \(PWRIN and VIN-\), never at 2 of them\./);
  assert.match(check('strip-dc').text.de, /Genau 1 Versorgung anschließen: an der DC-Buchse, an der VIN-Klemme oder am 2-poligen Pfostenverbinder H4 \(PWRIN und VIN-\), nie an 2 davon\./);
  assert.match(strip.cons.map((c) => c.en).join(' '), /at the DC jack, the VIN terminal or the header H4, exactly 1 of the three \(/);
  assert.match(strip.cons.map((c) => c.de).join(' '), /an der DC-Buchse, der VIN-Klemme oder dem Pfostenverbinder H4, an genau einem der drei Anschlüsse \(/);
  // The 3V3 claim carries the condition the unknown states.
  for (const lang of ['en', 'de'] as const) {
    const con = strip.cons.find((c) => /3V3/.test(c[lang]) && /3[.,]3 V/.test(c[lang]) && !/VSYS/.test(c[lang]))!;
    assert.match(con[lang], lang === 'en' ? /whether USB alone powers it is not resolved/ : /ob USB allein ihn versorgt, ist nicht geklärt/);
    assert.match(check('strip-jumper').text[lang], lang === 'en' ? /not resolved/ : /nicht geklärt/);
  }
  // The straight-in advantage names the same exclusions as the strip-plug check.
  assert.match(strip.pros[0]!.en, /Without the servo programmer and without the INA3221/);
  assert.match(strip.pros[0]!.de, /Ohne den Servo-Programmierer und ohne den Stromsensor INA3221/);
  // Each check of the strip holds the facts of one source.
  assert.equal(check('strip-jumper').src.kind, 'doc');
  assert.notDeepEqual(check('strip-dc').src, check('strip-jumper').src);
  assert.notDeepEqual(check('strip-measure').src, check('strip-converter').src);
  assert.equal(check('strip-measure').src.kind, 'doc');
  // The check gives the expected reading per jumper position, in both languages.
  assert.equal(check('strip-jumper').kind, 'note');
  for (const lang of ['en', 'de'] as const) {
    const m = check('strip-measure').text[lang];
    assert.match(m, /5 V nominal/);
    assert.match(m, /0 V/);
    assert.match(m, lang === 'en' ? /3\.3 V/ : /3,3 V/);
  }
  // The 3.3V limit of the Pico pin, the factory position and the shared supply without USB are stated.
  assert.match(check('strip-level').text.en, /3\.3 V I\/O only/);
  assert.match(check('strip-level').text.de, /nur 3,3-V-Ein- und Ausgänge/);
  assert.match(check('strip-regulator').text.en, /as delivered/);
  assert.match(check('strip-regulator').text.de, /Lieferzustand/);
  assert.match(strip.pros[2]!.en, /With USB connected.*without USB voltage.*also feeds VSYS/);
  assert.match(strip.pros[2]!.de, /Mit angeschlossenem USB.*ohne USB-Spannung.*auch VSYS/);
  assert.doesNotMatch(check('strip-dc').text.en, /damage/);
  assert.doesNotMatch(check('strip-jumper').text.en, /4\.8|7 to 1[26] V/);
  assert.doesNotMatch(check('strip-input').text.en, /7 to 40 V/);
  assert.match(check('strip-converter').text.en, /7 to 40 V/);
});

test('the Osoyoo strip: the kind and the source of every check', () => {
  const check = (id: string) => CATALOGUE.checks.find((c) => c.id === id)!;
  const SCHEMATIC = /^Schematic of the OSOYOO IO Breakout Board/;
  const PAGE = /product page, published 2024-12-02$/;
  const PHOTOS = /product photos$/;
  const table: Array<[string, string, RegExp | 'owner']> = [
    ['osoyoo-terminals', 'note', SCHEMATIC],
    ['strip-esc', 'note', 'owner'],
    ['strip-servo-plus', 'note', 'owner'],
    ['strip-jumper', 'note', SCHEMATIC],
    ['strip-dc', 'note', PAGE],
    ['strip-measure', 'measure', SCHEMATIC],
    ['strip-feed', 'note', SCHEMATIC],
    ['strip-converter', 'note', /XL1509/],
    ['strip-input', 'note', PAGE],
    ['strip-print', 'note', PHOTOS],
    ['strip-regulator', 'note', PAGE],
    ['strip-parts', 'note', SCHEMATIC],
    ['strip-load', 'note', /SN74HC04/],
    ['strip-level', 'note', PAGE],
    ['strip-plug', 'note', 'owner'],
    ['strip-prog', 'note', 'owner'],
    ['strip-supply', 'unknown', PAGE],
  ];
  for (const [id, kind, src] of table) {
    assert.equal(check(id).kind, kind, `${id} kind`);
    if (src === 'owner') assert.equal(check(id).src.kind, 'owner', `${id} src`);
    else {
      const s = check(id).src;
      assert.equal(s.kind, 'doc', `${id} src kind`);
      if (s.kind === 'doc') assert.match(s.title, src, `${id} src title`);
    }
  }
  // The urls: the schematic, the product page (page and photos), the 2 datasheets.
  const url = (id: string): string => { const s = check(id).src; return s.kind === 'doc' ? s.url : ''; };
  assert.match(url('strip-jumper'), /Schematic_OSOYOO-IO-Expansion-Board-for-Pico-series_2024-12-03\.pdf$/);
  assert.equal(url('strip-dc'), 'https://osoyoo.com/2024/12/02/osoyoo-breakout-board-for-pico-series/');
  assert.match(url('strip-converter'), /XL1509\.pdf$/);
  assert.match(url('strip-load'), /sn74hc04\.pdf$/);
});

test('the Osoyoo strip: the operative text of every check, in English and German', () => {
  const check = (id: string) => CATALOGUE.checks.find((c) => c.id === id)!;
  const rows: Array<[string, RegExp, RegExp]> = [
    ['strip-jumper', /Check it before a servo is plugged in/, /Vor dem Einstecken eines Servos prüfen/],
    ['strip-jumper', /Position 5V gives 5 V, but only while a DC supply is connected/, /Stellung 5V gibt 5 V, aber nur, solange eine DC-Versorgung angeschlossen/],
    ['strip-jumper', /without it the V pins carry 0 V/, /ohne sie führen die V-Pins 0 V/],
    ['strip-jumper', /Set the jumper to 5V for the servo/, /Den Jumper für das Servo auf 5V setzen/],
    ['strip-dc', /recommends 7 to 12 V/, /empfiehlt 7 bis 12 V/],
    ['strip-measure', /V pin of the strip at GP2 to the G pin/, /V-Pin der Leiste an GP2 gegen den G-Pin/],
    ['strip-measure', /5V without a DC supply or with the switch on OFF, 0 V/, /5V ohne DC-Versorgung oder mit Schalter auf OFF 0 V/],
    ['strip-feed', /Pin 1 is the net 3\.3V.*pin 2 is the net VCC, pin 3 is the net VOUT_5V/, /Pin 1 ist das Netz 3\.3V.*Pin 2 ist das Netz VCC, Pin 3 ist das Netz VOUT_5V/],
    ['strip-feed', /VCC is the V pin of all 40 strips/, /VCC ist der V-Pin aller 40 Leisten/],
    ['strip-feed', /input from VSYS of the Pico board/, /Eingang aus VSYS der Pico-Platine/],
    ['strip-feed', /jumper on 5V and no DC supply the V pins carry 0 V/, /Jumper auf 5V und ohne DC-Versorgung führen die V-Pins 0 V/],
    ['strip-feed', /joins VOUT_5V to the V pins when the jumper is on 3V3, and none joins VBUS/, /verbindet VOUT_5V mit den V-Pins, wenn der Jumper auf 3V3 steht, und keines verbindet VBUS/],
    ['strip-converter', /7 to 40 V.*4\.8 to 5\.2 V at 0\.2 to 2 A.*150 kHz \(127 to 173 kHz\).*limit 4 A.*82 % typical at 12 V in and 2 A/, /7 bis 40 V.*4,8 bis 5,2 V bei 0,2 bis 2 A.*150 kHz \(127 bis 173 kHz\).*grenze 4 A.*82 % bei 12 V Eingang und 2 A/],
    ['strip-input', /7 to 12 V and limits the input voltage to 7 to 18 V/, /7 bis 12 V und begrenzt die Eingangsspannung auf 7 bis 18 V/],
    ['strip-input', /5 V and 2 A, or 3\.3 V and 500 mA/, /5 V und 2 A oder 3,3 V und 500 mA/],
    ['strip-print', /reads 7 to 16 V.*100 uF 16 V/, /Aufdruck 7 bis 16 V.*100 uF 16 V/],
    ['strip-regulator', /ME6210A33M3G.*3\.3 V and 500 mA.*VSYS.*more than 0\.5 A/, /ME6210A33M3G.*3,3 V und 500 mA.*VSYS.*mehr als 0,5 A/],
    ['strip-regulator', /S pin of the 3\.3 V row/, /S-Pin der 3,3-V-Reihe/],
    ['strip-parts', /the 5 SN74HC04 hex inverters/, /Die 5 Sechsfach-Inverter SN74HC04/],
    ['strip-parts', /N-channel MOSFET.*only while no USB voltage is present/, /N-Kanal-MOSFET.*nur auf VSYS, solange keine USB-Spannung anliegt/],
    ['strip-load', /1 inverter input.*leakage up to 1 uA and 3 to 10 pF/, /1 Inverter-Eingang.*Leckstrom bis 1 uA und 3 bis 10 pF/],
    ['strip-level', /In position 5V the V pin carries 5 V/, /In Stellung 5V führt der V-Pin 5 V/],
    ['strip-supply', /2 A at 5 V and 500 mA at 3\.3 V are vendor values.*XL1509-5\.0/, /2 A bei 5 V und 500 mA bei 3,3 V sind Herstellerangaben.*XL1509-5\.0/],
    ['strip-supply', /stall current of the servo is not stated/, /Blockierstrom des Servos ist nicht angegeben/],
    ['strip-esc', /S and G pins of the strip at GP5 only/, /Pins S und G der Leiste an GP5/],
    ['strip-plug', /strip at GP2.*wire 7.*wire 61.*wire 8/, /Leiste an GP2.*Draht 7.*Draht 61.*Draht 8/],
    ['strip-prog', /to GP13 by wire 64/, /über Draht 64 an GP13/],
    ['osoyoo-terminals', /All V pins of both sides are 1 net/, /Alle V-Pins beider Seiten sind 1 Netz/],
    ['servo-bench-supply', /At the 5 V pin of the IOmcu board/, /Am 5-V-Pin der IOmcu-Platine/],
  ];
  for (const [id, en, de] of rows) {
    assert.match(check(id).text.en, en, `${id} en`);
    assert.match(check(id).text.de, de, `${id} de`);
  }
});

test('the Osoyoo strip: the fields and the texts of the power source and of wires 61 and 62', () => {
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  assert.deepEqual(strip.volts, { min: 3.3, max: 5, steps: [3.3, 5] });
  assert.deepEqual(strip.basis, { en: 'Vendor values, not measured.', de: 'Herstellerangaben, nicht gemessen.' });
  assert.equal(strip.feeds, 'servo');
  const join = (list: Array<{ en: string; de: string }>, lang: 'en' | 'de'): string => list.map((x) => x[lang]).join(' | ');
  const rows: Array<[string, 'pros' | 'cons' | 'unknowns', RegExp, RegExp]> = [
    ['strip-v', 'pros', /In position 5V the servo draws from the DC input through a converter rated 2 A/, /In Stellung 5V zieht das Servo aus dem DC-Eingang über einen Wandler mit 2 A/],
    ['strip-v', 'pros', /With USB connected this is not the supply of the logic; without USB voltage/, /Mit angeschlossenem USB ist das nicht die Versorgung der Logik; ohne USB-Spannung/],
    ['strip-v', 'pros', /Without the servo programmer and without the INA3221/, /Ohne den Servo-Programmierer und ohne den Stromsensor INA3221/],
    ['strip-v', 'cons', /As delivered the jumper cap sits on the 3V3 side.*so position 5V is a change/, /Im Lieferzustand sitzt die Jumperkappe auf der Seite 3V3.*Stellung 5V ist also eine Änderung/],
    ['strip-v', 'cons', /7 to 12 V.*print 7 to 16 V, vendor limit 18 V, capacitors rated 16 V.*USB does not feed it\. Without it the V pins carry 0 V/, /7 bis 12 V.*Aufdruck 7 bis 16 V, Herstellergrenze 18 V, Kondensatoren mit 16 V.*USB speist sie nicht\. Ohne sie führen die V-Pins 0 V/],
    ['strip-v', 'cons', /2 A at 5 V and 500 mA at 3\.3 V \(vendor values\)/, /2 A bei 5 V und 500 mA bei 3,3 V \(Herstellerwerte\)/],
    ['strip-v', 'cons', /In position 3V3 the servo draws from VSYS of the IOmcu board/, /In Stellung 3V3 zieht das Servo aus VSYS der IOmcu-Platine/],
    ['strip-v', 'cons', /In position 3V3 the servo gets 3\.3 V where the regulator is powered/, /In Stellung 3V3 bekommt das Servo 3,3 V, wo der Regler versorgt ist/],
    ['strip-v', 'unknowns', /2 A and 500 mA are vendor values/, /2 A und 500 mA sind Herstellerwerte/],
    ['strip-v', 'unknowns', /Whether position 3V3 works with USB alone/, /Ob Stellung 3V3 mit USB allein arbeitet/],
    ['strip-v', 'unknowns', /Stall current of the servo: not stated/, /Blockierstrom des Servos: nicht angegeben/],
    ['usb', 'cons', /Osoyoo breakout board with the jumper on 3V3/, /Osoyoo-Breakout-Platine mit dem Jumper auf 3V3/],
  ];
  for (const [id, field, en, de] of rows) {
    const src = CATALOGUE.powerSources.find((s) => s.id === id)!;
    assert.match(join(src[field], 'en'), en, `${id} ${field} en`);
    assert.match(join(src[field], 'de'), de, `${id} ${field} de`);
  }
  for (const n of [61, 62]) {
    const w = CATALOGUE.wires.find((x) => x.n === n)!;
    assert.match(w.signal.en, /servo power, 3\.3 V or 5 V by the jumper/, `wire ${n} en`);
    assert.match(w.signal.de, /Servoversorgung, 3,3 V oder 5 V je nach Jumper/, `wire ${n} de`);
    assert.match(w.note!.en, /jumper V Selector on 5V the servo draws from the DC input of the board through the XL1509 converter, with the jumper on 3V3 from VSYS/, `wire ${n} note en`);
    assert.match(w.note!.de, /Jumper V Selector auf 5V zieht das Servo aus dem DC-Eingang der Platine über den XL1509-Wandler, mit dem Jumper auf 3V3 aus VSYS/, `wire ${n} note de`);
  }
});

test('the Osoyoo strip: whole clauses of the readings, switch states, values and designators, in English and German', () => {
  const check = (id: string) => CATALOGUE.checks.find((c) => c.id === id)!;
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  const rows: Array<[string, RegExp, RegExp]> = [
    ['strip-measure', /5V with a DC supply connected and the switch on ON, the output of the converter, 5 V nominal/, /5V mit angeschlossener DC-Versorgung und Schalter auf ON der Ausgang des Wandlers, 5 V nominal/],
    ['strip-measure', /5V without a DC supply or with the switch on OFF, 0 V; 3V3, 3\.3 V where the regulator is powered \(whether USB alone powers it is not resolved\)/, /5V ohne DC-Versorgung oder mit Schalter auf OFF 0 V; 3V3 3,3 V, wo der Regler versorgt ist \(ob USB allein ihn versorgt, ist nicht geklärt\)/],
    ['strip-jumper', /only while a DC supply is connected and the OFF\/ON switch is on ON; without it/, /nur, solange eine DC-Versorgung angeschlossen ist und der Schalter OFF\/ON auf ON steht; ohne sie/],
    ['strip-jumper', /Position 3V3 feeds the V pins from the 3\.3 V regulator, which gives 3\.3 V where the regulator is powered; whether USB alone powers it is not resolved/, /Stellung 3V3 speist die V-Pins aus dem 3,3-V-Regler und gibt 3,3 V, wo der Regler versorgt ist; ob USB allein ihn versorgt, ist nicht geklärt/],
    ['strip-feed', /the 3-pin header H3\. Pin 1 is the net 3\.3V from the regulator U1, pin 2 is the net VCC, pin 3 is the net VOUT_5V from the converter U9/, /3-polige Pfostenverbinder H3\. Pin 1 ist das Netz 3\.3V vom Regler U1, Pin 2 ist das Netz VCC, Pin 3 ist das Netz VOUT_5V vom Wandler U9/],
    ['strip-feed', /No part joins VOUT_5V to the V pins when the jumper is on 3V3, and none joins VBUS to them when it is on 5V/, /Kein Bauteil verbindet VOUT_5V mit den V-Pins, wenn der Jumper auf 3V3 steht, und keines verbindet VBUS mit ihnen, wenn er auf 5V steht/],
    ['strip-feed', /The regulator takes its input from VSYS of the Pico board\. The converter takes its input from the DC jack, the VIN terminal or the 2-pin header H4 \(pin 2 PWRIN, pin 1 VIN-\) through the OFF\/ON switch/, /Der Regler bezieht seinen Eingang aus VSYS der Pico-Platine\. Der Wandler bezieht seinen Eingang aus der DC-Buchse, der VIN-Klemme oder dem 2-poligen Pfostenverbinder H4 \(Pin 2 PWRIN, Pin 1 VIN-\) über den Schalter OFF\/ON/],
    ['osoyoo-terminals', /S joins the pin of the Pico board directly\. All V pins of both sides are 1 net\. The 3-pin jumper V Selector \(print 5V and 3V3\) feeds that net from the 5 V converter of the DC input or from the 3\.3 V regulator/, /S liegt direkt am Pin der Pico-Platine\. Alle V-Pins beider Seiten sind 1 Netz\. Der 3-polige Jumper V Selector \(Aufdruck 5V und 3V3\) speist dieses Netz aus dem 5-V-Wandler des DC-Eingangs oder aus dem 3,3-V-Regler/],
    ['strip-esc', /uses the S and G pins of the strip at GP5 only/, /nutzt nur die Pins S und G der Leiste an GP5/],
    ['strip-dc', /The vendor states not to use the DC jack and the screw terminal together/, /Der Hersteller gibt an, die DC-Buchse und die Schraubklemme nicht zusammen zu verwenden/],
    ['strip-input', /set by the board, not by the converter chip/, /von der Platine bestimmt, nicht vom Wandler-Chip/],
    ['strip-print', /the 2 electrolytic capacitors beside the converter are printed 100 uF 16 V\. A supply above 16 V is outside the print and the capacitor rating/, /die 2 Elektrolytkondensatoren neben dem Wandler sind mit 100 uF 16 V bedruckt\. Eine Versorgung über 16 V liegt außerhalb des Aufdrucks und der Kondensatorangabe/],
    ['strip-regulator', /the jumper cap sits on the 3V3 side as delivered \(osoyoo\.com page; osoyoo\.store: not stated\)/, /Die Jumperkappe sitzt im Lieferzustand auf der Seite 3V3 \(Seite osoyoo\.com; osoyoo\.store: nicht angegeben\)/],
    ['strip-regulator', /the vendor names a workaround: a Dupont cable from the S pin of the 3\.3 V row to a V pin\. Do not use it for the servo\./, /nennt der Hersteller als Abhilfe ein Dupont-Kabel vom S-Pin der 3,3-V-Reihe zu einem V-Pin\. Für das Servo nicht verwenden\./],
    ['strip-regulator', /The S pin of the 3\.3 V row is the 3V3 pin of the IOmcu board \(pad 36\), the 3\.3 V supply of its logic; its current limit is not stated\./, /Der S-Pin der 3,3-V-Reihe ist der 3V3-Pin der IOmcu-Platine \(Pad 36\), die 3,3-V-Versorgung ihrer Logik; seine Stromgrenze ist nicht angegeben\./],
    ['strip-regulator', /With the cable in place and the jumper cap on 5V, the 5 V of the converter reaches that supply\. A cable already in place comes off before the jumper cap goes back on any position\.$/, /Mit gestecktem Kabel und der Jumperkappe auf 5V erreichen die 5 V des Wandlers diese Versorgung\. Ein schon gestecktes Kabel kommt ab, bevor die Jumperkappe wieder auf eine Stellung gesteckt wird\.$/],
    ['strip-servo-plus', /the servo lead connects only its signal and GND to the board, at the S pin of the strip at GP2 \(GP13 with the servo programmer\) and a G pin\./, /Das Servokabel verbindet nur Signal und GND mit der Platine, am S-Pin der Leiste an GP2 \(GP13 mit dem Servo-Programmierer\) und an einem G-Pin\./],
    ['strip-servo-plus', /Its plus wire goes to the servo supply, the external supply or the PD mini, as the drawing shows; with the INA3221 board in the setup it goes to VIN1- of that board, and the supply to VIN1\+\. The V pin of the strip stays free\./, /Seine Plusader geht an die Servoversorgung, die externe Versorgung oder den PD mini, wie die Zeichnung zeigt; mit der INA3221-Platine im Aufbau geht sie an VIN1- dieser Platine und die Versorgung an VIN1\+\. Der V-Pin der Leiste bleibt frei\./],
    ['strip-servo-plus', /All 40 V pins are 1 net fed through the jumper V Selector: a supply on 1 V pin reaches every strip and the output of the converter or the regulator\.$/, /Alle 40 V-Pins sind 1 Netz, gespeist über den Jumper V Selector: Eine Versorgung an 1 V-Pin erreicht jede Leiste und den Ausgang des Wandlers oder des Reglers\.$/],
    ['strip-parts', /drive only the LEDs of the IO pins, with their supply at VSYS/, /treiben nur die LEDs der IO-Pins, ihre Versorgung liegt an VSYS/],
    ['strip-parts', /A second MOSFET Q1 joins VOUT_5V to VSYS, its gate at VBUS: it passes the converter to VSYS only while no USB voltage is present/, /Ein zweiter MOSFET Q1 verbindet VOUT_5V mit VSYS, sein Gate liegt an VBUS: Er gibt den Wandler nur auf VSYS, solange keine USB-Spannung anliegt/],
    ['strip-level', /so a signal above 3\.3 V at S reaches the pin\. In position 5V the V pin carries 5 V, and a device fed from it can drive S above 3\.3 V/, /ein Signal über 3,3 V an S erreicht also den Pin\. In Stellung 5V führt der V-Pin 5 V, und ein davon gespeistes Gerät kann S über 3,3 V treiben/],
    ['strip-supply', /vendor values, not measured\. The marking of the converter chip is not stated in the schematic; the feedback pin joins the output directly, which fits the fixed 5 V version XL1509-5\.0\. The part number of the MOSFET Q1 is not stated\. With USB alone and the jumper on 3V3 the schematic feeds the 3\.3 V regulator from VSYS, while the product page says the V pins carry no voltage with USB alone and a jumper cap on the selector; which holds is not measured\. The stall current of the servo is not stated\.$/, /Herstellerangaben, nicht gemessen\. Die Kennzeichnung des Wandler-Chips steht nicht im Schaltplan; der Feedback-Pin liegt direkt am Ausgang, was zur festen 5-V-Version XL1509-5\.0 passt\. Die Teilenummer des MOSFET Q1 ist nicht angegeben\. Mit USB allein und dem Jumper auf 3V3 speist der Schaltplan den 3,3-V-Regler aus VSYS, während die Produktseite sagt, dass die V-Pins mit USB allein und einer Jumperkappe am Selector keine Spannung führen; was gilt, ist nicht gemessen\. Der Blockierstrom des Servos ist nicht angegeben\.$/],
  ];
  for (const [id, en, de] of rows) {
    assert.match(check(id).text.en, en, `${id} en`);
    assert.match(check(id).text.de, de, `${id} de`);
  }
  const power: Array<['desc' | 'cons' | 'unknowns', RegExp, RegExp]> = [
    ['desc', /position 5V feeds it from the XL1509 converter of the DC input, position 3V3 from the 3\.3 V regulator\. The servo lead/, /Stellung 5V speist es aus dem XL1509-Wandler des DC-Eingangs, Stellung 3V3 aus dem 3,3-V-Regler\. Das Servokabel/],
    ['desc', /All V pins are 1 net\./, /Alle V-Pins sind 1 Netz\./],
    ['cons', /Position 5V needs a DC supply of 7 to 12 V at the DC jack, the VIN terminal or the header H4, exactly 1 of the three/, /Stellung 5V braucht eine DC-Versorgung von 7 bis 12 V an der DC-Buchse, der VIN-Klemme oder dem Pfostenverbinder H4, an genau einem der drei Anschlüsse/],
    ['cons', /\) and the switch on ON; USB does not feed it\. Without it the V pins carry 0 V\. The supply is in the parts list as a modelled part\./, /\) und den Schalter auf ON; USB speist sie nicht\. Ohne sie führen die V-Pins 0 V\. Die Versorgung steht als modelliertes Teil in der Teileliste\./],
    ['unknowns', /^Current of the board in a measurement: not measured; 2 A and 500 mA are vendor values\./, /^Strom der Platine in einer Messung: nicht gemessen; 2 A und 500 mA sind Herstellerwerte\./],
    ['unknowns', /the schematic and the product page disagree, not measured\./, /Schaltplan und Produktseite widersprechen sich, nicht gemessen\./],
  ];
  for (const [field, en, de] of power) {
    const get = (lang: 'en' | 'de'): string => field === 'desc' ? strip.text[lang] : strip[field].map((x) => x[lang]).join(' | ');
    assert.match(get('en'), en, `strip-v ${field} en`);
    assert.match(get('de'), de, `strip-v ${field} de`);
  }
  // Wire notes 61 and 62 name the vendor wording in both languages (pinned elsewhere); the strip-v pros keep the shared 2A.
  assert.match(strip.pros[2]!.en, /share the 2 A\.$/);
  assert.match(strip.pros[2]!.de, /teilen sich die 2 A\.$/);
});

test('every unknown of the Osoyoo strip states that it is not measured, not stated or not resolved', () => {
  const EN = /not (measured|stated|resolved)/;
  const DE = /nicht (gemessen|angegeben|geklärt)/;
  for (const c of CATALOGUE.checks) {
    if (c.kind !== 'unknown' || !c.id.startsWith('strip-')) continue;
    assert.match(c.text.en, EN, `${c.id} en`);
    assert.match(c.text.de, DE, `${c.id} de`);
  }
  for (const s of CATALOGUE.powerSources.filter((x) => x.id === 'strip-v')) {
    for (const u of s.unknowns) {
      assert.match(u.en, EN, `${s.id} unknown en: ${u.en}`);
      assert.match(u.de, DE, `${s.id} unknown de: ${u.de}`);
    }
  }
  // The strip-supply text carries 5 English clauses; the German text words one of them as "steht nicht im Schaltplan" (4).
  const count = (text: string, re: RegExp): number => text.match(new RegExp(re.source, 'g'))?.length ?? 0;
  const supply = CATALOGUE.checks.find((c) => c.id === 'strip-supply')!;
  assert.equal(count(supply.text.en, EN), 5);
  assert.equal(count(supply.text.de, DE), 4);
});

test('the Osoyoo sources: the full title, address and reference of each document', () => {
  const doc = (id: string) => {
    const s = CATALOGUE.checks.find((c) => c.id === id)!.src;
    assert.equal(s.kind, 'doc', id);
    return s as Extract<Src, { kind: 'doc' }>;
  };
  assert.deepEqual(doc('strip-jumper'), {
    kind: 'doc',
    title: 'Schematic of the OSOYOO IO Breakout Board for Pico-series, REV 1.0, file dated 2024-12-03',
    url: 'https://osoyoo.com/driver/OSOYOO_Breakout_Board_for_Pico/Schematic_OSOYOO-IO-Expansion-Board-for-Pico-series_2024-12-03.pdf',
    ref: 'sheet 1: header H3 (pins 1 3.3V, 2 VCC, 3 VOUT_5V), connectors U15 and U17 (VCC), converter U9, regulator U1, MOSFETs Q1 and Q3',
  });
  assert.deepEqual(doc('strip-dc'), {
    kind: 'doc',
    title: 'OSOYOO Breakout Board for Pico series, product page, published 2024-12-02',
    url: 'https://osoyoo.com/2024/12/02/osoyoo-breakout-board-for-pico-series/',
    ref: 'sections TECH SPECS, DIAGRAM (V selector) and Note',
  });
  assert.deepEqual(doc('strip-print'), {
    kind: 'doc',
    title: 'OSOYOO Breakout Board for Pico series, product photos',
    url: 'https://osoyoo.com/2024/12/02/osoyoo-breakout-board-for-pico-series/',
    ref: 'photos of the board: print DC:7~16V at the DC jack, print 100 uF 16 V on the capacitors C1 and C2',
  });
  assert.deepEqual(doc('strip-load'), {
    kind: 'doc',
    title: 'Texas Instruments SN74HC04, SN54HC04, hex inverters',
    url: 'https://osoyoo.com/driver/OSOYOO_Breakout_Board_for_Nano/sn74hc04.pdf',
    ref: 'electrical characteristics: input leakage up to 1 uA, input capacitance 3 to 10 pF',
  });
  assert.deepEqual(doc('strip-converter'), {
    kind: 'doc',
    title: 'XLSEMI XL1509, 2A 150KHz 40V Buck DC to DC Converter, Rev 2.0',
    url: 'https://osoyoo.com/driver/OSOYOO_Breakout_Board_for_Nano/XL1509.pdf',
    ref: 'page 6, table XL1509-5.0 (4.8 to 5.2 V from 7 to 40 V in, 0.2 to 2 A); page 7, DC parameters (150 kHz, current limit 4 A)',
  });
});

test('doc source addresses: https, one host without blanks, a path that may be only a slash', () => {
  assert.ok(DOC_URL.test('https://example.com/a.pdf'));
  assert.ok(DOC_URL.test('https://example.com/'));
  assert.ok(!DOC_URL.test('http://x.y/?u=https://a.b/c'));
  assert.ok(!DOC_URL.test('xhttps://a.b/c'));
  assert.ok(!DOC_URL.test('https://a b.c/d'));
  assert.ok(!DOC_URL.test('https://example.com'));
  assert.ok(!DOC_URL.test('https://example.com/a b'));
  // An empty host with a path of 2 parts: the host holds no slash.
  assert.ok(!DOC_URL.test('https:///a/b'));
  const c = structuredClone(CATALOGUE) as Catalogue;
  c.functions[0]!.src = { kind: 'doc', url: 'https://example.com/a.pdf', ref: 'page 2' } as unknown as Src;
  const found = checkSrc(c, null).findings.filter((f) => /src "doc"/.test(f.msg));
  assert.deepEqual(found.map((f) => f.msg), ['src "doc" has no title']);
  c.functions[0]!.src = { kind: 'doc', title: 'T', ref: 'page 2' } as unknown as Src;
  assert.deepEqual(checkSrc(c, null).findings.filter((f) => /src "doc"/.test(f.msg)).map((f) => f.msg), ['src "doc" has no https address']);
  c.functions[0]!.src = { kind: 'doc', title: 'T', url: 'https://example.com/a.pdf' } as unknown as Src;
  assert.deepEqual(checkSrc(c, null).findings.filter((f) => /src "doc"/.test(f.msg)).map((f) => f.msg), ['src "doc" names no page, net or table']);
});

test('the Osoyoo strip: the order word of each safety instruction, in English and German', () => {
  const check = (id: string) => CATALOGUE.checks.find((c) => c.id === id)!;
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  assert.match(check('strip-measure').text.en, /G pin of the same strip before the servo is plugged in\./);
  assert.match(check('strip-measure').text.de, /^Osoyoo-Breakout-Platine, Ausgangsprüfung: Vor dem Einstecken des Servos den V-Pin/);
  assert.match(strip.cons[0]!.en, /^Warning: the jumper needs a check before a servo is plugged in\./);
  assert.match(strip.cons[0]!.de, /^Warnung: Der Jumper muss vor dem Einstecken eines Servos geprüft werden\./);
});

test('the Osoyoo strip: the build stage of wires 61 and 62', () => {
  const stage = (n: number): number | undefined => CATALOGUE.wires.find((w) => w.n === n)?.stage;
  // Wire 61 is the plug of the servo in stage 1, wire 62 the supply into the INA3221 shunt in stage 3.
  assert.equal(stage(61), 1);
  assert.equal(stage(62), 3);
});

test('the Osoyoo strip: the chip, the position and the source each check names, in English and German', () => {
  const check = (id: string) => CATALOGUE.checks.find((c) => c.id === id)!;
  const rows: Array<[string, RegExp, RegExp]> = [
    ['strip-converter', /^Osoyoo breakout board, converter chip XL1509, datasheet values of the 5\.0 V version: input/, /^Osoyoo-Breakout-Platine, Wandler-Chip XL1509, Datenblattwerte der 5,0-V-Version: Eingang/],
    ['strip-regulator', /^Osoyoo breakout board, 3\.3 V position: the jumper cap/, /^Osoyoo-Breakout-Platine, Stellung 3,3 V: Die Jumperkappe/],
    ['strip-regulator', /The regulator is an ME6210A33M3G according to the vendor\. The vendor rates it at 3\.3 V and 500 mA\./, /Der Regler ist laut Hersteller ein ME6210A33M3G\. Der Hersteller gibt ihn mit 3,3 V und 500 mA an\./],
    ['strip-input', /chip\. The vendor recommends 7 to 12 V and limits/, /Wandler-Chip\. Der Hersteller empfiehlt 7 bis 12 V und begrenzt/],
    ['strip-parts', /hex inverters take the Pico pins as inputs and drive only the LEDs of the IO pins/, /SN74HC04 nehmen die Pico-Pins als Eingänge und treiben nur die LEDs der IO-Pins/],
    ['strip-parts', /S joins the Pico pin directly and nothing is level-shifted or inverted\./, /S liegt direkt am Pico-Pin, nichts wird im Pegel gewandelt oder invertiert\./],
    ['strip-level', /signal level: the vendor states that the Pico accepts 3\.3 V I\/O only\. S of a strip joins the Pico pin directly, so/, /Signalpegel: Der Hersteller gibt an, dass der Pico nur 3,3-V-Ein- und Ausgänge verträgt\. S einer Leiste liegt direkt am Pico-Pin, ein/],
    ['strip-load', /leakage up to 1 uA and 3 to 10 pF \(datasheet\)\.$/, /Leckstrom bis 1 uA und 3 bis 10 pF \(Datenblatt\)\.$/],
    ['strip-print', /the DC jack reads 7 to 16 V and the 2 electrolytic capacitors beside the converter/, /Die DC-Buchse trägt den Aufdruck 7 bis 16 V, und die 2 Elektrolytkondensatoren neben dem Wandler/],
    ['osoyoo-terminals', /^Osoyoo breakout board: every IO has a strip of 3 pins\. From the terminal inward: S, V, G\./, /^Osoyoo-Breakout-Platine: Jeder IO hat eine Leiste mit 3 Pins\. Von der Klemme nach innen: S, V, G\./],
  ];
  for (const [id, en, de] of rows) {
    assert.match(check(id).text.en, en, `${id} en`);
    assert.match(check(id).text.de, de, `${id} de`);
  }
});

test('the DC supply of the Osoyoo breakout board: a modelled part with its range, its source and its 2 leads', () => {
  const dc = CATALOGUE.parts.find((p) => p.id === 'dcsup')!;
  // The only modelled part, added by a rule, never a part to own or a test object.
  assert.deepEqual(CATALOGUE.parts.filter((p) => p.modelled === true).map((p) => p.id), ['dcsup']);
  assert.equal(dc.selectable, false);
  assert.equal(dc.locked, false);
  assert.equal(dc.testObject, undefined);
  assert.equal(dc.release, 'released');
  assert.equal(dc.pictures, undefined);
  assert.deepEqual(dc.name, {
    en: 'DC supply for the Osoyoo breakout board, 7 to 12 V DC (yours)',
    de: 'DC-Versorgung für die Osoyoo-Breakout-Platine, 7 bis 12 V DC (eigene)',
  });
  assert.deepEqual(dc.short, { en: 'Osoyoo DC supply', de: 'Osoyoo-DC-Versorgung' });
  assert.deepEqual(dc.slot, { column: 'L', order: 12, sub: { en: '7 to 12 V DC', de: '7 bis 12 V DC' } });
  assert.deepEqual(dc.role, {
    en: 'Feeds the XL1509 converter of the Osoyoo breakout board, which feeds the V pins while the jumper V Selector sits on 5V. 7 to 12 V DC (vendor recommendation; print 7 to 16 V, vendor limit 18 V, capacitors rated 16 V). Above 12 V the gate-source voltage of the SI2302 (Q3) exceeds its rating of ±12 V (datasheet linked on the product page) at every input: the gate sits at PWRIN through R7, with no clamp. The print of 16 V and the vendor limit of 18 V are above that. The supply connects at exactly 1 of these: the DC jack, the VIN terminal or the header H4 (pin 2 PWRIN, pin 1 VIN-). At the DC jack: plug 5.5 x 2.1 mm, centre positive. The product page names the socket DC005-2.1: the 2.1 mm pin follows from that name; the 5.5 mm outer diameter is the usual size of that socket family and is not stated by the vendor. The minus of the jack is GND of the board, with no reverse-polarity protection: the SI2302 (Q3) in the lead VIN- protects the VIN terminal and H4 only. The switch OFF/ON is on ON. Current it must deliver: not stated.',
    de: 'Speist den XL1509-Wandler der Osoyoo-Breakout-Platine, der die V-Pins speist, solange der Jumper V Selector auf 5V sitzt. 7 bis 12 V DC (Herstellerempfehlung; Aufdruck 7 bis 16 V, Herstellergrenze 18 V, Kondensatoren mit 16 V). Über 12 V überschreitet die Gate-Source-Spannung des SI2302 (Q3) an jedem Eingang ihren Grenzwert von ±12 V (Datenblatt, verlinkt auf der Produktseite): Das Gate liegt über R7 ohne Begrenzung an PWRIN. Der Aufdruck von 16 V und die Herstellergrenze von 18 V liegen darüber. Die Versorgung wird an genau einem dieser Anschlüsse angeschlossen: an der DC-Buchse, an der VIN-Klemme oder am Pfostenverbinder H4 (Pin 2 PWRIN, Pin 1 VIN-). An der DC-Buchse: Stecker 5,5 x 2,1 mm, Mittelstift Plus. Die Produktseite nennt die Buchse DC005-2.1: Der Stift von 2,1 mm folgt aus diesem Namen; der Außendurchmesser von 5,5 mm ist das übliche Maß dieser Buchsenfamilie und vom Hersteller nicht angegeben. Das Minus der Buchse ist GND der Platine, ohne Verpolschutz: Der SI2302 (Q3) in der Leitung VIN- schützt nur die VIN-Klemme und H4. Der Schalter OFF/ON steht auf ON. Strom, den sie liefern muss: nicht angegeben.',
  });
  // No source states the input current, so the part names none.
  assert.doesNotMatch(dc.role!.en, /\d+(\.\d+)? ?m?A\b/);
  // The range is the one the strip states: the same 7 to 12V and the same bounds.
  const strip = CATALOGUE.powerSources.find((x) => x.id === 'strip-v')!;
  assert.match(strip.cons[1]!.en, /7 to 12 V .*print 7 to 16 V, vendor limit 18 V, capacitors rated 16 V/);
  // Sources: the product page for the part, the schematic for its pins and leads.
  assert.equal(dc.src.kind, 'doc');
  assert.match((dc.src as { url: string }).url, /osoyoo-breakout-board-for-pico-series/);
  assert.deepEqual(dc.pins.map((p) => [p.id, p.label.en, p.src.kind, p.confirmed]), [['POS', '+', 'doc', true], ['NEG', '−', 'doc', true]]);

  // The rule: exactly the condition of the warning that says to set the jumper to 5V.
  const rule = CATALOGUE.rules.find((r) => r.adds === 'dcsup')!;
  assert.equal(rule.id, 'osoyoo-dc');
  assert.deepEqual(rule.when, CATALOGUE.checks.find((c) => c.id === 'strip-jumper')!.when);
  assert.deepEqual(rule.when, {
    all: [
      { all: [{ has: 'servo' }, { not: { has: 'pdmini' } }, { opt: 'servoPower', is: 'bench' }] },
      { opt: 'connection', is: 'osoyoo' },
    ],
  });
  assert.deepEqual(rule.because, { en: 'servo on the V pin, jumper on 5V', de: 'Servo am V-Pin, Jumper auf 5V' });
  assert.deepEqual(rule.src, dc.pins[0]!.src);

  // The 2 leads: stubs to the DC input, which is no pad of the IOmcu.
  const leads = CATALOGUE.wires.filter((w) => w.a.startsWith('dcsup.'));
  assert.deepEqual(leads.map((w) => [w.n, w.kind, w.a, w.b, w.net, w.cls, w.stage, w.route.kind]), [
    [65, 'stub', 'dcsup.POS', null, 'osoyoo_vin', 'power', 1, 'S'],
    [66, 'stub', 'dcsup.NEG', null, 'osoyoo_vin_neg', 'power', 1, 'S'],
  ]);
  for (const w of leads) assert.deepEqual(w.when, rule.when, `wire ${w.n}`);
  assert.deepEqual(leads.map((w) => w.to), [
    {
      en: 'plus of the DC input of the Osoyoo breakout board: the centre pin of the DC jack, the VIN terminal or H4 pin 2 PWRIN, exactly 1 of them',
      de: 'Plus des DC-Eingangs der Osoyoo-Breakout-Platine: Mittelstift der DC-Buchse, VIN-Klemme oder H4 Pin 2 PWRIN, genau einer davon',
    },
    {
      en: 'minus of the same DC input of the Osoyoo breakout board: VIN- at the minus pole of the 2-pole VIN terminal or at H4 pin 1, GND at the outer contact of the DC jack',
      de: 'Minus desselben DC-Eingangs der Osoyoo-Breakout-Platine: VIN- am Minuspol der 2-poligen VIN-Klemme oder an H4 Pin 1, GND am Außenkontakt der DC-Buchse',
    },
  ]);
  for (const w of leads) {
    assert.deepEqual(w.signal, { en: 'DC input of the Osoyoo breakout board, 7 to 12 V', de: 'DC-Eingang der Osoyoo-Breakout-Platine, 7 bis 12 V' });
    assert.deepEqual(w.note, {
      en: 'Exactly 1 input: the DC jack, the VIN terminal or the header H4, never 2 of them. DC jack: type DC005-2.1 (product page), plug 5.5 x 2.1 mm (2.1 mm from the socket name, 5.5 mm the usual size of that socket family, not stated by the vendor), centre positive, no reverse-polarity protection; the SI2302 protects the VIN terminal and H4 only. The switch OFF/ON on ON. USB does not feed the V pins.',
      de: 'Genau 1 Eingang: DC-Buchse, VIN-Klemme oder Pfostenverbinder H4, nie 2 davon. DC-Buchse: Typ DC005-2.1 (Produktseite), Stecker 5,5 x 2,1 mm (2,1 mm aus dem Namen der Buchse, 5,5 mm das übliche Maß dieser Buchsenfamilie, vom Hersteller nicht angegeben), Mittelstift Plus, ohne Verpolschutz; der SI2302 schützt nur die VIN-Klemme und H4. Der Schalter OFF/ON auf ON. USB speist die V-Pins nicht.',
    });
  }
});

test('the DC jack of the Osoyoo breakout board: centre positive, minus on GND, the SI2302 on the terminal and H4 only', () => {
  // Schematic REV 1.0: DC1 pin 1 is PWRIN, pins 2 and 3 are GND. Q3 joins VIN-
  // to GND, and only the VIN terminal CN7 and the header H4 carry VIN-.
  const dc = CATALOGUE.parts.find((p) => p.id === 'dcsup')!;
  const w66 = CATALOGUE.wires.find((w) => w.n === 66)!;
  const w65 = CATALOGUE.wires.find((w) => w.n === 65)!;
  const parts = CATALOGUE.checks.find((c) => c.id === 'strip-parts')!;
  for (const [what, text] of [['role', dc.role!], ['note of wire 66', w66.note!]] as const) {
    assert.match(text.en, /plug 5\.5 x 2\.1 mm.*centre positive/, `${what} en`);
    assert.match(text.de, /Stecker 5,5 x 2,1 mm.*Mittelstift Plus/, `${what} de`);
    assert.match(text.en, /no reverse-polarity protection/, `${what} en`);
    assert.match(text.de, /ohne Verpolschutz/, `${what} de`);
    assert.match(text.en, /the SI2302 (\(Q3\) in the lead VIN- )?protects the VIN terminal and H4 only/, `${what} en`);
    assert.match(text.de, /SI2302 (\(Q3\) in der Leitung VIN- )?schützt nur die VIN-Klemme und H4/, `${what} de`);
  }
  // The plus is the centre pin of the jack; the minus of the jack is GND, VIN- only at the terminal and H4.
  assert.match(w65.to!.en, /the centre pin of the DC jack/);
  assert.match(w65.to!.de, /Mittelstift der DC-Buchse/);
  assert.match(w66.to!.en, /VIN- at the minus pole of the 2-pole VIN terminal or at H4 pin 1, GND at the outer contact of the DC jack/);
  assert.match(w66.to!.de, /VIN- am Minuspol der 2-poligen VIN-Klemme oder an H4 Pin 1, GND am Außenkontakt der DC-Buchse/);
  assert.doesNotMatch(w66.to!.en, /: the DC jack/);
  // The board notes: the MOSFET protects the terminal and H4 only, the jack has none.
  assert.match(parts.text.en, /protects the terminal and H4 only against reverse polarity\. The minus of the DC jack is GND directly: the jack has no reverse-polarity protection\./);
  assert.match(parts.text.de, /schützt nur die Klemme und H4 gegen Verpolung\. Das Minus der DC-Buchse liegt direkt an GND: Die Buchse hat keinen Verpolschutz\./);
  assert.doesNotMatch(parts.text.en, /minus lead of the DC input/);
  assert.doesNotMatch(parts.text.de, /Minusleitung des DC-Eingangs/);
});

test('the DC jack of the Osoyoo breakout board: the plug size names its basis, the SI2302 gate limits the input to 12 V', () => {
  // The product page names the socket DC005-2.1 only. The 2.1mm pin follows
  // from that name; the 5.5mm outer diameter is the usual size of the socket
  // family. Every text that gives the plug size says so.
  const dc = CATALOGUE.parts.find((p) => p.id === 'dcsup')!;
  const w66 = CATALOGUE.wires.find((w) => w.n === 66)!;
  for (const [what, text] of [['role', dc.role!], ['note of wire 66', w66.note!]] as const) {
    assert.match(text.en, /DC005-2\.1/, `${what} en`);
    assert.match(text.de, /DC005-2\.1/, `${what} de`);
    assert.match(text.en, /usual size of that socket family.*not stated by the vendor/, `${what} en`);
    assert.match(text.de, /übliche Maß dieser Buchsenfamilie.*vom Hersteller nicht angegeben/, `${what} de`);
  }
  assert.match(dc.role!.en, /The product page names the socket DC005-2\.1: the 2\.1 mm pin follows from that name/);
  assert.match(dc.role!.de, /Die Produktseite nennt die Buchse DC005-2\.1: Der Stift von 2,1 mm folgt aus diesem Namen/);
  // Schematic REV 1.0: the gate of Q3 sits at PWRIN through R7 with no clamp,
  // its source at GND; the HXY datasheet rates the gate-source voltage at ±12V.
  assert.match(dc.role!.en, /Above 12 V the gate-source voltage of the SI2302 \(Q3\) exceeds its rating of ±12 V \(datasheet linked on the product page\) at every input: the gate sits at PWRIN through R7, with no clamp\. The print of 16 V and the vendor limit of 18 V are above that\./);
  assert.match(dc.role!.de, /Über 12 V überschreitet die Gate-Source-Spannung des SI2302 \(Q3\) an jedem Eingang ihren Grenzwert von ±12 V \(Datenblatt, verlinkt auf der Produktseite\): Das Gate liegt über R7 ohne Begrenzung an PWRIN\. Der Aufdruck von 16 V und die Herstellergrenze von 18 V liegen darüber\./);
  // The range stays 7 to 12V.
  assert.deepEqual(dc.slot!.sub, { en: '7 to 12 V DC', de: '7 bis 12 V DC' });
});

test('the Osoyoo strip: the whole advantage, drawbacks and the USB drawback that name the strip supply', () => {
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  const usb = CATALOGUE.powerSources.find((s) => s.id === 'usb')!;
  assert.deepEqual(strip.pros[2], {
    en: 'In position 5V the servo draws from the DC input through a converter rated 2 A. With USB connected this is not the supply of the logic; without USB voltage the converter also feeds VSYS of the Pico board, and the servo and the logic share the 2 A.',
    de: 'In Stellung 5V zieht das Servo aus dem DC-Eingang über einen Wandler mit 2 A. Mit angeschlossenem USB ist das nicht die Versorgung der Logik; ohne USB-Spannung speist der Wandler auch VSYS der Pico-Platine, und Servo und Logik teilen sich die 2 A.',
  });
  assert.deepEqual(strip.cons[1], {
    en: 'As delivered the jumper cap sits on the 3V3 side (osoyoo.com page), so position 5V is a change. Position 5V needs a DC supply of 7 to 12 V at the DC jack, the VIN terminal or the header H4, exactly 1 of the three (vendor recommendation; print 7 to 16 V, vendor limit 18 V, capacitors rated 16 V) and the switch on ON; USB does not feed it. Without it the V pins carry 0 V. The supply is in the parts list as a modelled part.',
    de: 'Im Lieferzustand sitzt die Jumperkappe auf der Seite 3V3 (Seite osoyoo.com), Stellung 5V ist also eine Änderung. Stellung 5V braucht eine DC-Versorgung von 7 bis 12 V an der DC-Buchse, der VIN-Klemme oder dem Pfostenverbinder H4, an genau einem der drei Anschlüsse (Herstellerempfehlung; Aufdruck 7 bis 16 V, Herstellergrenze 18 V, Kondensatoren mit 16 V) und den Schalter auf ON; USB speist sie nicht. Ohne sie führen die V-Pins 0 V. Die Versorgung steht als modelliertes Teil in der Teileliste.',
  });
  assert.deepEqual(strip.cons[2], {
    en: 'The rating is 2 A at 5 V and 500 mA at 3.3 V (vendor values). A stalled servo draws more than that where its stall current is higher; the stall current of the servo is not stated here.',
    de: 'Die Angabe lautet 2 A bei 5 V und 500 mA bei 3,3 V (Herstellerwerte). Ein blockiertes Servo zieht mehr, wenn sein Blockierstrom höher liegt; der Blockierstrom des Servos steht hier nicht.',
  });
  assert.deepEqual(strip.cons[3], {
    en: 'In position 3V3 the servo draws from VSYS of the IOmcu board, the supply that also powers the logic. A current peak can drop it and reset the boards.',
    de: 'In Stellung 3V3 zieht das Servo aus VSYS der IOmcu-Platine, der Versorgung, die auch die Logik speist. Eine Stromspitze kann sie einbrechen lassen und die Platinen zurücksetzen.',
  });
  assert.deepEqual(usb.cons, [{
    en: 'A servo on the 5 V pin, or on a V pin of the Osoyoo breakout board with the jumper on 3V3, draws from it.',
    de: 'Ein Servo am 5-V-Pin, oder an einem V-Pin der Osoyoo-Breakout-Platine mit dem Jumper auf 3V3, zieht daraus.',
  }]);
});

test('the Osoyoo strip: the power source cites the schematic', () => {
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  const schematic = CATALOGUE.checks.find((c) => c.id === 'strip-jumper')!.src;
  assert.equal(schematic.kind, 'doc');
  assert.deepEqual(strip.src, schematic);
});

test('the Osoyoo strip: every text names the selector "jumper V Selector", in English and German', () => {
  const texts: Array<[string, string, string]> = [];
  for (const c of CATALOGUE.checks) {
    if (c.id.startsWith('strip-') || c.id.startsWith('osoyoo-') || c.id === 'servo-bench-supply') texts.push([c.id, c.text.en, c.text.de]);
  }
  const strip = CATALOGUE.powerSources.find((s) => s.id === 'strip-v')!;
  texts.push(['strip-v desc', strip.text.en, strip.text.de]);
  [...strip.pros, ...strip.cons, ...strip.unknowns].forEach((x, i) => texts.push([`strip-v ${i}`, x.en, x.de]));
  for (const w of CATALOGUE.wires.filter((x) => x.n === 61 || x.n === 62)) texts.push([`wire ${w.n}`, w.note!.en, w.note!.de]);
  const named: string[] = [];
  for (const [id, en, de] of texts) {
    // Every "V Selector" follows the word jumper: "jumper V Selector", German "Jumper V Selector" or "Jumpers V Selector".
    const enAll = en.match(/\S+ V Selector/g) ?? [];
    const deAll = de.match(/\S+ V Selector/g) ?? [];
    for (const m of enAll) assert.match(m, /^jumper V Selector$/, `${id} en: ${m}`);
    for (const m of deAll) assert.match(m, /^Jumpers? V Selector$/, `${id} de: ${m}`);
    assert.equal(enAll.length, deAll.length, `${id}: the same count in both languages`);
    if (deAll.length > 0) named.push(id);
  }
  assert.deepEqual(named, [
    'osoyoo-terminals', 'strip-esc', 'strip-servo-plus', 'servo-bench-supply', 'strip-jumper', 'strip-measure', 'strip-feed',
    'strip-v desc', 'wire 61', 'wire 62',
  ]);
});
