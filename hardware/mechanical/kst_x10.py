"""The KST X10 wing servo, 10 thick.

    kst_x10    silver case with a red band, short cable

The case is 30 x 10 x 35.5. Two ears beside the output end and a tab at
the other end lie flat on one of the two large faces and carry the
mounting holes; the output shaft leaves the 30 x 10 end face.

Sources: a dimension drawing of the servo with two views, and five
photos of an X10 on a frame (neither in the repository).

    Drawing: case 30 x 10, 35.5 high; 42 over the ears; holes 3 across
    and 36 apart; ears 8 high and 1.8 thick; shaft axis 29.4 from one
    ear's end, which is 6.6 from the nearer side of the case; shaft's
    end 38.8 over the case's far end, so 3.3 out of the case; tab 10.1
    wide.
    The drawing's 39.2 and 41.9 start at the case's output end: laid on
    the drawing at 5.07 pixels a mm they end at the tab's hole (3.7
    past the case) and at the tab's end (6.4 past the case). The
    drawing does not say so.

Scaled from the drawing, not dimensioned: the red band (16.8 to 22.3
from the output end), the slot in the upper face (3.6 wide, 13.3 to
16.9 from the shaft's axis, 11.0 to 31.4 from the output end), the
tab's place (in the middle of the case).

Estimated from the photos: the corner radii (case 0.8, ears and tab
2.5), the tab's hole (3 across, as the ears'), the shaft's diameter
(4.8) and its collar (7.2 across, 0.5 high), the 4 screws of the output
end, the place of the cable (out of the side next to the shaft, 3.5
from the far end) and its wires (1.0 across, 1.2 apart, brown, red,
orange). The cable is drawn 15 long, without its plug.

The print is set in the font of _font.py, not traced: "KST" and "X10"
in black on the silver part and a white "X" on the band, each read
with the output end up. The slot is drawn as a dark mark of the
print's thickness, not as a recess. The shaft's splines and its screw
hole are not drawn.

Frame: origin on the shaft's axis, at the case's output end, on the
large face the ears lie on. x along the 30, the longer part of the case
towards +x; y along the shaft, out of the case towards +y; z through
the case's 10. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

LENGTH, THICK, HEIGHT = 30.0, 10.0, 35.5
SIDE = 6.6              # shaft axis from the nearer side of the case
EAR = (6.0, 8.0, 1.8)   # past the case, along the shaft, thickness
HOLE = 3.0
HOLES = 36.0
TAB = (10.1, 6.4, 3.7)  # width, length, hole past the case
BAND = (16.8, 22.3)
SLOT = (13.3, 16.9, 11.0, 31.4)
SHAFT = (4.8, 3.3, 7.2, 0.5)    # diameter, length, collar, its height
CORNER, LUG = 0.8, 2.5
CABLE = (3.5, 15.0, 1.0, 1.2)   # from the far end, length, wire, pitch
SILVER = (0.80, 0.81, 0.82)
BAND_RED = (0.72, 0.09, 0.10)
MARK = (0.20, 0.20, 0.21)
WIRES = ((0.35, 0.20, 0.10), p.RED, p.ORANGE)
X0, X1 = -SIDE, LENGTH - SIDE


def hole_places():
    """The centres of the three mounting holes, (x, y)."""
    middle = (X0 + X1) / 2
    return [(middle - HOLES / 2, -EAR[1] / 2),
            (middle + HOLES / 2, -EAR[1] / 2),
            (middle, -HEIGHT - TAB[2])]


def _lug(x0, y0, x1, y1, hole, round_at):
    """A flat lug with one hole; two corners rounded.

    Args:
        x0: The lug's box, lower x.
        y0: Lower y.
        x1: Upper x.
        y1: Upper y.
        hole: The hole's centre.
        round_at: "x0", "x1" or "y0": the side whose corners are round.
    """
    r = LUG
    if round_at == "x1":
        outer = [(x0, y0), (x1 - r, y0, (x1 - r, y0 + r, 1)), (x1, y0 + r),
                 (x1, y1 - r, (x1 - r, y1 - r, 1)), (x1 - r, y1), (x0, y1)]
    elif round_at == "x0":
        outer = [(x1, y1), (x0 + r, y1, (x0 + r, y1 - r, 1)), (x0, y1 - r),
                 (x0, y0 + r, (x0 + r, y0 + r, 1)), (x0 + r, y0), (x1, y0)]
    else:
        outer = [(x1, y1), (x0, y1), (x0, y0 + r, (x0 + r, y0 + r, 1)),
                 (x0 + r, y0), (x1 - r, y0, (x1 - r, y0 + r, 1)),
                 (x1, y0 + r)]
    return s.ext(outer, 0.0, EAR[2], SILVER,
                 holes=[s.circle(hole[0], hole[1], HOLE / 2)])


def servo():
    """The servo as shapes."""
    left, right, rear = hole_places()
    side = s.rounded(X0, 0.0, X1, THICK, CORNER)
    out = [s.ext(side, a, b, rgb, frame=s.Y)
           for a, b, rgb in ((0.0, BAND[0], SILVER),
                             (BAND[0], BAND[1], BAND_RED),
                             (BAND[1], HEIGHT, SILVER))]
    out.append(_lug(X0 - EAR[0], -EAR[1], X0, 0.0, left, "x0"))
    out.append(_lug(X1, -EAR[1], X1 + EAR[0], 0.0, right, "x1"))
    middle = (X0 + X1) / 2
    out.append(_lug(middle - TAB[0] / 2, -HEIGHT - TAB[1],
                    middle + TAB[0] / 2, -HEIGHT, rear, "y0"))
    # Output end: collar, shaft, the heads of 4 screws.
    axis = (0.0, THICK / 2)
    out.append(s.ext(s.circle(*axis, SHAFT[2] / 2), -SHAFT[3], 0.0, SILVER,
                     frame=s.Y))
    out.append(s.ext(s.circle(*axis, SHAFT[0] / 2), -SHAFT[1], -SHAFT[3],
                     p.GOLD, frame=s.Y))
    for x in (X0 + 1.6, X1 - 1.6):
        for z in (1.6, THICK - 1.6):
            if abs(x) > SHAFT[2] / 2 + 0.8:
                out.append(s.ext(s.circle(x, z, 0.7), -0.15, 0.0, p.STEEL,
                                 frame=s.Y))
    # Upper face: slot mark and print.
    for a, b in ((SLOT[2], BAND[0]), (BAND[0], BAND[1]),
                 (BAND[1], SLOT[3])):
        out.append(s.ext(s.rect(SLOT[0], -b, SLOT[1], -a), THICK,
                         THICK + p.PRINT, MARK))
    out += s.place(p.text("KST", 3.0, p.BLACK), 3.2, -6.0, THICK)
    out += s.place(p.text("X10", 2.2, p.BLACK), 3.2, -12.0, THICK)
    out += s.place(p.text("X", 4.2, p.WHITE),
                   3.2, -(BAND[0] + BAND[1]) / 2, THICK)
    # Cable: 3 wires out of the side next to the shaft.
    y = -HEIGHT + CABLE[0]
    for k, rgb in enumerate(WIRES):
        out.append(s.ext(s.circle(y + (k - 1) * CABLE[3], THICK / 2,
                                  CABLE[2] / 2),
                         X0 - CABLE[1], X0, rgb, frame=s.X))
    return out


def parts(_colors):
    """The model of the servo.

    Returns:
        A dict of file stem to solids.
    """
    return {"kst_x10": s.items(servo())}
