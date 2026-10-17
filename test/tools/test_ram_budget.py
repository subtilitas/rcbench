"""ram_budget.py: the queue reader, the bounce buffers, the floor and the
table in the docs.

SPDX-License-Identifier: MIT
"""

import re
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


def test_a_length_from_a_header_beside_the_file(panel):
    panel("main/panel.h", "#define DEPTH 8\n#define ITEM 16u\n")
    panel("other/far.h", "#define FAR 2\n")
    panel("main/a.c", '#include "panel.h"\n#include "far.h"\n'
                      "#define ITEM 4\n"
                      "void f(void) { xQueueCreate(DEPTH, ITEM);\n"
                      "  xQueueCreate(FAR, ITEM); }\n")
    rows, errors = rb.queues()
    assert [r[1] for r in rows] == [rb.QUEUE + 8 * 4]
    assert len(errors) == 1 and "queue length 'FAR'" in errors[0]


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


@pytest.mark.parametrize("tree", ["build", "build-inject"])
def test_the_build_directory_is_not_read(panel, tree):
    panel(f"{tree}/x.c", "void f(void) { xQueueCreate(n, sizeof(x_t)); }\n")
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


# ------------------------------------------------------ the PSRAM table --

# An ESP-IDF map's .ext_ram.bss as the linker writes it: an input section's
# name alone on its line when it is long, fill between objects, COMMON, and
# the next output section after it.  The objects' paths are cut short
# here; the map gives each from the build directory down.
KST_O = "esp-idf/bench/libbench.a(kst_card.c.obj)"
CSV_O = "esp-idf/logfile/liblogfile.a(log_csv.c.obj)"
WPA_O = "esp-idf/wpa_supplicant/libwpa_supplicant.a(wpa.c.obj)"
MAP = f"""\
.ext_ram.dummy  0x3c000020   0x1bffe0
 *fill*         0x3c000020   0x1bfd60

.ext_ram.bss    0x3c1c0000    0x2b27c
                0x3c1c0000          _ext_ram_bss_start = ABSOLUTE (.)
 *(.ext_ram.bss .ext_ram.bss.*)
 *libbench.a:kst_card.*(.bss .bss.*)
 .bss.s_list    0x3c1c0000      0x940 {KST_O}
 .bss.s_text    0x3c1c0ad8      0xd68 {KST_O}
 .ext_ram.bss.s_attr
                0x3c1c1840       0x20 {KST_O}
 *liblogfile.a:log_csv.*(.bss .bss.*)
 .bss.sniff_buf$0
                0x3c1c28c4      0x800 {CSV_O}
 *fill*         0x3c1c3bed        0x3
 COMMON         0x3c1c30c4       0x10 {CSV_O}
 *liblwip.a:(.bss .bss.*)
 .bss.sockets   0x3c1c3bf8       0xc8 esp-idf/lwip/liblwip.a(sockets.c.obj)
                0x3c1c3bf8                sockets
 .bss.gWpaSm    0x3c1eaf44      0x338 {WPA_O}
                0x3c1eb27c          _ext_ram_bss_end = ABSOLUTE (.)

.iram0.text_end
                0x4038ccb3       0x4d
 .bss.later     0x3c1eb27c      0x100 {KST_O}
"""

LF = """\
# A comment that names nothing (extram_bss)
[mapping:a]
archive: libbench.a
entries:
    kst_card (extram_bss)

[mapping:b]
archive: liblogfile.a
entries:
    log_csv (extram_bss)
"""

PSRAM_EN = rb.PSRAM_PAGES[0][1]
PSRAM_DE = rb.PSRAM_PAGES[1][1]


def en_page(rows, count, total, idf):
    table = "".join(f"| `{o}` | {c} | {n:,} |\n" for o, c, n in rows)
    return (f"What lives in PSRAM instead: the zero-initialised data (.bss) "
            f"of {count} objects,\n{total:,} bytes, named in "
            f"`firmware/panel/main/psram_bss.lf`, and, through\n"
            f"`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, lwIP, net80211, "
            f"pp and wpa_supplicant,\n{idf:,} bytes.\n\n{PSRAM_EN}\n"
            f"| --- | --- | ---: |\n{table}\nAfter.\n")


def de_page(rows, count, total, idf):
    def de(n):
        return f"{n:,}".replace(",", " ")
    table = "".join(f"| `{o}` | {c} | {de(n)} |\n" for o, c, n in rows)
    return (f"Was stattdessen im PSRAM liegt: die nullinitialisierten Daten "
            f"(.bss) von {count}\nObjekten, {de(total)} Bytes, benannt in "
            f"`firmware/panel/main/psram_bss.lf`, und über die Auswahl von "
            f"ESP-IDF pp und\nwpa_supplicant, {de(idf)} Bytes.\n\n"
            f"{PSRAM_DE}\n| --- | --- | ---: |\n{table}\n")


KST = 0x940 + 0xd68 + 0x20
CSV = 0x800 + 0x10
IDF = 0xc8 + 0x338
ROWS = [("kst_card", "bench", KST), ("log_csv", "logfile", CSV)]


@pytest.fixture
def psram_tree(tmp_path, monkeypatch):
    """A build with MAP and a fragment with LF; pages that agree with them.
    Returns (build, en, de)."""
    build = tmp_path / "build"
    build.mkdir()
    (build / rb.MAP_NAME).write_text(MAP, encoding="utf-8")
    lf = tmp_path / "psram_bss.lf"
    lf.write_text(LF, encoding="utf-8")
    en = tmp_path / "Performance.md"
    de = tmp_path / "Performance-de.md"
    en.write_text(en_page(ROWS, 2, KST + CSV, IDF), encoding="utf-8")
    de.write_text(de_page(ROWS, 2, KST + CSV, IDF), encoding="utf-8")
    monkeypatch.setattr(rb, "PSRAM_LF", lf)
    monkeypatch.setattr(rb, "PSRAM_PAGES",
                        ((en, PSRAM_EN, rb.PSRAM_PAGES[0][2]),
                         (de, PSRAM_DE, rb.PSRAM_PAGES[1][2])))
    return build, en, de


def test_the_map_is_read_object_by_object(tmp_path):
    path = tmp_path / "a.map"
    path.write_text(MAP, encoding="utf-8")
    # The section after .ext_ram.bss is not read, a long name on its own
    # line is joined to the next, COMMON and an EXT_RAM_BSS_ATTR section
    # count, fill does not.
    assert rb.map_psram(path) == {
        ("libbench.a", "kst_card"): KST,
        ("liblogfile.a", "log_csv"): CSV,
        ("liblwip.a", "sockets"): 0xc8,
        ("libwpa_supplicant.a", "wpa"): 0x338,
    }


def test_a_map_without_the_section_stops(tmp_path):
    path = tmp_path / "a.map"
    path.write_text(".dram0.bss  0x3fca1ea0  0x51b8\n", encoding="utf-8")
    with pytest.raises(SystemExit):
        rb.map_psram(path)


def test_the_fragment_is_read_entry_by_entry(tmp_path):
    lf = tmp_path / "a.lf"
    lf.write_text(LF, encoding="utf-8")
    assert rb.lf_objects(lf) == [("libbench.a", "kst_card"),
                                 ("liblogfile.a", "log_csv")]


def test_the_repositorys_fragment_names_objects():
    named = rb.lf_objects(rb.PSRAM_LF)
    assert len(named) >= 10
    assert len(set(named)) == len(named)


def test_pages_that_agree_pass(psram_tree):
    build, _, _ = psram_tree
    objects, idf, errors = rb.psram(build)
    assert errors == []
    assert objects == ROWS and idf == IDF
    assert rb.check_psram_doc(objects, idf) == []


def test_a_drifted_row_fails_in_both_languages(psram_tree):
    build, en, de = psram_tree
    rows = [("kst_card", "bench", KST - 72), ROWS[1]]
    en.write_text(en_page(rows, 2, KST + CSV, IDF), encoding="utf-8")
    de.write_text(de_page(rows, 2, KST + CSV, IDF), encoding="utf-8")
    objects, idf, _ = rb.psram(build)
    assert rb.check_psram_doc(objects, idf) == [
        f"{page}: the PSRAM table gives kst_card as {KST - 72} bytes; the "
        f"map places {KST}" for page in ("Performance.md",
                                         "Performance-de.md")]


def test_a_drifted_total_count_or_esp_idf_figure_fails(psram_tree):
    build, en, de = psram_tree
    en.write_text(en_page(ROWS, 3, KST + CSV - 72, IDF + 1),
                  encoding="utf-8")
    objects, idf, _ = rb.psram(build)
    assert rb.check_psram_doc(objects, idf) == [
        "Performance.md: gives the count of objects in PSRAM as 3; the map "
        "has 2",
        f"Performance.md: gives the objects' total in PSRAM as "
        f"{KST + CSV - 72:,}; the map has {KST + CSV}",
        f"Performance.md: gives ESP-IDF's own .bss in PSRAM as "
        f"{IDF + 1:,}; the map has {IDF}"]


def test_an_object_missing_from_the_table_fails(psram_tree):
    build, en, _ = psram_tree
    en.write_text(en_page(ROWS[:1], 2, KST + CSV, IDF), encoding="utf-8")
    objects, idf, _ = rb.psram(build)
    assert rb.check_psram_doc(objects, idf) == [
        "Performance.md: the PSRAM table has no row for log_csv"]


def test_a_row_the_fragment_does_not_name_fails(psram_tree):
    build, en, _ = psram_tree
    rows = ROWS + [("gone_screen", "ui", 0)]
    en.write_text(en_page(rows, 2, KST + CSV, IDF), encoding="utf-8")
    objects, idf, _ = rb.psram(build)
    assert rb.check_psram_doc(objects, idf) == [
        "Performance.md: the PSRAM table has a row for gone_screen, which "
        "psram_bss.lf does not name"]


def test_a_wrong_component_fails(psram_tree):
    build, en, _ = psram_tree
    rows = [("kst_card", "ui", KST), ROWS[1]]
    en.write_text(en_page(rows, 2, KST + CSV, IDF), encoding="utf-8")
    objects, idf, _ = rb.psram(build)
    assert rb.check_psram_doc(objects, idf) == [
        "Performance.md: the PSRAM table gives kst_card's component as ui; "
        "the fragment's archive is libbench.a"]


def test_a_page_without_the_table_or_the_sentence_fails(psram_tree):
    build, en, _ = psram_tree
    en.write_text("nothing here\n", encoding="utf-8")
    objects, idf, _ = rb.psram(build)
    assert rb.check_psram_doc(objects, idf) == [
        "Performance.md: no PSRAM table to hold",
        "Performance.md: the sentence with the PSRAM count and total is "
        "there 0 times, not once"]


def test_an_object_the_map_does_not_place_fails(psram_tree):
    build, _, _ = psram_tree
    rb.PSRAM_LF.write_text(LF + "    log_gone (extram_bss)\n",
                           encoding="utf-8")
    _, _, errors = rb.psram(build)
    assert errors == ["psram_bss.lf names log_gone in liblogfile.a, and the "
                      "map places none of its .bss in PSRAM"]


def test_an_archive_nobody_names_fails(psram_tree):
    build, _, _ = psram_tree
    (build / rb.MAP_NAME).write_text(
        MAP.replace("esp-idf/lwip/liblwip.a(sockets.c.obj)",
                    "esp-idf/mbedtls/libmbedcrypto.a(aes.c.obj)"),
        encoding="utf-8")
    _, idf, errors = rb.psram(build)
    assert idf == 0x338
    assert errors == ["the map places .bss of libmbedcrypto.a in PSRAM, "
                      "which neither psram_bss.lf nor ESP_IDF_PSRAM names"]


def test_check_doc_reads_the_map(psram_tree, monkeypatch, capsys):
    build, en, _ = psram_tree
    en.write_text(en_page([("kst_card", "bench", 1), ROWS[1]], 2,
                          KST + CSV, IDF), encoding="utf-8")
    monkeypatch.setattr(rb, "budget",
                        lambda build: ([], summary(rb.FLOOR), "AP", []))
    monkeypatch.setattr(rb, "check_doc", lambda summary: [])
    monkeypatch.setattr(sys, "argv", ["ram_budget.py", str(build),
                                      "--check-doc"])
    assert rb.main() == 1
    out = capsys.readouterr().out
    assert "PSRAM .bss, 2 objects" in out
    assert "FAIL Performance.md: the PSRAM table gives kst_card as 1" in out


def test_check_doc_without_a_map_stops(tmp_path, monkeypatch):
    monkeypatch.setattr(rb, "budget",
                        lambda build: ([], summary(rb.FLOOR), "AP", []))
    monkeypatch.setattr(rb, "check_doc", lambda summary: [])
    monkeypatch.setattr(sys, "argv", ["ram_budget.py", str(tmp_path),
                                      "--check-doc"])
    with pytest.raises(SystemExit):
        rb.main()


def test_both_pages_carry_the_psram_table_and_its_sentence():
    for page, header, sentence in rb.PSRAM_PAGES:
        text = page.read_text(encoding="utf-8")
        assert rb.psram_rows(text, header), page.name
        assert len(re.findall(sentence, " ".join(text.split()))) == 1, \
            page.name


def test_both_pages_list_the_fragments_objects():
    named = {o for _, o in rb.lf_objects(rb.PSRAM_LF)}
    for page, header, _ in rb.PSRAM_PAGES:
        said = rb.psram_rows(page.read_text(encoding="utf-8"), header)
        assert set(said) == named, page.name
