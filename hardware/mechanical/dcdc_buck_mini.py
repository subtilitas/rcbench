"""Mini560 buck converter board, 30.0 x 18.0mm, blue, part by part.

    dcdc_buck_mini   the bare board: plate, 4 corner pads with 2 holes
                     each, the enable hole, the parts and the print of
                     both sides

The board of hardware/kicad/modules/dcdc_buck_mini.py. Its underside
prints "Mini 560" and "DC-DC Step-down".

Sources, none in the repository: a seller's top view and underside view
of 800 x 800px (kunkune.co.uk, mini560-Step-Down-module-siz.jpg and
-back.jpg) and the same seller's dimension drawing, which prints 30,
18, 26, 2.54 and 8.92mm, the KiCad spec's hole grid. The top view is
laid onto the board by its 8 corner holes, which fit within 0.02mm, and
part places are read from it at 40px/mm, within about 0.3mm. The
underside's print is read from the unrectified view at 22px/mm, within
about 0.5mm. Three owner photos of 2 boards on a carrier show the same
parts.

Drawn on the top side: the inductor 10.3 x 10.2 with its print 4R7 and
its 2 tabs, 2 input and 4 output capacitors (1206), the controller
(QFN, 3.0 x 3.0, 20 pads; its type is not printed legibly), 4 small
capacitors, 6 resistors, the LED, the round solder-bridge pad that
switches the LED on, and the print of the 4 signs and EN. Underside:
the print with its arrow.

Not measured: the board's thickness (drawn 1.6). The inductor is drawn
3.8 high, which gives the seller's 5.4 over all. The half holes at the
board's 2 short edges and the print's frames round the parts are not
drawn. The enable hole is drawn at the KiCad spec's place, 9.4 from the
input end; the seller's top view gives 9.0.

Mounted, the board stands on 4 pin headers of 2, one under each corner
pad, its underside 2.5 over the carrier (owner, 2026-11-21). The enable
hole has no pin; solder and the enable wire are not drawn.

Frame: the KiCad model's. Origin at pad 1 on the board's underside (on
the carrier's top side for the mounted model), x to the right, y up.
Places below are mm from the board's upper left corner seen from the
top side, x to the right, y down, the input at the left and IN+ at the
top.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("dcdc_buck_mini")
STEM = "dcdc_buck_mini"
BLUE = (0.05, 0.22, 0.55)
FERRITE = (0.62, 0.61, 0.58)
W, H = kicad.FOOTPRINT["board"]
B = _board.Board(kicad)

CORNERS = [(x, ys) for x in (2.0, 28.0) for ys in (kicad.TOP, kicad.BOTTOM)]
CORNER_PAD = (3.1, 5.2)         # along x, along y, round its 2 holes
ENABLE = (9.4, 17.0, 0.8, 1.4)  # x, y, drill, pad
INDUCTOR = (13.35, 5.3, 10.3, 10.2, 3.8)
TABS = ((7.6, 5.2), (19.1, 5.2))
INPUT_CAPS = ((3.0, 7.6), (3.0, 10.45))
OUTPUT_CAPS = ((21.9, 2.9), (24.5, 2.9), (21.9, 8.4), (24.5, 8.4))
SMALL_CAPS = ((11.05, 11.5), (14.6, 11.5), (7.0, 13.25), (17.9, 12.45))
RESISTORS = ((17.9, 13.9, 0.0), (17.9, 15.25, 0.0), (17.9, 16.75, 0.0),
             (22.4, 13.05, 0.0), (6.2, 15.25, 90.0), (7.6, 15.25, 90.0))
CHIP = (12.45, 14.7)
LED = (21.7, 15.25)
BRIDGE = (24.4, 16.05, 1.6)
TOP_PRINT = (("+", 4.2, 3.2, 1.0, 0.9), ("-", 4.1, 15.2, 1.0, 0.9),
             ("+", 28.2, 6.7, 1.0, 0.9), ("-", 28.25, 11.4, 1.0, 0.9),
             ("EN", 7.55, 17.3, 0.8, 1.4))
UNDER_PRINT = (("DC-DC Step-down", 14.7, 3.45, 1.3, 14.7),
               ("Mini 560", 15.6, 12.9, 1.9, 10.1),
               ("OUT+", 27.3, 6.8, 1.2, 4.6), ("OUT-", 27.4, 11.2, 1.2, 4.6),
               ("IN+", 1.8, 6.9, 1.2, 2.9), ("IN-", 1.9, 11.05, 1.2, 2.9),
               ("EN", 7.3, 17.2, 0.8, 1.2))
# The underside's arrow, seen from the top side: it points to the output.
ARROW = ((8.7, 8.2), (20.0, 8.2), (20.0, 6.45), (22.5, 8.6), (20.0, 10.8),
         (20.0, 9.1), (8.7, 9.1))


def at(x, y):
    """A place from the board's upper left corner as Board takes it."""
    return x - W / 2, y - H / 2


def board():
    """The plate, its 4 corner pads and the enable hole's plating."""
    ex, ey, drill, pad = ENABLE
    holes = [(x, y, 1.0) for x, ys in CORNERS for y in ys]
    out = B.plate(BLUE, B.outline(0.2), [(*at(x, y), d) for x, y, d in holes]
                  + [(*at(ex, ey), drill)])
    wide, long = CORNER_PAD
    for x, ys in CORNERS:
        cx = wide / 2 + 0.2 if x < W / 2 else W - wide / 2 - 0.2
        (ax, ay), (bx, by) = (B.at(*at(cx - wide / 2, sum(ys) / 2 - long / 2)),
                              B.at(*at(cx + wide / 2, sum(ys) / 2 + long / 2)))
        cuts = [s.circle(*B.at(*at(x, y)), 0.5) for y in ys]
        for z0 in (-p.PAD, B.t):
            out.append(s.ext(s.rect(ax, by, bx, ay), z0, z0 + p.PAD, p.TIN,
                             holes=cuts))
    out += B.through(p.hole_pad(drill, pad, B.t, p.TIN), *at(ex, ey))
    return out


def top_side():
    """The parts and the print of the top side."""
    x, y, wide, deep, high = INDUCTOR
    out = B.top(p.block(wide, deep, high, FERRITE, 1.2), *at(x, y))
    out += B.top(p.text("4R7", 3.6, p.EPOXY, 4.7), *at(x, y), 180.0, z=high)
    for x, y in TABS:
        out += B.top(p.pad(1.0, 3.2), *at(x, y))
    for x, y in INPUT_CAPS:
        out += B.top(p.c1206(), *at(x, y))
    for x, y in OUTPUT_CAPS:
        out += B.top(p.c1206(), *at(x, y), 90.0)
    for x, y in SMALL_CAPS:
        out += B.top(p.c0603(), *at(x, y))
    for x, y, turn in RESISTORS:
        out += B.top(p.r0603(), *at(x, y), turn)
    out += B.top(p.qfn(3.0, 3.0, 0.75, 5, 5, 0.4), *at(*CHIP))
    out += B.top(p.led0603(), *at(*LED), 90.0)
    x, y, d = BRIDGE
    out += B.top([s.ext(s.circle(0.0, 0.0, d / 2), 0.0, p.PAD, p.TIN)],
                 *at(x, y))
    return out + B.words([(t, *at(x, y), *rest)
                          for t, x, y, *rest in TOP_PRINT])


def underside():
    """The print of the underside with its arrow."""
    arrow = [B.at(*at(x, y)) for x, y in reversed(ARROW)]
    return [s.ext(arrow, -p.PRINT, 0.0, p.WHITE)] + B.words(
        [(t, *at(x, y), *rest) for t, x, y, *rest in UNDER_PRINT],
        under=True)


def bare():
    """The board alone, as shapes."""
    return board() + top_side() + underside()


def mounted():
    """The board on its 4 pin headers of 2, as shapes.

    The headers' plastic stands on z = 0 and the board's underside pads
    lie on it.
    """
    out = s.place(bare(), z=2.5 + p.PAD + p.PRINT)
    header = p.pin_header(2, below=6.0, above=3.0)
    for x, ys in CORNERS:
        out += B.through(header, *at(x, sum(ys) / 2), 90.0)
    return out


def kicad_model():
    """The model of the KiCad footprint: the board on its headers."""
    return s.items(mounted())


def parts(_colors):
    """The model of the bare board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bare())}
