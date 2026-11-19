"""hardware/mechanical: the standard servo is as large as its sketch.

Every solid closes, the model has the sketch's dimensions, the mounting
holes lie where the sketch puts them and stay open to the ears' ends,
and the print stands on both long sides.

SPDX-License-Identifier: MIT
"""

import sys
from collections import Counter
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _parts  # noqa: E402
import _solid  # noqa: E402
import servo_standard as servo  # noqa: E402


def extent(items):
    """The box round the points of brep items."""
    points = [q for _k, pts, _e, _f, _rgb in items for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def solids():
    """The model's solids."""
    (items,) = servo.parts(None).values()
    return items


def test_every_solid_closes():
    """Each edge of each solid is used once in each direction."""
    for _kind, _points, edges, faces, _rgb in solids():
        used = Counter((edge, forward) for _s, _same, loops in faces
                       for loop in loops for edge, forward in loop)
        assert all(used[(e, d)] == 1 for e in range(len(edges))
                   for d in (True, False))


def test_overall_size_is_the_sketch():
    """7.2 + 40.5 + 7.2 long, the horn's 21 wide, 44.1 high."""
    box = extent(solids())
    assert box[3] - box[0] == pytest.approx(7.2 + 40.5 + 7.2)
    assert box[0] == pytest.approx(-30.4 - 7.2)
    assert (box[1], box[4]) == pytest.approx((-10.5, 10.5))
    assert (box[2], box[5]) == pytest.approx((0.0, 44.1))


def test_case_and_ears():
    """The case is 40.5 x 20 x 36.3; the ears lie from 26.3 to 28.8."""
    shapes = servo.servo()
    case = extent(_solid.items(shapes[:3]))
    assert case == pytest.approx([-30.4, -10.0, 0.0, 10.1, 10.0, 36.3])
    ears = extent(_solid.items(shapes[3:5]))
    assert (ears[2], ears[5]) == pytest.approx((26.3, 28.8))


@pytest.mark.parametrize("side", [1, -1])
def test_mounting_holes(side):
    """Holes 49.5 and 10 apart, 5 across, each open to the ear's end."""
    outline = servo._ear(side)
    centres = sorted({c[2][:2] for c in outline if len(c) == 3})
    middle = 40.5 / 2 - 30.4
    assert [c[0] for c in centres] == pytest.approx(
        [middle + side * 49.5 / 2] * 2)
    assert [c[1] for c in centres] == pytest.approx([-5.0, 5.0])
    for cx, cy in centres:
        on = [c for c in outline if len(c) == 3 and c[2][:2] == (cx, cy)]
        assert all(abs(complex(c[0] - cx, c[1] - cy)) == pytest.approx(2.5)
                   for c in on)
    end = (10.1 + 7.2) if side > 0 else (-30.4 - 7.2)
    at_end = sorted(c[1] for c in outline if c[0] == pytest.approx(end))
    assert at_end == pytest.approx([-10.0, -6.3, -3.7, 3.7, 6.3, 10.0])


def test_horn():
    """The horn is 21 across and 2 thick, 5.8 over the case, 13 holes."""
    outer, holes, d0, d1, rgb, _frame = servo.servo()[-1]
    box = extent(_solid.items([(outer, [], d0, d1, rgb, _solid.Z)]))
    assert box == pytest.approx([-10.5, -10.5, 42.1, 10.5, 10.5, 44.1])
    assert len(holes) == 13
    assert rgb == _parts.WHITE


def test_print_stands_on_both_long_sides():
    """White letters, PRINT thick, outside y = -10 and y = +10."""
    letters = len(servo.label())
    assert letters == len("StandardServo")
    shapes = servo.servo()[5:5 + 2 * letters]
    assert all(shape[4] == _parts.WHITE for shape in shapes)
    near = extent(_solid.items(shapes[:letters]))
    far = extent(_solid.items(shapes[letters:]))
    assert (near[1], near[4]) == pytest.approx((-10.0 - _parts.PRINT, -10.0))
    assert (far[1], far[4]) == pytest.approx((10.0, 10.0 + _parts.PRINT))
    assert near[0] > -30.4 + 1.0 and near[3] < 10.1 - 1.0
    assert 3.7 < near[2] and near[5] < 22.5


def test_letters_do_not_touch():
    """Each letter's box ends before the next one's begins."""
    boxes = [extent(_solid.items([shape])) for shape in servo.label()]
    assert all(a[3] < b[0] for a, b in zip(boxes, boxes[1:], strict=False))
