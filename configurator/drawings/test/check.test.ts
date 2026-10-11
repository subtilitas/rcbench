import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { Catalogue } from '../../contract.ts';
import { CATALOGUE } from '../../core/catalogue.ts';
import { checkAll, checkContrast, checkDrift, checkPins, checkRecord, checkSvg, checkWires, filesOf, malformed } from '../check.ts';
import type { Files, Finding } from '../check.ts';
import type { Anchors } from '../generate.ts';
import { colourOf } from '../wires.ts';

const clone = (): Catalogue => structuredClone(CATALOGUE) as Catalogue;
const good = filesOf(CATALOGUE);
const anchors = (): Anchors => JSON.parse(good.anchors) as Anchors;
const copy = (): Files => ({ anchors: good.anchors, svgs: { ...good.svgs } });
const ids = (f: Finding[]): string[] => [...new Set(f.map((x) => x.id))].sort();
const has = (f: Finding[], id: string, text: RegExp): void => {
  assert.ok(f.some((x) => x.id === id && text.test(x.msg)), `${id} ${text} in ${JSON.stringify(f)}`);
};

test('the output of the generator has no finding', () => {
  assert.deepEqual(checkAll(CATALOGUE, good), []);
});

test('a catalogue pin without an anchor is a finding', () => {
  const c = clone();
  const servo = c.parts.find((p) => p.id === 'servo')!;
  servo.pins.push({ ...servo.pins[0]!, id: 'EXTRA' });
  const f = checkPins(c, anchors());
  assert.deepEqual(ids(f), ['pin-without-anchor']);
  has(f, 'pin-without-anchor', /servo\.svg: catalogue pin EXTRA has no anchor/);
});

test('an anchor removed from the table is a finding for its catalogue pin', () => {
  const a = anchors();
  delete a.files['estop.svg']!.pins['NC1'];
  const f = checkPins(CATALOGUE, a);
  assert.deepEqual(ids(f), ['pin-without-anchor']);
  has(f, 'pin-without-anchor', /estop\.svg: catalogue pin NC1 has no anchor/);
});

test('an anchor without a catalogue pin is a finding', () => {
  const c = clone();
  const estop = c.parts.find((p) => p.id === 'estop')!;
  estop.pins = estop.pins.filter((p) => p.id !== 'NC1');
  const f = checkPins(c, anchors());
  assert.deepEqual(ids(f), ['anchor-without-pin']);
  has(f, 'anchor-without-pin', /estop\.svg: anchor NC1 is not a catalogue pin/);
});

test('an anchor on a pad the catalogue does not list is a finding', () => {
  const a = anchors();
  a.files['copro.svg']!.pins['pad.41'] = { ...a.files['copro.svg']!.pins['pad.40']! };
  has(checkPins(CATALOGUE, a), 'anchor-without-pin', /copro\.svg: anchor pad\.41 is not a catalogue pin/);
});

test('a drawing marked as not in the catalogue carries anchors without catalogue pins', () => {
  const a = anchors();
  assert.equal(a.files['ina219.svg']!.part, null);
  assert.ok(Object.keys(a.files['ina219.svg']!.pins).length > 0);
  assert.deepEqual(checkPins(CATALOGUE, a), []);
});

test('a drawing of a part the catalogue does not list is a finding', () => {
  const a = anchors();
  a.files['ina219.svg']!.part = 'ina219';
  has(checkPins(CATALOGUE, a), 'anchor-without-pin', /ina219\.svg: part ina219 is not in the catalogue/);
});

test('a label that differs from the catalogue is a finding, per language', () => {
  for (const lang of ['en', 'de'] as const) {
    const c = clone();
    const pin = c.parts.find((p) => p.id === 'estop')!.pins.find((p) => p.id === 'NC1')!;
    pin.label = { ...pin.label, [lang]: 'other' };
    const f = checkPins(c, anchors());
    assert.deepEqual(ids(f), ['label']);
    assert.equal(f.length, 1);
    has(f, 'label', lang === 'en' ? /^estop\.svg: pin NC1 is labelled ".+", the catalogue has "other"/ : /^de\/estop\.svg: pin NC1/);
  }
});

test('the supply pin of the XT90 module follows the catalogue: another label is a finding', () => {
  const a = anchors();
  const rec = a.files['ina228--xt90.svg']!;
  const id = 'V3' in rec.pins ? 'V3' : 'V5';
  rec.pins[id]!.label = { en: 'x', de: 'x' };
  const f = checkPins(CATALOGUE, a);
  assert.equal(f.length, 2);
  assert.deepEqual(ids(f), ['label']);
});

test('a pad label that differs from the print of the breakout board is a finding', () => {
  const c = clone();
  c.pads.find((p) => p.n === 36)!.breakoutLabel = '3V3 OUT';
  const f = checkPins(c, anchors());
  assert.equal(f.length, 2);
  has(f, 'label', /copro--osoyoo\.svg: pin pad\.36 is labelled "3V3", the catalogue has "3V3 OUT"/);
});

test('a part or a board without a drawing is a finding', () => {
  const a = anchors();
  delete a.files['ina228--xt90.svg'];
  delete a.files['tap.svg'];
  const f = checkPins(CATALOGUE, a);
  has(f, 'no-drawing', /part ina228: no drawing for xt90/);
  has(f, 'no-drawing', /part tap: no drawing/);
  const d = anchors();
  delete d.files['copro.svg'];
  const g = checkPins(CATALOGUE, d);
  has(g, 'no-drawing', /part copro: no drawing for the default pads/);
  assert.equal(g.filter((x) => x.id === 'no-drawing').length, 1);
  delete d.files['copro--osoyoo.svg'];
  const h = checkPins(CATALOGUE, d).filter((x) => x.id === 'no-drawing').map((x) => x.msg);
  assert.deepEqual(h, ['part copro: no drawing', 'part copro: no drawing for osoyoo']);
  const e = anchors();
  delete e.files['ina228--matek.svg'];
  assert.deepEqual(checkPins(CATALOGUE, e).filter((x) => x.id === 'no-drawing').map((x) => x.msg), ['part ina228: no drawing for matek']);
  // A drawing of a board the part does not name: no setup selects it.
  const o = anchors();
  o.files['tap--foo.svg'] = { ...structuredClone(o.files['tap.svg']!), variant: 'foo' };
  assert.deepEqual(checkPins(CATALOGUE, o).map((x) => x.msg), ['tap--foo.svg: part tap names no board foo']);
  o.files['ina228--foo.svg'] = { ...structuredClone(o.files['ina228--xt90.svg']!), variant: 'foo' };
  has(checkPins(CATALOGUE, o), 'no-drawing', /ina228--foo\.svg: part ina228 names no board foo/);
  // Such a drawing does not stand in for the drawing of the part.
  delete o.files['tap.svg'];
  has(checkPins(CATALOGUE, o), 'no-drawing', /part tap: no drawing$/);
  for (const name of ['toString', '__proto__']) {
    const p = anchors();
    p.files[`tap--${name}.svg`] = { ...structuredClone(p.files['tap.svg']!), variant: name };
    has(checkPins(CATALOGUE, p), 'no-drawing', /names no board/);
  }
  const b = anchors();
  b.files['other.svg'] = b.files['tap.svg']!;
  delete b.files['tap.svg'];
  has(checkPins(CATALOGUE, b), 'no-drawing', /other\.svg: file name does not match part tap/);
});

test('the map of the INA3221 covers the catalogue pins in both directions', () => {
  const c = clone();
  const ina = c.parts.find((p) => p.id === 'ina3221')!;
  ina.pins.push({ ...ina.pins[0]!, id: 'NEW' });
  has(checkPins(c, anchors()), 'pin-without-anchor', /ina3221\.svg: catalogue pin NEW is not in catalogueMap/);
  const d = clone();
  const ina2 = d.parts.find((p) => p.id === 'ina3221')!;
  ina2.pins = ina2.pins.filter((p) => p.id !== 'LP');
  has(checkPins(d, anchors()), 'map', /catalogueMap names LP/);
  const a = anchors();
  delete a.files['ina3221.svg']!.pins['VIN1+'];
  has(checkPins(CATALOGUE, a), 'pin-without-anchor', /catalogue pin C1A maps to VIN1\+, which has no anchor/);
});

test('a table of another catalogue release is a finding', () => {
  const c = clone();
  c.release = '9.9.9';
  assert.deepEqual(ids(checkPins(c, anchors())), ['release']);
});

test('a wire whose end has no anchor is a finding', () => {
  const c = clone();
  c.wires.find((w) => w.n === 5)!.a = 'pad.99';
  const f = checkWires(c, anchors());
  has(f, 'wire-end', /wire 5: end pad\.99 has no anchor/);
  const d = clone();
  const w = d.wires.find((x) => x.b !== null && x.b.startsWith('estop.'))!;
  w.b = 'estop.NONE';
  has(checkWires(d, anchors()), 'wire-end', new RegExp(`wire ${w.n}: end estop\\.NONE has no anchor`));
  const e = clone();
  e.wires[0]!.a = 'nopart.X';
  has(checkWires(e, anchors()), 'wire-end', /end nopart\.X has no anchor/);
  const g = clone();
  g.wires[0]!.a = 'nodot';
  has(checkWires(g, anchors()), 'wire-end', /end nodot has no anchor/);
});

test('a wire end loses its anchor when the anchor is removed from the table', () => {
  const a = anchors();
  delete a.files['copro.svg']!.pins['can.H'];
  delete a.files['copro--osoyoo.svg']!.pins['can.H'];
  has(checkWires(CATALOGUE, a), 'wire-end', /end can\.H has no anchor/);
});

test('the address pin of the INA3221 has no wire point and is not a finding; any other pin without one is', () => {
  const a = anchors();
  assert.equal(a.files['ina3221.svg']!.catalogueMap!['A0'], null);
  assert.ok(CATALOGUE.wires.some((w) => w.a === 'ina3221.A0' || w.b === 'ina3221.A0'));
  assert.deepEqual(checkWires(CATALOGUE, a).filter((f) => f.id === 'wire-end'), []);
  delete a.files['ina3221.svg']!.catalogueMap!['A0'];
  has(checkWires(CATALOGUE, a), 'wire-end', /end ina3221\.A0 has no anchor/);
});

test('a wire without a colour of the palette is a finding', () => {
  const c = clone();
  const w = c.wires.find((x) => x.cls === 'uart')!;
  w.net = 'unknown_net';
  has(checkWires(c, anchors()), 'wire-colour', new RegExp(`wire ${w.n}: no colour for net unknown_net, class uart`));
});

test('2 wires of 1 colour on neighbouring pins are a finding', () => {
  const c = clone();
  // Wires 1 and 2 end on the 2 poles of the CAN terminal: CAN H ochre, CAN L green.
  const l = c.wires.find((w) => w.n === 2)!;
  l.cls = 'canh';
  const f = checkWires(c, anchors());
  has(f, 'neighbour', /wires 1 and 2 end on the neighbouring pins .* and are both ochre/);
  assert.deepEqual(ids(f), ['neighbour']);
});

test('2 plus wires on neighbouring pins are no finding, a signal in the colour of its neighbour is', () => {
  // Wires 28 and 30 end on VIN1+ and VIN1- of the INA3221: both supply plus, both red.
  const c = clone();
  assert.deepEqual(ids(checkWires(c, anchors())), []);
  const colours = c.wires.filter((w) => w.n === 28 || w.n === 30).map((w) => colourOf(w));
  assert.deepEqual(colours, ['red', 'red']);
  // Wires 26 and 27 end on SDA and SCL of the same board. On 1 class they share a colour.
  c.wires.find((w) => w.n === 27)!.cls = 'sda';
  c.wires.find((w) => w.n === 27)!.net = 'sda2';
  has(checkWires(c, anchors()), 'neighbour', /wires 26 and 27 end on the neighbouring pins .* and are both blue/);
});

test('a colour below 3:1 on a page background is a finding, per background', () => {
  assert.deepEqual(checkContrast(), []);
  const f = checkContrast({ pale: { core: '#c0c0c0', casing: null } });
  assert.deepEqual(ids(f), ['contrast']);
  assert.equal(f.length, 3);
  has(f, 'contrast', /colour pale: 1\.82:1 on light surface #ffffff, below 3:1/);
  // Exactly 3:1 passes, 1 step darker background fails.
  assert.deepEqual(checkContrast({ c: { core: '#000000', casing: null } }, { b: '#5a5a5a' }).length, 0);
  assert.equal(checkContrast({ c: { core: '#000000', casing: null } }, { b: '#595959' }).length, 1);
  // A casing carries the wire where the core does not.
  assert.deepEqual(checkContrast({ w: { core: '#ffffff', casing: '#000000' } }), []);
  assert.equal(checkContrast({ w: { core: '#ffffff', casing: null } }).length, 3);
});

test('a changed, missing or added file is drift', () => {
  const changed = copy();
  changed.svgs['tap.svg'] = changed.svgs['tap.svg']!.replace('<desc', '<desc ');
  has(checkDrift(good, changed), 'drift', /^tap\.svg differs from the output of the generator: run "npm run drawings"/);
  const missing = copy();
  delete missing.svgs['de/tap.svg'];
  has(checkDrift(good, missing), 'drift', /^de\/tap\.svg is missing/);
  const extra = copy();
  extra.svgs['old.svg'] = '<svg/>';
  has(checkDrift(good, extra), 'drift', /^old\.svg is not a drawing of the generator/);
  const table = copy();
  table.anchors += ' ';
  has(checkDrift(good, table), 'drift', /^anchors\.json differs/);
  assert.deepEqual(checkDrift(good, copy()), []);
});

test('committed files of an older catalogue fail the whole check with drift and a label finding', () => {
  const c = clone();
  const pin = c.parts.find((p) => p.id === 'estop')!.pins.find((p) => p.id === 'NC1')!;
  pin.label = { en: 'NC new', de: 'NC neu' };
  const f = checkAll(c, good);
  assert.deepEqual(ids(f), ['drift', 'label']);
  assert.equal(f.filter((x) => x.id === 'label').length, 2);
  assert.deepEqual(checkAll(c, filesOf(c)), []);
});

test('the whole check reports a missing file, an empty table and a table that is not JSON', () => {
  const missing = copy();
  delete missing.svgs['de/tap.svg'];
  has(checkAll(CATALOGUE, missing), 'svg', /^de\/tap\.svg: file is missing/);
  assert.deepEqual(ids(checkAll(CATALOGUE, { anchors: '', svgs: {} })), ['anchor']);
  assert.deepEqual(ids(checkAll(CATALOGUE, { anchors: '{', svgs: {} })), ['anchor']);
});

test('a label text that differs from the table, is missing or has no anchor is a finding', () => {
  const a = anchors();
  const rec = a.files['estop.svg']!;
  const en = good.svgs['estop.svg']!;
  const labelId = rec.pins['NC1']!.labelId;
  const text = new RegExp(`(<text id="${labelId}"[^>]*>)[^<]*(</text>)`);
  assert.match(en, text);
  has(checkRecord('estop.svg', rec, { en: en.replace(text, '$1other$2') }), 'anchor', /label of pin NC1 is "other", anchors\.json has/);
  has(checkRecord('estop.svg', rec, { en: en.replace(text, '$1 $2') }), 'anchor', /label of pin NC1 is empty/);
  has(checkRecord('estop.svg', rec, { en: en.replace(text, '') }), 'anchor', /pin NC1 has 0 label texts, 1 expected/);
  has(checkRecord('estop.svg', rec, { en: en.replace(text, '$&$&') }), 'anchor', /pin NC1 has 2 label texts/);
  const less = structuredClone(rec);
  delete less.pins['NC1'];
  has(checkRecord('estop.svg', less, { en }), 'anchor', new RegExp(`label ${labelId} has no anchor`));
});

test('an anchor outside the viewBox, without a side or with a shared label id is a finding', () => {
  const rec = anchors().files['estop.svg']!;
  const [, , w, h] = rec.viewBox;
  const at = (x: number, y: number): Finding[] => {
    const r = structuredClone(rec);
    r.pins['NC1']!.x = x;
    r.pins['NC1']!.y = y;
    return checkRecord('estop.svg', r, {});
  };
  assert.deepEqual(at(0, 0), []);
  assert.deepEqual(at(w, h), []);
  has(at(w + 0.1, 10), 'anchor', /lies outside the viewBox/);
  has(at(10, -0.1), 'anchor', /lies outside the viewBox/);
  for (const v of [Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY, null, '10', undefined]) {
    has(at(v as number, 10), 'anchor', /is not a pair of finite numbers/);
    has(at(10, v as number), 'anchor', /is not a pair of finite numbers/);
  }
  // The table as the file holds it: JSON writes a coordinate that is not finite as null.
  const text = structuredClone(rec);
  text.pins['NC1']!.x = Number.NaN;
  const read = JSON.parse(JSON.stringify(text)) as typeof rec;
  assert.equal(read.pins['NC1']!.x, null);
  assert.equal(checkRecord('estop.svg', read, {}).length, 1);
  const side = structuredClone(rec);
  (side.pins['NC1'] as { side: string }).side = 'X';
  has(checkRecord('estop.svg', side, {}), 'anchor', /has no side N, E, S or W/);
  const shared = structuredClone(rec);
  const [first, second] = Object.keys(shared.pins);
  shared.pins[second!]!.labelId = shared.pins[first!]!.labelId;
  has(checkRecord('estop.svg', shared, {}), 'anchor', /serves 2 pins/);
});

test('an SVG that is not self-contained or lacks title, description or viewBox is a finding', () => {
  const rec = anchors().files['tap.svg']!;
  const svg = good.svgs['tap.svg']!;
  const f = (s: string): Finding[] => checkSvg('tap.svg', s, rec);
  assert.deepEqual(f(svg), []);
  // The root is svg in the SVG namespace and states its language twice.
  assert.deepEqual(checkSvg('tap.svg', svg, rec, 'en'), []);
  assert.deepEqual(checkSvg('de/tap.svg', good.svgs['de/tap.svg']!, rec, 'de'), []);
  has(checkSvg('de/tap.svg', svg, rec, 'de'), 'svg', /does not state the language de/);
  has(checkSvg('tap.svg', svg.replace(' xml:lang="en"', ''), rec, 'en'), 'svg', /does not state the language en/);
  has(checkSvg('tap.svg', svg.replace(' lang="en"', ''), rec, 'en'), 'svg', /does not state the language en/);
  has(f(svg.replace(' xmlns="http://www.w3.org/2000/svg"', '')), 'svg', /root element is not svg in the namespace/);
  has(f(svg.replace('xmlns="http://www.w3.org/2000/svg"', 'xmlns="http://www.w3.org/1999/xhtml"')), 'svg', /root element is not svg in the namespace/);
  has(f(svg.replace(' xmlns="http://www.w3.org/2000/svg"', ' xmlns:s="http://www.w3.org/2000/svg"')), 'svg', /root element is not svg in the namespace/);
  has(f(svg.replace(/^<svg/, '<div').replace(/<\/svg>\s*$/, '</div>')), 'svg', /root element is not svg in the namespace/);
  has(f(svg.replace('<svg ', '<svg xml:base="other.svg" ')), 'svg', /base address attribute/);
  has(f(svg.replace('</svg>', '<g xml:base="../x/"/></svg>')), 'svg', /base address attribute/);
  has(f(svg.replace('</svg>', '<g BASE="a"/></svg>')), 'svg', /base address attribute/);
  assert.deepEqual(f(svg.replace('</svg>', '<g baseline-shift="1"/></svg>')), []);
  const add = (s: string): string => svg.replace('</svg>', `${s}</svg>`);
  has(f(add('<script>1</script>')), 'svg', /script element/);
  has(f(add('<svg:script>1</svg:script>')), 'svg', /script element/);
  has(f(add('<x:SCRIPT/>')), 'svg', /script element/);
  has(f(add('<svg:image href="#a"/>')), 'svg', /contains an image/);
  has(f(add('<h:foreignObject/>')), 'svg', /contains an image/);
  assert.deepEqual(f(add('<g><scripted/></g>')).filter((x) => /script element/.test(x.msg)), []);
  has(f(add('<rect fill="URL(evil.png)"/>')), 'svg', /leaves the file/);
  has(f(add('<rect fill="Url( \'a.svg#b\')"/>')), 'svg', /leaves the file/);
  has(f(add('<rect fill="URL(#nothing)"/>')), 'svg', /reference to missing id nothing/);
  // The font stack, the title and the description are stated on the root element.
  has(f(svg.replace(/ font-family="[^"]*"/, '')), 'svg', /root element does not state a font stack/);
  has(f(svg.replace(/ font-family="[^"]*"/, '').replace('</svg>', '<g font-family="system-ui, sans-serif"/></svg>')), 'svg', /root element does not state a font stack/);
  const moved = (name: string): string => {
    const attr = new RegExp(` ${name}="[^"]*"`).exec(svg)![0];
    return svg.replace(attr, '').replace('</svg>', `<g${attr}/></svg>`);
  };
  has(f(moved('aria-labelledby')), 'svg', /does not name the title/);
  has(f(moved('aria-describedby')), 'svg', /does not name the description/);
  has(f(moved('viewBox')), 'svg', /viewBox differs/);
  has(f(add('<rect onclick="x"/>')), 'svg', /event handler/);
  has(f(add('<image href="a.png"/>')), 'svg', /contains an image/);
  has(f(add('<use href="other.svg#a"/>')), 'svg', /reference "other\.svg#a" leaves the file/);
  has(f(add('<rect fill="url(http://example.org/a)"/>')), 'svg', /leaves the file/);
  has(f(add('<rect fill="url(#nothing)"/>')), 'svg', /reference to missing id nothing/);
  has(f(add('<rect id="other-1"/>')), 'svg', /id other-1 lacks the prefix/);
  has(f(add('<text font-family="Arial">a</text>')), 'svg', /font stack/);
  has(f(svg.replace(/<title[^>]*>[^<]*<\/title>/, '')), 'svg', /no title element/);
  has(f(svg.replace(/<desc[^>]*>[^<]*<\/desc>/, '')), 'svg', /no desc element/);
  has(f(svg.replace(/ aria-labelledby="[^"]*"/, '')), 'svg', /does not name the title/);
  has(f(svg.replace(/ aria-describedby="[^"]*"/, '')), 'svg', /does not name the description/);
  has(f(svg.replace(/viewBox="0 0 [\d.]+/, 'viewBox="0 0 1')), 'svg', /viewBox differs/);
  has(f(svg.replace('</svg>', '')), 'svg', /not well formed/);
  const id = svg.match(/\sid="([^"]+)"/)![1]!;
  has(f(add(`<rect id="${id}"/>`)), 'svg', /an id is given twice/);
});

test('the well-formedness check accepts the subset the generator writes and names the first defect', () => {
  assert.equal(malformed('<svg a="1"><g><path d="M0 0"/></g><text>a &amp; b</text></svg>'), null);
  assert.equal(malformed('<svg/>'), null);
  assert.match(malformed('')!, /no root element/);
  assert.match(malformed('<svg><g></svg>')!, /<\/svg> closes <g>/);
  assert.match(malformed('<svg><g>')!, /<g> is not closed/);
  assert.match(malformed('<svg></svg></g>')!, /closes <nothing>/);
  assert.match(malformed('<svg></svg><svg></svg>')!, /second root element/);
  assert.match(malformed('<svg a=1></svg>')!, /not a tag/);
  assert.match(malformed('<svg a="1" a="2"></svg>')!, /attribute given twice/);
  assert.match(malformed('<svg>a & b</svg>')!, /bare &/);
  assert.match(malformed('<svg id="a&b"></svg>')!, /bare & in an attribute of <svg>/);
  assert.match(malformed('<svg><g a="1" b="x &amp y"/></svg>')!, /bare & in an attribute of <g>/);
  assert.match(malformed('<svg><g a="&"/></svg>')!, /bare & in an attribute/);
  assert.equal(malformed('<svg a="x &amp; y &#38; &#x26; &lt;"><g b="&quot;"/></svg>'), null);
  assert.match(malformed('<svg>a > b</svg>')!, /bare >/);
  assert.match(malformed('<svg>a\u0000b</svg>')!, /character U\+0000 at offset 6 is not a character of XML 1\.0/);
  assert.match(malformed('<svg a="\u001F"/>')!, /U\+001F/);
  assert.match(malformed('<svg>\u000B</svg>')!, /U\+000B/);
  assert.match(malformed('<svg>\uFFFE</svg>')!, /U\+FFFE/);
  assert.match(malformed('<svg>\uD83D</svg>')!, /U\+D83D/);
  assert.match(malformed('<svg>\uDE00x</svg>')!, /U\+DE00/);
  assert.equal(malformed('<svg a="\t">\n\r\u0020\u00A0\uD83D\uDE00\uFFFD</svg>'), null);
  for (const ref of ['&#0;', '&#x0;', '&#8;', '&#xB;', '&#xD800;', '&#xDFFF;', '&#xFFFE;', '&#65535;', '&#x110000;', '&#99999999999;']) {
    assert.match(malformed(`<svg>${ref}</svg>`)!, /names no character of XML 1\.0/, ref);
    assert.match(malformed(`<svg a="${ref}"/>`)!, /character reference .* at offset 8/, ref);
  }
  assert.equal(malformed('<svg a="&#9;&#xA;&#13;">&#x20;&#xD7FF;&#xE000;&#xFFFD;&#x10000;&#x10FFFF;</svg>'), null);
  assert.match(malformed('x<svg></svg>')!, /text outside the root element/);
  assert.match(malformed('<svg></svg>x')!, /text outside the root element/);
  assert.match(malformed('<svg>< g></svg>')!, /not a tag/);
  assert.match(malformed('<svg></svg a="1">')!, /carries attributes/);
});
