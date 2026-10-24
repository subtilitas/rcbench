#!/usr/bin/env python3
"""Hold a panel image to its RCBENCH_PERF_LOG setting.

A panel built with RCBENCH_PERF_LOG ON carries the console's frame, stack
and heap lines: the start-up probe, the stack scan and the code behind
them.  Release images are built with it OFF.  The setting is one CMake
option, and a release build that stops passing it still builds, so the
image is read for the strings and the symbol the code leaves behind.

    tools/check_perf_log.py off ELF     fail when the image holds any of them
    tools/check_perf_log.py on ELF      fail when the image lacks any of them

ELF is the panel's linked file, firmware/panel/build/rcbench-panel.elf.

SPDX-License-Identifier: MIT
"""

import sys
from pathlib import Path

# The text of the start-up probe's line, the text of the stack scan's line
# and the scan function's symbol, all compiled with RCBENCH_PERF_LOG only
# (firmware/panel/main/panel_perf.c).
MARKS = (b"PSRAM probe:", b"stack scan:", b"scan_stacks")


def found(image: bytes) -> list[bytes]:
    """Name the marks an image holds.

    Args:
        image: The bytes of the panel's ELF file.

    Returns:
        The entries of MARKS found in the image, in MARKS order.
    """
    return [m for m in MARKS if m in image]


def problems(image: bytes, want_on: bool) -> list[str]:
    """Name what is wrong with an image against its setting.

    Args:
        image: The bytes of the panel's ELF file.
        want_on: True for a build with RCBENCH_PERF_LOG ON, False for OFF.

    Returns:
        One line per mark that is present when it should not be or missing
        when it should be; empty when the image matches the setting.
    """
    have = found(image)
    if want_on:
        return [f"the image lacks {m.decode()!r}, which a build with "
                "RCBENCH_PERF_LOG=ON holds"
                for m in MARKS if m not in have]
    return [f"the image holds {m.decode()!r}, which a build with "
            "RCBENCH_PERF_LOG=OFF leaves out" for m in have]


def main(argv: list[str]) -> int:
    """Check a panel ELF file against a setting.

    Args:
        argv: Program name, then "on" or "off", then the path of the ELF.

    Returns:
        0 when the image matches the setting, 1 when it does not or the
        file is missing, 2 on a wrong command line.
    """
    if len(argv) != 3 or argv[1] not in ("on", "off"):
        print("usage: check_perf_log.py on|off ELF", file=sys.stderr)
        return 2
    path = Path(argv[2])
    if not path.is_file():
        print(f"check_perf_log: {path} is not there; build the panel first",
              file=sys.stderr)
        return 1
    errors = problems(path.read_bytes(), argv[1] == "on")
    for e in errors:
        print(f"check_perf_log: {e}", file=sys.stderr)
    if not errors:
        print(f"check_perf_log: {path.name} matches RCBENCH_PERF_LOG="
              f"{argv[1].upper()}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
