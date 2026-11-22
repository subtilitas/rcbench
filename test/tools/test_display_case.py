"""hardware/mechanical/_gen_display_case.py: the display node's cases.

Of each variant, XT30 and XT60, the 2 parts are valid solids in one
piece of the documented size, they share no volume with each other,
with the display board or with a connector, the stack closes with the
lid's flange on the box's rim, each insert has its hole and relief, and
every opening is open. The XT60 seat has the sizes of the front case's.

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

XT30, XT60 = case.XT30_CASE, case.XT60_CASE
VARIANTS = pytest.mark.parametrize("one", case.CASES,
                                   ids=("xt30", "xt60"))


@pytest.fixture(scope="module")
def built():
    """The 4 parts, built once."""
    return case.parts()


def filled(solid, tool):
    """The volume of a part inside a tool shape."""
    return volume(BRepAlgoAPI_Common(solid, tool).Shape())


def test_parts_fit_board_and_connectors(built):
    """Valid, each in one piece, apart, clear of board and connectors."""
    assert list(built) == list(case.STEMS)
    assert len(built) == 4
    assert case.check(built) == []


def test_files_are_current(built):
    """The STEP files hold what the spec builds; each has its STL."""
    for stem, solid in built.items():
        assert _models.path(stem).read_text() == case.step_text(stem, solid)
        assert _models.path(stem, ".stl").stat().st_size > 10000
        assert _models.FOLDER[stem] == "casing"


def test_cases_have_their_sizes(built):
    """59.8 wide; XT30 101.2 x 17.8, XT60 105.6 x 24.4."""
    for one, long, rim, front in ((XT30, 101.2, 16.0, 17.8),
                                  (XT60, 105.6, 22.6, 24.4)):
        box, lid = (_extent(built[stem]) for stem in one.stems)
        for part in (box, lid):
            assert part[3] - part[0] == pytest.approx(59.8, abs=0.01)
            assert part[4] - part[1] == pytest.approx(long, abs=0.01)
        assert (box[2], box[5]) == pytest.approx((0.0, rim), abs=0.01)
        assert (lid[2], lid[5]) == pytest.approx((rim - 4.2, front),
                                                 abs=0.01)


@VARIANTS
def test_lid_closes_on_the_rim(built, one):
    """The flange meets the rim all round; the plug ends over the board."""
    stack = case.stack(one)
    assert stack["play"] == pytest.approx(0.18, abs=0.001)
    assert stack["window"] == pytest.approx(0.5)
    assert stack["touch"] == pytest.approx(1.88, abs=0.001)
    assert stack["fold"] > 0.6
    assert stack["thread"] == pytest.approx(4.4)
    box, lid = (built[stem] for stem in one.stems)
    rim = one.rim
    slab = _box(-40, -60, rim, 40, 60, rim + 0.1)
    ring = filled(box, _box(-40, -60, rim - 0.1, 40, 60, rim)) / 0.1
    inner = case.rounded(case.X0 + case.WALL, one.y0 + one.end,
                         case.X1 - case.WALL, one.y1 - one.end,
                         case.RADIUS - case.WALL, rim, rim + 0.1)
    over = (filled(lid, slab) - filled(lid, inner)) / 0.1
    assert ring > 500.0
    assert over == pytest.approx(ring, abs=0.5)


@VARIANTS
def test_window_leaves_the_panel_room(built, one):
    """51.0 wide round a panel of 50.0, centred on the board."""
    x0, y0, x1, y1 = case.window()
    assert x1 - x0 == pytest.approx(51.0)
    assert (x0 + x1) / 2 == pytest.approx(case.BOARD_AT[0])
    assert y1 - y0 == pytest.approx(70.6, abs=0.01)
    gone = _box(x0, y0, 0.0, x1, y1, 30.0)
    assert filled(built[one.stems[1]], gone) == pytest.approx(0.0, abs=1e-6)


@VARIANTS
def test_inserts_have_hole_and_relief(built, one):
    """A blind hole of 4.0 x 6.0, a relief of 4.6 x 0.5, a closed floor."""
    hole, deep, relief, relief_deep = case.INSERT
    top = one.boss_top
    box, lid = (built[stem] for stem in one.stems)
    assert len(case.holes()) == 4
    for x, y in case.holes():
        assert filled(box, _cylinder(
            x, y, hole / 2 - 0.01, top - deep, top)) == pytest.approx(
                0.0, abs=1e-6)
        assert filled(box, _cylinder(
            x, y, relief / 2 - 0.01, top - relief_deep, top)) == (
                pytest.approx(0.0, abs=1e-6))
        under = _cylinder(x, y, hole / 2, 0.0, top - deep)
        assert filled(box, under) == pytest.approx(volume(under), rel=1e-3)
        assert filled(lid, _cylinder(
            x, y, case.SCREW[0] / 2 - 0.01, 0.0, 40.0)) == pytest.approx(
                0.0, abs=1e-6)
        assert filled(lid, _cylinder(
            x, y, case.SCREW[1] / 2 - 0.01, one.front - case.SCREW[2],
            40.0)) == pytest.approx(0.0, abs=1e-6)


def test_xt30_openings_are_open(built):
    """One in each short wall, 11.2 x 5.6, with one round end."""
    wide, high, low = case.XT30
    box = built[XT30.stems[0]]
    for straight, y0 in ((case.XT30_AT[0], XT30.y1 - XT30.end),
                         (case.XT30_AT[1], XT30.y0)):
        flat = _box(straight - wide + high / 2, y0, low, straight,
                    y0 + XT30.end, low + high)
        assert filled(box, flat) == pytest.approx(0.0, abs=1e-6)
        corner = _box(straight - wide, y0, low, straight - wide + 0.3,
                      y0 + XT30.end, low + 0.3)
        assert filled(box, corner) > 0.0
        tip = _box(straight - wide + 0.02, y0, low + high / 2 - 0.1,
                   straight - wide + 0.3, y0 + XT30.end,
                   low + high / 2 + 0.1)
        assert filled(box, tip) == pytest.approx(0.0, abs=1e-6)


def test_xt60_seat_is_the_front_cases(built):
    """Wall 3.0, recess 2.0 in the flange's outline, opening, 2 bosses."""
    box = built[XT60.stems[0]]
    x, z = case.BOARD_AT[0], XT60.connector
    assert XT60.end == pytest.approx(3.0)
    for side, face in ((1, XT60.y1), (-1, XT60.y0)):
        def along(a, b, side=side, face=face):
            return sorted((face - side * a, face - side * b))

        # The recess: 34.7 x 13.3 at the ears, 20.7 x 17.3 in the middle.
        y0, y1 = along(0.0, 2.0)
        for wide, high in ((34.7, 13.3), (20.7, 17.3)):
            room = _box(x - wide / 2 + 0.01, y0, z - high / 2 + 0.01,
                        x + wide / 2 - 0.01, y1, z + high / 2 - 0.01)
            assert filled(box, room) == pytest.approx(0.0, abs=1e-6)
        # The wall stands beside the ears and over the middle's corners.
        y0, y1 = along(0.1, 1.9)
        assert filled(box, _box(x + 17.5, y0, z - 1, x + 18.5, y1,
                                z + 1)) == pytest.approx(1.8 * 2, rel=1e-3)
        assert filled(box, _box(x + 15.5, y0, z + 7.6, x + 16.5, y1,
                                z + 8.6)) == pytest.approx(1.8, rel=1e-3)
        # 1.0 of wall is left behind the recess, with the body's opening.
        y0, y1 = along(2.0, 3.0)
        skin = _box(x - 17, y0, z - 6.6, x - 16, y1, z - 5.6)
        assert filled(box, skin) == pytest.approx(1.0, rel=1e-3)
        body = _box(x - 9.79 + (3.0 if side < 0 else 0), y0, z - 6.19,
                    x + 9.79 - (3.0 if side > 0 else 0), y1, z + 6.19)
        assert filled(box, body) == pytest.approx(0.0, abs=1e-6)
        # 2 of the opening's corners are round: material in them.
        corner = _box(x + side * 9.3, y0, z + 5.7, x + side * 9.8, y1,
                      z + 6.2)
        assert filled(box, corner) > 0.02
        # The bosses, 25.6 apart, 4.0 behind the wall, bored 4.0.
        y0, y1 = along(3.0, 7.0)
        for k in (-1, 1):
            u = x + k * 12.8
            ring = _box(u + k * 2.3, y0, z - 0.5, u + k * 3.7, y1, z + 0.5)
            assert filled(box, ring) == pytest.approx(1.4 * 4.0, rel=1e-2)
            b0, b1 = along(2.1, 6.9)
            bore = _box(u - 1.2, b0, z - 1.2, u + 1.2, b1, z + 1.2)
            assert filled(box, bore) == pytest.approx(0.0, abs=1e-6)


def test_xt60_bodies_lie_under_the_board():
    """The flange stands 0.5 out; body and contacts end before the USB."""
    plugs = case.connectors(XT60)
    assert len(plugs) == 8
    assert case.connectors(XT30) == []
    box = [_extent(plug) for plug in plugs]
    assert max(b[4] for b in box) == pytest.approx(XT60.y1 + 0.5)
    assert min(b[1] for b in box) == pytest.approx(XT60.y0 - 0.5)
    assert max(b[5] for b in box[2:4]) < XT60.board_back - 0.9
    # At the USB socket's end the body ends 1.4 before the board.
    board_end = case.BOARD_AT[1] - case.BOARD[1] / 2
    assert board_end - box[6][4] == pytest.approx(1.4, abs=0.01)


def test_board_is_where_the_holes_are():
    """The screw axes are the display board's holes, 42.0 x 78.0 apart."""
    xs = sorted({x for x, _y in case.holes()})
    ys = sorted({y for _x, y in case.holes()})
    assert xs[1] - xs[0] == pytest.approx(42.0)
    assert ys[1] - ys[0] == pytest.approx(78.0)
    assert case.at_board(*case.es3c28p.HOLES[0]) in [
        pytest.approx(hole) for hole in case.holes()]
