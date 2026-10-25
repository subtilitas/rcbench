"""ADS1115 16-bit ADC board, blue, 28.0 x 17.0mm, 10-hole header, 2 holes.

The common board printed "16Bit I2C ADC+PGA ADS1115" (often sold as
GY-ADS1115). No owner photo. Measured from the components101.com top-view
product photo ADS1115-Module.jpg (750 x 500px, near-orthographic); the
board size 28 x 17mm is the dimension drawing ADS1115-Dimensions.jpg on the
same page. Scale from the header: 9 x 2.54mm = 605.7px, so 26.5px/mm. The
photo then gives the outline as 28.0 x 16.9mm, matching the drawing.
Positions are within about 0.2mm (estimate; one vendor photo, no board in
hand). Corners rounded, radius about 1.0mm (photo).

Results: 10 holes at 2.54mm, row centred on the board width (within
0.1mm), 15.1mm below the top edge (1.9mm above the bottom edge). Two
mounting holes at the top corners, 2.50mm below the top edge, on the same
x as header holes 1 and 10 (22.87mm apart in the photo, 22.86mm on the
grid). Mounting hole: white opening about 2.4mm, plated ring about 3.0mm;
drawn 2.5mm on F.Fab only. Chip: ADS1115 in VSSOP-10 (3 x 3mm body), centre
at (21.98, 7.34)mm, leads left and right, pin 1 at the top left (dot).
Above pads 1-6 sit 6 vertical 0805 parts (caps on VDD/GND, 10kΩ "103"
pull-ups on SCL SDA ADDR ALRT); one horizontal cap below the first two, one
at the top right.

Unknown: board thickness, the drill of the mounting holes (photo only),
whether this matches every seller's board; some versions have 4 holes.
"""

NAME = "ADS1115_Module_28x17mm"
DESC = ("ADS1115 16-bit 4-channel ADC board (16Bit I2C ADC+PGA, GY-ADS1115 "
        "style) 28.0 x 17.0mm. Pads 1-10: VDD GND SCL SDA ADDR ALRT A0 A1 "
        "A2 A3, 2.54mm, pad 1 square. 2 mounting holes about 2.5mm at the "
        "top corners above pads 1 and 10, 12.6mm from the header row. "
        "Scaled from a vendor photo, about 0.2mm; check with a caliper.")

W, H = 28.0, 17.0
NAMES = ("VDD", "GND", "SCL", "SDA", "ADDR", "ALRT", "A0", "A1", "A2", "A3")
PAD_Y = 15.1
PAD_X = [W / 2 + (i - 4.5) * 2.54 for i in range(10)]
HOLES = ((PAD_X[0], 2.5), (PAD_X[9], 2.5))
HOLE_D = 2.5
CHIP = (21.98, 7.34)


def _pads():
    return [{"num": str(i + 1), "x": x, "y": PAD_Y,
             "shape": "rect" if i == 0 else "circle", "size": (1.7, 1.7),
             "drill": 1.0} for i, x in enumerate(PAD_X)]


def _chip():
    """VSSOP-10 top view, leads left and right, pin 1 top left."""
    cx, cy = CHIP
    items = [("rect", cx - 1.5, cy - 1.5, cx + 1.5, cy + 1.5)]
    for k in range(5):
        y = cy - 1.0 + 0.5 * k
        items += [("rect", cx - 2.45, y - 0.13, cx - 1.5, y + 0.13),
                  ("rect", cx + 1.5, y - 0.13, cx + 2.45, y + 0.13)]
    items.append(("circle", cx - 1.05, cy - 1.05, 0.2))
    return items


def _parts():
    items = []
    for x in PAD_X[:6]:
        items += [("rect", x - 1.0, 4.3, x + 1.0, 8.8),
                  ("rect", x - 0.62, 5.55, x + 0.62, 7.55)]
    items += [("rect", 1.66, 8.98, 6.45, 11.02),
              ("rect", 3.06, 9.38, 5.06, 10.62),
              ("rect", 19.66, 3.70, 24.45, 5.70),
              ("rect", 21.06, 4.08, 23.06, 5.32)]
    items += [("circle", x, y, HOLE_D / 2) for x, y in HOLES]
    items += [("circle", x, y, 1.5) for x, y in HOLES]
    return items + _chip()


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
    "outline": _rounded(W, H, 1.0),
    "origin": (PAD_X[0], PAD_Y),
    "pads": _pads(),
    "silk": _parts()
    + [("text", n, x, 12.2, 0.7, 90)
       for n, x in zip(NAMES, PAD_X, strict=True)]
    + [("text", "16Bit I2C ADC+PGA", 14.0, 0.95, 1.0),
       ("text", "ADS1115", 14.0, 2.6, 0.9)],
    "fab": _parts() + [("text", "ADS1115", CHIP[0], CHIP[1] + 2.3, 0.6)],
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
    "body": (-10.16, 7.62, 10.16, -7.62),
    "graphics": [
        ("rect", 1.27, 2.79, 4.32, -0.25),
        *[("poly", [(0.76, y), (1.27, y)]) for y in (2.29, 1.27, 0.25)],
        *[("poly", [(4.32, y), (4.83, y)]) for y in (2.29, 1.27, 0.25)],
        ("text", "ADS1115", 2.79, -1.52, 0.9),
        ("text", "16 bit ADC", 2.79, -4.83, 1.0),
        _sym_circle(-2.54, 5.84, 0.76),
        _sym_circle(2.54, 5.84, 0.76),
    ],
    "pins": [
        ("power_in", "VDD", "1", -12.7, 5.08, 0),
        ("power_in", "GND", "2", -12.7, 2.54, 0),
        ("input", "SCL", "3", -12.7, 0, 0),
        ("bidirectional", "SDA", "4", -12.7, -2.54, 0),
        ("input", "ADDR", "5", -12.7, -5.08, 0),
        ("open_collector", "ALRT", "6", 12.7, 5.08, 180),
        ("input", "A0", "7", 12.7, 2.54, 180),
        ("input", "A1", "8", 12.7, 0, 180),
        ("input", "A2", "9", 12.7, -2.54, 180),
        ("input", "A3", "10", 12.7, -5.08, 180),
    ],
}
