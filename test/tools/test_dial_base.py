"""hardware/mechanical/_gen_dial_base.py: the M5Stack Dial's base.

The two parts are valid solids of the documented size, the joint is
level at the belly with the bottom part's rim inside the top part, the
dial's hole and the screws lie where the constants put them, and the
cell's pocket leaves the cell its play.

SPDX-License-Identifier: MIT
"""

import sys
from pathlib import Path

import pytest
from conftest import import_or_fail

import_or_fail("OCP", "needs OCP, the OpenCASCADE binding")

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _gen_dial_base as base  # noqa: E402
from OCP.BRepAlgoAPI import (  # noqa: E402
    BRepAlgoAPI_Common, BRepAlgoAPI_Cut,
)
from OCP.BRepCheck import BRepCheck_Analyzer  # noqa: E402


@pytest.fixture(scope="module")
def built():
    """Both parts, built once."""
    return base.parts()


def test_parts_are_valid_and_apart(built):
    """Each part is one valid solid; the two share no volume."""
    for solid in built.values():
        assert BRepCheck_Analyzer(solid).IsValid()
        assert base.volume(solid) > 5000.0
    both = BRepAlgoAPI_Common(*built.values()).Shape()
    assert base.volume(both) == pytest.approx(0.0, abs=1e-6)


def test_every_flute_is_open(built):
    """No part has material in one of the 18 flutes."""
    flutes = base.flutes()
    assert len(flutes) == 18
    for solid in built.values():
        for flute in flutes:
            filled = BRepAlgoAPI_Common(solid, flute).Shape()
            assert base.volume(filled) == pytest.approx(0.0, abs=1e-3)
    # The flutes take material off: each part is lighter than its share
    # of the unfluted shell.
    assert base.check(built) == []


def test_rim_stands_on_the_wall(built):
    """The rim has material under it all round, not only at the screws.

    Under the rim's whole width a ledge joins it to the wall, at an
    angle between two screw columns as at one; each part is one piece.
    """
    rise, thick, gap = base.RIM
    outer = base.inner_radius(base.SPLIT + rise) - gap
    bottom = built["dial_base_bottom"]
    for angle in (0.0, 90.0, 200.0):
        probe = base._turned(base._box(
            outer - thick, -0.5, base.SPLIT - 1.0, outer, 0.5,
            base.SPLIT), angle)
        shared = base.volume(BRepAlgoAPI_Common(bottom, probe).Shape())
        assert shared == pytest.approx(thick * 1.0 * 1.0, rel=0.02)
        gap_probe = base._turned(base._box(
            outer, -0.5, base.SPLIT - 1.0, outer + 0.3, 0.5, base.SPLIT),
            angle)
        assert base.volume(
            BRepAlgoAPI_Common(bottom, gap_probe).Shape()) > 0.25
    assert [base.pieces(solid) for solid in built.values()] == [1, 1]


def test_overall_size(built):
    """72 across at the belly, 32.5 high, split 19.0 below the top."""
    top = base._extent(built["dial_base_top"])
    bottom = base._extent(built["dial_base_bottom"])
    assert (top[2], top[5]) == pytest.approx((-19.0, 0.0), abs=1e-3)
    assert bottom[2] == pytest.approx(-32.5, abs=1e-3)
    assert bottom[5] == pytest.approx(-19.0 + base.RIM[0], abs=1e-3)
    assert bottom[3] - bottom[0] == pytest.approx(72.0, abs=0.01)
    # The top part is widest just above its shadow gap.
    assert top[3] - top[0] == pytest.approx(71.88, abs=0.01)


def test_dial_fits_its_hole(built):
    """A 45.0 rod passes the top plate; a 45.4 one does not."""
    top = built["dial_base_top"]
    for radius, free in ((22.5, True), (22.7, False)):
        rod = base._cylinder(0, 0, radius, -base.PLATE, 0.0)
        shared = base.volume(BRepAlgoAPI_Common(top, rod).Shape())
        assert (shared < 1e-6) is free


def test_nut_has_room_under_the_plate():
    """4.2 between the 50.82 nut and the wall, 0.3 to each boss."""
    assert base.inner_radius(-base.PLATE) - 25.41 == pytest.approx(
        4.19, abs=0.01)
    assert base.SCREW_R - base.BOSS / 2 - 25.41 == pytest.approx(0.59)


def test_bosses_take_the_threaded_inserts(built):
    """A hole of 4.0, 5.0 deep, with 1.6 of wall, under the ring nut."""
    hole, deep, wall, length = base.INSERT
    assert (hole, deep, wall, length) == (4.0, 5.0, 1.6, 4.0)
    assert hole + 2 * wall == pytest.approx(7.2)
    assert deep >= length + 1.0
    # The wide part ends 1.0 under the nut, which reaches to z = -7.0.
    assert base.WIDE_TO == pytest.approx(-7.0 - 1.0)
    top = built["dial_base_top"]
    for x, y, _angle in base._screw_points():
        # The insert's place is empty and has its wall all round.
        insert = base._cylinder(x, y, hole / 2 - 0.01, base.SPLIT,
                                base.SPLIT + deep)
        shared = BRepAlgoAPI_Common(top, insert).Shape()
        assert base.volume(shared) == pytest.approx(0.0, abs=1e-6)
        ring = BRepAlgoAPI_Cut(
            base._cylinder(x, y, hole / 2 + wall - 0.01, base.SPLIT + 0.5,
                           base.SPLIT + deep),
            base._cylinder(x, y, hole / 2 + 0.01, base.SPLIT,
                           base.SPLIT + deep + 1)).Shape()
        held = BRepAlgoAPI_Common(top, ring).Shape()
        assert base.volume(held) == pytest.approx(base.volume(ring),
                                                  rel=1e-3)
    # The rim of the bottom part leaves each boss 0.2 of room.
    both = BRepAlgoAPI_Common(*built.values()).Shape()
    assert base.volume(both) == pytest.approx(0.0, abs=1e-6)


def test_screws_reach_through_their_inserts(built):
    """An M3 x 12 from its seat passes the insert and ends in the relief."""
    tip = base.SEAT + 12.0
    assert base.SPLIT + base.INSERT[3] < tip < base.RELIEF[1]
    assert tip - base.SPLIT == pytest.approx(6.5)
    assert base.RELIEF[0] > 3.0
    x, y, _angle = base._screw_points()[0]
    shank = base._cylinder(x, y, 1.2, -base.HEIGHT, tip)
    for solid in built.values():
        assert base.volume(
            BRepAlgoAPI_Common(solid, shank).Shape()) < 1e-6


def test_cell_pocket(built):
    """The 40.5 x 25 x 10 cell lies on the floor, 1.2 free at each end."""
    length, width, play, _side, _rail, _high = base.CELL
    cell = base._box(-length / 2, -width / 2, base.FLOOR, length / 2,
                     width / 2, base.FLOOR + 10.0)
    snug = base._box(-length / 2 - play - 0.1, -4.0, base.FLOOR,
                     length / 2 + play + 0.1, 4.0, base.FLOOR + 1.0)
    bottom = built["dial_base_bottom"]
    assert base.volume(BRepAlgoAPI_Common(bottom, cell).Shape()) < 1e-6
    assert base.volume(BRepAlgoAPI_Common(bottom, snug).Shape()) > 0.1
    assert base.FLOOR + 10.0 < -17.7 - 2.0


def test_usb_tube_guides_the_plug(built):
    """A 12.4 x 6.5 plug passes the tube to 20 from the axis.

    The tube's wall stands round it from the outer wall in to 24.5
    from the axis, 2.5 off the dial's thread, and stays 0.33 under the
    ring nut.
    """
    _width, height, z, _corner = base.USB
    wall, near, _tie = base.TUBE
    top = built["dial_base_top"]
    plug = base._box(-6.2, -60.0, z - 3.25, 6.2, -20.0, z + 3.25)
    assert base.volume(BRepAlgoAPI_Common(top, plug).Shape()) < 1e-6
    for y, there in ((-near - 0.5, True), (-near + 0.5, False)):
        roof = base._box(-2.0, y - 0.2, z + height / 2, 2.0, y + 0.2,
                         z + height / 2 + wall)
        got = base.volume(BRepAlgoAPI_Common(top, roof).Shape())
        assert (got > 1.5) is there
    assert -7.0 - (z + height / 2 + wall) == pytest.approx(0.33)
    assert near - 22.0 == pytest.approx(2.5)


def test_floor_is_a_wall_thick():
    """The floor's upper face is 2.4 over the foot."""
    assert base.FLOOR == pytest.approx(-32.5 + 2.4)
    assert base.inner_radius(base.FLOOR) > 24.3
