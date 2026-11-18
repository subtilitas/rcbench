"""INA219 current sensor board, print CJMCU, part by part.

The board is the one of hardware/kicad/modules/ina219_cjmcu219.py;
outline, holes and pads come from that spec. This model adds the
plating of the holes, the shunt, the chip, the 5 chip parts, the pads
of the 2 address bridges and the print of both sides.

Sources: two owner photos on 5mm squared paper (top side, underside;
not in the repository). Positions are in mm from the board's center,
within about 0.2mm. The print's letters are the font of _font.py set
to the height and width each line has in the photos, not traced.

Packages by their look: shunt 2512, chip SOT-23-8, chip parts 0805.
Their sizes and heights are the packages' nominal ones.

Not measured: the board's thickness (drawn 1.6). The vias and the
tracks are not drawn.

Frame: as the KiCad model of the module. Origin at header pin 1 (Vcc)
on the board's underside, x along the header towards pin 6, y from the
header towards the terminal holes, z up. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("ina219_cjmcu219")
STEM = "ina219_cjmcu219"
BLUE = (0.05, 0.22, 0.55)
B = _board.Board(kicad)
CX, CY = kicad.W / 2, kicad.H / 2

HEADER = [(kicad.HDR_X0 + 2.54 * i - CX, kicad.HDR_Y - CY)
          for i in range(6)]
TERMINAL = [(x - CX, kicad.TERM_Y - CY) for _n, _s, x in kicad.TERM]
MOUNT = [(x - CX, y - CY) for x, y in kicad.HOLES]
SHUNT = (0.0, -2.25)
CHIP = (0.0, 3.3)
CAPACITOR = (-9.05, 3.5)
PULL_UPS = ((-6.35, 3.5), (-3.95, 3.5))
ADDRESS = ((5.6, 2.1), (5.6, 4.55))
BRIDGES = ((8.65, 2.05), (10.1, 2.05), (8.65, 4.6), (10.1, 4.6))
# Print: text, x, y of the line's center, letter height, width, turn.
TOP_PRINT = (
    ("Vin-", -5.85, -10.05, 0.95, 3.3), ("Vin+", 5.95, -10.05, 0.95, 3.5),
    ("INA219 DC", -8.15, -4.75, 0.95, 7.7),
    ("Current", -8.35, -2.95, 0.95, 5.9),
    ("Sensor", -8.5, -1.15, 0.95, 5.0),
    ("R5", 0.35, 0.3, 0.6, 1.1), ("I2C Address", 7.55, 0.05, 0.9, 7.5),
    ("A1", 11.6, 2.05, 0.7, 1.2, 90.0), ("A0", 11.6, 4.6, 0.7, 1.2, 90.0),
    ("Gnd", -3.65, 7.0, 0.95, 2.5), ("Sda", 1.75, 7.0, 0.95, 2.5),
    ("Vin+", 6.9, 6.9, 0.95, 3.2), ("Vcc", -6.0, 10.4, 0.9, 2.6),
    ("Scl", -0.9, 10.4, 0.9, 2.2), ("Vin-", 4.6, 10.4, 0.9, 3.2),
)
UNDER_PRINT = (
    ("INA219 DC Current", -0.1, -5.6, 1.2, 18.6),
    ("Monitor", -0.1, -3.65, 1.2, 7.2), ("CJMCU", -0.2, -0.75, 1.0, 4.4),
    ("Bus Voltage: 0-26V", 0.0, 1.15, 1.2, 16.0),
    ("Max Current:3.2A", 0.45, 3.7, 1.1, 14.1),
    ("VCC/LOGIC: 3-5V", 0.45, 5.95, 1.1, 13.3),
)
# Printed frames, (x, y, width, height): the terminal's place, the
# three upright chip parts, the address resistors and their bridges.
FRAMES = ((0.0, -7.0, 7.2, 5.55), (-9.05, 3.1, 2.0, 3.8),
          (-6.4, 3.1, 2.15, 3.8), (-3.95, 3.1, 2.15, 3.8),
          (5.6, 2.1, 3.8, 2.0), (5.6, 4.55, 3.8, 2.0),
          (9.4, 2.05, 3.2, 2.0), (9.4, 4.6, 3.2, 2.0))


def board():
    """The plate and the plating of its 12 holes."""
    out = B.plate(BLUE, B.outline(kicad.RADIUS),
                  [(x, y, 1.0) for x, y in HEADER]
                  + [(x, y, 1.2) for x, y in TERMINAL]
                  + [(x, y, kicad.HOLE) for x, y in MOUNT])
    for x, y in HEADER:
        out += B.through(p.hole_pad(1.0, 1.8, B.t, p.TIN), x, y)
    for x, y in TERMINAL:
        out += B.through(p.hole_pad(1.2, 2.0, B.t, p.TIN), x, y)
    for x, y in MOUNT:
        out += B.through(p.hole_pad(kicad.HOLE, kicad.RING, B.t, p.TIN),
                         x, y)
    return out


def top_side():
    """The parts and the print of the top side."""
    out = B.top(p.r2512(), *SHUNT)
    out += B.top(p.text("R100", 1.5, p.WHITE, 3.6), *SHUNT, 180.0, z=0.6)
    out += B.top(p.sot23_8(), *CHIP, 90.0)
    out += B.top(p.c0805(), *CAPACITOR, 90.0)
    for x, y in PULL_UPS:
        out += B.top(p.r0805(), x, y, 90.0)
    for x, y in ADDRESS:
        out += B.top(p.r0805(), x, y)
    for x, y in BRIDGES:
        out += B.top(p.pad(1.0, 1.4), x, y)
    for x, y, w, h in FRAMES:
        out += B.top(p.frame(-w / 2, -h / 2, w / 2, h / 2), x, y)
    return out + B.words(TOP_PRINT)


def bare():
    """The board without a header, as shapes."""
    return board() + top_side() + B.words(UNDER_PRINT, under=True)


def kicad_model():
    """The module on a 6-pin header, the model of the KiCad footprint.

    Returns:
        The solids: the header's plastic stands on z = 0 and the board's
        underside pads lie on it.
    """
    x = (HEADER[0][0] + HEADER[-1][0]) / 2
    header = p.pin_header(6, below=6.0, above=3.0)
    return s.items(s.place(bare(), z=2.5 + p.PAD)
                   + B.through(header, x, HEADER[0][1]))


def parts(_colors):
    """The model of the bare board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bare())}
