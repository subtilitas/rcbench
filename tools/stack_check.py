#!/usr/bin/env python3
"""Hold the panel's task stacks to their deepest call chain.

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
- a call to an address where no function starts;
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
build's config/sdkconfig.h; every other task's is the size
firmware/panel/main/main.c passes to xTaskCreatePinnedToCore().  The check
fails when a task's depth exceeds its stack less the margin, when a screen
table cannot be read, or when a router function in SCREEN_CALLS makes no
indirect call.

    tools/stack_check.py [BUILD]        check; BUILD defaults to
                                        firmware/panel/build
    tools/stack_check.py [BUILD] -v     and print each task's deepest chain
                                        and every call not followed
"""

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN_C = ROOT / "firmware" / "panel" / "main" / "main.c"
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
    "end" (nothing falls through), or None."""
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
            if m == 2:
                return ("end", None)                   # ret, retw, jx
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
        if kind in ("jump", "end"):
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
                 "lost")

    def __init__(self, addr: int, name: str) -> None:
        self.addr = addr
        self.name = name
        self.frame = 0
        self.calls = set()      # addresses called
        self.callx = 0          # calls through a register not followed
        self.rom = set()        # ROM functions called, by name
        self.stray = 0          # calls to an address no function starts at
        self.lost = 0           # branch targets the decode does not land on


def analyse(elf: Elf, addr: int, size: int, name: str) -> Func:
    f = Func(addr, name)
    code = elf.read(addr, size)
    if code is None:
        return f
    # A second pass knows the targets of backward branches; more settle a
    # restart that moved a target.
    targets = set()
    for _ in range(6):
        starts, found = sweep(code, addr, targets)
        found = {a for a in found if addr <= a < addr + size}
        if found == targets:
            break
        targets = found
    at = {pc for pc, _, _ in starts}
    f.lost = len(found - at)
    targets = found
    regs = {}        # register -> address an l32r loaded, this block only
    for pc, w, n in starts:
        if pc in targets:
            regs = {}
        kind, arg = decode(pc, w, n)
        if kind == "entry" and pc == addr:
            f.frame = arg
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

def task_stacks() -> list:
    """(entry function, task name, stack bytes) from main.c."""
    text = MAIN_C.read_text(encoding="utf-8")
    pat = (r"xTaskCreatePinnedToCore\(\s*(\w+)\s*,\s*\"([^\"]+)\"\s*,"
           r"\s*(\d+)\s*,")
    return [(m.group(1), m.group(2), int(m.group(3)))
            for m in re.finditer(pat, text)]


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


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", nargs="?",
                    default=str(ROOT / "firmware" / "panel" / "build"))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    build = Path(args.build)
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
    tasks += [(fn, name, size, "main.c")
              for fn, name, size in task_stacks()]

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
            unknown.append(f"{stray} calls to no function's start")
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
        if unknown:
            print(f"{'':<8} not followed: " + "; ".join(unknown))
        detail.append((name, depth, chain, indirect, called_rom, stray,
                       lost, cycles))

    if args.verbose:
        for (name, depth, chain, indirect, called_rom, stray, lost,
             cycles) in detail:
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
                print(f"   {stray} calls to an address no function starts "
                      f"at")
            if lost:
                print("   branch targets the decode does not land on:")
                for fn, n in lost:
                    print(f"     {fn} x{n}")
            if cycles:
                print("   cycles, each counted once:")
                for c in cycles:
                    print(f"     {c}")

    for f in fails:
        print("FAIL " + f)
    print(f"{len(fails)} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
