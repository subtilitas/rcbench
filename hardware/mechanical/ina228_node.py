"""Layout of the remote INA228 sensor node: its boards and cell in place.

    ina228_node_layout   the INA228 module with its 2 XT90PW connectors
                         at the bottom, the 1000mAh cell and the charger
                         board over it, the ES3C28P display on top

A study of where the parts go, before a case is drawn round them. No
part is changed: each is the model of its own spec, moved and turned.

    ina228_xt90.py              the INA228 module, in its own frame
    lipo_cells.py               the cell 102540, 25.0 x 40.5 x 10.0,
                                without its wires and plug, which bend
                                in the node
    lipo_charger_boost_5v.py    the charger board with 5V boost output
    es3c28p.py                  the display board

Chosen (owner, 2026-11-21: INA228 module at the bottom, display on top,
the 1000mAh cell and the charger board for power):

- The cell lies across the INA228 board between the 2 connectors'
  tails, 0.2 over the shunt, clear of the 4-pin header.
- The charger board lies 0.2 over the socket half's housing.
- The display's long side runs along the INA228 board's, the two
  boards' centers over each other, its USB Type-C socket at the socket
  half's end. Its lowest part is GAP over the charger board's highest.

Not drawn: wires, the cell's leads and plug, anything that holds the
parts; they float where a case will hold them.

Frame: the INA228 module's. Origin at its header's pin 1 on the board's
underside, x along the header and the board's long side, z up. All
sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _solid as s
import es3c28p
import ina228_xt90
import lipo_cells
import lipo_charger_boost_5v as charger

STEM = "ina228_node_layout"
GAP = 0.5               # between the display's parts and what lies under
CLEAR = 0.2             # between the cell and the shunt, the charger
#                         board and the housing
BOARD = (-56.16, -4.2, 23.94, 44.4)     # the INA228 board, x0, y0, x1, y1
SHUNT_TOP = ina228_xt90.T + ina228_xt90.SHUNT[2]
HOUSING_TOP = 11.5      # the connectors' upper face over the underside
SOCKET = (5.99, 9.35, 24.99, 30.15)     # the socket half, seen from above
CELL_X = -19.3          # the cell's middle along the board


def extent(shapes):
    """The box round shapes: x0, y0, z0, x1, y1, z1."""
    points = [q for _k, pts, _e, _f, _rgb in s.items(shapes) for q in pts]
    return tuple(min(q[k] for q in points) for k in range(3)) + tuple(
        max(q[k] for q in points) for k in range(3))


def module():
    """The INA228 module, where its own spec draws it."""
    return (ina228_xt90.board() + ina228_xt90.top_side()
            + ina228_xt90.underside() + ina228_xt90.printed())


def cell():
    """The cell across the board, 0.2 over the shunt."""
    body = lipo_cells.cell("lipo_102540_1000mah")[:-3]
    box = extent(body)
    middle = (BOARD[1] + BOARD[3]) / 2
    return s.place(body, x=CELL_X - (box[0] + box[3]) / 2,
                   y=middle - (box[1] + box[4]) / 2,
                   z=SHUNT_TOP + CLEAR - box[2])


def charger_board():
    """The charger board over the socket half's housing."""
    shapes = charger.board() + charger.top_side() + charger.underside()
    box = extent(charger.board()[:1])
    return s.place(
        shapes, x=(SOCKET[0] + SOCKET[2]) / 2 - (box[0] + box[3]) / 2,
        y=(SOCKET[1] + SOCKET[3]) / 2 - (box[1] + box[4]) / 2,
        z=HOUSING_TOP + CLEAR - extent(shapes)[2])


def display():
    """The display over everything, its long side along the board's."""
    shapes = s.place(es3c28p.bare(), turn=90.0)
    box = extent(es3c28p.board()[:1])
    # Turned 90 degrees, the board's (x, y) is (-y, x).
    cx, cy = -(box[1] + box[4]) / 2, (box[0] + box[3]) / 2
    under = max(extent(charger_board())[5], extent(cell())[5], HOUSING_TOP)
    return s.place(shapes, x=(BOARD[0] + BOARD[2]) / 2 - cx,
                   y=(BOARD[1] + BOARD[3]) / 2 - cy,
                   z=under + GAP - extent(shapes)[2])


def layout():
    """Every part in place.

    Returns:
        A dict of name to shapes.
    """
    return {"module": module(), "cell": cell(), "charger": charger_board(),
            "display": display()}


def parts(_colors):
    """The layout as one model.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items([shape for shapes in layout().values()
                           for shape in shapes])}
