#!/usr/bin/env python3
"""Measure host-test line coverage and keep the README table honest.

The firmware's pure-C core (the gfx rasteriser and the touch coordinate/event
logic) builds and runs on the host, so its coverage is a real number rather
than a guess.  This script builds that suite with gcov instrumentation, runs
it, and renders the result into the block between the ``coverage:start`` and
``coverage:end`` markers in README.md.

    python3 tools/coverage.py            # update README.md in place
    python3 tools/coverage.py --check    # fail if README.md is out of date
    python3 tools/coverage.py --json coverage.json

``--check`` is what CI runs: a README that rewrites itself is one nobody reads
the diff of, so drift fails the build instead of being papered over.
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
README = REPO / "README.md"

START = "<!-- coverage:start -->"
END = "<!-- coverage:end -->"

# Files the host suite is expected to cover, in the order they appear in the
# README table.
TRACKED = [
    "shared/gfx/gfx.c",
    "shared/touch/touch_map.c",
    "shared/ui/ui_theme.c",
    "shared/ui/ui_widgets.c",
    "shared/ui/ui_icons.c",
    "shared/ui/ui_band.c",
    "shared/ui/ui_router.c",
    "shared/ui/splash_screen.c",
    "shared/ui/overview_screen.c",
    "shared/ui/stub_screen.c",
    "shared/settings/settings.c",
    "shared/logfile/log_numbers.c",
    "shared/logfile/log_csv.c",
    "shared/logfile/log_fields.c",
    "shared/link/link_crc.c",
    "shared/link/link_frame.c",
    "shared/link/link_dev.c",
    "shared/link/link_host.c",
]

# Sources that are compiled into the suite but deliberately not measured.
# Anything else that is instrumented and missing from TRACKED is an omission,
# not a decision -- see check_tracked_is_complete().
UNTRACKED_OK = {
    "greatest.h",
    # Test scaffolding, not code under test.  A fake wire at 100% would say
    # nothing about the firmware and would dilute the number that does.
    "fake_wire.c",
}

# Below this, CI fails.  Raise it when the suite gets better; never lower it
# to make a red build go green.
MIN_TOTAL_COVERAGE = 93.0

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
        sys.exit(f"no {wanted} under {BUILD_DIR}; was the suite built and run?")
    return matches[0]


def check_tracked_is_complete() -> None:
    """Every instrumented source must be in TRACKED.

    docs/Testing-and-CI.md and docs/Screens.md both promise that forgetting to
    list a new source fails the coverage check.  It could not: measure() only
    ever looked at TRACKED, so an unlisted file was simply absent from the
    numerator and the denominator both, and the badge stayed green over a
    shrinking fraction of the code.
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
        # it touched; pick out the one we asked about.
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
            sys.exit(f"gcov produced no data for {rel}\n{proc.stdout}\n{proc.stderr}")

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
        "![coverage](https://img.shields.io/badge/host--test%%20coverage-%.1f%%25-%s)"
        % (total_pct, "brightgreen" if total_pct >= MIN_TOTAL_COVERAGE else "orange"),
        "",
        *rows,
        "",
        "_Generated by `tools/coverage.py`; CI runs `--check` and fails on drift._",
        "",
    ])
    return body, total_pct


def splice(text: str, body: str) -> str:
    if START not in text or END not in text:
        sys.exit(f"README.md is missing the {START} / {END} markers")
    head = text.split(START)[0]
    tail = text.split(END)[1]
    return head + START + body + END + tail


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if README.md would change")
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
            {"files": results, "total_percent": round(total_pct, 2)}, indent=2) + "\n")

    original = README.read_text()
    updated = splice(original, body)

    failed = False
    if args.check:
        if updated != original:
            print("README.md coverage table is out of date; run tools/coverage.py",
                  file=sys.stderr)
            failed = True
        else:
            print("README.md coverage table is up to date")
    else:
        README.write_text(updated)
        print(f"updated {README.relative_to(REPO)}")

    print(f"total line coverage: {total_pct:.1f}% (floor {MIN_TOTAL_COVERAGE:.1f}%)")
    if total_pct < MIN_TOTAL_COVERAGE:
        print("coverage below the floor", file=sys.stderr)
        failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
