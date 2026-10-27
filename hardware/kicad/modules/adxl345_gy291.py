"""ADXL345 accelerometer board GY-291, 14.8 x 20.6mm, 8-hole header, 2 holes.

No owner photo. Measured from the Core Electronics product photo
018-gy-291.jpg (700 x 700px, oblique). The photo is rectified by a fit:
the image-to-board homography that makes the 8 pad rings and the 2
mounting holes circles, with the pad centres 2.54mm apart on one line
(outline residual 0.07mm rms, 0.4mm max). The ADXL345 package comes out
3.0mm wide (datasheet 3 x 5mm), which checks the result. A rectification
by the 4 board corners alone gives a width 20% too large; the corners of
an oblique photo are not sharp enough for it. Positions are within about
0.3mm (estimate; one vendor photo, no board in hand). The Makerlab photo
of the same board was tried and gives no stable fit.

Results: outline 14.8 x 20.6mm (listings: 21 x 15 x 3mm elecena, 18.5 x
14.5mm eleparts, 27 x 17mm and 28 x 16mm elsewhere; the last two do not
fit the 8-hole row). Header row 1.43mm from the left edge, pad 1 (GND)
1.24mm below the top edge. Mounting holes about 3.3mm at x 12.2mm, y 2.50
and 17.99mm (15.49mm apart, 10.77mm from the header row), drawn on F.Fab
only. ADXL345 (LGA 3 x 5mm) at x 7.0-10.0mm, y 7.0-12.0mm; the axis print
below it: +Y to the right (away from the header), +X down (towards SCL), Z
out of the board. 3.3V regulator (SOT-23-5) at the top, tantalum cap
right of the ADXL345.

Unknown: board thickness, the exact hole drill, the pin 1 corner of the
ADXL345 (not readable in the photo).
"""

NAME = "ADXL345_GY-291_14.8x20.6mm"
DESC = ("ADXL345 3-axis accelerometer board GY-291, 14.8 x 20.6mm. Pads 1-8: "
        "GND VCC CS INT1 INT2 SDO SDA SCL, 2.54mm, pad 1 square. 2 mounting "
        "holes about 3.3mm, 10.77mm from the header row, 1.26mm and 16.75mm "
        "along it from pad 1. Axis print: +Y away from the header, +X "
        "towards pad 8. Scaled from a vendor photo, about 0.3mm; check with "
        "a caliper.")

W, H = 14.8, 20.6
NAMES = ("GND", "VCC", "CS", "INT1", "INT2", "SDO", "SDA", "SCL")
PAD_X, PAD_Y0 = 1.43, 1.24
HOLES = ((12.2, 2.50), (12.2, 17.99))
HOLE_D = 3.3
CHIP = (7.0, 7.0, 10.0, 12.0)
AXIS = (9.25, 15.45)


def _pads():
    return [{"num": str(i + 1), "x": PAD_X, "y": PAD_Y0 + 2.54 * i,
             "shape": "rect" if i == 0 else "circle", "size": (1.7, 1.7),
             "drill": 1.0} for i in range(8)]


def _parts():
    items = [("rect",) + CHIP]
    # LDO SOT-23-5, leads 3 left, 2 right
    items += [("rect", 7.0, 0.25, 8.6, 3.15)]
    items += [("rect", 6.4, y - 0.2, 7.0, y + 0.2) for y in (0.75, 1.7, 2.65)]
    items += [("rect", 8.6, y - 0.2, 9.2, y + 0.2) for y in (0.75, 2.65)]
    # chip caps and resistors (silk pads as printed)
    for box in ((5.75, 3.6, 8.0, 4.9), (5.75, 5.4, 8.0, 6.6),
                (10.0, 5.6, 12.25, 6.9), (10.0, 13.1, 12.25, 14.25),
                (5.75, 13.5, 8.25, 14.4), (5.75, 15.1, 8.25, 16.0),
                (5.75, 16.5, 8.25, 17.5)):
        items.append(("rect",) + box)
    # tantalum cap, long axis along y
    items += [("rect", 10.95, 7.4, 12.85, 11.4),
              ("line", 10.95, 7.9, 12.85, 7.9)]
    items += [("circle", x, y, HOLE_D / 2) for x, y in HOLES]
    return items


def _axes():
    x, y = AXIS
    return [("line", x, y, 11.0, y), ("line", 11.0, y, 10.5, y - 0.4),
            ("line", 11.0, y, 10.5, y + 0.4),
            ("line", x, y, x, 17.75), ("line", x, 17.75, x - 0.4, 17.25),
            ("line", x, 17.75, x + 0.4, 17.25),
            ("circle", x, y, 0.3),
            ("text", "Y", 11.9, y, 0.8), ("text", "X", x, 18.75, 0.8),
            ("text", "Z", x - 0.5, 14.3, 0.8)]


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (W, H),
    "origin": (PAD_X, PAD_Y0),
    "pads": _pads(),
    "silk": _parts() + _axes()
    + [("text", n, 4.1, PAD_Y0 + 2.54 * i, 0.8) for i, n in enumerate(NAMES)]
    + [("text", "ADXL345", 13.75, 9.8, 0.9, 90)],
    "fab": _parts() + _axes(),
}


def _sym_circle(cx, cy, r, n=20):
    import math
    return ("poly", [(round(cx + r * math.cos(2 * math.pi * i / n), 3),
                      round(cy + r * math.sin(2 * math.pi * i / n), 3))
                     for i in range(n + 1)])


SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-7.62, 10.16, 7.62, -12.7),
    "graphics": [
        ("rect", -0.76, 3.81, 2.29, -1.27),
        ("text", "ADXL345", 0.76, -2.54, 0.8),
        ("poly", [(0.25, -5.84), (3.3, -5.84)]),
        ("poly", [(2.79, -5.33), (3.3, -5.84), (2.79, -6.35)]),
        ("poly", [(0.25, -5.84), (0.25, -8.89)]),
        ("poly", [(-0.25, -8.38), (0.25, -8.89), (0.76, -8.38)]),
        ("text", "Y", 4.06, -5.84, 0.9),
        ("text", "X", 0.25, -9.91, 0.9),
        _sym_circle(4.57, 7.62, 0.89),
        _sym_circle(4.57, -10.16, 0.89),
    ],
    "pins": [
        ("power_in", "GND", "1", -10.16, 7.62, 0),
        ("power_in", "VCC", "2", -10.16, 5.08, 0),
        ("input", "CS", "3", -10.16, 2.54, 0),
        ("output", "INT1", "4", -10.16, 0, 0),
        ("output", "INT2", "5", -10.16, -2.54, 0),
        ("bidirectional", "SDO", "6", -10.16, -5.08, 0),
        ("bidirectional", "SDA", "7", -10.16, -7.62, 0),
        ("input", "SCL", "8", -10.16, -10.16, 0),
    ],
}

# 3D model. Heights are part class figures, not measured: ADXL345 (LGA)
# 1.0mm, SOT-23-5 1.1mm, chip parts 0.5mm, tantalum capacitor 1.6mm. Board
# colour not stated; drawn green.
MODEL = {
    "mount": "header",
    "pins": [str(n) for n in range(1, 9)],
    "holes": [(x, y, HOLE_D) for x, y in HOLES],
    "bodies": [("box",) + CHIP + (1.0, "chip"),
               ("box", 7.0, 0.25, 8.6, 3.15, 1.1, "chip"),
               ("box", 10.95, 7.4, 12.85, 11.4, 1.6, "yellow")]
    + [("box", x0 + 0.25, y0 + 0.15, x1 - 0.25, y1 - 0.15, 0.5, "ceramic")
       for x0, y0, x1, y1 in ((5.75, 3.6, 8.0, 4.9), (5.75, 5.4, 8.0, 6.6),
                              (10.0, 5.6, 12.25, 6.9),
                              (10.0, 13.1, 12.25, 14.25),
                              (5.75, 13.5, 8.25, 14.4),
                              (5.75, 15.1, 8.25, 16.0),
                              (5.75, 16.5, 8.25, 17.5))],
}
