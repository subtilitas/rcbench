"""Lithium polymer charger board with a 5V boost output, 15.2 x 24mm.

Black board, underside print "Charge & DisCharge". 6 holes at 2.54mm
along one short edge; the underside names them VIN, GND, GND, BAT, GND
and OUT 5V. Top side: an 8-lead chip, an inductor printed 1R0, 4 LEDs
at the far edge and a push button whose plunger stands 1.5mm out of
one long edge.

Measured from two owner photos (top side, underside; not in the
repository) of the board lying on 5mm squared paper. The paper's grid
gives the scale; hardware/mechanical/_photo_grid.py fits it within
0.20mm. The row's 5 x 2.54mm reads 12.75mm in that scale, so sizes read
off the board's top are divided by 1.005. Positions are within about
0.2mm.

Results: board 15.2 x 24.0mm, square corners. Hole row 1.3mm below
the upper edge, hole 1 1.2mm from the left edge. Pads 1.5 x 2.3mm,
holes about 0.9mm. Seen from the top side with the row up, the holes
are, left to right, OUT 5V, GND, BAT, GND, GND and VIN; OUT 5V, BAT
and VIN have rectangular pads.

Unknown: the chip's type, the charge current, the output's current
limit, what the button and the LEDs do. The board's thickness is not
measured; the model draws 1.6mm.

The 3D model is hardware/mechanical/lipo_charger_boost_5v.py.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

NAME = "LiPo_Charger_Boost5V_15.2x24mm"
DESC = ("Lithium polymer charger board with 5V boost output (print "
        "Charge & DisCharge) 15.2 x 24mm. Pads 1-6, 2.54mm, along one "
        "short edge, seen from the top side: 1 OUT 5V, 2 GND, 3 BAT, "
        "4 GND, 5 GND, 6 VIN. Push button at the right long edge, "
        "plunger 1.5mm past the edge, 20.4mm below the pad row; 4 LEDs "
        "at the far short edge. Measured from owner photos on squared "
        "paper, about 0.2mm.")

W, H = 15.2, 24.0
ROW_X0, ROW_Y = 1.2, 1.3
NAMES = ("OUT 5V", "GND", "BAT", "GND", "GND", "VIN")
SQUARE = (0, 2, 5)
PAD, DRILL = (1.5, 2.3), 0.9
BUTTON = (13.15, 17.75, 15.5, 22.3)
PLUNGER = (15.5, 19.2, 16.7, 20.8)
INDUCTOR = (2.85, 4.0, 9.55, 10.7)
CHIP = (5.05, 14.4, 9.95, 18.3)

_PARTS = [("rect",) + INDUCTOR, ("rect",) + CHIP, ("rect",) + BUTTON,
          ("rect",) + PLUNGER]

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (W, H),
    "origin": (ROW_X0, ROW_Y),
    "pads": [{"num": str(i + 1), "x": ROW_X0 + 2.54 * i, "y": ROW_Y,
              "shape": "rect" if i in SQUARE else "oval", "size": PAD,
              "drill": DRILL} for i in range(6)],
    "silk": _PARTS + [("text", n, ROW_X0 + 2.54 * i, 3.6, 0.6, 90)
                      for i, n in enumerate(("5V", "GND", "BAT", "GND",
                                             "GND", "VIN"))],
    "fab": _PARTS + [("text", "1R0", 6.2, 7.35, 0.9),
                     ("text", "button", 12.4, 20.0, 0.6, 90)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-10.16, 7.62, 10.16, -7.62),
    "graphics": [
        ("text", "LiPo charger", 0, 1.27, 1.0),
        ("text", "5V boost", 0, -1.27, 1.0),
    ],
    "pins": [
        ("power_in", "VIN", "6", -12.7, 5.08, 0),
        ("passive", "BAT", "3", -12.7, 0.0, 0),
        ("power_in", "GND", "2", -12.7, -5.08, 0),
        ("power_out", "OUT 5V", "1", 12.7, 5.08, 180),
        ("passive", "GND", "4", 12.7, -2.54, 180),
        ("passive", "GND", "5", 12.7, -5.08, 180),
    ],
}

MODEL = {"detail": "lipo_charger_boost_5v"}
