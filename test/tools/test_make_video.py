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
    assert said("The IOmcu and the ESC") == "The I O M C U and the E S C"
    assert said("LIMIT RUN at 6.0 V") == "Limit Run at 6.0 volts"
    assert said("Motor & ESC") == "Motor and E S C"


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
