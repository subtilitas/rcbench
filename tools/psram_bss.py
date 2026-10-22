#!/usr/bin/env python3
"""Hold the panel's PSRAM .bss placement to the objects the fragment names.

firmware/panel/main/psram_bss.lf maps the .bss of named objects to
extern_ram, the .ext_ram.bss output section in PSRAM (pseudo-static
random-access memory).  ldgen skips an entry whose object is not in the
archive, and a mapping whose scheme no fragment of the build defines,
without a message; the .bss then stays in internal RAM and the build
passes.  This tool reads the linker map the build wrote and checks the
outcome.

    tools/psram_bss.py [BUILD]          check; BUILD defaults to
                                        firmware/panel/build
    tools/psram_bss.py --list N [BUILD] also list every object with more
                                        than N bytes of .bss in internal RAM

It fails when a line in the entries of a mapping is not
``<object> (rcbench_extram_bss)``, when a named object has no .bss in
.ext_ram.bss, when a named object keeps .bss in internal RAM, and when the
map or the fragment is not there.  .sbss (the RISC-V small data, 8 bytes
and less per variable) is not .bss to ldgen's bss section and stays
internal.

SPDX-License-Identifier: MIT
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "firmware" / "panel" / "build"
MAP = "rcbench-panel.map"
FRAGMENT = ROOT / "firmware" / "panel" / "main" / "psram_bss.lf"
SCHEME = "rcbench_extram_bss"
EXT = ".ext_ram.bss"

_HEADER = re.compile(r"^\[(\w+):(\w+)\]")
_ENTRY = re.compile(r"^\s+(\w+)\s+\((\w+)\)\s*$")
_ROW = re.compile(r"^ (\S+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)$")
_NAME = re.compile(r"^ (\S+)$")
_TAIL = re.compile(r"^\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)$")
_MEMBER = re.compile(r"([^/()]+\.a)\(([^)]+?)(?:\.c)?\.(?:obj|o)\)$")


def named(fragment: str) -> tuple[list[tuple[str, str]], list[str]]:
    """Read the objects a linker fragment maps with SCHEME.

    Args:
        fragment: The text of a .lf file.

    Returns:
        (names, strays).  names: (archive, object) per entry
        ``object (SCHEME)``, in file order; the archive as the mapping's
        ``archive:`` line gives it.  strays: one line per other line in the
        entries of a ``[mapping:...]`` block, an entry in another scheme,
        for a symbol or for the whole archive among them, which this check
        would not see.
    """
    names, strays = [], []
    archive, in_mapping, in_entries = None, False, False
    for number, raw in enumerate(fragment.splitlines(), 1):
        line = raw.split("#", 1)[0].rstrip()
        header = _HEADER.match(line)
        if header:
            archive, in_entries = None, False
            in_mapping = header.group(1) == "mapping"
        elif not in_mapping or not line:
            continue
        elif line.startswith("archive:"):
            archive = line.split(":", 1)[1].strip()
        elif line.startswith("entries:"):
            in_entries = True
        else:
            m = _ENTRY.match(line)
            if in_entries and archive and m and m.group(2) == SCHEME:
                names.append((archive, m.group(1)))
            else:
                strays.append(f"line {number}: '{line.strip()}' is not "
                              f"'<object> ({SCHEME})'")
    return names, strays


def bss_by_object(linker_map: str) -> dict[str, dict[tuple[str, str], int]]:
    """Sum the .bss and COMMON input sections per output section and object.

    Args:
        linker_map: The text of a GNU ld map file.

    Returns:
        {output section: {(archive, object): bytes}}, read from the part
        after "Linker script and memory map" only; the discarded sections
        before it are not placed.  An object outside an archive is not
        counted.
    """
    out = defaultdict(lambda: defaultdict(int))
    lines = linker_map.splitlines()
    try:
        start = lines.index("Linker script and memory map")
    except ValueError:
        return {}
    section, pending = None, None
    for line in lines[start + 1:]:
        if line and not line[0].isspace():
            section = line.split()[0] if line.startswith(".") else None
            pending = None
            continue
        m = _ROW.match(line)
        if m:
            name, size, obj = m.groups()
        else:
            m = _NAME.match(line)
            if m:
                pending = m.group(1)
                continue
            m = _TAIL.match(line)
            if not (m and pending):
                pending = None
                continue
            name, (size, obj) = pending, m.groups()
        pending = None
        member = _MEMBER.search(obj)
        if section and member and (name == "COMMON" or name == ".bss"
                                   or name.startswith(".bss.")):
            out[section][member.groups()] += int(size, 16)
    return {k: dict(v) for k, v in out.items()}


def problems(names: list[tuple[str, str]],
             placed: dict[str, dict[tuple[str, str], int]],
             strays: list[str] | None = None) -> list[str]:
    """Name each object the fragment names whose .bss is not all in PSRAM.

    Args:
        names: (archive, object) pairs from named().
        placed: The result of bss_by_object().
        strays: The stray mapping lines from named().

    Returns:
        One line per problem; empty when the fragment has no stray mapping
        line and every named object has .bss in .ext_ram.bss and none
        anywhere else.
    """
    out = [f"psram_bss.lf {s}" for s in strays or []]
    if not names:
        out.append(f"the fragment maps no object with ({SCHEME})")
    ext = placed.get(EXT, {})
    for key in names:
        label = f"{key[0]}({key[1]})"
        if not ext.get(key):
            out.append(f"{label} has no .bss in {EXT}: the object is gone, "
                       "holds no .bss, or the mapping did not apply")
        for section, objs in sorted(placed.items()):
            if section != EXT and objs.get(key):
                out.append(f"{label} keeps {objs[key]:,} bytes of .bss "
                           f"in {section}")
    return out


def main(argv: list[str]) -> int:
    """Check a panel build's PSRAM .bss and print the bytes it holds.

    Args:
        argv: Program name, then optionally ``--list N``, then optionally
            the build directory.

    Returns:
        0 when every named object's .bss is in PSRAM, 1 otherwise.
    """
    args, over = argv[1:], None
    if args[:1] == ["--list"]:
        if len(args) < 2 or not args[1].isdigit():
            print("psram_bss: --list takes a byte count", file=sys.stderr)
            return 1
        over, args = int(args[1]), args[2:]
    build = Path(args[0]) if args else BUILD
    for f in (build / MAP, FRAGMENT):
        if not f.is_file():
            print(f"psram_bss: {f} is not there; build the panel first",
                  file=sys.stderr)
            return 1
    names, strays = named(FRAGMENT.read_text())
    placed = bss_by_object((build / MAP).read_text(errors="replace"))
    ext = placed.get(EXT, {})
    moved = sum(ext.get(k, 0) for k in names)
    print(f"{len(names)} named objects, {moved:,} bytes of .bss in {EXT}; "
          f"{sum(ext.values()):,} bytes in {EXT} in all")
    if over is not None:
        for section, objs in sorted(placed.items()):
            if section == EXT:
                continue
            for (a, o), size in sorted(objs.items(), key=lambda i: -i[1]):
                if size > over:
                    print(f"{section} {size:8,} {a}({o})")
    errors = problems(names, placed, strays)
    for e in errors:
        print(f"psram_bss: {e}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
