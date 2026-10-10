#!/usr/bin/env python3
"""Hold every module under protocols/ to the rule that lets it leave.

A module under protocols/ is built in other pico-sdk projects, C and C++,
without the rest of this repository (CONTRIBUTING.md, "Where code goes").
This tool reads each module and fails when one of these does not hold:

- the module has a README.md, a CMakeLists.txt, a header under include/
  and a C file beside them;
- a core file, which is every C file, header and PIO (programmable
  input/output) program outside the module's rp2350/ folder, includes only
  C standard headers and headers of its own core;
- a file under rp2350/ includes only those, the module's own driver
  headers, the header pioasm generates from a .pio file of the module, and
  pico-sdk headers: `pico.h`, `pico/...` and `hardware/...`;
- a header under rp2350/ includes no pico-sdk header, so the driver's
  interface is read without the SDK (software development kit);
- every header under include/ and rp2350/ carries `extern "C"` and
  compiles by itself as C11 and as C++17, with `-Wall -Wextra -Werror`;
- the module's CMake files name no ESP-IDF (Espressif Internet-of-Things
  Development Framework) requirement and no `rcbench_` library of another
  module.

    python3 tools/check_protocols.py            # the modules, then the check
    python3 tools/check_protocols.py --check    # the check alone

Both forms exit with 1 when a rule is broken.  CI (continuous integration)
runs `--check`.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
PROTOCOLS = REPO / "protocols"

# The folder of a module that holds its RP2350 pin driver.
DRIVER = "rp2350"

# The headers of ISO C11, clause 7.1.2.
C_STANDARD = frozenset("""
    assert.h complex.h ctype.h errno.h fenv.h float.h inttypes.h iso646.h
    limits.h locale.h math.h setjmp.h signal.h stdalign.h stdarg.h
    stdatomic.h stdbool.h stddef.h stdint.h stdio.h stdlib.h stdnoreturn.h
    string.h tgmath.h threads.h time.h uchar.h wchar.h wctype.h
""".split())

# What a pin driver may include from the pico-sdk.
SDK_RE = re.compile(r"^(pico\.h|pico/.+\.h|hardware/.+\.h)$")

INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]',
                        re.M)
C_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
SOURCE_SUFFIXES = (".c", ".h", ".pio")

CMAKE_COMMENT_RE = re.compile(r"#[^\n]*")
REQUIRES_RE = re.compile(r"\b(?:PRIV_)?REQUIRES\b")
LIBRARY_RE = re.compile(r"\brcbench_\w+")

WARNINGS = ("-Wall", "-Wextra", "-Werror")
LANGUAGES = (("C11", "c", "-std=c11"), ("C++17", "c++", "-std=c++17"))


def includes(text: str) -> list[tuple[str, str]]:
    """(form, name) for every #include of @p text outside a C comment, in
    order: form is `<` or `"`."""
    blanked = C_COMMENT_RE.sub(
        lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)
    return INCLUDE_RE.findall(blanked)


def modules(root: pathlib.Path = PROTOCOLS) -> list[pathlib.Path]:
    if not root.is_dir():
        return []
    return sorted(p for p in root.iterdir() if p.is_dir())


def is_driver(path: pathlib.Path, module: pathlib.Path) -> bool:
    return DRIVER in path.relative_to(module).parts


def sources(module: pathlib.Path) -> list[pathlib.Path]:
    """Every C file, header and PIO program of @p module."""
    return sorted(p for p in module.rglob("*")
                  if p.is_file() and p.suffix in SOURCE_SUFFIXES)


def public_headers(module: pathlib.Path) -> list[pathlib.Path]:
    """The headers another project includes: the core's under include/ and
    the driver's under rp2350/."""
    return sorted([*(module / "include").glob("*.h"),
                   *(module / DRIVER).glob("*.h")])


def include_problems(module: pathlib.Path) -> list[str]:
    """What @p module includes that it may not, one line per include."""
    files = sources(module)
    core = {p.name for p in files
            if p.suffix == ".h" and not is_driver(p, module)}
    driver = {p.name for p in files
              if p.suffix == ".h" and is_driver(p, module)}
    generated = {p.name + ".h" for p in files if p.suffix == ".pio"}
    problems = []
    for path in files:
        rel = path.relative_to(module.parent).as_posix()
        in_driver = is_driver(path, module)
        for form, name in includes(path.read_text(encoding="utf-8")):
            if name in C_STANDARD:
                continue
            if form == '"' and name in core:
                continue
            sdk = SDK_RE.match(name) is not None
            if in_driver and path.suffix == ".h" and sdk:
                problems.append(
                    f"{rel} includes {name}: a driver's header is read "
                    "without the pico-sdk")
                continue
            if in_driver and (sdk or (form == '"' and name in driver)
                              or (form == '"' and name in generated)):
                continue
            if not in_driver and form == '"' and name in driver:
                problems.append(
                    f"{rel} includes {name}: a core file does not depend "
                    f"on the pin driver in {DRIVER}/")
                continue
            if not in_driver and sdk:
                problems.append(
                    f"{rel} includes {name}: a pico-sdk header belongs in "
                    f"{DRIVER}/")
                continue
            problems.append(
                f"{rel} includes {name}, which is neither a C standard "
                f"header nor a header of {module.name}/")
    return problems


def layout_problems(module: pathlib.Path) -> list[str]:
    """What @p module lacks of a module's parts."""
    name = module.relative_to(module.parent.parent).as_posix()
    problems = []
    for part in ("README.md", "CMakeLists.txt"):
        if not (module / part).is_file():
            problems.append(f"{name}/ has no {part}")
    if not list((module / "include").glob("*.h")):
        problems.append(f"{name}/ has no header under include/")
    if not [p for p in module.rglob("*.c") if not is_driver(p, module)]:
        problems.append(f"{name}/ has no C file outside {DRIVER}/")
    if (module / DRIVER).is_dir():
        for suffix, what in ((".pio", "PIO program"), (".c", "C file"),
                             (".h", "header")):
            if not list((module / DRIVER).glob("*" + suffix)):
                problems.append(f"{name}/{DRIVER}/ has no {what}")
    return problems


def cmake_problems(module: pathlib.Path) -> list[str]:
    """What the module's CMake files take from outside the module."""
    problems = []
    for path in sorted(module.rglob("CMakeLists.txt")):
        rel = path.relative_to(module.parent).as_posix()
        text = CMAKE_COMMENT_RE.sub("", path.read_text(encoding="utf-8"))
        if REQUIRES_RE.search(text):
            problems.append(f"{rel} carries REQUIRES: a module needs no "
                            "other component")
        own = "rcbench_" + module.name
        for lib in sorted(set(LIBRARY_RE.findall(text))):
            if lib != own and not lib.startswith(own + "_"):
                problems.append(f"{rel} names {lib}, a library of another "
                                "module")
    return problems


def header_problems(module: pathlib.Path, cc: str, cxx: str) -> list[str]:
    """Every public header of @p module that lacks `extern "C"` or does not
    compile by itself, as C and as C++."""
    problems = []
    dirs = [d for d in (module / "include", module / DRIVER) if d.is_dir()]
    flags = [arg for d in dirs for arg in ("-I", str(d))]
    with tempfile.TemporaryDirectory(prefix="rcbench-proto-") as tmp:
        for header in public_headers(module):
            rel = header.relative_to(module.parent).as_posix()
            if 'extern "C"' not in header.read_text(encoding="utf-8"):
                problems.append(f'{rel} has no extern "C" guard')
            # Twice: a header without an include guard fails here.
            unit = '#include "%s"\n#include "%s"\n' % (header.name,
                                                       header.name)
            for language, lang_flag, std in LANGUAGES:
                src = pathlib.Path(tmp) / "unit.txt"
                src.write_text(unit, encoding="utf-8")
                compiler = cc if lang_flag == "c" else cxx
                run = subprocess.run(
                    [compiler, "-x", lang_flag, std, "-fsyntax-only",
                     *WARNINGS, *flags, str(src)],
                    capture_output=True, text=True)
                if run.returncode != 0:
                    first = (run.stderr.strip().splitlines() or ["?"])
                    wanted = [line for line in first if "error" in line]
                    problems.append(
                        f"{rel} does not compile as {language}: "
                        + (wanted or first)[0].replace(tmp + "/", ""))
    return problems


def compilers() -> tuple[str, str]:
    cc = shutil.which("cc") or shutil.which("gcc")
    cxx = shutil.which("c++") or shutil.which("g++")
    if cc is None or cxx is None:
        sys.exit("a C and a C++ compiler are required on PATH")
    return cc, cxx


def check(root: pathlib.Path = PROTOCOLS) -> list[str]:
    """Every broken rule under @p root, one line each."""
    cc, cxx = compilers()
    problems = []
    for module in modules(root):
        problems += layout_problems(module)
        problems += include_problems(module)
        problems += cmake_problems(module)
        problems += header_problems(module, cc, cxx)
    return problems


def describe(root: pathlib.Path = PROTOCOLS) -> str:
    """One line per module: its core files, driver files and headers."""
    lines = []
    for module in modules(root):
        files = sources(module)
        drv = [p for p in files if is_driver(p, module)]
        lines.append("%-12s %2d core files, %d driver files, %d public "
                     "headers" % (module.name + "/", len(files) - len(drv),
                                  len(drv), len(public_headers(module))))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if args not in ([], ["--check"]):
        sys.exit(__doc__)
    found = modules(PROTOCOLS)
    if not found:
        sys.exit(f"no module under {PROTOCOLS}; the check cannot pass")
    if not args:
        print(describe(PROTOCOLS))
        print()
    problems = check(PROTOCOLS)
    for line in problems:
        print(line)
    if problems:
        print(f"\n{len(problems)} broken rule(s) under protocols/",
              file=sys.stderr)
        return 1
    print(f"{len(found)} modules under protocols/ hold the rule")
    return 0


if __name__ == "__main__":
    sys.exit(main())
