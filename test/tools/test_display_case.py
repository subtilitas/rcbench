"""hardware/mechanical/_gen_display_case.py: the display node's XT30 case.

The 2 parts are valid solids in one piece of the documented size, they
share no volume with each other or with the display board, the stack
closes with the lid's flange on the box's rim, each insert has its hole
and relief, and every opening is open.

SPDX-License-Identifier: MIT
"""

import sys
from pathlib import Path

import pytest
from conftest import import_or_fail

import_or_fail("OCP", "needs OCP, the OpenCASCADE binding")

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _gen_display_case as case  # noqa: E402
import _models  # noqa: E402
from _gen_dial_base import _box, _cylinder, _extent, volume  # noqa: E402
from OCP.BRepAlgoAPI import BRepAlgoAPI_Common  # noqa: E402

BOX, LID = case.STEMS


@pytest.fixture(scope="module")
def built():
    """The 2 parts, built once."""
    return case.parts()


def filled(solid, tool):
    """The volume of a part inside a tool shape."""
    return volume(BRepAlgoAPI_Common(solid, tool).Shape())


def test_parts_fit_the_board(built):
    """Valid, each in one piece, apart, clear of the board's solids."""
    assert list(built) == list(case.STEMS)
    assert case.check(built) == []


def test_files_are_current(built):
    """The STEP files hold what the spec builds; each has its STL."""
    for stem, solid in built.items():
        assert _models.path(stem).read_text() == case.step_text(stem, solid)
        assert _models.path(stem, ".stl").stat().st_size > 10000
        assert _models.FOLDER[stem] == "casing"


def test_case_has_its_size(built):
    """59.8 x 101.2 outside; box 16.0 high, lid 1.8 with a plug of 4.2."""
    box, lid = _extent(built[BOX]), _extent(built[LID])
    for part in (box, lid):
        assert part[3] - part[0] == pytest.approx(59.8, abs=0.01)
        assert part[4] - part[1] == pytest.approx(101.2, abs=0.01)
    assert (box[2], box[5]) == pytest.approx((0.0, 16.0), abs=0.01)
    assert (lid[2], lid[5]) == pytest.approx((11.8, 17.8), abs=0.01)


def test_lid_closes_on_the_rim(built):
    """The flange meets the rim; nothing of the lid stops it earlier."""
    stack = case.stack()
    assert stack["play"] == pytest.approx(0.18, abs=0.001)
    assert stack["window"] == pytest.approx(0.5)
    assert stack["touch"] == pytest.approx(1.88, abs=0.001)
    assert stack["fold"] > 0.6
    assert stack["thread"] == pytest.approx(4.4)
    # The flange's underside and the rim's top are one plane, and they
    # touch all round: the box's wall has the lid over it everywhere.
    rim = _extent(built[BOX])[5]
    slab = _box(-40, -60, rim, 40, 60, rim + 0.1)
    wall = _box(-40, -60, rim - 0.1, 40, 60, rim)
    ring = filled(built[BOX], wall) / 0.1
    assert ring == pytest.approx(559.5, abs=1.0)
    inner = case.rounded(case.X0 + case.WALL, case.Y0 + case.WALL,
                         case.X1 - case.WALL, case.Y1 - case.WALL,
                         case.RADIUS - case.WALL, rim, rim + 0.1)
    over = (filled(built[LID], slab) - filled(built[LID], inner)) / 0.1
    assert over == pytest.approx(ring, abs=0.5)


def test_window_leaves_the_panel_room(built):
    """51.0 wide round a panel of 50.0, centred on the board."""
    x0, y0, x1, y1 = case.window()
    assert x1 - x0 == pytest.approx(51.0)
    assert (x0 + x1) / 2 == pytest.approx(case.BOARD_AT[0])
    assert y1 - y0 == pytest.approx(70.6, abs=0.01)
    gone = _box(x0, y0, 0.0, x1, y1, 30.0)
    assert filled(built[LID], gone) == pytest.approx(0.0, abs=1e-6)


def test_inserts_have_hole_and_relief(built):
    """A blind hole of 4.0 x 6.0, a relief of 4.6 x 0.5, a closed floor."""
    hole, deep, relief, relief_deep = case.INSERT
    top = case.BOSS_TOP
    assert len(case.holes()) == 4
    for x, y in case.holes():
        assert filled(built[BOX], _cylinder(
            x, y, hole / 2 - 0.01, top - deep, top)) == pytest.approx(
                0.0, abs=1e-6)
        assert filled(built[BOX], _cylinder(
            x, y, relief / 2 - 0.01, top - relief_deep, top)) == (
                pytest.approx(0.0, abs=1e-6))
        under = _cylinder(x, y, hole / 2, 0.0, top - deep)
        assert filled(built[BOX], under) == pytest.approx(volume(under),
                                                          rel=1e-3)
        ring = _cylinder(x, y, case.BOSS / 2 - 0.05, 5.0, 9.0)
        bore = _cylinder(x, y, hole / 2, 5.0, 9.0)
        assert filled(built[BOX], ring) == pytest.approx(
            volume(ring) - volume(bore), rel=1e-2)
        # The screw's hole in the lid, and its head's recess.
        assert filled(built[LID], _cylinder(
            x, y, case.SCREW[0] / 2 - 0.01, 0.0, 30.0)) == pytest.approx(
                0.0, abs=1e-6)
        assert filled(built[LID], _cylinder(
            x, y, case.SCREW[1] / 2 - 0.01, case.FRONT - case.SCREW[2],
            30.0)) == pytest.approx(0.0, abs=1e-6)


def test_xt30_openings_are_open(built):
    """One in each short wall, 11.2 x 5.6, with one round end."""
    wide, high, low = case.XT30
    for straight, y0 in ((case.XT30_AT[0], case.Y1 - case.WALL),
                         (case.XT30_AT[1], case.Y0)):
        flat = _box(straight - wide + high / 2, y0, low, straight,
                    y0 + case.WALL, low + high)
        assert filled(built[BOX], flat) == pytest.approx(0.0, abs=1e-6)
        corner = _box(straight - wide, y0, low, straight - wide + 0.3,
                      y0 + case.WALL, low + 0.3)
        assert filled(built[BOX], corner) > 0.0
        tip = _box(straight - wide + 0.02, y0, low + high / 2 - 0.1,
                   straight - wide + 0.3, y0 + case.WALL,
                   low + high / 2 + 0.1)
        assert filled(built[BOX], tip) == pytest.approx(0.0, abs=1e-6)


def test_board_is_where_the_holes_are():
    """The screw axes are the display board's holes, 42.0 x 78.0 apart."""
    xs = sorted({x for x, _y in case.holes()})
    ys = sorted({y for _x, y in case.holes()})
    assert xs[1] - xs[0] == pytest.approx(42.0)
    assert ys[1] - ys[0] == pytest.approx(78.0)
    assert case.at_board(*case.es3c28p.HOLES[0]) in [
        pytest.approx(hole) for hole in case.holes()]
