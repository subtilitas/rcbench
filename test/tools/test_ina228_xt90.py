"""hardware/mechanical/ina228_xt90.py: the INA228 module and its case.

A rod of _solid.py becomes a closed shell of cylinders and pieces of
tori. The module's solids close, the module is as large as its sources
give it, the shunt has the heights measured on the board, both XT90PW
halves carry two tails at the contact pitch, and the grid fit the sizes
come from finds a drawn grid.

SPDX-License-Identifier: MIT
"""

import sys
from collections import Counter
from pathlib import Path

import pytest
from conftest import import_or_fail

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
sys.path.insert(0, str(ROOT / "hardware" / "kicad" / "tools"))
import _parts  # noqa: E402
import _solid  # noqa: E402
import ina228_xt90  # noqa: E402

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


def test_rod_closes_and_bends():
    """A rod of two straight pieces and an arc is one closed shell."""
    path = [(0, 0), (5, 0, (5, 4, 1)), (9, 4), (9, 10)]
    item = _solid.items([_solid.pipe(path, 1.0, GREY, frame=_solid.Y)])[0]
    assert open_edges(item) == []
    kinds = Counter(face[0][0] for face in item[3])
    assert kinds == {"cylinder": 8, "torus": 4, "plane": 2}
    assert extent([item]) == pytest.approx([0, -1, -1, 10, 1, 10])


def test_rod_refuses_a_corner():
    """Two pieces that meet at an angle are not a rod."""
    with pytest.raises(ValueError):
        _solid.items([_solid.pipe([(0, 0), (5, 0), (5, 5)], 1.0, GREY)])


def test_turned_profile_keeps_its_arcs():
    """Turning a slot a quarter swaps its length and width."""
    slot = _solid.turned(_solid.stadium(-2, -0.5, 2, 0.5), 0, 0, 90)
    item = _solid.items([_solid.ext(slot, 0, 1, GREY)])[0]
    assert open_edges(item) == []
    assert extent([item]) == pytest.approx([-0.5, -2, 0, 0.5, 2, 1])


def test_xt90pw_sizes():
    """Socket half 24.0 long to its plate's end, pin half 25.7.

    The tails stand 0.55 past the plate's end; both are 20.8 wide and
    9.9 high.
    """
    socket = extent(_solid.items(_parts.xt90pw_f()))
    plug = extent(_solid.items(_parts.xt90pw_m()))
    assert socket[3] - socket[0] == pytest.approx(24.0 + 0.55)
    assert plug[3] - plug[0] == pytest.approx(25.7 + 0.55)
    for box in (socket, plug):
        assert box[4] - box[1] == pytest.approx(20.8)
        assert (box[2], box[5]) == pytest.approx((-3.0, 9.9))


def test_module_solids_close():
    """Every solid of the module and of the case is a closed shell."""
    for solids, count in ((ina228_xt90.module(), 225),
                          (ina228_xt90.case(), 17)):
        assert len(solids) == count
        assert [n for n, item in enumerate(solids) if open_edges(item)] == []


def test_module_extent():
    """83.95 x 48.6 x 12.6: plug 2.25 and socket 1.05 past the board."""
    box = extent(ina228_xt90.module())
    ox, oy = ina228_xt90.OX, ina228_xt90.OY
    assert box[0] + ox == pytest.approx(-2.25)
    assert box[3] + ox == pytest.approx(ina228_xt90.kicad.BW + 1.05)
    assert box[4] - box[1] == pytest.approx(ina228_xt90.kicad.BH)
    assert box[4] == pytest.approx(oy)
    assert (box[2], box[5]) == pytest.approx((-1.1, 1.6 + 9.9))


def test_board_holes():
    """4 screw, 5 header, 4 tail and 4 peg holes and 42 slots."""
    _outer, holes, d0, d1, _rgb, _frame = ina228_xt90.board()[0]
    assert len(holes) == 59 and (d0, d1) == (0.0, 1.6)
    assert len(ina228_xt90.slots()) == 42


def test_shunt_heights():
    """Ends 1.2 high; the middle from 0.2 to 1.8 over the board."""
    ends, middle, other = (extent(_solid.items([shape])) for shape in
                           _parts.shunt(15.3, 7.9, 1.2, 5.3, 1.8, 0.2))
    assert (ends[2], ends[5]) == (0.0, 1.2) == (other[2], other[5])
    assert (middle[2], middle[5]) == (0.2, 1.8)
    assert (ends[0], other[3]) == (-7.65, 7.65)
    assert middle[3] - middle[0] == pytest.approx(5.3)
    assert ina228_xt90.SHUNT == (1.2, 5.3, 1.8, 0.2)


def test_tails_and_pegs_stand_where_the_drawing_puts_them():
    """Tails 10.9 apart, 3.8 behind each housing; pegs 9.4 and 8.9 on."""
    assert ina228_xt90.TAIL_Y[1] - ina228_xt90.TAIL_Y[0] == pytest.approx(
        10.9)
    assert ina228_xt90.TAIL_X == pytest.approx((22.25, 58.35))
    xs = sorted({round(x, 2) for x, _y in ina228_xt90.pegs()})
    assert xs == pytest.approx([12.85, 67.25])


def test_underside_is_solder_and_bare_pads():
    """Nothing under the board reaches further than the tails' 1.1."""
    shapes = ina228_xt90.underside()
    box = extent(_solid.items(shapes))
    assert (box[2], box[5]) == pytest.approx((-1.0, 0.0))
    assert len(shapes) == 4 + 5 + 4 + 4 + 3 + 2 + 7


def test_case_holds_the_board():
    """The shell is 2.5 wider than the board all round, 10.1 high."""
    box = extent(ina228_xt90.case())
    assert box[3] - box[0] == pytest.approx(ina228_xt90.kicad.BW + 5.0)
    assert box[4] - box[1] == pytest.approx(ina228_xt90.kicad.BH + 5.0)
    assert (box[2], box[5]) == pytest.approx((-7.9, 1.6 + 0.05 + 2.0))
    shell = extent(ina228_xt90.case()[:5])
    assert shell[5] - shell[2] == pytest.approx(10.1)


def test_print_lies_on_the_board():
    """20 traced shapes; all print is 0.02 thick on the top side."""
    assert len(ina228_xt90.PRINT) == 20
    assert {legend for legend, _o, _h in ina228_xt90.PRINT} == {
        "BIFRC V1.1", "GND", "5V", "L", "A", "V", "G", "5"}
    shapes = ina228_xt90.printed()
    assert len(shapes) == 57
    assert {(d0, d1) for _o, _h, d0, d1, _rgb, _f in shapes} == {(1.6, 1.62)}
    box = extent(_solid.items(shapes))
    ox, oy = ina228_xt90.OX, ina228_xt90.OY
    assert box[0] + ox > 0 and box[3] + ox < ina228_xt90.kicad.BW
    assert oy - box[4] > 0 and oy - box[1] < ina228_xt90.kicad.BH


def test_grid_fit_finds_a_drawn_grid():
    """A drawn grid seen at an angle is found to within 0.1mm."""
    cv2 = import_or_fail("cv2", "needs OpenCV")
    import numpy as np

    import _photo_grid
    flat = np.full((900, 1200, 3), 235, np.uint8)
    for k in range(0, 1200, 60):
        cv2.line(flat, (k, 0), (k, 899), (120, 120, 120), 3)
    for k in range(0, 900, 60):
        cv2.line(flat, (0, k), (1199, k), (120, 120, 120), 3)
    corners = np.float32([[0, 0], [1199, 0], [1199, 899], [0, 899]])
    seen = np.float32([[40, 30], [1170, 10], [1190, 880], [20, 860]])
    image = cv2.warpPerspective(
        flat, cv2.getPerspectiveTransform(corners, seen), (1200, 900),
        borderValue=(235, 235, 235))
    matrix, count, rms = _photo_grid.lattice(image, 58)
    assert count > 150 and rms < 0.1
    # 60 pixels of the drawing are one square of 5mm.
    ends = cv2.perspectiveTransform(seen[None, :2], matrix)[0]
    assert np.linalg.norm(ends[1] - ends[0]) == pytest.approx(
        1199 / 60 * 5.0, abs=0.2)
