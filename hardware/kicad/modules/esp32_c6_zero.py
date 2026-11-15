# SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""Waveshare ESP32-C6-Zero, 18.0 x 23.5mm, 18 edge pads and 7 under it.

A microcontroller board with a USB Type-C socket at one short edge and
a ceramic antenna at the other. Each long edge carries 9 pads at
2.54mm, each with a hole for a header pin and a half hole in the edge
for soldering the board flat. 7 more pads lie on the underside.

Sources:
    D: the maker's dimension drawing ESP32-C6-Zero-Diagram-2 and pin
    drawing ESP32-C6-Zero_Pin (waveshare.com/wiki/ESP32-C6-Zero; not
    in the repository): board 18.00 x 23.50mm, corner radius 1.00mm;
    hole centres 1.38mm from the long edges, the first 1.59mm from the
    USB edge, 2.54mm apart; underside pads 3.63mm from one long edge,
    1.60mm apart, the last 5.15mm from the antenna edge.
    P: two owner photos on 5mm squared paper (top side, underside; not
    in the repository). They agree with D within 0.2mm and give the
    side the underside pads lie on and the names printed at the pads.

Seen from the top side with the USB socket up, the left pads are, from
the socket, 5V, GND, 3V3, 0, 1, 2, 3, 4, 5 and the right pads TX, RX,
14, 15, 18, 19, 20, 21, 22. The underside pads lie under the right
half, 3.63mm from the right edge; from the antenna they are 6, 7, 8,
9, 23, 12, 13. That order is D's; P shows 23 at the fifth pad and does
not resolve the other names.

Pads 1-9 are the left edge from the socket, 10-18 the right edge from
the antenna, 19-25 the underside pads from the antenna. The footprint
is for the board soldered flat: the edge pads are plated holes of
1.0mm, which also take header pins, and the underside pads are pads
on the carrier's top side.

The socket's shell stands 1.2mm past the board's USB edge and 3.25mm
over the board. Unknown: the drill of the holes (drawn 1.0mm), the
size of the underside pads (1.0 x 1.2mm in P).

The 3D model is hardware/mechanical/esp32_c6_zero.py.
"""

NAME = "ESP32-C6-Zero_Waveshare_18x23.5mm"
DESC = ("Waveshare ESP32-C6-Zero, 18.0 x 23.5mm. Pads 1-9 = left edge "
        "from the USB socket: 5V GND 3V3 GP0 GP1 GP2 GP3 GP4 GP5; pads "
        "10-18 = right edge from the antenna: GP22 GP21 GP20 GP19 GP18 "
        "GP15 GP14 RX TX; 2.54mm, rows 15.24mm apart. Pads 19-25 = "
        "underside pads from the antenna: GP6 GP7 GP8 GP9 GP23 GP12 "
        "GP13, 1.6mm, 3.63mm from the right edge. USB socket shell "
        "1.2mm past the edge. From the maker's drawing.")

W, H = 18.0, 23.5
RADIUS = 1.0
INSET, FIRST = 1.38, 1.59
LEFT = ("5V", "GND", "3V3", "GP0", "GP1", "GP2", "GP3", "GP4", "GP5")
RIGHT = ("GP22", "GP21", "GP20", "GP19", "GP18", "GP15", "GP14", "RX",
         "TX")
UNDER = ("GP6", "GP7", "GP8", "GP9", "GP23", "GP12", "GP13")
UNDER_X, UNDER_Y0, UNDER_PITCH = W - 3.63, H - 5.15, 1.60
UNDER_PAD = (1.0, 1.2)
USB = (4.2, -1.2, 13.8, 6.15)


def _pads():
    pads = [{"num": str(i + 1), "x": INSET, "y": FIRST + 2.54 * i,
             "shape": "rect" if i == 0 else "oval", "size": (2.4, 1.6),
             "drill": 1.0} for i in range(9)]
    pads += [{"num": str(i + 10), "x": W - INSET,
              "y": FIRST + 2.54 * (8 - i), "shape": "oval",
              "size": (2.4, 1.6), "drill": 1.0} for i in range(9)]
    pads += [{"num": str(i + 19), "x": UNDER_X,
              "y": UNDER_Y0 - UNDER_PITCH * i, "shape": "rect",
              "size": UNDER_PAD} for i in range(7)]
    return pads


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


_PARTS = [("rect",) + USB, ("rect", 5.25, 20.9, 12.7, 22.9)]

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (W, H),
    "outline": _rounded(W, H, RADIUS),
    "origin": (INSET, FIRST),
    "pads": _pads(),
    "silk": _PARTS + [("text", "USB", 9.0, 3.0, 0.9)],
    "fab": _PARTS + [("text", "antenna", 9.0, 19.9, 0.7),
                     ("rect", 5.4, 11.3, 10.4, 16.3)],
}


def _pins():
    kinds = {"5V": "power_in", "GND": "power_in", "3V3": "power_out"}
    pins = [(kinds.get(n, "bidirectional"), n, str(i + 1), -15.24,
             20.32 - 2.54 * i, 0) for i, n in enumerate(LEFT)]
    names = RIGHT[::-1] + UNDER
    nums = [str(18 - i) for i in range(9)] + [str(19 + i) for i in range(7)]
    pins += [("bidirectional", n, num, 15.24, 20.32 - 2.54 * i, 180)
             for i, (n, num) in enumerate(zip(names, nums, strict=True))]
    return pins


SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-12.7, 22.86, 12.7, -20.32),
    "graphics": [("text", "ESP32-C6", 0, 2.54, 1.27),
                 ("text", "Zero", 0, 0, 1.27)],
    "pins": _pins(),
}

MODEL = {"detail": "esp32_c6_zero"}
