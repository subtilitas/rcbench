"""stack_check.py: the task table in the docs, the call graph, the
tables of callbacks followed by name, the build trees it does not read and
the machine it reads.  The decoder's tests are in test_stack_riscv.py.

SPDX-License-Identifier: MIT
"""

import re

import pytest

import stack_check as sc
import stack_riscv as sr


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
    ("build-noinject/esp-idf/mbedtls/library/error.c", True),
    ("main/main.c", False),
    ("components/gt911/touch_inject.c", False),
    ("components/builder/x.c", False),
    ("managed_components/espressif__esp_hosted/host/x.c", True),
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


def graph(frames: dict, calls: dict) -> sc.Graph:
    """A call graph of {address: frame} and {address: [callee]}."""
    funcs = {}
    for addr, frame in frames.items():
        f = sr.RvFunc(addr, f"f{addr}")
        f.frame = frame
        f.calls = set(calls.get(addr, ()))
        funcs[addr] = f
    return sc.Graph(funcs, {a: f.name for a, f in funcs.items()})


def cycle_graph(a: int, b: int) -> sc.Graph:
    """root (1 byte) calls A (10) and B (100); A and B call each other, and
    A calls Y (1000).  The deepest chain is root, B, A, Y: 1111 bytes."""
    return graph({1: 1, a: 10, b: 100, 9: 1000},
                 {1: [a, b], a: [b, 9], b: [a]})


@pytest.mark.parametrize("a, b", [(2, 3), (3, 2)])
def test_a_depth_through_a_cycle_does_not_depend_on_the_addresses(a, b):
    """A chain cut short where it meets a function already on it is no
    answer for a caller that meets the same function from elsewhere: A
    visited first cuts B's chain at A, and root's call to B still reaches
    A and Y."""
    g = cycle_graph(a, b)
    depth, chain = g.depth(1)
    assert depth == 1111
    assert [addr for _, _, addr in chain] == [1, b, a, 9]
    assert [total for _, total, _ in chain] == [1111, 1110, 1010, 1000]


def test_a_cycle_counts_each_function_once_and_is_reported():
    # root -> A <-> B, A -> A, B -> C: root, A, B, C; the loops are not
    # run round again.
    g = graph({1: 1, 2: 10, 3: 100, 4: 1000, 5: 5},
              {1: [2], 2: [2, 3], 3: [2, 4], 5: [5]})
    assert g.depth(1)[0] == 1111
    assert g.recursion(g.reach(1)) == ["f2, f3"]
    assert g.recursion(g.reach(5)) == ["f5"]
    assert g.recursion(g.reach(4)) == []


def test_every_entry_into_a_cycle_takes_its_own_longest_way_round():
    # A ring 2 -> 3 -> 4 -> 2 entered at 2 and at 4.  From 2 the deepest
    # chain is 2, 3, 7; from 4 it is 4, 2, 3, 7, which runs through 2 with
    # 4 already on the chain.  The deepest from root is root, 4, 2, 3, 7.
    g = graph({1: 1, 2: 10, 3: 20, 4: 30, 6: 5, 7: 500},
              {1: [2, 4], 2: [3], 3: [4, 7], 4: [2, 6]})
    assert g.depth(2)[0] == 10 + 20 + 500
    assert g.depth(4)[0] == 30 + 10 + 20 + 500
    assert g.depth(1)[0] == 1 + 30 + 10 + 20 + 500


def longest_simple_chain(frames: dict, calls: dict, addr: int,
                         on: frozenset = frozenset()) -> int:
    """Every chain from @p addr with no function twice, tried one by one."""
    if addr not in frames or addr in on:
        return 0
    on = on | {addr}
    return frames[addr] + max((longest_simple_chain(frames, calls, c, on)
                               for c in calls.get(addr, ())), default=0)


@pytest.mark.parametrize("seed", range(200))
def test_the_depth_is_the_longest_chain_with_no_function_twice(seed):
    """Random graphs of 2 to 12 functions, some calling themselves, some
    calling addresses where no function is, as a call into ROM is.  Every
    root is asked in a shuffled order of one graph, so the depths kept for
    one root serve the next."""
    import random
    rnd = random.Random(seed)
    n = rnd.randint(2, 12)
    frames = {a: rnd.choice((0, 16, 32, 48, 96, 400)) for a in range(n)}
    calls = {a: [rnd.randrange(n + 3) for _ in range(rnd.randint(0, 3))]
             for a in range(n)}
    for a in range(n):
        if rnd.random() < 0.2:
            calls[a].append(a)
    g = graph(frames, calls)
    roots = list(range(n))
    rnd.shuffle(roots)
    for a in roots:
        depth, chain = g.depth(a)
        assert depth == longest_simple_chain(frames, calls, a), (seed, a)
        assert chain[0][2] == a
        assert len({addr for _, _, addr in chain}) == len(chain)
        for (_, _, here), (_, _, there) in zip(chain, chain[1:],
                                                  strict=False):
            assert there in calls[here], (seed, here, there)
        below = 0
        for frame, total, addr in reversed(chain):
            assert frame == frames[addr]
            below += frame
            assert total == below, (seed, addr)
        assert below == depth


def test_a_search_through_a_dense_cycle_fails_and_names_it():
    """12 functions that all call each other: more ways through than the
    cap allows.  The check fails with the group's size and its functions,
    and does not run on for hours."""
    frames = {a: 16 for a in range(12)}
    calls = {a: [b for b in range(12) if b != a] for a in range(12)}
    g = graph(frames, calls)
    with pytest.raises(SystemExit) as e:
        g.depth(0)
    msg = str(e.value)
    assert "a cycle of 12 functions" in msg
    assert f"passed {sc.Graph.VISIT_CAP} visits from f0" in msg
    assert ", ".join(sorted(f"f{a}" for a in range(12))) in msg


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


def page_set_fields():
    """{hook: {function}} over every prog_page_set_t defined in shared/ui."""
    found = {}
    for src in sorted(sc.UI_DIR.glob("*.c")):
        text = src.read_text(encoding="utf-8")
        for m in re.finditer(r"\bprog_page_set_t\s+\w+\s*=\s*\{([^}]*)\}",
                             text):
            for field, value in re.findall(r"\.(\w+)\s*=\s*(\w+)",
                                           m.group(1)):
                found.setdefault(field, set()).add(value)
    return found


def page_set_callers():
    """{function: {hook}} over the shared/ui files that call a page set's
    hook: ps->hook( in a function's body."""
    out = {}
    for src in sorted(sc.UI_DIR.glob("*.c")):
        text = src.read_text(encoding="utf-8")
        for m in re.finditer(r"^\w[^\n]*?\b(\w+)\(\s*(?:\n[^)]*)?[^)]*\)"
                             r"\s*\{(.*?)^\}", text, re.S | re.M):
            hooks = set(re.findall(r"\bps->(\w+)\(", m.group(2)))
            if hooks:
                out[m.group(1)] = hooks
    return out


def test_prog_calls_name_every_hook_of_every_page_set():
    """A page set's hook PROG_CALLS leaves out, or one called from a
    function the table does not name, is a call the graph does not
    follow."""
    fields, callers = page_set_fields(), page_set_callers()
    assert fields and callers
    assert set(sc.PROG_CALLS) == set(callers)
    for caller, hooks in callers.items():
        assert len(hooks) == 1, caller
        want = set()
        for h in hooks:
            want |= fields[h]
        assert set(sc.PROG_CALLS[caller]) == want, caller


LOGFILE = sc.ROOT / "shared" / "logfile"
PANEL_MAIN = sc.PANEL_DIR / "main"


def io_given(path, struct):
    """{field: function} of the one `struct` initialised in @p path."""
    text = path.read_text(encoding="utf-8")
    inits = re.findall(struct + r" \w+ = \{(.*?)\};", text, re.S)
    assert len(inits) == 1, (path, struct)
    return dict(re.findall(r"\.(\w+)\s*=\s*(\w+)", inits[0]))


def io_table_holds(table, header, struct, source, panel):
    """A table of callbacks names, over its callers, the panel function of
    every field of @p struct that @p source calls, and every caller is a
    function of @p source.  A field left out is a FatFs chain on the runlog
    task the graph does not follow."""
    text = (LOGFILE / "include" / header).read_text(encoding="utf-8")
    body = re.search(r"typedef struct \{(.*?)\} " + struct + ";", text,
                     re.S)
    assert body
    fields = set(re.findall(r"\(\*(\w+)\)\(", body.group(1)))
    src = (LOGFILE / source).read_text(encoding="utf-8")
    called = set(re.findall(r"\bio->(\w+)\(", src))
    assert called <= fields
    given = io_given(PANEL_MAIN / panel, struct)
    assert set(given) - {"ctx", "stem"} == fields
    qualified = set().union(*(set(v) for v in table.values()))
    # A callback written `file:name` names the panel source it is in.
    assert all(q.split(":")[0] == panel for q in qualified if ":" in q)
    named = {q.split(":")[-1] for q in qualified}
    assert {given[f] for f in called} <= named
    for caller in table:
        assert re.search(r"\b" + caller + r"\(", src), caller
    return named - {given[f] for f in called}


def test_take_calls_name_every_card_function_the_panel_hands_log_take():
    rest = io_table_holds(sc.TAKE_CALLS, "log_take.h", "log_take_io_t",
                          "log_take.c", "panel_log.c")
    assert rest == set()


def test_fault_calls_name_every_card_function_of_the_fault_log():
    rest = io_table_holds(sc.FAULT_CALLS, "fault_log.h", "fault_log_io_t",
                          "fault_log.c", "panel_log.c")
    assert rest == set()


def servo_store_calls_of(panel):
    """SERVO_STORE_CALLS cut to the callbacks of one panel source."""
    cut = {caller: tuple(c for c in callbacks if c.startswith(panel + ":"))
           for caller, callbacks in sc.SERVO_STORE_CALLS.items()}
    assert all(cut.values())
    return cut


def test_servo_store_calls_name_every_card_function_of_the_kst_files():
    rest = io_table_holds(servo_store_calls_of("kst_files.c"),
                          "servo_store.h", "servo_store_io_t",
                          "servo_store.c", "kst_files.c")
    assert rest == set()


def test_servo_store_calls_name_every_card_function_of_the_hitec_files():
    rest = io_table_holds(servo_store_calls_of("hitec_files.c"),
                          "servo_store.h", "servo_store_io_t",
                          "servo_store.c", "hitec_files.c")
    assert rest == set()


def test_servo_store_calls_are_qualified_where_another_file_has_the_name():
    """panel_files.c defines static io_mounted, io_walk and io_read of its
    own; a bare name would be two functions in the ELF and fail the run."""
    for caller, callbacks in sc.SERVO_STORE_CALLS.items():
        for cb in callbacks:
            assert cb.startswith(("kst_files.c:", "hitec_files.c:")), (
                caller, cb)
    other = (PANEL_MAIN / "panel_files.c").read_text(encoding="utf-8")
    for name in ("io_mounted", "io_walk", "io_read"):
        assert re.search(r"\bstatic\b[^;{]*\b" + name + r"\(", other), name


def test_card_calls_name_every_folder_maker_handed_to_card_make_dirs():
    """Every function a caller hands card_make_dirs(), in shared/ and in the
    panel, and no other."""
    handed = set()
    for path in [*LOGFILE.glob("*.c"), *PANEL_MAIN.glob("*.c")]:
        text = path.read_text(encoding="utf-8")
        handed |= set(re.findall(
            r"\bcard_make_dirs\((?!const )[^,()]+,\s*(\w+)\s*,", text))
    assert handed
    assert set(sc.CARD_CALLS) == {"card_make_dirs"}
    assert set(sc.CARD_CALLS["card_make_dirs"]) == handed


def elf_of_machine(path, machine):
    """Write the 52-byte header of a 32-bit little-endian ELF.

    Args:
        path: The file to write.
        machine: The e_machine value; the file has no sections.
    """
    head = bytearray(52)
    head[:6] = b"\x7fELF\x01\x01"
    head[0x12:0x14] = machine.to_bytes(2, "little")
    path.write_bytes(bytes(head))


def test_an_xtensa_panel_build_is_refused(tmp_path, monkeypatch):
    elf_of_machine(tmp_path / sc.ELF_NAME, 94)           # EM_XTENSA
    monkeypatch.setattr("sys.argv", ["stack_check.py", str(tmp_path)])
    with pytest.raises(SystemExit) as stop:
        sc.main()
    assert "machine 94, not RISC-V (243)" in str(stop.value)


def test_a_risc_v_header_passes_the_machine_check(tmp_path, monkeypatch):
    elf_of_machine(tmp_path / sc.ELF_NAME, 243)
    monkeypatch.setattr("sys.argv", ["stack_check.py", str(tmp_path)])
    with pytest.raises(SystemExit) as stop:
        sc.main()                    # on to the symbols, which this ELF lacks
    assert "not RISC-V" not in str(stop.value)
    assert "_thread_local_data_start" in str(stop.value)
