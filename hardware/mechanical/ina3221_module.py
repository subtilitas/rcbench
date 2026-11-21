"""INA3221 board with 3 shunts, 38.0 x 31.8mm, black, part by part.

    ina3221_module   the bare board: plate, 20 plated holes, the parts
                     and the print of both sides

Sold as DAOKAI and others; the same board in CJMCU-3221 style with 3
mounting holes. The holes are those of hardware/kicad/modules/
ina3221_module.py, measured from two owner photos.

Parts and print: a seller's top view and underside view of 1024 x
1024px (makerhub.my, INA322156ADA459.png and INA3221F0E23950.png, not
in the repository). The top view is laid onto the board by 11 of its
holes; they fall within 0.2mm of the KiCad spec's places. Part places
are read from that picture at 30px/mm, within about 0.3mm.

Drawn on the top side: the INA3221 (QFN-16, 4.0 x 4.0), 3 shunts R100
(2512) with their print, the middle one turned round, a capacitor and 2
resistors at each shunt, 5 LEDs with their resistors, 8 resistors and
capacitors and 2 smaller parts under the header row, 4 pairs of
solder-bridge pads at the left edge, the print with its 4 boxes.
Underside: the names of the 17 holes.

Package sizes are nominal. Not measured: the board's thickness (drawn
1.6), the mounting holes' ring (4.4), the print's letter sizes beyond
their line lengths. Which parts under the header row are resistors and
which capacitors is read from their colour.

Mounted, the board stands on an 8-pin header, its underside 2.5 over
the carrier. The 6 channel holes and the 3 large holes are empty: what
joins them to a carrier is not drawn.

Frame: the KiCad model's. Origin at pad 1 on the board's underside (on
the carrier's top side for the mounted model), x to the right, y up.
Places below are mm from the board's upper left corner seen from the
top side, x to the right, y down, header row at the top.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("ina3221_module")
STEM = "ina3221_module"
BLACK = (0.06, 0.06, 0.07)
W, H = kicad.FOOTPRINT["board"]
B = _board.Board(kicad)

HEADER = [(kicad.PIN1_X - 2.54 * i, kicad.HDR_Y) for i in range(8)]
LARGE = [(kicad.BIG_X, y) for _n, y in kicad.BIG]
CHANNEL = [(c + d, kicad.CH_Y) for c in kicad.CH_C for d in (-2.54, 2.54)]
MOUNT, RING = kicad.MOUNT, 4.4
CHIP = (16.27, 11.33)
SHUNTS = ((10.83, 20.43, 0.0), (19.2, 20.43, 180.0), (27.53, 20.43, 0.0))
SHUNT_CAPS = ((10.73, 15.67), (19.07, 15.67), (27.4, 15.67))
SHUNT_RESISTORS = ((8.6, 17.67), (12.9, 17.67), (17.07, 17.67),
                   (21.33, 17.67), (25.33, 17.67), (29.6, 17.67))
LED_Y = (5.87, 7.33, 8.83, 10.37, 11.83)
LED_X, LED_RESISTOR_X, LED_NAME_X = 28.4, 24.6, 26.45
ROW_Y = 4.5
ROW_RESISTORS = (9.0, 10.7, 12.4, 14.0, 20.5, 22.2)
ROW_CAPS = (17.3, 18.9)
ROW_SMALL = ((15.6, 4.0), (15.6, 5.4))
BRIDGES = ((1.67, 7.57), (1.67, 10.07), (1.67, 12.57), (1.67, 15.2))
FRAMES = ((32.07, 0.93, 37.27, 16.33),) + tuple(
    (c - 5.05, 26.0, c + 5.05, 31.2) for c in kicad.CH_C)
# Text, x, y, letter height, line length, turn.
TOP_PRINT = (
    ("A0", 1.5, 5.75, 0.8, 1.3), ("A0", 1.5, 16.7, 0.8, 1.3),
    ("VS", 4.0, 7.5, 0.8, 1.6), ("SCL", 4.15, 10.0, 0.8, 1.9),
    ("SDA", 4.15, 12.57, 0.8, 1.9), ("GND", 4.2, 15.07, 0.8, 2.0),
    ("CH1", 6.93, 24.27, 1.27, 3.0), ("CH2", 19.2, 24.27, 1.27, 3.3),
    ("CH3", 31.2, 24.27, 1.27, 3.3),
    ("VPU", 31.1, 3.6, 1.0, 2.9, -90.0), ("GND", 31.2, 8.7, 1.0, 2.9, -90.0),
    ("GND", 31.2, 13.8, 1.0, 2.9, -90.0),
) + tuple((name, LED_NAME_X, y, 0.6, 0.5 * len(name), -90.0)
          for name, y in zip(("VS", "PV", "C", "W", "TC"), LED_Y,
                             strict=True))
UNDER_PRINT = tuple(
    (name, x, y + 4.4, 1.1, 0.9 * len(name) + 0.4, 90.0)
    for name, (x, y) in zip(kicad.NAMES, HEADER, strict=True)) + tuple(
    (name, x - 3.3, y, 1.1, 3.2, 90.0)
    for name, (x, y) in zip(("VPU", "GND", "GND"), LARGE, strict=True)
) + tuple(
    (name, x, y - 4.4, 1.0, 3.5)
    for name, (x, y) in zip(("VIN1-", "VIN1+", "VIN2-", "VIN2+", "VIN3-",
                             "VIN3+"), CHANNEL, strict=True))


def at(x, y):
    """A place from the board's upper left corner as Board takes it."""
    return x - W / 2, y - H / 2


def board():
    """The plate and the plating of its 20 holes."""
    out = B.plate(BLACK, B.outline(0.3),
                  [(*at(x, y), 1.0) for x, y in HEADER]
                  + [(*at(x, y), 2.6) for x, y in LARGE + CHANNEL]
                  + [(*at(x, y), 3.2) for x, y in MOUNT])
    for i, (x, y) in enumerate(HEADER):
        out += B.through(p.hole_pad(1.0, 1.7, B.t, p.TIN, square=i == 0),
                         *at(x, y))
    for x, y in LARGE + CHANNEL:
        out += B.through(p.hole_pad(2.6, 4.0, B.t, p.TIN), *at(x, y))
    for x, y in MOUNT:
        out += B.through(p.hole_pad(3.2, RING, B.t, p.TIN), *at(x, y))
    return out


def top_side():
    """The parts and the print of the top side."""
    out = B.top(p.qfn(4.0, 4.0, 0.9, 4, 4, 0.65), *at(*CHIP))
    for x, y, turn in SHUNTS:
        out += B.top(p.r2512(), *at(x, y))
        out += B.top(p.text("R100", 1.5, p.WHITE, 3.6), *at(x, y), turn,
                     z=0.6)
    for x, y in SHUNT_CAPS:
        out += B.top(p.c0603(), *at(x, y))
    for x, y in SHUNT_RESISTORS:
        out += B.top(p.r0603(), *at(x, y))
    for y in LED_Y:
        out += B.top(p.led0603(), *at(LED_X, y))
        out += B.top(p.r0603(), *at(LED_RESISTOR_X, y))
    for x in ROW_RESISTORS:
        out += B.top(p.r0603(), *at(x, ROW_Y), 90.0)
    for x in ROW_CAPS:
        out += B.top(p.c0603(), *at(x, ROW_Y), 90.0)
    for x, y in ROW_SMALL:
        out += B.top(p.r0402(), *at(x, y))
    for x, y in BRIDGES:
        for dx in (-0.4, 0.4):
            out += B.top(p.pad(0.6, 0.6), *at(x + dx, y))
    for x0, y0, x1, y1 in FRAMES:
        (ax, ay), (bx, by) = B.at(*at(x0, y0)), B.at(*at(x1, y1))
        out += s.place(p.frame(ax, by, bx, ay), z=B.t)
    return out + B.words([(t, *at(x, y), *rest)
                          for t, x, y, *rest in TOP_PRINT])


def bare():
    """The board without its header, as shapes."""
    return board() + top_side() + B.words(
        [(t, *at(x, y), *rest) for t, x, y, *rest in UNDER_PRINT],
        under=True)


def mounted():
    """The board on its 8-pin header, as shapes.

    The header's plastic stands on z = 0 and the board's underside pads
    lie on it.
    """
    x = (HEADER[0][0] + HEADER[-1][0]) / 2
    header = p.pin_header(8, below=6.0, above=3.0)
    return (s.place(bare(), z=2.5 + p.PAD)
            + B.through(header, *at(x, HEADER[0][1])))


def kicad_model():
    """The model of the KiCad footprint: the board on its header."""
    return s.items(mounted())


def parts(_colors):
    """The model of the bare board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bare())}
