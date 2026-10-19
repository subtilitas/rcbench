// The help text of the functions with a tick: one entry per such function,
// none for another id, each under the text rules of the data.

import assert from 'node:assert/strict';
import { dirname } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { CATALOGUE } from '../catalogue.ts';
import { alwaysOn } from '../groups.ts';
import { FUNCTION_HELP } from '../help.ts';
import { RELEASE_TAG, checkAll, checkHelp, gitTree } from '../tools/check.ts';

const TICKED = CATALOGUE.functions.filter((fn) => !alwaysOn(CATALOGUE, fn)).map((fn) => fn.id);

test('the help text covers every function with a tick and no other', () => {
  assert.equal(TICKED.length, 12);
  assert.deepEqual(Object.keys(FUNCTION_HELP).sort(), [...TICKED].sort());
  assert.deepEqual(checkHelp(CATALOGUE), []);
  assert.ok(Object.isFrozen(FUNCTION_HELP));
});

test('the check finds a missing entry, an entry of a function without a tick and a text that breaks a rule', () => {
  const { servo: _servo, ...missing } = FUNCTION_HELP;
  assert.deepEqual(checkHelp(CATALOGUE, missing).map((f) => f.id), ['help.servo']);
  assert.deepEqual(checkHelp(CATALOGUE, { ...FUNCTION_HELP, link: { en: 'Links.', de: 'Verbindet.' } }).map((f) => f.id), ['help.link']);
  assert.deepEqual(checkHelp(CATALOGUE, { ...FUNCTION_HELP, nosuch: { en: 'x', de: 'x' } }).map((f) => f.id), ['help.nosuch']);
  const bad = checkHelp(CATALOGUE, {
    ...FUNCTION_HELP,
    servo: { en: 'Drives a servo, easy.', de: '' },
    esc: { en: 'Drives an ESC!', de: 'Steuert einen ESC.' },
  });
  assert.deepEqual(bad.map((f) => f.id), ['help.servo.en', 'help.servo.de', 'help.esc.en']);
  assert.ok(bad.every((f) => f.level === 'error'));
});

test('the check of the data file runs the check of the help text', () => {
  const report = checkAll(CATALOGUE, null);
  assert.deepEqual(report.findings.filter((f) => f.id.startsWith('help')), []);
});

const help = (id: string): { en: string; de: string } => FUNCTION_HELP[id as keyof typeof FUNCTION_HELP]!;

/** A `#define NAME value` of a C header, as a number, or null. */
function define(source: string, name: string): number | null {
  const m = new RegExp(`#define\\s+${name}\\s+(\\d+)u?\\b`).exec(source);
  return m ? Number(m[1]) : null;
}

/** Millivolts as the text writes them: "0.5 V" in English, "0,5 V" in German. */
const volts = (mv: number, lang: 'en' | 'de'): string => {
  const v = String(mv / 1000);
  return `${lang === 'de' ? v.replace('.', ',') : v} V`;
};

test('the wording of the texts: no pad, the German labels of the panel, SAFE and ARMED', () => {
  // The Osoyoo variant names terminals, not pads: the function text names the pin alone.
  assert.doesNotMatch(help('servoprog').en, /\bpad\b/i);
  assert.doesNotMatch(help('servoprog').de, /\bPad\b/);
  assert.match(help('servoprog').en, /programming port, GP13\./);
  // The two states of the bench carry the names of the status band of the panel, SAFE and ARMED, in both
  // languages; no text names them in prose.
  assert.match(help('logs').en, /while the bench is ARMED, and while it is SAFE with the supply output on\./);
  assert.match(help('logs').de, /, solange der Prüfstand ARMED ist oder bei SAFE der Ausgang des Netzteils an ist\./);
  // A state word in prose, any case but the band's ARMED; the German words of the glossary and their forms.
  const stateEn = /\b(?:un|dis)?armed\b|\bArmed\b|\bdisarm\w*|\bDisarm\w*|\bDISARM\w*/;
  const stateDe = /scharf|schärf|gesichert|entschärf|\bDISARM\w*|NICHT ARMED|\bnach ARM\b/i;
  for (const word of ['armed', 'Armed', 'unarmed', 'disarmed', 'Disarmed', 'DISARMED', 'disarms', 'DISARM']) {
    assert.match(`the bench is ${word}`, stateEn, word);
  }
  for (const word of ['scharf', 'unscharf', 'geschärft', 'schärfen', 'entschärft', 'gesichert', 'DISARMED', 'NICHT ARMED', 'nach ARM']) {
    assert.match(`der Prüfstand ist ${word}`, stateDe, word);
  }
  assert.doesNotMatch('while the bench is ARMED or SAFE', stateEn);
  assert.doesNotMatch('solange der Prüfstand ARMED oder SAFE ist', stateDe);
  const parts = CATALOGUE.parts.filter((p) => p.selectable && p.role).map((p) => p.role!);
  for (const text of [...Object.values(FUNCTION_HELP).map((x) => x!), ...parts]) {
    assert.doesNotMatch(text.en, stateEn, text.en);
    assert.doesNotMatch(text.de, stateDe, text.de);
  }
  // German: one word per English term, as the glossary of docs/Language-de.md and the German docs name it: Log,
  // Netzteil, Piepton, Ruderhorn, Lastwiderstand, and Frame Rate stays English.
  const names = CATALOGUE.functions.filter((fn) => TICKED.includes(fn.id)).map((fn) => fn.name.de);
  for (const text of [...Object.values(FUNCTION_HELP).map((x) => x!.de), ...parts.map((x) => x.de), ...names]) {
    assert.doesNotMatch(text, /Protokoll|Ausgang der Versorgung|Töne|Servohorn|Widerstandslast|Frame-Rate/, text);
  }
  assert.doesNotMatch(help('esc').de, /Leistung/);
  // The emergency stop breaks no power line, and the IOmcu switches the PD mini off.
  const estopPart = CATALOGUE.parts.find((p) => p.id === 'estop')!;
  for (const text of [help('estop').en, estopPart.role!.en]) {
    assert.doesNotMatch(text, /switches off no supply/);
    assert.match(text, /breaks no power line/);
    assert.match(text, /output of the PD mini (?:off|go off)/);
  }
  for (const text of [help('estop').de, estopPart.role!.de]) {
    assert.doesNotMatch(text, /schaltet keine Versorgung ab/);
    assert.match(text, /trennt keine Versorgungsleitung/);
    assert.match(text, /Ausgang des PD mini (?:aus|gehen aus)/);
  }
  // Both name the bench switching to SAFE, not the PD mini alone.
  assert.match(estopPart.role!.en, /the IOmcu switches the bench to SAFE and the output of the PD mini off\./);
  assert.match(estopPart.role!.de, /der IOmcu schaltet den Prüfstand auf SAFE und den Ausgang des PD mini aus\./);
  assert.match(help('estop').en, /the IOmcu switches the bench to SAFE: its outputs and the output of the PD mini go off\./);
  assert.match(help('estop').de, /schaltet der IOmcu den Prüfstand auf SAFE: seine Ausgänge und der Ausgang des PD mini gehen aus\./);
  assert.match(help('estop').en, /an ESC battery or a servo supply of your own stays connected/);
});

test('the numbers of the texts match the sources at the release tag', (t) => {
  const tree = gitTree(dirname(fileURLToPath(import.meta.url)));
  if (tree === null) {
    t.skip(`git or the tag ${RELEASE_TAG} is absent`);
    return;
  }
  const read = (path: string): string => {
    const text = tree.read(path);
    assert.ok(text !== null, `${path} at ${RELEASE_TAG}`);
    return text;
  };

  // estop: the heartbeat ceiling, and SUPPLY switches the output off when the heartbeat is lost.
  const gap = define(read('shared/safety/include/heartbeat.h'), 'HEARTBEAT_MAX_GAP_MS');
  assert.equal(gap, 150);
  assert.match(help('estop').en, new RegExp(`after ${gap} ms without a heartbeat edge`));
  assert.match(help('estop').de, new RegExp(`nach ${gap} ms ohne Heartbeat-Flanke`));
  assert.match(read('shared/outputs/supply_page.c'), /if \(!beat_alive\)/);

  // supply: the range of the module and the cap of the set point under its input. The cap is
  // PDMINI_CAP_MV where the header defines it, PDMINI_HEADROOM_MV before that.
  const pd = read('protocols/pdmini/include/pdmini.h');
  const cap = define(pd, 'PDMINI_CAP_MV') ?? define(pd, 'PDMINI_HEADROOM_MV');
  assert.ok(cap !== null);
  const vMin = define(pd, 'PDMINI_V_MIN_MV')!;
  const vMax = define(pd, 'PDMINI_V_MAX_MV')!;
  const iMin = define(pd, 'PDMINI_I_MIN_MA')!;
  const iMax = define(pd, 'PDMINI_I_MAX_MA')!;
  assert.match(help('supply').en, new RegExp(`${vMin / 1000} to ${vMax / 1000} V, ${iMin / 1000} to ${iMax / 1000} A`));
  // Every text that states the cap, the bubble of the supply among them, states it in one form: at least
  // the cap below the input, and the same number.
  assert.match(help('supply').en, /the voltage at least \d+(?:\.\d+)? V below the input voltage of the module/);
  assert.match(help('supply').de, /die Spannung mindestens \d+(?:,\d+)? V unter der Eingangsspannung des Moduls/);
  const texts = [
    help('supply'),
    ...CATALOGUE.safety.map((s) => s.text),
    ...CATALOGUE.wires.flatMap((w) => (w.note ? [w.note] : [])),
    ...CATALOGUE.powerSources.map((p) => p.text),
  ];
  const stated = texts.flatMap((x) => [
    ...[...x.en.matchAll(/at least (\d+(?:\.\d+)?) V below/g)].map((m) => `${m[1]} V`),
    ...[...x.de.matchAll(/mindestens (\d+(?:,\d+)?) V unter/g)].map((m) => `${m[1]} V`),
  ]);
  assert.ok(stated.length >= 10, `${stated.length} statements of the cap`);
  // The cap is never stated as "at most N V under the input", which reads as "within N V of the input", and
  // no text keeps the form "input less N V" next to the stated one.
  for (const x of texts) {
    assert.doesNotMatch(x.en, /at most \d+(?:\.\d+)? V (?:under|below)/, x.en);
    assert.doesNotMatch(x.de, /höchstens \d+(?:,\d+)? V unter/, x.de);
    assert.doesNotMatch(x.en, /\bless \d+(?:\.\d+)? V/, x.en);
    assert.doesNotMatch(x.de, /\bminus \d+(?:,\d+)? V/, x.de);
  }
  assert.deepEqual([...new Set(stated)].sort(), [volts(cap, 'de'), volts(cap, 'en')].sort());

  // stick: the share of the ESC profiles the ESC STICK class runs.
  const runs = /runs (\d+) of the (\d+) profiles/.exec(read('STATUS.md'));
  assert.ok(runs !== null);
  assert.match(help('stick').en, new RegExp(`; ${runs[1]} of the ${runs[2]} ESC profiles run\\.`));
  assert.match(help('stick').de, new RegExp(`; ${runs[1]} der ${runs[2]} ESC-Profile laufen\\.`));

  // servo, German: the controls carry the labels of the German panel.
  const de = read('shared/ui/ui_text_de.c');
  const label = (key: string): string => new RegExp(`\\[${key}\\]\\s*=\\s*"([^"]+)"`).exec(de)![1]!;
  const controls = ['TX_SV_ROW_TRIM', 'TX_SV_ROW_TRAVEL', 'TX_SV_ROW_REVERSE'].map(label);
  assert.deepEqual(controls, ['TRIMMUNG', 'WEG', 'UMKEHREN']);
  assert.match(help('servo').de, new RegExp(`Pulsbreite, ${controls[0]}, ${controls[1]} und ${controls[2]},`));
});
