"""check_twins.py: an English page and its German twin by structure.

SPDX-License-Identifier: MIT
"""

import check_twins as ct

EN = """# Title

Lead.

| A | B |
| --- | --- |
| 1 | 2 |
| 3 | 4 |

## One

### Deep

| A |
| --- |
| 1 |

## Two
"""

DE = """# Titel

Vorspann.

| A | B |
| --- | --- |
| 1 | 2 |
| 3 | 4 |

## Eins

### Tief

| A |
| --- |
| 1 |

## Zwei
"""


def diff(english, german):
    """The differences of one pair named en.md and de.md."""
    return ct.compare(english, german, "en.md", "de.md")


def test_a_translation_of_the_same_structure_passes():
    """Other words under the same headings and tables are no difference."""
    assert diff(EN, DE) == []


def test_a_table_counts_its_header_and_rows_not_its_delimiter():
    """A header and two rows are 3; the tables belong to their heading."""
    got = ct.sections(EN)
    assert [(s.level, s.title, s.tables) for s in got] == [
        (0, "", []), (1, "Title", [3]), (2, "One", []), (3, "Deep", [2]),
        (2, "Two", [])]
    assert [s.line for s in got] == [0, 1, 10, 12, 18]


def test_a_missing_heading_is_named_with_both_lines():
    """A section one page lacks fails at the first level that differs."""
    got = diff(EN, DE.replace("### Tief\n\n", ""))
    assert len(got) == 1
    assert "heading 3 differs" in got[0]
    assert "en.md:12 has level 3 \"Deep\"" in got[0]
    assert "de.md:16 has level 2 \"Zwei\"" in got[0]


def test_a_missing_last_heading_is_the_page_ending():
    """A heading only the English page ends with is a difference."""
    got = diff(EN, DE.replace("\n## Zwei\n", ""))
    assert len(got) == 1
    assert "de.md ends" in got[0]


def test_a_missing_heading_of_the_same_level_is_found_at_the_end():
    """The levels still agree one by one; the count does not."""
    got = diff(EN + "\n## Three\n", DE)
    assert len(got) == 1
    assert "heading 5 differs" in got[0]


def test_a_missing_table_row_names_the_section_and_the_counts():
    """One row fewer under a heading fails with both row counts."""
    got = diff(EN, DE.replace("| 3 | 4 |\n", ""))
    assert got == [
        "de.md:1: tables under \"Title\" (en.md:1) differ: rows [3] in "
        "English, [2] in German"]


def test_a_missing_table_is_a_difference():
    """A table one page has and the other lacks fails."""
    got = diff(EN, DE.replace("| A |\n| --- |\n| 1 |\n", ""))
    assert len(got) == 1
    assert "rows [2] in English, [] in German" in got[0]


def test_two_tables_under_one_heading_are_counted_apart():
    """Rows moved from one table to the next are a difference."""
    two = "# T\n\n| a |\n| - |\n| 1 |\n\ntext\n\n| a |\n| - |\n"
    other = "# T\n\n| a |\n| - |\n\ntext\n\n| a |\n| - |\n| 1 |\n"
    assert ct.sections(two)[1].tables == [2, 1]
    assert len(diff(two, other)) == 1


def test_a_table_before_the_first_heading_is_compared():
    """Rows above the title count as well."""
    got = diff("| a |\n| - |\n| 1 |\n\n# T\n", "| a |\n| - |\n\n# T\n")
    assert len(got) == 1
    assert "before a heading" in got[0]


def test_a_fenced_block_holds_no_structure():
    """A # or a | inside a code fence is neither heading nor row."""
    fenced = EN + "\n```sh\n# a comment\n| a pipe\n~~~\n## still code\n```\n"
    assert diff(fenced, DE) == []
    tilde = EN + "\n~~~\n# a comment\n```\n| a pipe\n~~~\n"
    assert diff(tilde, DE) == []


def test_a_hash_without_a_space_is_no_heading():
    """`#define` in prose and a tag like #878 open no section."""
    assert diff(EN + "\n#878 and #define\n", DE) == []


def test_pairs_are_docs_readme_and_help(tmp_path):
    """Each kind of pair is found; a page without a twin is left out."""
    (tmp_path / "docs").mkdir()
    (tmp_path / "help" / "content" / "en").mkdir(parents=True)
    (tmp_path / "help" / "content" / "de").mkdir(parents=True)
    for name in ("docs/A.md", "docs/A-de.md", "docs/Lone.md", "README.md",
                 "README-de.md", "help/content/en/band.md",
                 "help/content/de/band.md", "help/content/en/lone.md"):
        (tmp_path / name).write_text("# T\n", encoding="utf-8")
    got = [(a.relative_to(tmp_path).as_posix(),
            b.relative_to(tmp_path).as_posix())
           for a, b in ct.pairs(tmp_path)]
    assert got == [("docs/A.md", "docs/A-de.md"),
                   ("README.md", "README-de.md"),
                   ("help/content/en/band.md", "help/content/de/band.md")]


def test_check_reports_a_pair_by_its_path(tmp_path):
    """A difference is reported with the German file's path in the tree."""
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "A.md").write_text(EN, encoding="utf-8")
    (tmp_path / "docs" / "A-de.md").write_text(
        DE.replace("| 3 | 4 |\n", ""), encoding="utf-8")
    got = ct.check(tmp_path)
    assert len(got) == 1
    assert got[0].startswith("docs/A-de.md:1: ")


def test_the_tree_s_pairs_agree():
    """Every pair of the repository has the same structure."""
    assert ct.check() == []
    assert len(ct.pairs()) >= 62


def test_main_exits_1_on_a_difference(tmp_path, monkeypatch, capsys):
    """The command prints each difference and fails."""
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "A.md").write_text(EN, encoding="utf-8")
    (tmp_path / "docs" / "A-de.md").write_text(DE, encoding="utf-8")
    monkeypatch.setattr(ct, "REPO", tmp_path)
    assert ct.main(["--check"]) == 0
    (tmp_path / "docs" / "A-de.md").write_text(DE + "\n## Mehr\n",
                                               encoding="utf-8")
    assert ct.main(["--check"]) == 1
    assert "heading 5 differs" in capsys.readouterr().out
