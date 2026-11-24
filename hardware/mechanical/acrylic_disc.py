"""Clear acrylic disc, 49.5 across and 2 thick.

A flat round blank of clear acrylic, sold as "50mm" round miniature bases
(FINGERINSPIRE, 30 pieces). The seller's dimensioned picture gives the
diameter as 49.5 and the thickness as 2. The edges are drawn square;
whether they are rounded or chamfered is not known.

The disc is clear; STEP carries no transparency here, so the model is
drawn in a pale blue.

Frame: the disc's axis is z; z = 0 on one face, the other at z = 2; the
origin on the axis. All sizes mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _solid as s

STEM = "acrylic_disc_49_5"
DIAMETER, THICK = 49.5, 2.0
CLEAR = (0.78, 0.87, 0.92)


def disc():
    """The disc.

    Returns:
        The shapes.
    """
    return [s.ext(s.circle(0.0, 0.0, DIAMETER / 2), 0.0, THICK, CLEAR)]


def parts(_colors):
    """The model of the disc.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(disc())}
