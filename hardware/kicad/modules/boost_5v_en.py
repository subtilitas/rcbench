"""5V boost converter board with an enable pad, green, 13.7 x 21.7mm.

4 tinned corner pads with holes and one round pad without a hole. The
underside names the corner pads IN+ and IN- at the inductor's end and
OUT+ and OUT- at the other end. Top side: an 8-lead chip, an inductor
printed 2R2, 2 diodes.

Measured from two owner photos (top side, underside; not in the
repository) of the board lying on 5mm squared paper. The paper's grid
gives the scale; hardware/mechanical/_photo_grid.py fits it within
0.20mm. Sizes read off the board's top are divided by 1.005, the
factor the 2.54mm rows of the other boards on the same paper give.
Positions are within about 0.2mm.

Results: board 13.7 x 21.7mm, square corners. Seen from the top side
with the inductor down: OUT+ upper left, OUT- upper right, IN+ lower
left, IN- lower right. Output holes 10.1mm apart, 1.95mm below the
upper edge, pads about 3.5mm square. Input holes 10.4mm apart, 1.5mm
above the lower edge, pads about 2.6 x 3.0mm on the top side. Holes
about 1.3mm. A round pad of 1.4mm without a hole lies at the right
edge 8.05mm above the lower edge, next to R3. A hole of about 1.8mm
without a pad lies at the left edge 7.9mm above the lower edge.

The round pad is taken as the enable input: it is the board's only
pad besides the 4 corner pads. The board does not name it.

Unknown: the chip's type, the input voltage range, the output current,
the level that enables the output. The board's thickness is not
measured; the model draws 1.6mm.

The 3D model is hardware/mechanical/boost_5v_en.py.
"""

NAME = "Boost_5V_EN_13.7x21.7mm"
DESC = ("5V boost converter board with enable pad, 13.7 x 21.7mm. "
        "Corner pads with 1.3mm holes, seen from the top side with the "
        "inductor down: 3 OUT+ upper left, 4 OUT- upper right, 1 IN+ "
        "lower left, 2 IN- lower right. Pad 5 = round 1.4mm pad at the "
        "right edge next to R3, taken as enable, not named on the "
        "board. Measured from owner photos on squared paper, about "
        "0.2mm.")

W, H = 13.7, 21.7
IN_Y, OUT_Y = 20.2, 1.95
PADS = (("1", 1.65, IN_Y, (2.6, 3.0)), ("2", 12.05, IN_Y, (2.6, 3.0)),
        ("3", 1.8, OUT_Y, (3.5, 3.5)), ("4", 11.9, OUT_Y, (3.5, 3.5)))
DRILL = 1.3
EN = (12.85, 13.65, 1.4)
HOLE = (1.15, 13.8, 1.8)
INDUCTOR = (3.3, 13.15, 10.0, 19.85)
CHIP = (7.5, 6.55, 12.4, 10.45)

_PARTS = [("rect",) + INDUCTOR, ("rect",) + CHIP]

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (W, H),
    "origin": (PADS[0][1], PADS[0][2]),
    "pads": [{"num": n, "x": x, "y": y, "shape": "rect", "size": size,
              "drill": DRILL} for n, x, y, size in PADS]
    + [{"num": "5", "x": EN[0], "y": EN[1], "shape": "circle",
        "size": (EN[2], EN[2])},
       {"num": "", "x": HOLE[0], "y": HOLE[1], "shape": "circle",
        "size": (HOLE[2], HOLE[2]), "drill": HOLE[2], "type": "np"}],
    "silk": _PARTS + [("text", "IN+", 4.6, 20.9, 0.7),
                      ("text", "IN-", 9.3, 20.9, 0.7),
                      ("text", "OUT+", 5.1, 4.4, 0.7),
                      ("text", "OUT-", 9.0, 4.4, 0.7),
                      ("text", "EN", 11.0, 12.2, 0.7)],
    "fab": _PARTS + [("text", "2R2", 6.65, 16.5, 0.9)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-10.16, 7.62, 10.16, -7.62),
    "graphics": [("text", "5V boost", 0, 0, 1.0)],
    "pins": [
        ("power_in", "IN+", "1", -12.7, 5.08, 0),
        ("input", "EN", "5", -12.7, 0.0, 0),
        ("power_in", "IN-", "2", -12.7, -5.08, 0),
        ("power_out", "OUT+", "3", 12.7, 5.08, 180),
        ("passive", "OUT-", "4", 12.7, -5.08, 180),
    ],
}

MODEL = {"detail": "boost_5v_en"}
