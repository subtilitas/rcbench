"""The storyboard check of tools/make_video.py.

SPDX-License-Identifier: MIT
"""

import copy
import json

import make_video
import video_audio
import video_voice


def _board():
    path = make_video.VIDEO / "storyboards" / "kst-rbmod.json"
    return json.loads(path.read_text(encoding="utf-8"))


def test_committed_storyboards_pass():
    """Every storyboard in the tree is one the player draws."""
    boards = make_video.storyboards()
    assert boards
    for path in boards:
        board = json.loads(path.read_text(encoding="utf-8"))
        assert make_video.check(board) == [], path.name


def test_views_name_their_screen():
    """Each view comes with the renderer's screen name."""
    found = make_video.views(_board())
    assert found["servo-hub-rbmod"] == ("servo-hub-rbmod", "servo", "en")
    assert found["servo-kst-read-rbmod"][1] == "servo"


def test_a_step_can_set_its_language():
    """A tour step with lang is a shot of its own."""
    board = {"lang": "en", "steps": 1, "scenes": [{"type": "tour", "steps": [
        {"view": "overview", "dur": 3, "n": 1, "title": "x", "lang": "de"},
        {"view": "overview", "dur": 3, "n": 1, "title": "x"}]}]}
    assert make_video.check(board) == []
    found = make_video.views(board)
    assert found["overview@de"] == ("overview", None, "de")
    assert found["overview"] == ("overview", None, "en")


def test_a_focus_off_the_display_is_refused():
    """A spotlight lies on the 1024 x 600 display."""
    board = {"lang": "en", "steps": 1, "scenes": [{"type": "tour", "steps": [
        {"view": "overview", "dur": 3, "n": 1, "title": "x",
         "focus": [900, 10, 200, 100]}]}]}
    assert any("focus" in e for e in make_video.check(board))


def test_a_hold_needs_its_lead_in():
    """A hold shorter than its 2s ring is refused."""
    board = _board()
    step = next(s for s in board["scenes"][1]["steps"]
                if s.get("action") == "hold")
    step["dur"] = 2.0
    assert any("at least 3.1s" in e for e in make_video.check(board))


def test_a_target_off_the_display_is_refused():
    """A target lies on the 1024 x 600 display."""
    board = _board()
    board["scenes"][1]["steps"][0]["target"] = [1024, 10]
    assert any("target" in e for e in make_video.check(board))


def test_an_unknown_anchor_is_refused():
    """A label points at a named point of the model or at a position."""
    board = _board()
    board["scenes"][0]["labels"][0]["anchor"] = "battery"
    assert any("anchor" in e for e in make_video.check(board))
    fixed = copy.deepcopy(board)
    fixed["scenes"][0]["labels"][0]["anchor"] = [0, 10, 0]
    assert make_video.check(fixed) == []


def test_a_step_beyond_the_count_is_refused():
    """A step number above the storyboard's count is refused."""
    board = _board()
    board["steps"] = 3
    assert any("from 1 to 3" in e for e in make_video.check(board))


def test_the_track_is_as_long_as_the_video():
    """The scenes lie end to end, a tour's steps summed."""
    board = {"lang": "en", "steps": 1, "scenes": [
        {"type": "end", "dur": 4, "heading": "x"},
        {"type": "tour", "steps": [
            {"dur": 3, "n": 1, "card": {"title": "x"}},
            {"view": "overview", "dur": 5, "n": 1, "title": "x",
             "points": ["a", "b"], "focus": [0, 0, 100, 100]}]}]}
    assert make_video.check(board) == []
    assert video_audio.scene_times(board) == [(0.0, 4), (4.0, 8)]


def test_each_event_has_its_sound():
    """A card chimes, a caption pops, points tick, a spotlight swells."""
    board = {"lang": "en", "steps": 1, "scenes": [
        {"type": "tour", "steps": [
            {"dur": 3, "n": 1, "card": {"title": "x"}},
            {"view": "overview", "dur": 5, "n": 1, "title": "x",
             "points": ["a", "b"], "focus": [0, 0, 100, 100]}]}]}
    kinds = [kind for _, kind, _ in video_audio.events(board)]
    assert kinds.count("chime") == 1
    assert kinds.count("pop") == 1
    assert kinds.count("tick") == 2
    assert kinds.count("swell") == 1
    assert "tap" not in kinds


def test_a_hold_sounds_for_its_two_seconds():
    """The rising tone starts 2.45s before the step ends."""
    events = video_audio.events(_board())
    holds = [t for t, kind, _ in events if kind == "hold"]
    done = [t for t, kind, _ in events if kind == "done"]
    assert len(holds) == len(done) == 4
    assert all(abs(d - h - 2.03) < 1e-6 for h, d in zip(holds, done,
                                                        strict=True))


def test_the_music_is_a_known_bed():
    """A storyboard names one of the beds, nothing else."""
    board = _board()
    board["music"] = "bach-prelude"
    assert make_video.check(board) == []
    assert make_video.BEDS == video_audio.BEDS
    board["music"] = "drums"
    assert any("music" in e for e in make_video.check(board))


def test_the_prelude_has_its_bars():
    """35 bars of 5 notes, from C major to C major."""
    assert len(video_audio.PRELUDE) == 35
    assert all(len(bar) == 5 for bar in video_audio.PRELUDE)
    assert video_audio.PRELUDE[0] == (60, 64, 67, 72, 76)
    assert {n % 12 for n in video_audio.PRELUDE[-1]} == {0, 4, 7}


def test_names_are_written_as_they_are_said():
    """Units, spelled names and screen names reach the speaker as words."""
    said = video_voice.spoken
    assert said("Hold <b>CONNECT</b> 2 s.") == "Hold Connect 2 seconds."
    assert said("a 2 s hold") == "a 2 second hold"
    assert said("The IOmcu") == "The I O M C U"
    assert said("ESCs and an ESC") == ("electronic speed controllers and an "
                                       "electronic speed controller")
    assert said("LIMIT RUN at 6.0 V") == "Limit Run at 6.0 volts"
    assert said("Motor & ESC") == "Motor and electronic speed controller"


def test_every_step_has_a_line():
    """A card says its chapter, a step its title and text or its say."""
    board = {"lang": "en", "steps": 1, "scenes": [
        {"type": "case", "dur": 9, "camera": [[0, [0, 0, 9], [0, 0, 0]]],
         "say": [{"at": 1, "text": "one"}, {"at": 5, "text": "two"}]},
        {"type": "tour", "steps": [
            {"dur": 3, "n": 1, "card": {"title": "Servo", "text": "Test."}},
            {"view": "overview", "dur": 5, "n": 1, "title": "LIVE",
             "text": "A dial."},
            {"view": "overview", "dur": 5, "n": 1, "title": "x",
             "say": "Said instead."}]}]}
    assert make_video.check(board) == []
    lines = [text for _, _, _, text in video_voice.script(board)]
    assert lines == ["one", "two", "Chapter 1. Servo. Test.",
                     "Live. A dial.", "Said instead."]


def test_a_held_component_freezes_the_music():
    """Holds in the air are freezes; overlapping ones merge."""
    board = {"lang": "en", "steps": 0, "scenes": [
        {"type": "end", "dur": 10, "heading": "x"},
        {"type": "case", "dur": 30, "camera": [[0, [0, 0, 9], [0, 0, 0]]],
         "parts": {"case": [[0, 1], [2, 0]],
                   "xt60": [[3, 1], [4, .4], [8, .4], [9, 0]],
                   "usbc": [[3, 1], [5, .4], [9, .4], [10, 0]],
                   "lid": [[12, 1], [13, .5], [15, .5], [16, 0]]}}]}
    assert make_video.check(board) == []
    assert video_audio.freezes(board) == [(14, 19), (23, 25)]


_PCB = """(kicad_pcb (version 20260206)
 (gr_rect (start 10 20) (end 50 40) (layer "Edge.Cuts"))
 (segment (start 12 22) (end 14 22) (width 0.3) (layer "F.Cu") (net "A"))
 (segment (start 12 30) (end 14 30) (width 0.3) (layer "B.Cu") (net "B"))
 (footprint "lib:R_1206" (layer "F.Cu") (at 13 22)
  (property "Reference" "R1") (property "Value" "120R")
  (pad "1" smd roundrect (at -1 0) (size 1.2 1.8) (net "A"))
  (pad "2" smd roundrect (at 1 0) (size 1.2 1.8) (net "A")))
 (footprint "lib:Buck" (layer "F.Cu") (at 30 30 90)
  (property "Reference" "M7") (property "Value" "Buck")
  (fp_rect (start -2 -2) (end 6 4) (layer "F.CrtYd"))
  (pad "2" thru_hole circle (at 0 0 90) (size 1.7 1.7) (drill 1) (net "GND"))
  (pad "2" thru_hole circle (at 0 2.54 90) (size 1.7 1.7) (drill 1)
   (net 3 "GND"))))
"""


def _rework():
    return {"lang": "en", "steps": 1, "scenes": [{
        "type": "board", "board": "x", "dur": 10,
        "camera": [[0, [20, 10], 40], [5, [3, 37], 10]],
        "labels": [{"from": 1, "to": 4, "anchor": "R1.2", "at": [60, 300],
                    "title": "x"}],
        "tags": [{"from": 1, "to": 4, "at": "M7.2b", "text": "GND"}],
        "nets": [{"from": 1, "to": 4, "net": "A"}],
        "cuts": [{"between": ["R1.1", "R1.2"], "t": [5, 6]}],
        "wires": [{"from": "M7.2a", "to": "R1.2", "via": [[5, 30]],
                   "t": [6, 8]}],
        "fit": [{"kind": "zener", "a": "M7.2a", "b": "M7.2b", "t": 8.5}]}]}


def test_a_board_is_laid_out_with_its_high_x_at_the_bottom():
    """The converter turns the board and names every pad."""
    import video_board

    plan = video_board.convert(_PCB)
    assert plan["size"] == [20.0, 40.0]
    # The back copper's track is left out.
    assert [t["net"] for t in plan["tracks"]] == ["A"]
    pads = {p["name"]: p for p in plan["pads"]}
    assert sorted(pads) == ["M7.2a", "M7.2b", "R1.1", "R1.2"]
    # (12, 22) of the file lies 18mm from the left and 2mm from the top.
    assert (pads["R1.1"]["x"], pads["R1.1"]["y"]) == (18.0, 2.0)
    assert (pads["R1.1"]["w"], pads["R1.1"]["h"]) == (1.8, 1.2)
    # The footprint's quarter turn carries its second pad along x.
    assert (pads["M7.2b"]["x"], pads["M7.2b"]["y"]) == (10.0, 22.54)
    assert pads["M7.2b"]["net"] == "GND"
    assert video_board.convert(_PCB, 0)["size"] == [40.0, 20.0]


def test_a_board_scene_passes_and_names_its_faults():
    """A board scene is checked by itself and against its board."""
    import video_board

    board = _rework()
    assert make_video.check(board) == []
    plan = video_board.convert(_PCB)
    assert make_video.board_faults(board["scenes"][0], plan) == []
    board["scenes"][0]["wires"][0]["to"] = "R9.1"
    board["scenes"][0]["nets"][0]["net"] = "B"
    assert make_video.board_faults(board["scenes"][0], plan) == [
        "no pad R9.1", "no track of net B"]
    del board["scenes"][0]["cuts"][0]["t"]
    board["scenes"][0]["fit"][0]["kind"] = "coil"
    board["scenes"][0]["camera"][1][2] = 0
    errors = make_video.check(board)
    assert any("cut 1" in e for e in errors)
    assert any("fit 1" in e for e in errors)
    assert any("camera" in e for e in errors)


def test_a_board_scene_has_its_sounds():
    """A cut, a wire's end and a part's seat each sound once."""
    kinds = [kind for _, kind, _ in video_audio.events(_rework())]
    assert kinds.count("low") == 1
    assert kinds.count("tap") == 2
    assert video_audio.scene_times(_rework()) == [(0.0, 10)]
