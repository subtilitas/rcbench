"""Tests of tools/video_scope.py and of the scope scene's check.

SPDX-License-Identifier: MIT
"""

import copy
import json
import shutil
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import make_video  # noqa: E402
import video_scope  # noqa: E402
import video_voice  # noqa: E402

FIGURE = REPO / "tools" / "video" / "plots" / "hitec-precharge.json"
BOARD = REPO / "tools" / "video" / "storyboards" / "hitec-precharge.json"


def _board() -> dict:
    return json.loads(BOARD.read_text(encoding="utf-8"))


def _first(board: dict) -> int:
    return next(n for n, scene in enumerate(board["scenes"])
                if scene["type"] == "scope")


def test_frame_levels_invert_the_byte_between_a_high_start_and_a_low_stop():
    """Frame levels invert the byte between a high start and a low stop."""
    assert video_scope.frame_levels(0x69) == [
        True, False, True, True, False, True, False, False, True, False]


def test_thin_keeps_every_point_within_its_tolerance():
    """Thin keeps every point within its tolerance."""
    points = [(float(x), (x % 50) * 0.01 + (x > 300) * 2.0)
              for x in range(600)]
    kept = video_scope.thin(points, 0.02)
    assert kept[0] == points[0] and kept[-1] == points[-1]
    assert len(kept) < len(points) / 4
    for x, y in points:
        assert abs(video_scope.value(kept, x) - y) <= 0.02 + 1e-9


def test_value_reads_between_two_points():
    """Value reads between two points."""
    assert video_scope.value([(0.0, 0.0), (10.0, 5.0)], 4.0) == 2.0


def test_capture_keeps_the_edges_of_a_csv(tmp_path):
    """Capture keeps the edges of a csv."""
    path = tmp_path / "read.csv"
    path.write_text("; header\nD0\n0\n0\n1\n1\n1\n0\n", encoding="utf-8")
    trace = video_scope.capture(path, 200e6)
    assert trace["x"] == [0.0, 10.0, 10.0, 25.0, 25.0, 30.0]
    assert trace["y"] == [0, 0, 1, 1, 0, 0]


def test_faults_names_a_setting_the_model_reads_wrongly():
    """Faults names a setting the model reads wrongly."""
    found = json.loads(FIGURE.read_text(encoding="utf-8"))["window"]
    assert video_scope.faults(found) == []
    found["15/40"] = [1.5, True]
    assert len(video_scope.faults(found)) == 1
    found["pulse"] = 330.0
    found["low_pulse"] = 60.0
    assert len(video_scope.faults(found)) == 3


def test_the_figure_holds_the_tools_model_and_timing():
    """The figure holds the tools model and timing."""
    figure = json.loads(FIGURE.read_text(encoding="utf-8"))
    assert figure["model"] == video_scope.MODEL
    assert figure["timing"]["precharge_ns"] == pytest.approx(100.0)
    assert figure["timing"]["wait_ns"] == pytest.approx(113.33, abs=0.01)
    points = figure["series"]["byte-points"]["y"]
    levels = video_scope.frame_levels(figure["timing"]["byte"])
    # 8 samples a bit: each bit's middle samples read its level.
    first = points.index(1)
    for n, high in enumerate(levels):
        assert points[first + n * 8 + 3] == int(high)


@pytest.mark.skipif(shutil.which("ngspice") is None,
                    reason="ngspice is not installed")
def test_the_simulation_reproduces_the_measured_window():
    """The simulation reproduces the measured window."""
    assert video_scope.faults(video_scope.window()) == []


def test_the_scope_storyboard_passes_the_check():
    """The scope storyboard passes the check."""
    assert make_video.check(_board()) == []


@pytest.mark.parametrize("change, message", [
    (lambda s: s.update(figure="none"), "figure names a file"),
    (lambda s: s["panels"][0].update(circuit="none"),
     "circuit is not in the figure"),
    (lambda s: s["panels"][1]["traces"][0].update(series="none"),
     "series is not in the figure"),
    (lambda s: s["panels"][1].update(box=[0, 0, 4000, 10]), "box must be"),
    (lambda s: s["labels"][0].update(anchor=["c", "none"]), "anchor must be"),
    (lambda s: s["labels"][0].update(leader="no"), "leader must be"),
    (lambda s: s["panels"][0]["flow"][0].update(path="none"),
     "path is not in the circuit"),
])
def test_the_check_names_a_fault_of_a_scope_scene(change, message):
    """The check names a fault of a scope scene."""
    board = _board()
    change(board["scenes"][_first(board)])
    assert any(message in error for error in make_video.check(board))


def test_a_stretched_scope_scene_keeps_its_times_in_order():
    """A stretched scope scene keeps its times in order."""
    board = _board()
    n = _first(board)
    scene = copy.deepcopy(board["scenes"][n])
    old = [0.0, 10.0, scene["dur"]]
    new = [0.0, 10.0, scene["dur"] + 30.0]
    video_voice._stretch(scene, old, new)
    assert scene["dur"] == pytest.approx(board["scenes"][n]["dur"] + 30.0)
    board["scenes"][n] = scene
    assert make_video.check(board) == []
    late = board["scenes"][n]["labels"][-1]
    assert late["to"] > _board()["scenes"][n]["labels"][-1]["to"]


def test_the_narrator_is_given_kilo_ohms_as_two_words():
    """The narrator is given kilo ohms as two words."""
    assert video_voice.spoken("4.7 kilohms in series") == (
        "4.7 kilo ohms in series")
    assert video_voice.spoken("a 3 kilohm pull-down") == (
        "a 3 kilo ohm pull-down")


def test_a_stretched_scope_scene_moves_its_cues_with_its_other_times():
    """A stretched scope scene moves its cues with its other times."""
    board = _board()
    n = _first(board)
    scene = copy.deepcopy(board["scenes"][n])
    cues = [cue["at"] for cue in scene["say"]]
    old = [0.0] + cues + [scene["dur"]]
    # Every line needs 4s more than its gap gives it.
    new = [0.0, cues[0]]
    for k in range(len(cues)):
        new.append(round(new[-1] + old[k + 2] - old[k + 1] + 4.0, 2))
    video_voice._stretch(scene, old, new)
    moved = [cue["at"] for cue in scene["say"]]
    assert moved == pytest.approx(new[1:-1])
    gaps = [b - a for a, b in zip(moved, moved[1:], strict=False)]
    before = [b - a for a, b in zip(cues, cues[1:], strict=False)]
    assert gaps == pytest.approx([gap + 4.0 for gap in before])
    assert scene["dur"] == pytest.approx(new[-1])


def test_the_narrator_is_given_precharge_with_a_hyphen():
    """The narrator is given precharge with a hyphen."""
    assert video_voice.spoken("Set it to PRECHARGE, and one precharge "
                              "follows. Precharged.") == (
        "Set it to pre-charge, and one pre-charge follows. pre-charged.")
