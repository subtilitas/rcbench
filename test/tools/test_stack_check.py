"""stack_check.py: the Thumb decoder, a function's frame, and the table in
the docs."""

import struct

import pytest

import stack_check as sc


# Instructions as arm-none-eabi-objdump prints them for the coprocessor
# image: (address, first halfword, second halfword) -> what thumb() says.
@pytest.mark.parametrize("pc, hw1, hw2, want", [
    (0x100021B4, 0xE92D, 0x4FF0, (4, "push", 36)),      # stmdb sp!, {r4-fp,lr}
    (0x100021B8, 0xB0CF, 0, (2, "push", 316)),          # sub sp, #316
    (0x10000000, 0xB510, 0, (2, "push", 8)),            # push {r4, lr}
    (0x10000000, 0xBD10, 0, (2, "pop", (8, True))),     # pop {r4, pc}
    (0x10000000, 0xBC10, 0, (2, "pop", (4, False))),    # pop {r4}
    (0x10000000, 0xB002, 0, (2, "pop", (8, False))),    # add sp, #8
    (0x10000000, 0xE8BD, 0x8FF0, (4, "pop", (36, True))),
    (0x10000000, 0xED2D, 0x8B02, (4, "push", 8)),       # vpush {d8}
    (0x10000000, 0xF5AD, 0x7D02, (4, "push", 520)),     # sub.w sp, sp, #520
    (0x10000000, 0xF2AD, 0x4D1C, (4, "push", 1052)),    # subw sp, sp, #1052
    (0x1000833C, 0xF7FF, 0xFC22, (4, "call", 0x10007B84)),
    (0x100021BA, 0xF008, 0xFF4D, (4, "call", 0x1000B058)),
    (0x10008EE4, 0xD4F8, 0, (2, "branch", 0x10008ED8)),
    (0x10008F06, 0xE002, 0, (2, "jump", 0x10008F0E)),
    (0x10000000, 0xB120, 0, (2, "branch", 0x1000000C)),  # cbz r0, +8
    (0x10000000, 0x4770, 0, (2, "end", None)),           # bx lr
    (0x10000000, 0x4718, 0, (2, "jumpx", None)),         # bx r3
    (0x10000000, 0x4798, 0, (2, "callx", None)),         # blx r3
    (0x10000000, 0x46BD, 0, (2, "dynamic", None)),       # mov sp, r7
    (0x10008338, 0x490C, 0, (2, "lit", (1, 0x1000836C))),
    (0x1001FBA8, 0xF85F, 0xF000, (4, "jumplit", 0x1001FBAC)),
    (0x10000000, 0xE8DF, 0xF003, (4, "jumpx", None)),    # tbb [pc, r3]
    (0x10000000, 0x2307, 0, (2, None, None)),            # movs r3, #7
])
def test_thumb_decodes(pc, hw1, hw2, want):
    assert sc.thumb(pc, hw1, hw2) == want


def test_code_spans_leave_the_literal_pools_out():
    marks = [(0x100, "t"), (0x120, "d"), (0x128, "t"), (0x140, "d")]
    assert sc.code_spans(0x100, 0x48, marks) == [(0x100, 0x120),
                                                 (0x128, 0x140)]
    # A function that starts inside code another symbol opened.
    assert sc.code_spans(0x110, 0x8, marks) == [(0x110, 0x118)]
    assert sc.code_spans(0x120, 0x8, marks) == []


class Image:
    """What analyse_thumb() asks of an ELF: bytes at an address."""

    def __init__(self, base, halfwords, data=None):
        self.base = base
        self.code = b"".join(struct.pack("<H", h) for h in halfwords)
        self.data = data or {}

    def read(self, addr, n):
        if addr in self.data:
            return struct.pack("<I", self.data[addr])[:n]
        off = addr - self.base
        if 0 <= off and off + n <= len(self.code):
            return self.code[off: off + n]
        return None


def analyse(halfwords, installers=frozenset(), data=None):
    image = Image(0x1000, halfwords, data)
    return sc.analyse_thumb(image, 0x1000, 2 * len(halfwords), "f",
                            [(0x1000, "t")], set(installers))


def test_a_frame_is_the_lowest_the_stack_pointer_goes():
    f = analyse([
        0xB510,             # push {r4, lr}        8
        0xB082,             # sub sp, #8          16
        0xF000, 0xF87C,     # bl 0x1100
        0xB002,             # add sp, #8
        0xBD10,             # pop {r4, pc}
    ])
    assert f.frame == 16
    assert f.calls == {0x1100}
    assert (f.callx, f.tails, f.dynamic) == (0, 0, False)


def test_a_block_after_a_return_starts_at_the_lowest_point():
    f = analyse([
        0xB510,             # push {r4, lr}        8
        0xBD10,             # pop {r4, pc}
        0xB084,             # sub sp, #16         24: entered by a branch
        0xB004,             # add sp, #16
        0xBD10,             # pop {r4, pc}
    ])
    assert f.frame == 24


def test_a_jump_out_of_the_function_is_a_call():
    f = analyse([0xB510, 0xE7FE - 0x100])       # push; b far below
    assert f.tails == 1 and len(f.calls) == 1
    assert all(not 0x1000 <= c < 0x1004 for c in f.calls)


def test_calls_through_a_register_are_counted():
    f = analyse([0xB510, 0x4798, 0x4718])       # push; blx r3; bx r3
    assert f.callx == 2


def test_an_installed_handler_is_read_from_the_literal():
    # ldr r1, [pc, #4]; bl 0x1100: the literal at 0x1008 is the handler.
    f = analyse([0x4901, 0xF000, 0xF87D], installers={0x1100},
                data={0x1008: 0x2001})
    assert f.handlers == {0x2000}
    assert f.unread == 0


def test_an_install_without_a_literal_is_counted_as_unread():
    f = analyse([0xF000, 0xF87E], installers={0x1100})
    assert f.handlers == set() and f.unread == 1


def test_the_panel_decoder_reads_an_entry():
    # entry a1, 48
    assert sc.decode(0x42000000, 0x006136, 3) == ("entry", 48)


def test_no_strstr_needle_is_long():
    """The reason NOT_TAKEN gives for strstr() holds in the tree."""
    assert sc.long_needles() == []


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
    import re
    for page, header, sentence in sc.PERFORMANCE:
        text = page.read_text(encoding="utf-8")
        assert sc.doc_rows(text, header), page.name
        assert re.search(sentence, " ".join(text.split())), page.name
