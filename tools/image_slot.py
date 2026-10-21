#!/usr/bin/env python3
"""Hold the panel's application image to its update slot.

An update from the card or over Wi-Fi writes the application slot that is
not running.  An image larger than the smaller of the two slots builds, and
then cannot be installed from the one that is running.  The build's own size
check names one partition; this tool reads the partition table the build
wrote and holds the image to the smaller of the two OTA (over-the-air)
slots, and prints the room left.

    tools/image_slot.py [BUILD]     check; BUILD defaults to
                                    firmware/panel/build

It fails when the build has no image or no partition table, when the table
has fewer than two OTA application slots, when the slots differ in size, and
when the image is larger than a slot.

SPDX-License-Identifier: MIT
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "firmware" / "panel" / "build"
IMAGE = "rcbench-panel.bin"
TABLE = Path("partition_table") / "partition-table.bin"

ENTRY = struct.Struct("<HBBII16sI")     # magic, type, subtype, offset, size
MAGIC = 0x50AA
TYPE_APP = 0
OTA_FIRST, OTA_LAST = 0x10, 0x1F        # ota_0 to ota_15 (ESP-IDF)


def ota_slots(table: bytes) -> dict[str, int]:
    """{name: size in bytes} of every OTA application partition in a
    binary partition table."""
    slots = {}
    for at in range(0, len(table) - ENTRY.size + 1, ENTRY.size):
        magic, kind, sub, _off, size, name, _flags = ENTRY.unpack_from(
            table, at)
        if magic != MAGIC:
            break                       # the checksum entry or the blank
        if kind == TYPE_APP and OTA_FIRST <= sub <= OTA_LAST:
            slots[name.split(b"\0")[0].decode("ascii", "replace")] = size
    return slots


def problems(image: int, slots: dict[str, int]) -> list[str]:
    """What is wrong with an image of `image` bytes and these slots."""
    if len(slots) < 2:
        return [f"the partition table has {len(slots)} OTA application "
                "slot(s); an update needs two"]
    out = []
    if len(set(slots.values())) != 1:
        out.append("the OTA slots differ in size: " + ", ".join(
            f"{n} {s:,} bytes" for n, s in slots.items()))
    smallest = min(slots.values())
    if image > smallest:
        out.append(f"the image is {image:,} bytes; the smallest OTA slot "
                   f"holds {smallest:,}")
    return out


def main(argv: list[str]) -> int:
    build = Path(argv[1]) if len(argv) > 1 else BUILD
    image, table = build / IMAGE, build / TABLE
    for f in (image, table):
        if not f.is_file():
            print(f"image_slot: {f} is not there; build the panel first",
                  file=sys.stderr)
            return 1
    size = image.stat().st_size
    slots = ota_slots(table.read_bytes())
    if slots:
        smallest = min(slots.values())
        free = smallest - size
        print(f"image {size:,} bytes; smallest OTA slot {smallest:,} bytes; "
              f"{free:,} bytes ({100 * free / smallest:.1f}%) free")
    errors = problems(size, slots)
    for e in errors:
        print(f"image_slot: {e}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
