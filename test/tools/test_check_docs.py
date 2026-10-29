"""check_docs.py: the numbers a page states are held to their constants.

SPDX-License-Identifier: MIT
"""

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


def test_a_table_with_no_ceiling_in_ci_fails(tree):
    out = ceilings(tree, DOC, ci="      - name: x\n        run: true\n")
    assert out == ["Performance.md: has the ceiling table, and ci.yml has "
                   "no frame_cost.py call with --max-lines"]


def test_no_table_and_no_ceiling_in_ci_passes(tree):
    assert ceilings(tree, "no table here", ci="run: true\n") == []


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


def suite_tree(tree, status):
    host = tree / "test" / "host"
    host.mkdir(parents=True)
    (host / "CMakeLists.txt").write_text(
        "foreach(suite gfx kst_wire kst_reg)\nendforeach()\n")
    (tree / "STATUS.md").write_text(status, encoding="utf-8")
    problems = []
    cd.check_suites(problems)
    return problems


NAMED = "`test_gfx` `test_kst_wire` `test_kst_reg`\n"


def test_every_count_of_binaries_agrees_with_cmake(tree):
    assert suite_tree(
        tree, "The suite is 3 binaries. Of the 2 `test_kst_*` binaries, 1 "
        "runs.\n" + NAMED) == []


def test_a_second_count_of_binaries_is_checked_too(tree):
    out = suite_tree(
        tree, "The suite is 3 binaries.\nAnother page says 4 binaries.\n"
        + NAMED)
    assert out == ["STATUS.md: says 4 binaries; CMake builds 3"]


def test_a_count_of_one_prefix_is_checked(tree):
    out = suite_tree(
        tree, "The suite is 3 binaries. Of the 5 `test_kst_*` binaries.\n"
        + NAMED)
    assert out == ["STATUS.md: says 5 test_kst_* binaries; CMake builds 2"]


def test_spdx_covers_the_tool_scripts(tree):
    for rel, text in (("shared/a.c", "/* SPDX-License-Identifier: MIT */\n"),
                      ("tools/ok.py", "# SPDX-License-Identifier: MIT\n"),
                      ("tools/bad.py", "x = 1\n"),
                      ("tools/bad.js", "const x = 1;\n"),
                      ("tools/data.json", "{}\n"),
                      ("test/tools/bad.py", "x = 1\n"),
                      ("test/tools/__pycache__/c.py", "x = 1\n")):
        f = tree / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text)
    problems = []
    cd.check_spdx(problems)
    assert sorted(problems) == [
        "test/tools/bad.py has no SPDX-License-Identifier",
        "tools/bad.js has no SPDX-License-Identifier",
        "tools/bad.py has no SPDX-License-Identifier"]


def sdk_tree(tree, **change):
    """ci.yml, release.yml and the build and performance pages, agreeing on
    pico-sdk 2.3.0 and ESP-IDF v6.1, the panel job named as the ruleset
    requires it, unless a name in @p change
    replaces its text."""
    files = {
        ".github/workflows/ci.yml":
            "env:\n  PICO_SDK_TAG: '2.3.0'\n"
            "    name: Panel (ESP-IDF v5.5.5, ESP32-P4)\n"
            "          esp_idf_version: v6.1\n"
            "          esp_idf_version: v6.1\n"
            "    name: IOmcu (pico-sdk 2.3.0)\n",
        ".github/workflows/release.yml":
            "env:\n  PICO_SDK_TAG: '2.3.0'\n  esp_idf_version: v6.1\n",
        "docs/Building.md":
            "| pico-sdk | 2.0 or newer | CI builds 2.3.0 |\n"
            "CI builds v6.1\n"
            "IOmcu build on pico-sdk 2.3.0 with\n",
        "docs/Building-de.md":
            "| pico-sdk | 2.0 oder neuer | CI baut 2.3.0 |\n"
            "CI baut v6.1\n"
            "IOmcu-Build mit pico-sdk 2.3.0 mit\n",
        "docs/Performance.md": "the image CI builds, pico-sdk 2.3.0 with\n",
        "docs/Performance-de.md": "Image, das CI baut, pico-sdk 2.3.0 mit\n",
    }
    for rel, text in files.items():
        for old, new in change.get(rel, ()):
            assert old in text
            text = text.replace(old, new)
        f = tree / rel
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text, encoding="utf-8")
    problems = []
    cd.check_sdk_tags(problems)
    return problems


def test_sdk_tags_that_agree_pass(tree):
    assert sdk_tree(tree) == []


@pytest.mark.parametrize("rel, old, new, said", [
    (".github/workflows/release.yml", "'2.3.0'", "'2.3.1'",
     "release.yml PICO_SDK_TAG says pico-sdk 2.3.1"),
    ("docs/Building.md", "CI builds 2.3.0", "CI builds 2.2.0",
     "Building.md requirements says pico-sdk 2.2.0"),
    ("docs/Building-de.md", "mit pico-sdk 2.3.0", "mit pico-sdk 2.4.0",
     "Building-de.md CI row says pico-sdk 2.4.0"),
    ("docs/Performance.md", "pico-sdk 2.3.0", "pico-sdk 2.1.0",
     "Performance.md says pico-sdk 2.1.0"),
    (".github/workflows/ci.yml", "(pico-sdk 2.3.0)", "(pico-sdk 2.0.0)",
     "ci.yml job name says pico-sdk 2.0.0"),
    (".github/workflows/release.yml", "esp_idf_version: v6.1",
     "esp_idf_version: v5.4", "release.yml builds ESP-IDF v5.4"),
    ("docs/Building-de.md", "CI baut v6.1", "CI baut v5.5.3",
     "Building-de.md says ESP-IDF v5.5.3"),
    ("docs/Building.md", "CI builds v6.1", "CI builds v5.5",
     "Building.md says ESP-IDF v5.5;"),
    (".github/workflows/ci.yml", "Panel (ESP-IDF v5.5.5,",
     "Panel (ESP-IDF v6.1,",
     "the ci.yml panel job is named 'Panel (ESP-IDF v6.1, ESP32-P4)'; the "
     "ruleset requires 'Panel (ESP-IDF v5.5.5, ESP32-P4)'"),
])
def test_a_copy_that_differs_is_named(tree, rel, old, new, said):
    out = sdk_tree(tree, **{rel: [(old, new)]})
    assert len(out) == 1 and said in out[0]


@pytest.mark.parametrize("rel, old, new", [
    ("docs/Building.md", "CI builds v6.1", "CI builds the newest"),
    ("docs/Building-de.md", "CI baut v6.1", "CI baut das neueste"),
])
def test_a_page_without_the_esp_idf_statement_is_named(tree, rel, old, new):
    out = sdk_tree(tree, **{rel: [(old, new)]})
    page = rel.split("/")[-1]
    assert len(out) == 1 and f"ESP-IDF statement in {page}" in out[0]


def test_two_esp_idf_versions_in_ci_are_named(tree):
    """The build without the injector builds the same ESP-IDF as the
    first one."""
    ci = {".github/workflows/ci.yml": [
        ("          esp_idf_version: v6.1\n"
         "          esp_idf_version: v6.1\n",
         "          esp_idf_version: v6.1\n"
         "          esp_idf_version: v5.5.3\n")]}
    out = sdk_tree(tree, **ci)
    assert out == ["SDK tags: ci.yml builds the panel with 2 ESP-IDF "
                   "versions (v5.5.3, v6.1); it names one"]


def test_a_ci_without_a_panel_build_is_named(tree):
    ci = {".github/workflows/ci.yml": [
        ("          esp_idf_version: v6.1\n"
         "          esp_idf_version: v6.1\n", "")]}
    assert sdk_tree(tree, **ci)[0].endswith("(none); it names one")


def test_the_tree_holds_the_sdk_tags():
    problems = []
    cd.check_sdk_tags(problems)
    assert problems == []


def test_only_a_help_image_block_counts_a_view_as_shown(tmp_path, monkeypatch):
    help_root, img = tmp_path / "help", tmp_path / "img"
    for lang in ("en", "de"):
        (help_root / lang).mkdir(parents=True)
    (img / "de").mkdir(parents=True)
    head = "---\nscreen: none\ntitle: T\norder: 1\n---\n\n"
    (help_root / "en" / "a.md").write_text(
        head + "Text with `](view:motor)` and ](view:servo) in prose.\n\n"
        "![The motor](view:motor-table)\n", encoding="utf-8")
    monkeypatch.setattr(cd, "HELP", help_root)
    monkeypatch.setattr(cd, "IMG", img)
    shown = {p.name for p in cd.help_images()}
    assert shown == {"motor-table.png"}


def test_a_source_without_a_licence_line_fails(tree):
    """A source file of the tree without its SPDX line is reported."""
    src = tree / "firmware" / "panel" / "main"
    src.mkdir(parents=True)
    (src / "a.c").write_text("int a;\n", encoding="utf-8")
    problems = []
    cd.check_spdx(problems)
    assert problems == [
        "firmware/panel/main/a.c has no SPDX-License-Identifier"]


def test_registry_components_are_not_held_to_the_licence_line(tree):
    """Files under managed_components/ are the registry's, not the tree's."""
    fetched = (tree / "firmware" / "panel" / "managed_components"
               / "espressif__esp_hosted")
    fetched.mkdir(parents=True)
    (fetched / "protobuf-c.c").write_text("int b;\n", encoding="utf-8")
    problems = []
    cd.check_spdx(problems)
    assert problems == []
