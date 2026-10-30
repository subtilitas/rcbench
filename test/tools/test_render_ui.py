"""render_ui.py --fit: an overflow fails in either language unless it is a
known one, and a known one that is gone fails too.

SPDX-License-Identifier: MIT
"""

import render_ui as ui

KNOWN = {("setup", "Too long a help line", "is cut")}
VIEWS = {"setup", "logs"}


def tags(findings, known=KNOWN, views=VIEWS):
    judged, stale = ui.judge(findings, known, views)
    return [(tag, lang, view) for tag, lang, view, _, _ in judged], stale


def test_an_english_overflow_fails():
    assert tags([("en", "logs", "Wide", "is 9 px wide in a 8 px box")],
                known=set()) == ([("FAIL", "en", "logs")], [])


def test_a_german_overflow_fails():
    assert tags([("de", "logs", "Breit", "is cut")], known=set()) == (
        [("FAIL", "de", "logs")], [])


def test_a_known_english_overflow_is_listed_and_does_not_fail():
    assert tags([("en", "setup", "Too long a help line", "is cut")]) == (
        [("known", "en", "setup")], [])


def test_the_german_copy_of_a_known_overflow_does_not_fail():
    out, stale = tags([
        ("en", "setup", "Too long a help line", "is cut"),
        ("de", "setup", "Too long a help line", "is cut"),
        ("de", "setup", "Zu lange Hilfezeile", "is cut"),
    ])
    assert out == [("known", "en", "setup"), ("known", "de", "setup"),
                   ("FAIL", "de", "setup")]
    assert stale == []


def test_a_known_overflow_that_moved_fails():
    out, stale = tags([("en", "setup", "Too long a help line",
                        "is cut further")])
    assert out == [("FAIL", "en", "setup")]
    assert stale == [("setup", "Too long a help line", "is cut")]


def test_a_known_overflow_that_is_gone_is_stale():
    assert tags([]) == ([], [("setup", "Too long a help line", "is cut")])


def test_a_view_that_was_not_drawn_has_no_stale_entry():
    assert tags([], views={"logs"}) == ([], [])


def test_every_known_overflow_names_a_view_the_tool_draws():
    for view, text, why in ui.KNOWN_OVERFLOWS:
        assert view in ui.SCREENS
        assert text and why


def text(x, w, clip_x, clip_w, s="SUPPLY"):
    """A traced string at @p x, @p w px wide, in a clip from @p clip_x."""
    return ("T", ui.Rect(x, 100, w, 20), ui.Rect(clip_x, 60, clip_w, 540),
            None, s)


def test_a_string_wholly_outside_its_clip_is_not_drawn():
    # A tile's name on the half the screen guide's dock takes.
    assert ui.fit_problems([text(577, 120, 0, 528)]) == []
    assert ui.fit_problems([text(21, 220, 496, 528)]) == []


def test_a_string_cut_by_its_clip_still_fails():
    found = ui.fit_problems([text(500, 60, 0, 528)])
    assert len(found) == 1 and "is cut at the edge" in found[0][1]
    found = ui.fit_problems([text(470, 60, 496, 528)])
    assert len(found) == 1 and "is cut at the edge" in found[0][1]
