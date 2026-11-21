"""Layout of the remote INA228 sensor node: its boards in place.

    ina228_node_layout   the INA228 module with its 2 XT90PW connectors
                         at the bottom, the Matek mBEC12S buck converter
                         beside its plus bar, the ES3C28P display on top

A study of where the parts go, before a case is drawn round them. No
part is changed: each is the model of its own spec, moved and turned.

    ina228_xt90.py     the INA228 module, in its own frame
    matek_mbec12s.py   the buck converter, 6 to 60V in, 5V out
    es3c28p.py         the display board

Chosen (owner, 2026-11-21): INA228 module at the bottom, display on
top. The node has no cell and no charger board: the Matek mBEC12S feeds
it from the measured line.

- The converter lies between the 2 connectors on the side of the board
  away from the header, BUCK[1] over it, its 4 pads towards the plug
  half. The shunt with the minus bar (y 10.0 to 18.1) and the plus bar
  (y 22.5 to 27.6, to 28.5 on the underside) carry the whole current;
  the converter lies beside them, BAR_GAP past the plus bar's solder,
  not over them. The strip it lies in is 15.9 wide, the converter 15.0.
- The display's long side runs along the INA228 board's, the two
  boards' centers over each other, its USB Type-C socket at the socket
  half's end. Its lowest part is GAP over the connectors' housings.

Not drawn: wires, anything that holds the parts;
they float where a case will hold them.

Frame: the INA228 module's. Origin at its header's pin 1 on the board's
underside, x along the header and the board's long side, z up. All
sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _solid as s
import es3c28p
import ina228_xt90
import matek_mbec12s

STEM = "ina228_node_layout"
GAP = 0.5               # between the display's parts and what lies under
BOARD = (-56.16, -4.2, 23.94, 44.4)     # the INA228 board, x0, y0, x1, y1
HOUSING_TOP = 11.5      # the connectors' upper face over the underside
PLUS_BAR = 28.5         # the plus bar's solder, its edge away from
#                         the header, y
BAR_GAP = 0.5           # between the converter and the plus bar
# The converter: x of its edge with the pads, its height over the
# board's top side.
BUCK = (-30.0, 1.5)


def extent(shapes):
    """The box round shapes: x0, y0, z0, x1, y1, z1."""
    points = [q for _k, pts, _e, _f, _rgb in s.items(shapes) for q in pts]
    return tuple(min(q[k] for q in points) for k in range(3)) + tuple(
        max(q[k] for q in points) for k in range(3))


def module():
    """The INA228 module, where its own spec draws it."""
    return (ina228_xt90.board() + ina228_xt90.top_side()
            + ina228_xt90.underside() + ina228_xt90.printed())


def buck():
    """The buck converter beside the plus bar, over the board."""
    shapes = matek_mbec12s.bare()
    box = extent(shapes)
    return s.place(shapes, x=BUCK[0] - box[0],
                   y=PLUS_BAR + BAR_GAP - box[1],
                   z=ina228_xt90.T + BUCK[1] - box[2])


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
    return {"module": module(), "buck": buck(), "display": display()}


def parts(_colors):
    """The layout as one model.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items([shape for shapes in layout().values()
                           for shape in shapes])}
