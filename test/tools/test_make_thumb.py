"""tools/make_thumb.py: the thumb block, the bare board and the layout.

SPDX-License-Identifier: MIT
"""

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import make_thumb  # noqa: E402
import video_check  # noqa: E402

THUMB = {"at": 3, "lines": ["TOP", "*MARKED"], "crop": [0, 0, 1920],
         "side": "l", "accent": "#ff0000", "size": 120}
SCENE = {"type": "case", "dur": 5,
         "camera": [[0, [0, 200, 300], [0, 0, 0]]]}


def test_a_full_block_passes():
    """Every key at a valid value."""
    assert video_check.check_thumb(THUMB) == []
    assert video_check.check_thumb({"at": 0, "lines": ["A"]}) == []


@pytest.mark.parametrize("change, word", [
    ({"at": -1}, "at is"),
    ({"lines": []}, "lines holds"),
    ({"lines": ["A", "B", "C", "D"]}, "lines holds"),
    ({"lines": ["*"]}, "lines holds"),
    ({"crop": [0, 0, 300]}, "crop is"),
    ({"crop": [100, 0, 1920]}, "crop is"),
    ({"crop": [0, 100, 1920]}, "crop is"),
    ({"side": "m"}, "side is"),
    ({"accent": "red"}, "accent is"),
    ({"size": 20}, "size is"),
    ({"of": 3}, "of names"),
])
def test_a_wrong_key_is_named(change, word):
    """One fault, with the key's name."""
    errors = video_check.check_thumb({**THUMB, **change})
    assert len(errors) == 1 and word in errors[0]


def test_scene_and_of_exclude_each_other():
    """A block has a scene of its own or another video's frame."""
    errors = video_check.check_thumb({**THUMB, "of": "stop", "scene": SCENE})
    assert any("exclude" in line for line in errors)
    assert video_check.check_thumb({**THUMB, "scene": {"type": "x"}})


def test_check_reads_the_block():
    """A storyboard's fault in thumb is a fault of the storyboard."""
    board = {"lang": "en", "scenes": [SCENE], "thumb": {"at": 1}}
    assert any(line.startswith("thumb") for line in video_check.check(board))
    assert video_check.check({"lang": "en", "scenes": [SCENE],
                              "bare": 1}) == ["bare is true or false"]


def test_bare_board_keeps_the_video_or_takes_the_scene():
    """Bare is set, thumb and voice are gone; a scene replaces the rest."""
    board = {"lang": "en", "steps": 2, "chapters": ["a", "b"],
             "voice": {"name": "x"}, "scenes": [SCENE, SCENE],
             "thumb": THUMB}
    bare = make_thumb.bare_board(board)
    assert bare["bare"] is True and "thumb" not in bare
    assert "voice" not in bare and len(bare["scenes"]) == 2
    own = make_thumb.bare_board({**board, "thumb": {**THUMB, "scene": SCENE}})
    assert own["scenes"] == [SCENE] and own["steps"] == 0
    assert "chapters" not in own


def test_every_storyboard_block_is_valid():
    """Each thumb block in the tree passes, and of names a storyboard."""
    boards = sorted((ROOT / "tools/video/storyboards").glob("*.json"))
    found = 0
    for path in boards:
        thumb = json.loads(path.read_text(encoding="utf-8")).get("thumb")
        if thumb is None:
            continue
        found += 1
        assert video_check.check_thumb(thumb) == [], path.name
        if "of" in thumb:
            assert (path.parent / f"{thumb['of']}.json").is_file()
    assert found == 14


def test_compose_lays_out_1280_by_720():
    """The size, the accent bar, the wedge's side and the marked line."""
    image = pytest.importorskip("PIL.Image")
    try:
        font = make_thumb.font_file()
    except SystemExit:
        pytest.skip("Inter Black is not installed")
    frame = image.new("RGB", (1920, 1080), (200, 200, 200))
    out = make_thumb.compose(frame, THUMB, font)
    assert out.size == (1280, 720)
    assert out.getpixel((640, 715)) == (255, 0, 0)
    # The wedge darkens the text's side and leaves the other.
    assert sum(out.getpixel((20, 300))) < 120 < sum(out.getpixel((1260, 300)))
    reds = sum(1 for x in range(56, 700, 3) for y in range(360, 480, 3)
               if out.getpixel((x, y)) == (255, 0, 0))
    assert reds > 50
    right = make_thumb.compose(frame, {**THUMB, "side": "r"}, font)
    assert sum(right.getpixel((1260, 300))) < 120
