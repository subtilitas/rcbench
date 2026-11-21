"""Layout of the remote INA228 sensor node: its boards in place.

    ina228_node_layout   the INA228 module with its 2 XT90PW connectors
                         at the bottom, the ES3C28P display on top

A study of where the parts go, before a case is drawn round them. No
part is changed: each is the model of its own spec, moved and turned.

    ina228_xt90.py   the INA228 module, in its own frame
    es3c28p.py       the display board

Chosen (owner, 2026-11-21): INA228 module at the bottom, display on
top. The node has no cell and no charger board: a small buck converter
for inputs up to 60V feeds it from the measured line. That converter
has no model in this directory and is not placed.

- The display's long side runs along the INA228 board's, the two
  boards' centers over each other, its USB Type-C socket at the socket
  half's end. Its lowest part is GAP over the connectors' housings.

Not drawn: the buck converter, wires, anything that holds the parts;
they float where a case will hold them.

Frame: the INA228 module's. Origin at its header's pin 1 on the board's
underside, x along the header and the board's long side, z up. All
sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _solid as s
import es3c28p
import ina228_xt90

STEM = "ina228_node_layout"
GAP = 0.5               # between the display's parts and what lies under
BOARD = (-56.16, -4.2, 23.94, 44.4)     # the INA228 board, x0, y0, x1, y1
HOUSING_TOP = 11.5      # the connectors' upper face over the underside


def extent(shapes):
    """The box round shapes: x0, y0, z0, x1, y1, z1."""
    points = [q for _k, pts, _e, _f, _rgb in s.items(shapes) for q in pts]
    return tuple(min(q[k] for q in points) for k in range(3)) + tuple(
        max(q[k] for q in points) for k in range(3))


def module():
    """The INA228 module, where its own spec draws it."""
    return (ina228_xt90.board() + ina228_xt90.top_side()
            + ina228_xt90.underside() + ina228_xt90.printed())


def display():
    """The display over everything, its long side along the board's."""
    shapes = s.place(es3c28p.bare(), turn=90.0)
    box = extent(es3c28p.board()[:1])
    # Turned 90 degrees, the board's (x, y) is (-y, x).
    cx, cy = -(box[1] + box[4]) / 2, (box[0] + box[3]) / 2
    return s.place(shapes, x=(BOARD[0] + BOARD[2]) / 2 - cx,
                   y=(BOARD[1] + BOARD[3]) / 2 - cy,
                   z=HOUSING_TOP + GAP - extent(shapes)[2])


def layout():
    """Every part in place.

    Returns:
        A dict of name to shapes.
    """
    return {"module": module(), "display": display()}


def parts(_colors):
    """The layout as one model.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items([shape for shapes in layout().values()
                           for shape in shapes])}
