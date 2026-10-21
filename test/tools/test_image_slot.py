"""image_slot.py: the OTA slots a partition table holds and the image
against them.

SPDX-License-Identifier: MIT
"""

import struct

import image_slot as im

SLOT = 0x3E0000


def entry(kind, sub, off, size, name):
    """Pack one 32-byte partition table entry.

    Args:
        kind: Partition type.
        sub: Partition subtype.
        off: Offset in flash.
        size: Size in bytes.
        name: Partition name.

    Returns:
        The entry's bytes.
    """
    return im.ENTRY.pack(im.MAGIC, kind, sub, off, size,
                         name.encode().ljust(16, b"\0"), 0)


def table(*entries):
    """Join entries into a table that ends at a checksum entry.

    Args:
        *entries: Packed entries from entry().

    Returns:
        The bytes of a partition table.
    """
    # The checksum entry ends a table: not the partition magic.
    return b"".join(entries) + b"\xeb\xeb" + b"\0" * 30


TWO = table(entry(1, 2, 0x9000, 0x6000, "nvs"),
            entry(0, 0x10, 0x20000, SLOT, "ota_0"),
            entry(0, 0x11, 0x400000, SLOT, "ota_1"))


def test_the_ota_slots_are_read_by_type_and_subtype():
    assert im.ota_slots(TWO) == {"ota_0": SLOT, "ota_1": SLOT}


def test_an_application_that_is_not_an_ota_slot_is_not_one():
    t = table(entry(0, 0x00, 0x10000, SLOT, "factory"))
    assert im.ota_slots(t) == {}


def test_a_table_that_ends_at_the_checksum_is_read_to_there():
    t = TWO + entry(0, 0x12, 0x800000, SLOT, "ota_2")
    assert list(im.ota_slots(t)) == ["ota_0", "ota_1"]


def test_an_image_that_fits_passes_and_one_that_does_not_fails():
    slots = im.ota_slots(TWO)
    assert im.problems(SLOT, slots) == []               # fills the slot
    assert im.problems(SLOT + 1, slots) == [
        f"the image is {SLOT + 1:,} bytes; the smallest OTA slot holds "
        f"{SLOT:,}"]
    assert im.problems(0, slots) == []


def test_one_slot_or_none_fails():
    assert "has 1 OTA" in im.problems(1, {"ota_0": SLOT})[0]
    assert "has 0 OTA" in im.problems(1, {})[0]


def test_slots_of_two_sizes_fail_and_the_smaller_bounds_the_image():
    out = im.problems(SLOT, {"ota_0": SLOT, "ota_1": SLOT - 16})
    assert out[0].startswith("the OTA slots differ in size")
    assert "smallest OTA slot holds" in out[1]


def test_main_reports_a_missing_build(tmp_path, capsys):
    assert im.main(["image_slot.py", str(tmp_path)]) == 1
    assert "build the panel first" in capsys.readouterr().err


def test_main_checks_a_built_tree(tmp_path, capsys):
    (tmp_path / "partition_table").mkdir()
    (tmp_path / im.TABLE).write_bytes(TWO)
    (tmp_path / im.IMAGE).write_bytes(b"\0" * 1000)
    assert im.main(["image_slot.py", str(tmp_path)]) == 0
    assert "image 1,000 bytes" in capsys.readouterr().out
    (tmp_path / im.IMAGE).write_bytes(b"\0" * (SLOT + 1))
    assert im.main(["image_slot.py", str(tmp_path)]) == 1


def test_the_entry_layout_is_32_bytes():
    assert im.ENTRY.size == 32 == struct.calcsize("<HBBII16sI")
