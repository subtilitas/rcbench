// Static invariants 1 to 10 on the catalogue, and proof that each check
// reports a broken copy of the data.

import assert from 'node:assert/strict';
import { dirname } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { TAG, VERSION } from '../../release.ts';
import type { Catalogue, Picture, Src } from '../../contract.ts';
import { ANCHOR_TEXTS, CATALOGUE } from '../catalogue.ts';
import {
  ANCHOR_TEXT_MIN, BANNED_WORDS, RELEASE_TAG, checkAll, checkCounts, checkEnds, checkFunctions, checkIds,
  OWNER_STATEMENTS, checkRefs, checkRoutes, checkSrc, checkString, checkTexts, checkTree, collectAnchors,
  collectTexts, DOC_URL, gitTree, parseFirmwarePads, parseFirmwarePins, parseSettingDefault, wireTable,
} from '../tools/check.ts';
import type { Finding, Tree } from '../tools/check.ts';
import { PICTURE_DIR, checkPictures, pictureFolder, webpInfo } from '../tools/pictures.ts';
import type { PictureFolder } from '../tools/pictures.ts';
import { BEGIN, END, MEASURED, figure, parseLcov, percent, replaceFigure } from '../tools/coverage.ts';

const OUT_BIND = 'shared/outputs/out_bind.c';
const SETTINGS = 'shared/settings/settings.c';
const IOMCU_PINS = 'firmware/iomcu/include/iomcu_pins.h';

/** Index of the first function that needs a part. Its id is `servo`. */
const SERVO = CATALOGUE.functions.findIndex((f) => f.id === 'servo');

function copy(): Catalogue {
  return structuredClone(CATALOGUE);
}

function errors(findings: Finding[]): string[] {
  return findings.filter((f) => f.level === 'error').map((f) => `${f.id}: ${f.msg}`);
}

/** Asserts that at least one error names `id` and matches `pattern`. */
function reports(findings: Finding[], id: string, pattern: RegExp): void {
  const hit = findings.some((f) => f.level === 'error' && f.id === id && pattern.test(f.msg));
  assert.ok(hit, `no error "${id}" matching ${pattern}; got: ${errors(findings).join(' | ') || 'none'}`);
}

/**
 * A stand-in for the repository at the release tag, built from the data:
 * the firmware tables hold the rows the data names, on the lines it names.
 * Every other path is a file of 2000 lines. Each line an anchor names holds
 * the text of that anchor.
 */
function fakeTree(c: Catalogue, edit: (files: Map<string, string[]>) => void = () => {}): Tree {
  const filler = (): string[] => Array.from({ length: 2000 }, () => '/* text */');
  const heartbeat = c.pinDefaults.find((d) => d.signal === 'heartbeat')?.gpio;

  const outBind = filler();
  for (const pad of c.pads) {
    const line = pad.src.kind === 'repo' ? pad.src.line : 0;
    if (pad.gpio !== null) {
      const reserved = pad.reserved !== undefined || pad.gpio === heartbeat;
      outBind[line - 1] = `    { ${pad.gpio}, ${pad.n}, ${reserved}, ${reserved ? '"used"' : 'NULL'} },`;
    } else {
      const kind = pad.name === 'GND' ? 'GROUND' : 'POWER';
      const name = pad.name === 'GND' ? '' : `   /* ${pad.name} */`;
      outBind[line - 1] = `    { ${pad.n}, LINK_PAD_${kind}, 0 },${name}`;
    }
  }

  const settings = filler();
  const entry = (line: number, key: string, def: number): void => {
    settings[line - 2] = `    [${key}] = {`;
    settings[line - 1] = `        "key", "Name", "Help", "GPIO",`;
    settings[line] = `        SET_CAT_IFACE, SET_TYPE_INT, -1, 47, 1, ${def}, NULL, 0 },`;
  };
  entry(161, 'SET_SENSE_SDA', 16);
  entry(164, 'SET_SENSE_SCL', 17);
  entry(191, 'SET_TONE_PIN', 22);
  entry(226, 'SET_PDMINI_TX', -1);
  entry(229, 'SET_PDMINI_RX', -1);

  const iomcu = filler();
  iomcu[42] = '#define IOMCU_HEARTBEAT_PIN  3';

  const files = new Map<string, string[]>([[OUT_BIND, outBind], [SETTINGS, settings], [IOMCU_PINS, iomcu]]);
  const squash = (s: string): string => s.replace(/\s+/g, ' ').trim();
  for (const text of ANCHOR_TEXTS) {
    const held = files.get(text.path) ?? filler();
    files.set(text.path, held);
    const line = held[text.line - 1] ?? '';
    if (!squash(line).includes(squash(text.has))) held[text.line - 1] = `${line} /* ${text.has} */`;
  }
  edit(files);
  const known = new Set(collectAnchors(c).flatMap((a) => (a.src?.kind === 'repo' ? [a.src.path] : [])));
  return {
    read: (path) => {
      const lines = files.get(path);
      if (lines) return `${lines.join('\n')}\n`;
      return known.has(path) ? `${filler().join('\n')}\n` : null;
    },
    newestTag: RELEASE_TAG,
  };
}

function lines(files: Map<string, string[]>, path: string): string[] {
  const found = files.get(path);
  assert.ok(found, `${path} is in the stand-in tree`);
  return found;
}

// --- the data as it is ------------------------------------------------------

test('the catalogue passes every static check', () => {
  const report = checkAll(CATALOGUE, null);
  assert.deepEqual(report.findings, []);
  assert.equal(report.repoChecked, 0);
  assert.ok(report.repoAnchors > 0);
});

test('the catalogue passes against the stand-in tree', () => {
  const report = checkAll(CATALOGUE, fakeTree(CATALOGUE), true);
  assert.deepEqual(report.findings, []);
  assert.equal(report.repoChecked, report.repoAnchors);
});

test('the catalogue passes against the repository at the release tag', (t) => {
  const tree = gitTree(dirname(fileURLToPath(import.meta.url)));
  if (tree === null) {
    t.skip(`git or the tag ${RELEASE_TAG} is absent`);
    return;
  }
  assert.deepEqual(errors(checkAll(CATALOGUE, tree, true).findings), []);
});

test('counts: parts, pads, wires, functions, stages, safety notes', () => {
  assert.equal(CATALOGUE.parts.length, 20);
  assert.equal(CATALOGUE.parts.filter((p) => p.selectable).length, 8);
  assert.deepEqual(CATALOGUE.parts.filter((p) => p.locked).map((p) => p.id), ['panel', 'copro', 'usb', 'wire']);
  assert.deepEqual(CATALOGUE.parts.filter((p) => p.testObject === true).map((p) => p.id), ['servo', 'esc']);
  assert.equal(CATALOGUE.pads.length, 40);
  assert.equal(CATALOGUE.pads.filter((p) => p.gpio !== null).length, 26);
  assert.deepEqual(CATALOGUE.wires.map((w) => w.n), Array.from({ length: 66 }, (_, i) => i + 1));
  assert.equal(CATALOGUE.functions.length, 22);
  // 13 functions need no part beyond the base setup: 10 run on every setup, 3 drive a test object.
  assert.equal(CATALOGUE.functions.filter((f) => f.needs.length === 0).length, 13);
  assert.deepEqual(CATALOGUE.functions.filter((f) => f.needs.length === 0 && f.drives !== undefined).map((f) => f.id), ['servo', 'servoprog', 'esc']);
  assert.deepEqual(
    CATALOGUE.functions.filter((f) => f.drives !== undefined).map((f) => [f.id, f.drives, f.needs]),
    [
      ['servo', 'servo', []], ['servotest', 'servo', ['pdmini']], ['servocur', 'servo', ['ina3221']],
      ['angle', 'servo', ['as5600']], ['servoprog', 'servo', []], ['esc', 'esc', []], ['escpow', 'esc', ['ina228']],
      ['stick', 'esc', ['pdmini']], ['tap', 'esc', ['sim', 'tap']],
    ],
  );
  assert.equal(CATALOGUE.rules.length, 7);
  assert.equal(CATALOGUE.checks.length, 43);
  assert.deepEqual(
    CATALOGUE.powerSources.map((s) => [s.id, s.feeds]),
    [
      ['usb', 'logic'], ['copro-5v', 'servo'], ['strip-v', 'servo'], ['pd-servo', 'servo'], ['lipo-2s', 'servo'],
      ['own-supply', 'servo'], ['esc-battery', 'esc'], ['pd-esc', 'esc'],
    ],
  );
  assert.equal(CATALOGUE.stages.length, 5);
  assert.equal(CATALOGUE.safety.length, 7);
  assert.equal(CATALOGUE.release, VERSION);
  assert.equal(CATALOGUE.hashVersion, 1);
});

/** A picture folder built from a map of names to bytes. */
function folderOf(files: Record<string, Uint8Array>): PictureFolder {
  return { names: Object.keys(files).sort(), read: (name) => files[name] ?? null };
}

/** A lossy WebP header of the given size, 30 bytes. */
function webp(width: number, height: number): Uint8Array {
  const b = new Uint8Array(30);
  b.set([...'RIFF'].map((ch) => ch.charCodeAt(0)), 0);
  b.set([...'WEBPVP8 '].map((ch) => ch.charCodeAt(0)), 8);
  b.set([0x9d, 0x01, 0x2a, width & 0xff, width >> 8, height & 0xff, height >> 8], 23);
  return b;
}

const svg = (body: string, size = 'width="480" height="400"'): Uint8Array =>
  new TextEncoder().encode(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 400" ${size}>${body}</svg>`);

test('the pictures of the parts are the files of the picture folder', () => {
  const folder = pictureFolder(PICTURE_DIR);
  assert.deepEqual(errors(checkPictures(CATALOGUE, folder)), []);
  const pictures = CATALOGUE.parts.flatMap((p) => p.pictures ?? []);
  assert.equal(pictures.filter((p) => p.kind === 'photo').length, 12);
  assert.equal(pictures.filter((p) => p.kind === 'drawing').length, 0);
  assert.equal(folder.names.length, 24);
  // The check tool runs the same check.
  assert.deepEqual(errors(checkAll(CATALOGUE, null, false, ANCHOR_TEXTS, folder).findings), []);
  reports(checkAll(CATALOGUE, null, false, ANCHOR_TEXTS, folderOf({})).findings,
    'part.panel.picture.01-panel-back', /01-panel-back\.webp is missing/);
});

test('a picture record is held to its files', () => {
  const one = (picture: Picture): Catalogue => {
    const c = copy();
    for (const part of c.parts) delete part.pictures;
    c.parts[0]!.pictures = [picture];
    return c;
  };
  const alt = { en: 'Panel', de: 'Panel' };
  const photo: Picture = { kind: 'photo', file: 'p', alt, width: 480, height: 300 };
  const good = { 'p.webp': webp(1600, 1000), 'p-thumb.webp': webp(480, 300) };
  const id = 'part.panel.picture.p';
  assert.deepEqual(errors(checkPictures(one(photo), folderOf(good))), []);

  reports(checkPictures(one(photo), folderOf({ 'p.webp': good['p.webp'] })), id, /p-thumb\.webp is missing/);
  reports(checkPictures(one(photo), folderOf({ 'p-thumb.webp': good['p-thumb.webp'] })), id, /p\.webp is missing/);
  reports(checkPictures(one(photo), folderOf({ ...good, 'p.webp': new TextEncoder().encode('JFIF'.repeat(10)) })), id, /p\.webp is not a WebP file/);
  reports(checkPictures(one(photo), folderOf({ ...good, 'p-thumb.webp': webp(640, 300) })), id, /640 px wide/);
  reports(checkPictures(one(photo), folderOf({ ...good, 'p-thumb.webp': webp(480, 301) })), id, /480 x 301 px; the record says 480 x 300/);
  reports(checkPictures(one(photo), folderOf({ ...good, 'p.jpg': webp(1, 1) })), 'picture.p.jpg', /no part names this file/);
  reports(checkPictures(one({ ...photo, alt: { en: 'Panel', de: ' ' } }), folderOf(good)), id, /no alt text in de/);
  reports(checkPictures(one({ ...photo, file: 'P.webp' }), folderOf(good)), 'part.panel.picture.P.webp', /file name/);
  reports(checkPictures(one({ ...photo, kind: 'scan' as 'photo' }), folderOf(good)), id, /kind scan/);
  reports(checkPictures(one({ ...photo, width: 0 }), folderOf(good)), id, /positive whole numbers/);

  // A file with an Exif or XMP chunk is refused.
  const extended = new Uint8Array(30);
  extended.set([...'RIFF'].map((ch) => ch.charCodeAt(0)), 0);
  extended.set([...'WEBPVP8X'].map((ch) => ch.charCodeAt(0)), 8);
  extended.set([0x08], 20);
  extended.set([0xdf, 0x01, 0x00, 0x2b, 0x01, 0x00], 24);
  assert.deepEqual(webpInfo(extended), { width: 480, height: 300, metadata: true });
  reports(checkPictures(one(photo), folderOf({ ...good, 'p-thumb.webp': extended })), id, /Exif or XMP/);
  assert.equal(webpInfo(new Uint8Array(10)), null);

  // More than 2 pictures for one set of options.
  const three = one(photo);
  three.parts[0]!.pictures = [photo, photo, { ...photo, only: { connection: 'osoyoo' } }];
  reports(checkPictures(three, folderOf(good)), 'part.panel', /3 pictures apply/);
  const split = one(photo);
  split.parts[0]!.pictures = [photo, { ...photo, only: { connection: 'pads' } }, { ...photo, only: { connection: 'osoyoo' } }];
  assert.deepEqual(errors(checkPictures(split, folderOf(good))), []);
  const value = one({ ...photo, only: { connection: 'rail' as 'pads' } });
  reports(checkRefs(value), 'part.panel.picture.p', /connection=rail/);
});

test('a drawing is an SVG file without script, reference or raster', () => {
  const drawing: Picture = { kind: 'drawing', file: 'd', alt: { en: 'Board', de: 'Platine' }, width: 480, height: 400 };
  const c = copy();
  for (const part of c.parts) delete part.pictures;
  c.parts[0]!.pictures = [drawing];
  const id = 'part.panel.picture.d';
  const run = (bytes: Uint8Array): Finding[] => checkPictures(c, folderOf({ 'd.svg': bytes }));

  assert.deepEqual(errors(run(svg('<defs><g id="a"><rect width="4" height="4" fill="url(#g)"/></g></defs><use href="#a"/>'))), []);
  reports(run(svg('<script>alert(1)</script>')), id, /holds a script/);
  reports(run(svg('<rect onload="x()"/>')), id, /event attribute/);
  reports(run(svg('<image href="#a"/>')), id, /loads or embeds/);
  reports(run(svg('<foreignObject/>')), id, /loads or embeds/);
  reports(run(svg('<use href="other.svg#a"/>')), id, /refers to "other\.svg#a"/);
  reports(run(svg('<use xlink:href="https://example.org/a.svg#a"/>')), id, /refers to "https:\/\/example\.org/);
  reports(run(svg('<rect fill="url(pattern.svg#p)"/>')), id, /refers to "pattern\.svg#p"/);
  reports(run(svg('<rect fill="url(data:image/png;base64,AAAA)"/>')), id, /data URL/);
  reports(run(svg('<text>//example.org/x</text>')), id, /names the address/);
  reports(run(svg('', 'width="480" height="338"')), id, /480 x 338 px; the record says 480 x 400/);
  reports(run(new TextEncoder().encode('<html><svg width="480" height="400"></svg></html>')), id, /is no SVG file/);
  reports(run(new Uint8Array([0xff, 0xfe, 0xfd])), id, /not UTF-8/);
  reports(checkPictures(c, folderOf({})), id, /d\.svg is missing/);
  // A WebP file does not stand in for a drawing, and an SVG file not for a photo.
  reports(checkPictures(c, folderOf({ 'd.webp': webp(480, 400), 'd-thumb.webp': webp(480, 400) })), id, /d\.svg is missing/);
  c.parts[0]!.pictures = [{ ...drawing, kind: 'photo' }];
  reports(checkPictures(c, folderOf({ 'd.svg': svg('') })), id, /d\.webp is missing/);
});

test('every unknown of the data is a check entry of kind unknown', () => {
  const unknown = CATALOGUE.checks.filter((k) => k.kind === 'unknown').map((k) => k.id);
  assert.deepEqual(unknown, [
    'pd-shared', 'ina3221-pullups', 'ina228-xt90-pd', 'as5600-board',
    'osoyoo-labels', 'prog-level', 'servo-5v-pin', 'strip-supply', 'esc-programming-power',
  ]);
  const unconfirmed = CATALOGUE.parts.flatMap((part) =>
    part.pins.filter((p) => !p.confirmed).map((p) => `${part.id}.${p.id}`));
  assert.deepEqual(unconfirmed, ['ina3221.LP', 'as5600.GPO']);
  // No entry states whether a part or a function has run on hardware: the page states that once, for the project.
  for (const entry of [...CATALOGUE.parts, ...CATALOGUE.functions]) assert.ok(!('hardware' in entry), entry.id);
  assert.deepEqual(CATALOGUE.checks.filter((k) => k.id.endsWith('-state')).map((k) => k.id), []);
  // No part is without a stated role. The pins with one option value only are
  // the 2 supply pins of the INA228 board and the 2 minus poles of the module
  // with XT90 plugs.
  assert.deepEqual(CATALOGUE.parts.filter((p) => p.role === null && p.selectable).map((p) => p.id), []);
  assert.deepEqual(
    CATALOGUE.parts.flatMap((p) => p.pins.filter((q) => q.only !== undefined).map((q) => [`${p.id}.${q.id}`, q.only])),
    [
      ['ina228.V5', { ina228Board: 'matek' }], ['ina228.V3', { ina228Board: 'xt90' }],
      ['ina228.BATN', { ina228Board: 'xt90' }], ['ina228.ESCN', { ina228Board: 'xt90' }],
    ],
  );
});

// --- invariant 1 ------------------------------------------------------------

test('ids: a repeated part id, a bad pattern and a wire number out of order are reported', () => {
  const twice = copy();
  twice.parts[5]!.id = 'servo';
  reports(checkIds(twice), 'part.servo', /twice/);

  const pattern = copy();
  pattern.functions[SERVO]!.id = 'Servo_1';
  reports(checkIds(pattern), 'function.Servo_1', /match/);

  const pin = copy();
  pin.parts[0]!.pins[1]!.id = 'CANH';
  reports(checkIds(pin), 'part.panel.CANH', /twice/);

  const lower = copy();
  lower.parts[0]!.pins[0]!.id = 'canh';
  reports(checkIds(lower), 'part.panel.canh', /match/);

  const order = copy();
  order.wires[10]!.n = 3;
  reports(checkIds(order), 'wire.3', /ascending/);

  const signal = copy();
  signal.pinDefaults[1]!.signal = 'heartbeat';
  reports(checkIds(signal), 'pin.heartbeat', /twice/);
});

// --- invariants 2, 4, 9 -----------------------------------------------------

test('wire ends: an undeclared pin, a pad out of range and a reserved pad are reported', () => {
  const pin = copy();
  pin.wires[6]!.b = 'servo.SIGNAL';
  reports(checkEnds(pin), 'wire.7', /not a declared pin/);

  const part = copy();
  part.wires[6]!.b = 'gimbal.SIG';
  reports(checkEnds(part), 'wire.7', /not a declared pin/);

  const deep = copy();
  deep.wires[6]!.b = 'servo.SIG.X';
  reports(checkEnds(deep), 'wire.7', /not a declared pin/);

  const pad = copy();
  pad.wires[6]!.a = 'pad.41';
  reports(checkEnds(pad), 'wire.7', /not a pad/);

  const zero = copy();
  zero.wires[6]!.a = 'pad.0';
  reports(checkEnds(zero), 'wire.7', /not a pad/);

  // Pad 11 is GP8, a CAN pin of the board.
  const reserved = copy();
  reserved.wires[6]!.a = 'pad.11';
  reports(checkEnds(reserved), 'wire.7', /reserved/);
});

test('wire shape: stub, wire and component each need their fields', () => {
  const stub = copy();
  delete stub.wires[12]!.to;
  reports(checkEnds(stub), 'wire.13', /`to`/);

  const stubEnd = copy();
  stubEnd.wires[12]!.b = 'esc.SG';
  reports(checkEnds(stubEnd), 'wire.13', /no second end/);

  const open = copy();
  open.wires[0]!.b = null;
  reports(checkEnds(open), 'wire.1', /two ends/);

  const component = copy();
  delete component.wires[52]!.netB;
  reports(checkEnds(component), 'wire.53', /netB/);

  const route = copy();
  route.wires[0]!.route = { kind: 'S' };
  reports(checkRoutes(route), 'wire.1', /stub/);
});

test('a wire on a pin of one board variant requires that variant', () => {
  // The 2 supply pins of the INA228 are such pins: 5V on the MATEK board, V
  // on the module with XT90 plugs. The 2 minus poles of the plugs are pins of
  // the module with XT90 plugs only.
  const pins = CATALOGUE.parts.find((p) => p.id === 'ina228')!.pins;
  assert.deepEqual(pins.find((p) => p.id === 'V5')?.only, { ina228Board: 'matek' });
  assert.deepEqual(pins.find((p) => p.id === 'V3')?.only, { ina228Board: 'xt90' });
  assert.deepEqual(pins.filter((p) => p.only).map((p) => p.id), ['V5', 'V3', 'BATN', 'ESCN']);
  assert.deepEqual(checkEnds(CATALOGUE), []);
  // Wire 41 without the variant in its condition.
  const copy = (): Catalogue => {
    const c = structuredClone(CATALOGUE);
    assert.equal(c.wires[40]!.n, 41);
    c.wires[40]!.when = { has: 'ina228' };
    return c;
  };
  const c = copy();
  reports(checkEnds(c), 'wire.41', /ina228Board=matek/);

  const required = copy();
  required.wires[40]!.when = { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'matek' }] };
  assert.deepEqual(checkEnds(required), []);

  const other = copy();
  other.wires[40]!.when = { all: [{ has: 'ina228' }, { opt: 'ina228Board', is: 'xt90' }] };
  reports(checkEnds(other), 'wire.41', /ina228Board=matek/);

  // An alternative that holds without the variant does not require it.
  const any = copy();
  any.wires[40]!.when = { any: [{ opt: 'ina228Board', is: 'matek' }, { has: 'ina228' }] };
  reports(checkEnds(any), 'wire.41', /ina228Board=matek/);

  const always = copy();
  always.wires[40]!.when = null;
  reports(checkEnds(always), 'wire.41', /ina228Board=matek/);

  // Wire 55 on the pin of the module with XT90 plugs.
  const xt90 = structuredClone(CATALOGUE);
  assert.equal(xt90.wires[54]!.n, 55);
  xt90.wires[54]!.when = { has: 'ina228' };
  reports(checkEnds(xt90), 'wire.55', /ina228Board=xt90/);
});

// --- invariant 3 ------------------------------------------------------------

test('references: unknown parts and option values are reported', () => {
  const needs = copy();
  needs.functions.find((f) => f.id === 'servo')!.needs = ['servo', 'gimbal'];
  reports(checkRefs(needs), 'function.servo', /unknown part gimbal/);

  const servo = (c: Catalogue): Catalogue['functions'][number] => c.functions.find((f) => f.id === 'servo')!;
  const empty = copy();
  servo(empty).needs = [];
  assert.deepEqual(checkRefs(empty).filter((f) => f.id === 'function.servo'), []);
  // A function that drives a test object is in no setup of its own accord: it may force an option.
  servo(empty).forces = { escPower: 'pd' };
  assert.deepEqual(checkRefs(empty).filter((f) => f.id === 'function.servo'), []);
  const link = empty.functions.find((f) => f.id === 'link')!;
  link.forces = { escPower: 'pd' };
  reports(checkRefs(empty), 'function.link', /needs no part, drives no test object and forces an option/);

  const adds = copy();
  adds.rules[0]!.adds = 'gimbal';
  reports(checkRefs(adds), 'rule.esc-pd', /unknown part gimbal/);

  const locked = copy();
  locked.rules[0]!.adds = 'panel';
  reports(checkRefs(locked), 'rule.esc-pd', /locked/);

  const has = copy();
  has.wires[6]!.when = { all: [{ has: 'servo' }, { not: { any: [{ has: 'gimbal' }] } }] };
  reports(checkRefs(has), 'wire.7', /unknown part gimbal/);

  const value = copy();
  value.checks[0]!.when = { opt: 'escPower', is: 'batt' };
  reports(checkRefs(value), 'check.can-60', /escPower=batt/);

  const forces = copy();
  forces.functions[6]!.forces = { escPower: 'mains' as 'pd' };
  reports(checkRefs(forces), 'function.stick', /escPower=mains/);

  const clash = copy();
  clash.functions[SERVO]!.forces = { escPower: 'battery' };
  reports(checkRefs(clash), 'function.stick', /another function forces/);

  const only = copy();
  only.parts.find((p) => p.id === 'ina228')!.pins[0]!.only = { ina228Board: 'bifrc' as 'xt90' };
  reports(checkRefs(only), 'part.ina228.V5', /ina228Board=bifrc/);

  const variant = copy();
  variant.parts.find((p) => p.id === 'ina228')!.variantName!.names['bifrc'] = { en: 'Board', de: 'Platine' };
  reports(checkRefs(variant), 'part.ina228', /ina228Board=bifrc/);

  const release = copy();
  release.parts[4]!.release = 'planned';
  reports(checkRefs(release), 'part.servo', /released/);

  const both = copy();
  both.parts[0]!.selectable = true;
  reports(checkRefs(both), 'part.panel', /locked/);
});

test('references: a test object is driven, never needed, added, locked or ticked', () => {
  assert.deepEqual(checkRefs(CATALOGUE).filter((f) => f.level === 'error'), []);
  const fn = (c: Catalogue, id: string): Catalogue['functions'][number] => c.functions.find((f) => f.id === id)!;
  const part = (c: Catalogue, id: string): Catalogue['parts'][number] => c.parts.find((x) => x.id === id)!;

  const needs = copy();
  fn(needs, 'servotest').needs = ['servo', 'pdmini'];
  reports(checkRefs(needs), 'function.servotest', /needs the test object servo/);

  const drives = copy();
  fn(drives, 'servotest').drives = 'pdmini';
  reports(checkRefs(drives), 'function.servotest', /drives pdmini, which is no test object/);
  fn(drives, 'servotest').drives = 'gimbal';
  reports(checkRefs(drives), 'function.servotest', /drives gimbal, which is no test object/);

  const adds = copy();
  adds.rules[0]!.adds = 'servo';
  reports(checkRefs(adds), `rule.${adds.rules[0]!.id}`, /adds the test object servo/);

  for (const key of ['locked', 'selectable'] as const) {
    const fixed = copy();
    part(fixed, 'esc')[key] = true;
    reports(checkRefs(fixed), 'part.esc', /a test object is neither locked nor selectable/);
  }

  // A test object needs a function that drives it on the base setup: the tick that connects it.
  const noTick = copy();
  fn(noTick, 'servo').needs = ['sd'];
  fn(noTick, 'servoprog').needs = ['sd'];
  reports(checkRefs(noTick), 'part.servo', /without a function that drives it on the base setup/);
  const undriven = copy();
  delete fn(undriven, 'esc').drives;
  reports(checkRefs(undriven), 'part.esc', /without a function that drives it on the base setup/);
});

test('references: a power source feeds the logic or a test object, with an advantage, a drawback and ordered numbers', () => {
  const source = (c: Catalogue, id: string): Catalogue['powerSources'][number] => c.powerSources.find((x) => x.id === id)!;

  const twice = copy();
  twice.powerSources.push(structuredClone(source(twice, 'usb')));
  reports(checkRefs(twice), 'power.usb', /twice/);

  const feeds = copy();
  source(feeds, 'lipo-2s').feeds = 'pdmini' as never;
  reports(checkRefs(feeds), 'power.lipo-2s', /feeds pdmini, which is neither the logic nor a test object/);

  // The 2 external servo supplies are one choice; a choice holds 2 or more sources of one test object.
  assert.deepEqual(CATALOGUE.powerSources.filter((s) => s.choice !== undefined).map((s) => [s.id, s.choice]),
    [['lipo-2s', 'servo-supply'], ['own-supply', 'servo-supply']]);
  const single = copy();
  delete source(single, 'own-supply').choice;
  reports(checkRefs(single), 'power.lipo-2s', /the choice servo-supply holds 1 source/);
  const mixed = copy();
  source(mixed, 'esc-battery').choice = 'servo-supply';
  reports(checkRefs(mixed), 'power.lipo-2s', /the choice servo-supply feeds more than 1 thing/);

  const pros = copy();
  source(pros, 'copro-5v').pros = [];
  reports(checkRefs(pros), 'power.copro-5v', /no advantage/);
  const cons = copy();
  source(cons, 'copro-5v').cons = [];
  reports(checkRefs(cons), 'power.copro-5v', /no drawback/);

  const volts = copy();
  source(volts, 'pd-esc').volts = { min: 20, max: 1 };
  reports(checkRefs(volts), 'power.pd-esc', /voltage range/);
  source(volts, 'pd-esc').volts = { min: 0, max: 20 };
  reports(checkRefs(volts), 'power.pd-esc', /voltage range/);
  const amps = copy();
  source(amps, 'pd-esc').amps = { min: 3, max: 0.05 };
  reports(checkRefs(amps), 'power.pd-esc', /current range/);
  source(amps, 'pd-esc').amps = { min: null, max: 0 } as never;
  reports(checkRefs(amps), 'power.pd-esc', /current range/);

  const basis = copy();
  const bare = source(basis, 'pd-esc');
  bare.volts = null;
  bare.amps = null;
  assert.notEqual(bare.basis, null);
  reports(checkRefs(basis), 'power.pd-esc', /a basis without a number/);

  const when = copy();
  source(when, 'strip-v').when = { has: 'gimbal' };
  reports(checkRefs(when), 'power.strip-v', /gimbal/);
  const used = copy();
  source(used, 'strip-v').used = { opt: 'connection', is: 'rail' } as never;
  reports(checkRefs(used), 'power.strip-v', /connection=rail/);

  // Every test object has a source, and so has the logic.
  const none = copy();
  none.powerSources = none.powerSources.filter((x) => x.feeds !== 'esc');
  reports(checkRefs(none), 'part.esc', /a test object without a power source/);
  const logic = copy();
  logic.powerSources = logic.powerSources.filter((x) => x.feeds !== 'logic');
  reports(checkRefs(logic), 'power', /no power source of the logic/);
});

/** Ids of the parts a condition names with `has`, outside every `not`. */
function causes(cond: unknown): string[] {
  if (cond === null || typeof cond !== 'object') return [];
  const c = cond as { has?: string; all?: unknown[]; any?: unknown[] };
  if (typeof c.has === 'string') return [c.has];
  return [...(c.all ?? []), ...(c.any ?? [])].flatMap(causes);
}

/** The part ids at the ends of a wire. A pad of the coprocessor is no part. */
const partsOn = (w: Catalogue['wires'][number]): string[] =>
  [w.a, w.b].filter((e): e is string => e !== null && !/^(pad|can)\./.test(e)).map((e) => e.split('.')[0]!);

/** The nets of a wire. Ground joins every part of the bench, so it shows no cause. */
const netsOn = (w: Catalogue['wires'][number]): string[] => [w.net, ...(w.netB ? [w.netB] : [])].filter((n) => n !== 'gnd');

/**
 * Nets that a part outside the data file joins to another net. The XL1509
 * converter U9 of the Osoyoo breakout board feeds the V pins (net `strip_v`)
 * from its DC input (net `osoyoo_vin`) while the jumper V Selector sits on 5V.
 */
const THROUGH: Readonly<Record<string, string>> = { osoyoo_vin: 'strip_v' };

/**
 * Whether a wire joins part `p` to part `q`, or to a net other than ground that
 * a wire of `q` is on, directly or through a net of `through`.
 */
function joined(c: Catalogue, p: string, q: string, through: Readonly<Record<string, string>> = THROUGH): boolean {
  const nets = new Set(c.wires.filter((w) => partsOn(w).includes(q)).flatMap(netsOn));
  const reaches = (n: string): boolean => nets.has(n) || (through[n] !== undefined && nets.has(through[n]!));
  return c.wires.some((w) => partsOn(w).includes(p) && (partsOn(w).includes(q) || netsOn(w).some(reaches)));
}

/** Whether a wire has one end on part `p` and the other on part `q`. */
const direct = (c: Catalogue, p: string, q: string): boolean =>
  c.wires.some((w) => partsOn(w).includes(p) && partsOn(w).includes(q));

/** The parts a rule adds for another part without a wire to that part or its net. */
function looseAdds(c: Catalogue, through: Readonly<Record<string, string>> = THROUGH): string[] {
  const out: string[] = [];
  for (const rule of c.rules) {
    const from = causes(rule.when).filter((id) => id !== rule.adds);
    if (from.length > 0 && !from.some((id) => joined(c, rule.adds, id, through))) out.push(`${rule.id}: ${rule.adds} has no wire to ${from.join(', ')}`);
  }
  return out;
}

/** The parts that sit between 2 parts without a wire to each of them. */
function looseBetween(c: Catalogue): string[] {
  const out: string[] = [];
  for (const part of c.parts) {
    const role = `${part.role?.en ?? ''} ${part.role?.de ?? ''}`;
    if (/sits between|sitzt zwischen/i.test(role) && !part.between) out.push(`${part.id}: the role says "between" and the part names no 2 parts`);
    for (const id of part.between ?? []) {
      if (!direct(c, part.id, id)) out.push(`${part.id}: no wire to ${id}`);
    }
  }
  return out;
}

test('a part that a rule adds for another part has a wire to that part or to its net', () => {
  assert.deepEqual(looseAdds(CATALOGUE), []);
  // Every rule names a part as its cause, so every rule is held to this.
  for (const rule of CATALOGUE.rules) assert.ok(causes(rule.when).length > 0, rule.id);
  // The PD charger is joined to the PD mini by the net of the T-plug, the T-plug by a wire.
  assert.ok(!direct(CATALOGUE, 'charger', 'pdmini'));
  assert.ok(joined(CATALOGUE, 'charger', 'pdmini'));
  assert.ok(direct(CATALOGUE, 'usbtap', 'pdmini'));

  // Without the wire from the socket OUT, the T-plug keeps its UART wires to pads and its ground, and has none to the PD mini.
  const cut = copy();
  cut.wires = cut.wires.filter((w) => w.n !== 63);
  assert.deepEqual(looseAdds(cut), ['pd-charger: charger has no wire to pdmini', 'pd-tplug: usbtap has no wire to pdmini']);
  const alone = copy();
  alone.wires = alone.wires.filter((w) => w.n !== 19);
  assert.deepEqual(looseAdds(alone), ['pd-charger: charger has no wire to pdmini']);

  // The DC supply of the Osoyoo breakout board reaches the servo through the converter of the board:
  // its plus lead on the DC input, the servo plus on the V pin. Without that bridge, or without the
  // plus lead, the rule holds a part with no wire to its cause.
  assert.ok(!direct(CATALOGUE, 'dcsup', 'servo'));
  assert.ok(joined(CATALOGUE, 'dcsup', 'servo'));
  assert.deepEqual(looseAdds(CATALOGUE, {}), ['osoyoo-dc: dcsup has no wire to servo']);
  const lead = copy();
  lead.wires = lead.wires.filter((w) => w.n !== 65);
  assert.deepEqual(looseAdds(lead), ['osoyoo-dc: dcsup has no wire to servo']);
});

test('a part that sits between 2 parts has a wire to each of them', () => {
  assert.deepEqual(looseBetween(CATALOGUE), []);
  const plug = CATALOGUE.parts.find((p) => p.id === 'usbtap')!;
  assert.deepEqual(plug.between, ['charger', 'pdmini']);
  assert.deepEqual(plug.pins.map((p) => p.id), ['OUT', 'IN', 'G', 'DP', 'DM', 'V']);
  // The path as it is built: charger to IN, OUT to the PD mini, on 1 net.
  const path = CATALOGUE.wires.filter((w) => w.net === 'pd_in').map((w) => [w.n, w.a, w.b, w.kind]);
  assert.deepEqual(path, [[19, 'charger.USB', 'usbtap.IN', 'wire'], [63, 'usbtap.OUT', 'pdmini.USB', 'wire']]);
  for (const w of CATALOGUE.wires.filter((x) => x.net === 'pd_in')) {
    assert.ok(!/T-plug|T-Stecker/.test(`${w.note?.en} ${w.note?.de}`), `wire ${w.n} names the T-plug in its note`);
  }

  const noIn = copy();
  noIn.wires.find((w) => w.n === 19)!.b = 'pdmini.USB';
  assert.deepEqual(looseBetween(noIn), ['usbtap: no wire to charger']);
  const noOut = copy();
  noOut.wires = noOut.wires.filter((w) => w.n !== 63);
  assert.deepEqual(looseBetween(noOut), ['usbtap: no wire to pdmini']);
  const unnamed = copy();
  delete unnamed.parts.find((p) => p.id === 'usbtap')!.between;
  assert.deepEqual(looseBetween(unnamed), ['usbtap: the role says "between" and the part names no 2 parts']);
});

// --- invariant 5: text ------------------------------------------------------

test('text: a missing German string is reported', () => {
  const missing = copy();
  delete (missing.parts[4]!.name as Partial<{ en: string; de: string }>).de;
  reports(checkTexts(missing), 'parts.servo.name.de', /missing or empty/);

  const empty = copy();
  empty.wires[0]!.signal.de = '';
  reports(checkTexts(empty), 'wires.1.signal.de', /missing or empty/);

  const blank = copy();
  blank.checks[0]!.text.de = '   ';
  reports(checkTexts(blank), 'checks.can-60.text.de', /missing or empty/);

  const english = copy();
  english.baseFunction.en = '';
  reports(checkTexts(english), 'baseFunction.en', /missing or empty/);

  const extra = copy();
  (extra.baseFunction as Record<string, string>)['fr'] = 'Interface';
  reports(checkTexts(extra), 'baseFunction', /key fr/);
});

test('text: exclamation marks, banned words, foreign characters and tags are reported', () => {
  assert.deepEqual(checkString('x', 'Twist CAN H and CAN L together.', false), []);
  reports(checkString('x', 'No propeller!', false), 'x', /exclamation/);
  for (const word of BANNED_WORDS) {
    reports(checkString('x', `A ${word} thing.`, false), 'x', new RegExp(`"${word}"`));
    reports(checkString('x', `${word[0]!.toUpperCase()}${word.slice(1)}, it is.`, false), 'x', /word/);
  }
  // Whole words only: "adjust" and "Vereinfachung" hold a banned word and pass.
  assert.deepEqual(checkString('x', 'Adjust the trim. Vereinfachung, justieren.', false), []);

  reports(checkString('x', 'Fertig \u{1F600}', false), 'x', /U\+1F600/);
  reports(checkString('x', 'two\nlines', false), 'x', /U\+000A/);
  reports(checkString('x', 'tab\there', false), 'x', /U\+0009/);
  reports(checkString('x', 'CJK 中', false), 'x', /U\+4E2D/);
  assert.deepEqual(checkString('x', '120 Ω, 200 µΩ, 2 × 2,2 kΩ, −1, SETUP → OUTPUTS, 0,5 V – „Text“', false), []);

  reports(checkString('x', ' leading', false), 'x', /white space/);
  reports(checkString('x', '<b>Bold</b> in a wire note.', false), 'x', /tag <b>/);
  reports(checkString('x', 'A <i>tag</i> in a safety note.', true), 'x', /tag <i>/);
  reports(checkString('x', '<b>Open and never closed.', true), 'x', /unbalanced/);
  reports(checkString('x', '<a href="x">link</a>', true), 'x', /tag <a href/);
  assert.deepEqual(checkString('x', '<b>No propeller.</b> The motor screwed down.', true), []);
  // A comparison is not a tag.
  assert.deepEqual(checkString('x', 'Output < input, 3 > 2.', false), []);
  reports(checkString('x', undefined, false), 'x', /missing/);
  reports(checkString('x', 7, false), 'x', /missing/);
});

test('text: the walk reaches every kind of string', () => {
  const ids = collectTexts(CATALOGUE).map((t) => t.id);
  for (const id of [
    'parts.panel.name', 'parts.panel.short', 'parts.panel.role', 'parts.panel.slot.sub',
    'parts.panel.pins.CANH.label', 'parts.ina228.variantName.names.xt90', 'pads.11.reserved',
    'wires.1.signal', 'wires.1.note', 'wires.13.to', 'functions.servo.name',
    'rules.esc-pd.because', 'checks.can-60.text', 'stages.1.title', 'safety.0.text',
    'notInRelease.knob.text', 'baseFunction', 'powerSources.usb.label', 'powerSources.usb.text',
    'powerSources.copro-5v.pros.0', 'powerSources.copro-5v.cons.0', 'powerSources.copro-5v.unknowns.0',
    'powerSources.pd-servo.basis',
  ]) {
    assert.ok(ids.includes(id), `${id} is not walked`);
  }
  assert.equal(new Set(ids).size, ids.length);

  const safety = copy();
  safety.safety[0]!.text.en = 'No propeller!';
  reports(checkTexts(safety), 'safety.0.text.en', /exclamation/);

  const wire = copy();
  wire.wires[0]!.note = { en: '<b>Twist</b> the pair.', de: 'Das Paar verdrillen.' };
  reports(checkTexts(wire), 'wires.1.note.en', /tag/);
});

// --- invariant 5: source anchors --------------------------------------------

test('src: every item carries one', () => {
  const anchors = collectAnchors(CATALOGUE);
  assert.ok(anchors.every((a) => a.src !== undefined));
  const pins = CATALOGUE.parts.reduce((n, p) => n + p.pins.length, 0);
  assert.equal(
    anchors.length,
    CATALOGUE.parts.length + pins + CATALOGUE.pads.length + CATALOGUE.wires.length
      + CATALOGUE.functions.length + CATALOGUE.rules.length + CATALOGUE.checks.length
      + CATALOGUE.stages.length + CATALOGUE.safety.length + CATALOGUE.notInRelease.length
      + CATALOGUE.pinDefaults.length + CATALOGUE.powerSources.length,
  );

  const c = copy();
  delete (c.wires[0] as Partial<Catalogue['wires'][number]>).src;
  reports(checkSrc(c, null).findings, 'wire.1', /no src/);
});

test('src: a doc source needs a title, an https address with a path and a reference', () => {
  const at = (src: Src): Finding[] => {
    const c = copy();
    c.functions[SERVO]!.src = src;
    return checkSrc(c, null).findings;
  };
  const ok = { kind: 'doc', title: 'Schematic', url: 'https://example.com/a.pdf', ref: 'page 2' } as const;
  assert.deepEqual(at(ok), []);
  reports(at({ ...ok, title: '' }), 'function.servo', /src "doc" has no title/);
  reports(at({ ...ok, url: 'http://example.com/a.pdf' }), 'function.servo', /no https address/);
  reports(at({ ...ok, url: 'https://example.com' }), 'function.servo', /no https address/);
  reports(at({ ...ok, url: 'https://example.com/a b.pdf' }), 'function.servo', /no https address/);
  reports(at({ ...ok, url: 'https:///a.pdf' }), 'function.servo', /no https address/);
  reports(at({ ...ok, ref: '' }), 'function.servo', /names no page, net or table/);
});

test('src: a broken repository anchor is reported', () => {
  const tree = fakeTree(CATALOGUE);

  const path = copy();
  path.functions[SERVO]!.src = { kind: 'repo', path: 'docs/Gone.md', line: 3, tag: TAG };
  reports(checkSrc(path, tree).findings, 'function.servo', /does not exist/);

  const line = copy();
  line.functions[SERVO]!.src = { kind: 'repo', path: 'STATUS.md', line: 2001, tag: TAG };
  reports(checkSrc(line, tree).findings, 'function.servo', /past the end/);
  line.functions[SERVO]!.src = { kind: 'repo', path: 'STATUS.md', line: 2000, tag: TAG };
  const lastLine = [...ANCHOR_TEXTS, { path: 'STATUS.md', line: 2000, has: 'text' }];
  assert.deepEqual(checkSrc(line, tree, lastLine).findings, []);

  const zero = copy();
  zero.functions[SERVO]!.src = { kind: 'repo', path: 'STATUS.md', line: 0, tag: TAG };
  reports(checkSrc(zero, tree).findings, 'function.servo', /no line number/);

  const blank = copy();
  const emptyLine = fakeTree(CATALOGUE, (files) => {
    files.set('STATUS.md', ['# Status', '', 'text']);
  });
  blank.functions[SERVO]!.src = { kind: 'repo', path: 'STATUS.md', line: 2, tag: TAG };
  reports(checkSrc(blank, emptyLine).findings, 'function.servo', /empty line/);
  blank.functions[SERVO]!.src = { kind: 'repo', path: 'STATUS.md', line: 4, tag: TAG };
  reports(checkSrc(blank, emptyLine).findings, 'function.servo', /STATUS\.md:4 is past the end of the file \(3 lines\)/);

  const tag = copy();
  tag.functions[SERVO]!.src = { kind: 'repo', path: 'STATUS.md', line: 3, tag: 'v0.16.0'  };
  reports(checkSrc(tag, tree).findings, 'function.servo', /v0\.16\.0/);

  const absolute = copy();
  absolute.functions[SERVO]!.src = { kind: 'repo', path: '/etc/passwd', line: 1, tag: TAG };
  reports(checkSrc(absolute, tree).findings, 'function.servo', /no repository path/);
});

test('src: a repository anchor names a text, and the line at the tag holds it', () => {
  const tree = fakeTree(CATALOGUE);
  const moved = copy();
  const at = (line: number): Catalogue['functions'][number]['src'] => (
    { kind: 'repo', path: 'docs/Building.md', line, tag: TAG }
  );

  // A line that is present and holds another row: no text is named for it.
  moved.functions[SERVO]!.src = at(118);
  reports(checkSrc(moved, tree).findings, 'function.servo', /Building\.md:118 names no text of 4 characters or more/);
  reports(checkSrc(moved, null).findings, 'function.servo', /Building\.md:118 names no text/);
  reports(checkAll(moved, tree, true).findings, 'function.servo', /names no text/);

  // A named text that the line does not hold.
  const wrong = [...ANCHOR_TEXTS, { path: 'docs/Building.md', line: 118, has: '| PGO | GND |' }];
  reports(checkSrc(moved, tree, wrong).findings, 'function.servo', /Building\.md:118 does not hold "\| PGO \| GND \|" at v0\.17\.0/);
  reports(checkAll(moved, tree, true, wrong).findings, 'function.servo', /does not hold/);
  // Without the tree the text is not compared.
  assert.deepEqual(checkSrc(moved, null, wrong).findings, []);

  // Every text named for the line is compared.
  const rows = fakeTree(CATALOGUE, (files) => {
    lines(files, 'docs/Building.md')[117] = '|  DIR |   GND or VCC, never open |';
  });
  const dir = [...ANCHOR_TEXTS, { path: 'docs/Building.md', line: 118, has: '| DIR | GND or VCC' }];
  assert.deepEqual(checkSrc(moved, rows, dir).findings, []);
  reports(checkSrc(moved, rows, [...dir, ...wrong]).findings, 'function.servo', /does not hold "\| PGO/);

  // A text under the shortest length names nothing.
  assert.equal(ANCHOR_TEXT_MIN, 4);
  const short = [...ANCHOR_TEXTS, { path: 'docs/Building.md', line: 118, has: ' |  D ' }];
  reports(checkSrc(moved, rows, short).findings, 'function.servo', /names no text/);
  const none = [...ANCHOR_TEXTS, { path: 'docs/Building.md', line: 118 } as { path: string; line: number; has: string }];
  reports(checkSrc(moved, rows, none).findings, 'function.servo', /names no text/);

  // A row of the data moved off its line in the tree.
  const shifted = fakeTree(CATALOGUE, (files) => {
    const doc = lines(files, 'docs/Building.md');
    doc.splice(117, 0, '| DIR | GND or VCC, never open |');
  });
  const found = checkSrc(CATALOGUE, shifted).findings;
  reports(found, 'part.as5600.GPO', /Building\.md:119 does not hold "\| PGO \| GND \|"/);
  reports(found, 'part.as5600.OUT', /Building\.md:120 does not hold/);
  reports(found, 'wire.54', /Building\.md:119 does not hold/);
  reports(found, 'check.as5600-board', /Building\.md:119 does not hold/);
});

test('src: every repository anchor of the data names a text of its own', () => {
  const anchors = collectAnchors(CATALOGUE).filter((a) => a.src?.kind === 'repo');
  assert.equal(ANCHOR_TEXTS.length, anchors.length);
  assert.ok(Object.isFrozen(ANCHOR_TEXTS));
  for (const text of ANCHOR_TEXTS) {
    assert.ok(text.has.trim().length >= ANCHOR_TEXT_MIN, `${text.path}:${text.line}`);
    assert.ok(Object.isFrozen(text));
  }
  // The AS5600 rows of docs/Building.md at the tag: 118 DIR, 119 PGO, 120 OUT.
  const as5600 = CATALOGUE.parts.find((p) => p.id === 'as5600');
  const line = (pin: string): number | null => {
    const src = as5600?.pins.find((p) => p.id === pin)?.src;
    return src?.kind === 'repo' ? src.line : null;
  };
  assert.equal(line('GPO'), 119);
  assert.equal(line('OUT'), 120);
  const strap = CATALOGUE.wires.find((w) => w.n === 54)?.src;
  assert.deepEqual(strap, { kind: 'repo', path: 'docs/Building.md', line: 119, tag: TAG });
  const board = CATALOGUE.checks.find((k) => k.id === 'as5600-board')?.src;
  assert.deepEqual(board, strap);
});

test('src: guide, photo and page rules', () => {
  const section = copy();
  section.stages[0]!.src = { kind: 'guide', section: 'stage-6' };
  reports(checkSrc(section, null).findings, 'stage.1', /stage-6/);

  // Photo 09 has no check entry of kind unknown.
  const photo = copy();
  photo.parts[4]!.pins[0]!.src = { kind: 'photo', file: '09' };
  reports(checkSrc(photo, null).findings, 'part.servo.SIG', /photo 09/);

  // Photo 05 has one, until its kind changes.
  const kind = copy();
  kind.parts[4]!.pins[0]!.src = { kind: 'photo', file: '05' };
  assert.deepEqual(checkSrc(kind, null).findings, []);
  kind.checks.find((k) => k.id === 'osoyoo-labels')!.kind = 'note';
  reports(checkSrc(kind, null).findings, 'part.servo.SIG', /photo 05/);

  // An owner statement is named by a date of the list.
  const stated = copy();
  assert.deepEqual(stated.parts.find((p) => p.id === 'usbtap')?.src, { kind: 'owner', date: '2026-10-11' });
  assert.deepEqual(OWNER_STATEMENTS, ['2026-10-11']);
  stated.parts[4]!.src = { kind: 'owner', date: '2026-10-11' };
  assert.deepEqual(checkSrc(stated, null).findings, []);
  stated.parts[4]!.src = { kind: 'owner', date: '2026-10-12' };
  reports(checkSrc(stated, null).findings, 'part.servo', /owner statement "2026-10-12"/);
  stated.parts[4]!.src = { kind: 'owner' } as unknown as Catalogue['parts'][number]['src'];
  reports(checkSrc(stated, null).findings, 'part.servo', /owner statement "undefined"/);

  const name = copy();
  name.parts[4]!.src = { kind: 'photo', file: 'IMG_0412.jpg' };
  reports(checkSrc(name, null).findings, 'part.servo', /no photo number/);

  for (const [id, edit] of [
    ['part.servo', (c: Catalogue) => { c.parts[4]!.src = { kind: 'page' }; }],
    ['part.servo.SIG', (c: Catalogue) => { c.parts[4]!.pins[0]!.src = { kind: 'page' }; }],
    ['pad.1', (c: Catalogue) => { c.pads[0]!.src = { kind: 'page' }; }],
    ['wire.1', (c: Catalogue) => { c.wires[0]!.src = { kind: 'page' }; }],
    ['function.servo', (c: Catalogue) => { c.functions[SERVO]!.src = { kind: 'page' }; }],
    ['stage.1', (c: Catalogue) => { c.stages[0]!.src = { kind: 'page' }; }],
    ['safety.0', (c: Catalogue) => { c.safety[0]!.src = { kind: 'page' }; }],
    ['notInRelease.knob', (c: Catalogue) => { c.notInRelease[0]!.src = { kind: 'page' }; }],
    ['pin.sda', (c: Catalogue) => { c.pinDefaults[1]!.src = { kind: 'page' }; }],
  ] as Array<[string, (c: Catalogue) => void]>) {
    const c = copy();
    edit(c);
    reports(checkSrc(c, null).findings, id, /not allowed/);
  }

  const other = copy();
  (other.parts[4] as { src: unknown }).src = { kind: 'forum', url: 'x' };
  reports(checkSrc(other, null).findings, 'part.servo', /unknown kind/);
});

test('src: without the repository the anchors are counted; --strict makes that an error', () => {
  const report = checkAll(CATALOGUE, null);
  assert.equal(report.repoChecked, 0);
  assert.equal(report.repoAnchors, collectAnchors(CATALOGUE).filter((a) => a.src?.kind === 'repo').length);
  assert.deepEqual(report.findings, []);
  reports(checkAll(CATALOGUE, null, true).findings, 'repository', /absent/);
});

// --- the data file against the tree ------------------------------------------

test('tree: the firmware parsers read the table formats of the release', () => {
  const outBind = [
    'static const outbind_pin_t k_pico_pins[OUTBIND_PINS] = {',
    '    {  0,  1, false, NULL },',
    '    {  3,  5, true,  "heartbeat" },',
    '};',
    'static const outbind_pad_t k_pico_pads[] = {',
    '    {  3, LINK_PAD_GROUND, 0  },',
    '    { 36, LINK_PAD_POWER,  33 },   /* 3V3 */',
    '    { 39, LINK_PAD_POWER,  0  },   /* VSYS: an input, not a fixed rail */',
    '};',
  ].join('\n');
  assert.deepEqual(parseFirmwarePins(outBind), [
    { gpio: 0, pad: 1, reserved: false, line: 2 },
    { gpio: 3, pad: 5, reserved: true, line: 3 },
  ]);
  assert.deepEqual(parseFirmwarePads(outBind), [
    { pad: 3, ground: true, name: null, line: 6 },
    { pad: 36, ground: false, name: '3V3', line: 7 },
    { pad: 39, ground: false, name: 'VSYS', line: 8 },
  ]);
  const settings = [
    '    [SET_SENSE_SDA] = {',
    '        "sns_sda", "Sensor SDA", "Coprocessor GPIO; -1: no sensor bus", "GPIO",',
    '        SET_CAT_IFACE, SET_TYPE_INT, -1, 47, 1, 16, NULL, 0 },',
  ].join('\n');
  assert.deepEqual(parseSettingDefault(settings, 'SET_SENSE_SDA'), { def: 16, line: 1 });
  assert.equal(parseSettingDefault(settings, 'SET_TONE_PIN'), null);
  assert.equal(parseSettingDefault('    [SET_TONE_PIN] = {\n', 'SET_TONE_PIN'), null);
  assert.deepEqual(parseFirmwarePins(''), []);
});

test('tree: a data file that is stale against the firmware tables is reported', () => {
  assert.deepEqual(checkTree(CATALOGUE, fakeTree(CATALOGUE)), []);

  // The firmware reserves GP5, the ESC signal pin of the data.
  const reservedNow = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[37] = '    { 5, 7, true, "tacho" },';
  });
  reports(checkTree(CATALOGUE, reservedNow), 'pad.7', /reserves GP5/);

  // The firmware frees a pad the data marks reserved.
  const freed = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[40] = '    { 8, 11, false, NULL },';
  });
  reports(checkTree(CATALOGUE, freed), 'pad.11', /does not reserve GP8/);

  // A pad carries another GPIO.
  const moved = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[32] = '    { 23, 1, false, NULL },';
  });
  reports(checkTree(CATALOGUE, moved), 'pad.1', /GP0 in the data and GP23/);

  // A row moves by one line: the anchor of the data is stale.
  const shifted = fakeTree(CATALOGUE, (files) => {
    const text = lines(files, OUT_BIND);
    text[59] = text[32]!;
    text[32] = '/* text */';
  });
  reports(checkTree(CATALOGUE, shifted), 'pad.1', /not the row of this pad/);

  // A row is gone.
  const gone = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[57] = '/* text */';
  });
  reports(checkTree(CATALOGUE, gone), 'pad.34', /no GPIO on this pad/);
  reports(checkTree(CATALOGUE, gone), 'pads', /lists 39 pads/);

  // A ground pad is a power pad, and a named pad has another name.
  const power = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[85] = '    { 3, LINK_PAD_POWER, 50 },';
    lines(files, OUT_BIND)[98] = '    { 40, LINK_PAD_POWER, 50 },   /* VUSB */';
  });
  reports(checkTree(CATALOGUE, power), 'pad.3', /ground/);
  reports(checkTree(CATALOGUE, power), 'pad.40', /VBUS in the data and VUSB/);

  const full = fakeTree(CATALOGUE);
  const noFile: Tree = {
    read: (path) => (path === OUT_BIND ? null : full.read(path)),
    newestTag: RELEASE_TAG,
  };
  reports(checkTree(CATALOGUE, noFile), 'pads', /out_bind\.c does not exist/);
  const noSettings: Tree = {
    read: (path) => (path === SETTINGS ? null : full.read(path)),
    newestTag: RELEASE_TAG,
  };
  reports(checkTree(CATALOGUE, noSettings), 'pins', /settings\.c does not exist/);
});

test('tree: stale pin defaults are reported', () => {
  const sda = fakeTree(CATALOGUE, (files) => {
    lines(files, SETTINGS)[161] = '        SET_CAT_IFACE, SET_TYPE_INT, -1, 47, 1, 18, NULL, 0 },';
  });
  reports(checkTree(CATALOGUE, sda), 'pin.sda', /GP16 in the data and 18/);

  // The firmware has a default for a pin the data calls a proposal.
  const pd = fakeTree(CATALOGUE, (files) => {
    lines(files, SETTINGS)[226] = '        SET_CAT_IFACE, SET_TYPE_INT, -1, 47, 1, 14, NULL, 0 },';
  });
  reports(checkTree(CATALOGUE, pd), 'pin.pd_tx', /proposal in the data/);

  const renamed = fakeTree(CATALOGUE, (files) => {
    lines(files, SETTINGS)[189] = '    [SET_TAP_PIN] = {';
  });
  reports(checkTree(CATALOGUE, renamed), 'pin.tap', /no SET_TONE_PIN/);

  const anchor = copy();
  anchor.pinDefaults[1]!.src = { kind: 'repo', path: SETTINGS, line: 170, tag: TAG };
  reports(checkTree(anchor, fakeTree(CATALOGUE)), 'pin.sda', /not inside SET_SENSE_SDA/);

  const heartbeat = fakeTree(CATALOGUE, (files) => {
    lines(files, IOMCU_PINS)[42] = '#define IOMCU_HEARTBEAT_PIN  4';
  });
  reports(checkTree(CATALOGUE, heartbeat), 'pin.heartbeat', /GP3 in the data and GP4/);

  const undefinedPin = fakeTree(CATALOGUE, (files) => {
    lines(files, IOMCU_PINS)[42] = '/* text */';
  });
  reports(checkTree(CATALOGUE, undefinedPin), 'pin.heartbeat', /no IOMCU_HEARTBEAT_PIN/);
});

test('tree: a newer release tag is a warning, not an error', () => {
  const tree = { ...fakeTree(CATALOGUE), newestTag: 'v0.18.0' };
  const findings = checkTree(CATALOGUE, tree);
  assert.deepEqual(findings, [
    { level: 'warn', id: 'release', msg: `the data describes ${VERSION}; the newest release tag is v0.18.0` },
  ]);
  assert.deepEqual(checkTree(CATALOGUE, { ...tree, newestTag: null }), []);
});

// --- invariant 6 ------------------------------------------------------------

test('counts: a missing safety note, stage or pad is reported', () => {
  const safety = copy();
  safety.safety.pop();
  reports(checkCounts(safety), 'safety', /6 entries/);

  const stages = copy();
  stages.stages.reverse();
  reports(checkCounts(stages), 'stage.5', /in order/);
  stages.stages.pop();
  reports(checkCounts(stages), 'stages', /4 entries/);

  const pads = copy();
  pads.pads.splice(10, 1);
  reports(checkCounts(pads), 'pads', /39 entries/);
  reports(checkCounts(pads), 'pad.12', /in order/);

  const gpio = copy();
  gpio.pads[0]!.gpio = 1;
  reports(checkCounts(gpio), 'pads', /GP1 is on two pads/);

  const release = copy();
  release.release = '0.18.0';
  reports(checkCounts(release), 'release', /0\.18\.0/);
  release.release = 'next';
  reports(checkCounts(release), 'release', /version number/);

  const hash = copy();
  (hash as { hashVersion: number }).hashVersion = 2;
  reports(checkCounts(hash), 'hashVersion', /not 1/);
});

// --- invariant 7 ------------------------------------------------------------

test('functions: one that cannot be enabled on its own is reported', () => {
  assert.deepEqual(checkFunctions(CATALOGUE), []);
  // A need that no step adds: the id is unknown to the catalogue.
  const c = copy();
  c.functions[SERVO]!.needs = ['servo', 'gimbal'];
  reports(checkFunctions(c), 'function.servo', /not enabled; missing: gimbal/);

  // A forced value outside the option is reported by the reference check.
  const forced = copy();
  forced.functions[6]!.forces = { escPower: 'nowhere' as 'pd' };
  assert.deepEqual(checkFunctions(forced), []);
  reports(checkRefs(forced), 'function.stick', /escPower=nowhere/);
});

// --- invariant 10 -----------------------------------------------------------

test('routes: a repeated slot order and a lane out of range are reported', () => {
  const slot = copy();
  slot.parts[4]!.slot.order = slot.parts[6]!.slot.order;
  reports(checkRoutes(slot), `slot.L.${slot.parts[6]!.slot.order}`, /twice/);

  for (const [index, route, pattern] of [
    [0, { kind: 'L', lane: 19 }, /lane 19 is outside 0 to 18/],
    [0, { kind: 'L', lane: -1 }, /lane -1/],
    [0, { kind: 'L', lane: 1.5 }, /lane 1.5/],
    [23, { kind: 'R', lane: 23 }, /lane 23 is outside 0 to 22/],
    [8, { kind: 'O', lane: 8 }, /lane 8 is outside 0 to 7/],
    [27, { kind: 'X', left: 19, right: 16, channel: 0 }, /left lane 19/],
    [27, { kind: 'X', left: 12, right: 23, channel: 0 }, /right lane 23/],
    [27, { kind: 'X', left: 12, right: 16, channel: 7 }, /channel 7/],
  ] as Array<[number, Catalogue['wires'][number]['route'], RegExp]>) {
    const c = copy();
    c.wires[index]!.route = route;
    reports(checkRoutes(c), `wire.${c.wires[index]!.n}`, pattern);
  }

  // The limits themselves pass.
  const limits = copy();
  limits.wires[0]!.route = { kind: 'L', lane: 18 };
  limits.wires[23]!.route = { kind: 'R', lane: 22 };
  limits.wires[8]!.route = { kind: 'O', lane: 7 };
  limits.wires[27]!.route = { kind: 'X', left: 0, right: 0, channel: 6 };
  assert.deepEqual(checkRoutes(limits), []);
});

// --- the wire table and a catalogue that is not a catalogue -------------------

test('the wire table prints one row per wire in the column order of the specification', () => {
  const rows = wireTable(CATALOGUE).trimEnd().split('\n');
  assert.equal(rows.length, 2 + CATALOGUE.wires.length);
  assert.equal(rows[0], '| No. | From | To | Class | Route | Lane | Stage | Present when |');
  assert.equal(rows[2], '| 1 | panel.CANH | can.H | canh | L | 0 | 1 | always |');
  assert.equal(rows[5], '| 4 | panel.J8S | pad.5 | hb | L | 3 | 1 | not estop |');
  assert.equal(rows[14], '| 13 | esc.BEC | (nothing) | nc | S |  | 1 | esc |');
  assert.equal(rows[15], '| 14 | batt.POS | esc.POS | power | O | 2 | 1 | esc and escPower=battery and not ina228 |');
  assert.equal(rows[29], '| 28 | pdmini.OP | ina3221.C1A | power | X | L12, R16, C0 | 3 | servo and pdmini and ina3221 |');
  assert.equal(rows[17], '| 16 | pad.10 | usbtap.DM | uart | L | 9 | 2 | pdmini |');
  assert.equal(rows[18], '| 17 | usbtap.DP | pad.9 | uart | L | 10 | 2 | pdmini |');
  assert.equal(rows[19], '| 18 | pad.18 | usbtap.G | gnd | L | 11 | 2 | pdmini |');
  assert.equal(rows[42], '| 41 | pad.40 | ina228.V5 | logic | R | 0 | 5 | ina228 and ina228Board=matek |');
  assert.equal(rows[54], '| 53 | pad.29 | pad.28 | phase | Z |  | 5 | tap |');
  assert.equal(rows[55], '| 54 | as5600.GPO | (GND, on the board) | gnd | S |  | 4 | as5600 |');
  assert.equal(rows[56], '| 55 | pad.36 | ina228.V3 | logic | R | 0 | 5 | ina228 and ina228Board=xt90 |');
  assert.equal(rows[16], '| 15 | batt.NEG | esc.NEG | power | O | 3 | 1 | esc and escPower=battery and not (ina228 and ina228Board=xt90) |');
  assert.equal(rows[57], '| 56 | batt.NEG | ina228.BATN | power | X | L17, R21, C5 | 5 | esc and (ina228 and ina228Board=xt90) and escPower=battery |');
  assert.equal(rows[59], '| 58 | esc.NEG | ina228.ESCN | power | X | L18, R22, C6 | 5 | esc and (ina228 and ina228Board=xt90) |');

  const nested = copy();
  nested.wires[0]!.when = { all: [{ any: [{ has: 'esc' }, { has: 'servo' }] }, { not: { all: [{ has: 'sd' }] } }] };
  assert.match(wireTable(nested), /\| \(esc or servo\) and not \(sd\) \|/);
});

test('a catalogue with a missing table gives findings, not an exception', () => {
  const broken = copy();
  delete (broken as Partial<Catalogue>).wires;
  const report = checkAll(broken, null);
  assert.ok(report.findings.some((f) => f.level === 'error' && /check stopped/.test(f.msg)));
});

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
    { file: 'catalogue.ts', found: 9, hit: 9 },
  ]);
  assert.ok(block.startsWith(BEGIN) && block.endsWith(END));
  assert.match(block, /\| `resolve\.ts` \| 183 \| 184 \| 99\.4 % \|/);
  assert.match(block, /\| `cond\.ts` \| 51 \| 51 \| 100\.0 % \|/);
  assert.match(block, /\| `groups\.ts` \| 199 \| 200 \| 99\.5 % \|/);
  assert.doesNotMatch(block, /catalogue/);
  assert.throws(() => figure([{ file: 'cond.ts', found: 51, hit: 51 }]), /no record for resolve\.ts/);
  assert.deepEqual(MEASURED, ['resolve.ts', 'cond.ts', 'groups.ts']);
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
  // The 3.3 V limit of the Pico pin, the factory position and the shared supply without USB are stated.
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
    ['strip-input', /7 to 12 V and limits the input to 7 to 18 V/, /7 bis 12 V und begrenzt den Eingang auf 7 bis 18 V/],
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
    ['servo-bench-supply', /At the 5 V pin of the coprocessor board/, /Am 5-V-Pin der Koprozessor-Platine/],
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
    ['strip-v', 'cons', /In position 3V3 the servo draws from VSYS of the coprocessor board/, /In Stellung 3V3 zieht das Servo aus VSYS der Koprozessor-Platine/],
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
    ['strip-regulator', /The S pin of the 3\.3 V row is the 3V3 pin of the coprocessor board \(pad 36\), the 3\.3 V supply of its logic; its current limit is not stated\./, /Der S-Pin der 3,3-V-Reihe ist der 3V3-Pin der Koprozessor-Platine \(Pad 36\), die 3,3-V-Versorgung ihrer Logik; seine Stromgrenze ist nicht angegeben\./],
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
  // Wire notes 61 and 62 name the vendor wording in both languages (pinned elsewhere); the strip-v pros keep the shared 2 A.
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
  // The range is the one the strip states: the same 7 to 12 V and the same bounds.
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

  // The 2 leads: stubs to the DC input, which is no pad of the coprocessor.
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
  // The product page names the socket DC005-2.1 only. The 2.1 mm pin follows
  // from that name; the 5.5 mm outer diameter is the usual size of the socket
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
  // its source at GND; the HXY datasheet rates the gate-source voltage at ±12 V.
  assert.match(dc.role!.en, /Above 12 V the gate-source voltage of the SI2302 \(Q3\) exceeds its rating of ±12 V \(datasheet linked on the product page\) at every input: the gate sits at PWRIN through R7, with no clamp\. The print of 16 V and the vendor limit of 18 V are above that\./);
  assert.match(dc.role!.de, /Über 12 V überschreitet die Gate-Source-Spannung des SI2302 \(Q3\) an jedem Eingang ihren Grenzwert von ±12 V \(Datenblatt, verlinkt auf der Produktseite\): Das Gate liegt über R7 ohne Begrenzung an PWRIN\. Der Aufdruck von 16 V und die Herstellergrenze von 18 V liegen darüber\./);
  // The range stays 7 to 12 V.
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
    en: 'In position 3V3 the servo draws from VSYS of the coprocessor board, the supply that also powers the logic. A current peak can drop it and reset the boards.',
    de: 'In Stellung 3V3 zieht das Servo aus VSYS der Koprozessor-Platine, der Versorgung, die auch die Logik speist. Eine Stromspitze kann sie einbrechen lassen und die Platinen zurücksetzen.',
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
