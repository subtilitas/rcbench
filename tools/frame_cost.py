#!/usr/bin/env python3
"""Measure what a rendered frame costs in PSRAM traffic on the ESP32-P4.

The panel draws into one of two 1,228,800-byte DPI (display pixel
interface) frame buffers in PSRAM (pseudo-static random-access memory) while
the DPI scans the other out at 74MB/s.  The CPU (central processing unit)
reaches PSRAM through a 64KiB L1 data cache (2-way, 64-byte lines) and the
L2 cache (128KiB, 8-way, 64-byte lines as sdkconfig.defaults leaves them).
A line that misses the L2 is a line fetched from PSRAM, and a dirty line it
evicts is a line written back, so a frame's traffic is about
``2 x line x L2 misses``.  cachegrind models that pair of caches on the
host, and ``test/host/bench_frame.c`` renders the real screens through it.
The model counts traffic only: neither the instructions a frame executes
nor the PPA's copy of a frame turned by Screen rotation 180° is in it.

    python3 tools/frame_cost.py             # measure every mode
    python3 tools/frame_cost.py frame       # just the steady-state frame
    python3 tools/frame_cost.py --check-doc # and docs/Performance.md agrees

Numbers are per frame, taken as the difference between an 11-frame and a
1-frame run so one-off setup (the initial memset, first-touch of the buffer)
cancels out.

What a fill costs in time is the effective bandwidth of the CPU's writes into
PSRAM with the scan-out running.  The panel measures it at start-up: the
console line ``PSRAM probe: ... <n> us each`` gives the time of one clear of
the back buffer, which writes the whole buffer once.  PROBE_CLEAR_US holds
that figure; while it is None the bandwidth is not measured, the table has
no time columns, and the table of the board in docs/Performance.md says so
in its ``clear`` row.  A time the table gives is the traffic's alone.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import ui_sources

REPO = pathlib.Path(__file__).resolve().parent.parent
# Every page carrying the marker pair, so both language versions are checked.
DOCS = sorted((REPO / "docs").glob("Performance*.md"))
DOC_START = "<!-- framecost:start -->"
DOC_END = "<!-- framecost:end -->"
LAYOUT_H = REPO / "shared" / "ui" / "include" / "ui_layout.h"
BOARD_PINS_H = (REPO / "firmware" / "panel" / "components" / "board"
                / "include" / "board_pins.h")
SDKCONFIG_DEFAULTS = REPO / "firmware" / "panel" / "sdkconfig.defaults"

# The canvas the profile models: the 7-inch ESP32-P4 board's 1024 x 600px.
# The renderer draws the size in ui_layout.h; --check-doc and --update-doc
# fail while the two differ.
CANVAS = (1024, 600)
BYTES_PER_PIXEL = 2                 # RGB565

# The screens (ui_sources.py) and the bench frame that draws them.
SOURCES = ["test/host/bench_frame.c", *ui_sources.SOURCES]
INCLUDES = ui_sources.INCLUDES

MODES = ["frame", "frame-idle", "held", "sim", "throttle", "chrome",
         "overview", "servo", "servo-grip", "servo-current", "supply",
         "supply-chrome",
         "analyser", "logs", "settings", "settings-list", "battery",
         "balance",
         "motor-stick", "balance-sim", "settings-sim", "battery-sim",
         "analyser-chrome", "logs-chrome", "settings-chrome",
         "settings-list-chrome",
         "battery-chrome", "balance-chrome", "motor-stick-chrome",
         "picker", "picker-chrome", "wifi", "wifi-chrome", "wifi-keys",
         "clear",
         "vlines", "hlines"]

# The L1 data cache: MAX_L1_DCACHE_SIZE, MAX_L1_DCACHE_WAY and
# MIN_L1_DCACHE_LINE_SIZE in ESP-IDF's esp32p4/rom/cache.h.
D1 = "65536,2,64"
# The L2 cache's ways (MAX_L2_CACHE_WAY there); its size and line come from
# sdkconfig.defaults, ESP-IDF's defaults where it sets none.
L2_WAYS = 8
L2_DEFAULT = (131072, 64)

MISS_RE = re.compile(r"LLd\s+misses:\s*([0-9,]+)")

# The time of one clear of the 1,228,800-byte back buffer, in us, from the
# start-up probe on the board (firmware/panel/main/panel_perf.c).  None:
# not measured.
PROBE_CLEAR_US: float | None = None


def l2_cache(text: str) -> tuple[int, int]:
    """Read the L2 cache's size and line from sdkconfig.defaults.

    Args:
        text: The file's contents.

    Returns:
        (bytes, line bytes): CONFIG_CACHE_L2_CACHE_<n>KB and
        CONFIG_CACHE_L2_CACHE_LINE_<n>B set to y, or ESP-IDF's defaults,
        128KiB and 64 bytes, for the one the file does not set.
    """
    size, line = L2_DEFAULT
    m = re.search(r"^CONFIG_CACHE_L2_CACHE_(\d+)KB=y", text, re.M)
    if m:
        size = int(m.group(1)) * 1024
    m = re.search(r"^CONFIG_CACHE_L2_CACHE_LINE_(\d+)B=y", text, re.M)
    if m:
        line = int(m.group(1))
    return size, line


def panel_hz(text: str, canvas: tuple[int, int]) -> float:
    """Compute the panel's refresh from the DPI timing in board_pins.h.

    The requested pixel clock over the frame's total clocks: 52MHz over
    1354 x 636 gives 60.38Hz, the console's VSYNC figure on the board.  The
    DPI runs at 48MHz, and ESP-IDF keeps the requested line time by
    shortening the front porch, so the requested clock gives the rate
    (docs/Performance.md, Rules).

    Args:
        text: board_pins.h.
        canvas: The active width and height, px.

    Returns:
        The pixel clock over the total pixels of a frame, Hz.

    Raises:
        SystemExit: A timing define is missing.
    """
    names = ("DPI_CLOCK_MHZ", "HSYNC_PULSE_WIDTH", "HSYNC_BACK_PORCH",
             "HSYNC_FRONT_PORCH", "VSYNC_PULSE_WIDTH", "VSYNC_BACK_PORCH",
             "VSYNC_FRONT_PORCH")
    v = {}
    for n in names:
        m = re.search(rf"#define\s+BOARD_LCD_{n}\s+([\d.]+)f?", text)
        if m is None:
            sys.exit(f"{BOARD_PINS_H.name}: no BOARD_LCD_{n}")
        v[n] = float(m.group(1))
    h = (canvas[0] + v["HSYNC_PULSE_WIDTH"] + v["HSYNC_BACK_PORCH"]
         + v["HSYNC_FRONT_PORCH"])
    vt = (canvas[1] + v["VSYNC_PULSE_WIDTH"] + v["VSYNC_BACK_PORCH"]
          + v["VSYNC_FRONT_PORCH"])
    return v["DPI_CLOCK_MHZ"] * 1e6 / (h * vt)


def effective_bw(clear_us: float | None, canvas: tuple[int, int]
                 ) -> float | None:
    """Turn the probe's time of one clear into the effective bandwidth.

    A clear writes every line of the buffer once, and the model counts each
    as a fill and a write-back: 2 x the buffer's bytes of traffic.

    Args:
        clear_us: The probe's time of one clear, us; None if not measured.
        canvas: The buffer's width and height, px.

    Returns:
        Bytes of modelled traffic per second, or None.
    """
    if clear_us is None:
        return None
    return 2 * canvas[0] * canvas[1] * BYTES_PER_PIXEL / (clear_us / 1e6)


def build(tmp: pathlib.Path) -> pathlib.Path:
    """Compile the bench frame and the screens for the host.

    Args:
        tmp: The directory to build in.

    Returns:
        The executable.
    """
    exe = tmp / "bench_frame"
    cmd = ["cc", "-O2", "-g", "-o", str(exe)]
    cmd += [str(REPO / s) for s in SOURCES]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    cmd += ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def misses(exe: pathlib.Path, frames: int, mode: str, ll: str) -> int:
    """Count the L2 data misses of one run under cachegrind.

    Args:
        exe: The bench frame.
        frames: Frames to draw.
        mode: The mode to draw.
        ll: cachegrind's --LL geometry, "size,ways,line".

    Returns:
        LLd misses of the run.
    """
    # Invoked as ./bench_frame from its own directory rather than by absolute
    # path: argv[0] lands on the stack, so its length shifts what the first
    # cache lines hold, and a temporary directory name changes between runs.
    proc = subprocess.run(
        ["valgrind", "--tool=cachegrind", "--cache-sim=yes",
         f"--D1={D1}", f"--LL={ll}", "--cachegrind-out-file=/dev/null",
         "./" + exe.name, str(frames), mode],
        cwd=exe.parent, capture_output=True, text=True, check=True,
    )
    m = MISS_RE.search(proc.stderr)
    if not m:
        sys.exit("could not parse cachegrind output:\n" + proc.stderr)
    return int(m.group(1).replace(",", ""))


# The hyphen matters: \w+ silently skipped frame-idle, servo-grip and every
# -sim and -chrome mode, so they were absent from both sides of the
# comparison and the check passed without looking at them.
ROW_RE = re.compile(r"^([\w-]+)\s+([\d,]+)\s")
# Absolute fills depend on the machine (argv, environment and stack layout
# land in the same cache), so the doc table is checked to a tolerance rather
# than byte for byte: 2% of a mode, and never less than 100 fills.
DOC_TOLERANCE = 0.02
DOC_FLOOR = 100


def doc_rows(block: str) -> dict[str, int]:
    """Read the fills of every mode out of the block in a page.

    Args:
        block: The text between the markers.

    Returns:
        {mode: lines per frame}.
    """
    rows = {}
    for line in block.splitlines():
        m = ROW_RE.match(line)
        if m and m.group(1) in MODES:
            rows[m.group(1)] = int(m.group(2).replace(",", ""))
    return rows


def doc_header(block: str) -> str | None:
    """Return the block's line that states the profile.

    Args:
        block: The text between the markers.

    Returns:
        The line starting "panel ", or None.
    """
    for line in block.splitlines():
        if line.startswith("panel "):
            return line
    return None


def doc_block(lines: list[str]) -> str:
    """Build the fenced block docs/Performance.md shows, exactly as printed.

    Args:
        lines: The printed lines.

    Returns:
        The block with its markers.
    """
    body = "\n".join(lines)
    return (f"{DOC_START}\n```\n$ python3 tools/frame_cost.py\n"
            f"{body}\n```\n{DOC_END}")


def splice_docs(block: str, write: bool) -> int:
    """Update or verify the block in every page that carries it.

    Args:
        block: The block as measured.
        write: Write it rather than compare.

    Returns:
        0 when every page agrees or was written, 1 otherwise.
    """
    return max((splice_doc(doc, block, write) for doc in DOCS), default=0)


def splice_doc(doc: pathlib.Path, block: str, write: bool) -> int:
    """Update or verify the block in one page.

    Args:
        doc: The page.
        block: The block as measured.
        write: Write it rather than compare.

    Returns:
        0 if it agrees or was written, 1 otherwise.
    """
    with open(doc, encoding="utf-8", newline="") as fh:
        text = fh.read()
    start = text.find(DOC_START)
    end = text.find(DOC_END)
    if start < 0 or end < 0:
        sys.exit(f"{doc.name} has no {DOC_START} / {DOC_END} pair")
    current = text[start:end + len(DOC_END)]
    if not write:
        problems = block_problems(doc.name, current, block)
        if not problems:
            print(f"{doc.name} frame-cost table is up to date")
            return 0
        for p in problems:
            print(p, file=sys.stderr)
        print("run tools/frame_cost.py --update-doc", file=sys.stderr)
        return 1
    if current == block:
        print(f"{doc.name} frame-cost table is up to date")
        return 0
    with open(doc, "w", encoding="utf-8", newline="") as fh:
        fh.write(text[:start] + block + text[end + len(DOC_END):])
    print(f"updated {doc.name}")
    return 0


def block_problems(page: str, current: str, block: str) -> list[str]:
    """List what a page's block says that the measurement does not.

    Args:
        page: The page's name, for the messages.
        current: The block in the page.
        block: The block as measured.

    Returns:
        One line per disagreement: the profile line, a missing mode, a mode
        whose fills moved past the tolerance.
    """
    out = []
    if doc_header(current) != doc_header(block):
        out.append(f"{page} says '{doc_header(current)}'; the profile "
                   f"gives '{doc_header(block)}'")
    have, want = doc_rows(current), doc_rows(block)
    for m in sorted(set(want) - set(have)):
        out.append(f"{page} has no row for mode '{m}'")
    for m in sorted(want.keys() & have.keys()):
        if abs(have[m] - want[m]) > max(DOC_FLOOR, want[m] * DOC_TOLERANCE):
            out.append(f"{page} says {m} costs {have[m]:,} fills; it costs "
                       f"{want[m]:,}")
    return out


# ------------------------------------------------- the table of the board --

# The table of DRAW and WAIT measured on the board, in each language: the
# header the tool finds it by and the words a cell not measured holds.
BENCH_PAGES = {
    "Performance.md": (
        "| Mode | On the panel | DRAW min (ms) | DRAW mean (ms) "
        "| DRAW max (ms) | WAIT mean (ms) | fps |", "not measured"),
    "Performance-de.md": (
        "| Modus | Auf dem Panel | DRAW min (ms) | DRAW Mittel (ms) "
        "| DRAW max (ms) | WAIT Mittel (ms) | fps |", "nicht gemessen"),
}
# The cells after the description, in order.
BENCH_CELLS = 5


def bench_rows(text: str, header: str, unmeasured: str
               ) -> dict[str, list[float | None]] | None:
    """Read the table of the board out of a page.

    Args:
        text: The page.
        header: The table's header row.
        unmeasured: What a cell not measured says.

    Returns:
        {mode: [DRAW min, mean, max, WAIT mean, fps]}, a figure None where
        the cell says @p unmeasured; None without the table.

    Raises:
        SystemExit: A row names no mode in backticks, has another number
            of cells, or has a cell that is neither a number nor
            @p unmeasured.
    """
    if header not in text:
        return None
    rows = {}
    body = text[text.index(header):].split("\n\n", 1)[0].splitlines()[2:]
    for row in body:
        cells = [c.strip() for c in row.strip().strip("|").split("|")]
        m = re.fullmatch(r"`([\w-]+)`", cells[0])
        if m is None or len(cells) != 2 + BENCH_CELLS:
            sys.exit(f"the table of the board: a row that is not a mode in "
                     f"backticks, a description and {BENCH_CELLS} figures: "
                     f"{row}")
        figures = []
        for cell in cells[2:]:
            if cell == unmeasured:
                figures.append(None)
                continue
            try:
                figures.append(float(cell.replace(",", ".")))
            except ValueError:
                sys.exit(f"the table of the board: '{cell}' in the row of "
                         f"{m.group(1)} is neither a number nor "
                         f"'{unmeasured}'")
        rows[m.group(1)] = figures
    return rows


def bench_problems(page: str, rows: dict | None,
                   clear_us: float | None) -> list[str]:
    """Hold the table of the board to the modes and to the profile.

    Only a measured cell is held.  The clear row's DRAW mean is the start-up
    probe's time of one clear, the figure PROBE_CLEAR_US holds: the two are
    measured together or not at all, and agree to 0.05ms.

    Args:
        page: The page's name, for the messages.
        rows: bench_rows() of the page.
        clear_us: PROBE_CLEAR_US.

    Returns:
        One line per disagreement.
    """
    if rows is None:
        return [f"{page}: no table of DRAW and WAIT on the board"]
    out = [f"{page}: the table of the board names mode '{m}', which "
           f"frame_cost.py does not draw" for m in rows if m not in MODES]
    if "clear" not in rows:
        out.append(f"{page}: the table of the board has no row for 'clear', "
                   "the start-up probe")
        return out
    probe = rows["clear"][1]
    if probe is None and clear_us is not None:
        out.append(f"{page}: the clear row is not measured; PROBE_CLEAR_US "
                   f"is {clear_us} us")
    elif probe is not None and clear_us is None:
        out.append(f"{page}: the clear row gives {probe} ms; PROBE_CLEAR_US "
                   "is not set")
    elif probe is not None and abs(probe - clear_us / 1000.0) > 0.05:
        out.append(f"{page}: the clear row gives {probe} ms; PROBE_CLEAR_US "
                   f"is {clear_us / 1000.0:.2f} ms")
    return out


def check_bench(clear_us: float | None) -> int:
    """Run bench_problems() on every page.

    Args:
        clear_us: PROBE_CLEAR_US.

    Returns:
        0 when every page agrees, 1 otherwise.
    """
    rc = 0
    for doc in DOCS:
        header, unmeasured = BENCH_PAGES[doc.name]
        rows = bench_rows(doc.read_text(encoding="utf-8"), header,
                          unmeasured)
        problems = bench_problems(doc.name, rows, clear_us)
        for p in problems:
            print(p, file=sys.stderr)
        if problems:
            rc = 1
        else:
            measured = sum(1 for r in rows.values() if r[1] is not None)
            print(f"{doc.name} table of the board: {measured} of "
                  f"{len(rows)} rows measured")
    return rc


def panel_size() -> tuple[int, int]:
    """Read the panel size the renderer draws.

    Returns:
        UI_PANEL_W and UI_PANEL_H from shared/ui/include/ui_layout.h.
    """
    text = LAYOUT_H.read_text(encoding="utf-8")
    w = re.search(r"#define\s+UI_PANEL_W\s+(\d+)", text)
    h = re.search(r"#define\s+UI_PANEL_H\s+(\d+)", text)
    if w is None or h is None:
        sys.exit(f"{LAYOUT_H}: no UI_PANEL_W or UI_PANEL_H")
    return int(w.group(1)), int(h.group(1))


def table(per_mode: list[tuple[str, int]], line: int, hz: float,
          bw: float | None) -> list[str]:
    """Print the measured fills as the block in the docs shows them.

    Args:
        per_mode: (mode, L2 fills per frame) in order.
        line: The L2 line, bytes.
        hz: The panel's refresh, Hz.
        bw: The effective bandwidth, bytes per s; None if not measured.

    Returns:
        The lines.
    """
    out = []
    if bw is None:
        out.append(f"panel {hz:.1f} Hz, {line}-byte lines; effective "
                   "bandwidth not measured\n")
        out.append(f"{'mode':<21} {'lines/frame':>12} {'traffic':>11}")
        out.append("-" * 46)
    else:
        budget = bw / hz
        out.append(f"panel {hz:.1f} Hz, {line}-byte lines, "
                   f"~{bw / 1e6:.0f} MB/s effective -> {budget / 1024:.0f} "
                   "KiB of traffic per panel frame\n")
        out.append(f"{'mode':<21} {'lines/frame':>12} {'traffic':>11} "
                   f"{'est. ms':>9} {'est. fps':>9}")
        out.append("-" * 66)
    for mode, per in per_mode:
        traffic = per * line * 2          # fill + writeback
        row = f"{mode:<21} {per:>12,} {traffic / 1024:>7.0f} KiB"
        if bw is not None:
            ms = traffic / bw * 1000.0
            # display_flip() blocks until the next panel frame, so the
            # achieved rate is the panel rate over whole frames consumed.
            n = max(1, -(-ms // (1000.0 / hz)))
            row += f" {ms:>9.1f} {hz / n:>9.1f}"
        out.append(row)
    if bw is None:
        out.append("\nest. ms and est. fps: not measured")
    return out


def main() -> int:
    """Measure the modes the command line names.

    Returns:
        0 when every check passes, 1 otherwise.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("modes", nargs="*", default=None,
                    help=f"subset of {', '.join(MODES)}")
    ap.add_argument("--max-lines", type=int, metavar="N",
                    help="fail if any measured mode exceeds N cache-line "
                         "fills per frame (CI regression guard)")
    ap.add_argument("--update-doc", action="store_true",
                    help="write the measured table into docs/Performance.md")
    ap.add_argument("--check-doc", action="store_true",
                    help="fail if docs/Performance.md disagrees with the "
                         "measured table or with the profile")
    args = ap.parse_args()
    modes = args.modes or MODES
    # The doc shows every mode, so touching it means measuring every mode.
    if (args.update_doc or args.check_doc) and args.modes:
        sys.exit("--update-doc/--check-doc measure every mode; drop the "
                 "mode arguments")
    if args.update_doc or args.check_doc:
        size = panel_size()
        if size != CANVAS:
            print(f"the profile models {CANVAS[0]} x {CANVAS[1]} px; the "
                  f"renderer draws {size[0]} x {size[1]} px",
                  file=sys.stderr)
            return 1

    for tool in ("cc", "valgrind"):
        if not shutil.which(tool):
            sys.exit(f"required tool not found on PATH: {tool}")

    size, line = l2_cache(SDKCONFIG_DEFAULTS.read_text(encoding="utf-8"))
    ll = f"{size},{L2_WAYS},{line}"
    hz = panel_hz(BOARD_PINS_H.read_text(encoding="utf-8"), CANVAS)
    bw = effective_bw(PROBE_CLEAR_US, CANVAS)
    worst = 0
    per_mode = []
    with tempfile.TemporaryDirectory() as td:
        exe = build(pathlib.Path(td))
        for mode in modes:
            lo = misses(exe, 1, mode, ll)
            hi = misses(exe, 11, mode, ll)
            # A negative difference is noise around zero, not a mode that
            # gives memory back.
            per = max(0, (hi - lo) // 10)
            worst = max(worst, per)
            per_mode.append((mode, per))

    printed = table(per_mode, line, hz, bw)
    for p in printed:
        print(p)
    print("\nConfirm against the DRAW and WAIT the panel's console prints "
          "every 300 frames,\nand the table of the board in "
          "docs/Performance.md.")

    if args.update_doc or args.check_doc:
        rc = splice_docs(doc_block(printed), write=args.update_doc)
        rc = max(rc, check_bench(PROBE_CLEAR_US))
        if rc:
            return rc

    if args.max_lines is not None:
        if worst > args.max_lines:
            print(f"\nFAIL: {worst:,} cache-line fills per frame exceeds the "
                  f"{args.max_lines:,} ceiling", file=sys.stderr)
            return 1
        print(f"\nOK: worst mode is {worst:,} fills per frame, ceiling "
              f"{args.max_lines:,}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
