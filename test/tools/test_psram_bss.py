"""psram_bss.py: the objects a linker fragment maps to PSRAM and where the
linker map places their .bss.

SPDX-License-Identifier: MIT
"""

import psram_bss as pb

FRAGMENT = """\
# A comment that names lone (rcbench_extram_bss)
[scheme:rcbench_extram_bss]
entries:
    bss -> extern_ram

[mapping:ui]
archive: libui.a
entries:
    servo_screen (rcbench_extram_bss)
    picker_screen (noflash)

[mapping:bench]
archive: libbench.a
entries:
    kst_card (rcbench_extram_bss)  # trailing comment
"""

UI = "esp-idf/ui/libui.a({}.c.obj)"
BENCH = "esp-idf/bench/libbench.a(kst_card.c.obj)"

MAP = f"""\
Discarded input sections

 .bss           0x00000000        0x0 {UI.format("servo_screen")}
 .bss.gone      0x00000000      0x100 {UI.format("servo_screen")}

Linker script and memory map

.dram0.bss      0x4ff37d40     0x72e0
 *(.bss .bss.*)
 .bss.k         0x4ff37d40       0x20 {UI.format("picker_screen")}
 .sbss.small    0x4ff37d60        0x4 {UI.format("servo_screen")}
                0x4ff37d64                _bss_end
.ext_ram.bss    0x48000000    0x28a48
 .bss.s         0x48000000     0x7348 {UI.format("servo_screen")}
 .bss.a_name_too_long_for_one_line
                0x48007348       0x40 {UI.format("servo_screen")}
 COMMON         0x48007388       0x10 {BENCH}
 *fill*         0x48007398        0x8
 .bss.loose     0x480073a0       0x10 main.c.obj
"""


def test_the_fragment_names_the_objects_its_scheme_maps():
    assert pb.named(FRAGMENT) == [("libui.a", "servo_screen"),
                                  ("libbench.a", "kst_card")]


def test_the_map_sums_bss_and_common_per_section_and_object():
    placed = pb.bss_by_object(MAP)
    assert placed[".ext_ram.bss"] == {("libui.a", "servo_screen"): 0x7388,
                                      ("libbench.a", "kst_card"): 0x10}
    # .sbss is not .bss; the discarded sections are not placed.
    assert placed[".dram0.bss"] == {("libui.a", "picker_screen"): 0x20}


def test_a_build_with_every_named_object_in_psram_passes():
    assert pb.problems(pb.named(FRAGMENT), pb.bss_by_object(MAP)) == []


def test_an_object_left_in_internal_ram_fails():
    placed = pb.bss_by_object(MAP.replace(
        f" COMMON         0x48007388       0x10 {BENCH}\n", "").replace(
        " *(.bss .bss.*)\n", f" .bss.t         0x0 0x40 {BENCH}\n"))
    assert pb.problems(pb.named(FRAGMENT), placed) == [
        "libbench.a(kst_card) has no .bss in .ext_ram.bss: the object is "
        "gone, holds no .bss, or the mapping did not apply",
        "libbench.a(kst_card) keeps 64 bytes of .bss in .dram0.bss"]


def test_an_object_with_bss_on_both_sides_fails():
    placed = pb.bss_by_object(MAP)
    placed[".dram0.bss"][("libui.a", "servo_screen")] = 8
    assert pb.problems(pb.named(FRAGMENT), placed) == [
        "libui.a(servo_screen) keeps 8 bytes of .bss in .dram0.bss"]


def test_a_fragment_on_another_scheme_names_nothing_and_fails():
    other = FRAGMENT.replace("(rcbench_extram_bss)", "(extram_bss)")
    assert pb.named(other) == []
    assert pb.problems([], pb.bss_by_object(MAP)) == [
        "the fragment maps no object with (rcbench_extram_bss)"]


def test_a_map_without_the_memory_map_places_nothing():
    assert pb.bss_by_object("Discarded input sections\n") == {}


def test_main_reads_a_build_directory(tmp_path, capsys, monkeypatch):
    (tmp_path / pb.MAP).write_text(MAP)
    fragment = tmp_path / "psram_bss.lf"
    fragment.write_text(FRAGMENT)
    monkeypatch.setattr(pb, "FRAGMENT", fragment)
    assert pb.main(["psram_bss.py", "--list", "16", str(tmp_path)]) == 0
    out = capsys.readouterr().out
    assert "2 named objects, 29,592 bytes of .bss in .ext_ram.bss" in out
    assert "libui.a(picker_screen)" in out
    assert pb.main(["psram_bss.py", "--list"]) == 1
    assert pb.main(["psram_bss.py", str(tmp_path / "none")]) == 1
