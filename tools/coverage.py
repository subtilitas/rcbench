#!/usr/bin/env python3
"""Measure host-test line coverage, enforce the floors, and keep the table.

The pure-C core under shared/ builds and runs on the host.  This script
builds the suite with gcov instrumentation, runs it, and renders the result
into the block between the ``coverage:start`` and ``coverage:end`` markers in
STATUS.md.  The README badge comes from Codecov, which measures the same
build in CI (continuous integration).

    python3 tools/coverage.py            # update the STATUS.md table
    python3 tools/coverage.py --check    # fail if the table is out of date
    python3 tools/coverage.py --json coverage.json

``--check`` is what CI runs: drift fails the build rather than being
committed by a bot.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
TEST_DIR = REPO / "test" / "host"
BUILD_DIR = TEST_DIR / "build"
STATUS = REPO / "STATUS.md"

# The table lives in the running record.  This tool is the offline gate (the
# floors below) and the per-file breakdown; Codecov reports the same
# measurement in CI.
START = "<!-- coverage:start -->"
END = "<!-- coverage:end -->"

# Files the host suite is expected to cover, in the order they appear in the
# STATUS.md table.
TRACKED = [
    "shared/gfx/gfx.c",
    "shared/gfx/gfx_seg.c",
    "shared/touch/touch_map.c",
    "shared/ui/ui_theme.c",
    "shared/ui/ui_widgets.c",
    "shared/ui/ui_icons.c",
    "shared/ui/ui_band.c",
    "shared/ui/ui_watermark.c",
    "shared/ui/ui_plot.c",
    "shared/ui/ui_hero.c",
    "shared/ui/ui_slider.c",
    "shared/ui/ui_tabs.c",
    "shared/ui/ui_router.c",
    "shared/ui/splash_screen.c",
    "shared/ui/overview_screen.c",
    "shared/ui/stub_screen.c",
    "shared/ui/motor_screen.c",
    "shared/ui/servo_screen.c",
    "shared/ui/analyser_screen.c",
    "shared/ui/balance_screen.c",
    "shared/ui/battery_screen.c",
    "shared/ui/programmer_screen.c",
    "shared/ui/log_viewer_screen.c",
    "shared/ui/settings_screen.c",
    "shared/ui/outputs_screen.c",
    "shared/settings/settings.c",
    "shared/logfile/log_numbers.c",
    "shared/logfile/log_csv.c",
    "shared/logfile/log_fields.c",
    "shared/safety/heartbeat.c",
    "shared/safety/arming.c",
    "shared/servo/servo_limit.c",
    "shared/servo/servo_sync.c",
    "shared/openyge/openyge_frame.c",
    "shared/openyge/openyge_status.c",
    "shared/openyge/openyge_params.c",
    "shared/servo/servo_sim.c",
    "shared/sbus/sbus.c",
    "shared/dshot/dshot_frame.c",
    "shared/dshot/dshot_telem.c",
    "shared/ppm/ppm.c",
    "shared/can/can_timing.c",
    "shared/can/can_selftest.c",
    "shared/can/mcp2515.c",
    "shared/link/link_bringup.c",
    "shared/link/link_can.c",
    "shared/link/link_crc.c",
    "shared/link/link_dev.c",
    "shared/link/link_host.c",
    "shared/link/link_artxfer.c",
    "shared/bench/bench_state.c",
    "shared/outputs/outputs.c",
    "shared/outputs/outputs_pages.c",
    "shared/outputs/out_bind.c",
    "shared/bench/telemetry_sim.c",
    "shared/bench/log_writer.c",
]

# Sources that are compiled into the suite but deliberately not measured.
# Anything else that is instrumented and missing from TRACKED is an omission,
# not a decision -- see check_tracked_is_complete().
UNTRACKED_OK = {
    "greatest.h",
    # Test scaffolding, not code under test.
    "fake_wire.c",
}

# Below this, CI fails.  Raise it when the suite gets better; never lower it
# to make a red build go green.
MIN_TOTAL_COVERAGE = 94.0

# No single file may fall far below the whole: a healthy total can hide a file
# that is barely tested, so each tracked file carries its own floor.  It is
# lower than the total because its purpose is to catch a hole, not to demand
# every file match the best.  Raise it as the suite improves.
MIN_FILE_COVERAGE = 85.0

# Files that are meant to be thin.  A stub exists to be replaced, so it is
# exempt from the per-file floor by name.
FILE_FLOOR_EXEMPT = {
    "shared/ui/stub_screen.c",
}

LINES_RE = re.compile(r"Lines executed:([0-9.]+)% of (\d+)")


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, cwd=kw.pop("cwd", REPO),
                          capture_output=True, text=True, **kw)


def require(tool: str) -> str:
    path = shutil.which(tool)
    if not path:
        sys.exit(f"required tool not found on PATH: {tool}")
    return path


def build_and_run() -> None:
    require("cmake")
    require("gcov")
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)
    run(["cmake", "-S", str(TEST_DIR), "-B", str(BUILD_DIR),
         "-DENABLE_COVERAGE=ON", "-DCMAKE_BUILD_TYPE=Debug"])
    run(["cmake", "--build", str(BUILD_DIR), "-j", "4"])
    run(["ctest", "--output-on-failure"], cwd=BUILD_DIR)


def find_gcda(source: pathlib.Path) -> pathlib.Path:
    """CMake mangles object paths, so locate the data file by basename."""
    wanted = source.name + ".gcda"
    matches = sorted(p for p in BUILD_DIR.rglob(wanted))
    if not matches:
        sys.exit(f"no {wanted} under {BUILD_DIR}; "
                 "was the suite built and run?")
    return matches[0]


def check_tracked_is_complete() -> None:
    """Every instrumented source must be in TRACKED.

    measure() looks only at TRACKED, so an unlisted file would be absent from
    both the numerator and the denominator.
    """
    instrumented = {p.name[: -len(".gcda")] for p in BUILD_DIR.rglob("*.gcda")}
    tracked = {pathlib.Path(rel).name for rel in TRACKED}
    missing = sorted(instrumented - tracked - UNTRACKED_OK)
    if missing:
        sys.exit(
            "these sources are built into the host suite but missing from "
            "TRACKED in tools/coverage.py, so they count towards neither the "
            "badge nor the floor:\n  " + "\n  ".join(missing)
        )


def measure() -> dict[str, dict[str, float]]:
    results: dict[str, dict[str, float]] = {}

    for rel in TRACKED:
        src = REPO / rel
        gcda = find_gcda(src)
        proc = subprocess.run(
            ["gcov", str(gcda)],
            cwd=gcda.parent, capture_output=True, text=True,
        )
        # gcov prints one "File '...'" / "Lines executed:..." pair per source
        # it touched; pick out the requested one.
        blocks = proc.stdout.split("File '")
        for block in blocks[1:]:
            name, _, rest = block.partition("'")
            if pathlib.Path(name).resolve() != src.resolve():
                continue
            m = LINES_RE.search(rest)
            if not m:
                continue
            pct = float(m.group(1))
            total = int(m.group(2))
            results[rel] = {
                "percent": pct,
                "lines": total,
                "covered": round(total * pct / 100.0),
            }
            break
        else:
            sys.exit(f"gcov produced no data for {rel}\n"
                     f"{proc.stdout}\n{proc.stderr}")

    return results


def render(results: dict[str, dict[str, float]]) -> tuple[str, float]:
    total_lines = sum(int(r["lines"]) for r in results.values())
    total_covered = sum(int(r["covered"]) for r in results.values())
    total_pct = (100.0 * total_covered / total_lines) if total_lines else 0.0

    rows = ["| File | Lines | Covered | Coverage |",
            "| --- | ---: | ---: | ---: |"]
    for rel in TRACKED:
        r = results[rel]
        rows.append("| `%s` | %d | %d | %.1f%% |"
                    % (rel, int(r["lines"]), int(r["covered"]), r["percent"]))
    rows.append("| **total** | **%d** | **%d** | **%.1f%%** |"
                % (total_lines, total_covered, total_pct))

    body = "\n".join([
        "",
        *rows,
        "",
        "_Generated by `tools/coverage.py`; CI runs `--check` and fails on "
        "drift._",
        "",
    ])
    return body, total_pct


def splice(text: str, body: str, start: str, end: str, what: str) -> str:
    if start not in text or end not in text:
        sys.exit(f"{what} is missing the {start} / {end} markers")
    head = text.split(start)[0]
    tail = text.split(end)[1]
    return head + start + body + end + tail


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the STATUS.md table would change")
    ap.add_argument("--json", type=pathlib.Path,
                    help="also write the raw numbers here")
    ap.add_argument("--skip-build", action="store_true",
                    help="reuse an existing instrumented build")
    args = ap.parse_args()

    if not args.skip_build:
        build_and_run()

    check_tracked_is_complete()

    results = measure()
    body, total_pct = render(results)

    if args.json:
        args.json.write_text(json.dumps(
            {"files": results, "total_percent": round(total_pct, 2)},
            indent=2) + "\n")

    targets = [(STATUS, body, START, END)]

    failed = False
    for path, content, start, end in targets:
        original = path.read_text()
        updated = splice(original, content, start, end, path.name)
        if args.check:
            if updated != original:
                print(f"{path.name} coverage block is out of date; "
                      "run tools/coverage.py", file=sys.stderr)
                failed = True
            else:
                print(f"{path.name} coverage block is up to date")
        else:
            if updated != original:
                path.write_text(updated)
            print(f"updated {path.relative_to(REPO)}")

    print(f"total line coverage: {total_pct:.1f}% "
          f"(floor {MIN_TOTAL_COVERAGE:.1f}%)")
    if total_pct < MIN_TOTAL_COVERAGE:
        print("coverage below the floor", file=sys.stderr)
        failed = True

    for rel in TRACKED:
        if rel in FILE_FLOOR_EXEMPT:
            continue
        pct = results[rel]["percent"]
        if pct < MIN_FILE_COVERAGE:
            print(f"{rel} at {pct:.1f}% is below the per-file floor "
                  f"of {MIN_FILE_COVERAGE:.1f}%", file=sys.stderr)
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
