"""The KST programmer overlay's texts against the host test that holds
their bytes: every text shared/ui/kst_panel*.c names is a KP_ or PC_ text or
in k_routed of test/host/test_kst_panel_texts.c, so that test measures every
text the overlay copies into a KST_PANEL_LINE buffer.

SPDX-License-Identifier: MIT
"""

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
UI = REPO / "shared" / "ui"
HOST_TEST = REPO / "test" / "host" / "test_kst_panel_texts.c"

# The prefixes the host test takes whole.
PREFIXES = ("KP_", "PC_")


def named_texts(text):
    """The text IDs a C source names, without their TX_ prefix.

    Args:
        text: The source.

    Returns:
        The set of IDs in TR(ID) and TX_ID, TX_COUNT left out.
    """
    ids = set(re.findall(r"\bTR\((\w+)\)", text))
    ids |= set(re.findall(r"\bTX_(\w+)", text))
    ids.discard("COUNT")
    return ids


def routed_list(text):
    """The IDs of the host test's k_routed[] array.

    Args:
        text: The source of test_kst_panel_texts.c.

    Returns:
        The IDs without their TX_ prefix.

    Raises:
        AssertionError: The file has no k_routed[] array.
    """
    block = re.search(r"k_routed\[\]\s*=\s*\{(.*?)\};", text, re.S)
    assert block, "no k_routed[] in test_kst_panel_texts.c"
    return set(re.findall(r"\bTX_(\w+)", block.group(1)))


def unmeasured(named, routed):
    """The IDs the host test does not measure.

    Args:
        named: The IDs the overlay's sources name.
        routed: The host test's k_routed[] list.

    Returns:
        The IDs of named neither prefixed nor listed, sorted.
    """
    return sorted(i for i in named
                  if not i.startswith(PREFIXES) and i not in routed)


def test_tr_and_tx_names_are_read():
    src = "x = TR(KP_A); y = TX_SV_HV_1; z = TX_COUNT; TR(CANCEL)"
    assert named_texts(src) == {"KP_A", "SV_HV_1", "CANCEL"}


def test_a_text_neither_prefixed_nor_listed_is_reported():
    assert unmeasured({"KP_A", "PC_B", "ON", "SUP_X"}, {"ON"}) == ["SUP_X"]


def test_the_list_is_read_from_its_array():
    src = "static const ui_text_id_t k_routed[] = {\n TX_ON, TX_OFF,\n};"
    assert routed_list(src) == {"ON", "OFF"}


def test_every_text_the_overlay_names_is_measured():
    named = set()
    for path in sorted(UI.glob("kst_panel*.[ch]")):
        named |= named_texts(path.read_text(encoding="utf-8"))
    routed = routed_list(HOST_TEST.read_text(encoding="utf-8"))
    assert named, "no text named in shared/ui/kst_panel*"
    assert unmeasured(named, routed) == [], (
        "add these to k_routed[] of test/host/test_kst_panel_texts.c")


def test_every_listed_text_is_still_named():
    named = set()
    for path in sorted(UI.glob("kst_panel*.[ch]")):
        named |= named_texts(path.read_text(encoding="utf-8"))
    routed = routed_list(HOST_TEST.read_text(encoding="utf-8"))
    assert sorted(routed - named) == []
