"""check_docs.py: the numbers a page states are held to their constants."""

import re

import pytest

import check_docs as cd

TABLE = {
    "PERIOD_MS": "20u",
    "GOOD_RUN": "4u",
    "SETTLE_MS": "(GOOD_RUN * PERIOD_MS + PERIOD_MS)",
    "ALIAS": "PERIOD_MS",
    "HEX": "0x5D",
    "RATIO": "0.020f",
    "PIN": "GPIO_NUM_6",
    "CAST": "((uint16_t)7)",
    "TWICE": None,                    # defined with two values
    "TEXT": '"hello"',
}


@pytest.mark.parametrize("name, value", [
    ("PERIOD_MS", 20), ("SETTLE_MS", 100), ("ALIAS", 20), ("HEX", 0x5D),
    ("RATIO", 0.02), ("PIN", 6), ("CAST", 7),
])
def test_a_constant_is_a_number_an_alias_or_arithmetic(name, value):
    assert cd.constant(name, TABLE) == pytest.approx(value)


@pytest.mark.parametrize("name", ["TWICE", "TEXT", "MISSING"])
def test_what_is_not_one_number_is_unknown(name):
    assert cd.constant(name, TABLE) is None


def test_a_number_is_read_as_its_language_writes_it():
    assert cd.doc_number("15,600", german=False) == 15600
    assert cd.doc_number("0.020", german=False) == pytest.approx(0.02)
    assert cd.doc_number("15 600", german=True) == 15600
    assert cd.doc_number("0,020", german=True) == pytest.approx(0.02)
    assert cd.doc_number("seven", german=False) == 7
    assert cd.doc_number("sieben", german=True) == 7
    assert cd.doc_number("many", german=False) is None


@pytest.fixture
def tree(tmp_path, monkeypatch):
    monkeypatch.setattr(cd, "REPO", tmp_path)
    return tmp_path


def facts(tree, text, fact):
    (tree / "Page.md").write_text(text, encoding="utf-8")
    problems = []
    cd.check_facts(problems, facts=[fact], table=TABLE)
    return problems


FACT = ("Page.md", rf"edges every {cd.N} ms and waits {cd.N} ms",
        ("PERIOD_MS", "GOOD_RUN * PERIOD_MS"))


def test_a_statement_that_agrees_passes(tree):
    assert facts(tree, "It edges every 20 ms and\nwaits 80 ms.", FACT) == []


def test_a_statement_that_disagrees_fails(tree):
    out = facts(tree, "It edges every 25 ms and waits 80 ms.", FACT)
    assert out == ["Page.md: says 25 in 'edges every 25 ms and waits 80 "
                   "ms'; PERIOD_MS makes it 20"]


def test_a_statement_that_is_gone_fails(tree):
    out = facts(tree, "It edges now and then.", FACT)
    assert len(out) == 1 and out[0].startswith("Page.md: no statement")


def test_a_constant_with_two_values_fails(tree):
    out = facts(tree, "limit 5 ms", ("Page.md", rf"limit {cd.N} ms",
                                     ("TWICE",)))
    assert out == ["Page.md: TWICE is not a constant with one value in "
                   "the tree"]


def test_a_factor_changes_the_unit(tree):
    fact = ("Page.md", rf"after {cd.N} s", (("SETTLE_MS", 0.001),))
    assert facts(tree, "after 0.1 s", fact) == []
    assert len(facts(tree, "after 1 s", fact)) == 1


def test_every_fact_has_one_value_per_group():
    for page, pattern, wants in cd.FACTS:
        assert re.compile(pattern).groups == len(wants), (page, pattern)
        assert (cd.REPO / page).is_file(), page


def test_the_tree_meets_every_fact():
    problems = []
    cd.check_facts(problems)
    assert problems == []


def test_defines_reads_a_value_and_marks_a_conflict(tree):
    (tree / "shared").mkdir()
    (tree / "firmware").mkdir()
    (tree / "shared" / "a.h").write_text(
        "#define ONE 1u /* one */\n#define TWO \\\n    (ONE + 1)\n"
        "#define GUARD\n#define BOTH 3\n", encoding="utf-8")
    (tree / "firmware" / "b.c").write_text(
        "#define BOTH 4\n#define ONE 1u // again\n", encoding="utf-8")
    table = cd.defines()
    assert table["ONE"] == "1u"
    assert cd.constant("TWO", table) == 2
    assert "GUARD" not in table
    assert table["BOTH"] is None


def rows(tree, text, name="Servo.md"):
    page = tree / name
    page.write_text(text, encoding="utf-8")
    problems = []
    cd.check_constant_tables(problems, table=TABLE, docs=[page])
    return problems


def test_a_constant_row_is_held_to_its_header(tree):
    good = "| `PERIOD_MS` | 20 ms | the period |\n"
    assert rows(tree, good) == []
    out = rows(tree, "| `PERIOD_MS` | 25 ms | the period |\n")
    assert out == ["Servo.md:1: gives PERIOD_MS as 25; the header says 20"]


def test_a_german_constant_row_uses_a_comma(tree):
    assert rows(tree, "| `RATIO` | 0,020 A | x |\n", "Servo-de.md") == []
    assert len(rows(tree, "| `RATIO` | 0,030 A | x |\n", "Servo-de.md")) == 1


def test_a_row_that_names_no_constant_is_left_alone(tree):
    assert rows(tree, "| `WAITING` | 500 ms | a label |\n") == []


CI = """
      - name: one
        run: python3 tools/frame_cost.py frame sim supply --max-lines 15600
      - name: two
        run: python3 tools/frame_cost.py servo --max-lines 17000
             && python3 tools/frame_cost.py servo-grip --max-lines 4000
      - name: three
        run: python3 tools/frame_cost.py analyser logs
             picker --max-lines 1200
      - name: four
        run: python3 tools/frame_cost.py a-sim b-sim --max-lines 2800
      - name: doc
        run: python3 tools/frame_cost.py --check-doc
"""

HEADER = "| Modes | Ceiling (fills) | Catches |"
DOC = HEADER + """
| --- | ---: | --- |
| `frame`, `sim`, `supply` | 15,600 | a |
| `servo` | 17,000 | b |
| `servo-grip` | 4,000 | c |
| the three per-screen modes | 1,200 | d |
| the two `-sim` modes | 2,800 | e |

After the table.
"""


def test_ci_ceilings_reads_every_call():
    assert cd.ci_ceilings(CI) == [
        (frozenset({"frame", "sim", "supply"}), 15600),
        (frozenset({"servo"}), 17000),
        (frozenset({"servo-grip"}), 4000),
        (frozenset({"analyser", "logs", "picker"}), 1200),
        (frozenset({"a-sim", "b-sim"}), 2800),
    ]


def ceilings(tree, doc, ci=CI):
    page = tree / "Performance.md"
    page.write_text(doc, encoding="utf-8")
    problems = []
    cd.check_ceilings(problems, ci_text=ci, pages=[(page, HEADER)])
    return problems


def test_a_ceiling_table_that_matches_ci_passes(tree):
    assert ceilings(tree, DOC) == []


def test_a_changed_ceiling_fails(tree):
    out = ceilings(tree, DOC.replace("15,600", "15,700"))
    assert out == ["Performance.md: the ceiling table gives '`frame`, "
                   "`sim`, `supply`' as 15700; ci.yml holds it to 15600"]


def test_a_changed_count_fails(tree):
    out = ceilings(tree, DOC.replace("the three per", "the four per"))
    assert len(out) == 2            # the row fits no call; a call has no row


def test_a_mode_ci_holds_and_the_table_omits_fails(tree):
    out = ceilings(tree, DOC.replace("| `servo-grip` | 4,000 | c |\n", ""))
    assert out == ["Performance.md: the ceiling table has no row for "
                   "servo-grip, which ci.yml holds to 4000"]


def test_a_missing_table_fails(tree):
    out = ceilings(tree, "no table here")
    assert out == [f"Performance.md: no table headed '{HEADER}'"]


def test_the_tree_passes_the_new_checks():
    problems = []
    cd.check_constant_tables(problems)
    cd.check_ceilings(problems)
    cd.check_pin_counts(problems)
    assert problems == []


def modules_tree(tree, shared, protocols, listed):
    for root, names in (("shared", shared), ("protocols", protocols)):
        for name in names:
            (tree / root / name).mkdir(parents=True)
    (tree / "docs").mkdir()
    (tree / "docs" / "Building.md").write_text(
        "```\n" + "".join("    %s/   text\n" % n for n in listed) + "```\n",
        encoding="utf-8")
    problems = []
    cd.check_shared_modules(problems)
    return problems


def test_the_tree_lists_the_modules_of_both_roots(tree, monkeypatch):
    monkeypatch.setattr(cd, "DOCS", tree / "docs")
    assert modules_tree(tree, ["gfx"], ["ppm"], ["gfx", "ppm"]) == []
    assert cd.shared_modules() == ["gfx", "ppm"]
    assert cd.module_dirs()["ppm"] == tree / "protocols" / "ppm"


def test_a_protocol_module_the_tree_omits_fails(tree, monkeypatch):
    monkeypatch.setattr(cd, "DOCS", tree / "docs")
    assert modules_tree(tree, ["gfx"], ["ppm", "kst"], ["gfx", "ppm"]) == [
        "Building.md: the tree omits protocols/kst/"]


def test_one_name_in_both_roots_fails(tree, monkeypatch):
    monkeypatch.setattr(cd, "DOCS", tree / "docs")
    out = modules_tree(tree, ["ppm"], ["ppm"], ["ppm"])
    assert out == ["protocols/ppm/ and shared/ppm/ share a name; the builds "
                   "name a module by its folder"]


def test_a_tree_without_protocols_is_read(tree, monkeypatch):
    monkeypatch.setattr(cd, "DOCS", tree / "docs")
    assert modules_tree(tree, ["gfx"], [], []) == [
        "Building.md: the tree omits shared/gfx/"]
