"""render_ui.py --fit: an overflow fails in either language.

An overflow fails in either language unless it is a
known one, and a known one that is gone fails too.

SPDX-License-Identifier: MIT
"""

import render_ui as ui

KNOWN = {("setup", "Too long a help line", "is cut")}
VIEWS = {"setup", "logs"}


def tags(findings, known=KNOWN, views=VIEWS):
    """The judged (tag, language, view) triples and the stale entries."""
    judged, stale = ui.judge(findings, known, views)
    return [(tag, lang, view) for tag, lang, view, _, _ in judged], stale


def test_an_english_overflow_fails():
    """An English overflow fails."""
    assert tags([("en", "logs", "Wide", "is 9 px wide in a 8 px box")],
                known=set()) == ([("FAIL", "en", "logs")], [])


def test_a_german_overflow_fails():
    """A German overflow fails."""
    assert tags([("de", "logs", "Breit", "is cut")], known=set()) == (
        [("FAIL", "de", "logs")], [])


def test_a_known_english_overflow_is_listed_and_does_not_fail():
    """A known English overflow is listed and does not fail."""
    assert tags([("en", "setup", "Too long a help line", "is cut")]) == (
        [("known", "en", "setup")], [])


def test_the_german_copy_of_a_known_overflow_does_not_fail():
    """The German copy of a known overflow does not fail."""
    out, stale = tags([
        ("en", "setup", "Too long a help line", "is cut"),
        ("de", "setup", "Too long a help line", "is cut"),
        ("de", "setup", "Zu lange Hilfezeile", "is cut"),
    ])
    assert out == [("known", "en", "setup"), ("known", "de", "setup"),
                   ("FAIL", "de", "setup")]
    assert stale == []


def test_a_known_overflow_that_moved_fails():
    """A known overflow that moved fails."""
    out, stale = tags([("en", "setup", "Too long a help line",
                        "is cut further")])
    assert out == [("FAIL", "en", "setup")]
    assert stale == [("setup", "Too long a help line", "is cut")]


def test_a_known_overflow_that_is_gone_is_stale():
    """A known overflow that is gone is stale."""
    assert tags([]) == ([], [("setup", "Too long a help line", "is cut")])


def test_a_view_that_was_not_drawn_has_no_stale_entry():
    """A view that was not drawn has no stale entry."""
    assert tags([], views={"logs"}) == ([], [])


def test_every_known_overflow_names_a_view_the_tool_draws():
    """Every known overflow names a view the tool draws."""
    for view, text, why in ui.KNOWN_OVERFLOWS:
        assert view in ui.SCREENS
        assert text and why


def text(x, w, clip_x, clip_w, s="SUPPLY"):
    """A traced string at @p x, @p w px wide, in a clip from @p clip_x."""
    return ("T", ui.Rect(x, 100, w, 20), ui.Rect(clip_x, 60, clip_w, 540),
            None, s)


def test_a_string_wholly_outside_its_clip_is_not_drawn():
    """A string wholly outside its clip is not drawn."""
    # A tile's name on the half the screen guide's dock takes.
    assert ui.fit_problems([text(577, 120, 0, 528)]) == []
    assert ui.fit_problems([text(21, 220, 496, 528)]) == []


def test_a_string_cut_by_its_clip_still_fails():
    """A string cut by its clip still fails."""
    found = ui.fit_problems([text(500, 60, 0, 528)])
    assert len(found) == 1 and "is cut at the edge" in found[0][1]
    found = ui.fit_problems([text(470, 60, 496, 528)])
    assert len(found) == 1 and "is cut at the edge" in found[0][1]


def dock_line(x):
    """The guide's web line at @p x, which names the dock's side."""
    return ("T", ui.Rect(x, 560, 300, 20), ui.Rect(x - 8, 60, 480, 540),
            None, "More: rcbench/help")


def test_a_guide_view_leaves_the_live_half_to_the_view_it_opens_over():
    """A guide view leaves the live half to the view it opens over."""
    # SERVO's SETTINGS tab, x 524 to 604, cut by the live half with the
    # dock on the right: servo-settings measures it.
    assert ui.fit_problems([text(524, 80, 0, 528), dock_line(552)],
                           guide=True) == []
    # The same at x 470 with the dock on the left.
    assert ui.fit_problems([text(470, 60, 496, 528), dock_line(24)],
                           guide=True) == []
    # Without the web line the side is unknown: it is measured.
    found = ui.fit_problems([text(524, 80, 0, 528)], guide=True)
    assert len(found) == 1 and "is cut at the edge" in found[0][1]
    # A string the dock draws is measured.
    found = ui.fit_problems([text(1000, 60, 544, 480), dock_line(552)],
                            guide=True)
    assert len(found) == 1 and "is cut at the edge" in found[0][1]


def badge(x, y, num="1"):
    """A guide's badge with @p num at (@p x, @p y), as the marks draw it."""
    return ("T", ui.Rect(x + 9, y + 4, 10, 20), ui.Rect(0, 0, 1024, 600),
            ui.Rect(x, y, 28, 28), num)


def test_a_badge_over_a_string_of_the_screen_fails():
    """A badge over a string the screen drew is a finding."""
    found = ui.badge_problems([text(600, 60, 496, 528, "VOLT"),
                               dock_line(24), badge(604, 96)])
    assert found == [("VOLT", "is under badge 1 at x=604 y=96")]


def test_a_badge_beside_a_string_passes():
    """A badge clear of the string, or over its padding spaces, passes."""
    assert ui.badge_problems([text(600, 60, 496, 528, "VOLT"),
                              dock_line(24), badge(664, 96)]) == []
    assert ui.badge_problems([text(600, 100, 496, 528, "VOLT      "),
                              dock_line(24), badge(664, 96)]) == []


def test_only_badges_after_the_web_line_on_the_live_half_count():
    """The dock's own badges and a screen's 28px digit are not marks."""
    # The dock's list badge, on the dock's half.
    assert ui.badge_problems([text(30, 60, 0, 528, "LIVE"),
                              dock_line(24), badge(24, 96)]) == []
    # A digit the screen draws before the dock is not a badge.
    assert ui.badge_problems([badge(604, 96), text(600, 60, 496, 528),
                              dock_line(24)]) == []
    # No guide: nothing to check.
    assert ui.badge_problems([text(600, 60, 496, 528), badge(604, 96)]) == []


def test_a_string_hidden_by_a_later_fill_is_not_under_the_badge():
    """A string a fill covered before the marks shows nothing to cover."""
    fill = ("B", ui.Rect(590, 90, 100, 40), ui.Rect(0, 0, 1024, 600))
    assert ui.badge_problems([text(600, 60, 496, 528, "VOLT"), fill,
                              dock_line(24), badge(604, 96)]) == []
