"""check_prose.py: the documents hold to the owner's writing rules.

SPDX-License-Identifier: MIT
"""

import subprocess

import pytest

import check_prose as cp
import check_units as cu


def rules(text, path="docs/A.md"):
    """Lists the rules that fire on a page, one name per hit.

    Args:
        text: The page's text.
        path: Its path in the tree; a German path selects the German rules.

    Returns:
        The rule names in line order.
    """
    return [h[1] for h in cp.find(path, text)]


@pytest.mark.parametrize("text", [
    "a pin -- or none -- here\n",
    "a pin --\tor none\n",
])
def test_a_double_hyphen_between_spaces_is_a_dash(text):
    """The " -- " of a sentence is a dash hit."""
    assert "dash" in rules(text)


@pytest.mark.parametrize("text", [
    "run `tool --check`\n",
    "the flag --check\n",
    "| --- | --- |\n|---|---|\n",
    "a-b--c\n",
    "    tool -- arg\n",
])
def test_a_flag_a_table_rule_and_code_are_no_dash(text):
    """A flag, a table rule, joined hyphens and code are no dash hits."""
    assert "dash" not in rules(text)


@pytest.mark.parametrize("word", ["powerful", "Seamless", "ROBUST", "blazing",
                                  "comprehensive", "simply", "just", "easy"])
def test_each_marketing_word_is_found_in_any_case(word):
    """The eight words are whole-word, case-insensitive hits."""
    assert rules(f"It is {word} here.\n") == ["marketing"]


@pytest.mark.parametrize("text", [
    "adjust the justification\n", "an easygoing default\n",
    "a `just` word\n", "the robustness of it\n",
])
def test_a_part_of_a_longer_word_or_code_is_no_marketing_word(text):
    """Only the whole word counts, and code is skipped."""
    assert rules(text) == []


def test_the_narrator_is_found_in_english_and_german_pages_apart():
    """we/our/us fire in English pages, wir/unser in German pages only."""
    assert rules("We wire it. Ours differs. It gives us time.\n") == \
        ["narrator-en"]
    assert rules("Wir verdrahten es. Unsere Seite.\n", "docs/A-de.md") == \
        ["narrator-de"]
    assert rules("Wir verdrahten es.\n") == []
    assert rules("We wire it.\n", "docs/A-de.md") == []
    assert rules("help\n", "help/content/de/a.md") == []
    assert rules("Wir gehen.\n", "help/content/de/a.md") == ["narrator-de"]


@pytest.mark.parametrize("text", [
    "100 us per step\n", "in us\n", "a, us\n", "| us |\n", "(us)\n",
    "in whole us.\n", "the US bit\n", "WE is write enable\n",
])
def test_us_as_a_unit_and_the_capitals_are_no_narrator(text):
    """The microsecond unit and names in capitals are not hits."""
    assert rules(text) == []


@pytest.mark.parametrize("text", [
    "It was previously a pin.\n", "It now does this.\n",
    "It no longer\nanswers.\n", "It used to answer.\n", "Formerly a pin.\n",
])
def test_history_words_are_found(text):
    """Each history word is a hit, across a line break too."""
    assert rules(text) == ["history-en"]


@pytest.mark.parametrize("text", [
    "A pulse no longer than 2ms.\n", "It is used to drive a pin.\n",
    "It can be used to drive.\n", "NOT NOW closes it.\n", "I know it.\n",
    "a known pin\n",
])
def test_the_passive_used_to_and_no_longer_than_are_no_history(text):
    """The legitimate uses are left out."""
    assert rules(text) == []


@pytest.mark.parametrize("text", [
    "Das war bisher so.\n", "Jetzt geht es.\n", "Es antwortet nicht mehr.\n",
])
def test_german_history_words_are_found_in_german_pages_only(text):
    """bisher, jetzt and nicht mehr fire in -de pages."""
    assert rules(text, "docs/A-de.md") == ["history-de"]
    assert rules(text) == []


def test_nicht_mehr_als_and_a_label_in_capitals_are_no_history():
    """The comparison and the panel's label are left out."""
    assert rules("nicht mehr als 3\n", "docs/A-de.md") == []
    assert rules("JETZT NICHT und NICHT MEHR ERINNERN\n",
                 "docs/A-de.md") == []


@pytest.mark.parametrize("text", ["Done!\n", "Stop! Then wait\n"])
def test_an_exclamation_mark_in_prose_is_found(text):
    """A "!" in a sentence is a hit."""
    assert rules(text) == ["exclamation"]


@pytest.mark.parametrize("text", [
    "![alt](view:motor)\n", "> [!NOTE]\n", "<!-- done! -->\n", "a != b\n",
    "`pulse=$!`\n", "```\nset -x!\n```\n", "\n    pulse=$!\n",
])
def test_images_alerts_comments_and_code_hold_no_exclamation(text):
    """Markup and code are not prose."""
    assert rules(text) == []


@pytest.mark.parametrize("mark", ["✔", "✅", "⚠", "\U0001f600"])
def test_emoji_and_check_marks_are_found(mark):
    """An emoji or a pictograph in prose is a hit."""
    assert rules(f"state {mark} here\n") == ["emoji"]


def test_degree_micro_and_arrows_are_no_emoji():
    """Symbols of units and plain arrows are not hits."""
    assert rules("30°C, 5µs, a → b, ≤ 3\n") == []


@pytest.mark.parametrize("text,rule", [
    ("The ESP32-S3 panel.\n", "stale-esp32s3"),
    ("The ESP32S3 panel.\n", "stale-esp32s3"),
    ("SIMULATION shows.\n", "stale-simulation"),
    ("The coprocessor board.\n", "stale-coprocessor"),
    ("The co-processor board.\n", "stale-coprocessor"),
    ("Der Koprozessor.\n", "stale-coprocessor"),
    ("The usable release.\n", "stale-usable"),
    ("The usable\nrelease.\n", "stale-usable"),
])
def test_stale_names_are_found(text, rule):
    """Each stale name is a hit under its own rule."""
    assert rules(text) == [rule]


@pytest.mark.parametrize("text", [
    "The ESP32-P4 panel.\n", "SIMULATED and SIMULIERT.\n",
    "A simulation of it.\n", "The IOmcu.\n", "The stable release.\n",
])
def test_the_current_names_are_no_hits(text):
    """The names the screens use are not hits."""
    assert rules(text) == []


def test_code_links_and_urls_are_skipped():
    """Fenced and indented code, spans, link targets, URLs and tags."""
    text = ("`we` and ``just `x` easy``\n"
            "[see](docs/just-easy.md) and https://example.org/we/now\n"
            "<a href=\"we\">text</a> <https://x.org/simply>\n"
            "~~~\nwe now just\n~~~\n"
            "```md\nwe\n```\n\n    we now just\n\n"
            "[ref]: http://x.org/just\n"
            "<!-- we now\njust -->\n")
    assert rules(text) == []


def test_a_fence_closes_only_on_its_own_character_and_length():
    """A shorter or different fence inside a block does not end it."""
    lines = [h[0] for h in cp.find("docs/A.md",
                                   "````\n```\njust\n````\nsimply\n")]
    assert lines == [5]
    lines = [h[0] for h in cp.find("docs/A.md",
                                   "```\njust\n~~~\nsimply\n```\neasy\n")]
    assert lines == [6]


def test_an_unclosed_fence_runs_to_the_end():
    """A fence that never closes masks the rest, as Markdown renders it."""
    assert rules("text\n```\njust\n") == []


def test_an_indented_line_in_a_list_is_prose():
    """Four spaces under a list item continue it; they are no code."""
    assert rules("- an item\n\n    and just more\n") == ["marketing"]
    assert rules("para\n\n    code just\n") == []
    assert rules("para\n    still just prose\n") == ["marketing"]


def test_front_matter_is_skipped():
    """The header of a help page is no prose."""
    text = "---\ntitle: just easy\n---\n\n# Page\n\njust\n"
    assert cp.find("help/content/en/a.md", text) == \
        [(7, "marketing", "just", "just")]


def test_a_hit_reports_the_line_the_match_starts_on():
    """Line numbers are those of the page, 1-based."""
    text = "ok\n\n```\ncode\n```\nIt is simply so.\n"
    assert cp.find("docs/A.md", text) == \
        [(6, "marketing", "simply", "It is simply so.")]


def test_one_hit_per_line_and_rule():
    """Two matches of a rule on a line are one hit."""
    assert rules("just simply easy\n") == ["marketing"]
    assert sorted(rules("just -- simply\n")) == ["dash", "marketing"]


def test_the_empty_page_has_no_hit():
    """The limit: no text."""
    assert cp.find("docs/A.md", "") == []
    assert cp.long_sentences("") == []


@pytest.mark.parametrize("path,expected", [
    ("README.md", True), ("README-de.md", True), ("STATUS.md", True),
    ("SECURITY.md", True), ("CONTRIBUTING.md", True),
    ("docs/Screens.md", True), ("docs/img/a.png", False),
    ("help/content/en/motor.md", True), ("testbench/WIRING.md", True),
    ("testbench/captures/reference/README.md", True),
    ("hardware/docs/Pins.md", True), ("hardware/kicad/README.md", True),
    ("protocols/kst/README.md", True), ("protocols/kst/NOTES.md", False),
    ("configurator/README.md", True), ("configurator/core/README.md", True),
    ("configurator/src/NOTES.md", False),
    ("configurator/node_modules/x/README.md", False),
    ("CHANGELOG.md", False), ("CHANGELOG-0.9.md", False),
    ("docs/CHANGELOG.md", False), ("firmware/panel/README.md", False),
    ("help/site/FORMAT.md", False),
])
def test_the_scope(path, expected):
    """Which tracked paths are read."""
    assert cp.in_scope(path) is expected


def test_every_page_in_scope_is_held_to_its_units_by_check_units():
    """The unit rule is check_units.py; no page of the scope escapes it."""
    out = subprocess.run(["git", "ls-files", "-z"], cwd=cp.ROOT, check=True,
                         capture_output=True).stdout.decode()
    pages = [p for p in out.split("\0") if p and cp.in_scope(p)]
    assert pages
    for page in pages:
        if page.startswith("CHANGELOG"):
            continue
        assert cu.covered(page, "x") is not None, page


def test_the_allowlist_file_parses_and_matches_something():
    """Every entry of the committed allowlist is used on the tree."""
    allow = cp.parse_allow(cp.ALLOW.read_text(encoding="utf-8"))
    pages = [p for p in cp.tracked() if cp.in_scope(p)]
    _, _, used = cp.scan(pages, allow)
    assert {e[4] for e in allow} == used


def test_the_allowlist_wants_a_reason_a_known_rule_and_a_text():
    """A malformed line stops the tool."""
    ok = "# why\ndocs/A.md: marketing: just\n"
    assert cp.parse_allow(ok) == [["docs/A.md", "marketing", "just", "why", 2]]
    for bad in ("docs/A.md: marketing: just\n",
                "# why\ndocs/A.md: nonsense: just\n",
                "# why\ndocs/A.md: marketing\n",
                "# why\ndocs/A.md: marketing: \n"):
        with pytest.raises(SystemExit):
            cp.parse_allow(bad)


def test_a_blank_line_ends_a_reason():
    """The next entry needs a reason of its own."""
    with pytest.raises(SystemExit):
        cp.parse_allow("# why\ndocs/A.md: marketing: just\n\n"
                       "docs/A.md: marketing: easy\n")


def test_an_entry_allows_a_hit_by_text_or_pattern(tmp_path):
    """The text is a part of the line; re: is a regular expression."""
    page = tmp_path / "docs" / "A.md"
    page.parent.mkdir()
    page.write_text("It was just now.\nIt is easy.\nIt is easy to see.\n")
    allow = [["docs/A.md", "marketing", "just now", "r", 1],
             ["docs/A.md", "marketing", "re:easy to", "r", 2],
             ["docs/A.md", "history-en", "just now", "r", 3],
             ["docs/B.md", "marketing", "easy", "r", 4]]
    hits, _, used = cp.scan(["docs/A.md"], allow, root=tmp_path)
    assert [(h[1], h[2]) for h in hits] == [(2, "marketing")]
    assert used == {1, 2, 3}


def test_check_fails_on_a_hit_and_on_a_stale_entry(tmp_path, monkeypatch,
                                                   capsys):
    """--check exits 1 for a hit or a stale entry, else 0; the list stays."""
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "A.md").write_text("It is simply so.\n")
    allow = tmp_path / "allow.txt"
    monkeypatch.setattr(cp, "ROOT", tmp_path)
    monkeypatch.setattr(cp, "ALLOW", allow)
    monkeypatch.setattr(cp, "tracked", lambda: ["docs/A.md"])
    assert cp.main(["--check"]) == 1
    assert cp.main([]) == 0
    out = capsys.readouterr()
    assert "docs/A.md:1: [marketing]" in out.out
    assert "rule marketing: 1" in out.err
    assert "file docs/A.md: 1" in out.err
    allow.write_text("# why\ndocs/A.md: marketing: simply so\n")
    assert cp.main(["--check"]) == 0
    (tmp_path / "docs" / "A.md").write_text("It is so.\n")
    assert cp.main(["--check"]) == 1
    assert "matches nothing" in capsys.readouterr().out
    allow.write_text("")
    assert cp.main(["--check"]) == 0


def test_long_sentences_are_listed_only_with_long(tmp_path, monkeypatch,
                                                  capsys):
    """--long adds them and never changes the exit status of --check."""
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "A.md").write_text(
        "A " + "word " * 45 + "end.\n")
    monkeypatch.setattr(cp, "ROOT", tmp_path)
    monkeypatch.setattr(cp, "ALLOW", tmp_path / "allow.txt")
    monkeypatch.setattr(cp, "tracked", lambda: ["docs/A.md"])
    assert cp.main(["--check"]) == 0
    assert "[long]" not in capsys.readouterr().out
    assert cp.main(["--check", "--long"]) == 0
    assert "docs/A.md:1: [long] 47 words" in capsys.readouterr().out


def sentence(n):
    """Returns a sentence of n words."""
    return " ".join(["W"] + ["w"] * (n - 2)) + " end."


@pytest.mark.parametrize("n,found", [(40, False), (41, True), (1, False)])
def test_the_limit_is_more_than_40_words(n, found):
    """40 words pass, 41 are listed."""
    assert bool(cp.long_sentences(sentence(n) + "\n")) is found


def test_long_sentences_split_at_a_sentence_end_not_at_an_abbreviation():
    """Two sentences of 25 words are short; e.g. does not end one."""
    two = sentence(25) + " " + sentence(25) + "\n"
    assert cp.long_sentences(two) == []
    kept = "w " * 25 + "e.g. Foo " + "w " * 20 + "end.\n"
    assert len(cp.long_sentences(kept)) == 1
    split = "w " * 25 + "end. Foo " + "w " * 20 + "end.\n"
    assert cp.long_sentences(split) == []


def test_long_sentences_skip_tables_headings_and_code():
    """Only paragraphs and list items are sentences."""
    row = "| " + " ".join(["w"] * 60) + " |\n"
    head = "# " + " ".join(["w"] * 60) + "\n"
    code = "```\n" + " ".join(["w"] * 60) + "\n```\n"
    assert cp.long_sentences(row + head + code) == []


def test_a_code_span_counts_as_one_word_and_a_list_item_is_a_block():
    """A span of many words is one word; items are not joined."""
    span = "`" + " ".join(["w"] * 60) + "` " + sentence(30) + "\n"
    assert cp.long_sentences(span) == []
    items = "- " + sentence(25) + "\n- " + sentence(25) + "\n"
    assert cp.long_sentences(items) == []
    cont = "- " + sentence(25).replace(" end.", "") + "\n  " + sentence(25) \
        + "\n"
    assert cp.long_sentences(cont) == [(1, 49, cp.long_sentences(cont)[0][2])]


def test_the_report_groups_by_file_and_splits_in_halves():
    """The halves keep a page with its German twin and sum to all hits."""
    counts = {"docs/A.md": 5, "docs/A-de.md": 5, "docs/B.md": 4,
              "docs/C.md": 6}
    a, b = cp.split_halves(counts)
    assert sorted(a + b) == sorted(counts)
    assert ("docs/A.md" in a) == ("docs/A-de.md" in a)
    assert abs(sum(counts[p] for p in a) - sum(counts[p] for p in b)) <= 2
    assert cp.split_halves({"docs/A.md": 3}) == (["docs/A.md"], [])
    assert cp.split_halves({}) == ([], [])


def test_a_large_pair_of_twins_may_be_split_between_the_halves():
    """When keeping twins together leaves the halves far apart, they part."""
    counts = {"docs/A.md": 10, "docs/B.md": 50, "docs/B-de.md": 50,
              "docs/C.md": 10}
    a, b = cp.split_halves(counts)
    assert sum(counts[p] for p in a) == sum(counts[p] for p in b) == 60
    assert ("docs/B.md" in a) != ("docs/B-de.md" in a)


def test_report_text(tmp_path):
    """The report names counts per rule and per file and each hit's line."""
    page = tmp_path / "docs" / "A.md"
    page.parent.mkdir()
    page.write_text("It is simply so.\nA -- b\n")
    hits, _, _ = cp.scan(["docs/A.md"], [], root=tmp_path)
    text = cp.report(hits, ["docs/A.md"], root=tmp_path)
    assert "2 hits in 1 of 1 files." in text
    assert "| marketing | 1 |" in text
    assert "### docs/A.md (2: dash 1, marketing 1)" in text
    assert "- L2 `dash` `--`: A -- b" in text
    assert "Half A: 2 hits" in text


def test_the_report_flag_writes_the_file(tmp_path, monkeypatch):
    """--report FILE writes the Markdown; a missing name stops the tool."""
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "A.md").write_text("A -- b\n")
    monkeypatch.setattr(cp, "ROOT", tmp_path)
    monkeypatch.setattr(cp, "ALLOW", tmp_path / "allow.txt")
    monkeypatch.setattr(cp, "tracked", lambda: ["docs/A.md"])
    out = tmp_path / "out.md"
    assert cp.main(["--report", str(out)]) == 0
    assert "1 hits in 1 of 1 files." in out.read_text()
    with pytest.raises(SystemExit):
        cp.main(["--report"])


def test_a_file_that_is_not_utf8_is_skipped(tmp_path):
    """A page that does not decode is not read."""
    page = tmp_path / "docs" / "A.md"
    page.parent.mkdir()
    page.write_bytes(b"\xff\xfe just")
    assert cp.scan(["docs/A.md"], [], root=tmp_path) == ([], [], set())
    assert cp.scan(["docs/gone.md"], [], root=tmp_path) == ([], [], set())
