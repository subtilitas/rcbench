"""INA228 module with two XT90 plugs, board print "BIFRC V1.1", 80 x 48mm.

Source: owner photo 15-current-sensor-top.webp (top view, board in a black
printed frame). The photo is rectified to a top view: the 4 screw centres
are taken as a rectangle, a pinhole camera with the principal point at the
image centre is solved (focal length 3164px, aspect 0.548), and the scale
comes from the 5-hole header (pitch 2.54mm over 4 pitches = 202px, 19.88px/mm
in the rectified image). Owner photo 14-current-sensor-oblique.webp shows the
same board from an angle (straight header, connector shapes). Both owner
photos show this board, not the MATEK one.

Accuracy: header and points near it within about 0.2mm; board outline, screw
holes and solder bars within about 0.6mm (scale error 0.75% over 80mm, plus
the frame edge hiding the board edge). The XT90 bodies stand about 10mm above
the board; their outlines are corrected for that height by a factor 1/1.05
towards the board centre and are within about 1.5mm. Contact positions:
about 0.5mm.

Pads: header L A V G 5 (THT, 2.54mm). The XT90 plugs are soldered onto 3
exposed copper bars; the footprint gives each bar an SMD pad: the plus bar
runs from plug to plug, the minus bars meet at the shunt. GND and 5V are
round test pads at the top edge. The 4 screw holes are non-plated 3.2mm
holes in the footprint.

Unknown: the board edge under the frame lip (the visible board is
79.9 x 48.3mm; the frame may cover up to about 1mm); corner radius (about
2.4mm, photo); screw hole diameter (M3 screws fitted,
heads 5.3mm; 3.2mm drawn); which side the board expects the battery (left
side named BAT after the configurator drawing); pin 1 marking under the
header plastic; copper extent under the solder. The board print warns that
the screw holes carry circuitry ("do not short").
"""

import math

NAME = "INA228_BIFRC_XT90_80x48mm"
DESC = ("INA228 current and voltage monitor with 2 XT90 plugs, board print "
        "BIFRC V1.1, 80 x 48mm, 4 screw holes 70.4 x 38.6mm apart. "
        "Shunt (print 0m50) in the minus line between BAT- and ESC-; the plus "
        "bar runs straight through. Header print L A V G 5: 1 L = SCL, "
        "2 A = SDA, 3 V = 3.3V chip supply, 4 G = GND, 5 = hole without pin. "
        "Pads 6 + bar, 7 BAT-, 8 ESC-, 9 GND, 10 5V test pads. "
        "The screw holes carry circuitry (board print). Drawn from an owner "
        "photo; outline within about 0.6mm, plugs within about 1.5mm.")

BW, BH = 79.9, 48.3
HDR_Y = 44.11
HDR_X = [56.08 + 2.54 * i for i in range(5)]
HOLES = [(4.53, 4.93), (74.95, 4.93), (4.53, 43.51), (74.95, 43.51)]

PLUS_BAR = (22.6, 15.7, 57.1, 21.8)
BATN_BAR = (22.6, 26.6, 31.9, 32.3)
ESCN_BAR = (47.8, 26.6, 57.1, 32.3)
SHUNT = (31.9, 25.8, 47.8, 33.9)
CONTACT_Y = (18.67, 29.91)


def _xt90(contacts=True):
    """Both plugs, top view, height-corrected outlines.

    The solder cups go on fab only, where they may cover the bar pads.
    """
    items = [
        # left plug: front body and narrower rear holding the solder cups
        ("rect", -6.15, 13.61, 15.07, 35.17),
        ("rect", 15.07, 14.39, 23.45, 34.48),
        ("rect", 2.0, 17.0, 9.0, 31.8),
        # right plug: rear, flange, front
        ("rect", 56.28, 14.23, 61.30, 34.59),
        ("rect", 61.30, 13.61, 69.84, 35.06),
        ("rect", 69.84, 14.47, 80.86, 33.15),
    ]
    for y in CONTACT_Y if contacts else ():
        items += [("rect", 14.83, y - 2.25, 23.40, y + 2.25),
                  ("rect", 57.10, y - 2.25, 61.75, y + 2.25)]
    return items


def _slots():
    """Milled slots through the board, drawn as their centre lines."""
    items = []
    rows = ((24.04, 12.47, 12), (17.86, 37.88, 6), (49.4, 34.71, 3))
    for x0, y, n in rows:
        for i in range(n):
            x = x0 + 2.95 * i
            items.append(("line", x - 0.8, y + 0.7, x + 0.8, y - 0.7))
    for x in (25.9, 29.4, 50.05, 53.6):
        items.append(("line", x - 0.8, 25.3, x + 0.8, 25.3))
    return items


def _chip():
    items = [("rect", 38.98, 42.0, 42.0, 45.8), ("circle", 39.5, 45.3, 0.2)]
    for i in range(5):
        x = 39.13 + 0.7 * i
        items += [("line", x, 41.15, x, 42.0), ("line", x, 45.8, x, 46.68)]
    return items


def _drawing(fab=True):
    items = _xt90(fab)
    if fab:
        items += [("rect",) + SHUNT, ("rect", 37.2, SHUNT[1], 42.5, SHUNT[3])]
    else:
        items += [("line", SHUNT[0] + 0.3, SHUNT[1], SHUNT[2] - 0.3, SHUNT[1]),
                  ("line", SHUNT[0] + 0.3, SHUNT[3], SHUNT[2] - 0.3, SHUNT[3])]
    items += _chip()
    items += [("rect", 54.83, 42.81, 67.55, 45.32)]
    for x, y in HOLES:
        items += [("circle", x, y, 1.6), ("circle", x, y, 2.65)]
    return items


def _bar_pad(num, box):
    x0, y0, x1, y1 = box
    return {"num": num, "x": (x0 + x1) / 2, "y": (y0 + y1) / 2,
            "shape": "rect", "size": (x1 - x0, y1 - y0), "drill": None}


PADS = ([{"num": str(i + 1), "x": x, "y": HDR_Y,
          "shape": "rect" if i == 0 else "circle", "size": (1.7, 1.7),
          "drill": 1.0} for i, x in enumerate(HDR_X)]
        + [_bar_pad("6", PLUS_BAR), _bar_pad("7", BATN_BAR),
           _bar_pad("8", ESCN_BAR),
           {"num": "9", "x": 61.01, "y": 4.17, "shape": "circle",
            "size": (1.8, 1.8), "drill": None},
           {"num": "10", "x": 61.01, "y": 6.74, "shape": "circle",
            "size": (1.8, 1.8), "drill": None}]
        + [{"num": "", "x": x, "y": y, "shape": "circle", "size": (3.2, 3.2),
            "drill": 3.2, "type": "np"} for x, y in HOLES])


def _outline(r=2.4, n=6):
    """Board outline with rounded corners (radius about 2.4mm, photo)."""
    pts = []
    for cx, cy, a0 in ((r, r, 180), (BW - r, r, 270), (BW - r, BH - r, 0),
                       (r, BH - r, 90)):
        pts += [(cx + r * math.cos(math.radians(a0 + 90 * i / n)),
                 cy + r * math.sin(math.radians(a0 + 90 * i / n)))
                for i in range(n + 1)]
    return pts

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (BW, BH),
    "outline": _outline(),
    "origin": (HDR_X[0], HDR_Y),
    "pads": PADS,
    "silk": _drawing(False) + [
        ("text", c, x, 46.9, 1.0)
        for c, x in zip("LAVG5", HDR_X, strict=True)] + [
        ("text", "GND", 58.4, 4.17, 0.9), ("text", "5V", 58.9, 6.74, 0.9),
        ("text", "+", 21.0, 14.4, 1.2), ("text", "-", 21.0, 33.6, 1.2),
        ("text", "+", 58.9, 13.0, 1.2), ("text", "-", 58.9, 35.6, 1.2),
        ("text", "BIFRC V1.1", 22.0, 46.0, 1.5)],
    "fab": _drawing() + _slots() + [
        ("text", "XT90 BAT", 4.5, 24.4, 1.2), ("text", "XT90 ESC", 70.0, 24.4,
                                                 1.2),
        ("text", "SHUNT 0m50", 39.85, 35.0, 0.8),
        ("text", "INA228", 40.5, 40.3, 0.6),
        ("text", "screw holes carry circuitry", 39.95, 2.0, 0.8)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-20.32, 10.16, 20.32, -20.32),
    "graphics": [
        # XT90 plugs at both ends, plus bar on top, shunt in the minus bar
        ("rect", -12.7, 8.89, -8.89, -3.81),
        ("rect", 8.89, 8.89, 12.7, -3.81),
        ("text", "XT90", -10.8, 2.54, 0.9, 90),
        ("text", "XT90", 10.8, 2.54, 0.9, 90),
        ("poly", [(-8.89, 6.35), (8.89, 6.35)]),
        ("poly", [(-8.89, -2.54), (-3.81, -2.54)]),
        ("poly", [(3.81, -2.54), (8.89, -2.54)]),
        ("rect", -3.81, -1.52, 3.81, -3.56),
        ("text", "SHUNT", 0, -0.51, 0.9),
        ("rect", -2.54, -8.89, 2.54, -13.97),
        ("text", "INA228", 0, -11.43, 0.8),
        ("text", "BIFRC V1.1", 0, -17.78, 1.0),
    ],
    "pins": [
        ("passive", "+", "6", -22.86, 5.08, 0),
        ("passive", "BAT-", "7", -22.86, -2.54, 0),
        ("power_in", "GND", "9", -22.86, -15.24, 0),
        ("passive", "5V", "10", -22.86, -17.78, 0),
        ("passive", "ESC-", "8", 22.86, -2.54, 180),
        ("input", "L", "1", 22.86, -7.62, 180),
        ("bidirectional", "A", "2", 22.86, -10.16, 180),
        ("power_in", "V", "3", 22.86, -12.7, 180),
        ("power_in", "G", "4", 22.86, -15.24, 180),
        ("passive", "5", "5", 22.86, -17.78, 180),
    ],
}
