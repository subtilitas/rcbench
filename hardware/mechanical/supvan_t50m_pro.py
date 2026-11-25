"""A Supvan KATASYMBOL T50M Pro label printer, a bought part.

    supvan_t50m_pro    black case, orange lever, glossy black panel

The case is two shells: a base below the joint line and a lid above
it. The top falls towards the front, where a glossy black panel
carries the power button, a green LED bar and the print button. The
label leaves through a slit across the top, behind the panel. One side
carries the orange lever that frees the lid and a USB-C socket; both
sides have a ribbed grip low down. The front end has a slot in a raised
frame. Four round feet stand under the base.

Sources: seven owner photos on millimetre paper (A: the side with lever
and socket, twice; B: the underside; C: the front end, twice; D: the
top, twice; not in the repository). The paper's fine lines are 1mm
apart and every fifth is stronger: counted in crops, 5mm is 87px, so
the paper reads 16.8 to 17.7px a mm. A projective fit to the 5mm lines
gives each photo's scale within 0.2 to 0.3mm. The photos' metadata give
37mm focal length (35mm equivalent); with it the fit puts the camera
252mm over the paper in A, 257mm in B and 247mm in D. A face h over the
paper reads larger by 252 / (252 - h); the sizes below are divided by
that. No maker's dimensions were found.

    B, underside up, at h 56: 119.0 long, 86.3 wide over the grip.
    A, lying on the side, at h 82 (the near side's edge, the width
    less about 5mm of rounding): 120.1 long, 56.0 high over the feet.
    The two views share two unknowns (width and height); they meet at
    86.3 and 56.0. Length: B 119.0, A 120.1; the model takes 120.0.
    D, at h 30, gives 121.3 x 91.2, the lever included.

C's grid is out of focus across the paper and gives no fit; its
proportions put the joint line about 20mm and the slot's middle about
12.5mm over the underside. Its width, 2660px, is the 86.3 at about
11mm from the end face.

Read from A, at its scale and height: the side outline. The top falls
from 56.0 at 30 behind the middle to 41.6 at 46.5 in front of it, a
slope of 10.7 degrees. The back has a 45 degree chamfer from 40.4 to
54.0 high and a bottom chamfer from 44.4 behind the middle to 11.7 high
at the back face. The front rounds off from 41.6 to the front face at
31.1 and 17.0 high. The lever's pivot is 1.7 behind the middle and
30.5 high, its disc 11 across, its arm 3.4 wide to a tip 20.4 in front
of the middle and 28.3 high. The USB-C socket is 37.5 in front of the
middle and 29.8 high, long side along the case. The grip runs from 20
behind to 40 in front of the middle, up to 13 high.

Read from D, each point at its own height: the slit, 53 wide, 14 in
front of the middle; the panel, 47 wide at its back edge and 44.5 at
its front, from 27 to 45.5 in front of the middle along the slope; the
power button, LED bar (7.3 long) and print button 12.7 to one side,
in the middle and 12.5 to the other, 40.2 in front of the middle; the
flat of the lid, 58 wide. From B: the feet, 28.7 off the middle line
at 7 from the front end and 33.9 off it at 32.6 behind the middle.

Estimated, not measured: the side chamfers of the top (a 45 degree
face drawn as three steps, 2.5, 6 and 10 down), the plan corner radius
(about 8, drawn as steps of 0.3, 1.2 and 3.5), the bottom edge (steps
of 0.8 and 2.5), the joint's gap (0.4), the slit's depth (3) and width
(1.2), the feet (5 across, 1 high), the lever's thickness (3), the
ribs (14 a side, 1.6 wide, 0.5 proud), the slot (25 x 3.8 in a frame
36 x 8, 0.8 proud), the socket's shell (9.0 x 3.3, the standard size;
the photo's bezel reads larger and is not a usable scale), the panel's
thickness (0.6) and the print's size and place, read by eye. The print
is the font of _font.py, not traced.

Not drawn: the recess at the front of the underside, the crease across
the underside, the label on the underside, the lid's hinge and the
paper roll inside, the dimple on the lever.

Residuals: length 1.1 between A and B. Each value is good to about
2mm; the height correction rests on the focal length in the metadata.

Frame: origin at the middle of the underside, on the feet's plane.
y along the case towards the front (the panel and the slot), x across,
z up. The lever and the socket are on the -x side. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

import _parts as p
import _solid as s

STEM = "supvan_t50m_pro"

HALF = 43.15            # half the width over the ribs
RIB = 0.5               # ribs proud of the side
FOOT = 1.0              # feet's height: the underside's z
JOINT = (20.6, 21.0)    # base's top, lid's underside
SLOPE = (-30.0, 56.0, 46.5, 41.6)  # the top falls from (y, z) to (y, z)
SLIT = (14.0, 1.2, 3.0, 26.5)      # y, width, depth, half length
BODY = p.BLACK
DARK = (0.02, 0.02, 0.02)
GLOSS = (0.03, 0.03, 0.035)
GREEN = (0.25, 0.90, 0.45)
ORANGE = (0.95, 0.55, 0.20)

# The side outline (y, z) of the middle of the case.
OUTLINE = [
    (-44.4, FOOT), (53.5, FOOT), (57.5, 3.5), (59.6, 7.9), (60.0, 12.0),
    (60.0, 19.0), (59.4, 25.0), (57.7, 31.1), (53.6, 37.6), (50.0, 40.2),
    (SLOPE[2], SLOPE[3]), (SLOPE[0], SLOPE[1]), (-40.7, 54.0),
    (-56.9, 40.4), (-57.2, 34.3), (-60.0, 33.0), (-60.0, 12.0),
    (-54.8, 6.7),
]
# Bands across the case: |x| from, to; the top's drop, the ends' inset
# and the bottom's rise in each.
BANDS = [
    (SLIT[3], 29.0, 0.0, 0.0, 0.0),
    (29.0, 36.0, 2.5, 0.3, 0.0),
    (36.0, 40.5, 6.0, 1.2, 0.8),
    (40.5, HALF - RIB, 10.0, 3.5, 2.5),
]
FEET = ((28.7, 50.0), (33.9, -32.6))
LEVER = ((-1.7, 30.5), 5.5, (20.4, 28.3), 1.7, 3.0)  # pivot, r, tip, w/2, t
USB = (37.5, 29.8, 9.0, 3.3)
GRIP = (-20.0, 40.0, 14, 1.6, 13.0)  # y from, to, ribs, rib width, top


def top(y):
    """The z of the top's slope at y."""
    y0, z0, y1, z1 = SLOPE
    return z0 + (z1 - z0) * (y - y0) / (y1 - y0)


def clip(poly, a, b, c):
    """The part of a polygon (y, z) where a y + b z <= c."""
    out = []
    for i, q in enumerate(poly):
        r = poly[(i + 1) % len(poly)]
        fq, fr = a * q[0] + b * q[1] - c, a * r[0] + b * r[1] - c
        if fq <= 0:
            out.append(q)
        if fq * fr < 0:
            k = fq / (fq - fr)
            out.append((q[0] + k * (r[0] - q[0]), q[1] + k * (r[1] - q[1])))
    clean = []
    for q in out:
        if not clean or math.dist(q, clean[-1]) > 1e-6:
            clean.append(q)
    if len(clean) > 1 and math.dist(clean[0], clean[-1]) <= 1e-6:
        clean.pop()
    return clean


def outline(drop, inset, rise):
    """The side outline of one band, its top, ends and bottom moved in."""
    y0, z0, y1, z1 = SLOPE
    m = (z1 - z0) / (y1 - y0)
    n = math.hypot(m, 1.0)
    poly = clip(OUTLINE, -m / n, 1.0 / n, (z0 - m * y0) / n - drop)
    poly = clip(poly, 1.0, 0.0, 60.0 - inset)
    poly = clip(poly, -1.0, 0.0, 60.0 - inset)
    poly = clip(poly, 0.0, -1.0, -FOOT - rise)
    if drop:
        # The back chamfer and the front's rounding, moved in by drop.
        a, b = (-13.6, 16.2), (0.85, 0.53)
        for (u, v), (py, pz) in ((a, (-56.9, 40.4)), (b, (53.6, 37.6))):
            k = math.hypot(u, v)
            poly = clip(poly, u / k, v / k, (u * py + v * pz) / k - drop)
    return poly


def shells(poly, x0, x1):
    """A band's base and lid, from x0 to x1."""
    base = clip(poly, 0.0, 1.0, JOINT[0])
    lid = clip(poly, 0.0, -1.0, -JOINT[1])
    return [s.ext(base, x0, x1, BODY, frame=s.X),
            s.ext(lid, x0, x1, BODY, frame=s.X)]


def case():
    """Base and lid in bands across the case, with the slit."""
    whole = outline(0.0, 0.0, 0.0)
    out = []
    y, width, depth, half = SLIT
    out.append(s.ext(clip(whole, 0.0, 1.0, JOINT[0]), -half, half, BODY,
                     frame=s.X))
    lid = clip(whole, 0.0, -1.0, -JOINT[1])
    back = clip(lid, 1.0, 0.0, y - width / 2)
    front = clip(lid, -1.0, 0.0, -(y + width / 2))
    floor = [(y - width / 2, JOINT[1]), (y + width / 2, JOINT[1]),
             (y + width / 2, top(y + width / 2) - depth),
             (y - width / 2, top(y - width / 2) - depth)]
    out += [s.ext(back, -half, half, BODY, frame=s.X),
            s.ext(front, -half, half, BODY, frame=s.X),
            s.ext(floor, -half, half, DARK, frame=s.X)]
    outer = max(band[1] for band in BANDS)
    for x0, x1, drop, inset, rise in BANDS:
        poly = outline(drop, inset, rise)
        out += shells(poly, x0, x1) + shells(poly, -x1, -x0)
        if x0 < 29.0:
            continue
        # The joint's gap is closed 0.6 in from the band's surface.
        slab = clip(clip(poly, 0.0, -1.0, -JOINT[0]), 0.0, 1.0, JOINT[1])
        ys = [q[0] for q in slab]
        slab = clip(clip(slab, 1.0, 0.0, max(ys) - 0.6), -1.0, 0.0,
                    -(min(ys) + 0.6))
        end = x1 - 0.6 if x1 == outer else x1
        out += [s.ext(slab, x0, end, BODY, frame=s.X),
                s.ext(slab, -end, -x0, BODY, frame=s.X)]
    # The joint's gap is closed inside, 1.5 in from the ends.
    filler = [(-58.5, JOINT[0]), (58.5, JOINT[0]), (58.5, JOINT[1]),
              (-58.5, JOINT[1])]
    out.append(s.ext(filler, -29.0, 29.0, BODY, frame=s.X))
    return out


def feet():
    """The four feet under the base."""
    return [s.ext(s.circle(sx * x, y, 2.5), 0.0, FOOT, BODY)
            for x, y in FEET for sx in (-1, 1)]


def grip():
    """The ribs on both sides, low down."""
    y0, y1, count, width, high = GRIP
    out = []
    rise = BANDS[-1][4]
    for i in range(count):
        y = y0 + (y1 - y0) * i / (count - 1)
        z1 = min(high, FOOT + rise + 2.0 + (y - y0 + 2.0) * 0.8)
        rib = s.rect(y - width / 2, FOOT + rise + 0.5, y + width / 2, z1)
        out += [s.ext(rib, HALF - RIB, HALF, BODY, frame=s.X),
                s.ext(rib, -HALF, -HALF + RIB, BODY, frame=s.X)]
    return out


def lever():
    """The orange lever on the -x side: a disc and an arm, one outline."""
    (cy, cz), r, (ty, tz), w, thick = LEVER
    a = math.atan2(tz - cz, ty - cy)
    da = math.asin(w / r)
    dy, dz = math.cos(a), math.sin(a)

    def on_disc(t):
        return (cy + r * math.cos(t), cz + r * math.sin(t))

    poly = []
    sweep = 2 * math.pi - 2 * da
    for k in range(3):
        poly.append((*on_disc(a + da + sweep * k / 3), (cy, cz, 1)))
    poly.append(on_disc(a - da))
    poly.append((ty + w * dz, tz - w * dy, (ty, tz, 1)))
    poly.append((ty + w * dy, tz + w * dz, (ty, tz, 1)))
    poly.append((ty - w * dz, tz + w * dy))
    face = -HALF + RIB
    return [s.ext(poly, face - thick, face, ORANGE, frame=s.X)]


def usb():
    """The USB-C socket's shell and opening on the -x side."""
    y, z, length, width = USB
    face = -HALF + RIB
    hole = s.stadium(y - length / 2 + 0.5, z - width / 2 + 0.5,
                     y + length / 2 - 0.5, z + width / 2 - 0.5)
    shell = s.stadium(y - length / 2, z - width / 2, y + length / 2,
                      z + width / 2)
    return [s.ext(shell, face - 0.3, face, p.STEEL, holes=[hole],
                  frame=s.X),
            s.ext(hole, face - 0.1, face, DARK, frame=s.X)]


def slot():
    """The slot in its raised frame on the front end."""
    frame = [(-18.0, 16.5), (-18.0, 11.0), (-14.0, 8.5), (14.0, 8.5),
             (18.0, 11.0), (18.0, 16.5)]
    hole = s.stadium(-12.5, 10.6, 12.5, 14.4)
    return [s.ext(frame, -60.8, -60.0, BODY, holes=[hole], frame=s.Y),
            s.ext(hole, -60.3, -60.0, DARK, frame=s.Y)]


def on_slope(shapes):
    """Shapes drawn on z = 0 laid onto the top's slope.

    In the slope's plane, u is x and v runs down the slope from the
    point (0, SLOPE[0], SLOPE[1]); the shapes' z is the slope's normal.
    """
    y0, z0, y1, z1 = SLOPE
    k = math.hypot(y1 - y0, z1 - z0)
    o = (0.0, y0, z0)
    u = (1.0, 0.0, 0.0)
    v = (0.0, (y1 - y0) / k, (z1 - z0) / k)
    w = (0.0, -v[2], v[1])

    def vec(a):
        return tuple(a[0] * u[i] + a[1] * v[i] + a[2] * w[i]
                     for i in range(3))

    out = []
    for outer, holes, d0, d1, rgb, (fo, fu, fv, fw) in shapes:
        at = vec(fo)
        out.append((outer, holes, d0, d1, rgb,
                    (tuple(o[i] + at[i] for i in range(3)), vec(fu),
                     vec(fv), vec(fw))))
    return out


def along(y):
    """The distance down the slope from its first point to y."""
    y0, z0, y1, z1 = SLOPE
    return (y - y0) * math.hypot(y1 - y0, z1 - z0) / (y1 - y0)


def panel():
    """The glossy panel with its buttons, LED bar and print."""
    thick = 0.6
    v0, v1 = along(27.0), along(45.5)
    out = [s.ext([(23.5, v0), (22.25, v1), (-22.25, v1), (-23.5, v0)],
                 0.0, thick, GLOSS)]
    keys = along(40.2)
    marks = [
        s.ext(s.circle(12.7, keys, 2.0), 0.0, p.PRINT, p.WHITE,
              holes=[s.circle(12.7, keys, 1.65)]),
        s.ext(s.rect(12.525, keys - 1.6, 12.875, keys - 0.1), 0.0, p.PRINT,
              p.WHITE),
        s.ext(s.stadium(-3.65, keys - 0.8, 3.65, keys + 0.8), 0.0, 0.2,
              GREEN),
    ]
    marks += p.frame(-14.5, keys - 1.5, -10.5, keys + 1.5, 0.3)
    marks += p.frame(-13.5, keys - 0.2, -11.5, keys + 1.0, 0.3)
    words = along(30.0)
    marks += s.place(p.text("KATASYMBOL", 1.4, p.WHITE, 24.6), 9.35,
                     words, turn=180.0)
    marks += s.place(p.text("T50M Pro", 1.6, p.WHITE, 13.2), -14.55,
                     words, turn=180.0)
    out += s.place(marks, z=thick)
    return on_slope(out)


def printer():
    """The printer as shapes."""
    return (case() + feet() + grip() + lever() + usb() + slot()
            + panel())


def parts(_colors):
    """The model of the printer.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(printer())}
