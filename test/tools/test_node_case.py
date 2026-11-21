"""hardware/mechanical/_gen_node_case.py: the sensor node's case.

The 3 parts are valid solids in one piece of the documented size, they
share no volume with each other or with a solid of the layout, each
screw's hole runs through its part onto an insert's hole in a column,
and every opening is open.

SPDX-License-Identifier: MIT
"""

import sys
from pathlib import Path

import pytest
from conftest import import_or_fail

import_or_fail("OCP", "needs OCP, the OpenCASCADE binding")

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _gen_node_case as case  # noqa: E402
import ina228_node  # noqa: E402
from _gen_dial_base import _box, _cylinder, _extent, volume  # noqa: E402
from OCP.BRepAlgoAPI import BRepAlgoAPI_Common  # noqa: E402

PLATE, FRAME, BEZEL = case.STEMS


@pytest.fixture(scope="module")
def built():
    """The 3 parts, built once."""
    return case.parts()


def filled(solid, tool):
    """The volume of a part inside a tool shape."""
    return volume(BRepAlgoAPI_Common(solid, tool).Shape())


def test_parts_fit_the_layout(built):
    """Valid, each in one piece, apart, clear of every board's solid."""
    assert list(built) == list(case.STEMS)
    assert case.check(built) == []


def test_files_are_current(built):
    """The STEP files hold what the spec builds."""
    for stem, solid in built.items():
        path = case.MODELS / f"{stem}.step"
        assert path.read_text() == case.step_text(stem, solid)
        assert (case.MODELS / f"{stem}.stl").stat().st_size > 10000


def test_case_has_its_size(built):
    """91.4 x 55.4 outside, 28.0 high: plate 2.4, frame 18.3, bezel 7.3."""
    boxes = {stem: _extent(solid) for stem, solid in built.items()}
    for box in boxes.values():
        assert box[3] - box[0] == pytest.approx(91.4, abs=0.01)
        assert box[4] - box[1] == pytest.approx(55.4, abs=0.01)
    assert boxes[PLATE][2] == pytest.approx(-4.0, abs=0.01)
    assert boxes[PLATE][5] == pytest.approx(-0.05, abs=0.01)
    assert boxes[FRAME][2] == pytest.approx(-1.6, abs=0.01)
    assert boxes[FRAME][5] == pytest.approx(16.7, abs=0.01)
    assert boxes[BEZEL][2] == pytest.approx(16.7, abs=0.01)
    assert boxes[BEZEL][5] == pytest.approx(24.0, abs=0.01)


def test_boards_are_where_the_layout_puts_them():
    """The constants agree with ina228_node.py's layout."""
    parts = ina228_node.layout()
    assert ina228_node.BOARD == pytest.approx(case.INA)
    display = ina228_node.extent(parts["display"])
    assert display[5] == pytest.approx(case.TOUCH)
    assert display[0] == pytest.approx(case.DISPLAY[0])
    assert display[1] == pytest.approx(case.DISPLAY[1])
    assert display[4] == pytest.approx(case.DISPLAY[3])
    assert ina228_node.HOUSING_TOP == case.HOUSING_TOP


def test_screws_run_into_inserts(built):
    """A clear hole through plate and bezel, an insert's hole behind."""
    hole, deep, _wall = case.INSERT
    clear = case.SCREW[0]
    for x, y in case.INA_HOLES:
        assert filled(built[PLATE], _cylinder(
            x, y, clear / 2 - 0.01, -10.0, 1.0)) == pytest.approx(
                0.0, abs=1e-6)
        z = case.INA_Z[1] + case.FOIL
        assert filled(built[FRAME], _cylinder(
            x, y, hole / 2 - 0.01, z, z + deep)) == pytest.approx(
                0.0, abs=1e-6)
        ring = _cylinder(x, y, hole / 2 + 1.5, z + 0.1, z + deep)
        assert filled(built[FRAME], ring) > 0.9 * (
            volume(ring) - 3.1416 * (hole / 2) ** 2 * (deep - 0.1))
    for x, y in case.DISPLAY_HOLES:
        assert filled(built[BEZEL], _cylinder(
            x, y, clear / 2 - 0.01, 17.0, 30.0)) == pytest.approx(
                0.0, abs=1e-6)
        z = case.Z_SPLIT - case.PRINT
        assert filled(built[FRAME], _cylinder(
            x, y, hole / 2 - 0.01, z - deep, z)) == pytest.approx(
                0.0, abs=1e-6)


def test_columns_stand_on_one_board_and_carry_the_other(built):
    """Material from the INA228 board's top to the display board's back."""
    for x, y in case.DISPLAY_HOLES:
        post = _cylinder(x, y, 1.5, 1.7, 10.0)
        assert filled(built[FRAME], post) == pytest.approx(
            volume(post), rel=1e-3)
    box = _extent(built[FRAME])
    assert box[5] == pytest.approx(case.DISPLAY_Z[0], abs=0.01)


def test_openings_are_open(built):
    """XT90 at both ends, USB, tool slot, card, grille, window, mic."""
    y0, y1, z0, z1 = case.XT90
    frame, bezel = built[FRAME], built[BEZEL]
    for x in (case.X0 - case.WALL, case.X1):
        assert filled(frame, _box(x, y0, z0, x + case.WALL, y1, z1)) == (
            pytest.approx(0.0, abs=1e-6))
    x = case.X1
    for part, tool in (
            (frame, _box(x, case.TOOL[0], z1, x + case.WALL, case.TOOL[1],
                         case.TOOL[2])),
            (frame, _box(x, case.USB[0], z1, x + case.WALL, case.USB[1],
                         case.Z_SPLIT)),
            (bezel, _box(x, case.USB[0], case.Z_SPLIT, x + case.WALL,
                         case.USB[1], case.USB[2])),
            (frame, _box(case.CARD[0], case.Y0 - case.WALL, case.CARD[2],
                         case.CARD[1], case.Y0, case.Z_SPLIT)),
            (bezel, _box(*case.WINDOW[:2], 22.0, *case.WINDOW[2:], 25.0)),
            (bezel, _cylinder(case.MIC[0], case.MIC[1],
                              case.MIC[2] / 2 - 0.01, 22.0, 25.0))):
        assert filled(part, tool) == pytest.approx(0.0, abs=1e-6)
    xs, wide, z0, z1 = case.GRILLE
    for gx in xs:
        slot = _box(gx - wide / 2, case.Y1, z0, gx + wide / 2,
                    case.Y1 + case.WALL, z1)
        assert filled(frame, slot) == pytest.approx(0.0, abs=1e-6)


def test_window_shows_the_view_area():
    """The window is 0.5 wider than the view area on each side."""
    import es3c28p
    view = es3c28p.VIEW
    wide, high = view[2] - view[0], view[3] - view[1]
    assert case.WINDOW[2] - case.WINDOW[0] == pytest.approx(high + 1.0,
                                                            abs=0.1)
    assert case.WINDOW[3] - case.WINDOW[1] == pytest.approx(wide + 1.0,
                                                            abs=0.1)
