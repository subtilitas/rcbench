#!/usr/bin/env python3
"""Hold the panel's internal RAM to what its Wi-Fi start needs.

The ESP32-S3's Wi-Fi driver takes its static RX and TX buffers at
esp_wifi_init(): CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM and
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM buffers of about 1600 bytes each, all
DMA-capable (direct memory access) internal RAM, 51,200 bytes with 16 of
each.  With CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP the TX buffers are static
by force.  A panel whose internal RAM cannot hold them refuses ACCESS
POINT at once: v0.19.0 left about 20 KB.

The tool reads the internal heap the build leaves and subtracts what the
panel allocates at run time, task by task and buffer by buffer, then what
each Wi-Fi mode allocates on top.

The heap.  Internal RAM from the end of the static data, `_heap_start` in
the ELF (Executable and Linkable Format) file, to 0x3FCE9710, where the
ROM's (read-only memory's) stack starts; and 0x3FCE9710 to 0x3FCF0000, the
ROM's region, which the heap takes over once the application starts
(ESP-IDF's components/heap/port/esp32s3/memory_layout.c, v5.4 and v5.5).
RTC (real-time clock) fast memory, about 8 KB, is heap as well and is left
out: it is not DMA-capable, and leaving it out errs low.

The allocations.  Read from the build where the build says them: every
task stack from its creation in firmware/panel (stack_check.py's reader)
and ESP-IDF's own from config/sdkconfig.h; every queue from its
xQueueCreate() and every semaphore from its xSemaphoreCreate*(); the
Wi-Fi buffers from sdkconfig.h; the display's bounce buffers from
DISPLAY_CONFIG_DEFAULT and the board's line width.  The sizes of a task's
control block, a queue's header and each queue's item are sizeof() values
read with GDB from the ESP-IDF v5.4 build (SIZEOF); a queue whose item
type is not in the table fails the check.  What the build does not say is
an allowance, named as one, and not measured: the drivers' start-up
allocations, the Wi-Fi driver's beyond its buffers and its task, and TLS
(transport layer security).  The free heap on a panel is not measured.

The check fails when the heap left with the largest Wi-Fi mode up is
under FLOOR: 32,768 bytes, the size of the pool ESP-IDF keeps for internal
and DMA allocations at run time (CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL).
That is what the radio's sockets, the TLS engine's DMA buffers and a card
transfer take from while the radio runs, and what an allowance that is
short by takes from.

    tools/ram_budget.py [BUILD]               the budget; fails under the
                                              floor.  BUILD defaults to
                                              firmware/panel/build
    tools/ram_budget.py [BUILD] --check-doc   and hold the table in
                                              docs/Performance.md and its
                                              German page to this build

SPDX-License-Identifier: MIT
"""

import argparse
import re
import sys
from pathlib import Path

import stack_check

ROOT = Path(__file__).resolve().parent.parent
PANEL_DIR = ROOT / "firmware" / "panel"
ELF_NAME = "rcbench-panel.elf"

# ESP32-S3 internal data RAM: the start, the ROM stack's low end
# (SOC_ROM_STACK_START - SOC_ROM_STACK_SIZE) and the end.
DRAM_LOW = 0x3FC88000
HEAP_END = 0x3FCEB710 - 0x2000
DRAM_HIGH = 0x3FCF0000

FLOOR = 32768

# sizeof() on the panel build, ESP-IDF v5.4, read with GDB.
TCB = 340                         # StaticTask_t
QUEUE = 84                        # StaticQueue_t, a semaphore's as well
SIZEOF = {
    "touch_item_t": 24,
    "panel_cmd_t": 108,
    "kst_prog_cmd_t": 72,
    "bus_link_cmd_t": 36,
    "bench_state_t": 92,
    "supply_item_t": 84,
    "servo_test_enc_t": 16,
    "servo_win_item_t": 44,
    "log_row_t": 104,
    "test_line_t": 225,
    "queued_t": 20,
}
# A queue length that is a configuration field, not a number in the file.
QUEUE_LENGTHS = {
    "s_touch.cfg.event_queue_len": 32,     # TOUCH_CONFIG_DEFAULT
}

# The Wi-Fi driver.  A static buffer is "approximately 1.6KB" in ESP-IDF's
# Kconfig help; the driver's block around it is not known.  The Wi-Fi
# task's stack is one of 3072, 3584, 6144 and 6656 bytes in
# config_get_wifi_task_stack_size() (libnet80211.a); the largest is taken.
WIFI_BUFFER = 1600
WIFI_TASK = 6656
# struct httpd_data (2800) and four struct sock_db (192 each), sizeof()
# as above, and the server's URI (uniform resource identifier) table.
HTTPD_DATA = 2800 + 4 * 192 + 528

# Allowances: not read from the build, not measured.
BOOT_ALLOWANCE = 12288    # TWAI, I2C, SPI and card, NVS, the RGB panel's
#                           object and DMA lists, interrupt and heap
#                           bookkeeping
WIFI_ALLOWANCE = 16384    # the Wi-Fi driver's other blocks: management
#                           buffers, PHY calibration, its queues
TLS_ALLOWANCE = 4096      # the hardware AES and SHA engines' DMA buffers
#                           and esp_http_client's two 512-byte buffers;
#                           mbedTLS itself is in PSRAM

# The tasks the panel's code creates, by when they exist.
BOOT_TASKS = ("control", "runlog", "knob", "touch")
WIFI_TASKS = ("wifi_ctl", "mdns", "wifi_get")

PERFORMANCE = (
    (ROOT / "docs" / "Performance.md", "| Item | What it is | Bytes |"),
    (ROOT / "docs" / "Performance-de.md", "| Posten | Was es ist | Bytes |"),
)


def die(msg: str) -> None:
    sys.exit(f"ram_budget: {msg}")


def sdkconfig(build: Path) -> dict:
    """CONFIG_ name -> value: a number, a string, or 1 for a bool."""
    hdr = build / "config" / "sdkconfig.h"
    if not hdr.exists():
        die(f"{hdr} not found; is {build} an ESP-IDF build directory?")
    out = {}
    for name, value in re.findall(r"^#define (CONFIG_\w+) (.+)$",
                                  hdr.read_text(encoding="utf-8"), re.M):
        value = value.strip()
        if re.fullmatch(r"-?\d+", value):
            out[name] = int(value)
        elif re.fullmatch(r"0x[0-9a-fA-F]+", value):
            out[name] = int(value, 16)
        else:
            out[name] = value
    return out


def need(cfg: dict, name: str) -> int:
    if name not in cfg or not isinstance(cfg[name], int):
        die(f"{name} is not a number in the build's sdkconfig.h")
    return cfg[name]


def symbol(elf: stack_check.Elf, name: str) -> int:
    for addr, _, _, sym, _, _ in elf.symbols:
        if sym == name:
            return addr
    die(f"no symbol {name} in the ELF")
    return 0


def source_text(src: Path) -> str:
    """The file with its comments blanked, line breaks kept."""
    raw = src.read_text(encoding="utf-8")
    return re.sub(r"/\*.*?\*/|//[^\n]*",
                  lambda m: re.sub(r"[^\n]", " ", m.group(0)), raw,
                  flags=re.S)


def queues() -> tuple:
    """(label, bytes, how) for every queue and semaphore firmware/panel
    creates, and the errors: a length or an item size the tool cannot
    read fails rather than being guessed."""
    rows, errors = [], []
    for src in sorted(PANEL_DIR.rglob("*.c")):
        if "build" in src.relative_to(PANEL_DIR).parts:
            continue
        text = source_text(src)
        macros = dict(re.findall(r"^\s*#\s*define\s+(\w+)\s+\(?\s*(\d+)u?"
                                 r"\s*\)?\s*$", text, re.M))
        for m in re.finditer(r"\b(xQueueCreate\w*|xSemaphoreCreate\w*)\s*\(",
                             text):
            line = text.count("\n", 0, m.start()) + 1
            where = f"{src.relative_to(ROOT)}:{line}"
            fn = m.group(1)
            if fn.startswith("xSemaphoreCreate"):
                if fn not in ("xSemaphoreCreateMutex",
                              "xSemaphoreCreateBinary"):
                    errors.append(f"{where}: {fn}() is not one this tool "
                                  f"reads")
                    continue
                rows.append((f"semaphore, {where}", QUEUE,
                             "StaticSemaphore_t"))
                continue
            if fn != "xQueueCreate":
                errors.append(f"{where}: {fn}() is not one this tool reads")
                continue
            args = stack_check.call_args(text, m.end() - 1)
            if args is None or len(args) != 2:
                errors.append(f"{where}: xQueueCreate() not parsed")
                continue
            length, item = args
            length = re.sub(r"\s+", "", length)
            if length in QUEUE_LENGTHS:
                n = QUEUE_LENGTHS[length]
            else:
                lm = re.fullmatch(r"(\d+)u?", macros.get(length, length))
                if lm is None:
                    errors.append(f"{where}: queue length {length!r} is "
                                  f"not a number; add it to QUEUE_LENGTHS")
                    continue
                n = int(lm.group(1))
            sm = re.fullmatch(r"sizeof\s*\(\s*(\w+)\s*\)", item)
            if sm is not None:
                if sm.group(1) not in SIZEOF:
                    errors.append(f"{where}: sizeof({sm.group(1)}) is not "
                                  f"in SIZEOF")
                    continue
                size = SIZEOF[sm.group(1)]
            else:
                im = re.fullmatch(r"(\d+)u?", macros.get(item, item))
                if im is None:
                    errors.append(f"{where}: item size {item!r} is not "
                                  f"read")
                    continue
                size = int(im.group(1))
            rows.append((f"queue {n} x {size}, {where}", QUEUE + n * size,
                         "xQueueCreate"))
    return rows, errors


def bounce_buffers() -> int:
    """The RGB panel's two bounce buffers: DISPLAY_CONFIG_DEFAULT's lines
    of the board's width, 2 bytes a pixel."""
    disp = (PANEL_DIR / "components" / "display" / "include"
            / "display.h").read_text(encoding="utf-8")
    pins = (PANEL_DIR / "components" / "board" / "include"
            / "board_pins.h").read_text(encoding="utf-8")
    lm = re.search(r"\.bounce_buffer_lines\s*=\s*(\d+)", disp)
    wm = re.search(r"#define BOARD_LCD_H_RES\s+(\d+)", pins)
    if lm is None or wm is None:
        die("the bounce buffer's lines or the board's width not found")
    return 2 * int(lm.group(1)) * int(wm.group(1)) * 2


def budget(build: Path) -> tuple:
    """(rows, summary, errors).  rows: (section, label, bytes, how);
    summary: {name: bytes} for the table in the docs."""
    path = build / ELF_NAME
    if not path.exists():
        die(f"{path} not found; build the panel first")
    elf = stack_check.Elf(path)
    cfg = sdkconfig(build)
    heap_start = symbol(elf, "_heap_start")
    if not DRAM_LOW < heap_start < HEAP_END:
        die(f"_heap_start 0x{heap_start:08x} is outside internal RAM")
    errors = []
    rows = []

    static = heap_start - DRAM_LOW
    heap = (HEAP_END - heap_start) + (DRAM_HIGH - HEAP_END)

    # Start-up: ESP-IDF's tasks, the panel's, its queues, the display.
    idf_tasks = (
        ("main", stack_check.main_stack(build),
         "CONFIG_ESP_MAIN_TASK_STACK_SIZE"),
        ("IDLE0", need(cfg, "CONFIG_FREERTOS_IDLE_TASK_STACKSIZE"),
         "CONFIG_FREERTOS_IDLE_TASK_STACKSIZE"),
        ("IDLE1", need(cfg, "CONFIG_FREERTOS_IDLE_TASK_STACKSIZE"),
         "CONFIG_FREERTOS_IDLE_TASK_STACKSIZE"),
        ("ipc0", need(cfg, "CONFIG_ESP_IPC_TASK_STACK_SIZE"),
         "CONFIG_ESP_IPC_TASK_STACK_SIZE"),
        ("ipc1", need(cfg, "CONFIG_ESP_IPC_TASK_STACK_SIZE"),
         "CONFIG_ESP_IPC_TASK_STACK_SIZE"),
        ("esp_timer", need(cfg, "CONFIG_ESP_TIMER_TASK_STACK_SIZE"),
         "CONFIG_ESP_TIMER_TASK_STACK_SIZE"),
        ("Tmr Svc", need(cfg, "CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH"),
         "CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH"),
    )
    for name, stack, how in idf_tasks:
        rows.append(("start-up", f"task {name}", stack + TCB,
                     f"{stack} from {how}, {TCB} control block"))
    found, task_errors = stack_check.task_stacks()
    errors += task_errors
    stacks = {}
    for _, name, size, where in found:
        if name in stacks and stacks[name][0] != size:
            errors.append(f"task {name} is created with {stacks[name][0]} "
                          f"and {size} bytes")
        stacks.setdefault(name, (size, where))
    for name in stacks:
        if name not in BOOT_TASKS + WIFI_TASKS:
            errors.append(f"task {name} is in neither BOOT_TASKS nor "
                          f"WIFI_TASKS")
    for name in BOOT_TASKS + WIFI_TASKS:
        if name not in stacks:
            errors.append(f"task {name} is not created in firmware/panel")
    for name in BOOT_TASKS:
        if name in stacks:
            size, where = stacks[name]
            rows.append(("start-up", f"task {name}", size + TCB,
                         f"{size} from {where}, {TCB} control block"))
    q_rows, q_errors = queues()
    errors += q_errors
    for label, size, how in q_rows:
        rows.append(("start-up", label, size, how))
    rows.append(("start-up", "display bounce buffers", bounce_buffers(),
                 "DISPLAY_CONFIG_DEFAULT, BOARD_LCD_H_RES, 2 bytes a pixel"))
    rows.append(("start-up", "drivers and bookkeeping", BOOT_ALLOWANCE,
                 "allowance, not measured"))
    start_up = sum(r[2] for r in rows if r[0] == "start-up")

    # The Wi-Fi start, by mode.
    def task(name: str) -> tuple:
        size, where = stacks.get(name, (0, "not found"))
        return (f"task {name}", size + TCB,
                f"{size} from {where}, {TCB} control block")

    httpd = re.search(r"#define HTTPD_STACK\s+(\d+)",
                      (PANEL_DIR / "components" / "wifi_access"
                       / "wifi_access.c").read_text(encoding="utf-8"))
    if httpd is None:
        die("HTTPD_STACK not found in wifi_access.c")
    httpd_stack = int(httpd.group(1))
    event = need(cfg, "CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE")
    if cfg.get("CONFIG_LWIP_TCPIP_CORE_LOCKING"):
        event += 2048
    rx = need(cfg, "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM")
    tx = (need(cfg, "CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM")
          if cfg.get("CONFIG_ESP_WIFI_STATIC_TX_BUFFER") else 0)
    tcpip = need(cfg, "CONFIG_LWIP_TCPIP_TASK_STACK_SIZE")
    radio = [
        task("wifi_ctl"),
        ("Wi-Fi static RX buffers", rx * WIFI_BUFFER,
         f"{rx} x {WIFI_BUFFER}, CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM"),
        ("Wi-Fi static TX buffers", tx * WIFI_BUFFER,
         f"{tx} x {WIFI_BUFFER}, CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM"),
        ("task wifi", WIFI_TASK + TCB,
         f"{WIFI_TASK}, the largest config_get_wifi_task_stack_size() "
         f"returns, {TCB} control block"),
        ("Wi-Fi driver, other", WIFI_ALLOWANCE, "allowance, not measured"),
        ("task sys_evt", event + TCB,
         f"{event} from CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE, {TCB} "
         f"control block"),
        ("task tiT", tcpip + TCB,
         f"{tcpip} from CONFIG_LWIP_TCPIP_TASK_STACK_SIZE, {TCB} control "
         f"block"),
    ]
    page = [
        ("task httpd", httpd_stack + TCB,
         f"{httpd_stack} from HTTPD_STACK, {TCB} control block"),
        ("httpd data", HTTPD_DATA, "httpd_data, four sockets, URI table"),
    ]
    fetch = [task("wifi_get"),
             ("TLS", TLS_ALLOWANCE, "allowance, not measured")]
    modes = {
        "AP": radio + page,
        "JOIN": radio + page + [task("mdns")],
        "SCAN": radio,
        "FETCH": radio + fetch,
    }
    for mode, parts in modes.items():
        for label, size, how in parts:
            rows.append((mode, label, size, how))

    needs = {m: sum(p[1] for p in parts) for m, parts in modes.items()}
    largest = max(needs, key=lambda m: needs[m])
    summary = {
        "static": static,
        "heap": heap,
        "start-up": start_up,
        "before": heap - start_up,
        **needs,
        "left": heap - start_up - needs[largest],
    }
    return rows, summary, largest, errors


def fmt(n: int) -> str:
    return f"{n:,}"


def doc_rows(text: str, header: str) -> dict | None:
    """{first cell's quoted name: the number in the last cell} for the
    table under @p header; None without the table.  A number is written
    8,192 in English and 8 192 in German."""
    if header not in text:
        return None
    rows = {}
    for row in text[text.index(header):].split("\n\n", 1)[0].splitlines()[2:]:
        cells = [c.strip() for c in row.strip().strip("|").split("|")]
        name = re.search(r"`([^`]+)`", cells[0])
        digits = re.sub(r"[,\s  ]", "", cells[-1])
        rows[name.group(1) if name else cells[0]] = (
            int(digits) if digits.lstrip("-").isdigit() else None)
    return rows


def check_doc(summary: dict) -> list:
    out = []
    for page, header in PERFORMANCE:
        said = doc_rows(page.read_text(encoding="utf-8"), header)
        if said is None:
            out.append(f"{page.name}: no internal RAM table to hold")
            continue
        for name, want in summary.items():
            if name not in said:
                out.append(f"{page.name}: the internal RAM table has no "
                           f"row for {name}")
            elif said[name] != want:
                out.append(f"{page.name}: the internal RAM table gives "
                           f"{name} as {said[name]}; the build has {want}")
        for name in said:
            if name not in summary:
                out.append(f"{page.name}: the internal RAM table has a row "
                           f"for {name}, which the tool does not compute")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", nargs="?")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every allocation, not only the totals")
    ap.add_argument("--check-doc", action="store_true",
                    help="also fail when the internal RAM table in "
                         "docs/Performance.md or docs/Performance-de.md "
                         "differs from this build")
    args = ap.parse_args()
    build = Path(args.build or PANEL_DIR / "build")
    rows, summary, largest, fails = budget(build)

    print(f"ram_budget: {build / ELF_NAME}")
    print(f"static internal data and code   {fmt(summary['static']):>9}")
    print(f"internal heap at start          {fmt(summary['heap']):>9}")
    section = None
    for sec, label, size, how in rows:
        if sec != section:
            section = sec
            total = (summary["start-up"] if sec == "start-up"
                     else summary[sec])
            print(f"{sec:<31} {fmt(total):>9}")
        if args.verbose:
            print(f"  {label:<44} {fmt(size):>9}  {how}")
    print(f"free before the radio           {fmt(summary['before']):>9}")
    for mode in ("AP", "JOIN", "SCAN", "FETCH"):
        left = summary["before"] - summary[mode]
        print(f"free with {mode:<5} up               {fmt(left):>9}")
    print(f"floor                           {fmt(FLOOR):>9}")
    if summary["left"] < FLOOR:
        fails.append(f"{largest} leaves {summary['left']} bytes of internal "
                     f"heap, under the floor of {FLOOR}")
    if args.check_doc:
        fails += check_doc(summary)
    for f in fails:
        print("FAIL " + f)
    print(f"{len(fails)} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
