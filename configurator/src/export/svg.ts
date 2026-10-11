// Paints an SVG (Scalable Vector Graphics) document as PDF content operators.
// The output is vector: paths stay paths, text stays text in the embedded font.
//
// Supported: svg (root), g, rect, circle, line, path with the commands
// M L H V C Z in both cases, text without child elements, title and desc
// (not painted). Supported styling: fill, stroke, stroke-width,
// stroke-dasharray, stroke-linecap, stroke-linejoin, font-size, font-weight,
// text-anchor, as attributes or from the style sheet, and the transform
// attribute. Anything else raises SvgError: a drawing with a missing element
// is not exported.
//
// Operators are in the user space of the SVG, y down. The caller places them
// with a matrix that flips y.

import { colour, styleOf } from './css.ts';
import type { Css, Decls, Node } from './css.ts';
import { encode, width } from './font.ts';
import type { Fonts } from './font.ts';
import { num } from './pdf.ts';

export class SvgError extends Error {}

type El = { tag: string; attrs: Record<string, string>; children: El[]; text: string };
type Matrix = [number, number, number, number, number, number];

export type PaintedText = { text: string; classes: string[]; x1: number; x2: number; y: number; size: number };
export type PaintedBox = { classes: string[]; x: number; y: number; w: number; h: number };
export type Painted = { width: number; height: number; ops: string; texts: PaintedText[]; boxes: PaintedBox[] };

const ENTITIES: Record<string, string> = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" };

function unescape(text: string): string {
  return text.replace(/&(#x[0-9a-fA-F]+|#\d+|\w+);/g, (all, name: string) => {
    if (name.startsWith('#x')) return String.fromCodePoint(parseInt(name.slice(2), 16));
    if (name.startsWith('#')) return String.fromCodePoint(Number(name.slice(1)));
    const known = ENTITIES[name];
    if (known === undefined) throw new SvgError(`The drawing holds the unknown entity ${all}.`);
    return known;
  });
}

export function parseXml(source: string): El {
  const token = /<!--[\s\S]*?-->|<\?[\s\S]*?\?>|<\/([\w:-]+)\s*>|<([\w:-]+)((?:\s+[\w:-]+\s*=\s*(?:"[^"]*"|'[^']*'))*)\s*(\/?)>|([^<]+)/gy;
  const root: El = { tag: '#root', attrs: {}, children: [], text: '' };
  const stack: El[] = [root];
  let at = 0;
  for (;;) {
    token.lastIndex = at;
    const m = token.exec(source);
    if (!m) break;
    at = token.lastIndex;
    const top = stack[stack.length - 1] as El;
    if (m[1] !== undefined) {
      if (stack.length < 2 || top.tag !== m[1]) throw new SvgError(`The drawing closes <${m[1]}> out of order.`);
      stack.pop();
    } else if (m[2] !== undefined) {
      const attrs: Record<string, string> = {};
      for (const a of (m[3] ?? '').matchAll(/([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/g)) attrs[a[1] as string] = unescape(a[2] ?? a[3] ?? '');
      const el: El = { tag: m[2], attrs, children: [], text: '' };
      top.children.push(el);
      if (m[4] !== '/') stack.push(el);
    } else if (m[5] !== undefined) top.text += unescape(m[5]);
  }
  if (at !== source.length || stack.length !== 1) throw new SvgError('The drawing is not well-formed markup.');
  const svg = root.children[0];
  if (root.children.length !== 1 || !svg || svg.tag !== 'svg') throw new SvgError('The drawing has no single svg root.');
  return svg;
}

const INHERITED = ['fill', 'stroke', 'stroke-width', 'stroke-dasharray', 'stroke-linecap', 'stroke-linejoin', 'font-size', 'font-weight', 'text-anchor'];
/** Attributes that change the picture and that the painter does not read. */
const REFUSED = ['style', 'opacity', 'fill-opacity', 'stroke-opacity', 'clip-path', 'mask', 'filter', 'fill-rule', 'marker-start', 'marker-mid', 'marker-end', 'dx', 'dy', 'rotate', 'href', 'xlink:href'];

function length(value: string | undefined, fallback: number, what: string): number {
  if (value === undefined || value === '') return fallback;
  const m = /^\s*(-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?)(?:px)?\s*$/i.exec(value);
  if (!m) throw new SvgError(`The export reads no length from ${what}="${value}".`);
  return Number(m[1]);
}

function paint(value: string | undefined, what: string): [number, number, number] | null {
  if (value === undefined) return null;
  try {
    return colour(value);
  } catch {
    throw new SvgError(`The export reads no colour from ${what}="${value}".`);
  }
}

function matrixOf(value: string): Matrix {
  let m: Matrix = [1, 0, 0, 1, 0, 0];
  const mul = (p: Matrix, q: Matrix): Matrix => [
    q[0] * p[0] + q[1] * p[2], q[0] * p[1] + q[1] * p[3],
    q[2] * p[0] + q[3] * p[2], q[2] * p[1] + q[3] * p[3],
    q[4] * p[0] + q[5] * p[2] + p[4], q[4] * p[1] + q[5] * p[3] + p[5],
  ];
  let rest = value.trim();
  while (rest !== '') {
    const f = /^(matrix|translate|scale|rotate)\s*\(([^)]*)\)\s*,?\s*/.exec(rest);
    if (!f) throw new SvgError(`The export reads no transform from "${value}".`);
    rest = rest.slice(f[0].length);
    const v = (f[2] as string).trim().split(/[\s,]+/).map(Number);
    if (v.some((x) => !Number.isFinite(x))) throw new SvgError(`The export reads no transform from "${value}".`);
    let next: Matrix;
    if (f[1] === 'matrix' && v.length === 6) next = v as Matrix;
    else if (f[1] === 'translate' && v.length >= 1 && v.length <= 2) next = [1, 0, 0, 1, v[0] as number, v[1] ?? 0];
    else if (f[1] === 'scale' && v.length >= 1 && v.length <= 2) next = [v[0] as number, 0, 0, v[1] ?? (v[0] as number), 0, 0];
    else if (f[1] === 'rotate' && v.length === 1) {
      const a = ((v[0] as number) * Math.PI) / 180;
      next = [Math.cos(a), Math.sin(a), -Math.sin(a), Math.cos(a), 0, 0];
    } else throw new SvgError(`The export reads no transform from "${value}".`);
    m = mul(m, next);
  }
  return m;
}

const K = 0.5522847498;

function roundRect(x: number, y: number, w: number, h: number, rx: number, ry: number): string {
  if (rx <= 0 || ry <= 0) return `${num(x)} ${num(y)} ${num(w)} ${num(h)} re\n`;
  const r = Math.min(rx, w / 2);
  const s = Math.min(ry, h / 2);
  const p = (a: number, b: number): string => `${num(a)} ${num(b)}`;
  return `${p(x + r, y)} m\n${p(x + w - r, y)} l\n`
    + `${p(x + w - r + r * K, y)} ${p(x + w, y + s - s * K)} ${p(x + w, y + s)} c\n${p(x + w, y + h - s)} l\n`
    + `${p(x + w, y + h - s + s * K)} ${p(x + w - r + r * K, y + h)} ${p(x + w - r, y + h)} c\n${p(x + r, y + h)} l\n`
    + `${p(x + r - r * K, y + h)} ${p(x, y + h - s + s * K)} ${p(x, y + h - s)} c\n${p(x, y + s)} l\n`
    + `${p(x, y + s - s * K)} ${p(x + r - r * K, y)} ${p(x + r, y)} c\nh\n`;
}

function pathOps(d: string): string {
  const tokens = d.match(/[a-zA-Z]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?/g) ?? [];
  if (tokens.join('').length !== d.replace(/[\s,]+/g, '').length) throw new SvgError(`The export reads no path from "${d.slice(0, 40)}".`);
  let out = '';
  let i = 0;
  let x = 0;
  let y = 0;
  let sx = 0;
  let sy = 0;
  let cmd = '';
  const more = (): boolean => i < tokens.length && !/^[a-zA-Z]$/.test(tokens[i] as string);
  const next = (): number => {
    if (!more()) throw new SvgError(`A path of the drawing ends inside a command: "${d.slice(0, 40)}".`);
    return Number(tokens[i++]);
  };
  while (i < tokens.length) {
    if (/^[a-zA-Z]$/.test(tokens[i] as string)) cmd = tokens[i++] as string;
    else if (cmd === '' || cmd === 'Z' || cmd === 'z') throw new SvgError(`A path of the drawing starts without a command: "${d.slice(0, 40)}".`);
    const rel = cmd === cmd.toLowerCase();
    const ox = rel ? x : 0;
    const oy = rel ? y : 0;
    switch (cmd.toUpperCase()) {
      case 'M':
        x = ox + next();
        y = oy + next();
        sx = x;
        sy = y;
        out += `${num(x)} ${num(y)} m\n`;
        cmd = rel ? 'l' : 'L';
        break;
      case 'L':
        x = ox + next();
        y = oy + next();
        out += `${num(x)} ${num(y)} l\n`;
        break;
      case 'H':
        x = ox + next();
        out += `${num(x)} ${num(y)} l\n`;
        break;
      case 'V':
        y = oy + next();
        out += `${num(x)} ${num(y)} l\n`;
        break;
      case 'C': {
        const c = [next(), next(), next(), next(), next(), next()] as const;
        out += `${num(ox + c[0])} ${num(oy + c[1])} ${num(ox + c[2])} ${num(oy + c[3])} ${num(ox + c[4])} ${num(oy + c[5])} c\n`;
        x = ox + c[4];
        y = oy + c[5];
        break;
      }
      case 'Z':
        out += 'h\n';
        x = sx;
        y = sy;
        break;
      default:
        throw new SvgError(`The export does not paint the path command ${cmd}.`);
    }
  }
  return out;
}

const CAPS: Record<string, number> = { butt: 0, round: 1, square: 2 };
const JOINS: Record<string, number> = { miter: 0, round: 1, bevel: 2 };

/** Graphics state and painting operator for a shape, or '' when nothing is painted. */
function finish(shape: string, style: Decls): string {
  const fill = paint(style.fill ?? 'black', 'fill');
  const stroke = paint(style.stroke ?? 'none', 'stroke');
  const line = length(style['stroke-width'], 1, 'stroke-width');
  const stroked = stroke !== null && line > 0;
  if (fill === null && !stroked) return '';
  let out = 'q\n';
  if (fill) out += `${fill.map(num).join(' ')} rg\n`;
  if (stroked) {
    out += `${stroke.map(num).join(' ')} RG\n${num(line)} w\n`;
    const cap = CAPS[style['stroke-linecap'] ?? 'butt'];
    const join = JOINS[style['stroke-linejoin'] ?? 'miter'];
    if (cap === undefined || join === undefined) throw new SvgError('The export does not paint this line cap or line join.');
    out += `${cap} J\n${join} j\n`;
    const dash = (style['stroke-dasharray'] ?? 'none').trim();
    if (dash !== 'none') {
      const parts = dash.split(/[\s,]+/).map((v) => length(v, 0, 'stroke-dasharray'));
      if (parts.some((v) => v < 0)) throw new SvgError('A dash pattern of the drawing holds a negative length.');
      if (parts.some((v) => v > 0)) out += `[${parts.map(num).join(' ')}] 0 d\n`;
    }
  }
  return `${out}${shape}${fill && stroked ? 'B' : fill ? 'f' : 'S'}\nQ\n`;
}

export function paintSvg(source: string, css: Css, fonts: Fonts): Painted {
  const svg = parseXml(source);
  const view = (svg.attrs.viewBox ?? '').trim().split(/[\s,]+/).map(Number);
  if (view.length !== 4 || view.some((v) => !Number.isFinite(v)) || (view[2] as number) <= 0 || (view[3] as number) <= 0) {
    throw new SvgError('The drawing has no viewBox with a width and a height above 0.');
  }
  const texts: PaintedText[] = [];
  const boxes: PaintedBox[] = [];
  const [vx, vy, vw, vh] = view as [number, number, number, number];
  // The origin of the viewBox moves to 0,0. Nothing is painted outside the viewBox.
  let ops = `${vx !== 0 || vy !== 0 ? `1 0 0 1 ${num(-vx)} ${num(-vy)} cm\n` : ''}${num(vx)} ${num(vy)} ${num(vw)} ${num(vh)} re\nW n\n`;

  const walk = (el: El, path: Node[], inherited: Decls, upright: boolean): void => {
    for (const name of REFUSED) if (el.attrs[name] !== undefined) throw new SvgError(`The export does not paint the attribute ${name} of <${el.tag}>.`);
    const node: Node = { tag: el.tag, classes: (el.attrs.class ?? '').split(/\s+/).filter((c) => c !== '') };
    const here = [...path, node];
    const style: Decls = { ...inherited };
    for (const name of INHERITED) if (el.attrs[name] !== undefined) style[name] = el.attrs[name];
    Object.assign(style, styleOf(css, here));
    const down: Decls = {};
    for (const name of INHERITED) if (style[name] !== undefined) down[name] = style[name];
    const a = (name: string, fallback = 0): number => length(el.attrs[name], fallback, name);

    let level = upright;
    if (el.attrs.transform !== undefined) {
      if (el.tag === 'svg') throw new SvgError('The export does not paint a transform on the svg root.');
      const m = matrixOf(el.attrs.transform);
      ops += `q\n${m.map(num).join(' ')} cm\n`;
      level = false;
    }
    switch (el.tag) {
      case 'svg':
        if (path.length > 0) throw new SvgError('The export does not paint a nested svg element.');
        for (const child of el.children) walk(child, here, down, level);
        break;
      case 'g':
        for (const child of el.children) walk(child, here, down, level);
        break;
      case 'title':
      case 'desc':
        break;
      case 'rect': {
        const rx = a('rx', el.attrs.ry === undefined ? 0 : a('ry'));
        const box = { x: a('x'), y: a('y'), w: a('width'), h: a('height') };
        if (box.w > 0 && box.h > 0) {
          ops += finish(roundRect(box.x, box.y, box.w, box.h, rx, a('ry', rx)), style);
          if (level) boxes.push({ classes: node.classes, ...box });
        }
        break;
      }
      case 'circle': {
        const r = a('r');
        const cx = a('cx');
        const cy = a('cy');
        if (r > 0) ops += finish(roundRect(cx - r, cy - r, 2 * r, 2 * r, r, r), style);
        break;
      }
      case 'line':
        ops += finish(`${num(a('x1'))} ${num(a('y1'))} m\n${num(a('x2'))} ${num(a('y2'))} l\n`, { ...style, fill: 'none' });
        break;
      case 'path':
        if (el.children.some((c) => c.tag !== 'title' && c.tag !== 'desc')) throw new SvgError('The export does not paint a child element of a path.');
        ops += finish(pathOps(el.attrs.d ?? ''), style);
        break;
      case 'text': {
        if (el.children.length > 0) throw new SvgError('The export does not paint a child element of a text.');
        const text = el.text.replace(/\s+/g, ' ').trim();
        if (text === '') break;
        const fill = paint(style.fill ?? 'black', 'fill');
        if (fill === null) break;
        const size = length(style['font-size'], 16, 'font-size');
        const weight = style['font-weight'] ?? '400';
        const bold = weight === 'bold' || Number(weight) >= 600;
        const font = bold ? fonts.bold : fonts.regular;
        const natural = width(font.font, text) * size;
        const fitted = el.attrs.textLength === undefined ? natural : a('textLength');
        if (el.attrs.textLength !== undefined && (el.attrs.lengthAdjust !== 'spacingAndGlyphs' || fitted <= 0)) {
          throw new SvgError('The export paints textLength only with lengthAdjust="spacingAndGlyphs".');
        }
        const anchor = style['text-anchor'] ?? 'start';
        if (anchor !== 'start' && anchor !== 'middle' && anchor !== 'end') throw new SvgError(`The export does not paint text-anchor="${anchor}".`);
        const x = a('x') - (anchor === 'end' ? fitted : anchor === 'middle' ? fitted / 2 : 0);
        const y = a('y');
        const squeeze = natural > 0 && fitted !== natural ? `${num((fitted / natural) * 100)} Tz\n` : '';
        ops += `q\n${fill.map(num).join(' ')} rg\nBT\n/${bold ? 'F2' : 'F1'} ${num(size)} Tf\n${squeeze}1 0 0 -1 ${num(x)} ${num(y)} Tm\n<${encode(font, text)}> Tj\nET\nQ\n`;
        if (level) texts.push({ text, classes: node.classes, x1: x, x2: x + fitted, y, size });
        break;
      }
      default:
        throw new SvgError(`The export does not paint the element <${el.tag}>.`);
    }
    if (el.attrs.transform !== undefined) ops += 'Q\n';
  };
  walk(svg, [], {}, true);
  return { width: vw, height: vh, ops, texts, boxes };
}
