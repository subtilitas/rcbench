"""stack_check.py: the panel's decoder, the task table in the docs, and
the build trees it does not read.

SPDX-License-Identifier: MIT
"""

import re

import pytest

import stack_check as sc


def test_the_panel_decoder_reads_an_entry():
    # entry a1, 48
    assert sc.decode(0x42000000, 0x006136, 3) == ("entry", 48)


EN = sc.PERFORMANCE[0][1]
TABLE = EN + """
| --- | --- | ---: | ---: | ---: |
| `main` | `main_task`, which runs the UI | 8,192 | 3,984 | 3,184 |
| `knob` | `knob_task` | 3,072 | 1,360 | 688 |

Measured.
"""


def test_doc_rows_reads_the_numbers_of_a_table():
    assert sc.doc_rows(TABLE, EN) == {"main": [8192, 3984, 3184],
                                      "knob": [3072, 1360, 688]}
    german = TABLE.replace("8,192", "8 192")
    assert sc.doc_rows(german, EN)["main"][0] == 8192
    assert sc.doc_rows("nothing", EN) is None


def test_a_table_that_matches_the_build_passes():
    said = sc.doc_rows(TABLE, EN)
    measured = {"main": (8192, 3984, 3184), "knob": (3072, 1360, 688)}
    assert sc.table_problems("P.md", said, measured) == []


def test_a_depth_that_moved_fails():
    said = sc.doc_rows(TABLE, EN)
    measured = {"main": (8192, 4000, 3168), "knob": (3072, 1360, 688)}
    assert sc.table_problems("P.md", said, measured) == [
        "P.md: the task table gives main as [8192, 3984, 3184]; measured "
        "[8192, 4000, 3168]"]


def test_a_task_the_table_lacks_and_one_it_invents_fail():
    said = sc.doc_rows(TABLE, EN)
    out = sc.table_problems("P.md", said, {"main": (8192, 3984, 3184),
                                           "touch": (4096, 1808, 1264)})
    assert out == ["P.md: the task table has no row for touch",
                   "P.md: the task table has a row for knob, which the "
                   "image does not have"]


def test_both_pages_carry_the_task_table_and_the_count():
    for page, header, sentence in sc.PERFORMANCE:
        text = page.read_text(encoding="utf-8")
        assert sc.doc_rows(text, header), page.name
        assert re.search(sentence, " ".join(text.split())), page.name


@pytest.mark.parametrize("rel, inside", [
    ("build/esp-idf/x.c", True),
    ("build-inject/esp-idf/mbedtls/library/error.c", True),
    ("main/main.c", False),
    ("components/gt911/touch_inject.c", False),
    ("components/builder/x.c", False),
])
def test_build_trees_are_not_read_as_source(rel, inside):
    assert sc.in_build_dir(sc.Path(rel)) is inside


LINK = sc.ROOT / "shared" / "link"


def port_fields():
    """The link_port_t fields in order, as link_port.h declares them."""
    text = (LINK / "include" / "link_port.h").read_text(encoding="utf-8")
    body = re.search(r"typedef struct \{(.*?)\} link_port_t;", text,
                     re.S).group(1)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return [a or b for a, b in
            re.findall(r"\(\s*\*\s*(\w+)\s*\)|\b(\w+)\s*;", body)]


def panel_ports():
    """{field: {function}} over every link_port_t firmware/panel defines."""
    fields = port_fields()
    found = {f: set() for f in fields}
    for src in sorted(sc.PANEL_DIR.rglob("*.c")):
        if sc.in_build_dir(src.relative_to(sc.PANEL_DIR)):
            continue
        text = src.read_text(encoding="utf-8")
        for m in re.finditer(r"\blink_port_t\s+\w+\s*=\s*\{([^}]*)\}",
                             text):
            values = [v.strip() for v in m.group(1).split(",")
                      if v.strip()]
            for field, value in zip(fields, values, strict=True):
                if value != "NULL":
                    found[field].add(value)
    return found


def port_callers():
    """{function: {field}}: the link_port_t callbacks each function in
    shared/link/link_port.c calls."""
    text = (LINK / "link_port.c").read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r"^\w[^\n(]*?\b(\w+)\([^)]*\)\s*\{(.*?)^\}", text,
                         re.S | re.M):
        calls = set(re.findall(r"\bport->(\w+)\(", m.group(2)))
        if calls:
            out[m.group(1)] = calls
    return out


def test_port_calls_name_every_callback_of_every_panel_port():
    """A port's callback that PORT_CALLS leaves out is a call the graph
    does not follow, and the check would not say so: the caller's indirect
    calls count as followed."""
    ports, callers = panel_ports(), port_callers()
    assert ports["exchange"] and callers
    assert set(sc.PORT_CALLS) == set(callers)
    for caller, fields in callers.items():
        want = set().union(*(ports[f] for f in fields))
        assert set(sc.PORT_CALLS[caller]) == want, caller


OUTPUTS = sc.ROOT / "shared" / "outputs"


def servo_ctx_fields():
    """{field: function} of the servo_link_ctx_t the panel defines."""
    found = {}
    for src in sorted(sc.PANEL_DIR.rglob("*.c")):
        if sc.in_build_dir(src.relative_to(sc.PANEL_DIR)):
            continue
        text = src.read_text(encoding="utf-8")
        for m in re.finditer(r"\bservo_link_ctx_t\s+\w+\s*=\s*\{([^}]*)\}",
                             text):
            for field, value in re.findall(r"\.(\w+)\s*=\s*(\w+)",
                                           m.group(1)):
                found.setdefault(field, set()).add(value)
    return found


def servo_callers():
    """{function: {callback}} over shared/outputs/servo_link.c: a callback
    of the context as its field, one of its port's as port.<field>."""
    text = (OUTPUTS / "servo_link.c").read_text(encoding="utf-8")
    out = {}
    # The helpers carry __attribute__((noinline)) ahead of their name.
    for m in re.finditer(r"^\w[^\n]*?\b(\w+)\([^)]*\)\s*\{(.*?)^\}", text,
                         re.S | re.M):
        body = m.group(2)
        calls = {f"port.{f}"
                 for f in re.findall(r"\bs_ctx->port->(\w+)\(", body)}
        calls |= set(re.findall(r"\bs_ctx->(\w+)\(", body))
        if calls:
            out[m.group(1)] = calls
    return out


def test_servo_calls_name_every_callback_of_the_panel_servo_link():
    """A servo_link_ctx_t callback SERVO_CALLS leaves out, or one called
    from a function the table does not name, is a call the graph does not
    follow."""
    ctx, ports, callers = servo_ctx_fields(), panel_ports(), servo_callers()
    assert ctx and callers
    assert set(sc.SERVO_CALLS) == set(callers)
    for caller, fields in callers.items():
        want = set()
        for f in fields:
            if f.startswith("port."):
                want |= ports[f[len("port."):]]
            else:
                want |= ctx[f]
        assert set(sc.SERVO_CALLS[caller]) == want, caller
