"""Decode what one ESP32-P4 function does to its stack pointer and calls.

The panel half of tools/stack_check.py's decoder.

The ESP32-P4 runs RV32IMAFC (RISC-V, 32-bit, with the compressed
instructions) plus Espressif's hardware loop and vector extensions.  An
instruction is 2 bytes when its low two bits are not 11 and 4 bytes when
they are; no 6- or 8-byte form is in the image.  GCC leaves no data inside
a function's code (jump tables are in .rodata), so a linear decode from the
symbol's address to its end reads every instruction.

A function's frame is the lowest point the stack pointer reaches along its
instructions in address order: `addi sp, sp, -N` (and the compressed
`c.addi16sp` and `c.addi`) take it down, positive ones bring it back, and
`add` or `sub` of a register holding a known constant move it by that
constant.  After a return or a jump the count resumes at the lowest point
seen so far, because the block that follows is entered from a branch at a
depth this pass does not know; that never counts a frame too small.  A
stack pointer set from any other register, or moved by an unknown amount,
is a frame sized at run time: counted and named, and its frame is the part
the constants give.

Calls the decoder follows:

- `jal ra` and `c.jal`: a direct call;
- `auipc rX` and `jalr ra` through rX: a far call, the address known;
- `jal zero`, `c.j`, a branch, and `auipc` with `jalr zero`: a jump out of
  the function, which is a tail call, taken as a call: the jumping function
  has released its frame, so adding the target's depth to it is an upper
  bound.

What it cannot follow it counts: `jalr ra` and `c.jalr` through a register
whose value is not known (a call through a function pointer), and `jr`
through such a register (a switch table, which stays inside the function,
or a tail call through a pointer; the decode cannot tell them apart).

SPDX-License-Identifier: MIT
"""

from stack_elf import Elf, sext

# e_machine of a RISC-V ELF.
EM_RISCV = 243

# The registers the decode names: zero, the return address, the stack
# pointer, and t0, which the ABI (application binary interface) allows as
# an alternate return address.
ZERO, RA, SP, T0 = 0, 1, 2, 5
LINKS = (RA, T0)

# The 32-bit major opcodes whose bits 7 to 11 are a destination register.
# Stores, branches and FENCE have none; a load into a float register writes
# no integer one, which is harmless to forget.
WRITES_RD = {0x03, 0x13, 0x17, 0x1B, 0x2F, 0x33, 0x37, 0x3B, 0x53, 0x67,
             0x6F, 0x73}
# The custom opcodes, where Espressif's vector and loop instructions sit.
# Whether bits 7 to 11 name an integer register depends on the
# instruction, so a register they name is forgotten, and the stack pointer
# is never taken to be written by one.
CUSTOM = {0x0B, 0x2B, 0x5B, 0x7B}

MASK32 = 0xFFFFFFFF


def length(half: int) -> int:
    """An instruction's length in bytes from its first 16 bits.

    Args:
        half: The first halfword, little-endian.

    Returns:
        2 for a compressed instruction, 4 for any other.
    """
    return 2 if half & 3 != 3 else 4


def _j_imm(w: int) -> int:
    """The offset of a JAL: imm[20|10:1|11|19:12] in bits 31 to 12."""
    imm = (((w >> 31) & 1) << 20 | ((w >> 21) & 0x3FF) << 1
           | ((w >> 20) & 1) << 11 | ((w >> 12) & 0xFF) << 12)
    return sext(imm, 21)


def _b_imm(w: int) -> int:
    """The offset of a conditional branch: imm[12|10:5] and imm[4:1|11]."""
    imm = (((w >> 31) & 1) << 12 | ((w >> 25) & 0x3F) << 5
           | ((w >> 8) & 0xF) << 1 | ((w >> 7) & 1) << 11)
    return sext(imm, 13)


def _cj_imm(h: int) -> int:
    """The offset of c.j and c.jal: imm[11|4|9:8|10|6|7|3:1|5]."""
    imm = (((h >> 12) & 1) << 11 | ((h >> 11) & 1) << 4
           | ((h >> 9) & 3) << 8 | ((h >> 8) & 1) << 10
           | ((h >> 7) & 1) << 6 | ((h >> 6) & 1) << 7
           | ((h >> 3) & 7) << 1 | ((h >> 2) & 1) << 5)
    return sext(imm, 12)


def _cb_imm(h: int) -> int:
    """The offset of c.beqz and c.bnez: imm[8|4:3] and imm[7:6|2:1|5]."""
    imm = (((h >> 12) & 1) << 8 | ((h >> 10) & 3) << 3
           | ((h >> 5) & 3) << 6 | ((h >> 3) & 3) << 1
           | ((h >> 2) & 1) << 5)
    return sext(imm, 9)


def _ci_imm(h: int) -> int:
    """The 6-bit signed immediate of c.addi and c.li: imm[5] and imm[4:0]."""
    return sext(((h >> 12) & 1) << 5 | (h >> 2) & 0x1F, 6)


def _c16sp_imm(h: int) -> int:
    """The offset of c.addi16sp: nzimm[9|4|6|8:7|5], a multiple of 16."""
    imm = (((h >> 12) & 1) << 9 | ((h >> 6) & 1) << 4
           | ((h >> 5) & 1) << 6 | ((h >> 3) & 3) << 7
           | ((h >> 2) & 1) << 5)
    return sext(imm, 10)


def decode(pc: int, w: int, n: int) -> tuple:
    """What one instruction does that the stack check reads.

    Args:
        pc: The instruction's address.
        w: The instruction, 16 or 32 bits.
        n: Its length, 2 or 4.

    Returns:
        (kind, a, b, c).  kind is one of
        "sp+" (a: the constant added to the stack pointer),
        "sp+reg" (a: the register added, b: True for a subtraction),
        "sp=" (the stack pointer set from another register),
        "lui" (a: register, b: value), "auipc" (a: register, b: value),
        "addi" (a: rd, b: rs1, c: constant), "mv" (a: rd, b: rs),
        "call" (a: target), "callr" (a: register, b: offset),
        "jump" (a: target), "jumpr" (a: register, b: offset),
        "branch" (a: target), "ret", "write" (a: the register written),
        or None.
    """
    if n == 2:
        return _decode16(pc, w & 0xFFFF)
    op = w & 0x7F
    rd = (w >> 7) & 0x1F
    f3 = (w >> 12) & 7
    rs1 = (w >> 15) & 0x1F
    rs2 = (w >> 20) & 0x1F
    imm12 = sext(w >> 20, 12)
    if op == 0x6F:                                   # jal
        target = (pc + _j_imm(w)) & MASK32
        if rd in LINKS:
            return ("call", target, None, None)
        return ("jump", target, None, None) if rd == ZERO else (
            "write", rd, None, None)
    if op == 0x67 and f3 == 0:                       # jalr
        if rd == ZERO and rs1 == RA and imm12 == 0:
            return ("ret", None, None, None)
        if rd in LINKS:
            return ("callr", rs1, imm12, None)
        return ("jumpr", rs1, imm12, None) if rd == ZERO else (
            "write", rd, None, None)
    if op == 0x63:                                   # branches
        return ("branch", (pc + _b_imm(w)) & MASK32, None, None)
    if op == 0x37:
        return ("lui", rd, w & 0xFFFFF000, None)
    if op == 0x17:
        return ("auipc", rd, (pc + (w & 0xFFFFF000)) & MASK32, None)
    if op == 0x13 and f3 == 0:                       # addi
        if rd == SP:
            return ("sp+", imm12, None, None) if rs1 == SP else (
                "sp=", None, None, None)
        return ("addi", rd, rs1, imm12)
    if op == 0x33 and f3 == 0 and (w >> 25) in (0, 0x20):    # add, sub
        if rd == SP:
            if rs1 == SP:
                return ("sp+reg", rs2, (w >> 25) == 0x20, None)
            if rs2 == SP and (w >> 25) == 0:
                return ("sp+reg", rs1, False, None)
            return ("sp=", None, None, None)
        if rs2 == ZERO and (w >> 25) == 0:
            return ("mv", rd, rs1, None)
        return ("write", rd, None, None)
    if op in WRITES_RD and rd != ZERO:
        return ("sp=", None, None, None) if rd == SP else (
            "write", rd, None, None)
    if op in CUSTOM and rd not in (ZERO, SP):
        return ("write", rd, None, None)
    return (None, None, None, None)


def _decode16(pc: int, h: int) -> tuple:
    """decode() for a compressed instruction."""
    q, f3 = h & 3, (h >> 13) & 7
    rd = (h >> 7) & 0x1F
    rs2 = (h >> 2) & 0x1F
    if q == 0:
        # c.addi4spn, c.fld, c.lw, c.flw write rd'; the stores write none.
        if f3 in (0, 2):
            return ("write", 8 + ((h >> 2) & 7), None, None)
        return (None, None, None, None)
    if q == 1:
        if f3 == 0:                                  # c.addi, c.nop
            if rd == SP:
                return ("sp+", _ci_imm(h), None, None)
            if rd == ZERO:
                return (None, None, None, None)
            return ("addi", rd, rd, _ci_imm(h))
        if f3 == 1:                                  # c.jal
            return ("call", (pc + _cj_imm(h)) & MASK32, None, None)
        if f3 == 2:                                  # c.li
            return ("addi", rd, ZERO, _ci_imm(h)) if rd != ZERO else (
                None, None, None, None)
        if f3 == 3:
            if rd == SP:                             # c.addi16sp
                return ("sp+", _c16sp_imm(h), None, None)
            if rd == ZERO:
                return (None, None, None, None)
            return ("lui", rd, (_ci_imm(h) << 12) & MASK32, None)
        if f3 == 4:                                  # the ALU on rd'
            return ("write", 8 + ((h >> 7) & 7), None, None)
        if f3 == 5:                                  # c.j
            return ("jump", (pc + _cj_imm(h)) & MASK32, None, None)
        return ("branch", (pc + _cb_imm(h)) & MASK32, None, None)
    if q == 2:
        if f3 in (0, 2):                             # c.slli, c.lwsp
            if rd == SP:
                return ("sp=", None, None, None)
            return ("write", rd, None, None) if rd != ZERO else (
                None, None, None, None)
        if f3 == 4:
            bit12 = (h >> 12) & 1
            if bit12 == 0 and rs2 == ZERO:           # c.jr
                if rd == RA:
                    return ("ret", None, None, None)
                return ("jumpr", rd, 0, None)
            if bit12 == 0:                           # c.mv
                if rd == SP:
                    return ("sp=", None, None, None)
                return ("mv", rd, rs2, None)
            if rs2 == ZERO and rd == ZERO:           # c.ebreak
                return (None, None, None, None)
            if rs2 == ZERO:                          # c.jalr
                return ("callr", rd, 0, None)
            if rd == SP:                             # c.add sp, rs2
                return ("sp+reg", rs2, False, None)
            return ("write", rd, None, None)
    return (None, None, None, None)


class RvFunc:
    """One function as the stack check reads it.

    Attributes:
        addr: Its first address.
        name: Its symbol.
        frame: The lowest point its stack pointer reaches, in bytes.
        calls: The addresses it calls or tail-jumps to.
        callx: Its calls through a register whose value is not known.
        jumpx: Its jumps through such a register: switch tables or tail
            calls through a pointer.
        dynamic: Whether it sets or moves the stack pointer by an amount
            known only at run time.
        rom: The ROM functions it calls, by name; filled in by the caller.
        stray: Its calls to an address no function starts at; filled in by
            the caller.
        tails: Its jumps out of the function, taken as calls.
    """

    __slots__ = ("addr", "name", "frame", "calls", "callx", "jumpx",
                 "dynamic", "rom", "stray", "tails")

    def __init__(self, addr: int, name: str) -> None:
        """An empty record for the function at @p addr."""
        self.addr = addr
        self.name = name
        self.frame = 0
        self.calls = set()
        self.callx = 0
        self.jumpx = 0
        self.dynamic = False
        self.rom = set()
        self.stray = 0
        self.tails = 0


def steps(code: bytes, base: int) -> list:
    """Every instruction of @p code, which starts at @p base, decoded.

    Args:
        code: The function's bytes.
        base: Its address.

    Returns:
        [(pc, kind, a, b, c)] in address order; a last instruction that
        runs past the end is left out.
    """
    out = []
    pc, end = base, base + len(code)
    while pc + 2 <= end:
        half = int.from_bytes(code[pc - base: pc - base + 2], "little")
        n = length(half)
        if pc + n > end:
            break
        w = int.from_bytes(code[pc - base: pc - base + n], "little")
        out.append((pc, *decode(pc, w, n)))
        pc += n
    return out


def analyse_riscv(elf: Elf, addr: int, size: int, name: str) -> RvFunc:
    """A function's frame and calls.

    Args:
        elf: The image.
        addr: The function's address.
        size: Its size in bytes, from the symbol table.
        name: Its symbol.

    Returns:
        The function as the stack check reads it; empty when its bytes are
        not in the file.
    """
    f = RvFunc(addr, name)
    code = elf.read(addr, size)
    if code is None:
        return f

    def inside(a: int) -> bool:
        return addr <= a < addr + size

    decoded = steps(code, addr)
    # Where two paths meet, a register holds what either path loaded, so a
    # value known before a branch target is forgotten at it.
    joins = {a for _, kind, a, _, _ in decoded
             if kind in ("branch", "jump") and inside(a)}
    cur = low = 0
    regs = {ZERO: 0}            # register -> the constant it holds

    def forget(reg: int) -> None:
        if reg != ZERO:
            regs.pop(reg, None)

    def out(target: int) -> None:
        f.calls.add(target)
        f.tails += 1

    for pc, kind, a, b, c in decoded:
        if pc in joins:
            regs = {ZERO: 0}
        if kind == "sp+":
            cur = max(cur - a, 0)
            low = max(low, cur)
        elif kind == "sp+reg":
            if a in regs:
                v = sext(regs[a], 32)
                cur = max(cur + v if b else cur - v, 0)
                low = max(low, cur)
            else:
                f.dynamic = True
        elif kind == "sp=":
            f.dynamic = True
        elif kind in ("lui", "auipc"):
            regs[a] = b
        elif kind == "addi":
            if b in regs:
                value = (regs[b] + c) & MASK32
                forget(a)
                if a != ZERO:
                    regs[a] = value
            else:
                forget(a)
        elif kind == "mv":
            if b in regs and a != ZERO:
                regs[a] = regs[b]
            else:
                forget(a)
        elif kind == "write":
            forget(a)
        elif kind == "call":
            f.calls.add(a)
            regs = {ZERO: 0}
        elif kind == "callr":
            if a in regs:
                f.calls.add((regs[a] + b) & MASK32 & ~1)
            else:
                f.callx += 1
            regs = {ZERO: 0}
        elif kind == "branch":
            if not inside(a):
                out(a)
        elif kind == "jump":
            if not inside(a):
                out(a)
            cur, regs = low, {ZERO: 0}
        elif kind == "jumpr":
            if a in regs:
                target = (regs[a] + b) & MASK32 & ~1
                if not inside(target):
                    out(target)
            else:
                f.jumpx += 1
            cur, regs = low, {ZERO: 0}
        elif kind == "ret":
            cur, regs = low, {ZERO: 0}
    f.frame = low
    return f
