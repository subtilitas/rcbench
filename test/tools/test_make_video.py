"""The storyboard check of tools/make_video.py.

SPDX-License-Identifier: MIT
"""

import copy
import json

import make_video


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
    assert found["servo-hub-rbmod"] == "servo"
    assert found["servo-kst-read-rbmod"] == "servo"


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
