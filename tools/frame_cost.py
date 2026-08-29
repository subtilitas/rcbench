#!/usr/bin/env python3
"""Measure what a rendered frame costs in PSRAM traffic.

Frame rate on this board is decided by memory bandwidth, not by the CPU and
not by the panel: the framebuffer is in PSRAM behind a 64 KiB write-back,
write-allocate cache with 64-byte lines, and the LCD's own scan-out is already
reading ~30 MB/s of that bus continuously.

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
"""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
DOC = REPO / "docs" / "Performance.md"
DOC_START = "<!-- framecost:start -->"
DOC_END = "<!-- framecost:end -->"

SOURCES = [
    "test/host/bench_frame.c",
    "shared/gfx/gfx.c",
    "shared/gfx/gfx_seg.c",
    "shared/gfx/gfx_font8x16.c",
    "shared/gfx/gfx_font16x28.c",
    "shared/gfx/gfx_font_num24x30.c",
    "shared/ui/ui_theme.c",
    "shared/ui/ui_widgets.c",
    "shared/ui/ui_icons.c",
    "shared/ui/ui_band.c",
    "shared/ui/ui_plot.c",
    "shared/ui/ui_hero.c",
    "shared/ui/ui_slider.c",
    "shared/ui/ui_tabs.c",
    "shared/ui/ui_watermark.c",
    "shared/ui/ui_router.c",
    "shared/ui/splash_screen.c",
    "shared/ui/overview_screen.c",
    "shared/ui/stub_screen.c",
    "shared/ui/motor_screen.c",
    "shared/ui/log_viewer_screen.c",
    "shared/ui/settings_screen.c",
    "shared/settings/settings.c",
    "shared/logfile/log_numbers.c",
    "shared/logfile/log_csv.c",
    "shared/logfile/log_fields.c",
    "shared/bench/bench_state.c",
    "shared/bench/telemetry_sim.c",
    "shared/bench/throttle.c",
]
INCLUDES = [
    "shared/gfx/include",
    "shared/touch/include",
    "shared/ui/include",
    "shared/bench/include",
    "shared/link/include",
    "shared/logfile/include",
    "shared/settings/include",
]

MODES = ["frame", "frame-idle", "sim", "chrome", "overview", "clear",
         "vlines", "hlines"]

# ESP32-S3 with CONFIG_ESP32S3_DATA_CACHE_64KB + _LINE_64B.
D1 = "65536,8,64"
LL = "8388608,16,64"

MISS_RE = re.compile(r"D1\s+misses:\s*([0-9,]+)")

# Effective CPU-side PSRAM throughput while the LCD is scanning, in bytes per
# second.  Calibrated against the board: 3.0 MB/frame measured at 13 fps.
EFFECTIVE_BW = 39e6
# 16 MHz pclk over 820 x 500 total clocks.
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
    # Invoked as ./bench_frame from its own directory, not by absolute path.
    # argv[0] lands on the stack, so its length shifts what the first cache
    # lines hold -- building under a fresh mkdtemp() name moved the totals by
    # a handful of fills between runs, which is small but is exactly the kind
    # of wobble that makes people stop believing a benchmark.
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


ROW_RE = re.compile(r"^(\w+)\s+([\d,]+)\s")
# Absolute fills depend on the machine -- argv, environment and stack layout
# all land in the same cache -- so the doc table is checked to a tolerance
# rather than byte for byte.  1 % of the steady-state frame is about a hundred
# fills, two orders of magnitude above the wobble and two orders below any
# change worth writing a paragraph about.
DOC_TOLERANCE = 0.01
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
    return f"{DOC_START}\n```\n$ python3 tools/frame_cost.py\n{body}\n```\n{DOC_END}"


def splice_doc(block: str, write: bool) -> int:
    """Update or verify the block in docs/Performance.md.  0 if it agrees."""
    with open(DOC, encoding="utf-8", newline="") as fh:
        text = fh.read()
    start = text.find(DOC_START)
    end = text.find(DOC_END)
    if start < 0 or end < 0:
        sys.exit(f"{DOC.name} has no {DOC_START} / {DOC_END} pair")
    current = text[start:end + len(DOC_END)]
    if not write:
        have, want = doc_rows(current), doc_rows(block)
        missing = sorted(set(want) - set(have))
        drifted = [
            (m, have[m], want[m]) for m in sorted(want & have.keys())
            if abs(have[m] - want[m]) > max(DOC_FLOOR, want[m] * DOC_TOLERANCE)
        ]
        if not missing and not drifted:
            print(f"{DOC.name} frame-cost table is up to date")
            return 0
        for m in missing:
            print(f"{DOC.name} has no row for mode '{m}'", file=sys.stderr)
        for m, was, now in drifted:
            print(f"{DOC.name} says {m} costs {was:,} fills; it costs "
                  f"{now:,}", file=sys.stderr)
        print("run tools/frame_cost.py --update-doc", file=sys.stderr)
        return 1
    if current == block:
        print(f"{DOC.name} frame-cost table is up to date")
        return 0
    with open(DOC, "w", encoding="utf-8", newline="") as fh:
        fh.write(text[:start] + block + text[end + len(DOC_END):])
    print(f"updated {DOC.name}")
    return 0


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
    # The doc shows every mode, so touching it means measuring every mode --
    # a partial table is worse than none.
    if (args.update_doc or args.check_doc) and args.modes:
        sys.exit("--update-doc/--check-doc measure every mode; drop the "
                 "mode arguments")
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
        rc = splice_doc(doc_block(printed), write=args.update_doc)
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
