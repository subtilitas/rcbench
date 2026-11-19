"""A WitMotion inertial sensor in its case with two slotted ears.

    wit_imu    black case, white label with blue print

A flat base with an ear at each end carries a square body. Each ear has
one slot for a screw. A label on the body's face shows the maker's
name, the sensor's axes and where the USB socket lies.

Source: one product picture with two dimensioned outlines and a photo
of the sensor on a hand (not in the repository). The picture does not
name the sensor's type.

    36.1 wide, 51.5 over the ears, 15 high; base 3 thick.

Scaled from the outlines at 3.74 pixels a mm, not dimensioned: the
body's length (36.1, as its width), each ear (7.7 long, 27.5 wide at
the body and 20.6 at its end), the slots (14.7 x 3.2, their middle 4.2
from the body), the corner radii (body 3.0, ears 2.0).

Read from the photo by eye: the label (31 x 31, corners 2.0) with a
blue line 0.8 inside its edge, and the place and size of its print.
The print is set in the font of _font.py, not traced: the maker's logo
is drawn as the letters "WIT". The axes are drawn as the label shows
them: X to one ear, Y across, Z out of the face.

Not in the picture and not drawn: the USB socket, which the label
places at the side to the right of the print; the rounding of the
body's upper edge; any screw, lamp or switch.

Frame: origin at the middle of the base's underside. x across the
36.1, y along the 51.5 towards the ear the label's X arrow points to,
z up through the body. The label's Y arrow points to -x. All sizes
are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

WIDTH, LENGTH, HEIGHT, BASE = 36.1, 51.5, 15.0, 3.0
BODY = 36.1
EAR = (27.5, 20.6, 2.0)         # width at the body, at the end, radius
SLOT = (14.7, 3.2, 4.2)         # length, width, middle from the body
CORNER = 3.0
LABEL = (31.0, 2.0, 0.8, 0.25)  # side, radius, line from the edge, line
BLUE = (0.10, 0.42, 0.72)
INK = p.PRINT


def slots():
    """The middles of the two slots, (x, y)."""
    y = BODY / 2 + SLOT[2]
    return [(0.0, -y), (0.0, y)]


def _base():
    """The base's outline with both ears, counter-clockwise."""
    half, body, end = WIDTH / 2, BODY / 2, LENGTH / 2
    wide, narrow, r = EAR[0] / 2, EAR[1] / 2, EAR[2]
    top = [(half, body - CORNER, (half - CORNER, body - CORNER, 1)),
           (half - CORNER, body), (wide, body), (narrow, end - r,
                                                 (narrow - r, end - r, 1)),
           (narrow - r, end), (-narrow + r, end, (-narrow + r, end - r, 1)),
           (-narrow, end - r), (-wide, body), (-half + CORNER, body,
                                               (-half + CORNER,
                                                body - CORNER, 1))]
    # The lower half is the upper one turned half round.
    def turned(corner):
        if len(corner) == 3:
            cx, cy, way = corner[2]
            return (-corner[0], -corner[1], (-cx, -cy, way))
        return (-corner[0], -corner[1])

    return top + [turned(c) for c in top]


def _arrow(length, rgb):
    """An arrow along +x from the origin: shaft and head, on z = 0."""
    return [s.ext(s.rect(0.0, -0.15, length - 1.6, 0.15), 0.0, INK, rgb),
            s.ext([(length - 1.6, -0.75), (length, 0.0),
                   (length - 1.6, 0.75)], 0.0, INK, rgb)]


def label():
    """The label and its print as shapes on z = 0."""
    side, r, inset, line = LABEL
    half = side / 2
    out = [s.ext(s.rounded(-half, -half, half, half, r), 0.0, INK, p.WHITE)]
    a, b = half - inset, half - inset - line
    ring = s.ext(s.rounded(-a, -a, a, a, r - inset), INK, 2 * INK, BLUE,
                 holes=[s.rounded(-b, -b, b, b, r - inset - line)])
    out.append(ring)
    marks = (
        s.place(p.text("WIT", 4.6, BLUE, 13.0), 0.0, 9.4),
        s.place(p.text("wit motion", 1.3, BLUE, 9.0), 0.0, 5.6),
        s.place(p.text("www.wit-motion.com", 1.5, BLUE, 20.0), 0.0, -10.6),
        s.place(p.text("X", 1.5, BLUE), 0.0, 3.2),
        s.place(p.text("Y", 1.5, BLUE), -7.2, -5.2),
        s.place(p.text("Z", 1.5, BLUE), 2.6, -5.2),
        s.place(p.text("USB", 1.7, BLUE), 11.2, 1.0, turn=90.0),
        s.place(_arrow(6.0, BLUE), 0.0, -4.2, turn=90.0),
        s.place(_arrow(5.0, BLUE), -1.0, -5.2, turn=180.0),
        [s.ext(s.circle(0.0, -5.2, 1.0), 0.0, INK, BLUE,
               holes=[s.circle(0.0, -5.2, 0.7)]),
         s.ext(s.circle(0.0, -5.2, 0.3), 0.0, INK, BLUE)],
        [s.ext(s.rect(9.6, -1.9, 12.8, 3.9), 0.0, INK, BLUE,
               holes=[s.rect(9.85, -1.65, 12.55, 3.65)])],
    )
    for shapes in marks:
        out += s.place(shapes, z=INK)
    return out


def sensor():
    """The sensor as shapes: base, body and label."""
    half, body = WIDTH / 2, BODY / 2
    length, width, _middle = SLOT
    holes = [s.stadium(x - length / 2, y - width / 2, x + length / 2,
                       y + width / 2) for x, y in slots()]
    out = [s.ext(_base(), 0.0, BASE, p.BLACK, holes=holes),
           s.ext(s.rounded(-half, -body, half, body, CORNER), BASE, HEIGHT,
                 p.BLACK)]
    return out + s.place(label(), z=HEIGHT)


def parts(_colors):
    """The model of the sensor.

    Returns:
        A dict of file stem to solids.
    """
    return {"wit_imu": s.items(sensor())}
