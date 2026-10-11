// The input of the pictorial wiring view: the parts of a setup with their
// drawings, and each wire with the anchor points of its ends. Pure.

import type { Setup, Text, Wire } from '../../contract.ts';
import type { Anchors, DrawingRecord } from '../../drawings/generate.ts';
import type { Side } from '../../drawings/lib.ts';
import { colourOf, endPoints } from '../../drawings/wires.ts';

/** Column of the picture: left of the coprocessor, its own column, right of it. */
export type Lane = 'L' | 'C' | 'R';

export type ScenePart = { id: string; file: string; w: number; h: number; column: Lane; order: number; name: Text };

/** A wire point in px of the drawing of its part. */
export type ScenePin = { part: string; pin: string; x: number; y: number; side: Side };

/**
 * `a` and `b` list the wire points of an end. An end has more than 1 point when the
 * board offers several pads for it. `b` is null for a wire that ends open.
 */
export type SceneWire = { n: number; kind: Wire['kind']; colour: string | null; thick: boolean; a: ScenePin[]; b: ScenePin[] | null };

/**
 * True for a wire that carries load current: the supply of the ESC or of the
 * servo, plus and minus. The data file marks these wires with the class
 * `power`. The supply of a board from 3.3 V or VBUS has the class `logic`.
 */
export const carriesLoad = (wire: Pick<Wire, 'cls' | 'kind'>): boolean => wire.kind !== 'component' && wire.cls === 'power';

/** `undrawn`: numbers of the wires with an end that has no wire point on a drawing. */
export type Scene = { parts: ScenePart[]; wires: SceneWire[]; undrawn: number[] };

/** The drawing of a part for the option values of the setup: the board the options name, else the board without a variant. */
export function drawingOf(part: string, values: ReadonlyArray<string>, anchors: Anchors): [string, DrawingRecord] | null {
  const own = Object.entries(anchors.files).filter(([, rec]) => rec.part === part);
  return own.find(([, rec]) => rec.variant !== null && values.includes(rec.variant))
    ?? own.find(([, rec]) => rec.variant === null)
    ?? own[0]
    ?? null;
}

export function sceneOf(setup: Pick<Setup, 'parts' | 'options' | 'wires'>, anchors: Anchors): Scene {
  const values = setup.options.map((o) => o.value);
  const files = new Map<string, [string, DrawingRecord]>();
  const parts: ScenePart[] = [];
  for (const { part } of setup.parts) {
    const drawing = drawingOf(part.id, values, anchors);
    if (!drawing) continue;
    files.set(part.id, drawing);
    const [file, rec] = drawing;
    const column: Lane = part.slot.column === 'L' ? 'L' : part.slot.column === 'R' ? 'R' : 'C';
    parts.push({ id: part.id, file, w: rec.viewBox[2], h: rec.viewBox[3], column, order: part.slot.order, name: part.short });
  }
  parts.sort((p, q) => p.order - q.order || (p.id < q.id ? -1 : 1));

  const pins = (end: string): ScenePin[] => {
    const out: ScenePin[] = [];
    for (const point of endPoints(end, anchors)) {
      const rec = anchors.files[point.file]!;
      const drawing = rec.part === null ? undefined : files.get(rec.part);
      if (!drawing || drawing[0] !== point.file) continue;
      const pin = rec.pins[point.pin]!;
      out.push({ part: rec.part!, pin: point.pin, x: pin.x, y: pin.y, side: pin.side });
    }
    return out;
  };

  const wires: SceneWire[] = [];
  const undrawn: number[] = [];
  for (const wire of [...setup.wires].sort((p, q) => p.n - q.n)) {
    const a = pins(wire.a);
    const b = wire.b === null ? null : pins(wire.b);
    if (a.length === 0 || (b !== null && b.length === 0)) {
      undrawn.push(wire.n);
      continue;
    }
    wires.push({ n: wire.n, kind: wire.kind, colour: colourOf(wire), thick: carriesLoad(wire), a, b });
  }
  return { parts, wires, undrawn };
}
