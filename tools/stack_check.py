#!/usr/bin/env python3
"""Hold the panel's task stacks, and the IOmcu's two, to their
deepest call chain.

A task that runs past the end of its stack trips FreeRTOS's stack check and
restarts the panel.  Neither the host build nor the size report shows how
deep a task goes, so this reads it out of the firmware the panel runs.

The panel is an ESP32-P4: RISC-V code.  tools/stack_riscv.py decodes each
function from its symbol's address to its end: the frame is the lowest
point its stack pointer reaches, and the calls are its jal, its auipc and
jalr pairs, and its jumps out of the function.  A task's depth is the
largest sum of frames along any chain of calls from its entry point.  On
one build at ESP-IDF v5.5.5 the decoder finds every one of the 11,903
direct calls objdump lists in the panel's functions, and the frame of
every function but two equals its largest `addi sp, sp, -N`: one sizes a
buffer at run time, and one opens a second frame after an early return,
which the decoder counts on top of the first.

Calls the tool follows:

- direct calls and the far calls of an auipc and jalr pair;
- tail calls: a jump or a branch out of the function, each counted as a
  call (see stack_riscv.py for why that is the safe sum);
- the router's calls into a screen: ui_router.c calls the screen on top
  through its ui_screen_t, and SCREEN_CALLS below names the callbacks each
  router function reaches.  The callbacks themselves are read out of every
  screen's table in the ELF, or out of the table's source where the
  compiler folded the table into code;
- the callbacks the tables below name: the link port's, the drawing store's,
  the start check's, the SERVO screen link's, the programmers' and the card
  functions'.

What it cannot follow it counts and names, and never drops quietly:

- any other call through a function pointer;
- a jump through a register: a switch table, which stays inside the
  function, or a tail call through a pointer;
- a call into the ESP32-P4's ROM (read-only memory), which is not in the
  ELF, so its frame is unknown;
- a call or tail jump to an address where no function starts, such as a
  hand-written function with no size in the symbol table;
- a function that moves its stack pointer by an amount known only at run
  time, whose frame is the part the constants give;
- recursion: a function already on the chain is not entered again, so a
  chain runs round a cycle at most once, and the depth is the deepest of
  every chain with no function on it twice (Graph in tools/stack_elf.py).

A task with any of these has a depth that is a lower bound, and the result
says so.  The margin is what covers them.

The margin is 1024 bytes.  OUTSIDE_FRAMES of it every task spends outside
its frames on the ESP32-P4 under ESP-IDF v5.5.5 (rvruntime-frames.h and
port.c of the RISC-V port):

- 128 for the registers an interrupt saves on the task's stack before it
  moves to the interrupt stack (SAVE_REGS, 32 words, in vectors.S);
- 32 for the coprocessor save area's header at the top of the stack
  (RvCoprocSaveArea, 24 bytes, aligned down to 16);
- 384 for the coprocessor contexts the port takes from the bottom of the
  stack the first time a task uses the unit: the FPU (floating-point unit)
  132, the hardware loops 24, the vector unit 216, and up to 12 of
  alignment;
- 16 for FreeRTOS's canary at the bottom of the stack
  (CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY).

The thread-local storage at the top of each stack is read from the ELF
(_thread_local_*) and charged on top; the panel's build has none.  The rest
of the margin is for what the graph does not follow.

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
                       die, doc_rows)
from stack_iomcu import IOMCU_DIR, IOMCU_ELF, check_iomcu_doc, iomcu_check
from stack_riscv import EM_RISCV, analyse_riscv

PANEL_DIR = ROOT / "firmware" / "panel"
# The FreeRTOS calls that create a task with a stack from the heap; the
# entry, the name and the stack size are their first three arguments.
TASK_CREATE = ("xTaskCreatePinnedToCore", "xTaskCreate")
SCREEN_H = ROOT / "shared" / "ui" / "include" / "ui_screen.h"
UI_DIR = ROOT / "shared" / "ui"
ELF_NAME = "rcbench-panel.elf"
# The ESP32-P4's mask ROM, where a call leaves the ELF
# (SOC_IROM_MASK_LOW and _HIGH in ESP-IDF's soc.h).
ROM_LO, ROM_HI = 0x4FC00000, 0x4FC20000

MARGIN = 1024
# The interrupt's saved registers, the coprocessor save area's header, the
# three coprocessor contexts with their alignment, and the canary; the
# module docstring derives each.
OUTSIDE_FRAMES = 128 + 32 + (132 + 24 + 216 + 12) + 16
# The linker's bounds of the thread-local storage each stack carries.
TLS_SYMBOLS = ("_thread_local_data_start", "_thread_local_data_end",
               "_thread_local_bss_start", "_thread_local_bss_end")

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
    "router_safe_at": ("safe_at",),
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
    "prog_ps_safe_at": ("kst_panel_safe_at", "esc_stick_ps_safe_at"),
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
    """Whether @p rel, relative to firmware/panel, is not the panel's own
    source.

    Args:
        rel: A path relative to firmware/panel.

    Returns:
        True inside a build tree, build/ or a second one such as
        build-noinject/ (docs/Building.md), and inside managed_components/,
        where the build puts the components it fetches from the registry:
        their tasks are ESP-IDF's kind, created by code the panel does not
        own, and not in the table.
    """
    return any(p == "build" or p.startswith("build-")
               or p == "managed_components" for p in rel.parts)


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


def tls_bytes(elf: Elf) -> int:
    """The thread-local storage the port puts at the top of every stack.

    Args:
        elf: The panel image.

    Returns:
        The data and bss between the linker's _thread_local_* bounds,
        rounded up to 16 bytes as port.c rounds them.

    Raises:
        SystemExit: A bound is not in the ELF.
    """
    at = {s[3]: s[0] for s in elf.symbols if s[3] in TLS_SYMBOLS}
    missing = [n for n in TLS_SYMBOLS if n not in at]
    if missing:
        die(f"no {', '.join(missing)} in the ELF")
    size = (at["_thread_local_data_end"] - at["_thread_local_data_start"]
            + at["_thread_local_bss_end"] - at["_thread_local_bss_start"])
    return (size + 15) & ~15


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


class Reach:
    """What a task's call graph holds that the depth does not follow.

    Attributes:
        indirect: (function, count) of the calls through a pointer.
        jumps: (function, count) of the jumps through a register.
        rom: The ROM functions called, by name.
        stray: The calls or tail jumps to no function's start.
        dynamic: The functions that size a frame at run time.
        cycles: The recursive groups, one line each.
        tails: The jumps out of a function, followed as calls.
    """

    def __init__(self, funcs: dict, seen: set, cycles: list) -> None:
        """Collect what @p seen, the functions a task reaches, holds.

        Args:
            funcs: Every function, by address.
            seen: The addresses the task reaches.
            cycles: Graph.recursion() of @p seen.
        """
        self.indirect = sorted((funcs[a].name, funcs[a].callx) for a in seen
                               if funcs[a].callx)
        self.jumps = sorted((funcs[a].name, funcs[a].jumpx) for a in seen
                            if funcs[a].jumpx)
        self.rom = sorted({r for a in seen for r in funcs[a].rom})
        self.stray = sum(funcs[a].stray for a in seen)
        self.dynamic = sorted(funcs[a].name for a in seen
                              if funcs[a].dynamic)
        self.cycles = cycles
        self.tails = sum(funcs[a].tails for a in seen)

    def unknown(self) -> list:
        """One phrase for each kind of thing not followed.

        Returns:
            The phrases; empty when the depth is exact.
        """
        out = []
        if self.indirect:
            out.append(f"{sum(n for _, n in self.indirect)} calls through a "
                       f"pointer in {len(self.indirect)} functions")
        if self.jumps:
            out.append(f"{sum(n for _, n in self.jumps)} jumps through a "
                       f"register in {len(self.jumps)} functions")
        if self.rom:
            out.append(f"{len(self.rom)} ROM functions")
        if self.stray:
            out.append(f"{self.stray} calls or tail jumps to no function's "
                       f"start")
        if self.dynamic:
            out.append(f"{len(self.dynamic)} functions sizing a frame at "
                       f"run time")
        if self.cycles:
            out.append(f"{len(self.cycles)} recursive groups, each function "
                       f"entered once per chain")
        return out

    def print_detail(self) -> None:
        """Print every item unknown() counts, by name."""
        if self.indirect:
            print("   calls through a pointer, not followed:")
            for fn, n in self.indirect:
                print(f"     {fn} x{n}")
        if self.jumps:
            print("   jumps through a register (switch tables or tail calls "
                  "through a pointer), not followed:")
            for fn, n in self.jumps:
                print(f"     {fn} x{n}")
        if self.rom:
            print("   ROM functions, frame unknown:")
            print("     " + ", ".join(self.rom))
        if self.stray:
            print(f"   {self.stray} calls or tail jumps to an address no "
                  f"function starts at")
        if self.dynamic:
            print("   functions sizing a frame at run time, frame counted "
                  "in part:")
            print("     " + ", ".join(self.dynamic))
        if self.cycles:
            print("   recursive groups, each function entered once per "
                  "chain:")
            for c in self.cycles:
                print(f"     {c}")


def main() -> int:
    """Run the check the command line names.

    Returns:
        0 when every stack holds its deepest chain, 1 when one does not.

    Raises:
        SystemExit: The build is missing, or the panel ELF is not RISC-V.
    """
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
    if elf.machine != EM_RISCV:
        die(f"{path} is ELF machine {elf.machine}, not RISC-V ({EM_RISCV}): "
            "the panel decoder reads the ESP32-P4's RISC-V code only")
    outside = OUTSIDE_FRAMES + tls_bytes(elf)

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
    funcs = {a: analyse_riscv(elf, a, n, names[a]) for a, n in sizes.items()
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
        if f.callx + f.jumpx == 0:
            fails.append(f"SCREEN_CALLS: {router} makes no indirect call; "
                         f"the table no longer matches the router")
            continue
        for slot in slots:
            f.calls |= tables.get(slot, set())
        f.callx = f.jumpx = 0

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
            if f.callx + f.jumpx == 0:
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
            f.callx = f.jumpx = 0

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
    tls = outside - OUTSIDE_FRAMES
    print(f"stack_check: {path}")
    src_note = (f", {len(from_src)} from source ({', '.join(from_src)})"
                if from_src else "")
    print(f"screen tables: {from_elf} from the ELF{src_note}")
    print(f"margin: {MARGIN} bytes, {OUTSIDE_FRAMES} of them outside the "
          f"frames and {MARGIN - OUTSIDE_FRAMES} for what is not followed; "
          f"{tls} of thread-local storage on top")
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
        r = Reach(funcs, seen, g.recursion(seen))
        limit = stack - MARGIN - tls
        over = depth > limit
        if over:
            fails.append(f"{name}: {depth} bytes deep, over its limit of "
                         f"{limit} ({stack} from {src} less {MARGIN} and "
                         f"{tls} of thread-local storage)")
        unknown = r.unknown()
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
            pointer_calls = sum(n for _, n in r.indirect)
        if unknown:
            print(f"{'':<8} not followed: " + "; ".join(unknown))
        if r.tails:
            print(f"{'':<8} followed as calls: {r.tails} tail jumps")
        detail.append((name, depth, chain, r))

    if args.verbose:
        for name, depth, chain, r in detail:
            print(f"\n== {name}: {depth} bytes; frame, depth from here, "
                  f"function")
            for own, total, addr in chain:
                print(f"   {own:5d} {total:6d}  {g.label(addr)}")
            r.print_detail()

    if args.check_doc:
        fails += check_panel_doc(measured, pointer_calls)

    for f in fails:
        print("FAIL " + f)
    print(f"{len(fails)} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
