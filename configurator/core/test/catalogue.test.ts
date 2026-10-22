// Static invariants 1 to 10 on the catalogue, and proof that each check
// reports a broken copy of the data.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { TAG, VERSION } from '../../release.ts';
import type { Catalogue, Picture, Src } from '../../contract.ts';
import { ANCHOR_TEXTS, CATALOGUE } from '../catalogue.ts';
import {
  ANCHOR_TEXT_MIN, BANNED_WORDS, RELEASE_TAG, checkAll, checkCounts, checkEnds, checkFunctions, checkIds,
  OWNER_STATEMENTS, checkRefs, checkRoutes, checkSrc, checkString, checkTexts, checkTree, collectAnchors,
  collectTexts, gitTree, parseFirmwarePads, parseFirmwarePins, parseSettingDefault, wireTable,
} from '../tools/check.ts';
import type { Finding, Tree } from '../tools/check.ts';
import { PICTURE_DIR, checkPictures, pictureFolder, webpInfo } from '../tools/pictures.ts';
import type { PictureFolder } from '../tools/pictures.ts';

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
  entry(154, 'SET_SENSE_SDA', 16);
  entry(157, 'SET_SENSE_SCL', 17);
  entry(184, 'SET_TONE_PIN', 22);
  entry(219, 'SET_PDMINI_TX', -1);
  entry(222, 'SET_PDMINI_RX', -1);

  const iomcu = filler();
  iomcu[41] = '#define IOMCU_HEARTBEAT_PIN  3';

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
  assert.equal(CATALOGUE.functions.length, 24);
  // 15 functions need no part beyond the base setup: 12 run on every setup, 3 drive a test object.
  assert.equal(CATALOGUE.functions.filter((f) => f.needs.length === 0).length, 15);
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
  assert.equal(CATALOGUE.checks.length, 44);
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
  assert.equal(pictures.filter((p) => p.kind === 'photo').length, 11);
  assert.equal(pictures.filter((p) => p.kind === 'drawing').length, 0);
  assert.equal(folder.names.length, 22);
  // The panel has no photo: the picture folder holds none of the board.
  assert.deepEqual(CATALOGUE.parts.find((p) => p.id === 'panel')?.pictures, undefined);
  // The check tool runs the same check.
  assert.deepEqual(errors(checkAll(CATALOGUE, null, false, ANCHOR_TEXTS, folder).findings), []);
  reports(checkAll(CATALOGUE, null, false, ANCHOR_TEXTS, folderOf({})).findings,
    'part.copro.picture.05-rp2350-can-breakout', /05-rp2350-can-breakout\.webp is missing/);
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
    'panel-heartbeat-pin', 'pd-shared', 'ina3221-pullups', 'ina228-xt90-pd', 'as5600-board',
    'osoyoo-labels', 'prog-level', 'servo-5v-pin', 'strip-supply', 'esc-programming-power',
  ]);
  const unconfirmed = CATALOGUE.parts.flatMap((part) =>
    part.pins.filter((p) => !p.confirmed).map((p) => `${part.id}.${p.id}`));
  assert.deepEqual(unconfirmed, ['panel.HB', 'ina3221.LP', 'as5600.GPO']);
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

/** The part ids at the ends of a wire. A pad of the IOmcu is no part. */
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
    { kind: 'repo', path: 'docs/FirstRun.md', line, tag: TAG }
  );

  // A line that is present and holds another row: no text is named for it.
  moved.functions[SERVO]!.src = at(762);
  reports(checkSrc(moved, tree).findings, 'function.servo', /FirstRun\.md:762 names no text of 4 characters or more/);
  reports(checkSrc(moved, null).findings, 'function.servo', /FirstRun\.md:762 names no text/);
  reports(checkAll(moved, tree, true).findings, 'function.servo', /names no text/);

  // A named text that the line does not hold.
  const wrong = [...ANCHOR_TEXTS, { path: 'docs/FirstRun.md', line: 762, has: '| SDA | GP16 (pad 21) |' }];
  reports(checkSrc(moved, tree, wrong).findings, 'function.servo', /FirstRun\.md:762 does not hold "\| SDA \| GP16 \(pad 21\) \|" at v0\.22\.0/);
  reports(checkAll(moved, tree, true, wrong).findings, 'function.servo', /does not hold/);
  // Without the tree the text is not compared.
  assert.deepEqual(checkSrc(moved, null, wrong).findings, []);

  // Every text named for the line is compared.
  const rows = fakeTree(CATALOGUE, (files) => {
    lines(files, 'docs/FirstRun.md')[761] = '|  INA3221 power |   3V3 (pad 36), GND |';
  });
  const power = [...ANCHOR_TEXTS, { path: 'docs/FirstRun.md', line: 762, has: '| INA3221 power | 3V3' }];
  assert.deepEqual(checkSrc(moved, rows, power).findings, []);
  reports(checkSrc(moved, rows, [...power, ...wrong]).findings, 'function.servo', /does not hold "\| SDA/);

  // A text under the shortest length names nothing.
  assert.equal(ANCHOR_TEXT_MIN, 4);
  const short = [...ANCHOR_TEXTS, { path: 'docs/FirstRun.md', line: 762, has: ' |  I ' }];
  reports(checkSrc(moved, rows, short).findings, 'function.servo', /names no text/);
  const none = [...ANCHOR_TEXTS, { path: 'docs/FirstRun.md', line: 762 } as { path: string; line: number; has: string }];
  reports(checkSrc(moved, rows, none).findings, 'function.servo', /names no text/);

  // A row of the data moved off its line in the tree.
  const shifted = fakeTree(CATALOGUE, (files) => {
    const doc = lines(files, 'docs/FirstRun.md');
    doc.splice(761, 0, '| INA3221 power | 3V3 (pad 36), GND | INA3221 VS, GND |');
  });
  const found = checkSrc(CATALOGUE, shifted).findings;
  reports(found, 'wire.26', /FirstRun\.md:763 does not hold "\| SDA \| GP16 \(pad 21\) \|"/);
  reports(found, 'wire.27', /FirstRun\.md:764 does not hold/);
  reports(found, 'check.ina3221-pullups', /FirstRun\.md:765 does not hold/);
});

test('src: every repository anchor of the data names a text of its own', () => {
  const anchors = collectAnchors(CATALOGUE).filter((a) => a.src?.kind === 'repo');
  assert.equal(ANCHOR_TEXTS.length, anchors.length);
  assert.ok(Object.isFrozen(ANCHOR_TEXTS));
  for (const text of ANCHOR_TEXTS) {
    assert.ok(text.has.trim().length >= ANCHOR_TEXT_MIN, `${text.path}:${text.line}`);
    assert.ok(Object.isFrozen(text));
  }
  // The sensor bus rows of docs/FirstRun.md at the tag: 763 SDA, 764 SCL,
  // 765 the pull-ups.
  const sda = CATALOGUE.wires.find((w) => w.n === 26)?.src;
  const scl = CATALOGUE.wires.find((w) => w.n === 27)?.src;
  assert.deepEqual(sda, { kind: 'repo', path: 'docs/FirstRun.md', line: 763, tag: TAG });
  assert.deepEqual(scl, { kind: 'repo', path: 'docs/FirstRun.md', line: 764, tag: TAG });
  const pullups = CATALOGUE.checks.find((k) => k.id === 'ina3221-pullups')?.src;
  assert.deepEqual(pullups, { kind: 'repo', path: 'docs/FirstRun.md', line: 765, tag: TAG });
  // The AS5600 board's OUT and GPO have no repository source at the tag:
  // docs/Building.md describes the panel's knob, an MT6701, since 0.21.0.
  const as5600 = CATALOGUE.parts.find((p) => p.id === 'as5600');
  for (const pin of ['OUT', 'GPO']) {
    assert.deepEqual(as5600?.pins.find((p) => p.id === pin)?.src, { kind: 'guide', section: 'stage-4' });
  }
  assert.ok(!ANCHOR_TEXTS.some((x) => x.path === 'docs/Building.md'));
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
  assert.deepEqual(OWNER_STATEMENTS, ['2026-10-11', '2026-10-21']);
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
    '        "sns_sda", "Sensor SDA", "IOmcu GPIO; -1: no sensor bus", "GPIO",',
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
    lines(files, OUT_BIND)[38] = '    { 5, 7, true, "tacho" },';
  });
  reports(checkTree(CATALOGUE, reservedNow), 'pad.7', /reserves GP5/);

  // The firmware frees a pad the data marks reserved.
  const freed = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[41] = '    { 8, 11, false, NULL },';
  });
  reports(checkTree(CATALOGUE, freed), 'pad.11', /does not reserve GP8/);

  // A pad carries another GPIO.
  const moved = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[33] = '    { 23, 1, false, NULL },';
  });
  reports(checkTree(CATALOGUE, moved), 'pad.1', /GP0 in the data and GP23/);

  // A row moves by one line: the anchor of the data is stale.
  const shifted = fakeTree(CATALOGUE, (files) => {
    const text = lines(files, OUT_BIND);
    text[60] = text[33]!;
    text[33] = '/* text */';
  });
  reports(checkTree(CATALOGUE, shifted), 'pad.1', /not the row of this pad/);

  // A row is gone.
  const gone = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[58] = '/* text */';
  });
  reports(checkTree(CATALOGUE, gone), 'pad.34', /no GPIO on this pad/);
  reports(checkTree(CATALOGUE, gone), 'pads', /lists 39 pads/);

  // A ground pad is a power pad, and a named pad has another name.
  const power = fakeTree(CATALOGUE, (files) => {
    lines(files, OUT_BIND)[86] = '    { 3, LINK_PAD_POWER, 50 },';
    lines(files, OUT_BIND)[99] = '    { 40, LINK_PAD_POWER, 50 },   /* VUSB */';
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
    lines(files, SETTINGS)[154] = '        SET_CAT_IFACE, SET_TYPE_INT, -1, 47, 1, 18, NULL, 0 },';
  });
  reports(checkTree(CATALOGUE, sda), 'pin.sda', /GP16 in the data and 18/);

  // The firmware has a default for a pin the data calls a proposal.
  const pd = fakeTree(CATALOGUE, (files) => {
    lines(files, SETTINGS)[219] = '        SET_CAT_IFACE, SET_TYPE_INT, -1, 47, 1, 14, NULL, 0 },';
  });
  reports(checkTree(CATALOGUE, pd), 'pin.pd_tx', /proposal in the data/);

  const renamed = fakeTree(CATALOGUE, (files) => {
    lines(files, SETTINGS)[182] = '    [SET_TAP_PIN] = {';
  });
  reports(checkTree(CATALOGUE, renamed), 'pin.tap', /no SET_TONE_PIN/);

  const anchor = copy();
  anchor.pinDefaults[1]!.src = { kind: 'repo', path: SETTINGS, line: 170, tag: TAG };
  reports(checkTree(anchor, fakeTree(CATALOGUE)), 'pin.sda', /not inside SET_SENSE_SDA/);

  const heartbeat = fakeTree(CATALOGUE, (files) => {
    lines(files, IOMCU_PINS)[41] = '#define IOMCU_HEARTBEAT_PIN  4';
  });
  reports(checkTree(CATALOGUE, heartbeat), 'pin.heartbeat', /GP3 in the data and GP4/);

  const undefinedPin = fakeTree(CATALOGUE, (files) => {
    lines(files, IOMCU_PINS)[41] = '/* text */';
  });
  reports(checkTree(CATALOGUE, undefinedPin), 'pin.heartbeat', /no IOMCU_HEARTBEAT_PIN/);
});

test('tree: a newer release tag is a warning, not an error', () => {
  const tree = { ...fakeTree(CATALOGUE), newestTag: 'v0.23.0' };
  const findings = checkTree(CATALOGUE, tree);
  assert.deepEqual(findings, [
    { level: 'warn', id: 'release', msg: `the data describes ${VERSION}; the newest release tag is v0.23.0` },
  ]);
  assert.deepEqual(checkTree(CATALOGUE, { ...tree, newestTag: null }), []);
  // The release pull request before its tag: the newest tag is older than the data.
  assert.deepEqual(checkTree(CATALOGUE, { ...tree, newestTag: 'v0.0.1' }), []);
  assert.deepEqual(checkTree(CATALOGUE, { ...tree, newestTag: TAG }), []);
});

test('tree: read at the tag, or at HEAD before the tag when the header names the release', (t) => {
  const dir = mkdtempSync(join(tmpdir(), 'cfg-tree-'));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const git = (...args: string[]): void => {
    execFileSync('git', ['-C', dir, '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'commit.gpgsign=false', '-c', 'tag.gpgsign=false', ...args], {
      stdio: ['ignore', 'ignore', 'ignore'],
    });
  };
  const header = (v: string): string => {
    const [a, b, c] = v.split('.');
    return `#define RCBENCH_VERSION_MAJOR ${a}\n#define RCBENCH_VERSION_MINOR ${b}\n#define RCBENCH_VERSION_PATCH ${c}\n`;
  };
  const commit = (v: string, note: string): void => {
    mkdirSync(join(dir, 'shared/link/include'), { recursive: true });
    writeFileSync(join(dir, 'shared/link/include/rcbench_version.h'), header(v));
    writeFileSync(join(dir, 'note.txt'), note);
    git('add', '-A');
    git('commit', '-q', '-m', note);
  };
  try {
    git('init', '-q');
  } catch {
    t.skip('git is absent');
    return;
  }

  // No commit yet: no header at HEAD, no tag.
  assert.equal(gitTree(dir, 'v9.8.7', '9.8.7', true), null);

  // The release pull request: header and data name 9.8.7, the tag is not made.
  commit('9.8.7', 'release pr');
  writeFileSync(join(dir, 'note.txt'), 'uncommitted');
  // Without the explicit before-tag mode a clone without the tag reads nothing:
  // a clone that lacks a published tag does not fall back to HEAD.
  assert.equal(gitTree(dir, 'v9.8.7', '9.8.7', false), null);
  const head = gitTree(dir, 'v9.8.7', '9.8.7', true);
  assert.ok(head !== null);
  assert.equal(head.ref, 'HEAD');
  assert.equal(head.read('note.txt'), 'release pr');
  assert.equal(head.read('absent.txt'), null);
  assert.equal(head.newestTag, null);

  // A header of another release, at either side of the data's: no tree.
  assert.equal(gitTree(dir, 'v9.8.6', '9.8.6', true), null);
  assert.equal(gitTree(dir, 'v9.8.8', '9.8.8', true), null);

  // The tag exists: the files are read at the tag, not at a later HEAD.
  git('tag', 'v9.8.7');
  commit('9.9.0', 'after the tag');
  const tagged = gitTree(dir, 'v9.8.7', '9.8.7', true);
  assert.ok(tagged !== null);
  assert.deepEqual(gitTree(dir, 'v9.8.7', '9.8.7', false)?.ref, 'v9.8.7');
  assert.equal(tagged.ref, 'v9.8.7');
  assert.equal(tagged.read('note.txt'), 'release pr');
  assert.equal(tagged.newestTag, 'v9.8.7');

  // The next release pull request before its tag: HEAD, and the older tag is the newest.
  const next = gitTree(dir, 'v9.9.0', '9.9.0', true);
  assert.ok(next !== null);
  assert.equal(next.ref, 'HEAD');
  assert.equal(next.read('note.txt'), 'after the tag');
  assert.equal(next.newestTag, 'v9.8.7');

  // Not a repository: no tree.
  const bare = mkdtempSync(join(tmpdir(), 'cfg-none-'));
  t.after(() => rmSync(bare, { recursive: true, force: true }));
  assert.equal(gitTree(bare, 'v9.8.7', '9.8.7', true), null);
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
  release.release = '0.23.0';
  reports(checkCounts(release), 'release', /0\.23\.0/);
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
  assert.equal(rows[5], '| 4 | panel.HB | pad.5 | hb | L | 3 | 1 | not estop |');
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

test('the German catalogue texts use the glossary words, not Servohorn, Frame Rate or Töne for beeps', () => {
  const source = readFileSync(fileURLToPath(new URL('../catalogue.ts', import.meta.url)), 'utf8');
  assert.doesNotMatch(source, /Servohorn|Frame[- ]Rate|\bTöne\b/);
  assert.match(source, /Ruderhorns/);
  assert.match(source, /Pieptöne/);
});
