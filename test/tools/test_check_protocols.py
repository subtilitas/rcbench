"""check_protocols.py: each rule has a module that breaks it.

SPDX-License-Identifier: MIT
"""

import shutil

import pytest

import check_protocols as cp

GUARD_OPEN = '#ifdef __cplusplus\nextern "C" {\n#endif\n'
GUARD_CLOSE = "#ifdef __cplusplus\n}\n#endif\n"

needs_compilers = pytest.mark.skipif(
    not ((shutil.which("cc") or shutil.which("gcc"))
         and (shutil.which("c++") or shutil.which("g++"))),
    reason="no C and C++ compiler on PATH")


def header(body, extra_includes=""):
    return ("#pragma once\n#include <stdint.h>\n" + extra_includes
            + GUARD_OPEN + body + GUARD_CLOSE)


def module(tmp_path, name="wire", driver=True):
    """A module that holds every rule, under tmp_path/protocols/."""
    m = tmp_path / "protocols" / name
    (m / "include").mkdir(parents=True)
    (m / "README.md").write_text("# %s\n" % name)
    (m / "CMakeLists.txt").write_text(
        "add_library(rcbench_%s STATIC %s.c)\n" % (name, name))
    (m / "include" / (name + ".h")).write_text(
        header("uint8_t %s_sum(const uint8_t *p, uint8_t n);\n" % name))
    (m / (name + ".c")).write_text(
        '#include <stddef.h>\n#include "%s.h"\n'
        "uint8_t %s_sum(const uint8_t *p, uint8_t n)\n"
        "{ uint8_t s = 0; while (n--) { s += *p++; } return s; }\n"
        % (name, name))
    if driver:
        d = m / cp.DRIVER
        d.mkdir()
        (d / "CMakeLists.txt").write_text(
            "add_library(rcbench_%s_rp2350 INTERFACE)\n"
            "target_link_libraries(rcbench_%s_rp2350 INTERFACE rcbench_%s "
            "hardware_pio)\n" % (name, name, name))
        (d / (name + ".pio")).write_text(".program %s_tx\n    nop\n" % name)
        (d / (name + "_pin.h")).write_text(
            header("void %s_pin_open(uint8_t pin);\n" % name,
                   '#include "%s.h"\n' % name))
        (d / (name + "_pin.c")).write_text(
            '#include "hardware/pio.h"\n#include "pico/stdlib.h"\n'
            '#include "pico.h"\n'
            '#include "%s_pin.h"\n#include "%s.pio.h"\n'
            "void %s_pin_open(uint8_t pin) { (void)pin; }\n"
            % (name, name, name))
    return m


def static(m):
    """The rules that need no compiler."""
    return (cp.layout_problems(m) + cp.include_problems(m)
            + cp.cmake_problems(m))


def test_a_module_with_every_part_passes_the_static_rules(tmp_path):
    assert static(module(tmp_path)) == []


def test_a_module_without_a_driver_passes(tmp_path):
    assert static(module(tmp_path, driver=False)) == []


@needs_compilers
def test_a_module_with_every_part_passes_the_whole_check(tmp_path):
    module(tmp_path)
    module(tmp_path, "other", driver=False)
    assert cp.check(tmp_path / "protocols") == []


@needs_compilers
def test_the_tree_holds_the_rule():
    assert cp.modules() != []
    assert cp.check() == []


def test_includes_reads_both_forms_and_skips_comments():
    text = ('#include <stdint.h>\n  #  include "a.h"\n'
            '/* #include "gone.h" */\n// #include <unistd.h>\n'
            '/*\n#include "block.h"\n*/\n#include "b.h"\n')
    assert cp.includes(text) == [("<", "stdint.h"), ('"', "a.h"),
                                 ('"', "b.h")]


def test_a_header_of_the_project_is_flagged(tmp_path):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text('#include "link_crc.h"\n' + c.read_text())
    assert cp.include_problems(m) == [
        "wire/wire.c includes link_crc.h, which is neither a C standard "
        "header nor a header of wire/"]


def test_a_header_of_another_module_is_flagged(tmp_path):
    module(tmp_path, "other", driver=False)
    m = module(tmp_path)
    h = m / "include" / "wire.h"
    h.write_text('#include "other.h"\n' + h.read_text())
    assert cp.include_problems(m) == [
        "wire/include/wire.h includes other.h, which is neither a C "
        "standard header nor a header of wire/"]


def test_a_system_header_outside_the_standard_is_flagged(tmp_path):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text("#include <unistd.h>\n" + c.read_text())
    assert len(cp.include_problems(m)) == 1
    assert "unistd.h" in cp.include_problems(m)[0]


def test_an_own_header_in_angle_brackets_is_flagged(tmp_path):
    """<wire.h> is resolved by the include path, not by the module."""
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text(c.read_text().replace('"wire.h"', "<wire.h>"))
    assert len(cp.include_problems(m)) == 1


def test_an_sdk_header_in_the_core_is_flagged(tmp_path):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text('#include "hardware/pio.h"\n' + c.read_text())
    assert cp.include_problems(m) == [
        "wire/wire.c includes hardware/pio.h: a pico-sdk header belongs in "
        "rp2350/"]


def test_the_core_does_not_include_its_driver(tmp_path):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text('#include "wire_pin.h"\n' + c.read_text())
    assert cp.include_problems(m) == [
        "wire/wire.c includes wire_pin.h: a core file does not depend on "
        "the pin driver in rp2350/"]


def test_an_sdk_header_in_a_drivers_header_is_flagged(tmp_path):
    m = module(tmp_path)
    h = m / cp.DRIVER / "wire_pin.h"
    h.write_text('#include "hardware/pio.h"\n' + h.read_text())
    assert cp.include_problems(m) == [
        "wire/rp2350/wire_pin.h includes hardware/pio.h: a driver's header "
        "is read without the pico-sdk"]


def test_the_generated_header_of_a_program_that_is_not_there(tmp_path):
    m = module(tmp_path)
    c = m / cp.DRIVER / "wire_pin.c"
    c.write_text('#include "elsewhere.pio.h"\n' + c.read_text())
    assert len(cp.include_problems(m)) == 1
    assert "elsewhere.pio.h" in cp.include_problems(m)[0]


def test_a_driver_takes_no_header_outside_the_sdk_folders(tmp_path):
    m = module(tmp_path)
    c = m / cp.DRIVER / "wire_pin.c"
    c.write_text('#include "tusb.h"\n#include "outputs.h"\n' + c.read_text())
    assert len(cp.include_problems(m)) == 2


def test_an_include_in_the_pio_programs_c_block_is_read(tmp_path):
    m = module(tmp_path)
    pio = m / cp.DRIVER / "wire.pio"
    pio.write_text(pio.read_text() + '% c-sdk {\n#include "hardware/clocks.h"'
                   '\n#include "iomcu_pins.h"\n%}\n')
    assert cp.include_problems(m) == [
        "wire/rp2350/wire.pio includes iomcu_pins.h, which is neither a C "
        "standard header nor a header of wire/"]


@pytest.mark.parametrize("part", ["README.md", "CMakeLists.txt"])
def test_a_missing_part_is_flagged(tmp_path, part):
    m = module(tmp_path)
    (m / part).unlink()
    assert cp.layout_problems(m) == ["protocols/wire/ has no " + part]


def test_a_module_without_a_header_or_a_core_is_flagged(tmp_path):
    m = module(tmp_path)
    (m / "include" / "wire.h").unlink()
    (m / "wire.c").unlink()
    assert cp.layout_problems(m) == [
        "protocols/wire/ has no header under include/",
        "protocols/wire/ has no C file outside rp2350/"]


def test_a_driver_folder_without_its_parts_is_flagged(tmp_path):
    m = module(tmp_path)
    (m / cp.DRIVER / "wire.pio").unlink()
    (m / cp.DRIVER / "wire_pin.h").unlink()
    assert cp.layout_problems(m) == [
        "protocols/wire/rp2350/ has no PIO program",
        "protocols/wire/rp2350/ has no header"]


def test_a_requirement_on_another_component_is_flagged(tmp_path):
    m = module(tmp_path)
    (m / "CMakeLists.txt").write_text(
        "idf_component_register(SRCS wire.c INCLUDE_DIRS include\n"
        "                       REQUIRES link)\n")
    assert cp.cmake_problems(m) == [
        "wire/CMakeLists.txt carries REQUIRES: a module needs no other "
        "component"]


def test_a_library_of_another_module_is_flagged(tmp_path):
    m = module(tmp_path)
    cm = m / cp.DRIVER / "CMakeLists.txt"
    cm.write_text(cm.read_text() + "target_link_libraries("
                  "rcbench_wire_rp2350 INTERFACE rcbench_link)\n")
    assert cp.cmake_problems(m) == [
        "wire/rp2350/CMakeLists.txt names rcbench_link, a library of "
        "another module"]


def test_a_module_whose_name_begins_with_this_ones_is_another(tmp_path):
    """rcbench_wire2 is not a library of wire/; rcbench_wire_rp2350 is."""
    m = module(tmp_path)
    cm = m / "CMakeLists.txt"
    cm.write_text(cm.read_text()
                  + "target_link_libraries(rcbench_wire PUBLIC "
                    "rcbench_wire2)\n")
    assert len(cp.cmake_problems(m)) == 1


def test_a_cmake_comment_is_not_read(tmp_path):
    m = module(tmp_path)
    cm = m / "CMakeLists.txt"
    cm.write_text("# No REQUIRES, and not rcbench_link.\n" + cm.read_text())
    assert cp.cmake_problems(m) == []


@needs_compilers
def test_a_header_without_the_guard_is_flagged(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(
        "#pragma once\n#include <stdint.h>\n"
        "uint8_t wire_sum(const uint8_t *p, uint8_t n);\n")
    assert cp.header_problems(m, *cp.compilers()) == [
        'wire/include/wire.h has no extern "C" guard']


@needs_compilers
def test_a_header_that_is_not_cxx_is_flagged(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(
        header("void wire_make(int new);\n"))
    found = cp.header_problems(m, *cp.compilers())
    assert len(found) == 1
    assert found[0].startswith("wire/include/wire.h does not compile as "
                               "C++17: ")


@needs_compilers
def test_a_header_that_needs_another_included_first_is_flagged(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(
        "#pragma once\n" + GUARD_OPEN
        + "uint8_t wire_sum(const uint8_t *p, uint8_t n);\n" + GUARD_CLOSE)
    found = cp.header_problems(m, *cp.compilers())
    assert [f.split(": ")[0] for f in found] == [
        "wire/include/wire.h does not compile as C11",
        "wire/include/wire.h does not compile as C++17"]


@needs_compilers
def test_a_header_without_an_include_guard_is_flagged(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(
        "#include <stdint.h>\n" + GUARD_OPEN
        + "typedef struct { uint8_t a; } wire_t;\n" + GUARD_CLOSE)
    found = cp.header_problems(m, *cp.compilers())
    assert len(found) == 2 and "C11" in found[0]


@needs_compilers
def test_a_drivers_header_is_compiled_with_the_core_on_its_path(tmp_path):
    m = module(tmp_path)
    assert cp.header_problems(m, *cp.compilers()) == []
    (m / cp.DRIVER / "wire_pin.h").write_text(
        header("void wire_pin_open(uint8_t pin, bool invert);\n"))
    found = cp.header_problems(m, *cp.compilers())
    assert found and found[0].startswith(
        "wire/rp2350/wire_pin.h does not compile as C11")


def test_main_refuses_an_argument_it_does_not_know():
    with pytest.raises(SystemExit):
        cp.main(["--fix"])


@needs_compilers
def test_main_fails_on_a_broken_module_and_names_it(tmp_path, monkeypatch,
                                                    capsys):
    m = module(tmp_path)
    (m / "README.md").unlink()
    monkeypatch.setattr(cp, "PROTOCOLS", tmp_path / "protocols")
    assert cp.main(["--check"]) == 1
    assert "protocols/wire/ has no README.md" in capsys.readouterr().out


def test_main_fails_when_there_is_no_module(tmp_path, monkeypatch):
    monkeypatch.setattr(cp, "PROTOCOLS", tmp_path / "protocols")
    with pytest.raises(SystemExit):
        cp.main(["--check"])
