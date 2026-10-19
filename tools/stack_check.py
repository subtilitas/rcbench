#!/usr/bin/env python3
"""Hold the panel's task stacks, and the IOmcu's two, to their
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
  chain runs round a cycle at most once, and the depth is the deepest of
  every chain with no function on it twice (Graph in tools/stack_elf.py).

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

The IOmcu.  With --iomcu the tool reads the RP2350 image instead.
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
                                        the IOmcu's two cores; BUILD
                                        defaults to firmware/iomcu/build
    tools/stack_check.py [BUILD] --check-doc
                                        and hold the panel's table in
                                        docs/Performance.md and its German
                                        page to this build
    tools/stack_check.py --iomcu [BUILD] --check-doc
                                        and hold core 0's row of the
                                        IOmcu table in the same pages,
                                        its count of calls through a
                                        register there, and core 0's
                                        sentence in STATUS.md

The IOmcu's half is tools/stack_iomcu.py.  The ELF reader and the
call graph both halves use are tools/stack_elf.py.

SPDX-License-Identifier: MIT
"""

import argparse
import re
import struct
import sys
from pathlib import Path

from stack_elf import (ROOT, STT_FUNC, STT_OBJECT, Elf, Graph, call_args,
                       die, doc_rows, sext)
from stack_iomcu import IOMCU_DIR, IOMCU_ELF, check_iomcu_doc, iomcu_check

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
    "router_hand": ("event",),
    "router_back": ("back", "hold"),
    "ui_router_cancel_gestures": ("cancel",),
    "ui_router_render": ("render",),
}

# The link_port_t callbacks shared/link calls: the functions the panel puts
# in its two ports, k_port and the IOmcu update's k_cu_port, whose
# exchange, cu_port_exchange(), asks for the bus back and then calls
# exchange().  A request to the IOmcu waits in exchange(), which runs
# the safety loop, so this is the control task's deepest chain and it is
# followed by name.  A function listed here that makes no indirect call, or
# a callback that is not one function in the ELF, fails the check.
# test/tools/test_stack_check.py holds the table to every link_port_t in
# firmware/panel.
PORT_CALLS = {
    "link_write_acked": ("port_exchange", "cu_port_exchange", "port_now",
                         "port_between"),
    "link_read_window": ("port_exchange", "cu_port_exchange", "port_now"),
}

# The art_flash_t callbacks shared/artwork calls: the panel's one flash, in
# components/art_flash.  The drawing's keep takes a sector a pass on the
# control task, so its erase and write chain is followed by name.  The same
# checks as PORT_CALLS.
FLASH_CALLS = {
    "art_keep_step": ("part_read", "part_erase", "part_write"),
    "art_store_read": ("part_read",),
}

# The servo_link_ctx_t callbacks shared/outputs/servo_link.c calls: the
# SERVO screen's link on the control task, whose callbacks reach the
# panel's output bank, arming policy and alerts in main.c.  Each callback is
# called from one function of its own there.  The same checks as
# PORT_CALLS; test/tools/test_stack_check.py holds the table to
# servo_link.c and to the panel's k_servo_ctx.
SERVO_CALLS = {
    "sl_now_ms": ("port_now",),
    "sl_armed": ("servo_ctx_armed",),
    "sl_stopped": ("servo_ctx_stopped",),
    "sl_alert": ("servo_ctx_alert",),
    "sl_disarm": ("servo_ctx_disarm",),
    "sl_stop_count": ("servo_ctx_stop_count",),
    "sl_arm_surfaces": ("servo_ctx_arm_surfaces",),
}

# The prog_page_set_t hooks of the programmers' registry
# (shared/ui/prog_registry.h): the sessions SERVO and MOTOR & ESC open from
# their PROGRAM lists, on the main task.  Each hook is called from one
# function of its own, in shared/ui/prog_registry.c, and every page set's
# function for that hook is named here.  The same checks as PORT_CALLS;
# test/tools/test_stack_check.py holds the table to those callers and to
# every page set in shared/ui.
PROG_CALLS = {
    "prog_ps_init": ("kst_panel_init", "esc_stick_ps_init"),
    "prog_ps_entry": ("kst_panel_screen_entry", "esc_stick_ps_entry"),
    "prog_ps_open": ("kst_panel_open", "esc_stick_ps_open"),
    "prog_ps_is_open": ("kst_panel_is_open", "esc_stick_ps_is_open"),
    "prog_ps_back": ("kst_panel_back", "esc_stick_ps_back"),
    "prog_ps_holds": ("kst_panel_holds_port", "esc_stick_ps_holds"),
    "prog_ps_armed": ("kst_panel_bench_armed", "esc_stick_ps_armed"),
    "prog_ps_tick": ("kst_panel_tick", "esc_stick_ps_tick"),
    "prog_ps_has_press": ("kst_panel_has_press", "esc_stick_ps_has_press"),
    "prog_ps_event": ("kst_panel_event", "esc_stick_ps_event"),
    "prog_ps_render": ("kst_panel_render", "esc_stick_ps_render"),
    "prog_ps_revision": ("kst_panel_revision", "esc_stick_ps_revision"),
    "prog_ps_hold_revision": ("kst_panel_hold_revision",
                             "esc_stick_ps_hold_revision"),
    "prog_ps_hold_rect": ("kst_panel_apply_rect", "esc_stick_ps_hold_rect"),
    "prog_ps_leave": ("kst_panel_leave", "esc_stick_ps_leave"),
    "prog_ps_cancel": ("kst_panel_cancel", "esc_stick_ps_cancel"),
    "prog_ps_pin": ("kst_panel_pin", "esc_stick_ps_pin"),
}

# The card callbacks of shared/logfile, on the runlog task: the files of a
# run, of the fault log and of the servo programmer are opened through
# them, so each chain into FatFs (mkdir, stat, the directory walk, fopen,
# rename) is followed by name.  The same checks as PORT_CALLS;
# test/tools/test_stack_check.py holds each table to its source and to the
# functions the panel hands over.
#
# log_take_io_t (log_take.c): log_take() makes 4 of the calls, its helpers
# inlined; take_level() hands a folder level to make_dir().
TAKE_CALLS = {
    "log_take": ("take_mounted", "take_walk", "take_exists", "take_create"),
    "take_level": ("take_make_dir",),
}

# card_make_fn (card_tree.c): every function a caller hands
# card_make_dirs().
CARD_CALLS = {
    "card_make_dirs": ("take_level", "fault_level", "panel_card_make_dir"),
}

# fault_log_io_t (fault_log.c), the fault log of panel_log.c.
FAULT_CALLS = {
    "fault_log_note": ("note_mounted", "note_size", "note_remove",
                       "note_rename", "note_append"),
    "fault_level": ("panel_card_make_dir",),
}

# kst_store_io_t (kst_store.c), the servo files of kst_files.c.
KST_CALLS = {
    "kst_store_preset": ("io_mounted", "io_walk"),
    "kst_store_save": ("io_mounted", "io_walk", "io_create"),
    "kst_store_list_after": ("io_mounted", "io_walk"),
    "kst_store_load": ("io_mounted", "io_size", "io_read"),
}

# The fw_boot_ops_t callbacks shared/fwupdate calls: the panel's start
# check after an update, in components/fw_ota, which writes otadata on the
# main task.  The same checks as PORT_CALLS.
BOOT_CALLS = {
    "fw_boot_decide": ("boot_pending", "boot_mark_invalid", "boot_mark_valid",
                       "boot_restart", "boot_last_invalid"),
}


# ------------------------------------------------------------ the decoder --

def length(b0: int) -> int:
    """An ESP32-S3 instruction's length from its first byte: op0 0 to 7 is
    24 bits, 8 to 13 the 16-bit density forms, 14 and 15 the 32-bit PIE
    forms."""
    op0 = b0 & 0xF
    return 3 if op0 < 8 else (2 if op0 < 14 else 4)


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

def in_build_dir(rel: Path) -> bool:
    """Whether @p rel, relative to firmware/panel, lies in a build tree:
    build/, or a second one such as build-inject/ (docs/Building.md)."""
    return any(p == "build" or p.startswith("build-") for p in rel.parts)


def task_stacks() -> tuple:
    """(entry function, task name, stack bytes, where) for every task the
    panel's own code creates, and the errors: a creation whose entry, name
    or stack size is not a plain identifier, string or number (or a macro
    defined as one in the same file) fails the check rather than being
    skipped."""
    tasks, errors = [], []
    for src in sorted(PANEL_DIR.rglob("*.c")):
        if in_build_dir(src.relative_to(PANEL_DIR)):
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


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", nargs="?")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--iomcu", action="store_true",
                    help="BUILD is an IOmcu build: check its two cores")
    ap.add_argument("--check-doc", action="store_true",
                    help="also fail when the panel's task table, or with "
                         "--iomcu core 0's row of the IOmcu table and "
                         "its count of calls through a register, in "
                         "docs/Performance.md or docs/Performance-de.md, or "
                         "core 0's sentence in STATUS.md, differs from this "
                         "build")
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

    for table, home, calls in (("PORT_CALLS", "shared/link", PORT_CALLS),
                               ("FLASH_CALLS", "shared/artwork",
                                FLASH_CALLS),
                               ("BOOT_CALLS", "shared/fwupdate",
                                BOOT_CALLS),
                               ("SERVO_CALLS", "shared/outputs",
                                SERVO_CALLS),
                               ("PROG_CALLS", "shared/ui", PROG_CALLS),
                               ("TAKE_CALLS", "shared/logfile",
                                TAKE_CALLS),
                               ("CARD_CALLS", "shared/logfile",
                                CARD_CALLS),
                               ("FAULT_CALLS", "shared/logfile",
                                FAULT_CALLS),
                               ("KST_CALLS", "shared/logfile", KST_CALLS)):
        for caller, callbacks in calls.items():
            addrs = by_name.get(caller, [])
            if len(addrs) != 1:
                fails.append(f"{table}: {caller} is {len(addrs)} functions "
                             f"in the ELF, not 1")
                continue
            f = funcs[addrs[0]]
            if f.callx == 0:
                fails.append(f"{table}: {caller} makes no indirect call; "
                             f"the table no longer matches {home}")
                continue
            for cb in callbacks:
                targets = by_name.get(cb, [])
                if len(targets) != 1:
                    fails.append(f"{table}: callback {cb} is "
                                 f"{len(targets)} functions in the ELF, "
                                 f"not 1")
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
        depth, chain = g.depth(addrs[0])
        seen = g.reach(addrs[0])
        cycles = g.recursion(seen)
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
            unknown.append(f"{len(cycles)} recursive groups, each function "
                           f"entered once per chain")
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
                print("   recursive groups, each function entered once per "
                      "chain:")
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
