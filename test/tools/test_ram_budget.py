"""ram_budget.py: the queue reader, the bounce buffers, the floor and the
table in the docs.

SPDX-License-Identifier: MIT
"""

import sys

import pytest

import ram_budget as rb


@pytest.fixture
def panel(tmp_path, monkeypatch):
    """An empty panel tree under tmp_path; returns a writer for its files."""
    root = tmp_path
    panel_dir = root / "firmware" / "panel"
    panel_dir.mkdir(parents=True)
    monkeypatch.setattr(rb, "ROOT", root)
    monkeypatch.setattr(rb, "PANEL_DIR", panel_dir)

    def write(rel, text):
        path = panel_dir / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path
    return write


def test_the_panels_queues_are_all_read():
    rows, errors = rb.queues()
    assert errors == []
    assert rows, "no queue found in firmware/panel"


def test_a_queue_is_its_header_and_its_items(panel):
    panel("main/a.c", "#define DEPTH 8\n"
                      "void f(void) {\n"
                      "  xQueueCreate(DEPTH, sizeof(touch_item_t)); }\n")
    rows, errors = rb.queues()
    assert errors == []
    assert [r[1] for r in rows] == [rb.QUEUE + 8 * rb.SIZEOF["touch_item_t"]]
    assert "firmware/panel/main/a.c:3" in rows[0][0]


def test_a_numeric_item_size_and_a_configured_length(panel):
    panel("main/a.c", "void f(void) {\n"
                      "  xQueueCreate(s_touch.cfg.event_queue_len, 4u);\n"
                      "}\n")
    rows, errors = rb.queues()
    assert errors == []
    assert rows[0][1] == rb.QUEUE + 32 * 4


def test_a_mutex_and_a_binary_semaphore_are_a_queue_header(panel):
    panel("main/a.c", "void f(void) { xSemaphoreCreateMutex();\n"
                      "  xSemaphoreCreateBinary(); }\n")
    rows, errors = rb.queues()
    assert errors == []
    assert [r[1] for r in rows] == [rb.QUEUE, rb.QUEUE]


def test_a_commented_out_queue_is_not_counted(panel):
    panel("main/a.c", "/* xQueueCreate(4, sizeof(nothing_t)); */\n"
                      "// xSemaphoreCreateCounting(2, 0);\n")
    assert rb.queues() == ([], [])


def test_the_build_directory_is_not_read(panel):
    panel("build/x.c", "void f(void) { xQueueCreate(n, sizeof(x_t)); }\n")
    assert rb.queues() == ([], [])


@pytest.mark.parametrize("call, says", [
    ("xQueueCreate(4, sizeof(unknown_t))",
     "sizeof(unknown_t) is not in SIZEOF"),
    ("xQueueCreate(n, sizeof(touch_item_t))",
     "queue length 'n' is not a number"),
    ("xQueueCreate(4, item_size)", "item size 'item_size' is not read"),
    ("xQueueCreate(4)", "xQueueCreate() not parsed"),
    ("xQueueCreateWithCaps(4, 8, MALLOC_CAP_SPIRAM)",
     "xQueueCreateWithCaps() is not one this tool reads"),
    ("xSemaphoreCreateCounting(2, 0)",
     "xSemaphoreCreateCounting() is not one this tool reads"),
])
def test_a_queue_it_cannot_size_fails(panel, call, says):
    panel("main/a.c", f"void f(void) {{ {call}; }}\n")
    rows, errors = rb.queues()
    assert rows == []
    assert len(errors) == 1 and says in errors[0]


def test_bounce_buffers_are_two_of_the_configured_lines(panel):
    panel("components/display/include/display.h",
          "#define DISPLAY_CONFIG_DEFAULT { .bounce_buffer_lines = 10, }\n")
    panel("components/board/include/board_pins.h",
          "#define BOARD_LCD_H_RES   800\n")
    assert rb.bounce_buffers() == 2 * 10 * 800 * 2


def test_bounce_buffers_without_the_lines_stop(panel):
    panel("components/display/include/display.h", "\n")
    panel("components/board/include/board_pins.h",
          "#define BOARD_LCD_H_RES 800\n")
    with pytest.raises(SystemExit):
        rb.bounce_buffers()


EN = rb.PERFORMANCE[0][1]
DE = rb.PERFORMANCE[1][1]


def test_doc_rows_reads_both_number_forms():
    en = f"{EN}\n| --- | --- | ---: |\n| `heap` | the heap | 299,320 |\n" \
         "| `left` | what is left | -8,192 |\n\nafter"
    de = f"{DE}\n| --- | --- | ---: |\n| `heap` | der Heap | 299 320 |\n"
    assert rb.doc_rows(en, EN) == {"heap": 299320, "left": -8192}
    assert rb.doc_rows(de, DE) == {"heap": 299320}
    assert rb.doc_rows("no table", EN) is None


def test_check_doc_names_every_difference(tmp_path, monkeypatch):
    en = tmp_path / "Performance.md"
    de = tmp_path / "Performance-de.md"
    en.write_text(f"{EN}\n| --- | --- | ---: |\n| `heap` | x | 1 |\n"
                  "| `extra` | x | 3 |\n", encoding="utf-8")
    de.write_text("no table\n", encoding="utf-8")
    monkeypatch.setattr(rb, "PERFORMANCE", ((en, EN), (de, DE)))
    out = rb.check_doc({"heap": 2, "left": 5})
    assert any("gives heap as 1; the build has 2" in o for o in out)
    assert any("no row for left" in o for o in out)
    assert any("row for extra" in o for o in out)
    assert any("Performance-de.md: no internal RAM table" in o for o in out)


def test_check_doc_passes_a_table_that_agrees(tmp_path, monkeypatch):
    en = tmp_path / "Performance.md"
    en.write_text(f"{EN}\n| --- | --- | ---: |\n| `heap` | x | 2 |\n",
                  encoding="utf-8")
    monkeypatch.setattr(rb, "PERFORMANCE", ((en, EN),))
    assert rb.check_doc({"heap": 2}) == []


def summary(left):
    return {"static": 1, "heap": 2, "start-up": 3, "before": 4, "AP": 5,
            "JOIN": 6, "SCAN": 7, "FETCH": 8, "left": left}


@pytest.mark.parametrize("left, code", [
    (rb.FLOOR, 0),
    (rb.FLOOR - 1, 1),
])
def test_the_floor(monkeypatch, capsys, left, code):
    monkeypatch.setattr(rb, "budget",
                        lambda build: ([], summary(left), "JOIN", []))
    monkeypatch.setattr(sys, "argv", ["ram_budget.py", "build"])
    assert rb.main() == code
    out = capsys.readouterr().out
    assert ("under the floor" in out) == (code == 1)


def test_an_error_of_the_reader_fails(monkeypatch, capsys):
    monkeypatch.setattr(rb, "budget",
                        lambda build: ([], summary(rb.FLOOR), "AP",
                                       ["a.c:1: not read"]))
    monkeypatch.setattr(sys, "argv", ["ram_budget.py", "build"])
    assert rb.main() == 1
    assert "FAIL a.c:1: not read" in capsys.readouterr().out


def test_a_missing_build_stops(tmp_path):
    with pytest.raises(SystemExit):
        rb.budget(tmp_path)
