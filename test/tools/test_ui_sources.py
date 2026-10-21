"""ui_sources.py against shared/ui/CMakeLists.txt: every file the panel
builds there is in SOURCES or in EXCLUDED, so a screen split into more
files reaches render_ui.py and frame_cost.py.

SPDX-License-Identifier: MIT
"""

import pathlib
import re

import pytest
import ui_sources

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
UI = "shared/ui"


def panel_ui_sources(text):
    """The .c files of the set(SRCS ...) block of a CMakeLists.txt.

    Args:
        text: The text of shared/ui/CMakeLists.txt.

    Returns:
        The files as repo paths under shared/ui, in the block's order.

    Raises:
        AssertionError: The block is missing, or a line of it, less its
            comment, is not one file name.
    """
    block = re.search(r"set\(SRCS\b(.*?)\n\s*\)", text, re.S)
    assert block, "no set(SRCS ...) in shared/ui/CMakeLists.txt"
    out = []
    for line in block.group(1).splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        assert re.fullmatch(r"[\w.-]+\.c", line), (
            f"set(SRCS ...) holds a line this test does not read: {line!r}")
        out.append(f"{UI}/{line}")
    return out


def missing(built, sources, excluded):
    """The files the panel builds that neither list names.

    Args:
        built: The repo paths the panel's build compiles.
        sources: The renderers' list, ui_sources.SOURCES.
        excluded: The named exclusions, ui_sources.EXCLUDED.

    Returns:
        The paths of built in neither list, in their order.
    """
    return [p for p in built if p not in sources and p not in excluded]


def test_the_parser_reads_the_srcs_block():
    text = "set(SRCS\n    a.c\n    b_c.c\n)\nset(OTHER x.c)\n"
    assert panel_ui_sources(text) == [f"{UI}/a.c", f"{UI}/b_c.c"]


def test_a_comment_and_a_dash_are_read():
    text = ("set(SRCS\n    # the screens\n    a.c  # the first\n"
            "    b-c.c\n)\n")
    assert panel_ui_sources(text) == [f"{UI}/a.c", f"{UI}/b-c.c"]


@pytest.mark.parametrize("line", ["${EXTRA}", "a.c b.c", "a.h"])
def test_a_line_it_cannot_read_fails(line):
    with pytest.raises(AssertionError, match="does not read"):
        panel_ui_sources(f"set(SRCS\n    a.c\n    {line}\n)\n")


def test_a_file_in_neither_list_is_reported():
    built = [f"{UI}/a.c", f"{UI}/a_draw.c", f"{UI}/b.c"]
    assert missing(built, [f"{UI}/a.c"], {f"{UI}/b.c": "why"}) == [
        f"{UI}/a_draw.c"]


def test_every_panel_ui_file_is_listed_or_excluded():
    text = (REPO / UI / "CMakeLists.txt").read_text()
    built = panel_ui_sources(text)
    assert len(built) > 50
    assert missing(built, ui_sources.SOURCES, ui_sources.EXCLUDED) == []


def test_an_exclusion_names_a_built_file_the_list_leaves_out():
    built = panel_ui_sources((REPO / UI / "CMakeLists.txt").read_text())
    for path, why in ui_sources.EXCLUDED.items():
        assert path in built, f"{path} is excluded but not built"
        assert path not in ui_sources.SOURCES, f"{path} is in both"
        assert why.strip(), f"{path} has no reason"


def test_every_source_exists_once():
    assert len(set(ui_sources.SOURCES)) == len(ui_sources.SOURCES)
    for path in ui_sources.SOURCES:
        assert (REPO / path).is_file(), path
    for path in ui_sources.INCLUDES:
        assert (REPO / path).is_dir(), path
