import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { CATALOGUE } from '../../core/catalogue.ts';
import { ANCHORS_FILE, PANEL_FILE, checkPanel } from '../cli.ts';
import { LEAD, PANEL_RAILS, PANEL_SHAPE, SCALE, across, along, padXY, panelDrawing, panelGroups, panelText } from '../panel.ts';
import type { PanelShape } from '../panel.ts';
import { RP } from '../parts.ts';

const OUT_BIND = readFileSync(new URL('../../../shared/outputs/out_bind.c', import.meta.url), 'utf8');
const drawing = panelDrawing(CATALOGUE.pads);
/** Shapes and pads of the payload: the limits of shared/ui/include/board_drawing.h. */
const MAX_SHAPES = 400, MAX_PADS = 64;

type Box = { x0: number; y0: number; x1: number; y1: number };
function box(s: PanelShape): Box {
  if (s.kind === 'disc') return { x0: s.a - s.c, y0: s.b - s.c, x1: s.a + s.c, y1: s.b + s.c };
  if (s.kind === 'line') {
    const h = s.param / 2;
    return { x0: Math.min(s.a, s.c) - h, y0: Math.min(s.b, s.d) - h, x1: Math.max(s.a, s.c) + h, y1: Math.max(s.b, s.d) + h };
  }
  return { x0: s.a, y0: s.b, x1: s.a + s.c, y1: s.b + s.d };
}
const overlap = (a: Box, b: Box): boolean => a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
const union = (bs: Box[]): Box => ({
  x0: Math.min(...bs.map((b) => b.x0)), y0: Math.min(...bs.map((b) => b.y0)),
  x1: Math.max(...bs.map((b) => b.x1)), y1: Math.max(...bs.map((b) => b.y1)),
});

test('the committed panel.json is the output of the generator', () => {
  assert.equal(readFileSync(PANEL_FILE, 'utf8'), panelText(drawing));
  assert.deepEqual(checkPanel(panelText(drawing)), []);
});

test('a drifted or missing panel.json fails the check', () => {
  const dir = mkdtempSync(join(tmpdir(), 'panel-'));
  try {
    const file = join(dir, 'panel.json');
    assert.match(checkPanel('x', file)[0]!.msg, /^panel\.json is missing/);
    writeFileSync(file, `${panelText(drawing)} `);
    assert.equal(checkPanel(panelText(drawing), file)[0]!.id, 'drift');
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the outline and the pad grid are the ones of k_pico_shape in out_bind.c', () => {
  const m = /k_pico_shape = \{\s*(\d+)u, (\d+)u, (\d+)u, (\d+)u, (\d+)u,/.exec(OUT_BIND);
  assert.ok(m, 'k_pico_shape not found');
  const [, w, h, pitch, inset, per] = m.map(Number);
  assert.deepEqual([w, h, pitch, inset, per], [PANEL_SHAPE.width, PANEL_SHAPE.height, PANEL_SHAPE.pitch, PANEL_SHAPE.inset, PANEL_SHAPE.perSide]);
  assert.equal(drawing.width, w);
  assert.equal(drawing.height, h);
  // outbind_pad_xy(): pad 1 at the lead from the left end, the bottom row inset from the bottom edge.
  assert.equal(LEAD, 137);
  assert.deepEqual(padXY(1), { x: 137, y: 1939 });
  assert.deepEqual(padXY(20), { x: 137 + 19 * 254, y: 1939 });
  assert.deepEqual(padXY(21), { x: 137 + 19 * 254, y: 161 });
  assert.deepEqual(padXY(40), { x: 137, y: 161 });
});

test('the pad kinds and rail voltages are the ones of k_pico_pads and k_pico_pins in out_bind.c', () => {
  const kinds: Record<string, string> = { LINK_PAD_GROUND: 'ground', LINK_PAD_POWER: 'rail', LINK_PAD_OTHER: 'other' };
  const rows = [...OUT_BIND.matchAll(/\{\s*(\d+), (LINK_PAD_\w+),\s*(\d+)\s*\}/g)].map((r) => [Number(r[1]), kinds[r[2]!], Number(r[3])]);
  assert.equal(rows.length, 14);
  const pins = [...OUT_BIND.matchAll(/\{\s*(\d+),\s*(\d+), (?:true|false),/g)].map((r) => [Number(r[2]), Number(r[1])]);
  assert.equal(pins.length, 26);
  assert.equal(drawing.pads.length, 40);
  for (const [pad, kind, value] of rows) {
    const p = drawing.pads.find((q) => q.pad === pad)!;
    assert.deepEqual([p.kind, p.value], [kind, value], `pad ${pad}`);
  }
  for (const [pad, gpio] of pins) {
    const p = drawing.pads.find((q) => q.pad === pad)!;
    assert.deepEqual([p.kind, p.value], ['gpio', gpio], `pad ${pad}`);
  }
  assert.ok(Object.keys(PANEL_RAILS).every((name) => CATALOGUE.pads.some((p) => p.name === name)));
});

test('every ring of copro.svg lands on its pad of the grid to 0.01 mm', () => {
  const anchors = JSON.parse(readFileSync(ANCHORS_FILE, 'utf8')) as { files: Record<string, { pins: Record<string, { x: number; y: number }> }> };
  const pins = anchors.files['copro.svg']!.pins;
  for (let n = 1; n <= 40; n++) {
    const a = pins[`pad.${n}`]!;
    // copro.svg draws the board at (30, 40), scale 1.
    const x = Math.round(along(a.y - 40)), y = Math.round(across(a.x - 30));
    assert.deepEqual({ x, y }, padXY(n), `pad ${n}`);
    const p = drawing.pads[n - 1]!;
    assert.deepEqual({ pad: p.pad, x: p.x, y: p.y }, { pad: n, ...padXY(n) });
  }
  assert.equal(drawing.pads.filter((p) => p.square).map((p) => p.pad).join(), '1');
});

test('the shapes stay within the limits, inside the outline and are flat', () => {
  assert.ok(drawing.shapes.length > 0 && drawing.shapes.length <= MAX_SHAPES, `${drawing.shapes.length} shapes`);
  assert.ok(drawing.pads.length <= MAX_PADS);
  const outline: Box = { x0: 0, y0: 0, x1: drawing.width, y1: drawing.height };
  for (const s of drawing.shapes) {
    assert.match(s.colour, /^#[0-9a-f]{6}$/);
    assert.ok(['rect', 'rrect', 'disc', 'line'].includes(s.kind));
    for (const v of [s.param, s.a, s.b, s.c, s.d]) assert.ok(Number.isInteger(v) && v >= 0 && v <= 0xffff, JSON.stringify(s));
    const b = box(s);
    assert.ok(b.x0 >= outline.x0 && b.y0 >= outline.y0 && b.x1 <= outline.x1 && b.y1 <= outline.y1, `outside the outline: ${JSON.stringify(s)}`);
  }
  for (const p of drawing.pads) {
    assert.ok(p.x - drawing.ringRadius >= 0 && p.x + drawing.ringRadius <= drawing.width);
    assert.ok(p.y - drawing.ringRadius >= 0 && p.y + drawing.ringRadius <= drawing.height);
  }
  assert.ok(drawing.holeRadius < drawing.ringRadius);
  // The rings keep the scale of the SVG: radius 9 local units.
  assert.equal(drawing.ringRadius, Math.round(9 * SCALE));
});

test('no part covers a ring, and the end parts overlap no other part', () => {
  const groups = panelGroups();
  const rings = drawing.pads.map((p): Box => ({ x0: p.x - drawing.ringRadius, y0: p.y - drawing.ringRadius, x1: p.x + drawing.ringRadius, y1: p.y + drawing.ringRadius }));
  for (const g of groups) {
    for (const s of g.shapes) for (const r of rings) assert.ok(!overlap(box(s), r), `${g.part} covers a ring: ${JSON.stringify(s)}`);
  }
  const parts = groups.filter((g) => g.part !== 'traces');
  for (const e of parts.filter((g) => g.end)) {
    const eb = union(e.shapes.map(box));
    for (const o of parts) {
      if (o === e) continue;
      assert.ok(!overlap(eb, union(o.shapes.map(box))), `${e.part} overlaps ${o.part}`);
    }
  }
  assert.deepEqual(parts.filter((g) => g.end).map((g) => g.part), ['usbc', 'button', 'button', 'terminal']);
});

test('the USB-C socket sits on the end of pad 1, the CAN terminal on the end of pad 20, both between the pad rows', () => {
  const groups = panelGroups();
  const usb = union(groups.find((g) => g.part === 'usbc')!.shapes.map(box));
  const term = union(groups.find((g) => g.part === 'terminal')!.shapes.map(box));
  assert.equal(usb.x0, 0);
  assert.ok(usb.x1 < padXY(2).x);
  assert.equal(term.x1, drawing.width);
  assert.ok(term.x0 > padXY(18).x);
  for (const b of [usb, term]) {
    assert.ok(b.y0 > padXY(21).y + drawing.ringRadius);
    assert.ok(b.y1 < padXY(20).y - drawing.ringRadius);
  }
});

test('an interior part keeps its place between the same pads as in copro.svg', () => {
  const { y0, pitch } = RP.pads;
  for (const y of [70, 100, 128, 232, 386, 452, 540, 640]) {
    // The fraction of the pitch past pad 1 is the same in both drawings.
    assert.ok(Math.abs((along(y) - LEAD) / PANEL_SHAPE.pitch - (y - y0) / pitch) < 1e-9, `y ${y}`);
  }
  const interior = panelGroups().filter((g) => !g.end && g.part !== 'traces');
  const xs = interior.map((g) => union(g.shapes.map(box))).map((b) => (b.x0 + b.x1) / 2);
  assert.ok(xs.every((x) => x > padXY(1).x && x < padXY(20).x));
});

test('the text form puts one pad or shape on a line and parses back', () => {
  const text = panelText(drawing);
  assert.deepEqual(JSON.parse(text), drawing);
  assert.equal(text.split('\n').length, 1 + 9 + 1 + drawing.pads.length + 1 + 1 + drawing.shapes.length + 1 + 1 + 1);
  assert.equal(text.match(/"text"/g), null);
});
