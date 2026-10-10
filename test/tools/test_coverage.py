"""coverage.py: every source under shared/ is in the measurement, and the
README figure is rendered from it."""

import pytest

import coverage as cov

SOURCES = ["shared/a/a.c", "shared/a/table.c"]
TRACKED = ["shared/a/a.c"]
DATA = {"shared/a/table.c"}


def problems(sources=SOURCES, tracked=TRACKED, data=DATA,
             compiled=("a.c", "table.c"), counted=("a.c",), with_code=()):
    return cov.completeness(list(sources), list(tracked), set(data),
                            set(compiled), set(counted), set(with_code))


def test_a_complete_measurement_has_no_problem():
    assert problems() == []


def test_a_source_in_neither_list_fails():
    out = problems(sources=SOURCES + ["shared/a/new.c"],
                   compiled=("a.c", "table.c", "new.c"))
    assert len(out) == 1
    assert out[0].startswith("shared/a/new.c is in neither TRACKED nor "
                             "DATA_ONLY")


def test_a_source_the_suite_does_not_compile_fails():
    out = problems(compiled=("table.c",), counted=())
    assert out == ["shared/a/a.c is not compiled into the host suite: the "
                   "build has no .gcno for it"]


def test_a_tracked_source_without_counters_fails():
    out = problems(counted=())
    assert out == ["shared/a/a.c has no counters: no test links code from "
                   "it, and it is measured at 0%"]


def test_a_table_that_holds_a_function_fails():
    out = problems(with_code=("table.c",))
    assert out == ["shared/a/table.c is in DATA_ONLY and holds a function; "
                   "move it to TRACKED"]


def test_a_listed_file_that_is_gone_fails():
    out = problems(tracked=TRACKED + ["shared/a/gone.c"])
    assert out == ["shared/a/gone.c is listed in tools/coverage.py and is "
                   "not under shared/"]


def test_a_file_in_both_lists_fails():
    out = problems(data=DATA | {"shared/a/a.c"})
    assert "shared/a/a.c is in TRACKED and in DATA_ONLY" in out


def test_two_sources_of_one_name_fail():
    out = problems(sources=SOURCES + ["shared/b/a.c"],
                   tracked=TRACKED + ["shared/b/a.c"])
    assert out[0].startswith("two sources under shared/ are named a.c")


def test_the_lists_name_every_source_in_the_tree():
    """TRACKED and DATA_ONLY together are the C files under shared/."""
    listed = set(cov.TRACKED) | cov.DATA_ONLY
    assert listed == set(cov.library_sources())
    assert len(cov.TRACKED) == len(set(cov.TRACKED))


RESULTS = {
    "shared/a/a.c": {"percent": 90.0, "lines": 1000, "covered": 900},
    "shared/a/b.c": {"percent": 100.0, "lines": 2345, "covered": 2345},
}


def test_the_english_figure():
    assert cov.render_readme(RESULTS, "en") == (
        "\nHost-suite line coverage of `shared/`: **97.0%**, 3,245 of 3,345 "
        "lines in 2 files. CI fails below 94% in total or below 85% in any "
        "file; exempt from the per-file floor: `stub_screen.c`. "
        "[STATUS.md](STATUS.md#tests-and-ci) has the table per file.\n")


def test_the_german_figure_uses_a_decimal_comma_and_spaced_thousands():
    text = cov.render_readme(RESULTS, "de")
    assert "**97,0 %**, 3 245 von 3 345 Zeilen in 2 Dateien" in text
    assert "unter 94 % gesamt oder unter 85 % in einer Datei fehl; " \
           "ausgenommen von der Grenze je Datei: `stub_screen.c`." in text


def test_the_table_totals_its_rows(monkeypatch):
    monkeypatch.setattr(cov, "TRACKED", list(RESULTS))
    body, total = cov.render(RESULTS)
    assert "| `shared/a/a.c` | 1000 | 900 | 90.0% |" in body
    assert "| **total** | **3345** | **3245** | **97.0%** |" in body
    assert total == pytest.approx(97.01, abs=0.01)


def test_splice_replaces_what_is_between_the_markers():
    text = "a\n<!-- coverage:start -->old<!-- coverage:end -->\nb\n"
    assert cov.splice(text, "\nnew\n", cov.START, cov.END, "x") == (
        "a\n<!-- coverage:start -->\nnew\n<!-- coverage:end -->\nb\n")


def test_splice_stops_without_the_markers():
    with pytest.raises(SystemExit):
        cov.splice("no markers", "x", cov.START, cov.END, "README.md")


def test_both_readmes_carry_the_markers():
    for path in cov.READMES.values():
        text = path.read_text(encoding="utf-8")
        assert cov.START in text and cov.END in text
