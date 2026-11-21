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
from conftest import import_or_fail

import_or_fail("shapely", "needs shapely, which the reader's copper uses")

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
sys.path.insert(0, str(ROOT / "hardware" / "kicad" / "tools"))
import _read_kicad_pcb as reader  # noqa: E402
import _solid  # noqa: E402
import gen_all  # noqa: E402
import rbmod_v2  # noqa: E402

BOARD = """(kicad_pcb (general (thickness 1.2))
 (gr_line (start 10 20) (end 30 20) (layer "Edge.Cuts"))
 (gr_line (start 30 20) (end 30 50) (layer "Edge.Cuts"))
 (gr_line (start 10 50) (end 30 50) (layer "Edge.Cuts"))
 (gr_line (start 10 50) (end 10 20) (layer "Edge.Cuts"))
 (gr_line (start 0 0) (end 5 5) (layer "F.SilkS"))
 (segment (start 14 45) (end 14 30) (width 0.4) (layer "F.Cu"))
 (segment (start 14 30) (end 19 30) (width 0.4) (layer "F.Cu"))
 (segment (start 12 40) (end 28 40) (width 1) (layer "In1.Cu"))
 (via (at 25 40) (size 1) (drill 0.5) (layers "F.Cu" "B.Cu"))
 (gr_circle (center 12 22) (end 13.5 22) (layer "Edge.Cuts"))
 (footprint "Lib:Part" (layer "F.Cu") (at 20 30 90)
  (property "Reference" "J2") (property "Value" "plug")
  (attr through_hole dnp)
  (fp_line (start -1 -2) (end 3 4) (layer "F.Fab"))
  (pad "1" thru_hole circle (at 0 0) (size 2 2) (drill 1)
   (layers "*.Cu" "*.Mask"))
  (pad "2" thru_hole rect (at 2 0 90) (size 2 2) (drill 0.8)
   (layers "*.Cu" "*.Mask")))
 (footprint "Lib:Chip" (layer "F.Cu") (at 15 45)
  (property "Reference" "C1") (property "Value" "1u") (attr smd)
  (pad "1" smd roundrect (at -1 0) (size 1 1) (roundrect_rratio 0.25)
   (layers "F.Cu" "F.Mask"))))
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
    assert (chip["dnp"], plug["dnp"]) == (False, True)
    assert chip["at"] == (5.0, 5.0)
    assert (plug["ref"], plug["smd"], plug["turn"]) == ("J2", False, 90.0)
    # Turned 90 degrees counter-clockwise, pad 2 lies above pad 1.
    assert plug["drills"] == [(10.0, 20.0, 1.0), (10.0, 22.0, 0.8)]
    assert plug["fab"] == (-1.0, -4.0, 3.0, 2.0)


def test_reader_gives_the_outer_layers_copper():
    """Pads with their holes cut out; a track ends at the pads' edge."""
    data = reader.read(BOARD)
    assert len(data["pads_top"]) == 3
    assert len(data["pads_bottom"]) == 2
    assert all(len(holes) == 1 for _outer, holes in data["pads_bottom"])
    # The track and the via; the inner layer's track is not read.
    assert len(data["copper_top"]) == 2
    assert len(data["copper_bottom"]) == 1
    (via, no_holes), = data["copper_bottom"]
    assert no_holes == []
    assert reader._area(via) == pytest.approx(math.pi * 0.25, rel=0.03)
    track = max(data["copper_top"], key=lambda q: abs(reader._area(q[0])))
    xs = [x for x, _y in track[0]]
    ys = [y for _x, y in track[0]]
    # From the track's round end to the edge of the plug's round pad 1,
    # which the track keeps 0.02 from.
    assert min(xs) == pytest.approx(3.8, abs=0.01)
    assert 8.97 < max(xs) < 9.01
    assert max(ys) == pytest.approx(20.2, abs=0.01)


def test_reader_refuses_a_pad_shape_it_does_not_know():
    """A custom pad stops the tool."""
    with pytest.raises(ValueError, match="pad shape custom"):
        reader.read(BOARD.replace("smd roundrect", "smd custom"))


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
    assert extent(rbmod_v2.board()[:1]) == pytest.approx(
        [0.0, 0.0, -1.579, 80.0, 89.0, 0.0])
    assert extent(rbmod_v2.board()) == pytest.approx(
        [0.0, 0.0, -1.579 - 0.035, 80.0, 89.0, 0.035])
    assert reader._area(rbmod_v2.data.OUTLINE) == pytest.approx(
        80.0 * 89.0 - 4.4096 * 13.0)
    assert len(rbmod_v2.data.HOLES) == 6
    assert {d for _x, _y, d in rbmod_v2.data.HOLES} == {3.0}
    drills = [d for fp in rbmod_v2.data.FOOTPRINTS for d in fp["drills"]]
    assert len(drills) == 123
    (_outer, holes, *_rest) = rbmod_v2.board()[0]
    assert len(holes) == 129


def test_board_carries_its_copper():
    """157 pads on top, 123 under it, gold; tracks in a lighter green."""
    board = rbmod_v2.board()
    assert len(board) == 1 + 157 + 123 + 31 + 19
    gold = [shape for shape in board if shape[4] == rbmod_v2.ENEPIG]
    assert len(gold) == 280
    assert {(round(shape[2], 3), round(shape[3], 3)) for shape in gold} == {
        (0.0, 0.035), (-1.614, -1.579)}
    tracks = [shape for shape in board if shape[4] == rbmod_v2.COPPER[0]]
    assert len(tracks) == 50
    assert {round(shape[3] - shape[2], 3) for shape in tracks} == {0.02}
    # Every drilled pad's hole is open in its 2 pads.
    drilled = sum(len(fp["drills"]) for fp in rbmod_v2.data.FOOTPRINTS)
    assert sum(len(shape[1]) for shape in gold) == 2 * drilled


def test_parts_by_kind():
    """17 surface-mounted parts, 9 connectors, 5 modules; 2 left off."""
    refs = [fp["ref"] for fp in rbmod_v2.data.FOOTPRINTS]
    assert len(refs) == len(set(refs)) == 31
    smd = [fp["ref"] for fp in rbmod_v2.data.FOOTPRINTS if fp["smd"]]
    assert len(smd) == 17
    assert {r[0] for r in smd} == {"R", "C", "D"}
    rest = [r for r in refs if r not in smd]
    assert sorted(r[0] for r in rest) == ["J"] * 9 + ["M"] * 5
    off = [fp for fp in rbmod_v2.data.FOOTPRINTS if rbmod_v2.unpopulated(fp)]
    assert [fp["ref"] for fp in off] == ["J6", "J8"]
    assert not any(fp["dnp"] for fp in rbmod_v2.data.FOOTPRINTS)
    for fp in rbmod_v2.data.FOOTPRINTS:
        if fp not in off:
            assert rbmod_v2.part(fp)
    with pytest.raises(ValueError, match="no model of Molex"):
        rbmod_v2.part(off[1])


def test_an_unknown_footprint_is_refused():
    """A footprint without a model stops the build."""
    with pytest.raises(ValueError, match="no model of Other"):
        rbmod_v2.part({"footprint": "Lib:Other"})


def test_the_two_models_differ_by_the_through_hole_parts():
    """382 solids with the SMD parts, 1920 with connectors and modules."""
    built = rbmod_v2.parts(None)
    assert sorted(built) == ["rbmod_v2", "rbmod_v2_smd"]
    assert len(built["rbmod_v2_smd"]) == 382
    assert len(built["rbmod_v2"]) == 1920
    assert built["rbmod_v2"][:382] == built["rbmod_v2_smd"]
    assert extent(rbmod_v2.board() + rbmod_v2.placed(True))[5] == (
        pytest.approx(2.535))
    box = extent(rbmod_v2.placed(False))
    assert (box[2], box[5]) == pytest.approx((-5.965, 12.99), abs=0.01)
    # J6 and J8 are not populated: nothing stands on their pads.
    assert box[1] == pytest.approx(6.87, abs=0.01)


def test_smd_parts_lie_on_the_board_at_their_places():
    """Each stands on its pads' top, centred on its footprint's place."""
    for fp in rbmod_v2.data.FOOTPRINTS:
        if not fp["smd"]:
            continue
        box = extent(_solid.place(rbmod_v2.part(fp), x=fp["at"][0],
                                  y=fp["at"][1], turn=fp["turn"]))
        assert box[2] == pytest.approx(0.0)
        lifted = [shape for shape in rbmod_v2.placed(True)]
        assert min(extent([shape])[2] for shape in lifted) == (
            pytest.approx(0.035))
        assert ((box[0] + box[3]) / 2, (box[1] + box[4]) / 2) == (
            pytest.approx(fp["at"], abs=0.01))
        assert 0.0 < box[0] and box[3] < 80.0 and 0.0 < box[1]


def test_module_pins_stand_in_the_carriers_holes():
    """Every header pin under the carrier lies on a drilled pad."""
    count = 0
    for fp in rbmod_v2.data.FOOTPRINTS:
        if not fp["ref"].startswith("M"):
            continue
        shapes = _solid.place(rbmod_v2.part(fp), x=fp["at"][0],
                              y=fp["at"][1], turn=fp["turn"])
        for shape in shapes:
            box = extent([shape])
            if box[2] > -5.9:
                continue
            cx, cy = (box[0] + box[3]) / 2, (box[1] + box[4]) / 2
            x, y, d = min(fp["drills"],
                          key=lambda q: math.dist(q[:2], (cx, cy)))
            assert math.dist((x, y), (cx, cy)) < 0.01
            assert (box[3] - box[0]) * math.sqrt(2) < d
            count += 1
    assert count == 40 + 8 + 8 + 8


def test_the_pd_trigger_board_lies_on_the_carrier():
    """It has no pins; the buck converters stand 2.5 over the carrier."""
    by = {fp["ref"]: fp for fp in rbmod_v2.data.FOOTPRINTS}
    box = extent(rbmod_v2.part(by["M6"]))
    assert (box[2], box[5]) == pytest.approx((0.02, 4.91), abs=0.01)
    for ref in ("M7", "M8"):
        box = extent(rbmod_v2.part(by[ref]))
        assert (box[2], box[5]) == pytest.approx((-6.0, 7.97), abs=0.01)


def test_modules_are_the_library_models():
    """Each module's shapes are those of its KiCad model."""
    for name, stem in rbmod_v2.MODULES.items():
        spec = rbmod_v2._board.kicad_spec(stem)
        assert spec.MODEL == {"detail": stem}
        assert spec.NAME == name
        assert _solid.items(rbmod_v2.module(name)) == gen_all.detail(
            spec.MODEL)


def test_connectors():
    """PH headers 6.0 high, angled pins past the edge."""
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
    j1 = box("J1")
    assert j1[3] == pytest.approx(84.3, abs=0.01)
    assert j1[5] == pytest.approx(7.62)
