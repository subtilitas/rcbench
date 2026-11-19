"""A standard size servo with a round horn.

    servo_standard    black case, white print "Standard Servo"

The case is three shells on top of each other: bottom cover, middle
and top. Two ears with two open mounting holes each stand out of the
top shell. The output shaft carries a round horn.

Source: an owner sketch with three views and dimensions (not in the
repository). The sketch names neither maker nor type.

    Case 40.5 long, 20 wide, 36.3 high. 55 over the ears, each ear 7.2
    long and 2.5 thick, its underside 26.3 over the case's underside.
    Mounting holes diameter 5, 49.5 apart along the case and 10 across.
    Shaft axis 30.4 from one end of the case and 10.1 from the other,
    34.9 and 14.6 from the two rows of holes. Horn 2 thick, its
    underside 5.8 over the case, its top 44.1 over the case's underside.

The sketch gives 55 over the ears; its 7.2 + 40.5 + 7.2 are 54.9. The
model takes the three parts and is 54.9 long. The rows of holes lie
49.5 / 2 from the middle of the case, which puts them 34.9 and 14.6
from the shaft, as the sketch has them.

Not dimensioned, scaled from the sketch at 4.5 pixels a mm: the horn's
diameter (21), the boss round the shaft (10.5 across, 2.0 high) and the
shaft's hub (8.5 across), the seams between the shells (3.7 and 22.5
over the underside), the slot from each mounting hole to the ear's end
(2.6 wide), the case's corner radius (1.0), and the horn's holes (8 at
radius 8.5 and 4 at radius 6.0, each 1.2 across, and 2.6 at the
centre). The sketch draws 12 holes on the horn; their places are read
by eye.

Not in the sketch and not drawn: the cable and its plug, the horn's
screw, the splines of the shaft, the case's screws.

The print is the owner's choice: "Standard Servo" in white on both long
sides of the middle shell, in the font of _font.py, capitals 3.2 high.
The horn is drawn white; the sketch gives no colours.

Frame: origin on the shaft's axis, on the case's underside. x along
the case, the long end towards -x; y across; z up. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

import _parts as p
import _solid as s

LENGTH, WIDTH, HEIGHT = 40.5, 20.0, 36.3
SHAFT = 30.4            # shaft axis from the case's long end
EAR = (7.2, 2.5, 26.3)  # length, thickness, underside over the case's
HOLES = (49.5, 10.0, 5.0, 2.6)  # along, across, diameter, slot
SEAMS = (3.7, 22.5)
CORNER = 1.0
BOSS = (10.5, 2.0)      # diameter, height
HUB = 8.5
HORN = (21.0, 2.0, 5.8)  # diameter, thickness, underside over the case
HORN_HOLES = ((8, 8.5, 22.5), (4, 6.0, 0.0))  # count, radius, first angle
HORN_HOLE, HORN_BORE = 1.2, 2.6
LABEL = ("Standard Servo", 3.2, 0.12)   # text, letter height, added gap


def _ear(side):
    """One ear's outline with its two open holes, seen from above.

    Args:
        side: +1 for the ear at +x, -1 for the ear at -x.

    Returns:
        The profile, counter-clockwise.
    """
    x0 = LENGTH - SHAFT if side > 0 else -SHAFT
    x1 = x0 + side * EAR[0]
    cx = (LENGTH / 2 - SHAFT) + side * HOLES[0] / 2
    r, h = HOLES[2] / 2, HOLES[3] / 2
    mouth = cx + side * math.sqrt(r * r - h * h)
    # Up the ear's end at +x, down it at -x; the case's side closes it.
    out = [(x0, -side * WIDTH / 2), (x1, -side * WIDTH / 2)]
    for cy in (-side * HOLES[1] / 2, side * HOLES[1] / 2):
        a, b = cy - side * h, cy + side * h
        arc = (cx, cy, -1)
        out += [(x1, a), (mouth, a, arc), (cx, cy - side * r, arc),
                (cx - side * r, cy, arc), (cx, cy + side * r, arc),
                (mouth, b), (x1, b)]
    return out + [(x1, side * WIDTH / 2), (x0, side * WIDTH / 2)]


def _on_side(shapes, x, z, side):
    """Stand shapes drawn on z = 0 on a long side of the case.

    Args:
        shapes: Shapes in the frame Z, moved but not turned.
        x: The place of the shapes' origin along the case.
        z: Its height.
        side: -1 for the side at -y, +1 for the side at +y.

    Returns:
        The shapes, their x along the side as read from outside, their
        y up and their z out of the side.
    """
    u, w = (-side * 1.0, 0.0, 0.0), (0.0, side * 1.0, 0.0)
    out = []
    for outer, holes, d0, d1, rgb, (o, _u, _v, _w) in shapes:
        origin = (x + o[0] * u[0], side * WIDTH / 2 + o[2] * w[1],
                  z + o[1])
        out.append((outer, holes, d0, d1, rgb,
                    (origin, u, (0.0, 0.0, 1.0), w)))
    return out


def label():
    """The print as shapes on z = 0, its middle at the origin.

    The letters stand LABEL[2] further apart than _font.py sets them:
    at the font's own step the r and the v of "Servo" touch.
    """
    string, height, gap = LABEL
    steps = [(p.SPACE if c == " " else p.GLYPHS[c][0]) * height + gap
             for c in string]
    out, x = [], -(sum(steps) - gap) / 2
    for c, step in zip(string, steps, strict=True):
        if c != " ":
            out += s.place(p.text(c, height, p.WHITE),
                           x + (step - gap) / 2)
        x += step
    return out


def horn_holes():
    """The centres of the horn's holes round the bore, (x, y)."""
    return [(radius * math.cos(math.radians(first + 360.0 * k / count)),
             radius * math.sin(math.radians(first + 360.0 * k / count)))
            for count, radius, first in HORN_HOLES for k in range(count)]


def servo():
    """The servo as shapes: case, ears, print, boss, hub and horn."""
    x0, x1 = -SHAFT, LENGTH - SHAFT
    case = s.rounded(x0, -WIDTH / 2, x1, WIDTH / 2, CORNER)
    out = [s.ext(case, lo, hi, p.BLACK)
           for lo, hi in zip((0.0, *SEAMS), (*SEAMS, HEIGHT), strict=True)]
    out += [s.ext(_ear(side), EAR[2], EAR[2] + EAR[1], p.BLACK)
            for side in (1, -1)]
    for side in (-1, 1):
        out += _on_side(label(), (x0 + x1) / 2,
                        (SEAMS[0] + SEAMS[1]) / 2, side)
    top = HEIGHT + HORN[2]
    out.append(s.ext(s.circle(0.0, 0.0, BOSS[0] / 2), HEIGHT,
                     HEIGHT + BOSS[1], p.BLACK))
    out.append(s.ext(s.circle(0.0, 0.0, HUB / 2), HEIGHT + BOSS[1], top,
                     p.WHITE))
    holes = [s.circle(0.0, 0.0, HORN_BORE / 2)] + [
        s.circle(x, y, HORN_HOLE / 2) for x, y in horn_holes()]
    out.append(s.ext(s.circle(0.0, 0.0, HORN[0] / 2), top, top + HORN[1],
                     p.WHITE, holes=holes))
    return out


def parts(_colors):
    """The model of the servo.

    Returns:
        A dict of file stem to solids.
    """
    return {"servo_standard": s.items(servo())}
