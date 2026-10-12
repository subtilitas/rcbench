# Part drawings

Generator and check of the part drawings of the bench configurator.

- 23 drawings: 21 of the 19 parts of the data file (`core/catalogue.ts`), 2 of
  boards outside it. The coprocessor and the INA228 have 2 boards each.
- Each drawing is 1 SVG (Scalable Vector Graphics) file per language: English
  in `public/drawings/<file>.svg`, German in `public/drawings/de/<file>.svg`.
- `anchors.json` holds the wire point of each of the 177 pins in px of the
  viewBox of its drawing.
- The generator reads the data file and nothing else: no file, no clock, no
  random number. The same data file gives the same bytes.
- A file is self-contained: no script, no raster image, no style sheet, no
  reference that leaves the file. Every id carries the prefix of its drawing.

## Commands

Node.js 24 is required. Run in `configurator/`.

```bash
npm run drawings          # writes public/drawings/** and drawings/anchors.json
npm run drawings:check    # writes nothing; exit code 1 on a finding or on drift
node drawings/cli.ts --palette   # prints the contrast table of the wire palette
npm test                  # runs drawings/test/*.test.ts with the tests of the page
```

`npm run build` runs the check before Vite. `.github/workflows/pages.yml` runs
it as a step of the job `build`.

## Committed output, guarded by the check

The SVG files and `anchors.json` are committed. They are not written during
`npm run build`, `npm run dev` or `npm test`.

- A change of a label or of a shape shows as a diff of the drawing in the pull
  request.
- `--check` builds every file in memory and compares it byte by byte with the
  committed file. A changed, missing or extra file is a finding.
- After a change of the data file or of a draw function: `npm run drawings`,
  then commit the output.

## Files

| File | Content |
|---|---|
| `lib.ts` | Drawing primitives and the class `Dwg`: shapes, pin labels, anchors, SVG text. |
| `parts.ts` | 1 draw function per drawing, and the list of drawings. |
| `generate.ts` | `generate(catalogue)`: every SVG text and the anchor table. |
| `wires.ts` | Wire palette, colour rules, contrast formula, anchor points of a wire end. |
| `check.ts` | The checks as pure functions that return findings. |
| `cli.ts` | Command line: write, `--check`, `--palette`. |
| `anchors.json` | The anchor table. Written by the generator. |
| `test/` | Tests, run by `node --test`. |

## Anchor table

```json
{
 "release": "0.17.0",
 "files": {
  "estop.svg": {
   "part": "estop", "variant": null, "viewBox": [0, 0, 340, 200], "names": "catalogue",
   "pins": { "NC1": { "x": 283, "y": 72, "side": "E", "labelId": "est-lbl-NC1", "label": { "en": "…", "de": "…" } } }
  }
 }
}
```

| Field | Meaning |
|---|---|
| `part` | Part id of the data file. `null`: the board is not in the data file and no wire ends on it. |
| `variant` | Option value the drawing shows, for a part with 2 boards. `null` otherwise. |
| `viewBox` | Size of the drawing in px. |
| `names` | `catalogue`: pin ids and labels are those of the data file. `board print`: they are the print of the board. |
| `pins.<id>.x`, `.y` | Wire point in px of the viewBox, 1 decimal. |
| `pins.<id>.side` | Edge a wire leaves the pin on: `N`, `E`, `S` or `W`. |
| `pins.<id>.labelId` | Id of the `<text>` element of the label in both SVG files. |
| `catalogueMap` | For `board print`: pin id of the data file to pin ids of the drawing. `null`: the board has no wire point for the pin. |

The coprocessor carries 42 pins: `pad.1` to `pad.40` and `can.H`, `can.L`.
`copro.svg` labels a pad with its `name`, `copro--osoyoo.svg` with its
`breakoutLabel`.

A pin with `only` is on the drawing of the board it names and on no other.

The T-plug drawing has 2 USB-C sockets. A pin `IN` or `OUT` of the data file
gets its wire point at the mouth of that socket: `IN` at (28, 80) on side `W`,
`OUT` at (232, 80) on side `E`. A socket that the data file does not list as a
pin carries its print and no anchor.

## What the check fails on

| Finding | Condition |
|---|---|
| `pin-without-anchor` | A pin of the data file has no anchor in a drawing of its part. |
| `anchor-without-pin` | An anchor names no pin of the data file. A drawing with `part: null` is skipped. |
| `label` | A label differs from the label of the pin in the data file, in English or German. |
| `map` | A `catalogueMap` names a pin the data file does not have; a drawing with `names: "catalogue"` carries a `catalogueMap`; a drawing with `names: "board print"` and a part carries none; 2 pins of the data file map to 1 pin of the drawing, or 1 names it twice. |
| `no-drawing` | A part, or 1 of the 2 boards of a part, has no drawing; or a drawing shows a board that no option selects. |
| `release` | `anchors.json` is of another release than the data file. |
| `wire-end` | An end of a wire has no anchor. The end of a stub whose pin maps to `null` is skipped. |
| `wire-colour` | A wire has no colour of the palette. |
| `contrast` | A colour of the palette is below 3:1 on 1 of the 5 backgrounds. |
| `neighbour` | 2 wires that end on neighbouring pins of 1 drawing share a colour. 2 plus wires and 2 minus wires are exempt. |
| `anchor` | An anchor lies outside its viewBox, has no side, or its label text is missing, empty, doubled or different in the SVG file. |
| `svg` | A file is missing, not well formed, not self-contained, has no `svg` root in the SVG namespace, uses a namespace prefix other than `xml`, carries an element or an attribute outside the 14 elements and 43 attributes the generator writes, or lacks title, description, viewBox, language or the id prefix. The root element carries 10 attributes and no other: `width` and `height` are the px of the viewBox, `role` is `img`, `font-family` is the whole stack of the generator. No other element states a font, a language, a role or a viewBox. An id is a prefix of lower-case letters and digits and 1 or more parts of letters and digits, joined by hyphens. A reference is written `url(#id)`. Title and description are the first 2 children of the root. |
| `drift` | A committed file differs from the output of the generator, is missing, or is not a file of the generator. A file counts with the extension `.svg` in any case. |

The generator stops with an error, and the check with it, when:

- a pin of the data file is not drawn,
- a bare component lead or its eyelet ends at a point that no chain of leads
  connects to the wire point of a pin,
- a draw function draws a pin the data file does not have,
- a label is wider than its drawing. The width is an estimate: 0.62 em per
  character. The font of the viewer is not measured.

2 pins are neighbours when their distance is at most 1.6 times the distance
from each of them to its nearest pin.

## Wire palette

A wire of the pictorial wiring view has 1 of 15 colours. The colours are the
same in the light and in the dark theme. A wire of the kind `component` is
the lead of a part and has no colour.

- Supply plus is red: the 8 nets of `PLUS_NETS` (battery plus, PD input and
  output, servo supply, the output of both shunts, 3.3 V, VBUS).
- Ground and supply minus are black: the 3 nets of `MINUS_NETS`.
- A signal has 1 of the other 13 colours: its net decides first (`BY_NET`),
  its signal class second (`BY_CLASS`). No signal is red or black.
- A wire of the class `power` or `gnd` on a net outside the 2 sets has no
  colour. The check reports it as `wire-colour`.

Contrast ratio after WCAG (Web Content Accessibility Guidelines) 2.2 against
each background. The minimum for graphics is 3:1. The first 4 backgrounds are
the page tokens `--surface` and `--bg` of `src/style.css` in both themes. The
fifth is the card of the part drawings.

| Colour | Core | Casing | light surface #ffffff | light page #f4f5f7 | dark surface #171b22 | dark page #0e1116 | drawing card #efebe3 |
|---|---|---|---:|---:|---:|---:|---:|
| red | `#d6281e` | none | 5.02 | 4.60 | 3.44 | 3.77 | 4.22 |
| black | `#15191f` | `#c9d1dc` | 17.64 (core) | 16.17 (core) | 11.21 (casing) | 12.28 (casing) | 14.83 (core) |
| white | `#f4f5f7` | `#15191f` | 17.64 (casing) | 16.17 (casing) | 15.83 (core) | 17.34 (core) | 14.83 (casing) |
| orange | `#c96a0a` | none | 3.78 | 3.47 | 4.56 | 5.00 | 3.18 |
| ochre | `#9a8000` | none | 3.84 | 3.52 | 4.49 | 4.92 | 3.23 |
| olive | `#688c12` | none | 3.93 | 3.60 | 4.40 | 4.82 | 3.30 |
| green | `#1c9440` | none | 3.92 | 3.59 | 4.41 | 4.83 | 3.30 |
| teal | `#0e918b` | none | 3.86 | 3.54 | 4.47 | 4.90 | 3.25 |
| cyan | `#1a8ac8` | none | 3.81 | 3.49 | 4.53 | 4.97 | 3.20 |
| blue | `#4a74ea` | none | 4.22 | 3.87 | 4.09 | 4.48 | 3.55 |
| violet | `#9163f2` | none | 3.98 | 3.65 | 4.33 | 4.75 | 3.35 |
| magenta | `#c247cf` | none | 4.12 | 3.78 | 4.19 | 4.59 | 3.47 |
| pink | `#e0478a` | none | 3.86 | 3.54 | 4.47 | 4.90 | 3.25 |
| brown | `#9a6a45` | none | 4.64 | 4.26 | 3.72 | 4.07 | 3.91 |
| grey | `#79838f` | none | 3.85 | 3.53 | 4.49 | 4.91 | 3.24 |

`test/cli.test.ts` fails when this table differs from the output of
`node drawings/cli.ts --palette`.

Black and white carry a casing: an outline of 1 px on each side of the core.
No single colour reaches 3:1 against both the light and the dark backgrounds.
The table gives the higher of the 2 ratios and names its carrier.

1 colour serves 2 uses: grey is phase B and the stubs of the class `nc`.

2 plus wires, or 2 minus wires, on neighbouring pins share their colour, for
example both sides of a shunt. The `neighbour` rule exempts them. The wire
number tells them apart.

Colour is not the only carrier: a wire keeps its wire number of the data file.

## Limits

- INA219 and ADXL345: drawn, not in the data file, no wire ends on them. Their
  titles say so.
- INA3221: the drawing carries the print of the board, 17 pins. Its labels are
  not compared with the data file. `catalogueMap` leads from the 8 pins of the
  data file to them. Pin `A0` maps to `null`: the address is set by a solder
  bridge, the board has no wire point for it. Pin `LP` maps to 3 pads.
- Label fit is an estimate. A label that fits by the estimate and overlaps a
  shape of the drawing is not found.
- The carrier board drawing omits the 2-pole VIN terminal and the 3 x 3 pin
  header of the carrier.
- Carrier board: the servo strips S, V and G beside the screw terminals are
  drawn and carry no anchor: the data file lists no pin for them.
  `stripPoint(pad, row)` in `parts.ts` gives the place of each strip pin.
- Carrier board: the jumper and the DC-DC converter are drawn at the USB end,
  where photo `05-rp2350-can-breakout` shows them, not to scale. The print of
  the jumper reads 3V3 and 5V from the left beside the header and V Selector below it, per the
  product photo of the vendor, with the cap drawn on the 5V pair, the position the servo needs, and labelled "5V for the servo" (German "5V für den Servo"). The board is delivered with the cap on 3V3. The schematic REV 1.0 names the 8-pin chip U9
  (converter) and the inductor.
- microSD card, USB cable and hook-up wire: drawings without a pin.
- How the colours look with a colour vision deficiency: not measured.
- Not confirmed by a print on the part or by a maker document: which 2 tabs
  of the emergency stop button open on a press, which side of the INA228
  module with XT90 plugs is the battery side.
- The shapes follow photos of the boards where one exists. Servo, ESC, PD
  charger, battery, servo supply, motor simulator, phase tap and pull-up
  resistors are generic pictures.
