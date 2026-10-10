#!/usr/bin/env python3
"""Measure host-test line coverage, enforce the floors, and keep the table.

The pure-C code under shared/ and protocols/ builds and runs on the host.
This script builds the suite with gcov instrumentation, runs it, and renders
the result into the block between the ``coverage:start`` and
``coverage:end`` markers in STATUS.md, and the total into the block between
the same markers in README.md and README-de.md.  The README badge comes from
Codecov, which measures the same build in CI (continuous integration).

    python3 tools/coverage.py            # update the table and the figures
    python3 tools/coverage.py --check    # fail if any of them is out of date
    python3 tools/coverage.py --json coverage.json

``--check`` is what CI runs: drift fails the build rather than being
committed by a bot.

Every C file under shared/ and protocols/ has to be in the measurement.  A
file is in
TRACKED and has counters, or is in DATA_ONLY and holds no function.  A file
in neither list, a file the suite does not compile, a TRACKED file no test
links (it has no counters, and is measured at 0%), and a DATA_ONLY file that
holds a function each fail the run.

SPDX-License-Identifier: MIT
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
# The folders whose C files are measured.
SOURCE_DIRS = ("shared", "protocols")
READMES = {"en": REPO / "README.md", "de": REPO / "README-de.md"}

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
    "shared/ui/ui_text.c",
    "shared/ui/ui_router.c",
    "shared/ui/splash_screen.c",
    "shared/ui/overview_screen.c",
    "shared/ui/stub_screen.c",
    "shared/ui/motor_screen.c",
    "shared/ui/supply_screen.c",
    "shared/ui/ui_keypad.c",
    "shared/ui/ui_textkey.c",
    "shared/ui/servo_screen.c",
    "shared/ui/analyser_screen.c",
    "shared/ui/balance_screen.c",
    "shared/ui/battery_screen.c",
    "shared/ui/programmer_screen.c",
    "shared/ui/log_viewer_screen.c",
    "shared/ui/log_select.c",
    "shared/ui/settings_screen.c",
    "shared/ui/outputs_screen.c",
    "shared/ui/picker_screen.c",
    "shared/ui/busfault_screen.c",
    "shared/settings/settings.c",
    "shared/logfile/log_numbers.c",
    "shared/logfile/log_csv.c",
    "shared/logfile/log_fields.c",
    "shared/logfile/log_name.c",
    "shared/safety/heartbeat.c",
    "shared/safety/arming.c",
    "shared/safety/safety_gate.c",
    "shared/safety/touch_loss.c",
    "shared/servo/servo_limit.c",
    "shared/servo/servo_sync.c",
    "shared/servo/servo_sweep.c",
    "shared/servo/servo_move.c",
    "shared/servo/servo_test.c",
    "shared/servo/servo_report.c",
    "shared/openyge/openyge_frame.c",
    "shared/openyge/openyge_status.c",
    "shared/openyge/openyge_params.c",
    "shared/esc/esc_json.c",
    "shared/esc/esc_registry.c",
    "shared/esc/esc_stick.c",
    "shared/esc/esc_sim.c",
    "shared/servo/servo_sim.c",
    "shared/sbus/sbus.c",
    "shared/dshot/dshot_frame.c",
    "shared/dshot/dshot_telem.c",
    "shared/dshot/dshot_edt.c",
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
    "shared/link/link_control.c",
    "shared/link/link_port.c",
    "shared/artwork/art_store.c",
    "shared/artwork/art_fetch.c",
    "shared/bench/bench_state.c",
    "shared/outputs/outputs.c",
    "shared/outputs/outputs_pages.c",
    "shared/outputs/out_bind.c",
    "shared/outputs/out_pwm_map.c",
    "shared/outputs/servo_page.c",
    "shared/outputs/supply_page.c",
    "shared/outputs/out_store_map.c",
    "shared/outputs/out_store_rec.c",
    "shared/outputs/out_stage.c",
    "shared/outputs/bind_link.c",
    "shared/outputs/sense_page.c",
    "shared/outputs/tone_page.c",
    "shared/bench/telemetry_sim.c",
    "shared/bench/supply.c",
    "shared/bench/pdmini.c",
    "shared/bench/supply_link.c",
    "shared/bench/sense_link.c",
    "shared/bench/servo_source.c",
    "shared/bench/tone_link.c",
    "shared/bench/knob.c",
    "shared/bench/log_writer.c",
    "shared/bench/log_cadence.c",
    "shared/sense/sense_bus.c",
    "shared/sense/ina228.c",
    "shared/sense/ina3221.c",
    "shared/sense/as5600.c",
    "shared/sense/sense_sched.c",
    "shared/sense/sense_svc.c",
    "shared/sense/sense_trace.c",
    "shared/sense/tone.c",
    "shared/sense/edge_ring.c",
    "shared/sense/tone_svc.c",
    "protocols/kst/kst_wire.c",
    "protocols/kst/kst_reg.c",
    "protocols/kst/kst_limits.c",
    "protocols/kst/kst_plan.c",
    "protocols/kst/kst_session.c",
]

# Sources that are tables and hold no function: gcc gives them no counter,
# so there is nothing to measure.  Anything else under SOURCE_DIRS that is
# missing from TRACKED is an omission, not a decision -- see
# completeness().
DATA_ONLY = {
    # Glyph bitmaps written by tools/gen_font.py.
    "shared/gfx/gfx_font8x16.c",
    "shared/gfx/gfx_font16x28.c",
    "shared/gfx/gfx_font_num24x30.c",
    # The ESC profile tables written by tools/gen_esc_profiles.py.
    "shared/esc/esc_profiles_gen.c",
    # The German string tables; ui_text.c holds the lookups.
    "shared/ui/ui_text_de.c",
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


def library_sources() -> list[str]:
    """Every C file under SOURCE_DIRS, as TRACKED spells it."""
    return sorted(p.relative_to(REPO).as_posix()
                  for base in SOURCE_DIRS
                  for p in (REPO / base).rglob("*.c"))


def build_files(suffix: str) -> dict[str, pathlib.Path]:
    """Source basename -> its .gcno or .gcda in the build.  CMake mangles
    object paths, so a file is found by basename; only the libraries are
    instrumented, so a test file has neither."""
    return {p.name[: -len(suffix)]: p
            for p in sorted(BUILD_DIR.rglob("*" + suffix))}


def holds_code(gcno: pathlib.Path) -> bool:
    """Whether the translation unit behind @p gcno has a function."""
    proc = subprocess.run(["gcov", "-n", gcno.name], cwd=gcno.parent,
                          capture_output=True, text=True)
    return LINES_RE.search(proc.stdout) is not None


def completeness(sources: list[str], tracked: list[str],
                 data_only: set[str], compiled: set[str],
                 counted: set[str], with_code: set[str]) -> list[str]:
    """What keeps the measurement from covering every source: one line per
    problem.  @p compiled, @p counted and @p with_code are basenames: the
    sources with a .gcno, with a .gcda, and the DATA_ONLY ones whose .gcno
    holds a function.

    measure() looks only at TRACKED, so a file missing from it is absent
    from the total and from the floors.
    """
    problems = []
    names = [pathlib.Path(rel).name for rel in sources]
    for name in sorted({n for n in names if names.count(n) > 1}):
        problems.append(f"two sources under shared/ or protocols/ are "
                        f"named {name}; the build's counters are found "
                        "by that name")
    for rel in sorted(set(tracked) & data_only):
        problems.append(f"{rel} is in TRACKED and in DATA_ONLY")
    for rel in sorted((set(tracked) | data_only) - set(sources)):
        problems.append(f"{rel} is listed in tools/coverage.py and is not "
                        "under shared/ or protocols/")
    for rel in sources:
        name = pathlib.Path(rel).name
        if rel not in tracked and rel not in data_only:
            problems.append(
                f"{rel} is in neither TRACKED nor DATA_ONLY in "
                "tools/coverage.py, so it counts towards neither the total "
                "nor a floor")
        elif name not in compiled:
            problems.append(f"{rel} is not compiled into the host suite: "
                            "the build has no .gcno for it")
        elif rel in data_only and name in with_code:
            problems.append(f"{rel} is in DATA_ONLY and holds a function; "
                            "move it to TRACKED")
        elif rel in tracked and name not in counted:
            problems.append(f"{rel} has no counters: no test links code "
                            "from it, and it is measured at 0%")
    return problems


def measure() -> dict[str, dict[str, float]]:
    results: dict[str, dict[str, float]] = {}
    notes, data = build_files(".gcno"), build_files(".gcda")

    for rel in TRACKED:
        src = REPO / rel
        # A source no test links has no .gcda; its .gcno alone gives the
        # line count, none of them run.
        gcda = data.get(src.name, notes.get(src.name))
        if gcda is None:
            sys.exit(f"no {src.name}.gcno under {BUILD_DIR}; "
                     "was the suite built and run?")
        proc = subprocess.run(
            ["gcov", gcda.name],
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


def thousands(n: int, sep: str) -> str:
    return f"{n:,}".replace(",", sep)


def render_readme(results: dict[str, dict[str, float]], lang: str) -> str:
    """The figure the README carries: the total, what it is a share of, and
    the two floors."""
    lines = sum(int(r["lines"]) for r in results.values())
    covered = sum(int(r["covered"]) for r in results.values())
    pct = (100.0 * covered / lines) if lines else 0.0
    exempt = ", ".join("`%s`" % pathlib.Path(rel).name
                       for rel in sorted(FILE_FLOOR_EXEMPT))
    if lang == "de":
        text = ("Zeilenabdeckung von `shared/` und `protocols/` durch die "
                "Host-Suite: "
                "**%s %%**, %s von %s Zeilen in %d Dateien. CI schlägt unter "
                "%d %% gesamt oder unter %d %% in einer Datei fehl; "
                "ausgenommen von der Grenze je Datei: %s. Die Tabelle je "
                "Datei steht in [STATUS.md](STATUS.md#tests-and-ci)."
                % (("%.1f" % pct).replace(".", ","),
                   thousands(covered, " "), thousands(lines, " "),
                   len(results), MIN_TOTAL_COVERAGE, MIN_FILE_COVERAGE,
                   exempt))
    else:
        text = ("Host-suite line coverage of `shared/` and `protocols/`: "
                "**%.1f%%**, %s of %s lines in %d files. CI fails below "
                "%d%% in total or below %d%% in any file; exempt from the "
                "per-file floor: %s. "
                "[STATUS.md](STATUS.md#tests-and-ci) has the table per file."
                % (pct, thousands(covered, ","), thousands(lines, ","),
                   len(results), MIN_TOTAL_COVERAGE, MIN_FILE_COVERAGE,
                   exempt))
    return "\n" + text + "\n"


def splice(text: str, body: str, start: str, end: str, what: str) -> str:
    if start not in text or end not in text:
        sys.exit(f"{what} is missing the {start} / {end} markers")
    head = text.split(start)[0]
    tail = text.split(end)[1]
    return head + start + body + end + tail


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the STATUS.md table or a README figure "
                         "would change")
    ap.add_argument("--json", type=pathlib.Path,
                    help="also write the raw numbers here")
    ap.add_argument("--skip-build", action="store_true",
                    help="reuse an existing instrumented build")
    args = ap.parse_args()

    if not args.skip_build:
        build_and_run()

    notes, data = build_files(".gcno"), build_files(".gcda")
    incomplete = completeness(
        library_sources(), TRACKED, DATA_ONLY, set(notes), set(data),
        {pathlib.Path(rel).name for rel in DATA_ONLY
         if pathlib.Path(rel).name in notes
         and holds_code(notes[pathlib.Path(rel).name])})

    results = measure()
    body, total_pct = render(results)

    if args.json:
        args.json.write_text(json.dumps(
            {"files": results, "total_percent": round(total_pct, 2)},
            indent=2) + "\n")

    targets = [(STATUS, body, START, END)]
    targets += [(path, render_readme(results, lang), START, END)
                for lang, path in READMES.items()]

    failed = False
    for line in incomplete:
        print(line, file=sys.stderr)
        failed = True
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
