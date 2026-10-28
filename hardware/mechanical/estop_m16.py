"""Emergency stop button, M16 x 1 panel mount, mushroom head 24.9mm.

A latching mushroom push button turned to release ("STOP" with rotation
arrows on the head), red aluminium head, chrome bezel, M16 x 1 thread,
hex nut and anti-rotation ring, translucent switch body, 5 flat tabs
2.8 x 0.5mm (C, NO, NC and the lamp's + and -).

Source: the seller's dimension drawing (product image with the photo
above it; maker and part number not stated). Read from the drawing, all
mm, lengths from the tab ends:

- overall length 50.1; head 24.9 across, its front face 19.6 across;
  head back at 34.4, so the head is 15.7 long
- 31.7: the bezel's back face, so the bezel is 2.7 long
- 29.3: the nut's front face
- 12: thread length visible from the nut's front face to the switch body
- 7: tab length; tabs 2.8 x 0.5
- M16 x 1 thread
- rear view: blue base 12.8 wide, terminal block 13.8 tall
- side view: 20.4 and 19.6 across, read here as the bezel and the nut
  across flats

Estimated, not on the drawing: the head's front edge radius (2.65mm,
from the 24.9 and 19.6 diameters), the bezel's front chamfer (0.5mm),
nut thickness (3.0mm), ring thickness (1.0mm), ring bore 16.2mm and outer
diameter 20.4mm, the ring's tab (2.0mm wide, 1.4mm tall), the switch
body diameter (15.0mm) and the tab positions (C NO NC in a row 4.2mm
apart, + above and - below at 5.0mm). The thread is a plain 16.0mm
cylinder. The tab holes and the head's print are not modelled.

Model frame: z = 0 on the panel's front face, where the bezel seats; the
head points to +z, the tabs to -z; x to the right of the "STOP" print,
y up. The nut and ring files have their own origin: z = 0 on the face
towards the panel, the part below it. The assembly places them for a
1.4mm panel, which puts the nut's front face 2.4mm behind the bezel, as
the drawing's 31.7 and 29.3 give.
"""

import math

HEAD_D, FACE_D = 24.9, 19.6
LENGTH = 50.1
BEZEL_BACK, HEAD_BACK, NUT_FRONT = 31.7, 34.4, 29.3
THREAD_BEHIND_NUT = 12.0
TAB_LEN, TAB_W, TAB_T = 7.0, 2.8, 0.5
BASE_W, BASE_H = 12.8, 13.8
BEZEL_D, NUT_AF = 20.4, 19.6

# Estimates (see the docstring).
EDGE_R = (HEAD_D - FACE_D) / 2
BEZEL_CHAMFER = 0.5
NUT_T = 3.0
RING_T, RING_BORE, RING_D = 1.0, 16.2, 20.4
RING_TAB_W, RING_TAB_H = 2.0, 1.4
BODY_D = 15.0
BASE_LEN = 3.0
PANEL = BEZEL_BACK - NUT_FRONT - RING_T


def z(d):
    """Model z of a drawing length measured from the tab ends."""
    return d - BEZEL_BACK


def _arc(cr, cz, rad, a0, a1, n=8):
    return [(cr + rad * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cz + rad * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def _poly(n, r, rot=0.0):
    return [(r * math.cos(rot + 2 * math.pi * i / n),
             r * math.sin(rot + 2 * math.pi * i / n)) for i in range(n)]


def _box(x0, y0, x1, y1, z0, z1, col):
    return ("prism", [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], [], z0, z1,
            col)


def button(colors):
    """The button without nut and ring.

    Args:
        colors: Colour name to RGB.

    Returns:
        The solids, as step_gen.write takes them.
    """
    r_head = HEAD_D / 2
    top = z(LENGTH)
    head = [(r_head - 1.0, z(HEAD_BACK)), (r_head, z(HEAD_BACK) + 1.0)]
    head += _arc(FACE_D / 2, top - EDGE_R, EDGE_R, 0, 90)
    rb = BEZEL_D / 2
    bezel = [(rb, 0.0), (rb, z(HEAD_BACK) - BEZEL_CHAMFER),
             (rb - BEZEL_CHAMFER, z(HEAD_BACK))]
    body_top = z(NUT_FRONT - THREAD_BEHIND_NUT)
    base_top = z(TAB_LEN + BASE_LEN)
    out = [
        ("revolve", head, 64, colors["red"]),
        ("revolve", bezel, 64, colors["metal"]),
        ("revolve", [(8.0, body_top), (8.0, 0.0)], 48, colors["metal"]),
        ("revolve", [(BODY_D / 2, base_top), (BODY_D / 2, body_top)], 48,
         colors["white"]),
        _box(-BASE_W / 2, -BASE_H / 2, BASE_W / 2, BASE_H / 2, z(TAB_LEN),
             base_top, colors["termblue"]),
    ]
    zt0, zt1 = z(0.0), z(TAB_LEN)
    for x in (-4.2, 0.0, 4.2):            # C, NO, NC: tabs on edge
        out.append(_box(x - TAB_T / 2, -TAB_W / 2, x + TAB_T / 2, TAB_W / 2,
                        zt0, zt1, colors["metal"]))
    for y in (5.0, -5.0):                 # lamp + and -: tabs flat
        out.append(_box(-TAB_W / 2, y - TAB_T / 2, TAB_W / 2, y + TAB_T / 2,
                        zt0, zt1, colors["metal"]))
    return out


def nut(colors, z0=0.0):
    """The hex nut, its panel side at z0, the nut below it.

    Args:
        colors: Colour name to RGB.
        z0: z of the face towards the panel.

    Returns:
        The solids.
    """
    hexagon = _poly(6, NUT_AF / math.sqrt(3), math.pi / 6)
    return [("prism", hexagon, [_poly(48, 8.0)], z0 - NUT_T, z0,
             colors["metal"])]


def ring(colors, z0=0.0):
    """The anti-rotation ring, its panel side at z0; the tab stands up.

    Args:
        colors: Colour name to RGB.
        z0: z of the face towards the panel.

    Returns:
        The solids.
    """
    r = RING_D / 2
    return [("prism", _poly(64, r), [_poly(48, RING_BORE / 2)],
             z0 - RING_T, z0, colors["metal"]),
            _box(r - 1.0, -RING_TAB_W / 2, r, RING_TAB_W / 2, z0,
                 z0 + RING_TAB_H, colors["metal"])]


def parts(colors):
    """Every model file of the button.

    Args:
        colors: Colour name to RGB.

    Returns:
        A dict of file stem to solids.
    """
    return {
        "estop_m16_button": button(colors),
        "estop_m16_nut": nut(colors),
        "estop_m16_ring": ring(colors),
        "estop_m16_assembly": button(colors)
        + ring(colors, -PANEL) + nut(colors, z(NUT_FRONT)),
    }
