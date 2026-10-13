"""check_protocols.py: each rule has a module that breaks it.

SPDX-License-Identifier: MIT
"""

import os
import re
import shutil
import subprocess
import threading

import pytest

import check_protocols as cp

# A scan that loops forever ends the run with a dump of every thread's stack
# instead of hanging it (conftest.py, no_hang).
pytestmark = pytest.mark.usefixtures("no_hang")

GUARD_OPEN = '#ifdef __cplusplus\nextern "C" {\n#endif\n'
GUARD_CLOSE = "#ifdef __cplusplus\n}\n#endif\n"

HAVE_COMPILERS = bool((shutil.which("cc") or shutil.which("gcc"))
                      and (shutil.which("c++") or shutil.which("g++")))
needs_compilers = pytest.mark.skipif(
    not HAVE_COMPILERS, reason="no C and C++ compiler on PATH")


def test_compilers_exist_when_CI_is_set():
    """The cases marked needs_compilers skip without them; in CI that is a
    failure, not a pass."""
    if os.environ.get("CI") and not HAVE_COMPILERS:
        pytest.fail("CI is set and no C and C++ compiler is on PATH")


def header(body, extra_includes=""):
    return ("#pragma once\n#include <stdint.h>\n" + extra_includes
            + GUARD_OPEN + body + GUARD_CLOSE)


def module(tmp_path, name="wire", driver=True):
    """A module that holds every rule, under tmp_path/protocols/."""
    m = tmp_path / "protocols" / name
    (m / "include").mkdir(parents=True)
    (m / "README.md").write_text(
        "# %s\n\n## Interface\n\nA checksum.\n%s"
        % (name, "" if driver else "\nThe module has no pin driver.\n"))
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
            + cp.readme_problems(m) + cp.cmake_problems(m))


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


def test_a_path_include_that_leaves_the_module_is_flagged(tmp_path):
    """The name is flagged by its path, also when the file it reaches has
    the name of a header of the module."""
    m = module(tmp_path)
    for target in ("../other/include/x.h", "../include/wire.h",
                   "/usr/include/wire.h", "..\\other\\x.h"):
        c = m / "wire.c"
        c.write_text('#include "%s"\n' % target
                     + c.read_text().split("\n", 1)[1])
        found = cp.include_problems(m)
        assert len(found) == 1, target
        assert "a path with a .. component or an absolute path" in found[0]


def test_stdatomic_and_threads_are_standard_headers(tmp_path):
    """Both are C11 headers, and the ARM GNU 14.2 newlib of the RP2350
    build ships both."""
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text("#include <stdatomic.h>\n#include <threads.h>\n"
                 + c.read_text())
    assert cp.include_problems(m) == []


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


# The _Static_assert rule: a limit the module shares with the project is held
# equal by an assertion in the caller.

def caller(tmp_path, text, rel="shared/use/use.c"):
    f = tmp_path / rel
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(text)
    return rel


def asserted_problems(tmp_path, listed):
    return cp.assertion_problems(tmp_path / "protocols", listed)


def test_public_names_are_macros_and_enumerators(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header(
        "#define WIRE_MAX 8\n"
        "enum { WIRE_A = 0, WIRE_B,\n WIRE_C = 5 };\n"))
    assert cp.public_names(m) == {"WIRE_MAX", "WIRE_A", "WIRE_B", "WIRE_C"}


def test_a_listed_assertion_in_the_caller_passes(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header("#define WIRE_MAX 8\n"))
    rel = caller(tmp_path, "#define USE_MAX 8\n"
                 '_Static_assert(WIRE_MAX == USE_MAX,\n "same limit");\n')
    assert asserted_problems(
        tmp_path, {"wire": {rel: ("WIRE_MAX == USE_MAX",)}}) == []


def test_an_assertion_the_list_does_not_name_is_flagged(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header("#define WIRE_MAX 8\n"))
    rel = caller(tmp_path, '_Static_assert(WIRE_MAX == 8, "limit");\n')
    out = asserted_problems(tmp_path, {})
    assert len(out) == 1 and rel in out[0] and "does not list" in out[0]


def test_a_listed_file_without_the_assertion_is_flagged(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header("#define WIRE_MAX 8\n"))
    rel = caller(tmp_path, "int use(void) { return WIRE_MAX; }\n")
    out = asserted_problems(
        tmp_path, {"wire": {rel: ("WIRE_MAX == 8",)}})
    assert len(out) == 1 and "has no static assertion" in out[0]


def two_assertions(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header(
        "#define WIRE_MAX 8\n#define WIRE_MIN 1\n"))
    return caller(tmp_path, '_Static_assert(WIRE_MAX == 8, "max");\n'
                  'static_assert(WIRE_MIN\n == 1, "min");\n')


LISTED_TWO = ("WIRE_MAX == 8", "WIRE_MIN == 1")


def test_an_unchanged_tree_with_two_assertions_in_a_file_passes(tmp_path):
    rel = two_assertions(tmp_path)
    assert asserted_problems(tmp_path, {"wire": {rel: LISTED_TWO}}) == []


def test_removing_one_listed_assertion_of_a_file_fails(tmp_path):
    rel = two_assertions(tmp_path)
    (tmp_path / rel).write_text('_Static_assert(WIRE_MAX == 8, "max");\n')
    out = asserted_problems(tmp_path, {"wire": {rel: LISTED_TWO}})
    assert len(out) == 1 and "WIRE_MIN == 1" in out[0]
    assert "has no static assertion" in out[0]


def test_a_new_assertion_in_a_listed_file_fails(tmp_path):
    rel = two_assertions(tmp_path)
    out = asserted_problems(tmp_path, {"wire": {rel: LISTED_TWO[:1]}})
    assert len(out) == 1 and "does not list" in out[0]


def test_a_changed_condition_fails_and_a_changed_message_does_not(tmp_path):
    rel = two_assertions(tmp_path)
    (tmp_path / rel).write_text(
        '_Static_assert(WIRE_MAX == 8, "other text");\n'
        'static_assert(WIRE_MIN == 2, "min");\n')
    out = asserted_problems(tmp_path, {"wire": {rel: LISTED_TWO}})
    assert len(out) == 2 and all("WIRE_MIN" in o for o in out)


def test_an_assertion_in_a_comment_does_not_count(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header("#define WIRE_MAX 8\n"))
    rel = caller(tmp_path, '/* _Static_assert(WIRE_MAX == 8, "x"); */\n')
    assert len(asserted_problems(
        tmp_path, {"wire": {rel: ("WIRE_MAX == 8",)}})) == 1


def test_an_assertion_on_the_projects_own_names_is_not_the_modules(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header("#define WIRE_MAX 8\n"))
    caller(tmp_path, '_Static_assert(USE_A < USE_B, "order");\n')
    assert asserted_problems(tmp_path, {}) == []


def test_the_tree_lists_every_assertion_on_a_module_constant():
    assert cp.assertion_problems(cp.PROTOCOLS, cp.ASSERTED) == []


def test_a_readme_without_an_interface_heading_is_flagged(tmp_path):
    m = module(tmp_path)
    (m / "README.md").write_text("# wire\n\n## Use\n")
    assert cp.readme_problems(m) == [
        'wire/README.md has no heading with "Interface"']


def test_a_comment_in_a_fenced_block_is_no_heading(tmp_path):
    m = module(tmp_path)
    (m / "README.md").write_text(
        "# wire\n\n```cmake\n# Interface library\n```\n\n"
        "~~~~sh\n# Interface\n~~~\n## Use\n~~~~\n")
    assert cp.readme_problems(m) == [
        'wire/README.md has no heading with "Interface"']
    (m / "README.md").write_text(
        "# wire\n\n```cmake\n# Interface library\n```\n\n"
        "   ## Interface\n")
    assert cp.readme_problems(m) == []


def test_a_driver_interface_heading_counts(tmp_path):
    m = module(tmp_path)
    (m / "README.md").write_text("# wire\n\n## Driver interface\n")
    assert cp.readme_problems(m) == []


def test_a_readme_naming_a_brand_needs_a_trademarks_heading(tmp_path):
    m = module(tmp_path)
    readme = m / "README.md"
    readme.write_text(readme.read_text() + "Works with Futaba, S.BUS.\n")
    assert cp.readme_problems(m) == [
        'wire/README.md names Futaba, S.BUS and has no heading with '
        '"Trademark"']
    readme.write_text(readme.read_text() + "\n## Trademarks\n\nNotice.\n")
    assert cp.readme_problems(m) == []


def test_a_closing_hash_sequence_is_no_part_of_the_heading(tmp_path):
    m = module(tmp_path)
    readme = m / "README.md"
    readme.write_text("# wire #\n\n## Interface ##\n\nWorks with Futaba.\n"
                      "\n## Trademarks ##\n\nNotice.\n")
    assert cp.readme_problems(m) == []
    assert cp.HEADING_RE.findall("## Trademarks ##\n") == ["Trademarks"]


def test_a_brand_inside_a_longer_word_is_not_a_brand(tmp_path):
    m = module(tmp_path)
    readme = m / "README.md"
    readme.write_text(readme.read_text() + "The KSTART byte, DShotty.\n")
    assert cp.readme_problems(m) == []


def test_a_module_without_a_driver_says_so_in_its_readme(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "README.md").write_text("# wire\n\n## Interface\n")
    assert cp.readme_problems(m) == [
        'wire/README.md does not say "no pin driver": the module has no '
        "rp2350/ folder"]


def test_no_pin_driver_inside_a_fenced_block_does_not_count(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "README.md").write_text(
        "# wire\n\n## Interface\n\n```text\nno pin driver\n```\n")
    assert cp.readme_problems(m) == [
        'wire/README.md does not say "no pin driver": the module has no '
        "rp2350/ folder"]


def test_a_module_with_a_driver_needs_no_such_sentence(tmp_path):
    m = module(tmp_path)
    assert cp.readme_problems(m) == []


def test_a_missing_readme_is_the_layout_rules_finding(tmp_path):
    m = module(tmp_path)
    (m / "README.md").unlink()
    assert cp.readme_problems(m) == []
    assert cp.layout_problems(m) == ["protocols/wire/ has no README.md"]


@pytest.mark.parametrize("text", [
    "DShot300 and S.BUS2.\n", "works with FUTABA.\n", "the futaba radio\n"])
def test_a_brand_with_a_suffix_or_in_other_case_is_a_brand(tmp_path, text):
    m = module(tmp_path)
    readme = m / "README.md"
    readme.write_text(readme.read_text() + text)
    assert len(cp.readme_problems(m)) == 1
    assert "has no heading" in cp.readme_problems(m)[0]


def test_an_html_comment_is_no_heading_and_no_sentence(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "README.md").write_text(
        "# wire\n\n<!--\n## Interface\nno pin driver\n-->\n")
    assert len(cp.readme_problems(m)) == 2
    (m / "README.md").write_text(
        "# wire\n\n<!-- x -->## Interface\n\nno pin driver\n")
    assert cp.readme_problems(m) == []


def test_an_emphasised_heading_counts(tmp_path):
    m = module(tmp_path)
    (m / "README.md").write_text(
        "# wire\n\n## **Interface**\n\nFutaba.\n\n## `_Trademarks_`\n")
    assert cp.readme_problems(m) == []


@pytest.mark.parametrize("line", [
    '#include_next <foo.h>', '#import "../x.h"', '%:include "../x.h"',
    '# include\\\n"../../shared/x.h"', '#include "a//b.h"',
    '#include "//x"', '#include <a//b.h>', '#if __has_include("x.h")',
    '#if __has_include_next(<x.h>)', '#include H', '#include FILE_NAME(x)',
    '#include "..\\x.h"', '#include "\\x.h"', '#include "a.h" x'])
def test_an_include_bypass_is_flagged(tmp_path, line):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text(line + "\n" + c.read_text().split("\n", 1)[1])
    assert len(cp.include_problems(m)) >= 1, line


def test_includes_reads_continuations_digraphs_and_literal_slashes():
    text = ('#include_next <a.h>\n#import "b.h"\n%:include "c.h"\n'
            '#include \\\n  "d.h"\n#include "e//f.h" // tail\n'
            '#define H "g.h"\n#include H\n'
            'char *s = "/* #include \\"no.h\\" */";\n')
    assert cp.includes(text) == [
        ("next", "<a.h>"), ('"', "b.h"), ('"', "c.h"), ('"', "d.h"),
        ('"', "e//f.h"), ("?", "H")]


def test_a_string_with_slashes_does_not_hide_the_next_include():
    text = 'const char *u = "http://x";\n#include "a.h"\n'
    assert cp.includes(text) == [('"', "a.h")]


def write_core(m, text, encoding="utf-8"):
    (m / "wire.c").write_bytes(text.encode(encoding))


@pytest.mark.parametrize("text", [
    '# /*\n*/ include "../../shared/x.h"\n',
    '#\n#include <stdint.h>\n',
    '%:\n',
    '  # \t\n'])
def test_a_bare_directive_line_is_flagged(tmp_path, text):
    m = module(tmp_path)
    write_core(m, text)
    problems = cp.include_problems(m)
    assert len(problems) == 1
    assert "a line of only" in problems[0]


def test_a_bare_hash_inside_a_comment_or_string_is_not_a_directive():
    assert cp.includes('/*\n#\n*/\nchar *s = "#";\n') == []


def test_a_bom_does_not_hide_the_first_include(tmp_path):
    m = module(tmp_path)
    write_core(m, '\ufeff#include "../../shared/x.h"\n')
    assert any("a .. component" in p for p in cp.include_problems(m))


@pytest.mark.parametrize("line", [
    '\f#include "../x.h"', '#\finclude "../x.h"', '#\vinclude "../x.h"',
    '#include\f"../x.h"'])
def test_form_feed_and_vertical_tab_are_directive_white_space(line):
    assert cp.includes(line + "\n") == [('"', "../x.h")]


@pytest.mark.parametrize("line", [
    '??=include "../x.h"', '  ??= include <a.h>', '??=define A 1'])
def test_a_trigraph_directive_is_flagged(tmp_path, line):
    m = module(tmp_path)
    write_core(m, line + "\n")
    problems = cp.include_problems(m)
    assert len(problems) == 1
    assert "trigraph ??=" in problems[0]


def test_has_include_is_refused_as_an_operator_only():
    assert cp.includes("#if __has_include(<x.h>)\n") == [
        ("?", "__has_include")]
    assert cp.includes("#if __has_include_next (<x.h>)\n") == [
        ("?", "__has_include_next")]
    assert cp.includes("#if defined(__has_include) && __has_include\n(<x>)\n"
                       ) == [("?", "__has_include")]
    assert cp.includes("#ifdef __has_include\n#endif\n") == []
    assert cp.includes("#if __has_include  (<a.h>)\n#endif\n") == [
        ("?", "__has_include")]
    # A digit separator opens no character literal before the call.
    assert cp.includes("#if 0x1'0 && __has_include(<hardware/gpio.h>) "
                       "&& '1' == 49\n") == [("?", "__has_include")]
    assert cp.includes("#if 1__has_include(x)\n") == []
    assert cp.includes('puts("__has_include(x)");\n') == []
    assert cp.includes("/* __has_include(x) */\n") == []


def test_a_fence_opener_with_a_backtick_in_its_info_is_no_fence(tmp_path):
    m = module(tmp_path)
    (m / "README.md").write_text(
        "# wire\n\n```inline``` text\n\n## Interface\n")
    assert cp.readme_problems(m) == []
    (m / "README.md").write_text(
        "# wire\n\n~~~ a`b\n# Interface\n~~~\n")
    assert len(cp.readme_problems(m)) == 1


def test_a_fence_marker_inside_an_html_comment_opens_nothing(tmp_path):
    m = module(tmp_path)
    (m / "README.md").write_text(
        "# wire\n\n<!--\n```\n-->\n\n## Interface\n")
    assert cp.readme_problems(m) == []


@pytest.mark.parametrize("heading", [
    "Trademarks and notices", "Trademarks:", "[Trademarks](#t)",
    "**Trademark** notice", "TRADEMARKS"])
def test_a_heading_that_contains_trademark_counts(tmp_path, heading):
    m = module(tmp_path)
    (m / "README.md").write_text(
        "# wire\n\n## Interface\n\nFutaba.\n\n## %s\n" % heading)
    assert cp.readme_problems(m) == []


def test_a_brand_in_a_path_or_a_code_block_needs_the_heading(tmp_path):
    m = module(tmp_path)
    readme = m / "README.md"
    for text in ("See protocols/dshot/README.md.\n",
                 "```\nKST 1;\n```\n", "<!-- Futaba -->\n"):
        readme.write_text("# wire\n\n## Interface\n\n" + text)
        assert len(cp.readme_problems(m)) == 1, text


def test_a_raw_string_hides_no_include(tmp_path):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text(c.read_text()
                 + 'static const char *const rcb_raw = R"x(" /* )x";\n'
                 '#include "../../outside.h"\n/* */\n')
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


@pytest.mark.parametrize("prefix", ["", "u8", "u", "U", "L"])
def test_a_raw_string_is_one_literal(prefix):
    text = prefix + 'R"d(/* "#include "x.h" )d" ;\n#include "a.h"\n/* */\n'
    assert cp.includes(text) == [('"', "a.h")]
    assert cp.includes(prefix + 'R"(__has_include(x))";\n') == []
    # ISO C reads `R` and two strings, with the call between them.
    assert cp.includes(prefix + 'R"(a" __has_include(x) ")";\n') == [
        ("?", "__has_include")]


def test_an_identifier_ending_in_r_before_a_string_is_no_raw_string():
    """`FOOR"x("` is an identifier and a string; the comment after it runs
    to the next `*/`, as the compiler reads it."""
    assert cp.includes('FOOR"x(" /* )x";\n#include "a.h"\n/* */\n') == []


def test_a_digit_separator_hides_no_include_in_a_header(tmp_path):
    m = module(tmp_path, driver=False)
    h = m / "include" / "wire.h"
    h.write_text(h.read_text() + "#ifdef __cplusplus\n"
                 "static const int rcb_k = 1'0 + sizeof \"'/*\";\n"
                 '#include "../../../outside.h"\n'
                 "static const int rcb_o = OUTSIDE_VALUE + rcb_k;\n"
                 "// */\n#endif\n")
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


@pytest.mark.parametrize("number", ["1'000", "0x1'F", "0b1'0", "1.5e+3'1",
                                    "1e+3'1", ".5'5", "0x1p+4", "1e-x'y",
                                    "0x1p-a'b", "1E+z'0"])
def test_a_number_with_separators_is_one_token(number):
    text = "int k = %s + sizeof \"'/*\";\n#include \"a.h\"\n// */\n" % number
    assert cp.includes(text) == [('"', "a.h")]


def test_a_character_literal_with_a_prefix_is_not_a_number():
    text = "x = u8'a'; y = '/*';\n#include \"a.h\"\n/* */\n"
    assert cp.includes(text) == [('"', "a.h")]


def pio_with(m, text):
    pio = m / cp.DRIVER / "wire.pio"
    pio.write_text(text)
    return cp.include_problems(m)


def test_a_semicolon_comment_in_a_pio_file_hides_no_include(tmp_path):
    m = module(tmp_path)
    found = pio_with(
        m, ".program t\n; see /* below\n    nop\n% c-sdk {\n"
        '#include "../../../shared/x.h"\n%}\n; */\n')
    assert len(found) == 1 and "x.h" in found[0]


def test_a_semicolon_in_a_pio_c_block_starts_no_pio_comment(tmp_path):
    m = module(tmp_path)
    found = pio_with(
        m, ".program t\n    nop\n% c-sdk {\nint a; /* x */ int b;\n"
        '#include "../../../shared/x.h"\n%}\n')
    assert len(found) == 1
    # The block ends at `%}`: a `;` comment after it is blanked again.
    found = pio_with(
        m, ".program t\n% c-sdk {\n%}\n; /* c\n"
        '% c-sdk {\n#include "../../../shared/x.h"\n%}\n; */\n')
    assert len(found) == 1


def test_a_semicolon_comment_in_a_pio_file_may_name_an_include(tmp_path):
    m = module(tmp_path)
    assert pio_with(m, '.program t\n; #include "../x.h"\n    nop\n') == []


def test_a_semicolon_in_a_c_file_is_no_comment():
    assert cp.includes('int a; /* x */ int b;\n#include "a.h"\n') == [
        ('"', "a.h")]


@pytest.mark.parametrize("text", [
    "see protocols/hitec_x/hitec_x.c\n", "type kst_session_t\n",
    "link rcbench_kst\n", "dshot_tx and jeti_ex\n", "kst_core.h\n",
    "https://www.futaba.com\n", "a.kst\n", "x.hitec_y\n", "2kst\n",
    "foo.chaservo.md\n", "3KST\n"])
def test_a_brand_in_an_identifier_or_file_name_is_a_brand(tmp_path, text):
    """Also after a dot or a digit: in an address, a path and a number."""
    m = module(tmp_path)
    readme = m / "README.md"
    readme.write_text(readme.read_text() + text)
    assert len(cp.readme_problems(m)) == 1
    assert "has no heading" in cp.readme_problems(m)[0]


@pytest.mark.parametrize("text", ["the KSTART byte\n", "DShotty\n",
                                  "AKST and FutabaX\n", "KstSession\n"])
def test_a_brand_next_to_a_letter_is_no_brand(tmp_path, text):
    """A brand joined to a word without a separator is part of that word,
    also in a CamelCase identifier."""
    m = module(tmp_path)
    readme = m / "README.md"
    readme.write_text(readme.read_text() + text)
    assert cp.readme_problems(m) == []


def library_header(m, body):
    (m / "include" / "wire.h").write_text(header(body))
    return cp.header_problems(m, *cp.compilers())


@needs_compilers
@pytest.mark.parametrize("body, flag", [
    ("static inline void wire_u(void) { int u; }\n",
     "-Werror=unused-variable"),
    ("static inline int wire_f(int a, int b) { return a; }\n",
     "-Werror=unused-parameter")])
def test_a_warning_fails_the_header(tmp_path, body, flag):
    """-Wall alone finds the first, -Wextra the second, -Werror makes both
    an error.  The message names the compiler's error line, not the
    `In file included` line before it."""
    m = module(tmp_path, driver=False)
    found = library_header(m, body)
    assert found, flag
    assert found[0].startswith("wire/include/wire.h does not compile as C11: ")
    assert flag in found[0]
    assert "In file included" not in found[0]


@needs_compilers
def test_the_flags_are_wall_wextra_werror():
    assert cp.WARNINGS == ("-Wall", "-Wextra", "-Werror")


def test_callers_under_build_and_managed_components_are_not_read(tmp_path):
    m = module(tmp_path, driver=False)
    (m / "include" / "wire.h").write_text(header("#define WIRE_MAX 8\n"))
    for rel in ("shared/build_x/a.c", "firmware/managed_components/b/b.c",
                "firmware/build/c.c"):
        caller(tmp_path, '_Static_assert(WIRE_MAX == 8, "limit");\n', rel)
    assert asserted_problems(tmp_path, {}) == []
    caller(tmp_path, '_Static_assert(WIRE_MAX == 8, "limit");\n',
           "firmware/panel/p.c")
    out = asserted_problems(tmp_path, {})
    assert len(out) == 1 and "firmware/panel/p.c" in out[0]


@pytest.mark.parametrize("readme", [
    "```\n~~~\n## Interface\n```\n",
    "~~~\n```\n## Interface\n~~~\n",
    "````\n```\n## Interface\n````\n",
    "```\n``` text\n## Interface\n```\n",
    "```\n## Interface\n"])
def test_a_fence_closes_only_on_a_matching_bare_fence(tmp_path, readme):
    m = module(tmp_path)
    (m / "README.md").write_text("# wire\n\n" + readme)
    assert cp.readme_problems(m) == [
        'wire/README.md has no heading with "Interface"']


@pytest.mark.parametrize("readme", [
    "```\n```\n## Interface\n", "```\n````\n## Interface\n",
    "~~~~\n~~~~~\n## Interface\n", "```\n```  \n## Interface\n"])
def test_a_matching_fence_closes(tmp_path, readme):
    m = module(tmp_path)
    (m / "README.md").write_text("# wire\n\n" + readme)
    assert cp.readme_problems(m) == []


@pytest.mark.parametrize("sentence", [
    "No Pin Driver", "NO PIN DRIVER", "no  pin\ndriver", "no\tpin driver"])
def test_no_pin_driver_is_read_in_any_case_and_spacing(tmp_path, sentence):
    m = module(tmp_path, driver=False)
    (m / "README.md").write_text("# wire\n\n## Interface\n\n%s.\n"
                                 % sentence)
    assert cp.readme_problems(m) == []


@pytest.mark.parametrize("text", [
    "idf_component_register(SRCS wire.c PRIV_REQUIRES driver)\n",
    "idf_component_register(SRCS wire.c REQUIRES driver)\n"])
def test_requires_and_priv_requires_are_flagged(tmp_path, text):
    m = module(tmp_path)
    (m / "CMakeLists.txt").write_text(text)
    assert cp.cmake_problems(m) == [
        "wire/CMakeLists.txt carries REQUIRES: a module needs no other "
        "component"]


@pytest.mark.parametrize("heading", [
    "Inter**face**", "Inter_face_", "Inter`face`", "[Inter](x)face"])
def test_markup_inside_a_word_of_a_heading_is_stripped(tmp_path, heading):
    m = module(tmp_path)
    (m / "README.md").write_text("# wire\n\n## %s\n" % heading)
    assert cp.readme_problems(m) == []


def test_a_crlf_line_continuation_joins_the_include():
    assert cp.includes('#include \\\r\n  "../x.h"\r\n') == [
        ('"', "../x.h")]


# The C of a .pio file is what pioasm 2.3.0 writes into the generated
# header: the `% c-sdk {` blocks after the first `.program`, lexed as
# tools/pioasm/lexer.ll of the pico-sdk does.

X_H = '#include "../../../shared/x.h"\n'


@pytest.mark.parametrize("text", [
    # A `;` inside a `/* */` comment of PIO code ends nothing.
    ".program t\n/* shift out; LSB first */\n    out pins, 1\n% c-sdk {\n"
    '#include "hardware/clocks.h"\n' + X_H + "%}\n/* end */\n",
    ".program t\n/* ; */\n    nop\n% c-sdk {\n" + X_H + "%}\n// */\n",
    # A block ends only at `%}` in column 0 with blanks after it.
    ".program t\n    nop\n% c-sdk {\n/* x\n %}\n; */\n" + X_H
    + "// */\n%}\n",
    ".program t\n    nop\n% c-sdk {\n/* x\n%} y\n; */\n" + X_H
    + "// */\n%}\n",
    # An empty block name is c-sdk.
    ".program t\n    nop\n% {\n/* ; */\n" + X_H + "// */\n%}\n",
    # A block of another language is not written.
    ".program t\n    nop\n% python {\n/*\n%}\n% c-sdk {\n" + X_H
    + "// */\n%}\n",
    # The rest of a .lang_opt line is one string or word for pioasm.
    ".program t\n.lang_opt python a'b = \"' /* \"\n    nop\n% c-sdk {\n"
    + X_H + "// */\n%}\n",
    ".program t\n.LANG_OPT python a = \"/*\"\n    nop\n% c-sdk {\n"
    + X_H + "// */\n%}\n",
    # A block start inside a `/* */` comment opens no block.
    ".program t\n    nop\n/*\n% c-sdk {\n*/\n; /*\n% c-sdk {\n" + X_H
    + "%}\n; */\n// */\n",
    # A block before the first program is not written.
    "% c-sdk {\n/*\n%}\n.program t\n    nop\n% c-sdk {\n" + X_H
    + "// */\n%}\n"])
def test_a_pio_file_is_read_as_pioasm_lexes_it(tmp_path, text):
    m = module(tmp_path)
    found = pio_with(m, text)
    assert len(found) == 1 and "x.h" in found[0], text


def test_a_pio_c_block_reads_c_to_its_end(tmp_path):
    """Inside a block every line is C: a `;` on the second line of a
    three-line block starts no PIO comment, so its `/*` hides the include
    below it.  After `%}` a `;` comment is PIO again."""
    m = module(tmp_path)
    assert pio_with(
        m, ".program t\n    nop\n% c-sdk {\nint a = 1;\nint b; /* open\n"
        + X_H + "*/\n%}\n") == []
    found = pio_with(
        m, ".program t\n    nop\n% c-sdk {\nint a = 1;\nint b; // ;\n"
        + X_H + "%}\n")
    assert len(found) == 1 and "x.h" in found[0]
    found = pio_with(
        m, ".program t\n    nop\n% c-sdk {\nint a = 1;\nint b;\n%}\n"
        "; /* c\n% c-sdk {\n" + X_H + "%}\n; */\n")
    assert len(found) == 1 and "x.h" in found[0]


def test_pio_c_code_is_the_header_pioasm_writes():
    """Checked against pioasm 2.3.0 `-o c-sdk`: the text after the last `{`
    of a block start is the first line, blank lines go, a block name is
    trimmed and compared in its case, and each block ends with a newline."""
    text = ('% c-sdk {\n#include "glob.h"\n%}\n.PROGRAM t\n    nop\n'
            '% c-sdk { int z = 1;\n#include "a.h"\n\n   \n%}  \n'
            '% {\n#include "b.h"\n%}\n%c-sdk{\n#include "c.h"\n%}\n'
            '.LANG_OPT python x = "a /* b"\n'
            '% C-SDK {\n#include "upper.h"\n%}\n')
    assert cp.pio_c_code(text) == (
        ' int z = 1;\n#include "a.h"\n\n#include "b.h"\n\n'
        '#include "c.h"\n\n')


def test_a_c_file_is_read_with_the_rules_of_iso_c(tmp_path):
    """ISO C reads `0x1'2'` as a number and a character literal, and
    `R"( "( )"` as `R` and two strings: the include is outside a comment
    there, under the C++ rules it is not."""
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text(c.read_text() + "#define RCB_IGNORE(...)\n"
                 "RCB_IGNORE(0x1'2' \" ' /*\")\n"
                 '#include "../../outside.h"\n// */\n')
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]
    c.write_text(c.read_text().rsplit("#define", 1)[0]
                 + "#define RCB_IGNORE(...)\n"
                 'RCB_IGNORE(R"( "( )" /* ")\n'
                 '#include "../../outside.h"\n// */ )\n')
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


def test_an_exponent_sign_continues_the_number():
    """`1e-x'y` is one C++ number; the `'` after it opens the literal."""
    text = ("#ifdef __cplusplus\n#define RCB_IGNORE(...)\n"
            "RCB_IGNORE(1e-x'y' /* ')\n#include \"../outside.h\"\n"
            "// */\n#endif\n")
    assert cp.includes(text) == [('"', "../outside.h")]


def test_a_line_continuation_inside_a_raw_string_is_not_joined():
    """C++ keeps the backslash and the newline in the raw string, so
    `)\\` + newline + `x"` does not end it and the `/*` is inside it."""
    text = ('const char *s = R"x( )\\\nx" /* )x";\n'
            '#include "../outside.h"\n// */\n')
    assert cp.includes(text) == [('"', "../outside.h")]
    assert cp.includes('R"(a)\\\n" /* )";\n#include "a.h"\n// */\n') == [
        ('"', "a.h")]
    # A continuation before and after the raw string is joined.
    assert cp.includes('#define A \\\n R"(/*)" \\\n x\n#include "a.h"\n'
                       ) == [('"', "a.h")]


@pytest.mark.parametrize("blank", [" ", "\t", "\f", "\v", " \t "])
def test_blanks_between_backslash_and_newline_join_the_lines(tmp_path,
                                                             blank):
    """GCC and Clang join `\\`, blanks, newline: `*\\ ` + newline + `/`
    closes the comment, and the include below it is read."""
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text(c.read_text() + "/* note *\\" + blank + "\n/\n"
                 '#include "../../outside.h"\n/* */\n')
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


def test_a_lone_cr_ends_a_line_of_c(tmp_path):
    m = module(tmp_path)
    write_core(m, '// x\r#include "../../outside.h"\n')
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


def test_a_lone_cr_in_a_pio_block_is_part_of_the_line(tmp_path):
    """pioasm reads `%}` + CR + `*/` as one line of C, which closes the
    comment: the include after it is in the generated header."""
    m = module(tmp_path)
    (m / cp.DRIVER / "wire.pio").write_bytes(
        b'.program wire_tx\n    nop\n% c-sdk {\n/*\n%}\r*/\n'
        b'#include "../../../outside.h"\n%}\n')
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


def test_a_crlf_pio_file_ends_its_block_at_percent_brace(tmp_path):
    """`%}` + CRLF ends the block, so the `; /*` after it is a PIO
    comment and the include in the next block is C."""
    m = module(tmp_path)
    (m / cp.DRIVER / "wire.pio").write_bytes(
        b".program p\r\n    nop\r\n% c-sdk {\r\nint a;\r\n%}\r\n; /*\r\n"
        b'% c-sdk {\r\n#include "../../../outside.h"\r\n%}\r\n; */\r\n')
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


@pytest.mark.parametrize("blank", ["\v", "\f", "\r"])
def test_a_block_name_is_trimmed_of_spaces_and_tabs_only(blank):
    """pioasm 2.3.0 trims a block name of spaces and tabs; `c-sdk` + VT,
    FF or CR is a block of an unknown type and is not written."""
    assert cp.pio_c_code(".program p\n    nop\n% c-sdk" + blank + "{\n"
                         '#include "x.h"\n%}\n') == ""
    assert cp.pio_c_code(".program p\n    nop\n%" + blank + "{\n"
                         '#include "x.h"\n%}\n') == ""


@pytest.mark.parametrize("text", [
    "#if 0\ndon't /* worry\n#endif\n#include \"../../outside.h\"\n/* */\n",
    '#if 0\n"open /* string\n#endif\n#include "../../outside.h"\n/* */\n'])
def test_an_unterminated_literal_runs_to_the_line_end(tmp_path, text):
    """GCC lexes a `'` or `"` without its closing quote to the line end,
    so the `/*` after it opens no comment."""
    m = module(tmp_path)
    write_core(m, text)
    found = cp.include_problems(m)
    assert len(found) == 1 and "outside.h" in found[0]


@pytest.mark.parametrize("text", [
    '/* note *??/\n/\n#include "../../outside.h"\n/* */\n',
    "char c = '??/\n'; /* x */\n",
    'const char *s = "a??/"";\n',
    "/* ??/ */\n"])
def test_a_backslash_trigraph_is_flagged(tmp_path, text):
    """ISO C11 and C17 read `??/` as a backslash, also in a comment."""
    m = module(tmp_path)
    write_core(m, text)
    problems = cp.include_problems(m)
    assert any("trigraph ??/" in p for p in problems), problems


@pytest.mark.parametrize("tri", ["??=", "??/", "??'", "??(", "??)", "??!",
                                 "??<", "??>", "??-"])
def test_every_trigraph_is_flagged_anywhere(tmp_path, tri):
    m = module(tmp_path)
    write_core(m, "/* " + tri + ' */\nconst char *s = "' + tri + '";\n')
    problems = cp.include_problems(m)
    assert len(problems) == 2, problems
    assert all("trigraph " + tri in p for p in problems), problems


def test_a_quote_trigraph_fails_the_file_it_reads_differently():
    """ISO C11 reads `??'"'/*"` as `^` and the string `"'/*"`, so the
    include below is live there and hidden under the other standards."""
    assert cp.includes("#if 0\n??'\"'/*\"\n#endif\n" + OUT
                       + "#if 0\n*/\n#endif\n") == [("??", "??'")]


def test_each_trigraph_is_listed_and_a_lone_question_mark_pair_is_not():
    assert cp.includes("/* ??/ */\n/* ??/ */\n") == [("??", "??/"),
                                                     ("??", "??/")]
    assert cp.includes("/* ??' ??= */\n") == [("??", "??'"), ("??", "??=")]
    assert cp.includes("x = a ?? b; y = '?'; /* ?? ?a ??? */\n") == []
    # A trigraph is read before lines are joined, as GCC reads it.
    assert cp.includes("?\\\n?=define A\n") == []


def test_a_trigraph_in_the_c_of_a_pio_file_is_flagged(tmp_path):
    m = module(tmp_path)
    assert pio_with(m, ".program t\n; ??'\n    nop\n") == []
    found = pio_with(m, ".program t\n    nop\n% c-sdk {\n// ??'\n%}\n")
    assert len(found) == 1 and "trigraph ??'" in found[0], found


@pytest.mark.parametrize("data, line", [
    (b'/* x *\\\x00\n/\n#include "../../outside.h"\n/* */\n', 1),
    (b'#include <stdint.h>\n\x00\\\n#include "../../outside.h"\n', 2),
    (b"int a;\r\n/* \x00 */\r\n", 2), (b"int a;\r\r/* \x00 */\r", 3)])
def test_a_nul_byte_fails_the_file(tmp_path, data, line):
    """GCC reads a NUL as a blank: a backslash, NUL, newline joins the
    lines, and the include after it is live.  The file fails."""
    m = module(tmp_path)
    (m / "wire.c").write_bytes(data)
    problems = cp.include_problems(m)
    assert ("wire/wire.c has a NUL byte on line %d, which GCC reads as "
            "white space and the check does not read" % line) in problems


def test_a_nul_byte_in_a_pio_file_fails_it(tmp_path):
    m = module(tmp_path)
    (m / cp.DRIVER / "wire.pio").write_bytes(b".program t\n; \x00\n    nop\n")
    assert cp.include_problems(m) == [
        "wire/rp2350/wire.pio has a NUL byte on line 2, which GCC reads as "
        "white space and the check does not read"]


# Each set of literal rules is the only one that finds the include in one
# text: a raw string, a digit separator, a delimited universal character
# name, or none of them, opens a comment that hides it under the other
# four.  The second is gnu11 and gnu17: `\u{e9}R` is an identifier, so no
# raw string opens.
VIEW_ONLY = {
    (False, False, False):
        'R"(")" /*"\n1\'a /* \'\n#include "b.h"\n*/\n',
    (True, False, True):
        'R"(a" /* ")"\n1\'a /* \'\n\\u{e9}R"(" \'/*)" /*\'\n#include "b.h"\n'
        '*/\n',
    (False, True, False):
        'R"(")" /*"\n1\'a\'/*\'\n#include "b.h"\n*/\n',
    (True, True, False):
        'R"(a" /* ")"\n1\'a\'/*\'\n1\\u{e9}\'x /* \'\n#include "b.h"\n'
        '*/\n',
    (True, True, True):
        'R"(a" /* ")"\n1\'a\'/*\'\n1\\u{e9}\'x\' /* \'\n#include "b.h"\n'
        '*/\n',
}


def test_the_check_reads_five_sets_of_literal_rules():
    assert sorted(cp.LITERAL_VIEWS) == sorted(VIEW_ONLY)


@pytest.mark.parametrize("view", list(VIEW_ONLY))
def test_each_literal_view_finds_an_include_the_others_do_not(view):
    text = VIEW_ONLY[view]
    assert cp.includes(text) == [('"', "b.h")]
    for other in VIEW_ONLY:
        found = cp.view_includes(text, cp.source_token_re(*other))
        assert (found == [('"', "b.h")]) == (other == view), other


@pytest.mark.parametrize("text", [
    'R"(a)"\\\n /*"\n#include "b.h"\n*/\n',
    'R"(a)" /*"\n#include "b.h"\n*/\n',
    'R"(a)"/*"\n#include "b.h"\n*/\n'])
def test_the_text_after_a_raw_string_is_read_from_its_end(text):
    """The scan resumes right after the raw string's closing `"`, also when
    a line continuation follows it: the `/*` is a comment."""
    assert cp.includes(text) == []


def test_a_repeated_include_is_listed_each_time():
    assert cp.includes('#include "a.h"\n#include "a.h"\n') == [
        ('"', "a.h"), ('"', "a.h")]


# Differential: the check against the compilers and pioasm.  Each case puts
# `#include "../outside.h"` somewhere; the check finds it exactly when one
# of the compilers includes the file, under any of the C standards and, for
# a header, C++17.  The C of a .pio file is read from the header pioasm
# writes.  A missing tool skips the case.

GCC = shutil.which("gcc") or shutil.which("cc")
GXX = shutil.which("g++") or shutil.which("c++")
PIOASM = os.environ.get("PIOASM") or shutil.which("pioasm")
C_STDS = ("-std=c11", "-std=gnu11", "-std=c2x", "-std=gnu2x")
MARK = "rcb_outside_marker"
OUT = '#include "../outside.h"\n'

# A digit, then a character that continues a GCC preprocessing number
# besides a letter, digit and `_`: `$`, universal character names with
# lowercase and uppercase hex digits, delimited and named ones (gnu11,
# gnu17, gnu23, C++23 and gnu++14 on), and characters outside ASCII
# (U+00E9, U+0301, U+203F, U+00B7, U+200D).
NUMBER_IDENTIFIER_CHARS = ("1$", ".1$", "1\\u00e9", "1\\U000000e9",
                           "1\\u00E9", "1\\u00AA", "1\\U000000E9",
                           "1\\u{e9}", "1\\u{E9}", ".1\\u{e9}",
                           "1\\N{LATIN SMALL LETTER E WITH ACUTE}",
                           "1\\N{CJK UNIFIED IDEOGRAPH-4E00}",
                           "1\u00e9", "1\u0301", "1\u203f", "1\u00b7",
                           "1\u200d")
# A character before a digit that starts the number there: `×` and a
# no-break space are no identifier characters, so C23 reads `1'a`, then the
# character literal `' /* '`.  A punctuator ends a number: `1<'a'` is `1`,
# `<` and the character literal `'a'`.
NUMBER_STARTS = {"\u00d71": True, "\u00a01": True, "1<": False}
# A delimited or named universal character name after a digit: gnu11,
# gnu17, gnu23, C++23 and gnu++14 on read it as part of the number; C23 and
# C++14 to C++20 end the number before the `\`, so after a digit separator
# earlier on the line `'x /* '` is a character literal there.
DELIMITED_ENDS = tuple("#define Z 0'0 1" + u + "'x /* '\n" + OUT + "/* */\n"
                       for u in ("\\u{e9}",
                                 "\\N{LATIN SMALL LETTER E WITH ACUTE}"))
# Characters outside the identifier ranges of GCC 13 (cp.EXTENDED_RANGES):
# `\u2e2f`, which Python's `\\w` matches, and `\u00d7`.  In C each is a token
# of its own, so a number or raw string starts after it, and it ends a
# number.  In gnu11, gnu17, gnu23, C++23 and gnu++14 on, an identifier
# that starts at a
# delimited universal character name runs through the digit after it, so
# `'a /* '` is a character literal; C++14 to C++20 end a number before the
# `\`, so `R"(` opens a raw string.  `\u00b7` and `\u20ac` are in the
# ranges: `\u00b71` is an identifier, `'a'` a character literal, and the
# comment hides the include.
# gnu11 and gnu17 read a delimited or named universal character name into
# an identifier or number and have no digit separators: `\u{e9}R` is an
# identifier, `"("` a string and `'/*)" /*'` a character literal.  The first
# line hides the include from the rules without raw strings, the second from
# those with digit separators.
GNU_DELIMITED = tuple('R"(a" /* ")"\n1\'a /* \'\n' + w + 'R"(" \'/*)" /*\'\n'
                      + OUT + "/* */\n"
                      for w in ("\\u{e9}",
                                "\\N{LATIN SMALL LETTER E WITH ACUTE}",
                                "1\\u{e9}", "x\\u{e9}"))
# A decimal digit outside ASCII starts an identifier, not a number: GCC 13
# starts a number at 0 to 9 only.  After a digit separator earlier on the
# line, `\u06601'a /* '` is an identifier and a character literal in C23
# and C++, and `\u06601'a' /* '` opens a comment.
UNICODE_DIGITS = {
    "#define Z 0'0 \u06601'a /* '\n": True,
    "#define Z 0'0 \uff11'a /* '\n": True,
    "#define Z 0'0 \u0967'a /* '\n": True,
    "#define Z 0'0 .\u0660'a /* '\n": True,
    "#define Z \u06601'a' /* '\n": False,
}
UNLISTED_CHARS = {
    "#define Z \u2e2f1'a' /* '\n": True,
    '#define Z \u2e2fR"(" /* )"\n': True,
    "#define Z 0'0 1\u2e2f'a /* '\n": True,
    "#define Z 0'0 1\u00d7'a /* '\n": True,
    "#define Z 0'0 \\u{e9}1'a /* '\n": True,
    "#define Z 0'0 \\N{LATIN SMALL LETTER E WITH ACUTE}1'a /* '\n": True,
    '#define Z 1\\u{e9}R"(" /* )"\n': True,
    "#define Z \u00b71'a' /* '\n": False,
    "#define Z \u20ac1'a' /* '\n": False,
}

DIFF_C = [
    ("c", "#define RCB_IGNORE(...)\nRCB_IGNORE(0x1'2' \" ' /*\")\n" + OUT
     + "// */\n"),
    ("c", "#define RCB_IGNORE(...)\nRCB_IGNORE(R\"( \"( )\" /* \")\n" + OUT
     + "// */ )\n"),
    ("h", "#define RCB_IGNORE(...)\nRCB_IGNORE(R\"( \"( )\" /* \")\n" + OUT
     + "// */ )\n"),
    ("h", "#ifdef __cplusplus\n#define RCB_IGNORE(...)\n"
     "RCB_IGNORE(1e-x'y' /* ')\n" + OUT + "// */\n#endif\n"),
    ("h", 'const char *s = R"x( )\\\nx" /* )x";\n' + OUT + "// */\n"),
    ("c", 'static const char *const rcb_raw = R"x(" /* )x";\n' + OUT
     + "/* */\n"),
    ("h", "#ifdef __cplusplus\nstatic const int rcb_k = 1'0 + "
     "sizeof \"'/*\";\n" + OUT + "// */\n#endif\n"),
    ("c", "x = u8'a'; y = '/*';\n" + OUT + "/* */\n"),
    ("h", 'FOOR"x(" /* )x";\n' + OUT + "/* */\n"),
    ("c", "# include\\\n\"../outside.h\"\n"),
    ("c", "/\\\n* x *\\\n/\n" + OUT),
    ("c", "// x \\\n" + OUT),
    ("c", 'const char *u = "http://x";\n' + OUT),
    ("c", "/*\n" + OUT + "*/\n"),
    ("c", "int a; /* x */ int b;\n" + OUT),
    *[("h", "int k = %s + sizeof \"'/*\";\n" % n + OUT + "// */\n")
      for n in ("1'000", "0x1'F", "0b1'0", "1.5e+3'1", "1e+3'1", ".5'5",
                "0x1p+4", "1e-x'y", "0x1p-a'b")],
    *[("h", p + 'R"d(/* "#include "x.h" )d" ;\n' + OUT + "/* */\n")
      for p in ("", "u8", "u", "U", "L")],
    ("c", "/* note *\\ \n/\n" + OUT + "/* */\n"),
    ("c", "/* note *\\\t\n/\n" + OUT + "/* */\n"),
    ("c", "// x\r" + OUT),
    ("c", "#if 0\ndon't /* worry\n#endif\n" + OUT + "/* */\n"),
    ("c", '#if 0\n"open /* string\n#endif\n' + OUT + "/* */\n"),
    *[("h", text.replace('#include "b.h"\n', OUT))
      for text in VIEW_ONLY.values()],
    # A `'` before a `.` ends a number: C23 and C++ read `1`, then the
    # character literal `'.X'`, and no comment opens.
    *[(k, "#if 0\n1'2 1'.X' '/*'\n#endif\n" + OUT + "#if 0\n*/\n#endif\n")
      for k in ("c", "h")],
    ("h", "#ifdef __cplusplus\n#define F(...)\nF(1.'a' /*')\n" + OUT
     + "// */\n#endif\n"),
    ("h", "#ifdef __cplusplus\n#define F(...)\nF(x1'a' /*')\n" + OUT
     + "// */ )\n#endif\n"),
    # A backslash before the line end escapes nothing: `"abc\` ends there.
    *[("c", "#if 0\n" + q + "abc\\\\\n\nx" + q + " /*\n#endif\n" + OUT
       + "#if 0\n*/\n#endif\n") for q in "\"'"],
    ("c", 'x "a\\\\\n\n" /*\n' + OUT + "*/\n"),
    ("c", 'const char *s = "\\\\" /*\n' + OUT + "*/\n"),
    ("c", "int c = '\\\\' /*\n" + OUT + "*/\n"),
    ("c", '#if 0\nx "a\n#endif\n/* "\n' + OUT + "*/\n"),
    ("c", "#if 0\nx 'a\n#endif\n/* '\n" + OUT + "*/\n"),
    # Raw strings: every prefix, a 16-character delimiter, the first end.
    *[("h", p + 'R"x(" /* )x";\n' + OUT + "/* */\n")
      for p in ("", "u8", "u", "U", "L")],
    ("h", 'R"0123456789abcdef(" /* )0123456789abcdef";\n' + OUT
     + "/* */\n"),
    ("h", 'R"(a)" /* )";\n' + OUT + "// */\n"),
    # Offsets between the text as written and the joined text.
    ("h", '#define A \\\n x \\\nR"(" /* )"\n' + OUT + "/* */\n"),
    ("h", '#define A \\\nR"(" /* )"\n' + OUT + "/* */\n"),
    ("h", 'R\\\n"(" /* )";\n' + OUT + "/* */\n"),
    ("h", '#define A \\\n R"(a)"/*\n' + OUT + "*/\n"),
    # A directive starts a line; an include operand is one token.
    ("c", "int x; #include <a/*b.h>\n" + OUT + "/* */\n"),
    # An include operand is one token in a skipped group too, a `\` in it
    # escapes nothing, and it ends at the line end.
    ("c", '#if 0\n#include "w\\" x "/*"\n#endif\n' + OUT + "// */\n"),
    ("c", '#if 0\n#include <a\n#endif\nconst char *s = "> /*";\n' + OUT
     + "// */\n"),
    ("c", '#if 0\n#include "a\n#endif\nconst char *s = "/*";\n' + OUT
     + "// */\n"),
    # A number runs through `$`, a universal character name and a
    # character outside ASCII, and a digit separator after them: C23 and
    # C++ read `1$'a`, then the character literal `' /* '`.  A `'` before
    # any of them ends the number.
    *[("h", "#define Z " + n + "'a' /* '\n" + OUT + "/* */\n")
      for n in NUMBER_IDENTIFIER_CHARS],
    ("h", "#define Z 1'$a' /* '\n" + OUT + "/* */\n"),
    *[("c", "#define Z " + n + "'a' /* '\n" + OUT + "/* */\n")
      for n in NUMBER_STARTS],
    *[("h", text) for text in DELIMITED_ENDS],
    *[("h", line + OUT + "/* */\n") for line in UNLISTED_CHARS],
    *[("c", text) for text in GNU_DELIMITED],
    *[("h", line + OUT + "/* */\n") for line in UNICODE_DIGITS],
]
# The C pioasm writes for these .pio texts, read by gcc: True where the
# include is live.  Run without pioasm too, as fixed expectations.
PIO_EXPECTED = {
    # A `//` comment outside a block runs to the line end.
    ".program t\n    nop\n// /*\n% c-sdk {\n" + OUT + "%}\n// */\n": True,
    # A block start inside a `/* */` comment opens no block.
    "/* .program x */\n% c-sdk {\n" + OUT + "%}\n.program t\n    nop\n":
        False,
    # `/*/` opens a comment and does not close it.
    ".program t\n    nop\n/*/\n% c-sdk {\n" + OUT + "%}\n*/\n": False,
    # A comment ends after its `*/`, and the next `/*` opens another.
    ".program t\n    nop\n/* a *//* b\n% c-sdk {\n" + OUT + "%}\n*/\n":
        False,
    # A line of a VT is written, so the `//` comment ends before the
    # include; a line of a CR is blank and dropped, so it does not.
    ".program t\n    nop\n% c-sdk {\n// x \\\n\v\n" + OUT + "%}\n": True,
    ".program t\n    nop\n% c-sdk {\n// x \\\n\r\n" + OUT + "%}\n": False,
    # The name runs to the last `{`: `c-sdk {` is no c-sdk block.
    ".program t\n    nop\n% c-sdk { {\n" + OUT + "%}\n": False,
    # `%}` and a tab ends a block; `%}` and a form feed does not.
    ".program t\n    nop\n% c-sdk {\n%}\t\n; /*\n% c-sdk {\n" + OUT
    + "%}\n; */\n% c-sdk {\n*/\n%}\n": True,
    ".program t\n    nop\n% c-sdk {\n/*\n%}\f\n*/\n" + OUT + "%}\n": True,
}
DIFF_PIO = [
    ".program t\n/* shift out; LSB first */\n    out pins, 1\n% c-sdk {\n"
    '#include "hardware/clocks.h"\n' + OUT + "%}\n/* end */\n",
    ".program t\n/* ; */\n    nop\n% c-sdk {\n" + OUT + "%}\n// */\n",
    ".program t\n    nop\n% c-sdk {\n/* x\n %}\n; */\n" + OUT
    + "// */\n%}\n",
    ".program t\n    nop\n% c-sdk {\n/* x\n%} y\n; */\n" + OUT
    + "// */\n%}\n",
    ".program t\n    nop\n% {\n/* ; */\n" + OUT + "// */\n%}\n",
    ".program t\n    nop\n% python {\n/*\n%}\n% c-sdk {\n" + OUT
    + "// */\n%}\n",
    ".program t\n.lang_opt python a'b = \"' /* \"\n    nop\n% c-sdk {\n"
    + OUT + "// */\n%}\n",
    ".program t\n    nop\n/*\n% c-sdk {\n*/\n; /*\n% c-sdk {\n" + OUT
    + "%}\n; */\n// */\n",
    "% c-sdk {\n/*\n%}\n.program t\n    nop\n% c-sdk {\n" + OUT
    + "// */\n%}\n",
    ".program t\n    nop\n% c-sdk {\nint a = 1;\nint b; /* open\n"
    + OUT + "*/\n%}\n",
    ".program t\n    nop\n% c-sdk {\nint a = 1;\nint b;\n%}\n; /* c\n"
    "% c-sdk {\n" + OUT + "%}\n; */\n",
    ".program t\n; see /* below\n    nop\n% c-sdk {\n" + OUT
    + "%}\n; */\n",
    ".program t\n    nop\n% c-sdk {\n/*\n%}\n% c-sdk {\n" + OUT
    + "*/\n%}\n",
    ".program t\n    nop\n% c-sdk {\n/*\n%}\n.program u\n    nop\n"
    "% c-sdk {\n" + OUT + "*/\n%}\n",
    # A lone CR is part of a line for pioasm; CRLF ends a block at `%}`.
    ".program t\n    nop\n% c-sdk {\n/*\n%}\r*/\n" + OUT + "%}\n",
    ".program p\r\n    nop\r\n% c-sdk {\r\nint a;\r\n%}\r\n; /*\r\n"
    "% c-sdk {\r\n" + OUT.replace("\n", "\r\n") + "%}\r\n; */\r\n",
    # A block name with VT, FF or CR is no c-sdk block.
    *[".program t\n    nop\n% c-sdk" + b + "{\n/*\n%}\n% c-sdk {\n" + OUT
      + "%}\n" for b in ("\v", "\f", "\r")],
    *PIO_EXPECTED,
]


def compiler_runs(header):
    runs = [(GCC, "c", std) for std in C_STDS]
    if header:
        runs.append((GXX, "c++", "-std=c++17"))
    return runs


def compilers_include(path, header, tmp_path):
    """True when one compiler run includes ../outside.h from @p path."""
    stubs = tmp_path / "stubs" / "hardware"
    stubs.mkdir(parents=True, exist_ok=True)
    for name in ("pio.h", "clocks.h"):
        (stubs / name).write_text("\n")
    (tmp_path / "outside.h").write_text("int %s;\n" % MARK)
    for compiler, lang, std in compiler_runs(header):
        run = subprocess.run(
            [compiler, "-x", lang, std, "-E", "-I", str(tmp_path / "stubs"),
             str(path)], capture_output=True, text=True)
        if MARK in run.stdout:
            return True
    return False


needs_gcc = pytest.mark.skipif(not (GCC and GXX),
                               reason="no gcc and g++ on PATH")


@needs_gcc
@pytest.mark.parametrize("kind, text", DIFF_C)
def test_the_check_agrees_with_the_compilers(tmp_path, kind, text):
    src = tmp_path / "d" / ("case." + kind)
    src.parent.mkdir()
    src.write_bytes(text.encode())
    found = ('"', "../outside.h") in cp.source_includes(src)
    assert found == compilers_include(src, kind == "h", tmp_path), text


@needs_gcc
@pytest.mark.skipif(PIOASM is None, reason="no pioasm (set PIOASM)")
@pytest.mark.parametrize("text", DIFF_PIO)
def test_the_check_agrees_with_pioasm(tmp_path, text):
    src = tmp_path / "d" / "case.pio"
    src.parent.mkdir()
    src.write_bytes(text.encode())
    out = tmp_path / "d" / "case.pio.h"
    run = subprocess.run([PIOASM, "-o", "c-sdk", str(src), str(out)],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    found = ('"', "../outside.h") in cp.source_includes(src)
    assert found == compilers_include(out, True, tmp_path), text


@pytest.mark.parametrize("text", list(PIO_EXPECTED))
def test_a_pio_file_gives_the_c_pioasm_writes(text):
    """The expectations of PIO_EXPECTED, checked against pioasm 2.3.0 and
    gcc by test_the_check_agrees_with_pioasm."""
    found = ('"', "../outside.h") in cp.includes(text, pio=True)
    assert found == PIO_EXPECTED[text], text


@pytest.mark.parametrize("text, found", [
    ('u8R"x(" /* )x";\n' + OUT + "/* */\n", True),
    ('LR"x(" /* )x";\n' + OUT + "/* */\n", True),
    ('uR"x(" /* )x";\n' + OUT + "/* */\n", True),
    ('R"0123456789abcdef(" /* )0123456789abcdef";\n' + OUT + "/* */\n",
     True),
    ('R"(a)" /* )";\n' + OUT + "// */\n", False),
    ('#define A \\\n x \\\nR"(" /* )"\n' + OUT + "/* */\n", True),
    ('#define A \\\nR"(" /* )"\n' + OUT + "/* */\n", True),
    ('R\\\n"(" /* )";\n' + OUT + "/* */\n", True),
    ('#define A \\\n R"(a)"/*\n' + OUT + "*/\n", False),
    ('const char *s = "\\\\" /*\n' + OUT + "*/\n", False),
    ("int c = '\\\\' /*\n" + OUT + "*/\n", False),
    ('#if 0\nx "a\n#endif\n/* "\n' + OUT + "*/\n", False),
    ('x "a\\\\\n\n" /*\n' + OUT + "*/\n", True),
    ("x 'a\\\\\n\n' /*\n" + OUT + "*/\n", True),
    ("#if 0\n1'2 1'.X' '/*'\n#endif\n" + OUT + "#if 0\n*/\n#endif\n", True),
    ("#ifdef __cplusplus\n#define F(...)\nF(x1'a' /*')\n" + OUT
     + "// */ )\n#endif\n", False),
    ("#ifdef __cplusplus\n#define F(...)\nF(1.'a' /*')\n" + OUT
     + "// */\n#endif\n", True),
    ("int x; #include <a/*b.h>\n" + OUT + "/* */\n", False),
    ('#if 0\n#include "w\\" x "/*"\n#endif\n' + OUT + "// */\n", True),
    ('#if 0\n#include <a\n#endif\nconst char *s = "> /*";\n' + OUT
     + "// */\n", True),
    ('#if 0\n#include "a\n#endif\nconst char *s = "/*";\n' + OUT
     + "// */\n", True),
    *[("#define Z " + n + "'a' /* '\n" + OUT + "/* */\n", True)
      for n in NUMBER_IDENTIFIER_CHARS],
    ("#define Z 1'$a' /* '\n" + OUT + "/* */\n", False),
    *[("#define Z " + n + "'a' /* '\n" + OUT + "/* */\n", found)
      for n, found in NUMBER_STARTS.items()],
    ("#define Z 1'\u00e9a' /* '\n" + OUT + "/* */\n", False),
    ("#define Z $1'a' /* '\n" + OUT + "/* */\n", False),
    *[(text, True) for text in DELIMITED_ENDS],
    *[(line + OUT + "/* */\n", found)
      for line, found in UNLISTED_CHARS.items()],
    *[(text, True) for text in GNU_DELIMITED],
    *[(line + OUT + "/* */\n", found)
      for line, found in UNICODE_DIGITS.items()]])
def test_literals_numbers_and_raw_strings_end_where_gcc_ends_them(text,
                                                                  found):
    """The expectations of the DIFF_C cases on raw string prefixes and
    delimiters, escapes, unterminated literals and numbers, checked
    against gcc by test_the_check_agrees_with_the_compilers."""
    assert (('"', "../outside.h") in cp.includes(text)) == found, text


def test_an_angle_operand_with_slashes_is_one_token(tmp_path):
    assert cp.includes("#include <hardware//pio.h>\n") == [
        ("<", "hardware//pio.h")]
    assert cp.includes("int x;\n#include <hardware//pio.h>\n") == [
        ("<", "hardware//pio.h")]
    m = module(tmp_path)
    c = m / cp.DRIVER / "wire_pin.c"
    c.write_text(c.read_text() + "#include <hardware//pio.h>\n")
    assert cp.include_problems(m) == []


@pytest.mark.parametrize("name", [
    "/x.h", "\\x.h", "../x.h", "..\\x.h", "a/../x.h", "a\\..\\x.h"])
def test_a_parent_or_absolute_path_is_named_as_one(tmp_path, name):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text(c.read_text() + f'#include "{name}"\n')
    assert cp.include_problems(m) == [
        f"wire/wire.c includes {name}: a path with a .. "
        "component or an absolute path"], name


def test_an_include_is_read_exactly():
    """A token after the operand makes the operand unresolved, a line of
    only `#` is named by its `#`, and a `__has_include(` inside an include
    operand is part of the file name."""
    assert cp.includes('#include "a.h" x\n') == [("?", '"a.h" x')]
    assert cp.includes('#include "a.h" /* x */\n') == [('"', "a.h")]
    assert cp.includes("  #  \n%: \t\n") == [("#", "#"), ("#", "%:")]
    assert cp.includes("#include <__has_include(x.h)>\n") == [
        ("<", "__has_include(x.h)")]


@pytest.mark.parametrize("text", [
    "#if 0\n#importer \"../outside.h\"\n#endif\n",
    "#define foo__has_include(x) 1\n#if foo__has_include(x)\n#endif\n"])
def test_a_longer_word_is_no_directive_and_no_call(text):
    assert cp.includes(text) == []


@pytest.mark.parametrize("readme", [
    "# wire\n\n<!--\n## Interface\n",
    "## Inter<!--\n-->face\n",
    "    ## Interface\n",
    "####### Interface\n",
    "##Interface\n",
    "```\nx\n    ```\n## Interface\n",
    "  ```\n## Interface\n  ```\n",
    "  ~~~\n## Interface\n  ~~~\n"])
def test_a_line_commonmark_reads_as_no_heading_is_none(tmp_path, readme):
    """An unclosed comment runs to the end, a comment keeps its line
    breaks, a heading has 0 to 3 spaces of indent, 1 to 6 `#` and a blank
    after them, and a fence has 0 to 3 spaces of indent."""
    m = module(tmp_path)
    (m / "README.md").write_text(readme)
    assert cp.readme_problems(m) == [
        'wire/README.md has no heading with "Interface"'], readme


@pytest.mark.parametrize("readme", [
    "# Interface\nSxBUS frame\n",
    "~~\n## Interface\n~~\n",
    "    ~~~\n## Interface\n    ~~~\n",
    "    ```\n## Interface\n    ```\n"])
def test_a_line_commonmark_reads_as_no_fence_hides_nothing(tmp_path,
                                                          readme):
    """`~~` and a fence indented 4 spaces open no block, and the `.` of
    S.BUS matches only a dot."""
    m = module(tmp_path)
    (m / "README.md").write_text(readme)
    assert cp.readme_problems(m) == [], readme


@pytest.mark.parametrize("sentence", ["casino pin driver",
                                      "no pin drivers"])
def test_no_pin_driver_is_matched_as_whole_words(tmp_path, sentence):
    m = module(tmp_path, driver=False)
    (m / "README.md").write_text("# Interface\n%s\n" % sentence)
    assert len(cp.readme_problems(m)) == 1, sentence


# A macro body that a comment or raw string literal carries across line
# breaks, with a line that starts like an `#if` or `#elif` line of
# `defined` terms.  `#if` in the body stringifies the parameter `if` (and
# `#elif` the parameter `elif`), DROP swallows the string, and `defined` is
# a parameter that expands to nothing: `F(a, )` in an `#if` is
# `__has_include ("../outside.h")`.
CONTINUED_BODIES = tuple(
    "#define DROP(...)\n#define F(" + word + ", defined) DROP(" + opener
    + "\n" + closer + " " + hash + word + " ) defined __has_include /*\n"
    '*/ ("../outside.h")\n'
    for opener, closer in ((" /*", "*/"), ('R"(', ')"'))
    for hash in ("#", "#  ", "%:") for word in ("if", "elif"))


@pytest.mark.parametrize("text, name", [
    ('#define PROBE __has_include\n#if PROBE("../../x.h")\n#endif\n',
     "__has_include"),
    ("#define PROBE __has_include_next\n", "__has_include_next"),
    ("#if X(__has_include)\n#endif\n", "__has_include"),
    ("#undef __has_include\n", "__has_include"),
    ("#ifdef A __has_include\n#endif\n", "__has_include"),
    ("#if !defined(A) && __has_include(<a.h>)\n#endif\n", "__has_include"),
    ("int undefined __has_include;\n", "__has_include"),
    # A character outside the identifier ranges is no part of the name.
    ("int \u2e2f__has_include;\n", "__has_include"),
    ("int \u00d7__has_include_next;\n", "__has_include_next"),
    # `defined` that is no `#if` operator: part of the identifier `$defined`
    # or `x\u00b7defined`, a macro parameter, or pasted to `un` in a macro
    # argument.  GCC 13 evaluates each call.
    *[("#define " + n + "\n#if " + n + ' __has_include("../../x.h")\n'
       "#endif\n", "__has_include") for n in ("$defined", "x\u00b7defined")],
    ('#define F(defined) defined __has_include\n#if F()("../../x.h")\n'
     "#endif\n", "__has_include"),
    ("#define undefined\n#define G(x) un##x\n"
     '#if G(defined __has_include)("../../x.h")\n#endif\n', "__has_include"),
    ("#define undefined\n#define P(x) x##defined __has_include\n"
     '#if P(un)("../../x.h")\n#endif\n', "__has_include"),
    # Any other token on the line fails the `defined` term too.
    ("#if X || defined(__has_include)\n#endif\n", "__has_include"),
    ("#if defined(__has_include) && 1\n#endif\n", "__has_include"),
    # A comment or raw string literal across a line break continues the
    # line: the `#if` is in the body of F, and GCC 13 evaluates the call.
    *[(text, "__has_include") for text in CONTINUED_BODIES]])
def test_the_name_has_include_outside_a_literal_fails(text, name):
    """GCC evaluates a macro that stands for `__has_include` as the call, so
    the name fails wherever it stands outside a literal, except as the
    operand of a `defined` term on an `#if` line of nothing but such terms
    and `( ) ! & |`."""
    assert cp.includes(text) == [("?", name)]


@pytest.mark.parametrize("text", [
    "#ifdef __has_include\n#endif\n", "#ifndef __has_include_next\n#endif\n",
    "#if 0\n#elifdef __has_include\n#elifndef __has_include\n#endif\n",
    "%:ifdef __has_include\n#endif\n", "  #  ifdef\t__has_include\n#endif\n",
    "#if defined __has_include && defined(__has_include_next)\n#endif\n",
    "#if defined ( __has_include ) || defined\t__has_include\n#endif\n",
    "#if defined /* x */ ( /* y */ __has_include )\n#endif\n",
    "#if !(defined(__has_include) || defined __has_include_next)\n#endif\n",
    "#if 0\n%:elif defined(__has_include)\n#endif\n",
    # No limit on the white space and comments between `defined` and the
    # name: 60 and 100 characters.
    *["#if defined" + gap + "__has_include\n#endif\n"
      for gap in (" " * 60, " " * 100, "/*" + "x" * 96 + "*/")],
    "#if defined(" + " " * 100 + "__has_include)\n#endif\n",
    "#if foo__has_include || __has_include_x || __has_include__\n#endif\n",
    "int $__has_include, \u00e9__has_include, \u00b7__has_include;\n",
    "int __has_include\u00e9, __has_include$, \\u00e9__has_include;\n",
    'puts("__has_include"); /* __has_include */\n',
    # A comment across a line break is one space of the directive's line,
    # also before the `#` and between `defined` and the name.
    "/*\n*/ #if defined(__has_include)\n#endif\n",
    "/* a\n b */ %:if !defined __has_include\n#endif\n",
    "#if defined /*\n*/ __has_include\n#endif\n",
    "#if defined(__has_include) /*\n*/ || defined(X)\n#endif\n",
    "#ifdef /*\n*/ __has_include\n#endif\n"])
def test_has_include_as_the_operand_of_defined_or_ifdef_passes(text):
    """`defined`, `#ifdef`, `#ifndef`, `#elifdef` and `#elifndef` test the
    name as a macro and include nothing; a longer identifier is another
    name."""
    assert cp.includes(text) == []


@needs_gcc
@pytest.mark.parametrize("text", CONTINUED_BODIES)
def test_gcc_evaluates_has_include_in_a_body_a_comment_continues(tmp_path,
                                                                  text):
    """GCC 13 reads each body of CONTINUED_BODIES as one line and evaluates
    `__has_include` when F is expanded in an `#if`."""
    src = tmp_path / "d" / "case.h"
    src.parent.mkdir()
    src.write_text(text + "#if F(a, )\n#error rcb_evaluated\n#endif\n")
    (tmp_path / "outside.h").write_text("\n")
    runs = [(GCC, "c", std) for std in C_STDS] + [(GXX, "c++", "-std=c++17")]
    assert any("rcb_evaluated" in subprocess.run(
        [compiler, "-x", lang, std, "-E", str(src)], capture_output=True,
        text=True).stderr for compiler, lang, std in runs)


def test_python_word_characters_outside_the_identifier_ranges():
    """Of the 137,873 characters outside ASCII that Python's `\\w` matches,
    U+2E2F is the only one outside cp.EXTENDED_RANGES: a check on `\\w` in
    place of the ranges would join it to a name."""
    ranges = cp.EXTENDED_RANGES
    word = [c for c in range(0x80, 0x110000)
            if re.match(r"\w", chr(c))]
    assert len(word) == 137873
    assert [c for c in word
            if not any(lo <= c <= hi for lo, hi in ranges)] == [0x2E2F]


def range_edges():
    """The first and last character of each range of cp.EXTENDED_RANGES and
    the characters next to them outside it, without surrogates and ASCII:
    148 characters, 77 of them inside, as {character: in a range}."""
    edges = set()
    for lo, hi in cp.EXTENDED_RANGES:
        edges |= {lo - 1, lo, hi, hi + 1}
    return {c: any(lo <= c <= hi for lo, hi in cp.EXTENDED_RANGES)
            for c in sorted(edges)
            if 0x80 <= c <= 0x10FFFF and not 0xD800 <= c <= 0xDFFF}


def test_the_range_edges_hold_both_sides():
    edges = range_edges()
    assert len(edges) == 148
    assert sum(edges.values()) == 77


@needs_gcc
@pytest.mark.parametrize("compiler, lang, std", [
    ("gcc", "c", "-std=c11"), ("gcc", "c", "-std=gnu2x"),
    ("g++", "c++", "-std=c++17"), ("g++", "c++", "-std=gnu++23")])
def test_gcc_takes_the_range_edges_into_an_identifier(tmp_path, compiler,
                                                      lang, std):
    """GCC accepts `a<ch>` as an identifier without a diagnostic exactly for
    the characters of cp.EXTENDED_RANGES, at each range edge."""
    edges = range_edges()
    src = tmp_path / "edges.h"
    src.write_text("".join("#ifdef a%s\n#endif\n" % chr(c) for c in edges),
                   encoding="utf-8")
    run = subprocess.run(
        [GCC if lang == "c" else GXX, "-x", lang, std, "-E",
         "-Wno-normalized", "-Wno-bidi-chars", str(src)],
        capture_output=True, text=True, errors="replace")
    flagged = {int(n) for n in re.findall(r"edges\.h:(\d+):", run.stderr)}
    accepted = {c: 2 * i + 1 not in flagged for i, c in enumerate(edges)}
    assert accepted == edges


def test_a_has_include_alias_fails_the_module(tmp_path):
    m = module(tmp_path)
    c = m / "wire.c"
    c.write_text(c.read_text() + "#define PROBE __has_include\n"
                 '#if PROBE("../../proj/priv.h")\nint wire_probe;\n#endif\n')
    assert cp.include_problems(m) == [
        "wire/wire.c includes __has_include: an include the check cannot "
        "resolve (a macro or __has_include)"]


def include_next_problems(m, rel, line):
    f = m / rel
    f.write_text(line + "\n" + f.read_text())
    return cp.include_problems(m)


@pytest.mark.parametrize("rel, line", [
    ("wire.c", '#include_next "wire.h"'),
    ("wire.c", "#include_next <wire.h>"),
    ("include/wire.h", '#include_next "wire.h"'),
    ("wire.c", "#include_next <hardware/pio.h>"),
    ("wire.c", "#include_next <unistd.h>"),
    ("wire.c", '%:include_next "../x.h"'),
    ("rp2350/wire_pin.c", '#include_next "wire_pin.h"'),
    ("rp2350/wire_pin.c", '#include_next "wire.pio.h"'),
    ("rp2350/wire_pin.c", '#include_next "wire.h"')])
def test_include_next_of_a_module_header_fails(tmp_path, rel, line):
    """`#include_next` searches the include path after the directory the
    file was found in: it reaches the next file of that name, the
    consuming project's or a system header, never the module's own."""
    m = module(tmp_path)
    name = line.split(None, 1)[1]
    assert include_next_problems(m, rel, line) == [
        "wire/%s has #include_next %s: it includes the next file of that "
        "name on the search path, outside the module" % (rel, name)]


@pytest.mark.parametrize("rel, line", [
    ("wire.c", "#include_next <stdint.h>"),
    ("wire.c", '#include_next "stddef.h"'),
    ("rp2350/wire_pin.c", "#include_next <hardware/pio.h>"),
    ("rp2350/wire_pin.c", "#include_next <pico.h>")])
def test_include_next_of_a_standard_or_sdk_header_passes(tmp_path, rel,
                                                         line):
    m = module(tmp_path)
    assert include_next_problems(m, rel, line) == []


def test_include_next_of_an_sdk_header_in_a_drivers_header_fails(tmp_path):
    m = module(tmp_path)
    assert include_next_problems(
        m, "rp2350/wire_pin.h", "#include_next <hardware/pio.h>") == [
        "wire/rp2350/wire_pin.h includes hardware/pio.h: a driver's header "
        "is read without the pico-sdk"]


def test_a_macro_include_next_is_unresolved():
    assert cp.includes("#include_next H\n") == [("?", "H")]
    assert cp.includes('#include_next "a.h" x\n') == [("?", '"a.h" x')]


def within(seconds, fn, *args):
    """fn(*args), or a failure after @p seconds: a scan that loops forever
    fails the case instead of hanging the suite."""
    out = []

    def run():
        try:
            out.append((True, fn(*args)))
        except Exception as e:  # handed to the test thread below
            out.append((False, e))

    t = threading.Thread(target=run, daemon=True)
    t.start()
    t.join(seconds)
    assert out, "no result after %s s" % seconds
    ok, value = out[0]
    if not ok:
        raise value
    return value


@pytest.mark.parametrize("text, found", [
    # A `/*` without its `*/` runs to the end of the file.
    ('.program p\n/* never closed\n% c-sdk {\n#include "../x.h"\n%}\n', []),
    # A `;` comment and a .lang_opt line at the end, without a newline.
    (".program p\n; comment", []),
    (".program p\n.lang_opt c-sdk x", []),
    # A block without `%}` and without a final newline runs to the end.
    ('.program p\n% c-sdk {\n#include "../x.h"', [('"', "../x.h")]),
    # The file ends in a `;` after the block.
    ('.program p\n% c-sdk {\n#include "../x.h"\n%}\n;', [('"', "../x.h")]),
    # A `%` that opens no block.
    (".program p\n%x\n", [])])
def test_a_pio_file_is_read_to_its_end(text, found):
    assert within(5, cp.includes, text, True) == found


@pytest.mark.parametrize("text, found", [
    # `/**/` is a whole comment.
    ('.program p\n/**/\n% c-sdk {\n#include "../x.h"\n%}\n', True),
    # A `*` alone closes nothing: the block start is inside the comment.
    ('.program p\n/* a * b % c-sdk {\n#include "../x.h"\n*/\n%}\n', False),
    # A block name takes no `%`: the block starts at the second `%`.
    ('.program p\n% c-sdk % {\n#include "../x.h"\n%}\n', True),
    # A block start needs a name character between `%` and `{`.
    ('.program p\n%{\n#include "../x.h"\n%}\n', False)])
def test_pio_comments_and_block_starts(text, found):
    assert within(5, cp.includes, text, True) == (
        [('"', "../x.h")] if found else [])


@pytest.mark.parametrize("text, found", [
    # An empty comment ends at its own `*/`.
    ('/**/ int a;\n#include "../x.h"\n/* x */\n', True),
    # A division sign opens no comment.
    ("#if 2 / __has_include(<a.h>)\n#endif\n", "?"),
    # A `+` after a digit is no exponent sign; after P it is.
    ("#if 1+__has_include(<a.h>)\n#endif\n", "?"),
    ("#if 0x1P+__has_include(<a.h>)\n#endif\n", False),
    # An empty literal is whole, and two literals side by side are two.
    ('x = ""; /* s\n#include "../x.h"\n*/\n', False),
    ('x = "a""/*";\n#include "../x.h"\n/* e */\n', True),
    ("x = ''; /* s\n#include \"../x.h\"\n*/\n", False),
    ("x = 'a''/*';\n#include \"../x.h\"\n/* e */\n", True),
    # CRLF, then an LF: the continuation joins one line, not two.
    ('#define A \\\r\n\n#include "../x.h"\n', True),
    # A raw string after three continuations is matched where it stands.
    ('a\\\nb\\\nc\\\nd R"(x)"\n/* c\n#include "../x.h"\n*/\n', False)])
def test_comment_literal_and_number_tokens(text, found):
    want = {True: [('"', "../x.h")], False: [],
            "?": [("?", "__has_include")]}[found]
    assert cp.includes(text) == want


@pytest.mark.parametrize("readme, found", [
    # Four backticks open a fence that three do not close.
    ("````\n# Interface\n````\n", False),
    ("````\ncode\n   ```\n# Interface\n", False),
    # A fence closes on an indented fence of the same length.
    ("```\ncode\n ```\n# Interface\n", True),
    # A backtick in the info string of an indented fence: no fence.
    ("   ``` a`b\n# Interface\n", True),
    # `<!-` and `<!---->`: no comment, and an empty comment.
    ("<!- x\n# Interface\n", True),
    ("<!---->\n# Interface\n", True),
    # `->` does not end a comment.
    ("<!-- a ->\n# Interface\n-->\n", False)])
def test_fences_and_comments_end_where_commonmark_ends_them(tmp_path, readme,
                                                            found):
    m = module(tmp_path)
    (m / "README.md").write_text("# Title\n\n" + readme)
    assert cp.readme_problems(m) == (
        [] if found else ['wire/README.md has no heading with "Interface"'])


@pytest.mark.parametrize("sentence, found", [
    ("no pin  driver", True), ("no pin\n  driver", True),
    ("nopin driver", False), ("no pindriver", False)])
def test_no_pin_driver_takes_white_space_between_its_words(tmp_path,
                                                           sentence, found):
    m = module(tmp_path, driver=False)
    (m / "README.md").write_text("# Interface\nThe module has %s.\n"
                                 % sentence)
    assert (cp.readme_problems(m) == []) == found, sentence


@pytest.mark.parametrize("text, brand", [
    ("The kst-tool is supported.\n", "kst"), ("The --kst tool.\n", "kst"),
    ("KST-compatible.\n", "KST"), ("docs/futaba-notes\n", "futaba")])
def test_a_brand_next_to_a_hyphen_is_a_brand(tmp_path, text, brand):
    m = module(tmp_path)
    (m / "README.md").write_text("# Interface\n" + text)
    assert cp.readme_problems(m) == [
        'wire/README.md names %s and has no heading with "Trademark"' % brand]
