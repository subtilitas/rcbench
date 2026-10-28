"""WeAct Studio PD Power Mini V1 Buck, 61 x 40mm, USB-C PD input.

The board carries a 0.96 inch display, 4 buttons, a 3.81mm 4-pole pluggable
output terminal and a 2 x 4 header.

Sources: WeAct's board shape drawing "WeAct-PDPowerMiniV1-Buck Board Shape"
(Altium PDF, github.com/WeActStudio/WeActStudio.PDPowerMiniV1-Buck, folder
Hardware). Page 1 is vector data: outline, holes, pads and print are read
from its paths, scale from the outline (61.00 x 40.00mm, 12.969pt/mm across,
12.957pt/mm down). Page 2 (a rendered top view at 300dpi, 56.82px/mm across
and 56.91px/mm down, fitted to 3 holes of page 1) gives the bodies of the
USB-C socket, buttons, display, inductor and terminal.
Owner photo 02-pd-mini.webp shows the same V1.0 board with the terminal plug
fitted, which sticks out past the right edge.

Accuracy: holes, pads and outline from the vector page within 0.02mm of the
drawing. Component bodies from the render within about 0.2mm. The plug's
extent past the board edge is estimated from the owner photo (about 8.5mm,
within about 2mm). The drawing against a real board: not checked.

Pads: terminal pads 1-4 top to bottom (module pads 2.2mm square, 1.2mm
drill, 3.81mm pitch); 2 x 4 header pads 5-12 (module pads 1.52mm, 0.81mm
drill; drawn 1.7mm / 1.0mm for 2.54mm header pins), column x 48.30mm = 5V,
column x 50.84mm = G, rows 2.54mm apart. Terminal poles: minus, minus, plus,
plus from the top end (owner statement; board print "-" above, "+" below).
The 5V column pads are joined by one trace on the bottom side (render page 3).
The 4 holes (module: plated, 2.1mm drill, 3.4mm ring) are non-plated
2.1mm holes in the footprint.

UART: the module has no UART pads. Its UART uses the USB-C socket's data
lines (UART Mode setting): DP = TX, DM = RX, 3.3V logic (WeAct README;
settings.c). The bench reaches them through the USB-C T-plug.

Unknown: drill size of the 4 USB-C shell slots (the drawing gives the pad
ovals only); board thickness; height of the parts.
"""

import math

NAME = "PD_Buck_WeAct_PDPowerMiniV1_61x40mm"
DESC = ("WeAct Studio PD Power Mini V1 Buck, 61 x 40mm. Input USB-C PD/QC "
        "or DC up to 24V; output 1-20V, 0.05-3A, plus 5V 0.3A on the header "
        "(vendor values). Pads 1-2 OUT-, 3-4 OUT+ (3.81mm pluggable terminal, "
        "top to bottom); pads 5-12 2 x 4 header, odd 5V, even G. UART on the "
        "USB-C data lines, no pad: DP = TX, DM = RX, 3.3V. 4 holes 2.1mm. "
        "Drawn from the WeAct board shape drawing.")

TERM_X = 53.34
TERM_Y = (8.28, 12.09, 15.90, 19.71)
HDR_X = (48.30, 50.84)
HDR_Y = (28.28, 30.82, 33.36, 35.90)
HOLES = ((3.0, 3.0), (48.0, 3.5), (22.0, 22.5), (48.0, 24.5))
BUTTONS = (("S", 21.125), ("+", 26.755), ("-", 31.625), ("ON", 37.245))
BTN_X = 5.285


def _arc(cx, cy, rx, ry, a0, a1, n=6):
    """Points on an elliptic arc, angles in degrees, y down."""
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def _outline():
    """Board outline as the drawing has it.

    Corners 3.0mm; break-off tab notches 0.8mm deep at both short edges.
    """
    r = 3.0
    pts = _arc(r, r, r, r, 180, 270)
    pts += _arc(61 - r, r, r, r, 270, 360)
    pts += [(61.0, 10.17), (60.8, 10.17)]
    pts += _arc(60.8, 11.37, 0.6, 1.2, 270, 180)[1:]
    pts += _arc(60.8, 16.62, 0.6, 1.2, 180, 90)[:-1]
    pts += [(60.8, 17.82), (61.0, 17.82)]
    pts += _arc(61 - r, 40 - r, r, r, 0, 90)
    pts += _arc(r, 40 - r, r, r, 90, 180)
    pts += [(0.0, 15.55), (0.2, 15.55)]
    pts += _arc(0.2, 14.35, 0.6, 1.2, 90, 0)[1:]
    pts += _arc(0.2, 10.85, 0.6, 1.2, 0, -90)[:-1]
    pts += [(0.2, 9.65), (0.0, 9.65)]
    return pts


def _drawing(fab=True):
    items = [
        # USB-C socket, 8.94 x 7.35mm, front 0.16mm past the left edge
        ("rect", -0.16, 8.13, 7.19, 17.07),
        # display 0.96 inch, flex fold at its right end
        ("rect", 13.6, 25.5, 41.8, 39.2), ("line", 39.7, 25.5, 39.7, 39.2),
        # inductor
        ("rect", 29.55, 5.24, 40.15, 16.8),
        # electrolytic capacitors, 6.3mm
        ("circle", 48.08, 9.75, 3.15), ("circle", 48.08, 18.26, 3.15),
        # SMB diode
        ("rect", 53.4, 22.45, 58.0, 26.05),
        # output terminal: header on the board, plug past the edge
        ("rect", 52.2, 6.0, 61.0, 22.07), ("rect", 61.0, 6.0, 69.5, 22.07),
        # 2 x 4 header plastic, right-angle pins to the edge
        ("rect", 52.5, 27.1, 54.85, 37.2),
    ]
    for y in TERM_Y:
        items.append(("circle", 66.0, y, 1.3))   # clamp screws in the plug
    for y in HDR_Y:
        items.append(("line", 54.85, y, 61.3, y))
    for _, y in BUTTONS:
        items += [("rect", BTN_X - 2.13, y - 1.7, BTN_X + 2.13, y + 1.7),
                  ("circle", BTN_X, y, 1.0)]
    for x, y in HOLES:
        items.append(("circle", x, y, 1.7))
    if fab:
        items += [("rect", 7.17, 9.1, 8.33, 16.1)]          # USB-C contacts
        for x in (2.66, 6.84):
            for y in (8.28, 16.92):
                items.append(("rect", x - 0.6, y - 0.6, x + 0.6, y + 0.6))
        for y in (10.85, 11.73, 12.6, 13.48, 14.35):
            items.append(("circle", 0.8, y, 0.25))
        for y in (11.37, 12.25, 13.13, 14.0, 14.88, 15.75, 16.62):
            items.append(("circle", 60.2, y, 0.25))
    return items


def _pads():
    pads = [{"num": str(i + 1), "x": TERM_X, "y": y,
             "shape": "rect" if i == 0 else "circle", "size": (2.2, 2.2),
             "drill": 1.2} for i, y in enumerate(TERM_Y)]
    n = 5
    for y in HDR_Y:
        for x in HDR_X:
            pads.append({"num": str(n), "x": x, "y": y, "shape": "circle",
                         "size": (1.7, 1.7), "drill": 1.0})
            n += 1
    pads += [{"num": "", "x": x, "y": y, "shape": "circle",
              "size": (2.1, 2.1), "drill": 2.1, "type": "np"}
             for x, y in HOLES]
    return pads


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://github.com/WeActStudio/WeActStudio.PDPowerMiniV1-Buck",
    "board": (61.0, 40.0),
    "outline": _outline(),
    "origin": (TERM_X, TERM_Y[0]),
    "pads": _pads(),
    "silk": _drawing(False) + [
        ("text", s, 9.3, y, 1.2) for s, y in BUTTONS] + [
        ("text", "-", 56.4, 3.4, 1.5), ("text", "+", 51.6, 24.2, 1.5),
        ("text", "5V", 48.3, 38.3, 0.9), ("text", "G", 50.84, 38.3, 0.9),
        ("text", "USB-C", 3.3, 6.6, 1.0)],
    "fab": _drawing() + [
        ("text", "DISPLAY 0.96in", 26.65, 32.35, 1.2),
        ("text", "100", 34.8, 11.04, 1.2),
        ("text", "PLUG", 65.25, 24.0, 1.0),
        ("text", "DP=TX DM=RX", 3.3, 18.6, 0.8)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "datasheet": "https://github.com/WeActStudio/WeActStudio.PDPowerMiniV1-Buck",
    "body": (-15.24, 12.7, 12.7, -25.4),
    "graphics": [
        # USB-C socket at the left end
        ("rect", -16.51, 7.62, -11.43, 1.27),
        ("text", "USB-C", -7.62, 4.45, 1.0),
        ("text", "PD in", -12.7, -0.51, 0.9),
        ("text", "UART: DP=TX DM=RX 3.3V", -6.35, -2.79, 0.9),
        # display and buttons
        ("rect", -10.16, -6.35, 0.0, -12.7),
        ("text", "DISPLAY", -5.08, -9.53, 0.9),
        *[g for i, s in enumerate(("S", "+", "-", "ON")) for g in (
            ("rect", -13.97, -5.08 - i * 3.81, -12.19, -6.86 - i * 3.81),
            ("text", s, -11.18, -5.97 - i * 3.81, 0.9))],
        # output terminal
        ("rect", 3.81, 11.43, 5.08, 1.27),
        ("text", "OUT", 2.54, 6.35, 0.9, 90),
        ("rect", 3.81, -3.81, 5.08, -24.13),
        ("text", "AUX 5V", 2.54, -13.97, 0.9, 90),
    ],
    "pins": [
        ("passive", "OUT-", "1", 15.24, 10.16, 180),
        ("passive", "OUT-", "2", 15.24, 7.62, 180),
        ("power_out", "OUT+", "3", 15.24, 5.08, 180),
        ("passive", "OUT+", "4", 15.24, 2.54, 180),
        *[("power_out" if n == 5 else "passive", "5V" if n % 2 else "G",
           str(n), 15.24, -5.08 - 2.54 * (n - 5), 180) for n in range(5, 13)],
    ],
}

# 3D model. Heights from the maker's 3D model
# WeAct-PDPowerMiniV1-Buck_3D.step (WeActStudio GitHub, folder Hardware),
# solid bounding boxes above the board's top side: USB-C receptacle
# 3.25mm, buttons 2.5mm, display glass 1.5-3.5mm, inductor 3.8mm,
# electrolytic capacitors 6.6mm across and 5.41mm tall, terminal 7.2mm,
# 2 x 4 header 5.04mm, diode 2.2mm; board 1.69mm over copper. The plug's
# height past the board edge is taken equal to the terminal's. The header
# pins at 1.27mm and 3.81mm are not in the maker's model. The board is
# wired, so it rests on the carrier.
MODEL = {
    "mount": "flat",
    "bodies": [
        ("box", -0.16, 8.13, 7.19, 17.07, 3.25, "metal"),
        ("box", 13.6, 25.5, 41.8, 39.2, 1.5, "plastic"),
        ("box", 13.6, 25.5, 41.8, 39.2, 2.0, "glass", 1.5),
        ("box", 29.55, 5.24, 40.15, 16.8, 3.8, "chip"),
        ("cyl", 48.08, 9.75, 3.3, 5.41, "metal"),
        ("cyl", 48.08, 18.26, 3.3, 5.41, "metal"),
        ("box", 53.4, 22.45, 58.0, 26.05, 2.2, "chip"),
        ("box", 52.2, 6.0, 61.0, 22.07, 7.2, "termgreen"),
        ("box", 61.0, 6.0, 69.5, 22.07, 7.2, "termgreen"),
        ("box", 52.5, 27.1, 54.85, 37.2, 5.04, "plastic"),
    ]
    + [("box", 54.85, y - 0.32, 61.3, y + 0.32, 0.64, "gold", z)
       for y in HDR_Y for z in (0.95, 3.49)]
    + [b for _, y in BUTTONS for b in (
        ("box", BTN_X - 2.13, y - 1.7, BTN_X + 2.13, y + 1.7, 2.0, "metal"),
        ("cyl", BTN_X, y, 1.0, 0.5, "black", 2.0))],
}
