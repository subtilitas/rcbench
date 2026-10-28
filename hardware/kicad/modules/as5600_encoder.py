"""AS5600 magnetic angle encoder board, 22.5 x 22.5mm, white, 4 corner holes.

Measured from the owner's photo 10-as5600-b.webp (component side, oblique).
The photo is rectified by the 4 corner-hole centres (ellipse fits) to a
square; the scale comes from the two headers, 2.54mm = 79.5px (3 + 3
pitches, 79.3 and 79.8px), so 31.3px/mm. The SOIC-8 lead pitch gives the
same scale (1.27mm = 40px). Positions are within about 0.3mm (estimate, not
checked with a caliper).

Results: corner holes on a 16.0mm square grid, diameter about 3.9mm (photo;
the Tinytronics listing says 3.7mm). Board 22.4 x 22.7mm in the photo,
drawn 22.5 x 22.5mm centred on the hole grid (the listing says 23 x 23mm).
Corners cut at 45 degrees, about 1.2mm
(photo).
3-hole row GND OUT VCC (VCC square) 1.39mm below the top hole line, its
middle hole 0.22mm right of the board centre; 4-hole row GPO SDA SCL DIR
(DIR square) 1.39mm above the bottom hole line, its centre 0.78mm right of
the board centre. Rows 13.22mm apart. The rows are not on one 2.54mm grid.

Sensor: AS5600 in SOIC-8, package centre at (11.35, 11.15)mm in the board
frame, 0.1mm right of and 0.1mm above the board centre (within the 0.3mm
estimate, so equal to the board centre as far as the photo shows). Pin 1 is
at the top-right lead, next to the silk dot; pins 1-4 face the 3-hole row,
pins 5-8 (PGO SDA SCL DIR) face the 4-hole row. The AS5600 datasheet puts
the Hall array at the package centre: the magnet axis goes through
(11.35, 11.15)mm. The magnet circle is drawn 6mm in diameter (a common
diametric magnet); its size is not measured. Air gap: not stated here; the
datasheet range applies.

Unknown: board thickness, the pull-up values on the board, whether the
mounting holes connect to GND, the print of GPO (the catalogue marks it
unconfirmed; the photo shows "GPO").
"""

NAME = "AS5600_Encoder_22.5x22.5mm"
DESC = ("AS5600 magnetic angle encoder board 22.5 x 22.5mm, 4 corner holes "
        "on a 16.0mm grid. Pads: 1 VCC (square), 2 OUT, 3 GND on the 3-hole "
        "row; 4 DIR (square), 5 SCL, 6 SDA, 7 GPO on the 4-hole row; 2.54mm, "
        "rows 13.22mm apart. Magnet axis over the AS5600 package centre at "
        "(-2.65, 6.51)mm from pad 1. Scaled from the owner's photo, about "
        "0.3mm; check with a caliper before ordering a PCB.")

W = H = 22.5
TOP_Y, BOT_Y = 4.64, 17.86
TOP = (("1", "VCC", 14.0), ("2", "OUT", 11.46), ("3", "GND", 8.92))
BOT = (("4", "DIR", 15.84), ("5", "SCL", 13.30), ("6", "SDA", 10.76),
       ("7", "GPO", 8.22))
HOLES = ((3.25, 3.25), (19.25, 3.25), (3.25, 19.25), (19.25, 19.25))
HOLE_D = 3.9
CHIP = (11.35, 11.15)
MAGNET_D = 6.0
# Silk boxes of the passives as printed: (x0, y0, x1, y1, label)
PASSIVES = ((2.37, 6.46, 6.39, 8.57, "R3"), (2.37, 10.04, 6.39, 12.14, "R2"),
            (2.37, 13.74, 6.39, 15.85, "R4"), (15.15, 6.84, 18.92, 9.01, "C1"),
            (15.15, 10.04, 18.92, 12.21, "C2"),
            (15.15, 13.23, 18.92, 15.24, "R1"))


def _pads():
    pads = []
    for row, y in ((TOP, TOP_Y), (BOT, BOT_Y)):
        for i, (num, _name, x) in enumerate(row):
            pads.append({"num": num, "x": x, "y": y,
                         "shape": "rect" if i == 0 else "circle",
                         "size": (1.7, 1.7), "drill": 1.0})
    return pads


def _chip():
    """SOIC-8 body and leads, top view, pin 1 top right."""
    cx, cy = CHIP
    items = [("rect", cx - 2.45, cy - 1.95, cx + 2.45, cy + 1.95)]
    for dx in (-1.905, -0.635, 0.635, 1.905):
        items += [("rect", cx + dx - 0.2, cy - 3.0, cx + dx + 0.2, cy - 1.95),
                  ("rect", cx + dx - 0.2, cy + 1.95, cx + dx + 0.2, cy + 3.0)]
    items.append(("circle", cx + 1.9, cy - 1.4, 0.25))
    return items


def _magnet():
    cx, cy = CHIP
    r = MAGNET_D / 2
    return [("circle", cx, cy, r),
            ("line", cx - r - 0.8, cy, cx + r + 0.8, cy),
            ("line", cx, cy - r - 0.8, cx, cy + r + 0.8)]


def _common():
    items = [("rect", 7.80, 3.04, 15.53, 5.56),
             ("rect", 7.16, 16.36, 17.45, 18.82)]
    items += [("circle", x, y, HOLE_D / 2) for x, y in HOLES]
    items += [("rect",) + p[:4] for p in PASSIVES]
    return items + _chip()


def _labels():
    items = [("text", n, x, 1.6, 0.8, 90) for _num, n, x in TOP]
    items += [("text", n, x, 20.7, 0.8, 90) for _num, n, x in BOT]
    items.append(("circle", 14.6, 8.45, 0.25))
    return items


def _chamfered(w, h, c):
    """Closed outline of a w x h board with 45 degree corner cuts c."""
    return [(c, 0), (w - c, 0), (w, c), (w, h - c), (w - c, h), (c, h),
            (0, h - c), (0, c)]


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (W, H),
    "outline": _chamfered(W, H, 1.2),
    "origin": (14.0, TOP_Y),
    "pads": _pads(),
    "silk": _common() + _labels(),
    "fab": _common() + _magnet()
    + [("text", p[4], (p[0] + p[2]) / 2, (p[1] + p[3]) / 2, 0.7)
       for p in PASSIVES]
    + [("text", "AS5600", CHIP[0], CHIP[1] - 3.55, 0.6),
       ("text", "magnet axis", CHIP[0], CHIP[1] + 4.6, 0.6)],
}


def _sym_circle(cx, cy, r, n=24):
    import math
    return ("poly", [(round(cx + r * math.cos(2 * math.pi * i / n), 3),
                      round(cy + r * math.sin(2 * math.pi * i / n), 3))
                     for i in range(n + 1)])


SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-10.16, 10.16, 10.16, -10.16),
    "graphics": [
        ("rect", -2.03, 1.27, 2.03, -1.27),
        *[("poly", [(x, 1.27), (x, 1.9)]) for x in (-1.52, -0.51, 0.51, 1.52)],
        *[("poly", [(x, -1.27), (x, -1.9)])
          for x in (-1.52, -0.51, 0.51, 1.52)],
        _sym_circle(0, 0, 3.3),
        ("poly", [(-4.06, 0), (4.06, 0)]),
        ("poly", [(0, -4.06), (0, 4.06)]),
        *[_sym_circle(x, y, 0.9) for x in (-3.3, 3.3) for y in (-7.0, 7.0)],
        ("text", "AS5600", 0, -5.3, 1.0),
        ("text", "magnet over chip", 0, 5.3, 0.8),
    ],
    "pins": [
        ("power_in", "VCC", "1", -12.7, 7.62, 0),
        ("power_in", "GND", "3", -12.7, 5.08, 0),
        ("output", "OUT", "2", -12.7, 0, 0),
        ("input", "DIR", "4", -12.7, -5.08, 0),
        ("input", "GPO", "7", -12.7, -7.62, 0),
        ("bidirectional", "SDA", "6", 12.7, 2.54, 180),
        ("input", "SCL", "5", 12.7, 0, 180),
    ],
}

# 3D model. Both rows carry right-angle headers under the board (owner,
# 2026-10-28); their pins leave horizontally over the nearest edge. Part
# class figures, not measured: SOIC-8 body 1.5mm, chip parts 0.6mm. The
# magnet (6 x 2.5mm diametric, 0.5-3mm air gap per the ams AS5600L
# adapter-board manual) is not part of the model.
MODEL = {
    "mount": "angled",
    "pins": [str(n) for n in range(1, 8)],
    "holes": [(x, y, HOLE_D) for x, y in HOLES],
    "bodies": [("box", CHIP[0] - 2.45, CHIP[1] - 1.95, CHIP[0] + 2.45,
                CHIP[1] + 1.95, 1.5, "chip")]
    + [("box", x0 + 0.4, y0 + 0.4, x1 - 0.4, y1 - 0.4, 0.6,
        "black" if s.startswith("R") else "ceramic")
       for x0, y0, x1, y1, s in PASSIVES],
}
