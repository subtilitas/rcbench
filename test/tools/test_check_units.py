"""check_units.py: a number and its unit with no space between.

SPDX-License-Identifier: MIT
"""

import pytest

import check_units as cu


def gaps(path, text):
    """Lists each hit's line with its space marked.

    Args:
        path: The file's path in the tree.
        text: Its text.

    Returns:
        Each hit's line, the space before the unit replaced by "|".
    """
    out = []
    for _, gap, line in cu.find(path, text):
        start = text.index(line)
        cut = [k - start for k in gap]
        out.append(line[:cut[0]] + "|" + line[cut[-1] + 1:])
    return out


@pytest.mark.parametrize("prose", [
    "settle 40 s a search", "6.00 V", "20 ms", "1.6 MB/s", "180 °", "100 mΩ",
    "4.8 to 6.0 V", "100 %.", "0,42 g", "1024x600 px", "2392..14639 rpm",
    "120 Ohm", "30 °C", "10 min", "**97,9 %**",
])
def test_a_number_with_a_space_before_its_unit_is_found(prose):
    """A number, a space and a unit symbol is one hit."""
    assert len(gaps("docs/A.md", prose + "\n")) == 1


@pytest.mark.parametrize("prose", [
    "40s", "6.00V", "3 servos", "8 values", "115200 baud", "1024x600",
    "20 AWG", "frames=2 ms=110", "+-0.1", "GP13 A", "slot A",
    "### 8.4 A run", "60 ohms", "40 sec",
])
def test_a_word_a_key_or_a_section_number_is_not_a_unit(prose):
    """A word, a key=value, a size or a section number is no hit."""
    assert gaps("docs/A.md", prose + "\n") == []


def test_a_format_string_of_the_interface_is_held_and_a_log_line_is_not():
    """An interface file's format is held, a log line's is not."""
    src = ('snprintf(b, n, "%u ms", t);\n'
           'snprintf(b, n, "%u.%02u V", a, b);\n'
           'snprintf(b, n, "%d %%", p);\n'
           'snprintf(b, n, "%6s %-6s %+d %.*f", a, b, c, d, e);\n')
    assert gaps("shared/ui/x.c", src) == [
        'snprintf(b, n, "%u|ms", t);',
        'snprintf(b, n, "%u.%02u|V", a, b);',
        'snprintf(b, n, "%d|%%", p);',
    ]
    assert gaps("firmware/panel/main/x.c", src) == []


def test_a_number_and_its_unit_in_two_literals_are_found():
    """A number and its unit split over two literals is one hit."""
    src = 'UI_TEXT(X, 0,\n    "in the last 200 "\n    "ms",\n    NULL)\n'
    hits = cu.find("shared/text/include/ui_text.def", src)
    assert len(hits) == 1
    assert src[hits[0][1][0]] == " "


def test_a_comment_is_held_in_every_language():
    """Comments of C, Python, YAML and PIO are held."""
    assert gaps("a.c", "/* every 20 ms */ x = 1; // 5 V\n") == [
        "/* every 20|ms */ x = 1; // 5 V",
        "/* every 20 ms */ x = 1; // 5|V",
    ]
    assert len(gaps("a.py", '"""Wait 3 s."""\nx = "3 s"  # 3 s\n')) == 2
    assert len(gaps("a.yml", 'run: echo "# 3 s"  # 3 s\n')) == 1
    assert gaps("a.pio", "; 2 us a bit\n") == ["; 2|us a bit"]


def test_only_the_unreleased_changelog_is_held():
    """A released CHANGELOG section is history and not held."""
    text = "# Changelog\n\n## Unreleased\n\n- 5 ms\n\n## 0.1.0\n\n- 5 ms\n"
    assert len(gaps("CHANGELOG.md", text)) == 1


def test_a_block_a_tool_writes_is_left_as_printed():
    """The frame cost table stays as frame_cost.py prints it."""
    text = ("5 ms\n<!-- framecost:start -->\nframe 7,874 984 KiB\n"
            "<!-- framecost:end -->\n")
    assert gaps("docs/Performance.md", text) == ["5|ms"]


def test_a_page_outside_the_documents_is_not_held():
    """Research records and stray pages are not held."""
    assert gaps("hardware/research/round1/T1/a.md", "5 ms\n") == []
    assert gaps("notes/a.md", "5 ms\n") == []


def test_an_allowlist_entry_needs_a_reason():
    """An entry needs a reason; a re: entry is a pattern."""
    with pytest.raises(SystemExit):
        cu.parse_allow("docs/A.md: 5 ms\n")
    entries = cu.parse_allow(
        "# the console prints it\ndocs/A.md: re:\\d ms\n")
    assert entries[0][:3] == ["docs/A.md", "re:\\d ms",
                              "the console prints it"]
    assert cu.allows(entries[0][1], "took 5 ms")
    assert not cu.allows(entries[0][1], "took 5ms")
