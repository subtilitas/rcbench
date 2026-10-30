"""The IOmcu half of tools/stack_check.py: with --iomcu it reads the
RP2350 image, decodes its Thumb code, and holds core 0's and core 1's
stacks to their deepest call chain and one interrupt.  stack_check.py's
docstring says how; the ELF reader and the call graph are in stack_elf.py.

SPDX-License-Identifier: MIT
"""

import bisect
import re
import struct
import sys
from pathlib import Path

from stack_elf import (ROOT, STT_FUNC, STT_OBJECT, Elf, Graph, call_args,
                       die, doc_rows, sext)


# ------------------------------------------------------ the IOmcu --

IOMCU_DIR = ROOT / "firmware" / "iomcu"
IOMCU_ELF = "rcbench-iomcu.elf"

# Core 0 runs main() on the main stack, the top StackSize bytes of the
# region the pico-sdk's linker script reserves: __StackBottom to __StackTop.
# Thread code and interrupt handlers share it.  Core 1 runs on the array its
# launch passes.
CORE0 = ("core 0", "main", ("__StackBottom", "__StackTop"))
CORE1 = ("core 1", "core1_main", "s_stack")

# What an interrupt puts on the stack before its handler's first frame on a
# Cortex-M33 with the FPU (floating-point unit) in use: 8 words of integer
# state, 18 words of floating-point state, and up to 4 bytes to align the
# frame to 8.
ARM_EXCEPTION_FRAME = 8 * 4 + 18 * 4 + 4

# The pico-sdk calls that install an interrupt handler; the handler is their
# second argument, loaded into r1 from a literal before the call.  Every
# handler found this way is a root: the deepest of them, on top of an
# exception frame, is what an interrupt costs the stack it lands on.
IRQ_INSTALLERS = ("irq_set_exclusive_handler", "irq_add_shared_handler")

# The calls through a table of function pointers that are followed by
# name: caller -> (table object, bytes per row, offsets of the pointers in a
# row).  link_dev_dispatch() calls a page's read and write handler through
# the link_page_t table firmware/iomcu/src/main.c passes to link_dev_init();
# a write handler binds outputs and is the deepest thing a request reaches.
# The last number is how many calls through the table the caller's source
# makes: two of read and one of write in shared/link/link_dev.c.  A caller
# here that makes no indirect call or more than that many, or a table that
# is not rows of that size holding function addresses, fails the check: a
# further call through a pointer is not one the table stands for.
POINTER_TABLES = {
    "link_dev_dispatch": ("k_pages", 12, (4, 8), 3),
}

# Calls in the image that the firmware's arguments never take:
# (caller, callee) -> (the functions that may call the caller, why).  The
# edge is left out of the graph and printed.  The entry fails the check when
# the edge is not in the image, when a function outside the list calls the
# caller, and when NEEDLE_LIMIT below is not met, so it cannot outlive its
# reason.
#
# newlib's strstr() hands a needle of 255 characters or more to
# two_way_long_needle(), whose frame holds a 1024-byte shift table.  Every
# needle in shared/, protocols/ and firmware/iomcu is a string literal
# under that.
NOT_TAKEN = {
    ("strstr", "two_way_long_needle"): (
        ("outbind_board_to_regs",),
        "every strstr() needle is a literal shorter than 255 characters"),
}
NEEDLE_LIMIT = 255
NEEDLE_DIRS = (ROOT / "shared", ROOT / "protocols", IOMCU_DIR / "src")


def long_needles() -> list:
    """Every strstr() call under NEEDLE_DIRS whose needle is not a string
    literal shorter than NEEDLE_LIMIT, as "file:line"."""
    out = []
    for base in NEEDLE_DIRS:
        for src in sorted(base.rglob("*.c")):
            raw = src.read_text(encoding="utf-8")
            # Comments out, line breaks kept: a comment that names
            # strstr() is not a call.
            text = re.sub(r"/\*.*?\*/|//[^\n]*",
                          lambda m: re.sub(r"[^\n]", " ", m.group(0)), raw,
                          flags=re.S)
            for m in re.finditer(r"\bstrstr\s*\(", text):
                args = call_args(text, m.end() - 1)
                lit = (re.fullmatch(r'"((?:[^"\\]|\\.)*)"', args[1])
                       if args and len(args) == 2 else None)
                if lit is None or len(lit.group(1)) >= NEEDLE_LIMIT:
                    line = text.count("\n", 0, m.start()) + 1
                    out.append(f"{src.relative_to(ROOT)}:{line}")
    return out


# What a core's stack keeps free beyond its deepest chain and one interrupt:
# room for calls through a pointer, which the graph does not follow, and for
# a second interrupt on top of the first.
IOMCU_MARGIN = 256


def bits(v: int) -> int:
    return bin(v).count("1")


def thumb_imm(hw1: int, hw2: int) -> int:
    """ThumbExpandImm of a 32-bit data-processing instruction."""
    imm12 = ((hw1 >> 10) & 1) << 11 | ((hw2 >> 12) & 7) << 8 | hw2 & 0xFF
    if imm12 >> 10 == 0:
        b, mode = imm12 & 0xFF, (imm12 >> 8) & 3
        return (b, b << 16 | b, b << 24 | b << 8,
                b << 24 | b << 16 | b << 8 | b)[mode]
    rot, val = imm12 >> 7, 0x80 | imm12 & 0x7F
    return (val >> rot | val << (32 - rot)) & 0xFFFFFFFF


def thumb(pc: int, hw1: int, hw2: int) -> tuple:
    """One Thumb instruction: (length, kind, argument), kind one of
    "push" (bytes the stack pointer goes down), "pop" (bytes up, whether
    it loads pc), "call" (target), "callx", "branch" (target), "jump"
    (target), "jumpx" (a jump through a register or a table), "jumplit"
    (a jump to the address a literal holds; the literal's address), "end"
    (a return), "dynamic" (the stack pointer set from a register),
    "lit" (register, literal address), or None."""
    if hw1 >> 11 < 0x1D:                               # 16 bits
        if hw1 & 0xFE00 == 0xB400:                     # push
            return 2, "push", 4 * bits(hw1 & 0x1FF)
        if hw1 & 0xFE00 == 0xBC00:                     # pop
            return 2, "pop", (4 * bits(hw1 & 0x1FF), bool(hw1 & 0x100))
        if hw1 & 0xFF80 == 0xB080:                     # sub sp, #imm
            return 2, "push", 4 * (hw1 & 0x7F)
        if hw1 & 0xFF80 == 0xB000:                     # add sp, #imm
            return 2, "pop", (4 * (hw1 & 0x7F), False)
        if hw1 & 0xFF87 == 0x4700:                     # bx
            return 2, ("end" if (hw1 >> 3) & 0xF == 14 else "jumpx"), None
        if hw1 & 0xFF87 == 0x4780:                     # blx register
            return 2, "callx", None
        if hw1 & 0xFF87 == 0x4687:                     # mov pc, register
            return 2, "jumpx", None
        if hw1 & 0xFF87 == 0x4685 or hw1 & 0xFF87 == 0x4485:
            return 2, "dynamic", None                  # mov / add sp, reg
        if hw1 & 0xF000 == 0xD000 and (hw1 >> 8) & 0xF < 0xE:
            return 2, "branch", pc + 4 + sext((hw1 & 0xFF) << 1, 9)
        if hw1 & 0xF800 == 0xE000:
            return 2, "jump", pc + 4 + sext((hw1 & 0x7FF) << 1, 12)
        if hw1 & 0xF500 == 0xB100:                     # cbz, cbnz
            return 2, "branch", pc + 4 + (
                ((hw1 >> 9) & 1) << 6 | ((hw1 >> 3) & 0x1F) << 1)
        if hw1 & 0xF800 == 0x4800:                     # ldr rt, [pc, #imm]
            return 2, "lit", ((hw1 >> 8) & 7,
                              ((pc + 4) & ~3) + 4 * (hw1 & 0xFF))
        return 2, None, None
    if hw1 & 0xF800 == 0xF000 and hw2 & 0x8000:        # branches, bl
        s = (hw1 >> 10) & 1
        j1, j2 = (hw2 >> 13) & 1, (hw2 >> 11) & 1
        if hw2 & 0x5000 == 0x0000:                     # conditional, or msr
            if (hw1 >> 7) & 7 == 7:
                return 4, None, None                   # msr, mrs and kin
            off = (s << 20 | j2 << 19 | j1 << 18 | (hw1 & 0x3F) << 12
                   | (hw2 & 0x7FF) << 1)
            return 4, "branch", pc + 4 + sext(off, 21)
        i1, i2 = 1 - (j1 ^ s), 1 - (j2 ^ s)
        off = (s << 24 | i1 << 23 | i2 << 22 | (hw1 & 0x3FF) << 12
               | (hw2 & 0x7FF) << 1)
        target = pc + 4 + sext(off, 25)
        if hw2 & 0x5000 == 0x1000:
            return 4, "jump", target                   # b.w
        if hw2 & 0x5000 == 0x5000:
            return 4, "call", target                   # bl
        return 4, "callx", None                        # blx to ARM: none here
    if hw1 == 0xE92D:                                  # push.w
        return 4, "push", 4 * bits(hw2)
    if hw1 == 0xE8BD:                                  # pop.w
        return 4, "pop", (4 * bits(hw2), bool(hw2 & 0x8000))
    if hw1 == 0xF84D and hw2 & 0x0FFF == 0x0D04:       # str rt, [sp, #-4]!
        return 4, "push", 4
    if hw1 == 0xF85D and hw2 & 0x0FFF == 0x0B04:       # ldr rt, [sp], #4
        return 4, "pop", (4, hw2 >> 12 == 15)
    if hw1 & 0xFBEF == 0xF1AD and hw2 & 0x8F00 == 0x0D00:
        return 4, "push", thumb_imm(hw1, hw2)          # sub.w sp, sp, #imm
    if hw1 & 0xFBFF == 0xF2AD and hw2 & 0x8F00 == 0x0D00:
        return 4, "push", (((hw1 >> 10) & 1) << 11 | ((hw2 >> 12) & 7) << 8
                           | hw2 & 0xFF)               # subw sp, sp, #imm
    if hw1 & 0xFBEF == 0xF10D and hw2 & 0x8F00 == 0x0D00:
        return 4, "pop", (thumb_imm(hw1, hw2), False)  # add.w sp, sp, #imm
    if hw1 & 0xFBFF == 0xF20D and hw2 & 0x8F00 == 0x0D00:
        return 4, "pop", (((hw1 >> 10) & 1) << 11 | ((hw2 >> 12) & 7) << 8
                          | hw2 & 0xFF, False)         # addw sp, sp, #imm
    if hw1 & 0xFFEF in (0xEBAD, 0xEB0D) and hw2 & 0x0F00 == 0x0D00:
        return 4, "dynamic", None                      # sub / add sp, sp, reg
    if hw1 & 0xFFBF == 0xED2D and hw2 & 0x0E00 == 0x0A00:      # vpush
        return 4, "push", 4 * (hw2 & 0xFF)
    if hw1 & 0xFFBF == 0xECBD and hw2 & 0x0E00 == 0x0A00:      # vpop
        return 4, "pop", (4 * (hw2 & 0xFF), False)
    if hw1 & 0xFFF0 == 0xE8D0 and hw2 & 0xFFE0 == 0xF000:
        return 4, "jumpx", None                        # tbb, tbh
    if hw1 & 0xFF7F == 0xF85F:                         # ldr.w rt, literal
        off = hw2 & 0xFFF
        lit = ((pc + 4) & ~3) + (off if hw1 & 0x80 else -off)
        # Into pc it is the linker's veneer: a jump to the address the
        # literal holds, from flash to a function kept in RAM.
        return 4, ("jumplit" if hw2 >> 12 == 15 else "lit"), (
            lit if hw2 >> 12 == 15 else (hw2 >> 12, lit))
    return 4, None, None


class ArmFunc:
    __slots__ = ("addr", "name", "frame", "calls", "callx", "dynamic",
                 "stray", "asm", "tails", "handlers", "unread")

    def __init__(self, addr: int, name: str) -> None:
        self.addr = addr
        self.name = name
        self.frame = 0          # the lowest the stack pointer goes, in bytes
        self.calls = set()      # addresses called or tail-jumped to
        self.callx = 0          # calls and jumps through a register
        self.dynamic = False    # the stack pointer is set from a register
        self.stray = 0          # calls to an address no function starts at
        self.asm = set()        # functions called that have no size, by name
        self.tails = 0          # jumps out of the function, taken as calls
        self.handlers = set()   # interrupt handlers this function installs
        self.unread = 0         # installs whose handler was not read


def code_spans(addr: int, size: int, marks: list) -> list:
    """The parts of [addr, addr + size) that are Thumb code: the linker
    keeps the assembler's $t and $d symbols, which say where code stops and
    a literal pool or a jump table starts."""
    end = addr + size
    i = bisect.bisect_right(marks, (addr, "~")) - 1
    state = marks[i][1] if i >= 0 else "t"
    spans, at = [], addr
    for where, kind in marks[max(i + 1, 0):]:
        if where >= end:
            break
        if kind != state:
            if state == "t" and where > at:
                spans.append((at, where))
            state, at = kind, where
    if state == "t" and end > at:
        spans.append((at, end))
    return spans


def analyse_thumb(elf: Elf, addr: int, size: int, name: str, marks: list,
                  installers: set) -> ArmFunc:
    """A function's frame and calls.

    The frame is the lowest point the stack pointer reaches along the
    function's instructions in address order: pushes and subtractions take
    it down, pops and additions bring it back.  After a return the count
    resumes at the lowest point seen so far, because the block that follows
    is entered from a branch at a depth this pass does not know; that never
    counts a frame too small."""
    f = ArmFunc(addr, name)

    def inside(a: int) -> bool:
        return addr <= a < addr + size

    def install(target: int, regs: dict) -> None:
        """A call or a tail jump to an installer takes its handler from
        r1."""
        if target in installers:
            if 1 in regs:
                f.handlers.add(regs[1] & ~1)
            else:
                f.unread += 1

    steps = []
    for lo, hi in code_spans(addr, size, marks):
        code = elf.read(lo, hi - lo)
        if code is None:
            continue
        pc = lo
        while pc + 2 <= hi:
            hw1 = int.from_bytes(code[pc - lo: pc - lo + 2], "little")
            hw2 = 0
            if hw1 >> 11 >= 0x1D:
                if pc + 4 > hi:
                    break
                hw2 = int.from_bytes(code[pc - lo + 2: pc - lo + 4],
                                     "little")
            n, kind, arg = thumb(pc, hw1, hw2)
            steps.append((pc, kind, arg))
            pc += n
    # Where two paths meet, a register holds what either path loaded, so a
    # literal loaded before a branch target is forgotten at it.
    joins = {arg for _, kind, arg in steps
             if kind in ("branch", "jump") and inside(arg)}

    cur = low = 0
    regs = {}                   # register -> literal value loaded into it
    for pc, kind, arg in steps:
        if pc in joins:
            regs = {}
        if kind == "push":
            cur += arg
            low = max(low, cur)
        elif kind == "pop":
            cur = max(cur - arg[0], 0)
            if arg[1]:
                cur, regs = low, {}
        elif kind == "lit":
            word = elf.read(arg[1], 4)
            if word is None:
                regs.pop(arg[0], None)
            else:
                regs[arg[0]] = int.from_bytes(word, "little")
        elif kind == "call":
            f.calls.add(arg)
            install(arg, regs)
            regs = {}
        elif kind == "callx":
            f.callx += 1
            regs = {}
        elif kind in ("branch", "jump"):
            if not inside(arg):
                # A tail call: the jumping function has released its
                # frame, so adding the target's depth to this frame is
                # an upper bound.
                f.calls.add(arg)
                f.tails += 1
                install(arg, regs)
            if kind == "jump":
                cur, regs = low, {}
        elif kind == "jumplit":
            word = elf.read(arg, 4)
            if word is None:
                f.callx += 1
            else:
                f.calls.add(int.from_bytes(word, "little") & ~1)
                f.tails += 1
            cur, regs = low, {}
        elif kind == "jumpx":
            f.callx += 1
            cur, regs = low, {}
        elif kind == "end":
            cur, regs = low, {}
        elif kind == "dynamic":
            f.dynamic = True
    f.frame = low
    return f


def iomcu_elf(build: Path) -> Path:
    """The IOmcu image's ELF in a build directory.

    Args:
        build: The IOmcu build directory.

    Returns:
        rcbench-iomcu.elf, the module's image, when the directory holds it;
        else the one rcbench-iomcu-<board>.elf of a board variant
        (IOMCU_BOARD); else the path of rcbench-iomcu.elf, which does not
        exist.
    """
    path = build / IOMCU_ELF
    if not path.exists():
        variants = sorted(build.glob("rcbench-iomcu-*.elf"))
        if len(variants) == 1:
            return variants[0]
    return path


def iomcu_check(build: Path, verbose: bool) -> tuple:
    """(rows, fails, lines to print) for the IOmcu image: one row per
    core, (name, entry, stack, depth, interrupt, spare, calls and jumps
    through a register reachable from the entry)."""
    path = iomcu_elf(build)
    if not path.exists():
        die(f"{path} not found; build the IOmcu first")
    sys.setrecursionlimit(20000)
    elf = Elf(path)
    if elf.data[0x12:0x14] != b"\x28\x00":
        die(f"{path} is not an ARM ELF")

    marks = sorted({(s[0], s[3][1]) for s in elf.symbols
                    if s[3][:2] in ("$t", "$d") and s[3][2:3] in ("", ".")})
    sizes, names = {}, {}
    for addr, kind, size, name, _, _ in sorted(elf.symbols,
                                               key=lambda s: s[3]):
        if kind == STT_FUNC and addr & 1:
            names.setdefault(addr & ~1, name)
            sizes[addr & ~1] = max(sizes.get(addr & ~1, 0), size)
    by_name = {}
    for addr, name in names.items():
        by_name.setdefault(name, []).append(addr)
    installers = {a for n in IRQ_INSTALLERS for a in by_name.get(n, [])}
    funcs = {a: analyse_thumb(elf, a, n, names[a], marks, installers)
             for a, n in sizes.items() if n > 0}
    for f in funcs.values():
        for c in f.calls:
            if c in funcs:
                continue
            # A function symbol with no size is hand-written: the pico-sdk's
            # floating-point routines.  Its frame is not read.
            if c in names:
                f.asm.add(names[c])
            else:
                f.stray += 1

    fails, out = [], []
    # The calls shared/link makes through the page table: every page's read
    # and write handler, read out of the table in the ELF.
    for caller, (table, entry, offsets, sites) in POINTER_TABLES.items():
        addrs = by_name.get(caller, [])
        objs = [s for s in elf.symbols if s[3] == table
                and s[1] == STT_OBJECT]
        if len(addrs) != 1 or len(objs) != 1:
            fails.append(f"POINTER_TABLES: {caller} is {len(addrs)} "
                         f"functions and {table} is {len(objs)} objects in "
                         f"the ELF, not 1 each")
            continue
        f = funcs[addrs[0]]
        raw = elf.read(objs[0][0], objs[0][2])
        if not 0 < f.callx <= sites or raw is None or len(raw) % entry:
            fails.append(f"POINTER_TABLES: {caller} makes {f.callx} "
                         f"indirect calls, not 1 to {sites}, or {table} is "
                         f"not rows of {entry} bytes; the table no longer "
                         f"matches shared/link")
            continue
        for row in range(0, len(raw), entry):
            for off in offsets:
                word, = struct.unpack_from("<I", raw, row + off)
                if word == 0:
                    continue
                if word & ~1 not in funcs:
                    fails.append(f"POINTER_TABLES: {table} holds "
                                 f"0x{word:08x}, which is no function")
                    continue
                f.calls.add(word & ~1)
        f.callx = 0

    for (caller, callee), (allowed, why) in NOT_TAKEN.items():
        a, b = by_name.get(caller, []), by_name.get(callee, [])
        if len(a) != 1 or len(b) != 1 or b[0] not in funcs[a[0]].calls:
            fails.append(f"NOT_TAKEN: {caller} does not call {callee} in "
                         f"this image; take the entry out")
            continue
        others = sorted(f.name for f in funcs.values()
                        if a[0] in f.calls and f.name not in allowed)
        if others:
            fails.append(f"NOT_TAKEN: {caller} is also called by "
                         f"{', '.join(others)}; the entry covers "
                         f"{', '.join(allowed)}")
            continue
        funcs[a[0]].calls.discard(b[0])
        out.append(f"not taken: {caller} -> {callee} "
                   f"({funcs[b[0]].frame} bytes): {why}")
    for where in long_needles():
        fails.append(f"{where}: a strstr() needle that is not a string "
                     f"literal under {NEEDLE_LIMIT} characters")

    g = Graph(funcs, names)

    def measure(root: int) -> tuple:
        depth, chain = g.depth(root)
        seen = g.reach(root)
        cycles = g.recursion(seen)
        unknown = []
        indirect = sum(funcs[a].callx for a in seen)
        if indirect:
            unknown.append(f"{indirect} calls or jumps through a register")
        dynamic = sorted(funcs[a].name for a in seen if funcs[a].dynamic)
        if dynamic:
            unknown.append(f"{len(dynamic)} functions that size a frame at "
                           f"run time ({', '.join(dynamic)})")
        asm = sorted({n for a in seen for n in funcs[a].asm})
        if asm:
            unknown.append(f"{len(asm)} hand-written functions with no "
                           f"size, frame unknown")
        stray = sum(funcs[a].stray for a in seen)
        if stray:
            unknown.append(f"{stray} calls to no function's start")
        if cycles:
            unknown.append(f"{len(cycles)} recursive groups, each function "
                           f"entered once per chain")
        if verbose:
            notes[root] = (
                sorted((funcs[a].name, funcs[a].callx) for a in seen
                       if funcs[a].callx), asm, cycles)
        return depth, chain, unknown, indirect

    notes = {}

    # Every handler the image installs, and the deepest of them.
    unread = sorted(f.name for f in funcs.values() if f.unread)
    for name in unread:
        fails.append(f"{name} installs an interrupt handler this tool does "
                     f"not read: it is not loaded into r1 from a literal")
    if not installers:
        fails.append("no " + " or ".join(IRQ_INSTALLERS) + " in the ELF; "
                     "IRQ_INSTALLERS no longer matches the pico-sdk")
    handlers = sorted({h for f in funcs.values() for h in f.handlers})
    irq_depth, irq_name, irq_chain, irq_unknown = 0, "none", [], []
    out.append("interrupt handlers installed, and their deepest chain:")
    for h in handlers:
        if h not in funcs:
            fails.append(f"an interrupt handler at 0x{h:08x} is no "
                         f"function in the ELF")
            continue
        depth, chain, unknown, _ = measure(h)
        out.append(f"  {funcs[h].name:<34} {depth:>5}")
        if depth > irq_depth:
            irq_depth, irq_name = depth, funcs[h].name
            irq_chain, irq_unknown = chain, unknown
    irq_cost = ARM_EXCEPTION_FRAME + irq_depth
    out.append(f"an interrupt costs {irq_cost} bytes: a {ARM_EXCEPTION_FRAME}"
               f"-byte exception frame and {irq_depth} for {irq_name}")
    out.append(f"margin: {IOMCU_MARGIN} bytes for what is not followed")

    symbols = {}
    for addr, kind, size, name, _, _ in elf.symbols:
        symbols.setdefault(name, []).append((addr, kind, size))
    rows, detail = [], []
    out.append(f"{'core':<8} {'entry':<12} {'stack':>6} {'depth':>6} "
               f"{'irq':>5} {'spare':>6}  result")
    for label, entry, where in (CORE0, CORE1):
        if isinstance(where, tuple):
            ends = [symbols.get(n, []) for n in where]
            if any(len(e) != 1 for e in ends):
                fails.append(f"{label}: {' and '.join(where)} are not one "
                             f"symbol each in the ELF")
                continue
            stack = ends[1][0][0] - ends[0][0][0]
        else:
            arrays = [s for s in symbols.get(where, [])
                      if s[1] == STT_OBJECT]
            if len(arrays) != 1:
                fails.append(f"{label}: {where} is {len(arrays)} objects "
                             f"in the ELF, not 1")
                continue
            stack = arrays[0][2]
        addrs = by_name.get(entry, [])
        if len(addrs) != 1:
            fails.append(f"{label}: entry {entry} is {len(addrs)} functions "
                         f"in the ELF, not 1")
            continue
        depth, chain, unknown, through_reg = measure(addrs[0])
        spare = stack - IOMCU_MARGIN - depth - irq_cost
        over = spare < 0
        if over:
            fails.append(f"{label}: {depth} bytes deep and {irq_cost} for "
                         f"an interrupt, over its {stack} bytes less "
                         f"{IOMCU_MARGIN}")
        result = ("OVER" if over else "within, as a lower bound"
                  if unknown or irq_unknown else "within")
        out.append(f"{label:<8} {entry:<12} {stack:>6} {depth:>6} "
                   f"{irq_cost:>5} {spare:>6}  {result}")
        if unknown:
            out.append(f"{'':<8} not followed: " + "; ".join(unknown))
        rows.append((label, entry, stack, depth, irq_cost, spare,
                     through_reg))
        detail.append((label, depth, chain, addrs[0]))
    if irq_unknown:
        out.append(f"{'interrupt':<8} not followed: "
                   + "; ".join(irq_unknown))
    if verbose:
        if irq_chain:
            detail.append(("interrupt", irq_depth, irq_chain,
                           irq_chain[0][2]))
        for label, depth, chain, root in detail:
            out.append(f"\n== {label}: {depth} bytes; frame, depth from "
                       f"here, function")
            for own, total, addr in chain:
                out.append(f"   {own:5d} {total:6d}  {g.label(addr)}")
            indirect, asm, cycles = notes.get(root, ([], [], []))
            if indirect:
                out.append("   calls and jumps through a register, not "
                           "followed:")
                out += [f"     {fn} x{n}" for fn, n in indirect]
            if asm:
                out.append("   hand-written functions, frame unknown:")
                out.append("     " + ", ".join(asm))
            if cycles:
                out.append("   recursive groups, each function entered "
                           "once per chain:")
                out += [f"     {c}" for c in cycles]
    return rows, fails, out


# ------------------------------------------------- the table in the docs --

# The IOmcu's table in each language and the name of core 0's row.
# Core 0's row is held: stack, deepest chain, one interrupt, spare.  Core
# 1's is not: its chain moves with the compiler, 640 bytes with
# arm-none-eabi-gcc 13.2.1 and 644 with 14.2.1.
IOMCU_PERFORMANCE = (
    (ROOT / "docs" / "Performance.md",
     "| Core | Entry | Stack (bytes) | Deepest chain (bytes) | "
     "One interrupt (bytes) | Spare below the margin (bytes) |",
     "core 0"),
    (ROOT / "docs" / "Performance-de.md",
     "| Kern | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | "
     "Ein Interrupt (Bytes) | Reserve unter der Marge (Bytes) |",
     "Kern 0"),
)

# The prose that gives core 0's figures, page by page: a pattern over the
# page with its whitespace folded to single spaces, and what each group is.
# "calls" is the count of calls and jumps through a register reachable
# from main(); the others are core 0's row.  Every pattern has to match
# once: a page whose sentence is reworded fails until this follows it.
IOMCU_PROSE = (
    (ROOT / "docs" / "Performance.md",
     r"(\d[\d,]*) such calls are reachable from `main` in the image CI "
     r"builds", ("calls",)),
    (ROOT / "docs" / "Performance-de.md",
     r"(\d[\d ]*?) solche Aufrufe sind von `main` aus erreichbar, im Image, "
     r"das CI baut",
     ("calls",)),
    (ROOT / "STATUS.md",
     r"`stack_check\.py --iomcu` measures (\d[\d,]*) bytes for the "
     r"deepest chain and (\d[\d,]*) for one interrupt on the (\d[\d,]*)"
     r"-byte stack, (\d[\d,]*) bytes above the 256-byte margin\. The "
     r"figures are those of the image CI builds with arm-none-eabi-gcc "
     r"14\.2\.1, and `stack_check\.py --iomcu --check-doc` holds them\. "
     r"The depth is a lower bound: (\d[\d,]*) calls through a register "
     r"are not followed\.",
     ("depth", "interrupt", "stack", "spare", "calls")),
)


def number(text: str) -> int:
    """8,192 in English, 8 192 in German, 8192 in either."""
    return int(re.sub(r"[,\s\u00a0\u202f]", "", text))


def check_iomcu_prose(core0: dict) -> list:
    """The sentences of IOMCU_PROSE against core 0's measurement:
    {"stack", "depth", "interrupt", "spare", "calls": int}."""
    out = []
    for page, pattern, names in IOMCU_PROSE:
        text = " ".join(page.read_text(encoding="utf-8").split())
        found = re.findall(pattern, text)
        if len(found) != 1:
            out.append(f"{page.name}: the sentence with core 0's "
                       f"{', '.join(names)} is there {len(found)} times, "
                       f"not once; IOMCU_PROSE no longer matches the page")
            continue
        said = found[0] if isinstance(found[0], tuple) else (found[0],)
        for name, value in zip(names, said, strict=True):
            if number(value) != core0[name]:
                out.append(f"{page.name}: gives core 0's {name} as "
                           f"{value}; measured {core0[name]}")
    return out


def check_iomcu_doc(rows: list) -> list:
    """Core 0's row of the IOmcu table in Performance.md and its
    German page, and the sentences of IOMCU_PROSE, against iomcu_check()'s
    rows."""
    measured = [r for r in rows if r[0] == CORE0[0]]
    if len(measured) != 1:
        return ["core 0 is not measured; its row in the docs is not held"]
    _, _, stack, depth, irq, spare, calls = measured[0]
    want = [stack, depth, irq, spare]
    out = []
    for page, header, name in IOMCU_PERFORMANCE:
        said = doc_rows(page.read_text(encoding="utf-8"), header)
        if said is None or name not in said:
            out.append(f"{page.name}: no IOmcu table with a row for "
                       f"{name}")
        elif said[name] != want:
            out.append(f"{page.name}: the IOmcu table gives {name} "
                       f"as {said[name]}; measured {want}")
    out += check_iomcu_prose({"stack": stack, "depth": depth,
                              "interrupt": irq, "spare": spare,
                              "calls": calls})
    return out
