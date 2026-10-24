#!/usr/bin/env python3
"""Measure what a rendered frame costs in PSRAM traffic.

Frame rate on this board is decided by memory bandwidth, not by the CPU
(central processing unit) and not by the panel: the framebuffer is in PSRAM
(pseudo-static random-access memory) behind a 64KiB write-back,
write-allocate cache with 64-byte lines, and the LCD (liquid-crystal display)
scan-out reads about 30MB/s of that bus continuously.

Every cache-line fill is 64 bytes fetched from PSRAM, and every dirty line
evicted is 64 bytes written back -- so a frame's traffic is roughly
``2 x 64 x misses``.  cachegrind can model exactly that cache on the host, and
``test/host/bench_frame.c`` renders the real bench frame through it.

    python3 tools/frame_cost.py             # measure every mode
    python3 tools/frame_cost.py frame       # just the steady-state frame
    python3 tools/frame_cost.py --check-doc # and docs/Performance.md agrees

Numbers are per frame, taken as the difference between an 11-frame and a
1-frame run so one-off setup (the initial memset, first-touch of the buffer)
cancels out.

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

# The canvas the table in docs/Performance.md models: the ESP32-S3 panel's
# 800 x 480px.  The renderer draws the size in ui_layout.h; while the two
# differ, --check-doc reports the table as not checked and --update-doc
# writes nothing.  The ESP32-P4 profile replaces both.
DOC_MODEL = (800, 480)
LAYOUT_H = REPO / "shared" / "ui" / "include" / "ui_layout.h"

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

# ESP32-S3 with CONFIG_ESP32S3_DATA_CACHE_64KB + _LINE_64B.
D1 = "65536,8,64"
LL = "8388608,16,64"

MISS_RE = re.compile(r"D1\s+misses:\s*([0-9,]+)")

# Effective CPU-side PSRAM throughput while the LCD is scanning, in bytes per
# second.  Calibrated against the board: 3.0MB/frame measured at 13fps.
EFFECTIVE_BW = 39e6
# 16MHz pclk over 820 x 500 total clocks.
PANEL_HZ = 16e6 / (820 * 500)


def build(tmp: pathlib.Path) -> pathlib.Path:
    exe = tmp / "bench_frame"
    cmd = ["cc", "-O2", "-g", "-o", str(exe)]
    cmd += [str(REPO / s) for s in SOURCES]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    cmd += ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def misses(exe: pathlib.Path, frames: int, mode: str) -> int:
    # Invoked as ./bench_frame from its own directory rather than by absolute
    # path: argv[0] lands on the stack, so its length shifts what the first
    # cache lines hold, and a temporary directory name changes between runs.
    proc = subprocess.run(
        ["valgrind", "--tool=cachegrind", "--cache-sim=yes",
         f"--D1={D1}", f"--LL={LL}", "--cachegrind-out-file=/dev/null",
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
# than byte for byte.  1% of the steady-state frame is about 100 fills: two
# orders of magnitude above the run-to-run variation and two below a change
# worth documenting.
# 2%, not 1%.  The hyphenated modes were skipped by the row pattern until
# they were not, and the largest of them drifts about 275 fills of 22,700
# between this machine and CI's -- more than 1% allows.  The tolerance is for
# machine-to-machine variation, and it has to cover the modes actually checked.
DOC_TOLERANCE = 0.02
DOC_FLOOR = 100


def doc_rows(block: str) -> dict[str, int]:
    rows = {}
    for line in block.splitlines():
        m = ROW_RE.match(line)
        if m and m.group(1) in MODES:
            rows[m.group(1)] = int(m.group(2).replace(",", ""))
    return rows


def doc_block(lines: list[str]) -> str:
    """The fenced block docs/Performance.md shows, exactly as printed."""
    body = "\n".join(lines)
    return (f"{DOC_START}\n```\n$ python3 tools/frame_cost.py\n"
            f"{body}\n```\n{DOC_END}")


def splice_docs(block: str, write: bool) -> int:
    """Update or verify the block in every page that carries it."""
    return max((splice_doc(doc, block, write) for doc in DOCS), default=0)


def splice_doc(doc, block: str, write: bool) -> int:
    """Update or verify the block in one page.  0 if it agrees."""
    with open(doc, encoding="utf-8", newline="") as fh:
        text = fh.read()
    start = text.find(DOC_START)
    end = text.find(DOC_END)
    if start < 0 or end < 0:
        sys.exit(f"{doc.name} has no {DOC_START} / {DOC_END} pair")
    current = text[start:end + len(DOC_END)]
    if not write:
        have, want = doc_rows(current), doc_rows(block)
        missing = sorted(set(want) - set(have))
        drifted = [
            (m, have[m], want[m]) for m in sorted(want & have.keys())
            if abs(have[m] - want[m]) > max(DOC_FLOOR, want[m] * DOC_TOLERANCE)
        ]
        if not missing and not drifted:
            print(f"{doc.name} frame-cost table is up to date")
            return 0
        for m in missing:
            print(f"{doc.name} has no row for mode '{m}'", file=sys.stderr)
        for m, was, now in drifted:
            print(f"{doc.name} says {m} costs {was:,} fills; it costs "
                  f"{now:,}", file=sys.stderr)
        print("run tools/frame_cost.py --update-doc", file=sys.stderr)
        return 1
    if current == block:
        print(f"{doc.name} frame-cost table is up to date")
        return 0
    with open(doc, "w", encoding="utf-8", newline="") as fh:
        fh.write(text[:start] + block + text[end + len(DOC_END):])
    print(f"updated {doc.name}")
    return 0


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


def main() -> int:
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
                         "measured table")
    args = ap.parse_args()
    modes = args.modes or MODES
    # The doc shows every mode, so touching it means measuring every mode.
    if (args.update_doc or args.check_doc) and args.modes:
        sys.exit("--update-doc/--check-doc measure every mode; drop the "
                 "mode arguments")
    if args.update_doc or args.check_doc:
        size = panel_size()
        if size != DOC_MODEL:
            print(f"docs/Performance.md models {DOC_MODEL[0]} x "
                  f"{DOC_MODEL[1]} px; the renderer draws {size[0]} x "
                  f"{size[1]} px: the table is not checked or written")
            return 1 if args.update_doc else 0
    worst = 0
    printed: list[str] = []

    for tool in ("cc", "valgrind"):
        if not shutil.which(tool):
            sys.exit(f"required tool not found on PATH: {tool}")

    with tempfile.TemporaryDirectory() as td:
        exe = build(pathlib.Path(td))

        budget = EFFECTIVE_BW / PANEL_HZ
        printed.append(
            f"panel {PANEL_HZ:.1f} Hz, ~{EFFECTIVE_BW/1e6:.0f} MB/s effective "
            f"-> {budget/1024:.0f} KiB of traffic per panel frame\n")
        printed.append(
            f"{'mode':<9} {'lines/frame':>12} {'traffic':>11} {'est. ms':>9} "
            f"{'est. fps':>9}")
        printed.append("-" * 55)

        for mode in modes:
            lo = misses(exe, 1, mode)
            hi = misses(exe, 11, mode)
            # A negative difference is noise around zero, not a mode that
            # gives memory back.
            per = max(0, (hi - lo) // 10)
            worst = max(worst, per)
            traffic = per * 64 * 2          # fill + writeback
            ms = traffic / EFFECTIVE_BW * 1000.0
            # display_flip() blocks until the next panel frame, so the achieved
            # rate is the panel rate divided by whole frames consumed.
            n = max(1, -(-ms // (1000.0 / PANEL_HZ)))
            fps = PANEL_HZ / n
            printed.append(f"{mode:<9} {per:>12,} {traffic/1024:>8.0f} KiB "
                           f"{ms:>8.1f} {fps:>9.1f}")

    for line in printed:
        print(line)
    print("\nEstimates assume the render is bandwidth-bound, which on this "
          "board it is.\nConfirm against DRAW/WAIT in the log line "
          "main.c prints every 300 frames.")

    if args.update_doc or args.check_doc:
        rc = splice_docs(doc_block(printed), write=args.update_doc)
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
