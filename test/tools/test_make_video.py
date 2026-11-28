"""The storyboard check of tools/make_video.py.

SPDX-License-Identifier: MIT
"""

import copy
import json

import pytest

from conftest import import_or_fail

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


def test_a_press_a_finger_down_and_a_release_are_actions():
    """One touch over three steps, and a point for the bubble's line."""
    board = _board()
    steps = board["scenes"][1]["steps"]
    for step, action in zip(steps[:3], ("press", "down", "release"),
                            strict=True):
        step.update(action=action, target=[934, 29], point=[1012, 29],
                    dur=2.0)
    assert make_video.check(board) == []
    steps[2]["lift"] = 0.1
    assert make_video.check(board) == []
    steps[2]["lift"] = 1.8
    assert any("lift" in e for e in make_video.check(board))
    steps[2]["lift"] = 0.1
    steps[0]["dur"] = 1.0
    assert any("at least 1.4s" in e for e in make_video.check(board))
    steps[0]["dur"] = 2.0
    steps[1]["point"] = [1024, 29]
    assert any("point" in e for e in make_video.check(board))


def test_the_stop_buttons_keys_stretch_with_the_narration():
    """The head moves with the panel's shots when a line runs long."""
    scene = {"type": "case", "dur": 10, "camera": [[0, [0, 0, 1], [0, 0, 0]]],
             "estop": [[0, 0], [4, 1], [8, 0]],
             "panel": [[0, "motor-plot"], [4, "stop-hw"]],
             "say": [{"at": 2, "text": "x"}]}
    board = {"name": "x", "lang": "en", "brand": "x", "steps": 1,
             "scenes": [scene]}
    # One click where the head latches in, none where it comes out.
    assert [e for e in video_audio.events(board) if e[1] == "click"] \
        == [(4, "click", 0)]
    video_voice._stretch(scene, [0.0, 2, 10], [0.0, 6, 14])
    assert [key[0] for key in scene["estop"]] == [0, 8, 12]
    assert scene["panel"][1][0] == 8
    scene["estop"][1][1] = 2
    assert any("estop" in e for e in make_video.check(board))


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
    assert len(holds) == len(done) == 3
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


def test_a_board_scene_can_be_built_up():
    """Parts mount, joints are soldered and an inset shows a section."""
    import video_board

    board = _rework()
    scene = board["scenes"][0]
    scene.update(bare=True, mount=[{"refs": ["M7"], "t": 1}],
                 solder=[{"refs": ["M7"], "t": [2, 3]},
                         {"pads": ["R1.1"], "back": True, "t": [3, 4]}],
                 insets=[{"from": 1, "to": 4, "kind": "strip",
                          "at": [60, 250], "title": "x"}])
    assert make_video.check(board) == []
    plan = video_board.convert(_PCB)
    assert make_video.board_faults(scene, plan) == []
    scene["mount"][0]["refs"] = ["M9"]
    assert make_video.board_faults(scene, plan) == ["no part M9"]
    scene["insets"][0]["kind"] = "flat"
    scene["solder"][0]["t"] = [3, 2]
    errors = make_video.check(board)
    assert any("inset 1" in e for e in errors)
    assert any("solder 1" in e for e in errors)


def test_spring_is_whole_bars():
    """Each bar of the Spring bed is 8 eighths and has two bass notes."""
    for line, basses in video_audio.SPRING:
        assert sum(length for _, length in line) == 8
        assert len(basses) == 2
    assert max(video_audio.SPRING_ORDER) == len(video_audio.SPRING) - 1


def test_a_board_scene_is_stretched_with_its_cues():
    """Every time of a board scene follows its narration cues."""
    scene = _rework()["scenes"][0]
    scene.update(say=[{"at": 2, "text": "x"}, {"at": 6, "text": "y"}],
                 mount=[{"refs": ["M7"], "t": 4}])
    assert make_video.check({"lang": "en", "steps": 1,
                             "scenes": [scene]}) == []
    # The first line needs 8s where the storyboard gives it 4.
    video_voice._stretch(scene, [0.0, 2, 6, 10], [0.0, 2, 10, 14])
    assert scene["dur"] == 14
    assert scene["mount"][0]["t"] == 6
    assert scene["cuts"][0]["t"] == [8, 10]
    assert scene["wires"][0]["t"] == [10, 12]
    assert scene["fit"][0]["t"] == 12.5
    assert scene["camera"][1][0] == 8
    assert [cue["at"] for cue in scene["say"]] == [2, 10]
    assert (scene["tags"][0]["from"], scene["tags"][0]["to"]) == (1.0, 6)


def test_the_music_gain_is_bounded():
    """A storyboard sets its bed from 0 to 2 times the written level."""
    board = _rework()
    board["music_gain"] = 0.5
    assert make_video.check(board) == []
    board["music_gain"] = 3
    assert any("music_gain" in e for e in make_video.check(board))


def test_a_plug_board_adds_its_pads():
    """A wire can end at a pad of a scene's USB-C plug board."""
    import video_board

    board = _rework()
    scene = board["scenes"][0]
    scene["plugs"] = [{"name": "USB", "at": [-10, 20], "t": 1}]
    scene["wires"].append({"from": "R1.1", "to": "USB.D-", "plain": True,
                           "gone": [8, 9], "t": [6, 7]})
    assert make_video.check(board) == []
    assert make_video.board_faults(scene, video_board.convert(_PCB)) == []
    scene["wires"][-1]["to"] = "USB.CC"
    assert make_video.board_faults(
        scene, video_board.convert(_PCB)) == ["no pad USB.CC"]
    scene["plugs"][0]["name"] = "U.S"
    assert any("plug 1" in e for e in make_video.check(board))


def test_the_drawn_boards_hold_what_the_storyboards_name():
    """A board in the tree has every pad its scenes point at."""
    drawn = {p.stem: json.loads(p.read_text(encoding="utf-8"))
             for p in (make_video.VIDEO / "boards").glob("*.json")}
    assert "p4-7b-back" in drawn
    seen = 0
    for path in make_video.storyboards():
        board = json.loads(path.read_text(encoding="utf-8"))
        for scene in board["scenes"]:
            if scene["type"] == "board" and scene["board"] in drawn:
                assert make_video.board_faults(
                    scene, drawn[scene["board"]]) == [], path.name
                seen += 1
    assert seen


def test_a_module_brings_its_board_and_its_parts():
    """A footprint named as a module spec takes the spec's model."""
    import video_board

    specs = {"Buck": ((8.0, 6.0), (2.0, 2.0),
                      [("box", 3.0, 1.0, 6.0, 4.0, 4.5, "chip"),
                       ("cyl", 7.0, 5.0, 0.5, 1.0, "black", 1.0)],
                      [("CAN", 4.0, 2.0, 0.8), ("1", 1.0, 1.0, 0.8)],
                      {"2": "GP7_X"}, [("2", 2.0, 2.0)])}
    plan = video_board.convert(_PCB, 0, specs)
    part = next(p for p in plan["parts"] if p["ref"] == "M7")
    # The footprint at (30, 30), a quarter turned, on a board from (10, 20).
    assert part["box"] == [18.0, 4.0, 24.0, 12.0]
    assert part["details"] == [
        {"box": [19.0, 6.0, 22.0, 9.0], "color": "chip"},
        {"at": [23.0, 5.0], "r": 0.5, "color": "black"},
        {"text": "CAN", "at": [20.0, 8.0], "size": 0.8},
        {"text": "2", "at": [20.0, 10.0], "size": 0.8, "pad": True},
        {"text": "GP7", "at": [20.0, 11.27], "size": 0.6, "name": True}]
    assert next(p for p in plan["parts"]
                if p["ref"] == "R1")["details"] == []


def test_board_reads_module_specs_from_a_directory(tmp_path):
    """--modules names the specs; without it no module is read."""
    import video_board

    (tmp_path / "can.py").write_text(
        'FOOTPRINT = {"name": "RP2350-CAN", "board": (21.0, 51.0),\n'
        '             "origin": (1.0, 1.0), "pads": [\n'
        '                 {"num": "1", "x": 0.0, "y": 0.0}],\n'
        '             "fab": [("text", "USB-C", 2.0, 3.0, 0.8)],\n'
        '             "silk": [("text", "CAN", 4.0, 5.0, 0.8)]}\n')
    texts = [said for said, *_ in video_board.modules(tmp_path)[
        "RP2350-CAN"][3]]
    assert texts == ["USB-C", "CAN"]
    assert video_board.modules() == {}


def test_a_dial_scene_shows_faces_and_turns_its_knob():
    """A case scene of the dial model lists its faces and clicks."""
    import render_dial

    board = {"lang": "en", "steps": 1, "scenes": [{
        "type": "case", "model": "dial", "dur": 10,
        "camera": [[0, [0, 100, 100], [0, 20, 0]]],
        "faces": [[0, "bind"], [4, "motor-armed"]],
        "turn": [[0, 0], [5, 0], [7, 67.5]],
        "say": [{"at": 1, "text": "x"}],
        "labels": [{"from": 1, "to": 4, "anchor": "knob", "at": [60, 300],
                    "title": "x"}]}]}
    assert make_video.check(board) == []
    assert make_video.faces(board) == ["bind", "motor-armed"]
    assert set(make_video.faces(board)) <= set(render_dial.FACES)
    # 67.5 degrees are 3 detents of 22.5: 3 clicks.
    kinds = [kind for _, kind, _ in video_audio.events(board)]
    assert kinds.count("tick") == 3
    scene = board["scenes"][0]
    video_voice._stretch(scene, [0.0, 1, 10], [0.0, 1, 19])
    assert scene["faces"][1][0] == 7
    assert scene["turn"][2][0] == 13
    scene["faces"][1] = [0, "x"]
    assert any("faces" in e for e in make_video.check(board))
    for path in make_video.storyboards():
        made = json.loads(path.read_text(encoding="utf-8"))
        assert all(name in render_dial.FACES
                   or render_dial.other(name) is not None
                   for name in make_video.faces(made))


def _run_scene():
    return {"type": "case", "model": "duo", "dur": 20,
            "camera": [[0, [0, 100, 100], [0, 20, 0]]],
            "panel": [[0, "motor-idle", "motor"]],
            "run": {"from": 2, "arm": 5, "to": 18,
                    "throttle": [[8, 0], [12, 40], [14, 40],
                                 [14.2, 0, "press"], [16, 0], [17, 10]]}}


def test_a_run_gives_throttle_and_the_knob_its_turn():
    """A run's throttle is whole percent; the knob turns a detent each."""
    scene = _run_scene()
    board = {"lang": "en", "steps": 1, "scenes": [scene]}
    assert make_video.check(board) == []
    run = scene["run"]
    assert make_video.run_value(run, 6) == 0
    assert make_video.run_value(run, 10) == 20
    assert make_video.run_value(run, 13) == 40
    # A press sets its value at once: no line towards it.
    assert make_video.run_value(run, 14.1) == 40
    assert make_video.run_value(run, 14.3) == 0
    assert make_video.run_value(run, 19) == 10
    # 40 detents up, none for the press, 10 more from 0.
    assert make_video.run_turn(run) == [
        [5, 0.0], [8, 0.0], [12, 900.0], [14, 900.0], [16, 900.0],
        [17, 1125.0]]
    assert "motor-idle" in make_video.views(board)
    run["arm"] = 1
    assert any("run" in e for e in make_video.check(board))


def test_a_run_moves_with_its_narration():
    """A stretched scene stretches its run."""
    scene = _run_scene()
    scene["say"] = [{"at": 1, "text": "x"}]
    video_voice._stretch(scene, [0.0, 1, 20], [0.0, 1, 39])
    assert scene["run"]["arm"] == 9
    assert scene["run"]["throttle"][0][0] == 15
    assert scene["panel"][0][0] == 0


def test_a_nav_run_draws_at_its_gestures_and_turns_the_knob():
    """A nav run's moments are its gestures; a turn takes 0.25s."""
    scene = _run_scene()
    scene["run"] = {"kind": "nav", "view": "overview", "from": 1, "to": 9,
                    "ops": [[3, "t2"], [4, "p"], [6, "t-1"], [8, "h"]]}
    board = {"lang": "en", "steps": 1, "scenes": [scene]}
    assert make_video.check(board) == []
    frames = make_video.run_frames(scene["run"])
    assert [t for t, *_ in frames] == [1, 3, 4, 6, 8]
    assert frames[0][3] == {"RCBENCH_VIDEO_NAV": "t0"}
    assert frames[-1][3] == {"RCBENCH_VIDEO_NAV": "t0,t2,p,t-1,h"}
    assert make_video.run_turn(scene["run"]) == [
        [1, 0.0], [2.75, 0.0], [3, 45.0], [5.75, 45.0], [6, 22.5]]
    kinds = [kind for _, kind, _ in video_audio.events(board)]
    assert kinds.count("tap") == 1 and kinds.count("low") == 1
    scene["run"]["ops"][0][1] = "x3"
    assert any("run" in e for e in make_video.check(board))


def test_a_servo_run_follows_its_angle():
    """A servo run is drawn 10 times a second with the angle so far."""
    run = {"kind": "servo", "from": 1, "to": 2,
           "angle": [[1.5, 0], [2, 10]]}
    scene = _run_scene()
    scene["run"] = run
    assert make_video.check({"lang": "en", "steps": 1,
                             "scenes": [scene]}) == []
    frames = make_video.run_frames(run)
    assert len(frames) == 11
    assert frames[-1][1:3] == ("servo-live", "servo")
    assert frames[-1][3]["RCBENCH_VIDEO_SERVO"].endswith(",6,8,10")
    assert make_video.run_turn(run) == [[1, 0.0], [1.5, 0.0], [2, 225.0]]


def _mini():
    return {"lang": "en", "product": "MiniRcBench", "steps": 1, "scenes": [{
        "type": "case", "model": "mini", "dur": 6,
        "camera": [[0, [90, 120, 170], [0, 14, 0]],
                   [6, [0, 60, 190], [0, 10, 10]]],
        "panel": [[0, "mini-splash"], [3, "mini-supply-sense"]],
        "parts": {"board": [[0, 1], [2, 0]], "screws": [[0, 1], [6, 1]]},
        "flip": [[0, 0], [5, 180]],
        "labels": [{"from": 1, "to": 3, "anchor": "esc", "at": [200, 700],
                    "title": "ESC", "text": "x"}]}]}


def _mini_stills(tmp_path):
    """A stills directory beside a storyboard, as MiniRcBench keeps one."""
    import struct
    import zlib

    def chunk(kind, data):
        body = kind + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body)))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 320, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress((b"\0" + b"\0" * 720) * 320))
           + chunk(b"IEND", b""))
    stills = tmp_path / "stills"
    stills.mkdir()
    for name in ("mini-splash", "mini-supply-sense"):
        (stills / (name + ".png")).write_bytes(png)
    return stills


def test_a_mini_scene_takes_its_parts_stills_and_flip(tmp_path):
    """The MiniRcBench has components, stills and a roll-over of its own."""
    make_video.use_stills(_mini_stills(tmp_path))
    try:
        board = _mini()
        assert make_video.check(board) == []
        # Its screens are stills, not views of the panel's renderer.
        assert make_video.views(board) == {}
        for name in make_video.MINI_UNITS:
            assert name not in ("lid", "rbmod")
    finally:
        make_video.use_stills()


def test_a_mini_scene_refuses_what_it_does_not_have(tmp_path):
    """A component of the case model and an unknown still are errors."""
    make_video.use_stills(_mini_stills(tmp_path))
    try:
        board = _mini()
        board["scenes"][0]["parts"]["rbmod"] = [[0, 1], [2, 0]]
        assert any("rbmod is not one of" in e
                   for e in make_video.check(board))
        board = _mini()
        board["scenes"][0]["panel"][1][1] = "mini-nothing"
        assert any("mini-nothing.png" in e for e in make_video.check(board))
    finally:
        make_video.use_stills()


def test_stills_beside_a_storyboard_come_first(tmp_path):
    """A storyboard given by path takes its stills from stills/ beside it."""
    stills = _mini_stills(tmp_path)
    path = tmp_path / "mini.json"
    path.write_text(json.dumps(_mini()), encoding="utf-8")
    try:
        assert make_video.still("mini-splash") is None
        board = make_video.load(path)
        assert make_video.still("mini-splash") == stills / "mini-splash.png"
        assert make_video.still_dirs() == [stills, make_video.STILLS]
        assert make_video.check(board) == []
        # The view cache names a still outside the repository and
        # follows its content.
        key = make_video._views_key(board)
        (stills / "mini-splash.png").write_bytes(b"other")
        assert make_video._views_key(board) != key
    finally:
        make_video.use_stills()
    assert make_video.still_dirs() == [make_video.STILLS]


def test_the_wiring_drawing_holds_the_pads_of_the_build():
    """The MiniRcBench wiring drawing names the pads its storyboard uses."""
    plan = json.loads((make_video.VIDEO / "boards"
                       / "minircbench-wiring.json").read_text("utf-8"))
    names = {pad["name"] for pad in plan["pads"]}
    assert {"BAT.+", "BAT.-", "J4.11", "BOOST.EN", "INA.T+", "ESC.S",
            "TMT.S"} <= names


def test_a_first_cue_at_zero_stretches():
    """A scene whose first line starts at 0 takes the room its lines need."""
    scene = {"type": "case", "dur": 10, "camera": [[0, 1], [6, 2]],
             "say": [{"at": 0, "text": "a"}, {"at": 4, "text": "b"}]}
    video_voice._stretch(scene, [0.0, 0, 4, 10], [0.0, 0, 7, 13])
    assert scene["dur"] == 13
    assert [key[0] for key in scene["camera"]] == [0, 9]
    assert [cue["at"] for cue in scene["say"]] == [0, 7]


def test_a_track_takes_its_net_name_from_the_list_of_nets():
    """A board from before KiCad 10 names a track's net by its number."""
    import video_board

    old = _PCB.replace(
        ' (gr_rect', ' (net 0 "") (net 1 "A") (net 3 "GND")\n (gr_rect', 1
    ).replace('(layer "F.Cu") (net "A"))', '(layer "F.Cu") (net 1))', 1)
    plan = video_board.convert(old)
    assert [track["net"] for track in plan["tracks"]] == ["A"]
    assert {pad["net"] for pad in plan["pads"]} == {"A", "GND"}


def test_a_silent_bed_keeps_the_narration(tmp_path, monkeypatch):
    """With "music_gain" 0 and no event sound the line is still heard."""
    monkeypatch.setattr(video_audio, "events", lambda board: [])
    import wave

    np = import_or_fail("numpy", "needs numpy")

    board = {"lang": "en", "steps": 0, "music_gain": 0, "scenes": [
        {"type": "end", "dur": 4, "title": "x"}]}
    t = np.arange(video_audio.RATE) / video_audio.RATE
    clip = 0.3 * np.sin(2 * np.pi * 220 * t)
    video_audio.render(board, tmp_path / "a.wav", [(1.0, clip)])
    with wave.open(str(tmp_path / "a.wav")) as wav:
        data = np.frombuffer(wav.readframes(wav.getnframes()), "<i2")
    assert np.abs(data).max() > 10000


def test_ina219_is_said_letter_by_letter():
    """INA stays in capitals for the speaker; a screen's name does not."""
    assert video_voice.spoken("an INA219 board") == "an INA 2 19 board"
    assert video_voice.spoken("open SETUP") == "open Setup"


def test_a_dial_scene_takes_the_parts_of_its_base():
    """The Dial in its base stands apart by its own part names."""
    scene = {"type": "case", "model": "dial", "dur": 10,
             "camera": [[0, [0, 100, 200], [0, 20, 0]]],
             "parts": {"upper": [[0, 1], [4, 0]], "screws": [[0, 1]]},
             "flip": [[0, 0], [8, 180]],
             "labels": [{"from": 1, "to": 4, "anchor": "nut",
                         "at": [1200, 300], "title": "x"}]}
    board = {"lang": "en", "steps": 0, "dial_base": True,
             "scenes": [scene]}
    assert make_video.check(board) == []
    scene["parts"]["lid"] = [[0, 1]]
    assert any("lid is not one of" in e for e in make_video.check(board))


def test_a_part_without_video_is_not_reused(tmp_path):
    """A missing part, an empty one and a stub are captured again."""
    part = tmp_path / "part.mp4"
    assert not make_video._holds_video(part)
    part.write_bytes(b"")
    assert not make_video._holds_video(part)
    part.write_bytes(b"0" * make_video.PART_LEAST)
    assert not make_video._holds_video(part)
    part.write_bytes(b"0" * (make_video.PART_LEAST + 1))
    assert make_video._holds_video(part)


def test_a_step_can_set_its_versions():
    """A tour step with versions is a shot of its own."""
    versions = ["0.24.1", "0.24.2", "0.24.0"]
    board = {"lang": "en", "steps": 1, "scenes": [{"type": "tour", "steps": [
        {"view": "updates", "dur": 3, "n": 1, "title": "x",
         "versions": versions},
        {"view": "updates", "dur": 3, "n": 1, "title": "x"}]}]}
    assert make_video.check(board) == []
    found = make_video.views(board)
    assert found["updates~0.24.1-0.24.2-0.24.0"] == (
        "updates", None, "en", versions)
    assert found["updates"] == ("updates", None, "en")
    board["scenes"][0]["steps"][0]["versions"] = ["0.24", "0.24.2", "0.24.0"]
    assert any("versions" in e for e in make_video.check(board))


def test_the_first_programming_video_tells_one_version_sequence():
    """rbmod-first-program shows 0.24.0, then 0.24.1, then 0.24.2."""
    path = make_video.VIDEO / "storyboards" / "rbmod-first-program.json"
    board = json.loads(path.read_text(encoding="utf-8"))
    assert make_video.check(board) == []
    assert len(board["chapters"]) == board["steps"] == 6
    steps = [s for scene in board["scenes"] if scene["type"] == "tour"
             for s in scene["steps"] if "view" in s]
    running = [s["versions"][0] for s in steps]
    assert running == sorted(running)
    assert running[0] == "0.24.0" and running[-1] == "0.24.2"


def test_a_board_scene_takes_a_tilt_an_entrance_and_windows():
    """A window's pointer names items of that window."""
    scene = {"type": "board", "board": "p4-7b-back", "dur": 10,
             "camera": [[0, [83, 48], 190], [10, [83, 48], 190]],
             "tilt": [15, -15], "enter": {"t": [0.2, 3], "view": "splash"},
             "windows": [{"from": 3, "to": 9, "at": [60, 150], "w": 900,
                          "url": "example.org", "body": [
                              {"button": "Connect", "id": "go"},
                              {"file": "a.bin", "id": "f", "get": [5, 7]}],
                          "pointer": [[4, "go", "click"], [5, "f"]]}]}
    board = {"lang": "en", "steps": 1, "scenes": [scene]}
    assert make_video.check(board) == []
    assert make_video.views(board)["splash"] == ("splash", None, "en")
    scene["windows"][0]["pointer"][0][1] = "nowhere"
    assert any("pointer" in e for e in make_video.check(board))
    scene["windows"][0]["pointer"][0][1] = "go"
    scene["tilt"] = [80, 0]
    assert any("tilt" in e for e in make_video.check(board))


def test_a_rudder_scene_takes_a_run_of_rudder_auto():
    """A run of RUDDER AUTO belongs to the rudder model and its scene."""
    scene = {"type": "case", "model": "rudder", "dur": 60,
             "camera": [[0, [0, 250, 330], [0, 34, 10]],
                        [60, [0, 250, 330], [0, 34, 10]]],
             "rauto": {"at": 5, "speed": 2}, "labels": [],
             "parts": {"imu": [[0, 1], [2, 0]], "tape": [[0, 1], [1, 0]]},
             "deflect": [[0, 0], [5, 0]]}
    board = {"lang": "en", "steps": 1, "scenes": [scene]}
    assert make_video.check(board) == []
    assert make_video.views(board) == {}
    scene["rauto"]["at"] = 70
    assert any("rauto" in e for e in make_video.check(board))
    scene["rauto"]["at"] = 5
    scene["model"] = "duo"
    assert any("rauto" in e for e in make_video.check(board))


def test_a_3d_scene_takes_touches_on_the_panel_and_the_dial():
    """A touch names a screen the model shows, a pixel of it and a time."""
    scene = {"type": "case", "model": "rudder", "dur": 60,
             "camera": [[0, [0, 250, 330], [0, 34, 10]],
                        [60, [0, 250, 330], [0, 34, 10]]],
             "rauto": {"at": 5, "speed": 2}, "labels": [],
             "touches": [
                 {"at": 2, "on": "panel", "xy": [98, 324], "action": "tap"},
                 {"at": 3, "on": "dial", "xy": [120, 120], "action": "hold"},
                 {"run_ms": 2000, "on": "panel", "xy": [512, 528],
                  "action": "hold"}]}
    board = {"lang": "en", "steps": 1, "scenes": [scene]}
    assert make_video.check(board) == []
    scene["touches"][0]["xy"] = [1100, 324]
    assert any("touch 1" in e for e in make_video.check(board))
    scene["touches"][0]["xy"] = [98, 324]
    scene["touches"][1]["xy"] = [120, 300]
    assert any("touch 2" in e for e in make_video.check(board))
    scene["touches"][1]["xy"] = [120, 120]
    scene["touches"][1]["action"] = "swipe"
    assert any("touch 2" in e for e in make_video.check(board))
    scene["touches"][1]["action"] = "hold"
    scene["touches"][0]["at"] = 61
    assert any("touch 1" in e for e in make_video.check(board))
    scene["touches"][0]["at"] = 2
    del scene["rauto"]
    assert any("touch 3" in e for e in make_video.check(board))
    del scene["touches"][2]
    scene["model"] = "dial"
    assert any("touch 1" in e for e in make_video.check(board))


def test_a_german_storyboard_is_written_out_for_a_german_speaker():
    """A German board takes SPELL_DE: units and names as German says."""
    import video_voice

    said = video_voice.spoken("RESERVE 10%: 950 bis 2050us, 22.0° am IMU",
                              "de")
    assert said == ("Resärwe 10 Prozent: 950 bis 2050 Mikrosekunden, "
                    "22.0 Grad am I M U")
    assert video_voice.spoken("über eine Schubstange.", "de") == (
        "über eine Schub Stange.")
    assert video_voice.spoken("hold 2 s", "en") == "hold 2 seconds"
    assert video_voice.spoken("A KST or Chaservo servo holds") == (
        "A K S T or Cha servo holds")
    assert video_voice.spoken("KST and Chaservo servos, a Chaservo") == (
        "K S T and Cha servos, a Cha servo")
    assert video_voice.spoken("KST- oder Chaservo-Servos", "de") == (
        "K S T- oder Tscha-Servos")
    board = {"lang": "de", "scenes": [{"type": "end", "dur": 5,
                                       "say": "Der Dial am KST X10"}]}
    assert video_voice.script(board)[0][3] == "Der Dail am K S T X 10"


def test_a_face_names_a_screen_the_knob_does_nothing_on():
    """other-TITLE-safe and other-TITLE-armed pose the Dial's other face."""
    import render_dial

    assert render_dial.other("other-rudder-auto-armed") == "RUDDER AUTO:armed"
    assert render_dial.other("other-imu-safe") == "IMU"
    assert render_dial.other("other") is None
    assert render_dial.other("other-rudder-auto") is None
    assert render_dial.other("navigation") is None


def _g2p(words):
    """A stand-in for misaki's en.G2P over (text, phonemes) pairs."""
    import types

    tokens = [types.SimpleNamespace(text=text, phonemes=said, whitespace=" ")
              for text, said in words]
    tokens[-1].whitespace = ""
    return lambda text: ("", tokens)


def test_a_negation_keeps_its_vowel_and_a_letter_its_stop():
    """A negation is read with ə; "U." and "U-S-B." end a sentence."""
    said = video_voice.phonemes("", _g2p([
        ("It", "ˌɪt"), ("isn't", "ˈɪzᵊnt"), ("I", "ˌI"), ("M", "ˈɛm"),
        ("U.", "jˈu"), ("Over", "ˈOvəɹ"), ("U-S-B.", "jˌuˈɛsbˈi"),
        ("student", "stˈudᵊnt"), ("e.g.", "ˌiʤˈi"), ("x", None),
        ("end", "ˈɛnd."),
    ]))
    assert said == ("ˌɪt ˈɪzənt ˌI ˈɛm jˈu. ˈOvəɹ jˌuˈɛsbˈi. stˈudᵊnt "
                    "ˌiʤˈi  ˈɛnd.")


class _Kokoro:
    """A stand-in for kokoro_onnx.Kokoro that records create()."""

    def __init__(self):
        self.calls = []

    def create(self, text, **kwargs):
        import numpy as np

        self.calls.append((text, kwargs))
        clip = np.zeros(24000)
        clip[6000:18000] = 0.5
        return clip, 24000


def test_kokoro_reads_misaki_phonemes(monkeypatch):
    """en-us and en-gb go through misaki; another language does not."""
    np = import_or_fail("numpy", "needs numpy")

    speaker, asked = _Kokoro(), []
    monkeypatch.setattr(video_voice, "_SPEAKER", speaker)
    monkeypatch.setattr(video_voice, "_misaki", lambda british: (
        asked.append(british) or _g2p([("isn't", "ˈɪzᵊnt")])))
    clip = video_voice._speak(("isn't", {"name": "af_heart",
                                         "lang": "en-us"}))
    video_voice._speak(("isn't", {"name": "bm_george", "lang": "en-gb"}))
    video_voice._speak(("n'est", {"name": "ff_siwis", "lang": "fr-fr"}))
    assert asked == [False, True]
    assert [(text, kw["is_phonemes"], kw["lang"])
            for text, kw in speaker.calls] == [
        ("ˈɪzənt", True, "en-us"), ("ˈɪzənt", True, "en-gb"),
        ("n'est", False, "fr-fr")]
    # 0.5s of sound inside 1s: trimmed to the sound and its margins.
    want = 0.5 + video_voice.TRIM_LEAD_S + video_voice.TRIM_TAIL_S
    assert abs(len(clip) / video_voice.RATE - want) < 0.002
    assert np.abs(clip).max() == 1.0


def test_trim_keeps_the_sound_and_its_margins():
    """Silence goes from both ends; a quiet consonant above -40dB stays."""
    np = import_or_fail("numpy", "needs numpy")

    rate = 24000
    t = np.arange(rate) / rate
    loud = np.sin(2 * np.pi * 200 * t)
    soft = 0.02 * np.cos(2 * np.pi * 3000 * t[: rate // 20])    # -34dB
    hiss = 0.005 * np.sin(2 * np.pi * 3000 * t[: rate // 20])   # -46dB
    clip = np.concatenate([np.zeros(rate * 3 // 10), hiss, soft, loud,
                           np.zeros(rate // 2)])
    cut = video_voice.trim(clip, rate)
    first = rate * 3 // 10 + len(hiss)
    lead = round(video_voice.TRIM_LEAD_S * rate)
    tail = round(video_voice.TRIM_TAIL_S * rate)
    assert np.array_equal(cut, clip[first - lead:first + len(soft)
                                    + len(loud) + tail])
    # Margins stop at the clip's ends; a silent clip stays whole.
    assert np.array_equal(video_voice.trim(loud, rate), loud)
    assert len(video_voice.trim(np.zeros(100), rate)) == 100
    # Kokoro's own padding: 0.3s before and 0.5s after the sound.
    padded = np.concatenate([np.zeros(rate * 3 // 10), loud,
                             np.zeros(rate // 2)])
    assert abs(len(video_voice.trim(padded, rate)) / rate - 1.06) < 0.001


def test_a_stretched_scene_moves_the_times_of_its_windows():
    """A stretched scene moves the times of its windows."""
    import video_voice
    path = make_video.VIDEO / "storyboards" / "rbmod-first-program.json"
    board = json.loads(path.read_text(encoding="utf-8"))
    scene = next(s for s in board["scenes"] if any(
        "copy" in w for w in s.get("windows", [])))
    was = json.loads(json.dumps(scene))
    cues = [cue["at"] for cue in scene["say"]]
    old = [0.0] + cues + [scene["dur"]]
    new = [0.0, cues[0]] + [round(t + 5.0 * (n + 1), 2)
                            for n, t in enumerate(old[2:])]
    video_voice._stretch(scene, old, new)

    def moved(t):
        n = max(k for k in range(len(old) - 1) if old[k] <= t)
        n = min(n, len(old) - 2)
        return new[n] + (t - old[n]) * (new[n + 1] - new[n]) / (
            old[n + 1] - old[n])

    for now, then in zip(scene["windows"], was["windows"], strict=True):
        assert now["from"] == pytest.approx(moved(then["from"]), abs=0.02)
        assert now["to"] == pytest.approx(moved(then["to"]), abs=0.02)
        assert now.get("at") == then.get("at")
        if "copy" in then:
            assert now["copy"]["t"] == pytest.approx(
                [moved(t) for t in then["copy"]["t"]], abs=0.02)
            assert now["nav"][-1]["on"] is True
            late = [r for r in then["rows"] if "from" in r][0]["from"]
            assert [r for r in now["rows"] if "from" in r][0][
                "from"] == pytest.approx(moved(late), abs=0.02)
        for key, old_key in zip(now.get("pointer", []),
                                then.get("pointer", []), strict=True):
            assert key[0] == pytest.approx(moved(old_key[0]), abs=0.02)
            assert key[1:] == old_key[1:]
    board["scenes"][board["scenes"].index(scene)] = scene
    assert make_video.check(board) == []
