"""stack_riscv.py, one instruction at a time and one function at a time.

The bytes below are GNU as and ld output (ESP-IDF's riscv32-esp-elf, GCC
14.2) for this source, linked at 0x40000000 with --no-relax:

    f:  addi sp,sp,-48      # c.addi16sp
        sw   ra,44(sp)
        jal  ra, g
        call far            # auipc ra + jalr ra
        jalr a5             # c.jalr through an unknown register
        li   t0, 2048       # lui + addi
        sub  sp,sp,t0
        add  sp,sp,t0       # c.add
        c.jalr a4
        beqz a0, 1f         # c.beqz
        jr   a5             # c.jr through an unknown register
    1:  lw   ra,44(sp)
        addi sp,sp,48       # c.addi16sp
        ret
    g:  addi sp,sp,-16      # c.addi
        addi sp,sp,16
        tail far            # auipc t1 + jalr zero
        .org 0x1000
    far: mv sp,s0           # c.mv
        j    f
        ret

SPDX-License-Identifier: MIT
"""

import pytest

import stack_riscv as sr

BASE = 0x40000000
F_CODE = bytes.fromhex(
    "797106d6ef00600297100000e78080ff8297856293820280330151401691029711c1"
    "8287b25045618280")
G_CODE = bytes.fromhex("4111410117130000670023fd")
FAR_CODE = bytes.fromhex("22816fe0ffff8280")
F, G, FAR = BASE, BASE + 0x2A, BASE + 0x1000


class FakeElf:
    """The two pieces of code at their addresses, as Elf.read() gives them."""

    def __init__(self, pieces: dict) -> None:
        """Hold {address: bytes}."""
        self.pieces = pieces

    def read(self, addr: int, n: int) -> bytes | None:
        """The @p n bytes at @p addr, or None outside every piece."""
        for base, code in self.pieces.items():
            if base <= addr and addr + n <= base + len(code):
                return code[addr - base: addr - base + n]
        return None


ELF = FakeElf({F: F_CODE + G_CODE, FAR: FAR_CODE})


@pytest.mark.parametrize("half, n", [(0x7179, 2), (0x00EF, 4), (0x8082, 2),
                                     (0x0133, 4)])
def test_the_length_comes_from_the_low_two_bits(half, n):
    """The length comes from the low two bits."""
    assert sr.length(half) == n


@pytest.mark.parametrize("pc, word, n, want", [
    (F, 0x7179, 2, ("sp+", -48, None, None)),            # c.addi16sp
    (F + 0x26, 0x6145, 2, ("sp+", 48, None, None)),
    (G, 0x1141, 2, ("sp+", -16, None, None)),            # c.addi sp
    (F + 4, 0x026000EF, 4, ("call", G, None, None)),     # jal ra
    (F + 8, 0x00001097, 4, ("auipc", 1, F + 0x1008, None)),
    (F + 0xC, 0xFF8080E7, 4, ("callr", 1, -8, None)),    # jalr ra, -8(ra)
    (F + 0x10, 0x9782, 2, ("callr", 15, 0, None)),       # c.jalr a5
    (F + 0x12, 0x6285, 2, ("lui", 5, 0x1000, None)),     # c.lui t0
    (F + 0x14, 0x80028293, 4, ("addi", 5, 5, -2048)),
    (F + 0x18, 0x40510133, 4, ("sp+reg", 5, True, None)),     # sub
    (F + 0x1C, 0x9116, 2, ("sp+reg", 5, False, None)),        # c.add
    (F + 0x20, 0xC111, 2, ("branch", F + 0x24, None, None)),  # c.beqz
    (F + 0x22, 0x8782, 2, ("jumpr", 15, 0, None)),            # c.jr a5
    (F + 0x28, 0x8082, 2, ("ret", None, None, None)),
    (G + 8, 0xFD230067, 4, ("jumpr", 6, -46, None)),          # jr -46(t1)
    (FAR, 0x8122, 2, ("sp=", None, None, None)),              # c.mv sp, s0
    (FAR + 2, 0xFFFFE06F, 4, ("jump", F, None, None)),        # j f
])
def test_one_instruction_decodes_to_what_the_check_reads(pc, word, n, want):
    """One instruction decodes to what the check reads."""
    assert sr.decode(pc, word, n) == want


def test_a_function_with_a_far_call_and_a_constant_frame():
    """A function with a far call and a constant frame."""
    f = sr.analyse_riscv(ELF, F, len(F_CODE), "f")
    assert f.frame == 48 + 2048
    assert f.calls == {G, FAR}
    assert f.callx == 2          # jalr a5, c.jalr a4
    assert f.jumpx == 1          # jr a5
    assert f.tails == 0
    assert not f.dynamic


def test_a_tail_call_through_auipc_is_followed_as_a_call():
    """A tail call through auipc is followed as a call."""
    g = sr.analyse_riscv(ELF, G, len(G_CODE), "g")
    assert g.frame == 16
    assert g.calls == {FAR}
    assert g.tails == 1
    assert g.callx == 0 and g.jumpx == 0


def test_a_stack_pointer_from_a_register_is_a_frame_sized_at_run_time():
    """A stack pointer from a register is a frame sized at run time."""
    far = sr.analyse_riscv(ELF, FAR, len(FAR_CODE), "far")
    assert far.dynamic
    assert far.frame == 0
    assert far.calls == {F}
    assert far.tails == 1


def test_a_second_frame_after_a_return_is_counted_on_top_of_the_first():
    """A second frame after a return is counted on top of the first."""
    # addi sp,sp,-16; addi sp,sp,16; ret; addi sp,sp,-16; ret.  The block
    # after the return is entered from a branch at a depth the pass does
    # not know, so it resumes at the lowest point seen: an upper bound.
    code = bytes.fromhex("41114101828041118280")
    f = sr.analyse_riscv(FakeElf({F: code}), F, len(code), "twice")
    assert f.frame == 32


def test_a_function_whose_bytes_are_not_in_the_file_is_empty():
    """A function whose bytes are not in the file is empty."""
    f = sr.analyse_riscv(FakeElf({}), F, 8, "gone")
    assert (f.frame, f.calls, f.callx) == (0, set(), 0)


def test_a_last_instruction_cut_by_the_symbol_size_is_left_out():
    """A last instruction cut by the symbol size is left out."""
    # The 4-byte jal of f, cut after 2 bytes.
    assert sr.steps(F_CODE[4:6], F + 4) == []
