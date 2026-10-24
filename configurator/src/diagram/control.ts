// Pan, zoom and wire marking of the pictorial wiring view. The only module of
// the folder that touches the document. The listeners sit on the document, so
// they outlive a render; `sync` applies the state to the markup of a render.

import { PAN_STEP, ZOOM_STEP, centreOn, clamp, fit, pan, shows, transformOf, zoomAt } from './view.ts';
import type { Size, ViewT } from './view.ts';

let view: ViewT = fit();
/** The marked wire: stays until it is marked again or leaves the setup. */
let marked: number | null = null;
/** The wire under the pointer or under the focus. */
let hot: number | null = null;
const pointers = new Map<number, { x: number; y: number }>();
/** Distance in CSS px a pointer travels before a press counts as a drag. */
const DRAG = 4;
let travelled = 0;

type Parts = { box: HTMLElement; svg: SVGSVGElement; size: Size; rect: DOMRect; scale: number };

function parts(): Parts | null {
  const box = document.getElementById('dia');
  const svg = box?.querySelector<SVGSVGElement>('svg.picture');
  if (!box || !svg) return null;
  const size = { w: svg.viewBox.baseVal.width, h: svg.viewBox.baseVal.height };
  const rect = svg.getBoundingClientRect();
  // A hidden picture has no size. The view stays as it is.
  if (rect.width === 0 || rect.height === 0) return null;
  const scale = Math.min(rect.width / size.w, rect.height / size.h);
  return { box, svg, size, rect, scale };
}

/** Largest scale: 1px of the picture on 2 CSS px. */
const maxOf = (p: Parts): number => Math.max(2, 2 / p.scale);

/** A point of the window in px of the picture box. */
function pointOf(p: Parts, clientX: number, clientY: number): [number, number] {
  const ox = p.rect.left + (p.rect.width - p.size.w * p.scale) / 2;
  const oy = p.rect.top + (p.rect.height - p.size.h * p.scale) / 2;
  return [(clientX - ox) / p.scale, (clientY - oy) / p.scale];
}

function apply(p: Parts): void {
  view = clamp(view, p.size, maxOf(p));
  // At fit a finger scrolls the page. Zoomed in, every touch moves the picture.
  p.box.classList.toggle('zoomed', view.k > 1);
  p.svg.querySelector('g.vp')?.setAttribute('transform', transformOf(view));
}

function wiresOf(root: ParentNode): SVGGElement[] {
  return [...root.querySelectorAll<SVGGElement>('g.dw')];
}

function paint(): void {
  const svg = document.querySelector<SVGSVGElement>('svg.picture');
  const active = hot ?? marked;
  // A wire the picture does not draw dims no other wire.
  svg?.classList.toggle('has-on', active !== null && svg.querySelector(`g.dw[data-wire="${active}"]`) !== null);
  for (const el of document.querySelectorAll<HTMLElement | SVGElement>('[data-wire]')) {
    const n = Number(el.dataset.wire);
    const target = el instanceof HTMLButtonElement ? el.closest('tr') ?? el : el;
    target.classList.toggle('on', n === active);
    target.classList.toggle('marked', n === marked);
    if (el.hasAttribute('aria-pressed')) el.setAttribute('aria-pressed', String(n === marked));
  }
}

/** The wire the Tab key reaches: the marked wire, else the wire that has the place, else the first. */
function tabStop(svg: ParentNode): void {
  const all = wiresOf(svg);
  const stop = all.find((el) => Number(el.dataset.wire) === marked)
    ?? all.find((el) => el.getAttribute('tabindex') === '0')
    ?? all[0];
  for (const el of all) el.setAttribute('tabindex', el === stop ? '0' : '-1');
}

function mark(n: number): void {
  marked = marked === n ? null : n;
  const svg = document.querySelector<SVGSVGElement>('svg.picture');
  if (svg) tabStop(svg);
  paint();
}

function heat(n: number | null): void {
  if (hot === n) return;
  hot = n;
  paint();
}

function numberOf(target: EventTarget | null): number | null {
  const el = target instanceof Element ? target.closest<HTMLElement | SVGElement>('[data-wire]') : null;
  return el ? Number(el.dataset.wire) : null;
}

function command(id: string): void {
  const p = parts();
  if (!p) return;
  const max = maxOf(p);
  const cx = p.size.w / 2;
  const cy = p.size.h / 2;
  if (id === 'in') view = zoomAt(view, ZOOM_STEP, cx, cy, p.size, max);
  else if (id === 'out') view = zoomAt(view, 1 / ZOOM_STEP, cx, cy, p.size, max);
  else if (id === 'fit') view = fit();
  else if (id === 'left') view = pan(view, p.size.w * PAN_STEP, 0, p.size, max);
  else if (id === 'right') view = pan(view, -p.size.w * PAN_STEP, 0, p.size, max);
  else if (id === 'up') view = pan(view, 0, p.size.h * PAN_STEP, p.size, max);
  else if (id === 'down') view = pan(view, 0, -p.size.h * PAN_STEP, p.size, max);
  apply(p);
}

const KEYS: Readonly<Record<string, string>> = {
  ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down', '+': 'in', '=': 'in', '-': 'out', '0': 'fit',
};

function focusWire(el: SVGGElement, all: SVGGElement[]): void {
  for (const other of all) other.setAttribute('tabindex', other === el ? '0' : '-1');
  el.focus({ preventScroll: true });
}

function onKey(event: KeyboardEvent): void {
  if (event.ctrlKey || event.metaKey || event.altKey) return;
  const target = event.target instanceof Element ? event.target : null;
  const wire = target?.closest<SVGGElement>('g.dw');
  if (wire) {
    const all = wiresOf(wire.ownerSVGElement ?? document);
    const i = all.indexOf(wire);
    const walks = event.key.startsWith('Arrow') || event.key === 'Home' || event.key === 'End';
    const to = event.key === 'ArrowDown' || event.key === 'ArrowRight' ? all[i + 1]
      : event.key === 'ArrowUp' || event.key === 'ArrowLeft' ? all[i - 1]
        : event.key === 'Home' ? all[0]
          : event.key === 'End' ? all[all.length - 1]
            : undefined;
    if (walks) {
      // At the first and the last wire the key stays here: the page does not scroll.
      event.preventDefault();
      if (to) focusWire(to, all);
    } else if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      mark(Number(wire.dataset.wire));
    } else if (event.key === 'Escape') {
      // An Escape that another handler has taken, such as the answer to a removal, moves no focus here.
      if (!event.defaultPrevented) document.getElementById('dia')?.focus({ preventScroll: true });
    } else if (event.key in KEYS && !event.key.startsWith('Arrow')) {
      event.preventDefault();
      command(KEYS[event.key]!);
    }
    return;
  }
  if (target?.id !== 'dia') return;
  const id = KEYS[event.key];
  if (!id) return;
  event.preventDefault();
  command(id);
}

function onFocus(event: FocusEvent): void {
  const n = numberOf(event.target);
  heat(n);
  const wire = event.target instanceof Element ? event.target.closest<SVGGElement>('g.dw') : null;
  const p = wire ? parts() : null;
  if (!wire || !p) return;
  const [cx, cy] = (wire.dataset.c ?? '0 0').split(' ').map(Number) as [number, number];
  if (!shows(view, cx, cy, p.size)) {
    view = centreOn(view, cx, cy, p.size, maxOf(p));
    apply(p);
  }
}

function onPointerDown(event: PointerEvent): void {
  const box = event.target instanceof Element ? event.target.closest<HTMLElement>('#dia') : null;
  if (!box || (event.pointerType === 'mouse' && event.button !== 0)) return;
  if (pointers.size === 0) travelled = 0;
  pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
}

function onPointerMove(event: PointerEvent): void {
  const last = pointers.get(event.pointerId);
  if (!last) return;
  const p = parts();
  if (!p) return;
  const max = maxOf(p);
  const dx = event.clientX - last.x;
  const dy = event.clientY - last.y;
  travelled += Math.abs(dx) + Math.abs(dy);
  // The box holds the pointer from the start of a drag. A press without travel
  // stays a click on the wire under it.
  if (travelled >= DRAG && !p.box.hasPointerCapture?.(event.pointerId)) {
    try {
      p.box.setPointerCapture?.(event.pointerId);
    } catch {
      // The pointer has ended. The drag ends with it.
    }
  }
  if (pointers.size === 1) {
    if (travelled >= DRAG) view = pan(view, dx / p.scale, dy / p.scale, p.size, max);
  } else {
    // 2 pointers: the picture follows their middle and scales with their distance.
    const other = [...pointers.entries()].find(([id]) => id !== event.pointerId)![1];
    const before = Math.hypot(last.x - other.x, last.y - other.y);
    const after = Math.hypot(event.clientX - other.x, event.clientY - other.y);
    const [mx, my] = pointOf(p, (event.clientX + other.x) / 2, (event.clientY + other.y) / 2);
    view = pan(view, dx / 2 / p.scale, dy / 2 / p.scale, p.size, max);
    if (before > 0 && after > 0) view = zoomAt(view, after / before, mx, my, p.size, max);
  }
  pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
  apply(p);
}

function onPointerUp(event: PointerEvent): void {
  pointers.delete(event.pointerId);
}

function onWheel(event: WheelEvent): void {
  const box = event.target instanceof Element ? event.target.closest('#dia') : null;
  if (!box || !event.ctrlKey) return;
  const p = parts();
  if (!p) return;
  event.preventDefault();
  const [x, y] = pointOf(p, event.clientX, event.clientY);
  view = zoomAt(view, event.deltaY < 0 ? 1.2 : 1 / 1.2, x, y, p.size, maxOf(p));
  apply(p);
}

function onClick(event: MouseEvent): void {
  const target = event.target instanceof Element ? event.target : null;
  const tool = target?.closest<HTMLElement>('button[data-dia]');
  if (tool) {
    command(tool.dataset.dia!);
    return;
  }
  const n = numberOf(target);
  if (n === null) return;
  // A drag that ends on a wire is a pan, not a mark.
  if (target?.closest('#dia') && travelled >= DRAG) return;
  mark(n);
}

/** Binds the listeners. Call once. */
export function install(): void {
  document.addEventListener('click', onClick);
  document.addEventListener('keydown', onKey);
  document.addEventListener('focusin', onFocus);
  document.addEventListener('focusout', () => heat(null));
  document.addEventListener('pointerover', (event) => {
    if (event.pointerType === 'mouse') heat(numberOf(event.target));
  });
  document.addEventListener('pointerdown', onPointerDown);
  document.addEventListener('pointermove', onPointerMove);
  document.addEventListener('pointerup', onPointerUp);
  document.addEventListener('pointercancel', onPointerUp);
  document.addEventListener('wheel', onWheel, { passive: false });
  window.addEventListener('resize', () => {
    const p = parts();
    if (p) apply(p);
  });
}

/** Applies the view and the marked wire to the markup of a render. A marked wire that left the setup is dropped. */
export function sync(): void {
  pointers.clear();
  hot = null;
  if (marked !== null && !document.querySelector(`[data-wire="${marked}"]`)) marked = null;
  const p = parts();
  if (p) {
    apply(p);
    tabStop(p.svg);
  }
  paint();
}
