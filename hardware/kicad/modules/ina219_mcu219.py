"""INA219 current sensor board, blue, 25.4 x 21.5mm, 4 holes.

6-hole header + 2-pole screw terminal.

The board printed "INA219 DC Current Sensor" with an R100 (0.1Ω) shunt and
the A0/A1 "I2C Address" bridges, as drawn in configurator parts.ts (often
sold as MCU-219 or ROB-002, 8 connections). No owner photo. Measured from
the ProtoSupplies top-view photo INA219-DC-Current-Sensor-Module-Top-1.jpg
(800 x 600px, near-orthographic). The photo is rectified by the 4 mounting
hole centres (profiles across each hole) to a rectangle; the scale comes
from the header, 5 x 2.54mm = 235.1px at the header row, 18.39px/mm on
average. After rectification the header pitch reads 2.541mm and the
terminal pitch 3.50mm. Positions are within about 0.2mm (estimate; one
vendor photo, no board in hand). ProtoSupplies lists the PCB as 25 x 22mm;
the photo gives 25.35 x 21.45mm, drawn 25.4 x 21.5mm. Corners rounded,
radius about 1.5mm (photo).

Results: header row 18.64mm below the top edge, pad 1 (Vcc) 6.69mm from
the left edge. Terminal holes 3.50mm apart at y 2.87mm, x 11.02mm (Vin-)
and 14.52mm (Vin+); the wires enter from the top edge. Terminal block
outline as printed: x 9.15-16.22mm, y 0.15-6.35mm. Mounting holes at
(2.53, 2.10), (22.79, 2.10), (2.53, 18.89), (22.79, 18.89)mm, plated,
opening about 2.3mm, ring about 3.2mm; drawn on F.Fab only. Shunt 2512
below the terminal, INA219 (SOT-23-8) centre at (12.68, 12.95)mm.

Header Vin- and Vin+ are the same nets as the terminal (board print and
seller text); here they are separate pads 5/7 and 6/8.

Unknown: board thickness, terminal drill (holes about 0.9mm open in the
photo with the pins out; drawn 1.1mm), terminal block depth beyond the
print, whether the mounting holes connect to a net.
"""

NAME = "INA219_MCU-219_25.4x21.5mm"
DESC = ("INA219 current sensor board (INA219 DC Current Sensor, MCU-219 / "
        "ROB-002) 25.4 x 21.5mm, shunt 0.1Ω (R100). Pads 1-6 = header Vcc "
        "Gnd Scl Sda Vin- Vin+, 2.54mm, pad 1 square; pads 7-8 = screw "
        "terminal Vin- Vin+, 3.50mm, 15.77mm above the header row, wires in "
        "from the board edge away from the header. 4 mounting holes about "
        "2.3mm. Scaled from a vendor photo, about 0.2mm; check with a "
        "caliper.")

W, H = 25.4, 21.5
HDR_X0, HDR_Y = 6.69, 18.64
NAMES = ("Vcc", "Gnd", "Scl", "Sda", "Vin-", "Vin+")
TERM_Y = 2.87
TERM = (("7", "Vin-", 11.02), ("8", "Vin+", 14.52))
HOLES = ((2.53, 2.10), (22.79, 2.10), (2.53, 18.89), (22.79, 18.89))
CHIP = (12.68, 12.95)


def _pads():
    pads = [{"num": str(i + 1), "x": HDR_X0 + 2.54 * i, "y": HDR_Y,
             "shape": "rect" if i == 0 else "circle", "size": (1.7, 1.7),
             "drill": 1.0} for i in range(6)]
    pads += [{"num": n, "x": x, "y": TERM_Y, "shape": "circle",
              "size": (2.0, 2.0), "drill": 1.1} for n, _s, x in TERM]
    return pads


def _chip():
    cx, cy = CHIP
    items = [("rect", cx - 1.45, cy - 0.8, cx + 1.45, cy + 0.8)]
    for dx in (-0.975, -0.325, 0.325, 0.975):
        items += [("rect", cx + dx - 0.15, cy - 1.7, cx + dx + 0.15, cy - 0.8),
                  ("rect", cx + dx - 0.15, cy + 0.8, cx + dx + 0.15, cy + 1.7)]
    return items


def _parts():
    items = [("rect", 9.15, 0.15, 16.22, 6.35),
             ("line", 9.15, 0.85, 16.22, 0.85),
             ("rect", 8.83, 6.55, 16.22, 9.6),
             ("line", 9.9, 6.55, 9.9, 9.6), ("line", 15.15, 6.55, 15.15, 9.6)]
    for x0, x1 in ((3.03, 5.28), (5.48, 7.48), (7.78, 9.78)):
        items.append(("rect", x0, 11.15, x1, 15.1))
    for y0, y1 in ((11.15, 13.3), (13.8, 15.85)):
        items += [("rect", 16.22, y0, 20.4, y1),
                  ("rect", 20.53, y0, 23.73, y1),
                  ("rect", 20.85, y0 + 0.35, 21.95, y1 - 0.35),
                  ("rect", 22.3, y0 + 0.35, 23.4, y1 - 0.35)]
    items += [("circle", x, y, 1.15) for x, y in HOLES]
    items += [("circle", x, y, 1.6) for x, y in HOLES]
    return items + _chip()


def _labels():
    items = []
    for i, n in enumerate(NAMES):
        items.append(("text", n, HDR_X0 + 2.54 * i,
                      20.35 if i % 2 == 0 else 16.85, 0.9))
    items += [("text", "Vin-", 6.8, 0.75, 0.9),
              ("text", "Vin+", 18.3, 0.75, 0.9),
              ("text", "INA219 DC", 4.5, 5.6, 1.0),
              ("text", "Current", 4.5, 7.35, 1.0),
              ("text", "Sensor", 4.5, 9.05, 1.0),
              ("text", "I2C Address", 20.3, 10.5, 0.8),
              ("text", "A1", 24.4, 12.2, 0.8, 90),
              ("text", "A0", 24.4, 14.8, 0.8, 90)]
    return items


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
    "outline": _rounded(W, H, 1.5),
    "origin": (HDR_X0, HDR_Y),
    "pads": _pads(),
    "silk": _parts() + _labels(),
    "fab": _parts()
    + [("circle", x, TERM_Y, 1.3) for _n, _s, x in TERM]
    + [("text", "R100", 12.5, 8.1, 0.8), ("text", "INA219", 12.68, 15.4, 0.6),
       ("text", "R5", 15.3, 12.95, 0.6),
       ("line", 12.77, -0.8, 12.77, 0.6), ("line", 12.77, 0.6, 12.37, 0.1),
       ("line", 12.77, 0.6, 13.17, 0.1)],
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
    "body": (-12.7, 10.16, 12.7, -10.16),
    "graphics": [
        ("rect", -3.81, 8.89, 3.81, 3.81),
        _sym_circle(-1.78, 6.35, 0.76),
        _sym_circle(1.78, 6.35, 0.76),
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


# 3D model. Screw terminal 8.8mm tall: a KF350-3.5-2P (LCSC C474892,
# height above board 8.8mm); the terminal on the board is not
# identified. Part class figures, not measured: 2512 shunt 0.6mm,
# SOT-23-8 1.1mm, chip parts 0.6mm. The terminal holes take the
# terminal's pins, not header pins.
MODEL = {
    "mount": "header",
    "pins": ["1", "2", "3", "4", "5", "6"],
    "holes": [(x, y, 2.3) for x, y in HOLES],
    "bodies": [
        ("box", 9.15, 0.15, 16.22, 6.35, 8.8, "termblue"),
        ("box", 9.9, 6.55, 15.15, 9.6, 0.6, "black"),
        ("box", 8.83, 6.55, 9.9, 9.6, 0.6, "metal"),
        ("box", 15.15, 6.55, 16.22, 9.6, 0.6, "metal"),
        ("box", CHIP[0] - 1.45, CHIP[1] - 0.8, CHIP[0] + 1.45,
         CHIP[1] + 0.8, 1.1, "chip"),
    ]
    + [("box", x0, 11.6, x1, 14.65, 0.6, "ceramic")
       for x0, x1 in ((3.03, 5.28), (5.48, 7.48), (7.78, 9.78))]
    + [("box", 20.85, y0 + 0.35, 23.4, y1 - 0.35, 0.6, "ceramic")
       for y0, y1 in ((11.15, 13.3), (13.8, 15.85))],
}
