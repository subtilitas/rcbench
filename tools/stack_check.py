#!/usr/bin/env python3
"""Hold the panel's task stacks, and the coprocessor's two, to their
deepest call chain.

A task that runs past the end of its stack trips FreeRTOS's stack check and
restarts the panel.  Neither the host build nor the size report shows how
deep a task goes, so this reads it out of the firmware the panel runs.

On the Xtensa windowed ABI (application binary interface) every function
opens with `entry a1, N`, and N is its whole frame.  A task's depth is the
largest sum of frames along any chain of calls from its entry point.

The tool reads the panel ELF (Executable and Linkable Format) file and
decodes the instructions it needs itself.  A linear disassembly, objdump's,
loses its place on the alignment padding the assembler leaves after an
unconditional jump, and reads the real instructions that follow as other
ones.  Against the call graph GCC reported for one v0.13.0 build
(-fcallgraph-info), objdump's listing missed 133 of 6228 direct calls,
log_task's call to log_writer_row among them; this decoder misses 16 of
6274, calls to ROM arithmetic and from interrupt and start-up code.  After
an unconditional jump it restarts at the branch target within the next 3
bytes, or where none is known, at the first of those addresses whose
instructions run on exactly to the next known target.  A branch target it
still does not land on is counted.

Calls the tool follows:

- direct calls: call0, call4, call8 and call12;
- tail calls: a j, a branch, or a jx through an l32r-loaded address, out of
  the function, each counted as a call (see analyse() for why that is the
  safe sum);
- an address loaded with l32r and called with callx in the same basic
  block, with the register untouched in between: the far call the compiler
  emits from flash into IRAM (instruction RAM) and into ROM;
- the router's calls into a screen: ui_router.c calls the screen on top
  through its ui_screen_t, and SCREEN_CALLS below names the callbacks each
  router function reaches.  The callbacks themselves are read out of every
  screen's table in the ELF, or out of the table's source where the
  compiler folded the table into code.

What it cannot follow it counts and names, and never drops quietly:

- any other call through a function pointer;
- a call into the ESP32-S3's ROM (read-only memory), which is not in the
  ELF, so its frame is unknown;
- a call or tail jump to an address where no function starts;
- a function that opens with no entry: hand-written or call0 code, whose
  frame is not read;
- recursion: a function already on the chain is not entered again, so a
  depth through a cycle counts the cycle once.

A task with any of these has a depth that is a lower bound, and the result
says so.  The margin is what covers them.

The margin is 1024 bytes.  528 of it every task spends outside its frames on
the ESP32-S3 under ESP-IDF v5.4: 320 for the FPU (floating-point unit) and
PIE (processor instruction extensions) state saved at the top of the stack
(XT_CP_SIZE, 316, rounded to 16), 192 for the exception
frame an interrupt pushes before it moves to the interrupt stack
(XT_STK_FRMSZ), and 16 for the base save area below the deepest frame.  The
other 496 bytes are for what the graph does not follow.

The stacks: the main task's is CONFIG_ESP_MAIN_TASK_STACK_SIZE from the
build's config/sdkconfig.h; every other task's is the size its
xTaskCreatePinnedToCore() or xTaskCreate() call passes, found in every C
file under firmware/panel.  The check fails when a task's depth exceeds its
stack less the margin, when a task creation's entry or stack size is not a
name and a number, when another xTaskCreate*() variant appears, when a
screen table cannot be read, or when a router function in SCREEN_CALLS
makes no indirect call.

The coprocessor.  With --iomcu the tool reads the RP2350 image instead.
Core 0 runs main() on the pico-sdk's main stack, __StackBottom to __StackTop
in the ELF, and core 1 runs core1_main() on the array its launch passes;
neither has a guard, and a chain past the end of core 0's stack writes into
the memory below it.  The image is Thumb code: a function's frame is the
lowest point its pushes and subtractions take the stack pointer to, and the
linker's $t and $d symbols say where code stops and a literal pool starts.
Interrupt handlers run on the stack of the core they interrupt.  The tool
finds every handler the image installs with irq_set_exclusive_handler() or
irq_add_shared_handler(), and charges each core one interrupt: a 108-byte
exception frame and the deepest handler's chain.  The check fails when a
core's deepest chain, one interrupt and a margin of 256 bytes exceed its
stack, and when a handler is installed in a form the tool does not read.
The handlers of a page are followed through the link's page table.  The
pico-sdk's panic() and what it calls to print are part of a chain like any
other function: a core that halts still writes below its stack pointer.  One
call the firmware's arguments never take is left out by name (NOT_TAKEN).
Calls through a
register, hand-written functions, functions that size a frame at run time
and recursion are counted and named, as for the panel.

    tools/stack_check.py [BUILD]        check; BUILD defaults to
                                        firmware/panel/build
    tools/stack_check.py [BUILD] -v     and print each task's deepest chain
                                        and every call not followed
    tools/stack_check.py --iomcu [BUILD]
                                        the coprocessor's two cores; BUILD
                                        defaults to firmware/iomcu/build
    tools/stack_check.py [BUILD] --check-doc
                                        and hold the panel's table in
                                        docs/Performance.md and its German
                                        page to this build
    tools/stack_check.py --iomcu [BUILD] --check-doc
                                        and hold core 0's row of the
                                        coprocessor table in the same pages

SPDX-License-Identifier: MIT
"""

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PANEL_DIR = ROOT / "firmware" / "panel"
# The FreeRTOS calls that create a task with a stack from the heap; the
# entry, the name and the stack size are their first three arguments.
TASK_CREATE = ("xTaskCreatePinnedToCore", "xTaskCreate")
SCREEN_H = ROOT / "shared" / "ui" / "include" / "ui_screen.h"
UI_DIR = ROOT / "shared" / "ui"
ELF_NAME = "rcbench-panel.elf"
# The ESP32-S3's mask ROM, where a call leaves the ELF.
ROM_LO, ROM_HI = 0x40000000, 0x40060000

MARGIN = 1024
OUTSIDE_FRAMES = 320 + 192 + 16

# ESP-IDF's main task.  It calls app_main, which runs the UI.
MAIN_TASK = "main_task"

# The ui_screen_t callbacks each router function calls through the screen
# table.  A router function not listed here that makes an indirect call is
# reported as not followed.  One listed here that makes none fails the
# check: the calls it stood for have moved where this table does not reach.
SCREEN_CALLS = {
    "ui_router_init": ("reset", "enter"),
    "ui_router_goto": ("leave", "enter"),
    "ui_router_tick": ("tick",),
    "ui_router_event": ("event",),
    "ui_router_cancel_gestures": ("cancel",),
    "ui_router_render": ("render",),
}

# The link_port_t callbacks shared/link calls: the functions the panel puts
# in its two ports.  A request to the coprocessor waits in exchange(), which
# runs the safety loop, so this is the control task's deepest chain and it
# is followed by name.  A function listed here that makes no indirect call,
# or a callback that is not one function in the ELF, fails the check.
PORT_CALLS = {
    "link_write_acked": ("port_exchange", "port_now", "port_between"),
    "link_read_window": ("port_exchange", "port_now"),
}


def die(msg: str) -> None:
    sys.exit(f"stack_check: {msg}")


# --------------------------------------------------------------- the ELF --

class Elf:
    """The sections with contents, and the symbol table, of a 32-bit
    little-endian ELF."""

    def __init__(self, path: Path) -> None:
        d = path.read_bytes()
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            die(f"{path} is not a 32-bit little-endian ELF")
        self.data = d
        shoff, = struct.unpack_from("<I", d, 0x20)
        shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
        heads = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize)
                 for i in range(shnum)]
        # (address, file offset, size) of every allocated section that has
        # bytes in the file; 8 is SHT_NOBITS.
        self.sections = [(h[3], h[4], h[5]) for h in heads
                         if h[1] != 8 and h[3] != 0]
        self.symbols = []
        for h in heads:
            if h[1] != 2:                     # SHT_SYMTAB
                continue
            strtab = heads[h[6]]
            names = d[strtab[4]: strtab[4] + strtab[5]]
            current_file = None
            for off in range(h[4], h[4] + h[5], 16):
                name, value, size, info, _, shndx = struct.unpack_from(
                    "<IIIBBH", d, off)
                end = names.index(b"\0", name)
                text = names[name:end].decode("utf-8", "replace")
                kind, bind = info & 0xF, info >> 4
                if kind == 4:                 # STT_FILE
                    current_file = text
                    continue
                # (address, kind, size, name, file of a local symbol,
                #  absolute)
                self.symbols.append((value, kind, size, text,
                                     current_file if bind == 0 else None,
                                     shndx == 0xFFF1))

    def read(self, addr: int, n: int) -> bytes | None:
        for base, off, size in self.sections:
            if base <= addr and addr + n <= base + size:
                return self.data[off + addr - base: off + addr - base + n]
        return None


STT_OBJECT, STT_FUNC = 1, 2


# ------------------------------------------------------------ the decoder --

def length(b0: int) -> int:
    """An ESP32-S3 instruction's length from its first byte: op0 0 to 7 is
    24 bits, 8 to 13 the 16-bit density forms, 14 and 15 the 32-bit PIE
    forms."""
    op0 = b0 & 0xF
    return 3 if op0 < 8 else (2 if op0 < 14 else 4)


def sext(v: int, bits: int) -> int:
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def decode(pc: int, w: int, n: int) -> tuple:
    """What one instruction does to control flow:
    (kind, target or register), kind one of
    "entry" (frame), "call" (target, window), "callx" (register, window),
    "l32r" (register, literal address), "branch" (target), "jump" (target),
    "jx" (register), "end" (nothing falls through), or None."""
    op0 = w & 0xF
    if n == 2:
        if op0 == 0xC and (w >> 6) & 3 >= 2:          # beqz.n, bnez.n
            imm6 = ((w >> 4) & 3) << 4 | (w >> 12) & 0xF
            return ("branch", pc + 4 + imm6)
        if op0 == 0xD and (w >> 12) & 0xF == 0xF and (w >> 4) & 0xF in (0, 1):
            return ("end", None)                       # ret.n, retw.n
        return (None, None)
    if n != 3:
        return (None, None)
    t, s, r = (w >> 4) & 0xF, (w >> 8) & 0xF, (w >> 12) & 0xF
    op1, op2 = (w >> 16) & 0xF, (w >> 20) & 0xF
    if op0 == 0 and op1 == 0 and op2 == 0:
        if r == 0:
            m, nn = t >> 2, t & 3
            if m == 3:
                return ("callx", (s, 4 * nn))
            if m == 2 and nn == 2:
                return ("jx", s)
            if m == 2:
                return ("end", None)                   # ret, retw
        if r == 3:
            return ("end", None)                       # rfe, rfi and kin
        return (None, None)
    if op0 == 1:
        imm16 = w >> 8
        return ("l32r", (t, ((pc + 3) & ~3) + ((imm16 - 0x10000) << 2)))
    if op0 == 5:
        return ("call", (((pc & ~3) + (sext(w >> 6, 18) << 2) + 4),
                         4 * ((w >> 4) & 3)))
    if op0 == 6:
        nn, m = (w >> 4) & 3, (w >> 6) & 3
        if nn == 0:
            return ("jump", pc + 4 + sext(w >> 6, 18))
        if nn == 1:
            return ("branch", pc + 4 + sext(w >> 12, 12))
        if nn == 2:
            return ("branch", pc + 4 + sext(w >> 16, 8))
        if m == 0:
            return ("entry", (w >> 12) << 3)
        if m == 1 and r in (0, 1):                     # bf, bt
            return ("branch", pc + 4 + sext(w >> 16, 8))
        if m == 1 and r in (8, 9, 10):                 # loop ends
            return ("branch", pc + 4 + (w >> 16))
        if m >= 2:                                     # bltui, bgeui
            return ("branch", pc + 4 + sext(w >> 16, 8))
        return (None, None)
    if op0 == 7:
        return ("branch", pc + 4 + sext(w >> 16, 8))
    return (None, None)


def sweep(code: bytes, base: int, targets: set) -> tuple:
    """Instruction starts over @p code, restarting after every unconditional
    transfer at the next known branch target within the 3 bytes of padding
    the assembler may leave, or else at the first address that fits."""
    starts = []
    found = set()
    pc, end = base, base + len(code)
    while pc < end:
        b0 = code[pc - base]
        n = length(b0)
        if pc + n > end:
            break
        w = int.from_bytes(code[pc - base: pc - base + n], "little")
        starts.append((pc, w, n))
        kind, arg = decode(pc, w, n)
        if kind in ("branch", "jump"):
            found.add(arg)
        pc += n
        if kind in ("jump", "jx", "end"):
            known = targets | found
            near = [a for a in known if pc <= a <= pc + 3]
            if near:
                pc = min(near)
                continue
            # No branch lands here: code a jump table reaches, or none.
            # The first start whose instructions run on to the next known
            # target, or to the end, exactly.
            ahead = [a for a in known if a > pc + 3]
            stop = min(ahead) if ahead else end
            for c in range(pc, min(pc + 4, end)):
                if lands(code, base, c, stop):
                    pc = c
                    break
    return starts, found


def lands(code: bytes, base: int, pc: int, stop: int) -> bool:
    """Whether instructions from @p pc end exactly at @p stop."""
    while pc < stop:
        pc += length(code[pc - base])
    return pc == stop


class Func:
    __slots__ = ("addr", "name", "frame", "calls", "callx", "rom", "stray",
                 "lost", "tails")

    def __init__(self, addr: int, name: str) -> None:
        self.addr = addr
        self.name = name
        self.frame = 0          # 0: the function opens with no entry
        self.calls = set()      # addresses called or tail-jumped to
        self.callx = 0          # calls through a register not followed
        self.rom = set()        # ROM functions called, by name
        self.stray = 0          # calls to an address no function starts at
        self.lost = 0           # branch targets the decode does not land on
        self.tails = 0          # jumps out of the function, taken as calls


def analyse(elf: Elf, addr: int, size: int, name: str) -> Func:
    f = Func(addr, name)
    code = elf.read(addr, size)
    if code is None:
        return f
    def inside(a: int) -> bool:
        return addr <= a < addr + size

    # A second pass knows the targets of backward branches; more settle a
    # restart that moved a target.
    targets = set()
    for _ in range(6):
        starts, found = sweep(code, addr, targets)
        found = {a for a in found if inside(a)}
        if found == targets:
            break
        targets = found
    at = {pc for pc, _, _ in starts}
    f.lost = len(found - at)
    targets = found
    windowed = bool(starts) and decode(*starts[0])[0] == "entry"
    regs = {}        # register -> address an l32r loaded, this block only
    for pc, w, n in starts:
        if pc in targets:
            regs = {}
        kind, arg = decode(pc, w, n)
        if kind in ("jump", "branch") and not inside(arg):
            # A tail call: a jump, or a branch, out of the function.  Its
            # target's depth is added to this function's frame, as for a
            # call.  Under the call0 ABI the jumping function has already
            # released its frame, so the sum is an upper bound.  Under the
            # windowed ABI no tail jump is valid -- the target's entry would
            # rotate the window by the caller's CALLINC a second time -- and
            # GCC 14 for the ESP32-S3 emits none: a call in tail position
            # compiles to call8 and retw.n.  One found here is hand-written
            # and counted the same way.  A target where no function starts
            # is reported, as a call to one is.
            f.calls.add(arg)
            f.tails += 1
        if kind == "entry" and pc == addr:
            f.frame = arg
        elif kind == "jx":
            # Through a loaded address and out of the function: a tail call.
            # Unresolved in a windowed function: a jump table, which stays
            # inside it.  Unresolved in a call0 function: perhaps a tail
            # call, not followed.
            if arg in regs and not inside(regs[arg]):
                f.calls.add(regs[arg])
                f.tails += 1
            elif arg not in regs and not windowed:
                f.callx += 1
            regs = {}
        elif kind == "call":
            f.calls.add(arg[0])
            regs = {k: v for k, v in regs.items() if arg[1] == 0
                    or k < arg[1]}
        elif kind == "callx":
            reg, window = arg
            if reg in regs:
                f.calls.add(regs[reg])
            else:
                f.callx += 1
            regs = {k: v for k, v in regs.items() if window == 0
                    or k < window}
        elif kind == "l32r":
            lit = elf.read(arg[1], 4)
            if lit is None:
                regs.pop(arg[0], None)
            else:
                regs[arg[0]] = int.from_bytes(lit, "little")
        elif kind in ("jump", "end"):
            regs = {}
        else:
            # Anything else that names a register may have written it.
            fields = ({(w >> s) & 0xF for s in range(4, 16, 4)} if n < 4
                      else {(w >> s) & 0xF for s in range(0, 32, 4)})
            for k in fields & set(regs):
                del regs[k]
    return f


# ------------------------------------------------------- the screen tables --

def screen_slots() -> list:
    """The ui_screen_t fields in order, one word each."""
    text = SCREEN_H.read_text(encoding="utf-8")
    m = re.search(r"typedef struct \{((?:(?!typedef).)*?)\} ui_screen_t;",
                  text, re.S)
    if not m:
        die(f"no ui_screen_t in {SCREEN_H}")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    fields = re.findall(r"\(\s*\*\s*(\w+)\s*\)|\b(\w+)\s*;", body)
    return [a or b for a, b in fields]


def screen_tables(elf: Elf) -> tuple:
    """Every screen's callbacks by slot, {slot: {address}}; how many tables
    came from the ELF and which from their source; and the errors."""
    slots = screen_slots()
    found = {s: set() for s in slots}
    errors, from_elf, from_src = [], 0, []
    for src in sorted(UI_DIR.glob("*.c")):
        text = src.read_text(encoding="utf-8")
        for m in re.finditer(r"\bconst ui_screen_t (\w+)\s*=\s*\{([^}]*)\}",
                             text):
            obj = m.group(1)
            hits = [s for s in elf.symbols if s[1] == STT_OBJECT
                    and s[3] == obj and s[4] == src.name]
            raw = (elf.read(hits[0][0], 4 * len(slots))
                   if len(hits) == 1 and hits[0][2] == 4 * len(slots)
                   else None)
            if raw is not None:
                words = struct.unpack(f"<{len(slots)}I", raw)
                for slot, word in zip(slots, words, strict=True):
                    if slot != "title" and word != 0:
                        found[slot].add(word)
                from_elf += 1
                continue
            if hits:
                errors.append(f"{src.name}: {obj} is in the ELF but is not "
                              f"{len(slots)} words")
                continue
            # Folded into code: the callbacks the source names, which are
            # in the ELF because their addresses are taken.
            from_src.append(src.name)
            for slot, fn in re.findall(r"\.(\w+)\s*=\s*(\w+)", m.group(2)):
                if slot == "title" or fn == "NULL":
                    continue
                if slot not in found:
                    errors.append(f"{src.name}: {obj} sets .{slot}, which "
                                  f"ui_screen_t does not have")
                    continue
                cands = {s[0] for s in elf.symbols if s[1] == STT_FUNC
                         and s[3] == fn and s[4] in (src.name, None)}
                if len(cands) != 1:
                    errors.append(f"{src.name}: {obj}.{slot} = {fn} is "
                                  f"{len(cands)} functions in the ELF, "
                                  f"not 1")
                    continue
                found[slot] |= cands
    return found, from_elf, from_src, errors


# ----------------------------------------------------------------- tasks --

def call_args(text: str, start: int) -> list | None:
    """The top-level arguments of the call whose '(' is at @p start."""
    depth, args, cur = 0, [], start + 1
    for i in range(start, len(text)):
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                args.append(text[cur:i].strip())
                return args
        elif ch == "," and depth == 1:
            args.append(text[cur:i].strip())
            cur = i + 1
    return None


def task_stacks() -> tuple:
    """(entry function, task name, stack bytes, where) for every task the
    panel's own code creates, and the errors: a creation whose entry, name
    or stack size is not a plain identifier, string or number (or a macro
    defined as one in the same file) fails the check rather than being
    skipped."""
    tasks, errors = [], []
    for src in sorted(PANEL_DIR.rglob("*.c")):
        if "build" in src.relative_to(PANEL_DIR).parts:
            continue
        raw = src.read_text(encoding="utf-8")
        # Comments out, line breaks kept, so line numbers hold.
        text = re.sub(r"/\*.*?\*/|//[^\n]*",
                      lambda m: re.sub(r"[^\n]", " ", m.group(0)), raw,
                      flags=re.S)
        macros = dict(re.findall(r"^\s*#\s*define\s+(\w+)\s+\(?\s*(\d+)u?"
                                 r"\s*\)?\s*$", text, re.M))
        for m in re.finditer(r"\b(xTaskCreate\w*)\s*\(", text):
            line = text.count("\n", 0, m.start()) + 1
            where = f"{src.relative_to(ROOT)}:{line}"
            if m.group(1) not in TASK_CREATE:
                errors.append(f"{where}: {m.group(1)}() is not one this "
                              f"tool reads; add it to TASK_CREATE")
                continue
            args = call_args(text, m.end() - 1)
            if args is None or len(args) < 3:
                errors.append(f"{where}: {m.group(1)}() not parsed")
                continue
            entry, name, size = args[0], args[1], args[2]
            size = macros.get(size, size)
            if not re.fullmatch(r"\w+", entry):
                errors.append(f"{where}: task entry {entry!r} is not a "
                              f"function name")
                continue
            nm = re.fullmatch(r"\"([^\"]*)\"", name)
            sm = re.fullmatch(r"(\d+)u?", size)
            if sm is None:
                errors.append(f"{where}: stack size {args[2]!r} of "
                              f"{entry} is not a number")
                continue
            tasks.append((entry, nm.group(1) if nm else entry,
                          int(sm.group(1)), where))
    # One task created in two branches is one row; differing sizes are two.
    seen, rows = set(), []
    for entry, name, size, where in tasks:
        if (entry, name, size) not in seen:
            seen.add((entry, name, size))
            rows.append((entry, name, size, where))
    return rows, errors


def main_stack(build: Path) -> int:
    hdr = build / "config" / "sdkconfig.h"
    if not hdr.exists():
        die(f"{hdr} not found; is {build} an ESP-IDF build directory?")
    m = re.search(r"#define CONFIG_ESP_MAIN_TASK_STACK_SIZE (\d+)",
                  hdr.read_text(encoding="utf-8"))
    if not m:
        die(f"no CONFIG_ESP_MAIN_TASK_STACK_SIZE in {hdr}")
    return int(m.group(1))


class Graph:
    def __init__(self, funcs: dict, names: dict) -> None:
        self.funcs = funcs
        self.names = names          # address -> name, ROM included
        self.memo = {}
        self.cycles = set()

    def label(self, addr: int) -> str:
        f = self.funcs.get(addr)
        return f.name if f else self.names.get(addr, f"0x{addr:08x}")

    def depth(self, addr: int, path: list) -> tuple:
        """(bytes, [(frame, depth from here, address)]) of the deepest
        chain from @p addr."""
        if addr in path:
            loop = path[path.index(addr):] + [addr]
            self.cycles.add(" -> ".join(self.label(a) for a in loop))
            return 0, []
        if addr in self.memo:
            return self.memo[addr]
        f = self.funcs.get(addr)
        if f is None:
            return 0, []
        path.append(addr)
        best, chain = 0, []
        for c in sorted(f.calls):
            d, ch = self.depth(c, path)
            if d > best:
                best, chain = d, ch
        path.pop()
        r = (f.frame + best, [(f.frame, f.frame + best, addr)] + chain)
        self.memo[addr] = r
        return r

    def reach(self, root: int) -> set:
        seen, todo = set(), [root]
        while todo:
            a = todo.pop()
            if a not in seen and a in self.funcs:
                seen.add(a)
                todo.extend(self.funcs[a].calls)
        return seen


# ------------------------------------------------------ the coprocessor --

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
# needle in shared/ and firmware/iomcu is a string literal under that.
NOT_TAKEN = {
    ("strstr", "two_way_long_needle"): (
        ("outbind_board_to_regs",),
        "every strstr() needle is a literal shorter than 255 characters"),
}
NEEDLE_LIMIT = 255
NEEDLE_DIRS = (ROOT / "shared", IOMCU_DIR / "src")


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
    import bisect
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


def iomcu_check(build: Path, verbose: bool) -> tuple:
    """(rows, fails, lines to print) for the coprocessor image: one row per
    core, (name, entry, stack, depth, interrupt, spare)."""
    path = build / IOMCU_ELF
    if not path.exists():
        die(f"{path} not found; build the coprocessor first")
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
        g.cycles, g.memo = set(), {}
        depth, chain = g.depth(root, [])
        seen = g.reach(root)
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
        if g.cycles:
            unknown.append(f"{len(g.cycles)} cycles, each counted once")
        if verbose:
            notes[root] = (
                sorted((funcs[a].name, funcs[a].callx) for a in seen
                       if funcs[a].callx), asm, sorted(g.cycles))
        return depth, chain, unknown

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
        depth, chain, unknown = measure(h)
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
        depth, chain, unknown = measure(addrs[0])
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
        rows.append((label, entry, stack, depth, irq_cost, spare))
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
                out.append("   cycles, each counted once:")
                out += [f"     {c}" for c in cycles]
    return rows, fails, out


# ------------------------------------------------- the tables in the docs --

# The panel's task table in each language, and the sentence that counts the
# calls through a pointer reachable from the main task.
PERFORMANCE = (
    (ROOT / "docs" / "Performance.md",
     "| Task | Entry | Stack (bytes) | Deepest chain (bytes) | "
     "Spare below the margin (bytes) |",
     r"(\d+) such calls are reachable from `main_task`"),
    (ROOT / "docs" / "Performance-de.md",
     "| Task | Einstieg | Stack (Bytes) | Tiefste Kette (Bytes) | "
     "Reserve unter der Marge (Bytes) |",
     r"(\d+) solche Aufrufe sind von `main_task` aus erreichbar"),
)


def doc_rows(text: str, header: str) -> dict | None:
    """{first cell's quoted name: [the numbers of the cells after the
    second]} for the table under @p header; None without the table.  A
    number is written 8,192 in English and 8 192 in German."""
    if header not in text:
        return None
    rows = {}
    for row in text[text.index(header):].split("\n\n", 1)[0].splitlines()[2:]:
        cells = [c.strip() for c in row.strip().strip("|").split("|")]
        name = re.search(r"`([^`]+)`", cells[0])
        numbers = []
        for cell in cells[2:]:
            digits = re.sub(r"[,\s\u00a0\u202f]", "", cell)
            numbers.append(int(digits) if digits.isdigit() else None)
        rows[name.group(1) if name else cells[0]] = numbers
    return rows


def table_problems(page: str, said: dict | None, measured: dict) -> list:
    """What the task table in a page says that the measurement does not."""
    if said is None:
        return [f"{page}: no task table to hold"]
    out = []
    for name, want in measured.items():
        if name not in said:
            out.append(f"{page}: the task table has no row for {name}")
        elif said[name] != list(want):
            out.append(f"{page}: the task table gives {name} as "
                       f"{said[name]}; measured {list(want)}")
    for name in said:
        if name not in measured:
            out.append(f"{page}: the task table has a row for {name}, "
                       f"which the image does not have")
    return out


def check_panel_doc(rows: dict, pointer_calls: int) -> list:
    """Performance.md's task table and its count of pointer calls against
    the measurement: {task: (stack, depth, spare)}."""
    out = []
    for page, header, sentence in PERFORMANCE:
        text = page.read_text(encoding="utf-8")
        out += table_problems(page.name, doc_rows(text, header), rows)
        m = re.search(sentence, " ".join(text.split()))
        if m is None:
            out.append(f"{page.name}: no count of the calls through a "
                       f"pointer reachable from main_task")
        elif int(m.group(1)) != pointer_calls:
            out.append(f"{page.name}: says {m.group(1)} calls through a "
                       f"pointer are reachable from main_task; measured "
                       f"{pointer_calls}")
    return out


# The coprocessor's table in each language and the name of core 0's row.
# Core 0's row is held: stack, deepest chain, one interrupt, spare.  Core
# 1's is not: its chain moves with the compiler, 640 bytes with
# arm-none-eabi-gcc 13.2.1 and 644 with 14.2.
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


def check_iomcu_doc(rows: list) -> list:
    """Core 0's row of the coprocessor table in Performance.md and its
    German page against iomcu_check()'s rows."""
    measured = [list(r[2:]) for r in rows if r[0] == CORE0[0]]
    if len(measured) != 1:
        return ["core 0 is not measured; its row in the docs is not held"]
    out = []
    for page, header, name in IOMCU_PERFORMANCE:
        said = doc_rows(page.read_text(encoding="utf-8"), header)
        if said is None or name not in said:
            out.append(f"{page.name}: no coprocessor table with a row for "
                       f"{name}")
        elif said[name] != measured[0]:
            out.append(f"{page.name}: the coprocessor table gives {name} "
                       f"as {said[name]}; measured {measured[0]}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", nargs="?")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--iomcu", action="store_true",
                    help="BUILD is a coprocessor build: check its two cores")
    ap.add_argument("--check-doc", action="store_true",
                    help="also fail when the panel's task table, or with "
                         "--iomcu core 0's row of the coprocessor table, "
                         "in docs/Performance.md or docs/Performance-de.md "
                         "differs from this build")
    args = ap.parse_args()

    if args.iomcu:
        build = Path(args.build or IOMCU_DIR / "build")
        rows, fails, out = iomcu_check(build, args.verbose)
        if args.check_doc:
            fails += check_iomcu_doc(rows)
        print(f"stack_check: {build / IOMCU_ELF}")
        print("\n".join(out))
        for f in fails:
            print("FAIL " + f)
        print(f"{len(fails)} failure(s)")
        return 1 if fails else 0

    build = Path(args.build or PANEL_DIR / "build")
    path = build / ELF_NAME
    if not path.exists():
        die(f"{path} not found; build the panel first")
    sys.setrecursionlimit(20000)
    elf = Elf(path)

    # Where several names share an address, the alphabetically first.
    sizes, names, rom = {}, {}, {}
    for addr, kind, size, name, _, absolute in sorted(elf.symbols,
                                                      key=lambda s: s[3]):
        if absolute:
            rom.setdefault(addr, name)
        elif kind == STT_FUNC:
            names.setdefault(addr, name)
            sizes[addr] = max(sizes.get(addr, 0), size)
    names.update(rom)
    funcs = {a: analyse(elf, a, n, names[a]) for a, n in sizes.items()
             if n > 0}
    by_name = {}
    for f in funcs.values():
        by_name.setdefault(f.name, []).append(f.addr)
    fails = []

    tables, from_elf, from_src, errors = screen_tables(elf)
    fails += errors
    for router, slots in SCREEN_CALLS.items():
        addrs = by_name.get(router, [])
        if len(addrs) != 1:
            fails.append(f"SCREEN_CALLS: {router} is {len(addrs)} functions "
                         f"in the ELF, not 1")
            continue
        f = funcs[addrs[0]]
        if f.callx == 0:
            fails.append(f"SCREEN_CALLS: {router} makes no indirect call; "
                         f"the table no longer matches the router")
            continue
        for slot in slots:
            f.calls |= tables.get(slot, set())
        f.callx = 0

    for caller, callbacks in PORT_CALLS.items():
        addrs = by_name.get(caller, [])
        if len(addrs) != 1:
            fails.append(f"PORT_CALLS: {caller} is {len(addrs)} functions "
                         f"in the ELF, not 1")
            continue
        f = funcs[addrs[0]]
        if f.callx == 0:
            fails.append(f"PORT_CALLS: {caller} makes no indirect call; "
                         f"the table no longer matches shared/link")
            continue
        for cb in callbacks:
            targets = by_name.get(cb, [])
            if len(targets) != 1:
                fails.append(f"PORT_CALLS: callback {cb} is {len(targets)} "
                             f"functions in the ELF, not 1")
                continue
            f.calls.add(targets[0])
        f.callx = 0

    for f in funcs.values():
        for c in f.calls:
            if c in funcs:
                continue
            if c in rom or ROM_LO <= c < ROM_HI:
                f.rom.add(rom.get(c, f"0x{c:08x}"))
            else:
                f.stray += 1

    tasks = [(MAIN_TASK, "main", main_stack(build),
              "CONFIG_ESP_MAIN_TASK_STACK_SIZE")]
    found_tasks, task_errors = task_stacks()
    fails += task_errors
    tasks += found_tasks

    g = Graph(funcs, names)
    print(f"stack_check: {path}")
    src_note = (f", {len(from_src)} from source ({', '.join(from_src)})"
                if from_src else "")
    print(f"screen tables: {from_elf} from the ELF{src_note}")
    print(f"margin: {MARGIN} bytes, {OUTSIDE_FRAMES} of them outside the "
          f"frames and {MARGIN - OUTSIDE_FRAMES} for what is not followed")
    print(f"{'task':<8} {'entry':<14} {'stack':>6} {'limit':>6} "
          f"{'depth':>6} {'spare':>6}  result")
    detail = []
    measured, pointer_calls = {}, 0
    for entry, name, stack, src in tasks:
        addrs = by_name.get(entry, [])
        if len(addrs) != 1:
            fails.append(f"{name}: entry {entry} is {len(addrs)} functions "
                         f"in the ELF, not 1")
            continue
        g.cycles = set()
        g.memo = {}
        depth, chain = g.depth(addrs[0], [])
        cycles = sorted(g.cycles)
        seen = g.reach(addrs[0])
        indirect = sorted((funcs[a].name, funcs[a].callx) for a in seen
                          if funcs[a].callx)
        called_rom = sorted({r for a in seen for r in funcs[a].rom})
        stray = sum(funcs[a].stray for a in seen)
        lost = sorted((funcs[a].name, funcs[a].lost) for a in seen
                      if funcs[a].lost)
        tails = sum(funcs[a].tails for a in seen)
        no_entry = sorted(funcs[a].name for a in seen if funcs[a].frame == 0)
        limit = stack - MARGIN
        over = depth > limit
        if over:
            fails.append(f"{name}: {depth} bytes deep, over its limit of "
                         f"{limit} ({stack} from {src} less {MARGIN})")
        unknown = []
        if indirect:
            unknown.append(f"{sum(n for _, n in indirect)} calls through a "
                           f"pointer in {len(indirect)} functions")
        if called_rom:
            unknown.append(f"{len(called_rom)} ROM functions")
        if stray:
            unknown.append(f"{stray} calls or tail jumps to no function's "
                           f"start")
        if no_entry:
            unknown.append(f"{len(no_entry)} functions with no entry, frame "
                           f"unknown")
        if lost:
            unknown.append(f"{sum(n for _, n in lost)} branch targets not "
                           f"decoded in {len(lost)} functions")
        if cycles:
            unknown.append(f"{len(cycles)} cycles, each counted once")
        if over:
            result = "OVER"
        elif unknown:
            result = "within, as a lower bound"
        else:
            result = "within"
        print(f"{name:<8} {entry:<14} {stack:>6} {limit:>6} {depth:>6} "
              f"{limit - depth:>6}  {result}")
        measured[name] = (stack, depth, limit - depth)
        if entry == MAIN_TASK:
            pointer_calls = sum(n for _, n in indirect)
        if unknown:
            print(f"{'':<8} not followed: " + "; ".join(unknown))
        if tails:
            print(f"{'':<8} followed as calls: {tails} tail jumps")
        detail.append((name, depth, chain, indirect, called_rom, stray,
                       lost, cycles, no_entry))

    if args.verbose:
        for (name, depth, chain, indirect, called_rom, stray, lost,
             cycles, no_entry) in detail:
            print(f"\n== {name}: {depth} bytes; frame, depth from here, "
                  f"function")
            for own, total, addr in chain:
                print(f"   {own:5d} {total:6d}  {g.label(addr)}")
            if indirect:
                print("   calls through a pointer, not followed:")
                for fn, n in indirect:
                    print(f"     {fn} x{n}")
            if called_rom:
                print("   ROM functions, frame unknown:")
                print("     " + ", ".join(called_rom))
            if stray:
                print(f"   {stray} calls or tail jumps to an address no "
                      f"function starts at")
            if no_entry:
                print("   functions with no entry, frame unknown:")
                print("     " + ", ".join(no_entry))
            if lost:
                print("   branch targets the decode does not land on:")
                for fn, n in lost:
                    print(f"     {fn} x{n}")
            if cycles:
                print("   cycles, each counted once:")
                for c in cycles:
                    print(f"     {c}")

    if args.check_doc:
        fails += check_panel_doc(measured, pointer_calls)

    for f in fails:
        print("FAIL " + f)
    print(f"{len(fails)} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
