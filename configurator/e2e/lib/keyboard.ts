// Walks a page with the Tab key and records each stop.

import type { Page } from '@playwright/test';

export type Stop = {
  /** Tag and id of the element, for a message. */
  what: string;
  /** Index of the element among the controls the page offers to the Tab key, in document order. -1: not among them. */
  index: number;
  /** The id of the nearest ancestor section, header, nav or footer; the tag when it has no id. */
  region: string;
  /** Role and name as the accessibility tree of the browser states them. */
  role: string;
  name: string;
  /** Whether the element matches :focus-visible. */
  focusVisible: boolean;
  /** The outline of the focused element: width in px, and the contrast of its colour, blended over the background behind the element, against that background. Not rounded. */
  outlineWidth: number;
  outlineContrast: number;
  /** Whether a part of the element is inside the viewport and outside the fixed bar at the bottom. */
  inView: boolean;
  /** The tabindex attribute, when the element has one. */
  tabindex: string | null;
};

export type Walk = {
  /** The stops in the order the Tab key reaches them. */
  stops: Stop[];
  /** The count of controls the page offers to the Tab key. */
  expected: number;
  /** A description of each of these controls, in document order. */
  controls: string[];
  /** The stops in the order Shift+Tab reaches them, when asked for. */
  back: number[];
};

type Probe = Omit<Stop, 'role' | 'name'> & { done: boolean };

/** Runs in the page: marks the controls the Tab key reaches, in document order, and moves the start of the walk to the top. Returns their descriptions. */
function mark(): string[] {
  const describe = (el: Element): string => `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ''}`;
  const shown = (el: Element): boolean => {
    const style = getComputedStyle(el);
    if (style.visibility !== 'visible' || el.getClientRects().length === 0) return false;
    // The content of a closed details element takes no focus; its summary does.
    for (let d = el.parentElement?.closest('details'); d; d = d.parentElement?.closest('details') ?? null) {
      if (!(d as HTMLDetailsElement).open && !(el.tagName === 'SUMMARY' && el.parentElement === d)) return false;
    }
    return el.closest('[inert]') === null;
  };
  const all = [...document.querySelectorAll<HTMLElement>('a[href], button, input, select, textarea, summary, [tabindex], [contenteditable]')];
  const controls: HTMLElement[] = [];
  for (const el of all) {
    if (!shown(el)) continue;
    if ((el as HTMLInputElement).disabled) continue;
    if (el.tabIndex < 0) continue;
    if (el instanceof HTMLInputElement && el.type === 'hidden') continue;
    if (el instanceof HTMLInputElement && el.type === 'radio' && el.name !== '') {
      // A radio group is one stop: its checked button, or its first when none is checked.
      const group = all.filter((o): o is HTMLInputElement => o instanceof HTMLInputElement && o.type === 'radio' && o.name === el.name && o.form === el.form && shown(o) && !o.disabled);
      const stop = group.find((o) => o.checked) ?? group[0];
      if (stop !== el) continue;
    }
    controls.push(el);
  }
  controls.forEach((el, i) => el.setAttribute('data-walk', String(i)));
  // The walk starts at the top of the document, wherever the focus is.
  // The body takes the focus for a moment: the next Tab goes to the first control in it.
  const had = document.body.getAttribute('tabindex');
  document.body.setAttribute('tabindex', '-1');
  document.body.focus();
  if (had === null) document.body.removeAttribute('tabindex');
  else document.body.setAttribute('tabindex', had);
  window.scrollTo(0, 0);
  return controls.map(describe);
}

/** Runs in the page: describes the focused element. */
function probe(): Probe {
  const el = document.activeElement;
  const none: Probe = { what: 'body', index: -1, region: '', focusVisible: false, outlineWidth: 0, outlineContrast: 0, inView: false, tabindex: null, done: true };
  if (!el || el === document.body || el === document.documentElement) return none;

  const parse = (text: string): [number, number, number, number] | null => {
    const m = /^rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)$/.exec(text);
    return m ? [Number(m[1]), Number(m[2]), Number(m[3]), m[4] === undefined ? 1 : Number(m[4])] : null;
  };
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
  // The background behind an element: the backgrounds of its ancestors, the nearest on top.
  const behind = (from: Element | null): number[] => {
    const layers: number[][] = [];
    for (let e = from; e; e = e.parentElement) {
      const c = parse(getComputedStyle(e).backgroundColor);
      if (c && c[3] > 0) layers.push(c);
      if (c && c[3] === 1) break;
    }
    let out = [255, 255, 255];
    for (const c of layers.reverse()) out = out.map((v, i) => v * (1 - c[3]) + (c[i] as number) * c[3]);
    return out;
  };

  const style = getComputedStyle(el);
  const rect = el.getBoundingClientRect();
  const bar = [...document.querySelectorAll('*')].find((e) => getComputedStyle(e).position === 'fixed' && e.getClientRects().length > 0 && !e.contains(el));
  const floor = bar ? Math.min(bar.getBoundingClientRect().top, innerHeight) : innerHeight;
  const outline = parse(style.outlineColor);
  // An outline with a negative offset is drawn on the element, another one on what is behind it.
  const ground = Number.parseFloat(style.outlineOffset) < 0 ? behind(el) : behind(el.parentElement);
  const region = el.closest('section, header, nav, footer, main');
  return {
    what: `${el.tagName.toLowerCase()}${el.id ? `#${el.id}` : ''}`,
    index: Number(el.getAttribute('data-walk') ?? -1),
    region: region ? region.id || region.tagName.toLowerCase() : '',
    focusVisible: el.matches(':focus-visible'),
    outlineWidth: style.outlineStyle === 'none' ? 0 : Number.parseFloat(style.outlineWidth),
    // A translucent outline shows the colour behind it through.
    outlineContrast: outline && style.outlineStyle !== 'none' ? ratio(ground.map((v, i) => v * (1 - outline[3]) + (outline[i] as number) * outline[3]), ground) : 0,
    inView: rect.width > 0 && rect.height > 0 && rect.bottom > 0 && rect.top < floor && rect.right > 0 && rect.left < innerWidth,
    tabindex: el.getAttribute('tabindex'),
    done: false,
  };
}

/**
 * Presses Tab from the top of a loaded page until the focus leaves the document
 * or returns to a stop, and records each stop. With `back`, the walk then runs
 * the other way with Shift+Tab. The page is left with the attribute `data-walk`
 * on its controls.
 */
export async function keyboardWalk(page: Page, options: { back?: boolean } = {}): Promise<Walk> {
  const controls = await page.evaluate(mark);
  const session = await page.context().newCDPSession(page);
  const stops: Stop[] = [];
  const seen = new Set<string>();
  try {
    for (let i = 0; i < controls.length + 3; i += 1) {
      await page.keyboard.press('Tab');
      const at = await page.evaluate(probe);
      const key = `${at.index}:${at.what}`;
      if (at.done || seen.has(key)) break;
      seen.add(key);
      const { result } = await session.send('Runtime.evaluate', { expression: 'document.activeElement' });
      const { nodes } = await session.send('Accessibility.getPartialAXTree', { objectId: result.objectId, fetchRelatives: false });
      const node = nodes.find((n) => !n.ignored) ?? nodes[0];
      const { done: _done, ...rest } = at;
      stops.push({ ...rest, role: String(node?.role?.value ?? ''), name: String(node?.name?.value ?? '').trim() });
    }
  } finally {
    await session.detach();
  }
  const back: number[] = [];
  if (options.back && stops.length > 0) {
    // From the last stop back to the first.
    const last = stops[stops.length - 1] as Stop;
    await page.locator(`[data-walk="${last.index}"]`).focus();
    back.push(last.index);
    for (let i = 1; i < stops.length; i += 1) {
      await page.keyboard.press('Shift+Tab');
      back.push((await page.evaluate(probe)).index);
    }
  }
  return { stops, expected: controls.length, controls, back };
}

/**
 * The stops that are outside the viewport or entirely under a fixed bar while
 * they have the focus: criterion 2.4.11 of the Web Content Accessibility
 * Guidelines (WCAG) 2.2, "Focus Not Obscured (Minimum)".
 */
export function hiddenStops(walk: Walk): string[] {
  return walk.stops.filter((s) => !s.inView).map((s) => s.what);
}

/**
 * What is wrong with a walk, one line per finding. An empty list: the walk
 * meets every rule. Whether a stop is in view is the matter of `hiddenStops`.
 */
export function walkProblems(walk: Walk): string[] {
  const out: string[] = [];
  const order = walk.stops.map((s) => s.index);
  const want = walk.controls.map((_, i) => i);
  if (JSON.stringify(order) !== JSON.stringify(want)) {
    const missed = want.filter((i) => !order.includes(i)).map((i) => walk.controls[i]);
    out.push(`the Tab key reaches ${order.length} of ${walk.expected} controls, or in another order than the document: ${JSON.stringify(order)}${missed.length > 0 ? `, not reached: ${missed.join(', ')}` : ''}`);
  }
  const regions: string[] = [];
  for (const stop of walk.stops) {
    if (regions[regions.length - 1] === stop.region) continue;
    if (regions.includes(stop.region)) out.push(`the walk returns to the region "${stop.region}" at ${stop.what}`);
    regions.push(stop.region);
  }
  for (const stop of walk.stops) {
    if (stop.index < 0) out.push(`${stop.what} takes the focus and is no control of the page`);
    if (stop.tabindex !== null && Number(stop.tabindex) > 0) out.push(`${stop.what} has tabindex ${stop.tabindex}`);
    if (stop.name === '') out.push(`${stop.what} has no accessible name`);
    if (stop.role === '' || stop.role === 'generic' || stop.role === 'none') out.push(`${stop.what} has the role "${stop.role}"`);
    if (!stop.focusVisible) out.push(`${stop.what} does not match :focus-visible`);
    if (stop.outlineWidth < 2) out.push(`${stop.what} has a focus outline of ${stop.outlineWidth} px`);
    if (stop.outlineContrast < 3) out.push(`${stop.what} has a focus outline with a contrast of ${stop.outlineContrast.toFixed(3)}:1`);
  }
  if (walk.back.length > 0 && JSON.stringify(walk.back) !== JSON.stringify([...order].reverse())) {
    out.push(`Shift+Tab walks ${JSON.stringify(walk.back)}, the reverse of Tab is ${JSON.stringify([...order].reverse())}`);
  }
  return out;
}
