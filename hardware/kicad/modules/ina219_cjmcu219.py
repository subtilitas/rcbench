"""INA219 current sensor board, print CJMCU, blue, 24.2 x 21.9mm.

6-hole header and the 2 holes of a screw terminal that is not fitted.
Top print "INA219 DC Current Sensor", underside print "INA219 DC
Current Monitor CJMCU Bus Voltage: 0-26V Max Current:3.2A VCC/LOGIC:
3-5V". Shunt R100 (0.1Ω), size 2512.

Measured from two owner photos (top side, underside; not in the
repository) of the board lying on 5mm squared paper. The paper's grid
gives the scale; hardware/mechanical/_photo_grid.py fits it within
0.20mm. The header's 5 x 2.54mm reads 12.75mm in that scale, so sizes
read off the board's top are divided by 1.005. Positions are within
about 0.2mm.

Results: board 24.2 x 21.9mm, corners rounded by about 1.2mm. Header
row 2.0mm above the lower edge, pad 1 (Vcc) 5.75mm from the left edge.
Terminal holes 3.50mm apart, 3.6mm below the upper edge, 16.3mm above
the header row. Mounting holes 20.2mm apart along the header, 2.95mm
below the upper edge and on the header row's line, opening about
2.5mm, ring about 3.4mm, plated.

Header Vin- and Vin+ are the same nets as the terminal holes by the
board's print; here they are separate pads 5/7 and 6/8.

Unknown: the drills (drawn 1.0mm for the header, 1.2mm for the
terminal), whether the mounting holes connect to a net. The board's
thickness is not measured; the model draws 1.6mm.

The 3D model is hardware/mechanical/ina219_cjmcu219.py, on a 6-pin
header.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

NAME = "INA219_CJMCU-219_24.2x21.9mm"
DESC = ("INA219 current sensor board (INA219 DC Current Sensor, CJMCU) "
        "24.2 x 21.9mm, shunt 0.1Ω (R100). Pads 1-6 = header Vcc Gnd Scl "
        "Sda Vin- Vin+, 2.54mm, pad 1 square; pads 7-8 = holes of a "
        "3.50mm screw terminal Vin- Vin+, 16.3mm above the header row. "
        "4 mounting holes about 2.5mm, 20.2 x 16.95mm apart. Measured "
        "from owner photos on squared paper, about 0.2mm.")

W, H = 24.2, 21.9
HDR_X0, HDR_Y = 5.75, 19.9
NAMES = ("Vcc", "Gnd", "Scl", "Sda", "Vin-", "Vin+")
TERM_Y = 3.6
TERM = (("7", "Vin-", 10.35), ("8", "Vin+", 13.85))
HOLES = ((2.0, 2.95), (22.2, 2.95), (2.0, 19.9), (22.2, 19.9))
HOLE, RING = 2.5, 3.4
SHUNT = (8.95, 7.1, 15.25, 10.3)
CHIP = (12.1, 14.25)
RADIUS = 1.2


def _pads():
    pads = [{"num": str(i + 1), "x": HDR_X0 + 2.54 * i, "y": HDR_Y,
             "shape": "rect" if i == 0 else "circle", "size": (1.7, 1.7),
             "drill": 1.0} for i in range(6)]
    pads += [{"num": n, "x": x, "y": TERM_Y, "shape": "circle",
              "size": (2.0, 2.0), "drill": 1.2} for n, _s, x in TERM]
    pads += [{"num": "", "x": x, "y": y, "shape": "circle",
              "size": (HOLE, HOLE), "drill": HOLE, "type": "np"}
             for x, y in HOLES]
    return pads


def _drawn():
    cx, cy = CHIP
    items = [("rect", 8.5, 1.15, 15.7, 6.7), ("rect",) + SHUNT,
             ("rect", cx - 1.45, cy - 0.8, cx + 1.45, cy + 0.8)]
    items += [("circle", x, y, RING / 2) for x, y in HOLES]
    return items


def _labels():
    items = [("text", n, HDR_X0 + 2.54 * i, 21.3 if i % 2 == 0 else 17.9,
              0.9) for i, n in enumerate(NAMES)]
    return items + [("text", "Vin-", 6.25, 0.9, 0.9),
                    ("text", "Vin+", 18.05, 0.9, 0.9)]


def _rounded(w, h, r, n=4):
    """Closed outline of a w x h board with corner radius r."""
    import math
    pts = []
    for cx, cy, a0 in ((w - r, r, -90), (w - r, h - r, 0), (r, h - r, 90),
                       (r, r, 180)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((round(cx + r * math.cos(a), 3),
                        round(cy + r * math.sin(a), 3)))
    return pts


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (W, H),
    "outline": _rounded(W, H, RADIUS),
    "origin": (HDR_X0, HDR_Y),
    "pads": _pads(),
    "silk": _drawn() + _labels(),
    "fab": _drawn() + [("text", "R100", 12.1, 8.7, 0.8),
                       ("text", "INA219", 12.1, 16.7, 0.6)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-12.7, 10.16, 12.7, -10.16),
    "graphics": [
        ("rect", -3.3, 2.79, 3.3, 0.76),
        ("text", "R100", 0, 1.78, 0.9),
        ("rect", -1.27, -1.52, 1.27, -3.05),
        ("text", "INA219", 0, -4.32, 0.9),
        ("text", "0.1Ω shunt", 0, -7.62, 0.9),
    ],
    "pins": [
        ("power_in", "Vcc", "1", -15.24, 5.08, 0),
        ("power_in", "Gnd", "2", -15.24, 2.54, 0),
        ("input", "Scl", "3", -15.24, -2.54, 0),
        ("bidirectional", "Sda", "4", -15.24, -5.08, 0),
        ("passive", "Vin+", "6", 15.24, 7.62, 180),
        ("passive", "Vin+", "8", 15.24, 5.08, 180),
        ("passive", "Vin-", "5", 15.24, -5.08, 180),
        ("passive", "Vin-", "7", 15.24, -7.62, 180),
    ],
}

MODEL = {"detail": "ina219_cjmcu219"}
