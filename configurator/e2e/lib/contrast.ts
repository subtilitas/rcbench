// Measures the contrast of what a page paints, by the formula of the Web
// Content Accessibility Guidelines (WCAG) 2.2: text against its background
// (criterion 1.4.3) and wires and control borders against theirs (1.4.11).

import type { Page } from '@playwright/test';

export type Measured = {
  /** "text", "svg-text", "wire", "outline" or "border". */
  kind: string;
  /** Where: tag, class and the first 40 characters of the text. */
  what: string;
  /** Foreground and background as r,g,b. */
  fg: string;
  bg: string;
  /** The contrast ratio, not rounded. */
  ratio: number;
  /** The ratio the pair has to reach. */
  need: number;
};

export type Contrast = {
  /** One line per distinct pair of colours and kind, with the lowest ratio of the pair. */
  pairs: Measured[];
  /** Colours the measurement cannot read: a gradient, an image, another colour notation. */
  unknown: string[];
  /** The count of text nodes, wires and borders measured. */
  measured: number;
};

/** Runs in the page. */
function measure(): Contrast {
  type Rgba = [number, number, number, number];
  const unknown = new Set<string>();
  const pairs = new Map<string, Measured>();
  let measured = 0;

  const parse = (text: string, where: string): Rgba | null => {
    const m = /^rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)$/.exec(text);
    if (m) return [Number(m[1]), Number(m[2]), Number(m[3]), m[4] === undefined ? 1 : Number(m[4])];
    unknown.add(`${where}: ${text}`);
    return null;
  };
  const over = (top: Rgba, base: number[]): number[] => base.map((v, i) => v * (1 - top[3]) + (top[i] as number) * top[3]);
  const luminance = (c: number[]): number => {
    const [r, g, b] = c.slice(0, 3).map((v) => {
      const s = v / 255;
      return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
    }) as [number, number, number];
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  };
  const ratio = (a: number[], b: number[]): number => {
    const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x) as [number, number];
    return (hi + 0.05) / (lo + 0.05);
  };
  const label = (el: Element, text = ''): string => {
    const cls = el.getAttribute('class');
    return `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ''}${cls ? `.${cls.trim().replace(/\s+/g, '.')}` : ''}${text ? ` "${text.trim().slice(0, 40)}"` : ''}`;
  };
  const shown = (el: Element): boolean => {
    if (el.getClientRects().length === 0) return false;
    const style = getComputedStyle(el);
    return style.visibility === 'visible' && Number(style.opacity) > 0;
  };
  /** The colour behind an element: the backgrounds of the element and its ancestors, the nearest on top. */
  const behind = (from: Element | null): number[] => {
    const layers: Rgba[] = [];
    for (let e = from; e; e = e.parentElement) {
      const style = getComputedStyle(e);
      if (style.backgroundImage !== 'none') unknown.add(`${label(e)}: background-image ${style.backgroundImage.slice(0, 40)}`);
      const c = parse(style.backgroundColor, label(e));
      if (c && c[3] > 0) layers.push(c);
      if (c && c[3] === 1) break;
    }
    let out = [255, 255, 255];
    for (const c of layers.reverse()) out = over(c, out);
    return out;
  };
  const note = (kind: string, what: string, fg: number[], bg: number[], need: number): void => {
    measured += 1;
    const show = (c: number[]): string => c.slice(0, 3).map((v) => Math.round(v)).join(',');
    const key = `${kind} ${show(fg)} ${show(bg)} ${need}`;
    // Colours that read the same as whole numbers share a line: the line of the lowest ratio among them.
    const found = ratio(fg, bg);
    const held = pairs.get(key);
    if (!held || found < held.ratio) pairs.set(key, { kind, what, fg: show(fg), bg: show(bg), ratio: found, need });
  };

  // Text of the document outside the drawings.
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    const el = node.parentElement;
    const text = node.textContent ?? '';
    if (!el || text.trim() === '' || el.closest('svg, script, style, [hidden]') || !shown(el)) continue;
    const range = document.createRange();
    range.selectNodeContents(node);
    if (range.getClientRects().length === 0) continue;
    const style = getComputedStyle(el);
    const colour = parse(style.color, label(el));
    if (!colour) continue;
    const bg = behind(el);
    let alpha = 1;
    for (let e: Element | null = el; e; e = e.parentElement) alpha *= Number(getComputedStyle(e).opacity);
    const size = Number.parseFloat(style.fontSize);
    const large = size >= 24 || (size >= 18.66 && Number(style.fontWeight) >= 700);
    note('text', label(el, text), over([colour[0], colour[1], colour[2], colour[3] * alpha], bg), bg, large ? 3 : 4.5);
  }

  // Text of the drawings: the fill of the text against the filled shape under its centre.
  for (const text of document.querySelectorAll<SVGTextElement>('svg text')) {
    if (!shown(text) || (text.textContent ?? '').trim() === '') continue;
    const fill = parse(getComputedStyle(text).fill, label(text));
    if (!fill) continue;
    text.scrollIntoView({ block: 'center', inline: 'center', behavior: 'instant' });
    const box = text.getBoundingClientRect();
    const stack = document.elementsFromPoint(box.left + box.width / 2, box.top + box.height / 2);
    let bg: number[] | null = null;
    for (const under of stack) {
      if (under === text || under instanceof SVGTextElement || under instanceof SVGTSpanElement) continue;
      if (under instanceof SVGGeometryElement) {
        const paint = getComputedStyle(under).fill;
        if (paint === 'none') continue;
        const c = parse(paint, label(under));
        if (!c || c[3] === 0) continue;
        bg = over(c, behind(under.closest('svg')));
        break;
      }
      bg = behind(under);
      break;
    }
    if (!bg) bg = behind(text.closest('svg'));
    const size = Number.parseFloat(getComputedStyle(text).fontSize);
    const large = size >= 24 || (size >= 18.66 && Number(getComputedStyle(text).fontWeight) >= 700);
    note('svg-text', label(text, text.textContent ?? ''), over(fill, bg), bg, large ? 3 : 4.5);
  }

  // Wires of the drawings and of the legend, against the background of the drawing.
  for (const wire of document.querySelectorAll<SVGPathElement>('svg path.w')) {
    if (!shown(wire)) continue;
    const stroke = parse(getComputedStyle(wire).stroke, label(wire));
    if (!stroke) continue;
    const bg = behind(wire.closest('svg'));
    note('wire', label(wire), over(stroke, bg), bg, 3);
  }

  // Wires of the wiring picture, of its legend and of the colour samples of the table. A wire is a core
  // with, for some colours, a casing: the colour of the 2 that stands out more is the one that counts.
  const best = (colours: Array<Rgba | null>, bg: number[]): number[] | null => {
    let out: number[] | null = null;
    for (const c of colours) {
      if (!c || c[3] === 0) continue;
      const fg = over(c, bg);
      if (!out || ratio(fg, bg) > ratio(out, bg)) out = fg;
    }
    return out;
  };
  for (const wire of document.querySelectorAll<SVGElement>('svg.picture g.dw, #dia-legend svg.lg')) {
    if (!shown(wire)) continue;
    const bg = behind(wire.closest('svg'));
    const strokes = [...wire.querySelectorAll<SVGPathElement>('path:not(.hit)')].map((p) => parse(getComputedStyle(p).stroke, label(p)));
    const fg = best(strokes, bg);
    if (fg) note('wire', label(wire), fg, bg, 3);
  }
  for (const sample of document.querySelectorAll<SVGRectElement>('svg.sw rect')) {
    if (!shown(sample)) continue;
    const style = getComputedStyle(sample);
    const bg = behind(sample.closest('svg'));
    const fg = best([parse(style.fill, label(sample)), style.stroke === 'none' ? null : parse(style.stroke, label(sample))], bg);
    if (fg) note('wire', label(sample), fg, bg, 3);
  }

  // Outlines of the boxes and the wire numbers of the drawings.
  for (const shape of document.querySelectorAll<SVGGeometryElement>('svg rect, svg circle')) {
    if (!shown(shape)) continue;
    // A colour sample is measured above as a wire. The number of a wire of the picture stands on a plate
    // with a border in the colour of the wire: the number is measured as text, the wire as a wire.
    if (shape.closest('svg.sw') || shape.matches('svg.picture rect.nb')) continue;
    const style = getComputedStyle(shape);
    if (style.stroke === 'none' || Number.parseFloat(style.strokeWidth) === 0) continue;
    const stroke = parse(style.stroke, label(shape));
    if (!stroke) continue;
    const bg = behind(shape.closest('svg'));
    note('outline', label(shape), over(stroke, bg), bg, 3);
  }

  // Borders that alone mark a control.
  for (const el of document.querySelectorAll<HTMLElement>('button, input[type=text], select, textarea')) {
    if (!shown(el)) continue;
    const style = getComputedStyle(el);
    if (Number.parseFloat(style.borderTopWidth) === 0 || style.borderTopStyle === 'none') continue;
    const border = parse(style.borderTopColor, label(el));
    if (!border) continue;
    const outside = behind(el.parentElement);
    const inside = behind(el);
    // A control whose fill stands out from the page needs no border to be seen.
    if (ratio(inside, outside) >= 3) continue;
    note('border', label(el), over(border, outside), outside, 3);
  }

  window.scrollTo(0, 0);
  return { pairs: [...pairs.values()], unknown: [...unknown], measured };
}

/** Measures the page as it is shown: theme, language and open sections are the ones of the moment. */
export async function contrastOf(page: Page): Promise<Contrast> {
  return page.evaluate(measure);
}

/** The pairs below their ratio, one line each. */
export function contrastProblems(contrast: Contrast): string[] {
  return [
    ...contrast.pairs.filter((p) => p.ratio < p.need).map((p) => `${p.kind} ${p.what}: ${p.fg} on ${p.bg} is ${p.ratio.toFixed(3)}:1, the minimum is ${p.need}:1`),
    ...contrast.unknown.map((u) => `not measured: ${u}`),
  ];
}
