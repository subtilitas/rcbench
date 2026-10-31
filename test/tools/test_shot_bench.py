"""Hold panel_shot_frame() to every reading the screenshot gate takes.

SPDX-License-Identifier: MIT
"""

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parent.parent.parent


def fields():
    """Return the fields of shot_bench_t, from the header."""
    text = (REPO / "shared/ui/include/shot_gate.h").read_text(encoding="utf-8")
    start = text.index("} shot_facts_t;")
    body = text[start:text.index("} shot_bench_t;")]
    return re.findall(r"^\s*(?:bool|uint16_t)\s+(\w+);", body, re.M)


def test_the_panel_fills_every_reading_of_the_gate():
    """A reading left out would read as idle."""
    names = fields()
    assert len(names) == 19
    text = (REPO / "firmware/panel/main/panel_shot.c").read_text(
        encoding="utf-8")
    body = text[text.index("const shot_bench_t b = {"):]
    body = body[:body.index("};")]
    missing = [n for n in names if f".{n} =" not in body]
    assert missing == []
