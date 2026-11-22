"""hardware/mechanical: the Matek mBEC12S and the sensor node's layout.

The converter is 18.0 x 15.0mm with 4 edge pads 2.54 apart and 4.5 high
over all. In the layout no part is changed, the converter lies beside
the plus bar and not over the current path, and the display clears the
connectors' housings.

SPDX-License-Identifier: MIT
"""

import sys
from collections import Counter
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _solid  # noqa: E402
import es3c28p  # noqa: E402
import ina228_node  # noqa: E402
import ina228_xt90  # noqa: E402
import matek_mbec12s  # noqa: E402

# The shunt with the minus bar and the plus bar with its solder, y.
CURRENT_PATH = (10.0, 28.5)


@pytest.mark.parametrize("spec,count", [(matek_mbec12s, 147),
                                        (ina228_node, 1139)])
def test_solids_close(spec, count):
    """Each edge of each solid is used once in each direction."""
    (items,) = spec.parts(None).values()
    assert len(items) == count
    for _kind, _points, edges, faces, _rgb in items:
        used = Counter((edge, forward) for _s, _same, loops in faces
                       for loop in loops for edge, forward in loop)
        assert all(used[(e, d)] == 1 for e in range(len(edges))
                   for d in (True, False))


def test_converter_has_the_listed_size():
    """18.0 x 15.0, 4.5 over all, origin at the pad OUT on the edge."""
    box = ina228_node.extent(matek_mbec12s.bare())
    assert box[3] - box[0] == pytest.approx(18.0)
    assert box[4] - box[1] == pytest.approx(15.0)
    assert box[5] - box[2] == pytest.approx(4.5, abs=0.1)
    assert box[0] == pytest.approx(0.0)


def test_converter_pads_are_a_tenth_inch_apart():
    """OUT, G, G and IN on one edge."""
    names = [pad[0] for pad in matek_mbec12s.PADS]
    places = [pad[1] for pad in matek_mbec12s.PADS]
    assert names == ["OUT", "G", "G", "IN"]
    assert [b - a for a, b in zip(places, places[1:],
                                  strict=False)] == pytest.approx(
        [2.54] * 3)


def test_layout_changes_no_part():
    """Each part has the solids and the size of its own spec."""
    parts = ina228_node.layout()
    own = {"module": ina228_node.module(), "buck": matek_mbec12s.bare(),
           "display": es3c28p.bare()}
    assert set(parts) == set(own)
    for name, shapes in parts.items():
        assert len(_solid.items(shapes)) == len(_solid.items(own[name]))
        box, ref = ina228_node.extent(shapes), ina228_node.extent(own[name])
        size = sorted(box[k + 3] - box[k] for k in range(2))
        assert size == pytest.approx(
            sorted(ref[k + 3] - ref[k] for k in range(2)))
        assert box[5] - box[2] == pytest.approx(ref[5] - ref[2])


def test_converter_is_beside_the_current_path():
    """Past the plus bar's solder, over the board, between the housings."""
    box = ina228_node.extent(ina228_node.buck())
    assert box[1] == pytest.approx(CURRENT_PATH[1] + ina228_node.BAR_GAP)
    assert box[4] <= ina228_node.BOARD[3]
    assert box[2] == pytest.approx(ina228_xt90.T + ina228_node.BUCK[1],
                                   abs=0.05)
    assert box[5] < ina228_node.HOUSING_TOP


def test_display_clears_the_housings():
    """Its lowest part is GAP over the connectors, centred on the board."""
    box = ina228_node.extent(ina228_node.display())
    assert box[2] == pytest.approx(ina228_node.HOUSING_TOP
                                   + ina228_node.GAP)
    board = ina228_node.BOARD
    assert (box[1] + box[4]) / 2 == pytest.approx((board[1] + board[3]) / 2)


def test_layout_has_its_size():
    """86.4 x 50.0 x 23.7 over all."""
    (items,) = ina228_node.parts(None).values()
    points = [q for _k, pts, _e, _f, _rgb in items for q in pts]
    size = [max(q[k] for q in points) - min(q[k] for q in points)
            for k in range(3)]
    assert size == pytest.approx([86.4, 50.0, 23.7], abs=0.05)
