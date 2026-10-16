"""stack_check.py: the panel's decoder, the task table in the docs, and
the build trees it does not read.

SPDX-License-Identifier: MIT
"""

import pytest

import stack_check as sc


def test_the_panel_decoder_reads_an_entry():
    # entry a1, 48
    assert sc.decode(0x42000000, 0x006136, 3) == ("entry", 48)


EN = sc.PERFORMANCE[0][1]
TABLE = EN + """
| --- | --- | ---: | ---: | ---: |
| `main` | `main_task`, which runs the UI | 8,192 | 3,984 | 3,184 |
| `knob` | `knob_task` | 3,072 | 1,360 | 688 |

Measured.
"""


def test_doc_rows_reads_the_numbers_of_a_table():
    assert sc.doc_rows(TABLE, EN) == {"main": [8192, 3984, 3184],
                                      "knob": [3072, 1360, 688]}
    german = TABLE.replace("8,192", "8 192")
    assert sc.doc_rows(german, EN)["main"][0] == 8192
    assert sc.doc_rows("nothing", EN) is None


def test_a_table_that_matches_the_build_passes():
    said = sc.doc_rows(TABLE, EN)
    measured = {"main": (8192, 3984, 3184), "knob": (3072, 1360, 688)}
    assert sc.table_problems("P.md", said, measured) == []


def test_a_depth_that_moved_fails():
    said = sc.doc_rows(TABLE, EN)
    measured = {"main": (8192, 4000, 3168), "knob": (3072, 1360, 688)}
    assert sc.table_problems("P.md", said, measured) == [
        "P.md: the task table gives main as [8192, 3984, 3184]; measured "
        "[8192, 4000, 3168]"]


def test_a_task_the_table_lacks_and_one_it_invents_fail():
    said = sc.doc_rows(TABLE, EN)
    out = sc.table_problems("P.md", said, {"main": (8192, 3984, 3184),
                                           "touch": (4096, 1808, 1264)})
    assert out == ["P.md: the task table has no row for touch",
                   "P.md: the task table has a row for knob, which the "
                   "image does not have"]


def test_both_pages_carry_the_task_table_and_the_count():
    import re
    for page, header, sentence in sc.PERFORMANCE:
        text = page.read_text(encoding="utf-8")
        assert sc.doc_rows(text, header), page.name
        assert re.search(sentence, " ".join(text.split())), page.name


@pytest.mark.parametrize("rel, inside", [
    ("build/esp-idf/x.c", True),
    ("build-inject/esp-idf/mbedtls/library/error.c", True),
    ("main/main.c", False),
    ("components/gt911/touch_inject.c", False),
    ("components/builder/x.c", False),
])
def test_build_trees_are_not_read_as_source(rel, inside):
    assert sc.in_build_dir(sc.Path(rel)) is inside
