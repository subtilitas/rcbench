"""hardware/mechanical/_solid.py, _parts.py and panel_7b.py: the panel board.

A shape of _solid.py becomes a closed shell: every edge is used once in
each direction, with straight sides, arcs and holes, turned and turned
over. The panel board's model is closed solid by solid, is as large as
Waveshare's drawing gives the glass, and carries the wire sockets the
board has. The measuring tool sorts solids into chip parts, named
parts, leaded packages and boxes.

SPDX-License-Identifier: MIT
"""

import sys
from collections import Counter
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
sys.path.insert(0, str(ROOT / "hardware" / "kicad" / "tools"))
import _parts  # noqa: E402
import _solid  # noqa: E402
import panel_7b  # noqa: E402

GREY = (0.5, 0.5, 0.5)


def open_edges(item):
    """The edges of a brep item not used once in each direction."""
    _kind, _points, edges, faces, _rgb = item
    used = Counter()
    for _surface, _same, loops in faces:
        for loop in loops:
            for edge, forward in loop:
                used[(edge, forward)] += 1
    return [e for e in range(len(edges))
            if used[(e, True)] != 1 or used[(e, False)] != 1]


def extent(items):
    """The box round the points of brep items."""
    points = [q for _k, pts, _e, _f, _rgb in items for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def test_shapes_close():
    """Straight sides, arcs, holes and the three frames give closed shells."""
    shapes = [
        _solid.ext(_solid.rounded(0, 0, 10, 6, 1), 0, 2, GREY,
                   holes=[_solid.circle(3, 3, 1), _solid.rect(6, 2, 8, 4)]),
        _solid.ext(_solid.stadium(0, 0, 8, 3), 5, 9, GREY, frame=_solid.Y),
        _solid.ext(_solid.strip([(0, 0), (5, 0), (6, 3)], 0.2), 1, 4, GREY,
                   frame=_solid.X),
    ]
    shapes += _solid.place(shapes, 40, 3, -2, turn=30, flip=True)
    for item in _solid.items(shapes):
        assert open_edges(item) == []


def test_a_hole_given_either_way_round():
    """A hole listed counter-clockwise is the same hole."""
    hole = _solid.rect(1, 1, 2, 2)
    one = _solid.items([_solid.ext(_solid.rect(0, 0, 4, 4), 0, 1, GREY,
                                   holes=[hole])])
    other = _solid.items([_solid.ext(_solid.rect(0, 0, 4, 4), 0, 1, GREY,
                                     holes=[hole[::-1]])])
    assert open_edges(one[0]) == [] and open_edges(other[0]) == []
    assert sorted(one[0][1]) == sorted(other[0][1])


def test_place_turns_then_flips_then_moves():
    """Turning over maps (x, y, z) to (-x, y, -z) before the move."""
    shape = _solid.box(1, 2, 3, 4, 5, 6, GREY)
    moved = _solid.items(_solid.place([shape], 10, 20, 30, flip=True))
    assert extent(moved) == pytest.approx([7, 22, 24, 9, 24, 25])
    turned = _solid.items(_solid.place([shape], turn=90))
    assert extent(turned) == pytest.approx([-4, 1, 5, -2, 3, 6])


def test_strip_keeps_its_thickness_round_a_corner():
    """A band's two sides stay the thickness apart on both legs."""
    outline = _solid.strip([(0, 0), (10, 0), (10, 10)], 0.4)
    xs, ys = [q[0] for q in outline], [q[1] for q in outline]
    assert (min(ys), max(xs)) == pytest.approx((-0.2, 10.2))
    assert (9.8, 0.2) in [(round(x, 6), round(y, 6)) for x, y in outline]


def test_wire_socket_sizes():
    """A side-entry socket is 2 x (pins - 1) + 5.9 wide and 5.5 high."""
    for pins in (2, 4, 12):
        box = extent(_solid.items(_parts.ph_side(pins)))
        tabs = 2 * 0.7
        assert box[3] - box[0] == pytest.approx(2 * (pins - 1) + 5.9 + tabs)
        assert box[5] == pytest.approx(5.5)
        assert box[4] == pytest.approx(2.9 + 2.2)


def test_panel_solids_close():
    """Every solid of the panel board is a closed shell."""
    solids = panel_7b.board()
    assert len(solids) > 1300
    assert [n for n, item in enumerate(solids) if open_edges(item)] == []


def test_panel_extent():
    """164.28 x 99.18 glass, 1.76 of switch lever past it, 15.1 thick."""
    box = extent(panel_7b.board())
    assert box[3] - box[0] == pytest.approx(166.04, abs=0.01)
    assert box[4] - box[1] == pytest.approx(99.18, abs=0.01)
    assert box[5] == pytest.approx(0.0)
    assert box[2] == pytest.approx(-15.1, abs=0.01)


def test_panel_frame():
    """The view area is centred on the origin on the glass's front."""
    x0, y0, x1, y1 = panel_7b.VIEW
    assert x1 - x0 == pytest.approx(154.58) and y1 - y0 == pytest.approx(
        86.41)
    border = _solid.items(_solid.place(_solid.place(
        panel_7b.stack()[:1], -(x0 + x1) / 2, -(y0 + y1) / 2,
        -panel_7b.LENS_Z[0]), flip=True))[0]
    hole = [q for q in border[1] if abs(abs(q[0]) - 77.29) < 0.01]
    assert len(hole) == 8 and max(q[2] for q in border[1]) == 0.0


def test_panel_wire_sockets():
    """The board has 4 sockets of 4 pins, 2 of 12 and 1 of 2, 2.0 pitch."""
    assert Counter(s[0] for s in panel_7b.WIRE_SOCKETS) == {4: 4, 12: 2, 2: 1}
    left = sorted(s[2] for s in panel_7b.WIRE_SOCKETS if s[0] == 4)
    assert [round(b - a, 1) for a, b in zip(left, left[1:], strict=False)] \
        == [14.9, 14.9, 14.9]


def solid(box, fill=1.0, rgb=None, flats=()):
    """A solid as _measure_step.measure returns it."""
    return {"box": box, "fill": fill, "rgb": rgb, "flats": list(flats)}


def test_measured_solids_are_sorted_by_kind():
    """Chip, named, wrong, leaded and plain solids go where they belong."""
    measure = pytest.importorskip(
        "_measure_step", reason="needs OCP, the OpenCASCADE binding")
    chip = [solid((0.0, 0.0, 0.0, 0.35, 0.8, 0.8)),
            solid((0.35, 0.0, 0.0, 1.25, 0.8, 0.8)),
            solid((1.25, 0.0, 0.0, 1.6, 0.8, 0.8))]
    standoff = [solid((10.0, 10.0, -1.5, 15.5, 15.5, 6.0), fill=0.5)]
    shroud = [solid((20.0, 20.0, 0.0, 34.35, 23.17, 4.4), fill=0.4)]
    body = (40.0, 40.0, 0.1, 41.6, 42.9, 1.1)
    package = [solid((39.3, 40.0, 0.0, 42.3, 42.9, 1.1), fill=0.5, flats=[
        (1.1, 4.6, body),
        (0.0, 0.2, (39.3, 41.2, 0.0, 39.8, 41.7, 0.0)),
        (0.0, 0.2, (41.8, 40.3, 0.0, 42.3, 40.8, 0.0))])]
    # A hollow can with a part inside it: the part's box is left out.
    block = [solid((50.0, 50.0, 0.0, 53.0, 53.0, 1.0), fill=0.5,
                   rgb=(0.1, 0.1, 0.1)),
             solid((50.5, 50.5, 0.2, 52.5, 52.5, 0.8))]
    chips, named, leaded, boxes = measure.classify(
        chip + standoff + shroud + package + block)
    assert chips == [("c0603", 0.8, 0.4, 0)]
    assert list(named) == ["standoff"]
    assert len(leaded) == 1 and len(leaded[0][1]) == 2
    assert leaded[0][0] == body
    assert boxes == [((50.0, 50.0, 0.0, 53.0, 53.0, 1.0), "epoxy")]
