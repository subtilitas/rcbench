"""Deep-groove ball bearing 6704-2RS, 20 x 27 x 4, sealed both sides.

A thin-section bearing of the 67 series: bore 20, outside diameter 27,
width 4, with a rubber seal on each side (2RS). The model draws the
outer ring, the inner ring and the two seals; the balls and the cage lie
behind the seals and are not drawn.

Sources: the bore, outside diameter and width are the ISO 15 boundary
dimensions of a 6704 (d 20, D 27, B 4), which the seller's listing
(RowGlow, 10 pieces, "6704-2rs, 20 x 27 x 4 mm") repeats; the seals are
black in the seller's photos.

Estimated, in no source: the inner ring's outside diameter (22.0), the
outer ring's bore (25.0), the seals' thickness (0.5) and how far they lie
below the side faces (0.15). The rings' edge chamfers (0.2 at least by
ISO 15) are not drawn: each ring is a plain annulus.

Frame: the bearing's axis is z; z = 0 on one side face, the other at
z = 4; x and y in the side face, the origin on the axis. All sizes mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

STEM = "bearing_6704_2rs"
BORE, OUTSIDE, WIDTH = 20.0, 27.0, 4.0
INNER_OD, OUTER_ID = 22.0, 25.0
SEAL_T, SEAL_SET = 0.5, 0.15


def _ring(outside, inside, z0, z1, rgb):
    return s.ext(s.circle(0.0, 0.0, outside / 2), z0, z1, rgb,
                 holes=[s.circle(0.0, 0.0, inside / 2)])


def bearing():
    """The rings and the seals.

    Returns:
        The shapes.
    """
    out = [_ring(OUTSIDE, OUTER_ID, 0.0, WIDTH, p.STEEL),
           _ring(INNER_OD, BORE, 0.0, WIDTH, p.STEEL)]
    for z0 in (SEAL_SET, WIDTH - SEAL_SET - SEAL_T):
        out.append(_ring(OUTER_ID, INNER_OD, z0, z0 + SEAL_T, p.BLACK))
    return out


def parts(_colors):
    """The model of the bearing.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bearing())}
