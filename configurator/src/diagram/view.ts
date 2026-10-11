// Pan and zoom of the pictorial wiring view. Pure.
//
// The picture is w by h px. The box shows exactly that area. A view places the
// picture in it: scaled by k, then moved by (x, y). k = 1 with x = y = 0 shows
// the whole picture.

export type ViewT = { k: number; x: number; y: number };
export type Size = { w: number; h: number };

/** Factor of 1 zoom step. */
export const ZOOM_STEP = 1.5;
/** Share of the box that 1 pan step moves. */
export const PAN_STEP = 0.2;

export const fit = (): ViewT => ({ k: 1, x: 0, y: 0 });

/** Keeps the scale between 1 and `max`, and the picture over the whole box. */
export function clamp(v: ViewT, size: Size, max: number): ViewT {
  const k = Math.min(Math.max(1, max), Math.max(1, Number.isFinite(v.k) ? v.k : 1));
  const within = (value: number, extent: number): number => {
    const low = extent - extent * k;
    return Math.min(0, Math.max(low, Number.isFinite(value) ? value : 0));
  };
  return { k, x: within(v.x, size.w), y: within(v.y, size.h) };
}

/** Zoom by `factor` so that the point (px, py) of the box stays in place. */
export function zoomAt(v: ViewT, factor: number, px: number, py: number, size: Size, max: number): ViewT {
  const k = Math.min(Math.max(1, max), Math.max(1, v.k * factor));
  const ratio = k / v.k;
  return clamp({ k, x: px - (px - v.x) * ratio, y: py - (py - v.y) * ratio }, size, max);
}

export function pan(v: ViewT, dx: number, dy: number, size: Size, max: number): ViewT {
  return clamp({ k: v.k, x: v.x + dx, y: v.y + dy }, size, max);
}

/** Moves the point (cx, cy) of the picture to the middle of the box, as far as the edges allow. */
export function centreOn(v: ViewT, cx: number, cy: number, size: Size, max: number): ViewT {
  return clamp({ k: v.k, x: size.w / 2 - cx * v.k, y: size.h / 2 - cy * v.k }, size, max);
}

/** True when the point (cx, cy) of the picture lies inside the box. */
export function shows(v: ViewT, cx: number, cy: number, size: Size): boolean {
  const x = cx * v.k + v.x;
  const y = cy * v.k + v.y;
  return x >= 0 && x <= size.w && y >= 0 && y <= size.h;
}

export const transformOf = (v: ViewT): string =>
  `translate(${Math.round(v.x * 100) / 100} ${Math.round(v.y * 100) / 100}) scale(${Math.round(v.k * 1000) / 1000})`;
