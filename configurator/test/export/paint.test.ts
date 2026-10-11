// The readers behind the drawing of the PDF export: the style sheet reader,
// the SVG (Scalable Vector Graphics) painter and the font reader, at their
// limits and on input they refuse.

import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { test } from 'node:test';
import type { SignalClass } from '../../contract.ts';
import { colour, parseCss, styleOf } from '../../src/export/css.ts';
import { lineSample, wiringDiagram } from '../../src/export/diagram.ts';
import { FontError, advance, codePoints, encode, missing, parseFont, use, width } from '../../src/export/font.ts';
import { num } from '../../src/export/pdf.ts';
import { SvgError, paintSvg, parseXml } from '../../src/export/svg.ts';
import { LINE_WORDS } from '../../src/export/text.ts';
import { CATALOGUE } from '../../src/core.ts';
import { CSS_TEXT, FONT_BYTES, LANGS, LARGEST, SMALLEST, css, fonts } from './helpers.ts';

const svg = (inner: string, attrs = 'viewBox="0 0 100 50"'): string => `<svg xmlns="http://www.w3.org/2000/svg" ${attrs}>${inner}</svg>`;
const paint = (inner: string, attrs?: string) => paintSvg(svg(inner, attrs), css(), fonts());
const refuses = (inner: string, pattern: RegExp, attrs?: string): void => {
  assert.throws(() => paint(inner, attrs), (error: unknown) => error instanceof SvgError && pattern.test(error.message), inner);
};

test('the style sheet reader takes the print tokens and skips no selector that styles the drawing', () => {
  const sheet = css();
  // Print set: black phase lines, white surface. The dark set never reaches the PDF.
  assert.equal(sheet.tokens['--w-phase'], '#000');
  assert.equal(sheet.tokens['--w-power'], '#c0182b');
  assert.notEqual(sheet.tokens['--w-canh'], '#fb923c');
  for (const selector of sheet.skipped) {
    assert.ok(!/\bsvg\b|\.c-|\.box|\.halo|\.pill|\.dot|\.w\b|\.m[tsu]\b|\.on\b/.test(selector), `skipped: ${selector}`);
  }
  assert.ok(sheet.rules.length > 50);
});

test('the style sheet reader: specificity, order, tokens, the font shorthand and at-rules', () => {
  const sheet = parseCss(`
    /* a comment { with braces } */
    :root { --a: #111; --b: #222; }
    @media (prefers-color-scheme: dark) { :root { --a: #eee; } }
    @media print { :root { --b: #333; } text { fill: #f00; } }
    text { font: 12px system-ui, sans-serif; fill: var(--a); }
    svg .t { font-size: 9px; fill: var(--b); }
    .t { fill: #456; font-weight: 700; }
    g text.t.u { fill: #789 !important; }
    text:hover { fill: #abc; }
    a > text, .q { stroke: #def; }
  `);
  assert.deepEqual(sheet.tokens, { '--a': '#111', '--b': '#333' });
  assert.deepEqual(sheet.skipped, ['text:hover', 'a > text']);
  const root = { tag: 'svg', classes: [] };
  assert.deepEqual(styleOf(sheet, [root, { tag: 'text', classes: [] }]), { 'font-size': '12px', 'font-weight': '400', fill: '#111' });
  // 1 class and 1 tag outweigh 1 class; the shorthand of the tag rule comes first.
  assert.deepEqual(styleOf(sheet, [root, { tag: 'text', classes: ['t'] }]), { 'font-size': '9px', 'font-weight': '700', fill: '#333' });
  assert.equal(styleOf(sheet, [root, { tag: 'g', classes: [] }, { tag: 'text', classes: ['u', 't'] }]).fill, '#789');
  // The descendant combinator needs the ancestor.
  assert.equal(styleOf(sheet, [{ tag: 'text', classes: ['t'] }]).fill, '#456');
  assert.equal(styleOf(sheet, [root, { tag: 'rect', classes: ['q'] }]).stroke, '#def');
  assert.deepEqual(styleOf(sheet, [root, { tag: 'rect', classes: [] }]), {});
  // The same path gives the same object: results are memoised per sheet.
  assert.equal(styleOf(sheet, [root, { tag: 'rect', classes: [] }]), styleOf(sheet, [root, { tag: 'rect', classes: [] }]));
  const bold = parseCss('text { font: italic bold 10px serif; } .n { font: 600 8px serif; }');
  assert.equal(styleOf(bold, [{ tag: 'text', classes: [] }])['font-weight'], 'bold');
  assert.deepEqual(styleOf(bold, [{ tag: 'text', classes: ['n'] }]), { 'font-size': '8px', 'font-weight': '600' });
});

test('a rule that names a token the sheet does not hold is an error', () => {
  const sheet = parseCss('.a { fill: var(--none); }');
  assert.throws(() => styleOf(sheet, [{ tag: 'rect', classes: ['a'] }]), /no token --none/);
  assert.deepEqual(parseCss('').rules, []);
  assert.deepEqual(parseCss('.a { fill: red').rules, []);
});

test('a colour is read from 3 or 6 hexadecimal digits, black, white and none', () => {
  assert.deepEqual(colour('#000'), [0, 0, 0]);
  assert.deepEqual(colour(' #FFFFFF '), [1, 1, 1]);
  assert.deepEqual(colour('#f00'), [1, 0, 0]);
  assert.deepEqual(colour('white'), [1, 1, 1]);
  assert.deepEqual(colour('black'), [0, 0, 0]);
  assert.equal(colour('none'), null);
  assert.equal(colour('transparent'), null);
  const c = colour('#c0182b') as number[];
  assert.ok(Math.abs((c[0] as number) - 192 / 255) < 1e-12);
  for (const bad of ['red', 'rgb(0,0,0)', '#12', '#12345', '#1234567', '#ggg', '', 'currentColor', 'url(#a)']) {
    assert.throws(() => colour(bad), /reads no colour/, bad);
  }
});

/** Hue in degrees, saturation and lightness from 0 to 1. */
function hsl(rgb: readonly number[]): [number, number, number] {
  const [r, g, b] = rgb as [number, number, number];
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const l = (max + min) / 2;
  if (max === min) return [0, 0, l];
  const d = max - min;
  const h = max === r ? ((g - b) / d + 6) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return [h * 60, d / (1 - Math.abs(2 * l - 1)), l];
}

test('the colour and pattern words of the table match the lines the print tokens draw', () => {
  const sheet = css();
  // Hue bands in degrees per colour word. Grey and black are told by saturation and lightness.
  const hues: Record<string, [number, number]> = {
    orange: [15, 40], brown: [35, 50], green: [100, 160], teal: [170, 200], blue: [205, 250],
    violet: [250, 275], magenta: [280, 310], pink: [315, 345], red: [345, 360],
  };
  const patterns: Record<string, (dash: number[]) => boolean> = {
    solid: (d) => d.length === 0,
    dashed: (d) => d.length === 2 && (d[0] as number) >= 6 && (d[0] as number) < 12,
    'long dashes': (d) => d.length === 2 && (d[0] as number) >= 12,
    'short dashes': (d) => d.length === 2 && (d[0] as number) >= 3 && (d[0] as number) < 6,
    dotted: (d) => d.length === 2 && (d[0] as number) <= 2,
    'dash-dot': (d) => d.length === 4,
    'dash-dot-dot': (d) => d.length === 6,
  };
  for (const cls of Object.keys(LINE_WORDS) as SignalClass[]) {
    const style = styleOf(sheet, [{ tag: 'svg', classes: [] }, { tag: 'path', classes: ['w', `c-${cls}`] }]);
    const [h, s, l] = hsl(colour(style.stroke as string) as number[]);
    const words = LINE_WORDS[cls][0].split(', ');
    const word = words[0] as string;
    if (word === 'grey') assert.ok(s < 0.2 && l > 0.2 && l < 0.6, `${cls}: grey`);
    else if (word === 'black') assert.ok(l < 0.05, `${cls}: black`);
    else {
      const band = hues[word];
      assert.ok(band, `${cls}: no hue band for "${word}"`);
      assert.ok(s >= 0.5 && h >= band[0] && h <= band[1], `${cls}: ${word} at hue ${h.toFixed(0)}`);
    }
    const dash = (style['stroke-dasharray'] ?? 'none') === 'none' ? [] : (style['stroke-dasharray'] as string).split(/\s+/).map(Number);
    const check = patterns[words[1] as string];
    assert.ok(check, `${cls}: no pattern check for "${words[1]}"`);
    assert.ok(check(dash), `${cls}: ${words[1]} for ${dash.join(' ')}`);
    const wide = Number(style['stroke-width']);
    assert.equal(words.includes('thin'), wide < 2, `${cls}: thin at ${wide}`);
    assert.equal(words.includes('thick'), wide > 4, `${cls}: thick at ${wide}`);
    assert.equal(words.includes('wide gaps'), dash.length === 2 && (dash[1] as number) >= 4 * (dash[0] as number), `${cls}: wide gaps`);
    // Both languages give the same number of words.
    assert.equal(LINE_WORDS[cls][1].split(', ').length, words.length);
  }
  // No two classes share their words: the words tell every line apart.
  for (const lang of [0, 1] as const) {
    const all = Object.values(LINE_WORDS).map((w) => w[lang]);
    assert.equal(new Set(all).size, all.length);
  }
});

test('the painter turns shapes into path operators with the style of the sheet', () => {
  assert.equal(paint('').ops, '0 0 100 50 re\nW n\n');
  assert.equal(paint('', 'viewBox="10 -5 100 50"').ops, '1 0 0 1 -10 5 cm\n10 -5 100 50 re\nW n\n');
  const rect = paint('<rect x="1" y="2" width="30" height="4" fill="#f00"/>');
  assert.ok(rect.ops.endsWith('q\n1 0 0 rg\n1 2 30 4 re\nf\nQ\n'));
  assert.deepEqual(rect.boxes, [{ classes: [], x: 1, y: 2, w: 30, h: 4 }]);
  // Stroke and fill, dash, cap and join.
  const both = paint('<rect width="3" height="4" fill="#fff" stroke="#000" stroke-width="2" stroke-dasharray="5,4" stroke-linecap="round" stroke-linejoin="bevel"/>');
  assert.ok(both.ops.endsWith('q\n1 1 1 rg\n0 0 0 RG\n2 w\n1 J\n2 j\n[5 4] 0 d\n0 0 3 4 re\nB\nQ\n'));
  // No fill and no stroke: nothing is painted. A width or height of 0: nothing.
  assert.equal(paint('<rect width="3" height="4" fill="none"/>').ops, paint('').ops);
  assert.equal(paint('<rect width="0" height="4"/><circle r="0"/>').ops, paint('').ops);
  assert.equal(paint('<rect width="3" height="4" fill="none" stroke="#000" stroke-width="0"/>').ops, paint('').ops);
  // A dash pattern of zeros is a solid line.
  assert.ok(!paint('<line x2="5" stroke="#000" stroke-dasharray="0 0"/>').ops.includes(' d\n'));
  // A line never fills.
  assert.ok(paint('<line x1="1" y1="2" x2="5" y2="6" stroke="#000" fill="#f00"/>').ops.endsWith('0 J\n0 j\n1 2 m\n5 6 l\nS\nQ\n'));
  // Rounded corners and circles are 4 curves.
  assert.equal((paint('<rect width="30" height="20" rx="4"/>').ops.match(/ c\n/g) ?? []).length, 4);
  assert.equal((paint('<rect width="30" height="20" ry="4"/>').ops.match(/ c\n/g) ?? []).length, 4);
  assert.equal((paint('<circle cx="5" cy="5" r="2"/>').ops.match(/ c\n/g) ?? []).length, 4);
  // A radius above half the side is cut to half the side.
  assert.ok(paint('<rect width="10" height="4" rx="50"/>').ops.includes('5 0 m\n5 0 l\n'));
  // title and desc are not painted.
  assert.equal(paint('<title>a</title><desc>b</desc>').ops, paint('').ops);
});

test('the painter reads the path commands M L H V C Z in both cases', () => {
  const ops = (d: string): string => paint(`<path d="${d}" fill="none" stroke="#000"/>`).ops.split('0 j\n')[1] as string;
  assert.equal(ops('M1 2L3 4H10V20Z'), '1 2 m\n3 4 l\n10 4 l\n10 20 l\nh\nS\nQ\n');
  assert.equal(ops('m1,2 l3,4 h10 v20 z'), '1 2 m\n4 6 l\n14 6 l\n14 26 l\nh\nS\nQ\n');
  // Coordinates after M without a command are lines; after z the point is the start.
  assert.equal(ops('M1 2 3 4 5 6'), '1 2 m\n3 4 l\n5 6 l\nS\nQ\n');
  assert.equal(ops('m1 2 3 4zl1 1'), '1 2 m\n4 6 l\nh\n2 3 l\nS\nQ\n');
  assert.equal(ops('M0 0C1 2 3 4 5 6c1 1 2 2 3 3'), '0 0 m\n1 2 3 4 5 6 c\n6 7 7 8 8 9 c\nS\nQ\n');
  assert.equal(ops('M-1.5-2.5L.5.25 1e1 2e-1'), '-1.5 -2.5 m\n0.5 0.25 l\n10 0.2 l\nS\nQ\n');
  assert.equal(paint('<path d="" stroke="#000"/>').ops.split('0 j\n')[1], 'B\nQ\n');
  refuses('<path d="M0 0A5 5 0 0 1 10 10"/>', /path command A/);
  refuses('<path d="M0 0Q1 1 2 2"/>', /path command Q/);
  refuses('<path d="M0 0S1 1 2 2"/>', /path command S/);
  refuses('<path d="M0 0L1"/>', /ends inside a command/);
  refuses('<path d="1 2L3 4"/>', /starts without a command/);
  refuses('<path d="M0 0Z1 2"/>', /starts without a command/);
  refuses('<path d="M0 0 L 1 % 2"/>', /reads no path/);
  refuses('<path d="M0 0"><animate/></path>', /child element of a path/);
});

test('the painter sets text in the embedded font with anchor, weight and fitted length', () => {
  const f = fonts();
  const plain = paintSvg(svg('<text x="10" y="20">A  µ\n Ω</text>'), css(), f);
  // The sheet sets 12 px and the ink of the print set.
  assert.match(plain.ops, /BT\n\/F1 12 Tf\n1 0 0 -1 10 20 Tm\n<[0-9A-F]{20}> Tj\nET/);
  assert.equal(plain.texts.length, 1);
  const t = plain.texts[0] as (typeof plain.texts)[number];
  assert.equal(t.text, 'A µ Ω');
  assert.ok(Math.abs(t.x2 - t.x1 - width(f.regular.font, 'A µ Ω') * 12) < 1e-9);
  assert.equal(f.regular.used.size, 4);
  assert.equal(f.bold.used.size, 0);

  const mid = paint('<text x="50" y="20" text-anchor="middle">abc</text>').texts[0] as { x1: number; x2: number };
  assert.ok(Math.abs((mid.x1 + mid.x2) / 2 - 50) < 1e-9);
  const end = paint('<text x="50" y="20" text-anchor="end">abc</text>').texts[0] as { x1: number; x2: number };
  assert.ok(Math.abs(end.x2 - 50) < 1e-9);
  // Classes of the sheet: .mt is 13 px at weight 600, which is the bold face.
  assert.match(paint('<text class="mt" y="9">abc</text>').ops, /\/F2 13 Tf/);
  assert.match(paint('<text class="ms" y="9">abc</text>').ops, /\/F1 11 Tf/);
  // The style of a group reaches its text; the sheet outweighs the attribute.
  assert.match(paint('<g font-weight="700" fill="#f00"><text font-size="20" y="9">abc</text></g>').ops, /\/F1 12 Tf/);
  assert.match(paint('<g class="pill"><rect width="9" height="9"/><text y="9">1</text></g>').ops, /\/F2 11 Tf/);
  // A fitted length sets the horizontal scale and the recorded extent.
  const fit = paint('<text x="5" y="9" textLength="30" lengthAdjust="spacingAndGlyphs">abc</text>');
  assert.match(fit.ops, /\d+(\.\d+)? Tz\n1 0 0 -1 5 9 Tm/);
  assert.equal((fit.texts[0] as { x2: number }).x2, 35);
  refuses('<text textLength="30">abc</text>', /textLength only with/);
  refuses('<text textLength="0" lengthAdjust="spacingAndGlyphs">abc</text>', /textLength only with/);
  refuses('<text text-anchor="inherit">abc</text>', /text-anchor/);
  refuses('<text>a<tspan>b</tspan></text>', /child element of a text/);
  // Empty text and text without fill paint nothing.
  assert.equal(paint('<text>  </text><text fill="none" class="none">a</text>').texts.length, 1);
  assert.throws(() => paint('<text>中</text>'), (error: unknown) => error instanceof FontError && /U\+4E2D/.test(error.message));
});

test('the painter applies transforms and records no box or text under one', () => {
  const moved = paint('<g transform="translate(5 6) scale(2)"><rect width="3" height="4"/><text y="3">a</text></g>');
  assert.ok(moved.ops.includes('q\n2 0 0 2 5 6 cm\n'));
  assert.ok(moved.ops.endsWith('Q\nQ\n'));
  assert.deepEqual(moved.boxes, []);
  assert.deepEqual(moved.texts, []);
  assert.ok(paint('<g transform="translate(5)"/>').ops.includes('1 0 0 1 5 0 cm'));
  assert.ok(paint('<g transform="scale(2,3)"/>').ops.includes('2 0 0 3 0 0 cm'));
  assert.ok(paint('<g transform="matrix(1 2 3 4 5 6)"/>').ops.includes('1 2 3 4 5 6 cm'));
  assert.ok(paint('<g transform="rotate(90)"/>').ops.includes('0 1 -1 0 0 0 cm'));
  assert.ok(paint('<g transform=""/>').ops.includes('1 0 0 1 0 0 cm'));
  refuses('<g transform="skewX(10)"/>', /reads no transform/);
  refuses('<g transform="rotate(90 5 5)"/>', /reads no transform/);
  refuses('<g transform="translate(a)"/>', /reads no transform/);
  refuses('<g transform="matrix(1 2 3)"/>', /reads no transform/);
  refuses('', /transform on the svg root/, 'viewBox="0 0 1 1" transform="scale(2)"');
});

test('the painter refuses what it does not paint', () => {
  for (const tag of ['ellipse', 'polygon', 'polyline', 'image', 'use', 'defs', 'style', 'foreignObject', 'a', 'clipPath', 'linearGradient', 'script', 'tspan']) {
    refuses(`<${tag}/>`, new RegExp(`element <${tag}>`));
  }
  for (const attr of ['style', 'opacity', 'fill-opacity', 'stroke-opacity', 'clip-path', 'mask', 'filter', 'fill-rule', 'marker-start', 'marker-mid', 'marker-end', 'dx', 'dy', 'rotate', 'href', 'xlink:href']) {
    refuses(`<g><rect width="1" height="1" ${attr}="x"/></g>`, new RegExp(`attribute ${attr} of <rect>`));
  }
  refuses('<svg viewBox="0 0 1 1"/>', /nested svg/);
  refuses('<rect width="1em" height="1"/>', /no length from width="1em"/);
  refuses('<rect width="50%" height="1"/>', /no length/);
  refuses('<rect width="1" height="1" fill="url(#g)"/>', /no colour from fill/);
  refuses('<rect width="1" height="1" stroke="red"/>', /no colour from stroke/);
  refuses('<line x2="1" stroke="#000" stroke-linecap="arrow"/>', /line cap/);
  refuses('<line x2="1" stroke="#000" stroke-dasharray="-1 2"/>', /negative length/);
  for (const attrs of ['', 'viewBox="0 0 100"', 'viewBox="0 0 0 50"', 'viewBox="0 0 100 -5"', 'viewBox="a b c d"', 'width="100" height="50"']) {
    assert.throws(() => paintSvg(`<svg ${attrs}></svg>`, css(), fonts()), /no viewBox/, attrs);
  }
});

test('the markup reader refuses markup that is not well-formed', () => {
  assert.equal(parseXml('<?xml version="1.0"?><!-- a --><svg a="1" b=\'2\'><g/></svg>').children.length, 1);
  assert.deepEqual(parseXml('<svg a="&lt;&amp;&#181;&#x3a9;&quot;&apos;&gt;"/>').attrs, { a: '<&µΩ"\'>' });
  assert.equal((parseXml('<svg><text>a &amp; b</text></svg>').children[0] as { text: string }).text, 'a & b');
  for (const [bad, pattern] of [
    ['', /no single svg root/],
    ['<g/>', /no single svg root/],
    ['<svg/><svg/>', /no single svg root/],
    ['<svg>', /not well-formed/],
    ['<svg></g>', /out of order/],
    ['</svg>', /out of order/],
    ['<svg><g></svg></g>', /out of order/],
    ['<svg a=1/>', /not well-formed/],
    ['<svg a="1" <g/>', /not well-formed/],
    ['<svg>&nbsp;</svg>', /unknown entity &nbsp;/],
    ['<svg a="&auml;"/>', /unknown entity &auml;/],
    ['<svg><![CDATA[x]]></svg>', /not well-formed/],
  ] as const) {
    assert.throws(() => parseXml(bad), (error: unknown) => error instanceof SvgError && pattern.test(error.message), bad);
  }
});

test('the drawing and the line samples of the page paint without a refusal', () => {
  for (const lang of LANGS) {
    for (const sample of [SMALLEST, LARGEST]) {
      const painted = paintSvg(wiringDiagram(sample.setup, CATALOGUE, lang), css(), fonts());
      assert.ok(painted.width > 0 && painted.height > 0);
      assert.ok(painted.boxes.length >= sample.setup.parts.length);
      assert.ok(painted.texts.length > 0);
      // Every wire is a stroked path: at least as many strokes as wires.
      assert.ok((painted.ops.match(/\nS\nQ\n/g) ?? []).length >= sample.setup.wires.length);
    }
  }
  for (const cls of Object.keys(LINE_WORDS) as SignalClass[]) {
    const painted = paintSvg(lineSample(cls), css(), fonts());
    const stroke = colour(css().tokens[`--w-${cls}`] as string) as number[];
    assert.ok(painted.ops.includes(`${stroke.map(num).join(' ')} RG`), cls);
  }
});

test('the font files: size, licence, tables and the characters the catalogue needs', () => {
  assert.ok(existsSync(new URL('../../src/export/font/OFL.txt', import.meta.url)));
  const licence = readFileSync(new URL('../../src/export/font/OFL.txt', import.meta.url), 'utf8');
  assert.match(licence, /SIL OPEN FONT LICENSE Version 1\.1/);
  assert.match(licence, /Copyright .* The Inter Project Authors/);
  assert.ok(FONT_BYTES.regular.length < 30000 && FONT_BYTES.bold.length < 30000);
  const regular = parseFont(FONT_BYTES.regular);
  const bold = parseFont(FONT_BYTES.bold);
  for (const font of [regular, bold]) {
    assert.equal(font.unitsPerEm, 2048);
    assert.deepEqual(missing(font, 'ÄÖÜäöüß Ω µ ° − ± × → ← ≤ ≥ „“ ‚‘ – — … · • € §   ‑'), []);
    assert.deepEqual(missing(font, 'a中中😀'), ['中', '😀']);
    assert.equal(width(font, ''), 0);
    assert.ok(width(font, 'W') > width(font, 'i'));
    assert.equal(width(font, 'a\nb'), width(font, 'a b'));
    assert.equal(advance(font, 0x41) / font.unitsPerEm, width(font, 'A'));
    assert.throws(() => width(font, '中'), FontError);
    assert.throws(() => advance(font, 0x1f600), /U\+1F600/);
  }
  assert.ok(width(bold, 'Wiring') > width(regular, 'Wiring'));
  assert.deepEqual(codePoints('a\tb\r\n😀'), [0x61, 0x20, 0x62, 0x20, 0x20, 0x1f600]);
  const used = use(regular);
  assert.equal(encode(used, ''), '');
  const hex = encode(used, 'aba');
  assert.equal(hex.length, 12);
  assert.equal(hex.slice(0, 4), hex.slice(8));
  assert.equal(used.used.size, 2);
  assert.deepEqual([...used.used.values()].sort(), [0x61, 0x62]);
  assert.throws(() => encode(used, '中'), FontError);
});

test('a font file that is cut, empty or of another format is an error', () => {
  for (const cut of [0, 4, 11, 12, 200, 5000, FONT_BYTES.regular.length - 1]) {
    assert.throws(() => parseFont(FONT_BYTES.regular.slice(0, cut)), FontError, `cut at ${cut}`);
  }
  const otf = FONT_BYTES.regular.slice();
  otf.set([0x4f, 0x54, 0x54, 0x4f]);
  assert.throws(() => parseFont(otf), FontError);
  assert.throws(() => parseFont(new Uint8Array(4096)), FontError);
  assert.ok(CSS_TEXT.length > 0);
});
