"""hardware/mechanical: the KST X10 and its frame in a slice of wing.

The servo has the drawing's sizes. Every solid of the assembly is valid
and no two share volume, the frame's screws meet the servo's holes, the
strut's bush lies on the shaft's axis, the plate follows the upper
skin, the horn's eye lies above the flap, and the rod turns the flap both
ways.

SPDX-License-Identifier: MIT
"""

import math
import sys
from pathlib import Path

import pytest
from conftest import import_or_fail

import_or_fail("OCP", "needs OCP, the OpenCASCADE binding")

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _gen_flap_drive as drive  # noqa: E402
import _solid  # noqa: E402
import kst_x10  # noqa: E402


def extent(items):
    """The box round the points of brep items."""
    points = [q for _k, pts, _e, _f, _rgb in items for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


@pytest.fixture(scope="module")
def built():
    """Every part, built once."""
    return drive.parts()


def named(items, name):
    """The one shape of that name."""
    (shape,) = [s for n, s, _c in items if n == name]
    return shape


def test_servo_has_the_drawings_sizes():
    """Case 30 x 35.5 x 10, 42 over the ears, tab to 41.9, shaft 3.3."""
    shapes = kst_x10.servo()
    assert extent(_solid.items(shapes[:3])) == pytest.approx(
        [-6.6, -35.5, 0.0, 23.4, 0.0, 10.0])
    ears = extent(_solid.items(shapes[3:5]))
    assert (ears[0], ears[3]) == pytest.approx((-12.6, 29.4))
    assert (ears[1], ears[4], ears[5]) == pytest.approx((-8.0, 0.0, 1.8))
    tab = extent(_solid.items(shapes[5:6]))
    assert (tab[3] - tab[0], tab[1]) == pytest.approx((10.1, -41.9))
    shaft = extent(_solid.items(shapes[7:8]))
    assert (shaft[4], (shaft[2] + shaft[5]) / 2) == pytest.approx((3.3, 5.0))


def test_servo_holes():
    """Ear holes 36 apart, 29.4 - 3 from the shaft; tab hole 39.2 down."""
    left, right, rear = kst_x10.hole_places()
    assert right[0] - left[0] == pytest.approx(36.0)
    assert right[0] == pytest.approx(29.4 - 3.0)
    assert rear == pytest.approx((8.4, -39.2))


def test_assembly_is_sound(built):
    """Valid solids in one piece each, no shared volume."""
    assert drive.check(built["flap_drive_assembly"]) == []
    assert len(built["flap_drive"]) == 23
    assert len(built["flap_drive_wing"]) == 6


def test_horn_stands_out_of_the_flap(built):
    """The plates reach from the lower skin to above the upper surface."""
    for i in (1, 2):
        box = drive.extent(named(built["flap_drive"], f"horn plate {i}"))
        assert box[2] == pytest.approx(
            drive.inside(drive.HINGE + 5.5)[0] + 0.2, abs=0.05)
        assert box[5] > drive.height(drive.UPPER, drive.HINGE) + 5.0


def test_screws_meet_the_servos_holes():
    """Each screw's thread runs through a hole of the servo and the plate."""
    parts = drive.frame()
    for i, (x, y) in enumerate(kst_x10.hole_places()):
        box = drive.extent(named(parts, f"screw {i + 1}"))
        assert ((box[0] + box[3]) / 2, (box[1] + box[4]) / 2) == (
            pytest.approx((x, y)))
        assert box[2] == pytest.approx(-drive.PLATE)
        assert drive.SCREW[0] < kst_x10.HOLE


def test_bush_lies_on_the_shafts_axis():
    """The bush is round the axis, outside the arm's outer cheek."""
    box = drive.extent(named(drive.frame(), "bush"))
    assert ((box[0] + box[3]) / 2, (box[2] + box[5]) / 2) == pytest.approx(
        (0.0, kst_x10.THICK / 2))
    assert box[1] > drive.ARM[8]


def test_plate_follows_the_upper_skin(built):
    """The plate is BED under the skin at the nearest point."""
    move, slope = drive.setting()
    gaps = [drive.inside(x)[1] - z for x, _y, z in (
        drive._at(move, u, 0.0, -drive.PLATE) for u in range(-15, 33))]
    assert min(gaps) == pytest.approx(drive.BED, abs=0.02)
    assert max(gaps) < 0.8
    assert -3.0 < slope < 0.0


def test_joints():
    """Arm 7.0 down; the eye 3.5 over the flap, 1.0 behind the hinge."""
    j = drive.joints()
    assert math.dist(j["shaft"], j["pin"]) == pytest.approx(7.0)
    assert j["pin"][1] < j["shaft"][1]
    x = j["hinge"][0] + 1.0
    assert j["eye"] == pytest.approx((x, drive.height(drive.UPPER, x) + 3.5))
    assert j["eye"][1] - j["hinge"][1] == pytest.approx(12.9, abs=0.1)
    assert j["hinge"][0] == pytest.approx(0.75 * drive.CHORD)


def test_travel():
    """The flap follows the arm: down for a push, up for a pull."""
    assert drive.travel(0.0) == 0.0
    down, up = drive.travel(45.0), drive.travel(-45.0)
    assert down == pytest.approx(25.5, abs=0.3)
    assert up == pytest.approx(-20.3, abs=0.3)
    angles = [drive.travel(a) for a in range(-60, 61, 5)]
    assert all(a < b for a, b in zip(angles, angles[1:], strict=False))
