"""hardware/mechanical: the WitMotion sensor is as large as its picture.

Every solid closes, the case has the picture's three sizes and its base
thickness, the slots lie in the ears, and the label lies on the body's
face inside its edge.

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
import wit_imu  # noqa: E402


def extent(items):
    """The box round the points of brep items."""
    points = [q for _k, pts, _e, _f, _rgb in items for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def test_every_solid_closes():
    """Each edge of each solid is used once in each direction."""
    (items,) = wit_imu.parts(None).values()
    assert len(items) == 51
    for _kind, _points, edges, faces, _rgb in items:
        used = Counter((edge, forward) for _s, _same, loops in faces
                       for loop in loops for edge, forward in loop)
        assert all(used[(e, d)] == 1 for e in range(len(edges))
                   for d in (True, False))


def test_case_has_the_pictures_sizes():
    """36.1 wide, 51.5 over the ears, 15 high, base 3 thick."""
    base, body = wit_imu.sensor()[:2]
    assert extent(_solid.items([base])) == pytest.approx(
        [-18.05, -25.75, 0.0, 18.05, 25.75, 3.0])
    assert extent(_solid.items([body])) == pytest.approx(
        [-18.05, -18.05, 3.0, 18.05, 18.05, 15.0])


def test_slots_lie_in_the_ears():
    """Each slot is past the body and inside the ear's end and width."""
    length, width, _middle = wit_imu.SLOT
    for x, y in wit_imu.slots():
        assert x == 0.0
        assert abs(y) - width / 2 > wit_imu.BODY / 2
        assert abs(y) + width / 2 < wit_imu.LENGTH / 2 - 1.0
        assert length < wit_imu.EAR[1] - 4.0


def test_label_lies_on_the_face():
    """The label and its print stay on the body's face, 3 layers high."""
    shapes = wit_imu.sensor()[2:]
    box = extent(_solid.items(shapes))
    assert (box[2], box[5]) == pytest.approx(
        (15.0, 15.0 + 2 * _parts.PRINT))
    assert box[:2] == pytest.approx([-15.5, -15.5])
    assert box[3:5] == pytest.approx([15.5, 15.5])
    marks = extent(_solid.items(shapes[2:]))
    assert max(abs(v) for v in marks[:2] + marks[3:5]) < 14.4
