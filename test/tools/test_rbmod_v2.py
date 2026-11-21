"""hardware/mechanical: the rbmod carrier, version 2, and its reader.

The reader turns a KiCad board file into the frame of the model. The
model has the board file's outline and holes, every part stands at its
footprint's place, each module's pins stand in the carrier's drilled
pads, and the two files differ by the through-hole parts.

SPDX-License-Identifier: MIT
"""

import math
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
sys.path.insert(0, str(ROOT / "hardware" / "kicad" / "tools"))
import _read_kicad_pcb as reader  # noqa: E402
import _solid  # noqa: E402
import rbmod_v2  # noqa: E402
import step_gen  # noqa: E402

BOARD = """(kicad_pcb (general (thickness 1.2))
 (gr_line (start 10 20) (end 30 20) (layer "Edge.Cuts"))
 (gr_line (start 30 20) (end 30 50) (layer "Edge.Cuts"))
 (gr_line (start 10 50) (end 30 50) (layer "Edge.Cuts"))
 (gr_line (start 10 50) (end 10 20) (layer "Edge.Cuts"))
 (gr_line (start 0 0) (end 5 5) (layer "F.SilkS"))
 (gr_circle (center 12 22) (end 13.5 22) (layer "Edge.Cuts"))
 (footprint "Lib:Part" (layer "F.Cu") (at 20 30 90)
  (property "Reference" "J2") (property "Value" "plug")
  (attr through_hole)
  (fp_line (start -1 -2) (end 3 4) (layer "F.Fab"))
  (pad "1" thru_hole circle (at 0 0) (size 2 2) (drill 1))
  (pad "2" thru_hole circle (at 2 0 90) (size 2 2) (drill 0.8)))
 (footprint "Lib:Chip" (layer "F.Cu") (at 15 45)
  (property "Reference" "C1") (property "Value" "1u") (attr smd)
  (pad "1" smd rect (at -1 0) (size 1 1))))
"""


def extent(shapes):
    """The box round shapes."""
    points = [q for _k, pts, _e, _f, _rgb in _solid.items(shapes)
              for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def test_reader_turns_the_board_into_the_models_frame():
    """Origin at the lower left corner, y up, turns as KiCad draws them."""
    data = reader.read(BOARD)
    assert data["thickness"] == 1.2
    assert sorted(data["outline"]) == [(0.0, 0.0), (0.0, 30.0),
                                       (20.0, 0.0), (20.0, 30.0)]
    assert reader._area(data["outline"]) == pytest.approx(600.0)
    assert data["holes"] == [(2.0, 28.0, 3.0)]
    chip, plug = data["footprints"]
    assert (chip["ref"], chip["smd"], chip["drills"]) == ("C1", True, [])
    assert chip["at"] == (5.0, 5.0)
    assert (plug["ref"], plug["smd"], plug["turn"]) == ("J2", False, 90.0)
    # Turned 90 degrees counter-clockwise, pad 2 lies above pad 1.
    assert plug["drills"] == [(10.0, 20.0, 1.0), (10.0, 22.0, 0.8)]
    assert plug["fab"] == (-1.0, -4.0, 3.0, 2.0)


def test_reader_refuses_what_it_does_not_handle():
    """An arc in the outline, an open outline, a part on the underside."""
    arc = BOARD.replace('(gr_circle', '(gr_arc (start 1 1) (mid 2 2) (end '
                        '3 1) (layer "Edge.Cuts"))\n (gr_circle')
    with pytest.raises(ValueError, match="lines and circles"):
        reader.read(arc)
    with pytest.raises(ValueError, match="open|join"):
        reader.read(BOARD.replace("(end 10 20)", "(end 10 25)"))
    with pytest.raises(ValueError, match="J2 is not on the top side"):
        reader.read(BOARD.replace('"Lib:Part" (layer "F.Cu")',
                                  '"Lib:Part" (layer "B.Cu")'))


def test_board_has_the_files_outline_and_holes():
    """80 x 89 with a notch of 4.41 x 13, 6 holes of 3.0, 1.579 thick."""
    assert extent(rbmod_v2.board()) == pytest.approx(
        [0.0, 0.0, -1.579, 80.0, 89.0, 0.0])
    assert reader._area(rbmod_v2.data.OUTLINE) == pytest.approx(
        80.0 * 89.0 - 4.4096 * 13.0)
    assert len(rbmod_v2.data.HOLES) == 6
    assert {d for _x, _y, d in rbmod_v2.data.HOLES} == {3.0}
    drills = [d for fp in rbmod_v2.data.FOOTPRINTS for d in fp["drills"]]
    assert len(drills) == 123
    (_outer, holes, *_rest), = rbmod_v2.board()
    assert len(holes) == 129


def test_parts_by_kind():
    """17 surface-mounted parts, 9 connectors, 5 modules."""
    refs = [fp["ref"] for fp in rbmod_v2.data.FOOTPRINTS]
    assert len(refs) == len(set(refs)) == 31
    smd = [fp["ref"] for fp in rbmod_v2.data.FOOTPRINTS if fp["smd"]]
    assert len(smd) == 17
    assert {r[0] for r in smd} == {"R", "C", "D"}
    rest = [r for r in refs if r not in smd]
    assert sorted(r[0] for r in rest) == ["J"] * 9 + ["M"] * 5
    for fp in rbmod_v2.data.FOOTPRINTS:
        assert rbmod_v2.part(fp)


def test_an_unknown_footprint_is_refused():
    """A footprint without a model stops the build."""
    with pytest.raises(ValueError, match="no model of Other"):
        rbmod_v2.part({"footprint": "Lib:Other"})


def test_the_two_models_differ_by_the_through_hole_parts():
    """52 solids with the SMD parts, 334 with connectors and modules."""
    built = rbmod_v2.parts(None)
    assert sorted(built) == ["rbmod_v2", "rbmod_v2_smd"]
    assert len(built["rbmod_v2_smd"]) == 52
    assert len(built["rbmod_v2"]) == 334
    assert built["rbmod_v2"][:52] == built["rbmod_v2_smd"]
    assert extent(rbmod_v2.board() + rbmod_v2.placed(True))[5] == (
        pytest.approx(2.5))
    box = extent(rbmod_v2.placed(False))
    assert (box[2], box[5]) == pytest.approx((-6.0, 12.8))


def test_smd_parts_lie_on_the_board_at_their_places():
    """Each stands on z = 0, centred on its footprint's place."""
    for fp in rbmod_v2.data.FOOTPRINTS:
        if not fp["smd"]:
            continue
        box = extent(_solid.place(rbmod_v2.part(fp), x=fp["at"][0],
                                  y=fp["at"][1], turn=fp["turn"]))
        assert box[2] == pytest.approx(0.0)
        assert ((box[0] + box[3]) / 2, (box[1] + box[4]) / 2) == (
            pytest.approx(fp["at"], abs=0.01))
        assert 0.0 < box[0] and box[3] < 80.0 and 0.0 < box[1]


def test_module_pins_stand_in_the_carriers_holes():
    """Every pin that reaches under the carrier lies on a drilled pad."""
    gold = step_gen.COLORS["gold"]
    count = 0
    for fp in rbmod_v2.data.FOOTPRINTS:
        if not fp["ref"].startswith("M"):
            continue
        shapes = _solid.place(rbmod_v2.part(fp), x=fp["at"][0],
                              y=fp["at"][1], turn=fp["turn"])
        for shape in shapes:
            if shape[4] != gold or shape[2] >= 0.0:
                continue
            box = extent([shape])
            cx, cy = (box[0] + box[3]) / 2, (box[1] + box[4]) / 2
            x, y, d = min(fp["drills"],
                          key=lambda q: math.dist(q[:2], (cx, cy)))
            assert math.dist((x, y), (cx, cy)) < 0.01
            side = box[3] - box[0]
            assert side * math.sqrt(2) < d
            count += 1
    assert count == 40 + 8 + 9 + 9


def test_the_buck_converters_enable_pin_is_drawn_thin():
    """Its hole is 0.8; a 0.64 pin is 0.91 across its corners."""
    spec = rbmod_v2._board.kicad_spec("dcdc_buck_mini")
    small = [pad for pad in spec.FOOTPRINT["pads"]
             if pad.get("drill") and pad["drill"] < 0.95]
    assert [pad["drill"] for pad in small] == [0.8]
    assert step_gen.HDR_PIN * math.sqrt(2) > 0.8
    assert rbmod_v2.THIN * math.sqrt(2) < 0.8


def test_connectors():
    """PH headers 6.0 high, Molex 12.8, angled pins past the edge."""
    by = {fp["ref"]: fp for fp in rbmod_v2.data.FOOTPRINTS}

    def box(ref):
        fp = by[ref]
        return extent(_solid.place(rbmod_v2.part(fp), x=fp["at"][0],
                                   y=fp["at"][1], turn=fp["turn"]))
    j4 = box("J4")
    assert (j4[3] - j4[0], j4[4] - j4[1], j4[5]) == pytest.approx(
        (9.9, 4.5, 6.0))
    j3 = box("J3")
    assert j3[3] - j3[0] == pytest.approx(5.9)
    j8 = box("J8")
    assert (j8[3] - j8[0], j8[4] - j8[1], j8[5]) == pytest.approx(
        (9.6, 5.4, 12.8))
    j1 = box("J1")
    assert j1[3] == pytest.approx(84.3, abs=0.01)
    assert j1[5] == pytest.approx(7.62)
