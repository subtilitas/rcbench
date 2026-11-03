#!/usr/bin/env python3
"""Hold a panel image to its RCBENCH_PANEL_CONSOLE setting.

A panel built with RCBENCH_PANEL_CONSOLE ON relays its serial port to the
IOmcu's bench console.  Every released image is built with it OFF.  The
setting is one CMake option, so the image is read for what the code leaves
behind.

    tools/check_panel_console.py off ELF   fail when the image holds any
    tools/check_panel_console.py on ELF    fail when the image lacks any

ELF is the panel's linked file, firmware/panel/build/rcbench-panel.elf.

SPDX-License-Identifier: MIT
"""

import sys
from pathlib import Path

# The task's and the relay's symbols and the line at a link edge, compiled in
# with RCBENCH_PANEL_CONSOLE only (firmware/panel/main/panel_console.c).  The
# empty inline functions of a build without it leave their own names in the
# debug information, so panel_console_pass is no mark.
MARKS = (b"console_task", b"console_relay_next", b"console: link %s")


def problems(image: bytes, want_on: bool) -> list[str]:
    """Name what is wrong with an image against its setting.

    Args:
        image: The bytes of the panel's ELF file.
        want_on: True for a build with the option ON, False for OFF.

    Returns:
        One line per mark present when it should not be or missing when it
        should be; empty when the image matches the setting.
    """
    have = [m for m in MARKS if m in image]
    if want_on:
        return [f"missing {m.decode()}" for m in MARKS if m not in have]
    return [f"holds {m.decode()}" for m in have]


def main(argv: list[str]) -> int:
    """Check one image.

    Args:
        argv: The program's name, on or off, and the ELF file's path.

    Returns:
        0 when the image matches, 1 otherwise, 2 for a wrong call.
    """
    if len(argv) != 3 or argv[1] not in ("on", "off"):
        print(__doc__, file=sys.stderr)
        return 2
    found = problems(Path(argv[2]).read_bytes(), argv[1] == "on")
    for line in found:
        print(f"{argv[2]}: RCBENCH_PANEL_CONSOLE {argv[1]}: {line}",
              file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
