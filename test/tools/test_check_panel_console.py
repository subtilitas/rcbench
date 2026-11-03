"""Tests for check_panel_console.py.

SPDX-License-Identifier: MIT
"""

import check_panel_console as cp

ON = b"\x7fELF\0" + b"\0".join(cp.MARKS)
OFF = b"\x7fELF" + b"\0" * 32 + b"console_hw\0"


def test_an_image_with_the_marks_matches_on_and_not_off():
    """An image with the marks matches ON and fails OFF."""
    assert cp.problems(ON, True) == []
    assert len(cp.problems(ON, False)) == len(cp.MARKS)


def test_an_image_without_them_matches_off_and_not_on():
    """An image without the marks matches OFF and fails ON."""
    assert cp.problems(OFF, False) == []
    assert len(cp.problems(OFF, True)) == len(cp.MARKS)


def test_one_mark_is_enough_to_fail_off():
    """Any one mark fails OFF and is named."""
    for mark in cp.MARKS:
        assert cp.problems(mark, False) == [f"holds {mark.decode()}"]


def test_a_wrong_call_is_refused():
    """A call without a setting and a path ends 2."""
    assert cp.main(["check"]) == 2
