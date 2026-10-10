#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""tools/sense_trace.py against the synthetic console log.

Run by ctest (the `sense_trace_tool` test) with the two host programs the
suite builds:

    python3 test/host/test_sense_trace_tool.py --replay <sense_trace_replay>
        --gen <sense_trace_gen> --fixtures test/host/fixtures --work <dir>

Under test: the checked-in fixtures are what sense_trace_gen writes; the
tool's report on them -- two traces whose counts match, four moves seen
and arrived at every setting, each arrival at the model's own, the
difference to the horn's travel time and its median; the CSV files; a log
with CR LF and with LF line ends; a log cut off inside a trace, with a
sample line missing, with a trace starting inside another, with records
the coprocessor dropped, with no trace, with another format version and
with a trace that has no shunt; times across the 2^32 wrap of the 0.1 ms
count; clipped samples; edge lines with the command line nearest to
each, a command line with no edge line and an edge line with no command
line; commands within 30 ms of each other; a trace with trigger lines not
written, a move with records missing inside it and a slewed command with
its $D line before and after its $C line; what the traces alone give; a
servo CSV that pairs at two offsets, one set by hand, and one without the
columns; two boots whose rows lie in their order, among each other and
alike.

Regenerate the fixtures after a change that moves them:

    <build>/sense_trace_gen /tmp/sim.log /tmp/sim.csv
    tr -d '\\r' < /tmp/sim.log > test/host/fixtures/sense-trace-sim.log
    cp /tmp/sim.csv test/host/fixtures/sense-trace-sim.csv
"""

from __future__ import annotations

import argparse
import importlib.util
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
TOOL = REPO / "tools" / "sense_trace.py"

spec = importlib.util.spec_from_file_location("sense_trace", TOOL)
sense_trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sense_trace)

ARGS: argparse.Namespace
FAILED: list[str] = []
SETTINGS = [(n, band) for n in (1, 4, 8) for band in ("0.02", "0.05", "0.10")]


def check(cond: bool, what: str) -> None:
    if not cond:
        raise AssertionError(what)


def tool(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(TOOL), "--replay", str(ARGS.replay), *args],
        capture_output=True, text=True)


def work(name: str, text: str | None = None) -> pathlib.Path:
    path = ARGS.work / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if text is not None:
        path.write_bytes(text.encode("ascii"))
    return path


def fixture_log() -> str:
    return (ARGS.fixtures / "sense-trace-sim.log").read_text("ascii")


def setting_rows(out: str, after: str) -> dict[tuple[int, str], list[str]]:
    """The nine rows of the table that follows the line @p after."""
    lines = out.splitlines()
    at = next(k for k, line in enumerate(lines) if line.startswith(after))
    rows = {}
    for line in lines[at + 2: at + 11]:
        cells = line.split()
        rows[(int(cells[0]), cells[1])] = cells[2:]
    return rows


# --- the cases ---------------------------------------------------------------

def the_fixtures_are_what_the_generator_writes() -> None:
    log, csv = work("gen/sim.log"), work("gen/sim.csv")
    subprocess.run([str(ARGS.gen), str(log), str(csv)], check=True)
    raw = log.read_bytes()
    check(raw.count(b"\r\n") == raw.count(b"\n") > 9000,
          "the console's lines end CR LF")
    check(raw.replace(b"\r\n", b"\n")
          == (ARGS.fixtures / "sense-trace-sim.log").read_bytes(),
          "fixtures/sense-trace-sim.log is not what sense_trace_gen "
          "writes; see this file's head")
    check(csv.read_bytes()
          == (ARGS.fixtures / "sense-trace-sim.csv").read_bytes(),
          "fixtures/sense-trace-sim.csv is not what sense_trace_gen writes")
    # About 8 bytes a sample, line end included.
    samples = sum(1 for line in raw.split(b"\r\n")
                  if sense_trace.RE_S.match(line.decode("ascii")))
    check(samples == 561 + 8881, f"{samples} sample lines")
    check(len(raw) / samples < 8.7, "more than 8.7 bytes a sample")


def the_report_on_the_fixture() -> None:
    out_dir = work("report")
    r = tool(str(ARGS.fixtures / "sense-trace-sim.log"), "--servo-csv",
             str(ARGS.fixtures / "sense-trace-sim.csv"), "--out",
             str(out_dir))
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    out = r.stdout
    check("2 trace(s), 2 other line(s)" in out, "two traces, two others")
    check("4 travel time(s), 4 paired with a move" in out, "the pairing")
    check("trace 1: trigger key at tick 20400 ms, 561 samples over "
          "560.0 ms, 28 voltages, 0 records missing" in out, "trace 1")
    check("trace 2: trigger cmd at tick 22020 ms, 8881 samples over "
          "8880.0 ms, 444 voltages, 0 records missing" in out, "trace 2")
    check("ended by the console" in out and "ended by its time" in out,
          "how each ended")
    check(out.count("counts match the end line") == 2, "the counts")
    check("PROBLEM" not in out, "no problem")
    check("shunt 0.1 Ohm, Configuration 0x4007, part online, reset count 0"
          in out, "the set-up")
    # The model's noise is 0.02 A peak to peak: 0.0104 A from the mean.
    check("noise of the whole trace, 561 samples:" in out, "noise, trace 1")
    check("filter 1: mean 0.1204 A, standard deviation 0.0058 A, largest "
          "distance from the mean 0.0104 A" in out, "its figures")
    check("move 1 at 0.0 ms, channel 0 to 1800 us: before 0.119 A, holding "
          "0.119 A (idle level), threshold 0.020 A, horn 240 ms" in out,
          "move 1")
    check("move 3 at 3200.0 ms, channel 0 to 1800 us: before 0.120 A, "
          "holding 0.120 A (held before move 2), threshold 0.020 A, horn "
          "490 ms" in out, "move 3 takes the level its end was held at")
    # The model's horn takes 250 ms for 300 us and 500 ms for 600 us; a
    # mean of n samples shows the fall n - 1 samples later.
    arrival = setting_rows(out, "  arrival, ms from the command:")
    diff = setting_rows(out, "  arrival less the horn's travel time")
    total = setting_rows(out, "all traces, 4 move(s):")
    for n, band in SETTINGS:
        late = n - 1
        check(arrival[(n, band)] == [
            "4/4", "4/4", f"{250 + late}.0", f"{500 + late}.0",
            f"{500 + late}.0", f"{500 + late}.0"], f"arrival at {n} {band}")
        check(diff[(n, band)] == [f"+{10 + late}.0"] * 4,
              f"difference at {n} {band}")
        check(total[(n, band)] == ["4/4", "4/4", f"+{10 + late}.0", "ms",
                                   "over", "4", "move(s)"],
              f"median at {n} {band}")
    # The CSV files: a row a sample, the voltage on the rows that have one.
    one = (out_dir / "sense-trace-sim-trace-1.csv").read_text().splitlines()
    two = (out_dir / "sense-trace-sim-trace-2.csv").read_text().splitlines()
    check(one[0] == "time (ms);current (A);bus voltage (V)", "the header")
    check(len(one) == 1 + 561 and len(two) == 1 + 8881, "a row a sample")
    check(one[1] == "-61.0;0.1148;", f"the first row: {one[1]}")
    check(sum(1 for row in one[1:] if not row.endswith(";")) == 28,
          "28 voltages in trace 1")
    check(sum(1 for row in two[1:] if not row.endswith(";")) == 444,
          "444 voltages in trace 2")
    volts = [float(row.split(";")[2]) for row in two[1:]
             if not row.endswith(";")]
    # 6.0 V less 0.4 V an ampere: 5.952 V holding, 5.62 V moving.
    check(max(volts) == 5.952 and min(volts) == 5.616, "the rail's sag")
    times = [float(row.split(";")[0]) for row in two[1:]]
    check(all(b - a == 1.0 for a, b in zip(times, times[1:], strict=False)),
          "1 ms a row")
    check(max(float(row.split(";")[1]) for row in two[1:]) > 0.94,
          "the travel current")


def cr_lf_and_lf_read_the_same() -> None:
    lf = work("eol/lf.log", fixture_log())
    crlf = work("eol/crlf.log", fixture_log().replace("\n", "\r\n"))
    a = tool(str(lf), "--no-csv")
    b = tool(str(crlf), "--no-csv")
    check(a.returncode == 0 and b.returncode == 0, "both read")
    check(a.stdout.splitlines()[1:] == b.stdout.splitlines()[1:],
          "the same report")
    check(not list((ARGS.work / "eol").glob("*.csv")), "--no-csv writes none")


def a_log_cut_off_inside_a_trace() -> None:
    lines = fixture_log().splitlines()
    cut = work("cut.log", "\n".join(lines[:-1]) + "\n")
    r = tool(str(cut))
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: no end line: the log stops inside the trace"
          in r.stdout, r.stdout[-400:])
    check("1 trace(s) with a problem" in r.stderr, r.stderr)
    # What was read is still written.
    check((ARGS.work / "cut-trace-2.csv").exists(), "the CSV of the rest")


def a_missing_line_does_not_match_the_end_line() -> None:
    lines = fixture_log().splitlines()
    at = next(k for k, line in enumerate(lines) if line == "10,302")
    r = tool(str(work("short.log",
                      "\n".join(lines[:at] + lines[at + 1:]) + "\n")),
             "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: 560 sample line(s) read, the end line counts 561"
          in r.stdout, "the sample count")
    # A sample line the terminal lost inside the second trace's first
    # move: nobody knows where, so no move of that trace is replayed.
    move = next(k for k, line in enumerate(lines) if line.startswith("$C"))
    r = tool(str(work("short3.log", "\n".join(
        lines[:move + 200] + lines[move + 201:]) + "\n")), "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("no move of this trace is replayed" in r.stdout
          and "arrival, ms from the command" not in r.stdout
          and "all traces" not in r.stdout, "not replayed")
    volt = next(k for k, line in enumerate(lines) if line.startswith("v"))
    mark = next(k for k, line in enumerate(lines) if line.startswith("$C"))
    text = "\n".join(line for k, line in enumerate(lines)
                     if k not in (volt, mark)) + "\n"
    r = tool(str(work("short2.log", text)), "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: 27 voltage line(s) read, the end line counts 28"
          in r.stdout, "the voltage count")
    check("PROBLEM: 3 trigger line(s) read, the end line counts 4"
          in r.stdout, "the trigger count")


def a_trace_that_starts_inside_another() -> None:
    lines = fixture_log().splitlines()
    end = next(k for k, line in enumerate(lines) if line.startswith("$Z"))
    r = tool(str(work("nested.log",
                      "\n".join(lines[:end] + lines[end + 1:]) + "\n")),
             "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: no end line: the next trace starts inside it"
          in r.stdout, "said")
    check("trace 2:" in r.stdout and r.stdout.count("counts match") == 1,
          "the trace after it is read")


def dropped_records_are_counted_against_the_end_line() -> None:
    lines = fixture_log().splitlines()
    at = next(k for k, line in enumerate(lines) if line == "10,302")
    end = next(k for k, line in enumerate(lines) if line.startswith("$Z"))
    gap = lines[:at] + ["$L n=3", "40," + lines[at + 3].split(",")[1]]
    body = gap + lines[at + 4:end] + ["$Z n=1 s=558 v=28 l=3 m=1 ml=0 e=k"]
    r = tool(str(work("lost.log", "\n".join(body + lines[end + 1:]) + "\n")))
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[:600]}")
    check("558 samples over 560.0 ms, 28 voltages, 3 records missing"
          in r.stdout, "the gap keeps its time")
    rows = (ARGS.work / "lost-trace-1.csv").read_text().splitlines()
    check(rows[1].startswith("-61.0;") and rows[2].startswith("-57.0;"),
          f"4 ms across the gap: {rows[1]} {rows[2]}")
    # The end line counting another number of them is a problem.
    body[-1] = "$Z n=1 s=558 v=28 l=2 m=1 ml=0 e=k"
    r = tool(str(work("lost2.log", "\n".join(body) + "\n")), "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: the $L lines count 3 missing record(s), the end line 2"
          in r.stdout, "said")


def a_log_with_no_trace() -> None:
    r = tool(str(work("none.log", "rcbench-iomcu: CAN up\n10,300\nv6000\n")))
    check(r.returncode == 1, f"exit {r.returncode}")
    check("0 trace(s), 3 other line(s)" in r.stdout, r.stdout)
    check("no trace in the log" in r.stderr, r.stderr)
    r = tool(str(ARGS.work / "not-there.log"))
    check(r.returncode == 1 and "sense_trace:" in r.stderr, "a missing file")


def another_version_and_a_trace_without_a_shunt() -> None:
    text = ("$T v=3 n=7 trig=key t=100 ms=10 len=10000\n"
            "$H dt_us=1000 shunt_uohm=0 cfg=0x0000 on=0 rst=0\n"
            "$K t=100\n"
            "$Z n=7 s=0 v=0 l=0 m=1 ml=0 e=t\n")
    r = tool(str(work("v3.log", text)))
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: format version 3; this tool reads version 2"
          in r.stdout, "the version")
    check("part not online" in r.stdout, "the part")
    check(not (ARGS.work / "v3-trace-7.csv").exists(),
          "no CSV without a shunt")
    # A header lost by the terminal.
    r = tool(str(work("noh.log", "$T v=2 n=1 trig=key t=100 ms=10 len=10\n"
                                 "$Z n=1 s=0 v=0 l=0 m=0 ml=0 e=t\n")))
    check(r.returncode == 1 and "PROBLEM: no $H line" in r.stdout, "no $H")
    r = tool(str(work("otherz.log",
                      "$T v=2 n=1 trig=key t=100 ms=10 len=10\n"
                      "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 "
                      "rst=0\n$Z n=2 s=0 v=0 l=0 m=0 ml=0 e=t\n")))
    check(r.returncode == 1
          and "PROBLEM: the end line is trace 2's" in r.stdout, "another n")


def synthetic(t0: int, moves: list[tuple[int, int]], edges: bool = False,
              high: int = 2000, edge_after: int = 120) -> str:
    """A trace at @p t0: 0.12 A held, and after each (ms, us) command a
    burst of @p high codes for 100 ms.  With @p edges an edge line
    @p edge_after tenths of a ms after each command line's time."""
    out = [f"$T v=2 n=1 trig=cmd t={t0} ms=0 len=4000",
           "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=0"]
    n = 0
    last = moves[-1][0] + 600
    for ms in range(-60, last):
        for at, us in moves:
            if ms == at:
                t = (t0 + at * 10) & 0xFFFFFFFF
                out.append(f"$C t={t} ch=2 us={us}")
                if edges:
                    out.append(f"$E t={(t + edge_after) & 0xFFFFFFFF}")
        moving = any(at + 13 <= ms < at + 113 for at, _ in moves)
        out.append(f"{-600 if n == 0 else 10},{high if moving else 300}")
        n += 1
    marks = len(moves) * (2 if edges else 1)
    out.append(f"$Z n=1 s={n} v=0 l=0 m={marks} ml=0 e=t")
    return "\n".join(out) + "\n"


def times_run_across_the_wrap_of_the_count() -> None:
    # The trigger 20 ms before the 0.1 ms count wraps; the second command
    # 1 s after it.
    t0 = (1 << 32) - 200
    traces, other = sense_trace.parse_log(
        synthetic(t0, [(0, 1900), (1000, 1100)]))
    check(other == 0 and len(traces) == 1, "one trace")
    tr = traces[0]
    sense_trace.check(tr)
    check(not tr.problems, str(tr.problems))
    check([m[1] for m in tr.marks] == [0, 10000], "the commands' times")
    check(tr.t[0] == -600 and tr.t[-1] == tr.t[0] + 10 * (len(tr.t) - 1),
          "the samples' times")
    r = tool(str(work("wrap.log", synthetic(t0, [(0, 1900), (1000, 1100)]))),
             "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["2/2", "2/2", "113.0", "113.0"],
          f"the arrivals: {arrival[(1, '0.05')]}")
    # A second trace later in the log lies later, wrap or not.
    two = (synthetic(t0, [(0, 1900)])
           + synthetic(5000, [(0, 1100)]).replace(" n=1 ", " n=2 "))
    traces, _ = sense_trace.parse_log(two)
    check(traces[1].t0_abs - traces[0].t0_abs == 5200, "0.52 s on")


def clipped_samples_are_said_and_time_no_arrival() -> None:
    r = tool(str(work("clip.log", synthetic(1000, [(0, 1900)], high=4095))),
             "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    check("100 sample(s) at the end of the range: at or past it"
          in r.stdout, "said")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    # Seen at the first clipped sample; arrived at the first with a value.
    check(arrival[(1, "0.05")] == ["1/1", "1/1", "113.0"],
          f"{arrival[(1, '0.05')]}")
    # The last code before the end is a value: nothing is said, and the
    # bottom's end code is a clip as the top's is.
    r = tool(str(work("near.log", synthetic(1000, [(0, 1900)], high=4094))),
             "--no-csv")
    check("at the end of the range" not in r.stdout, "4094 is a value")
    r = tool(str(work("low.log", synthetic(1000, [(0, 1900)], high=-4096))),
             "--no-csv")
    check("100 sample(s) at the end of the range" in r.stdout, "-4096")
    r = tool(str(work("low2.log", synthetic(1000, [(0, 1900)], high=-4095))),
             "--no-csv")
    check("at the end of the range" not in r.stdout, "-4095 is a value")


def edge_lines_are_the_moves_when_there_are_any() -> None:
    text = synthetic(1000, [(0, 1900), (1000, 1100)], edges=True)
    r = tool(str(work("edge.log", text)), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    # Each edge 12 ms after its command line, with that line's pulse.
    check("move 1 at 12.0 ms, channel 2 to 1900 us" in r.stdout, "move 1")
    check("move 2 at 1012.0 ms, channel 2 to 1100 us" in r.stdout, "move 2")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["2/2", "2/2", "101.0", "101.0"],
          f"{arrival[(1, '0.05')]}")


def an_edge_takes_the_command_line_nearest_to_it() -> None:
    # A command line's time one frame late: 20 ms after its edge.  The
    # edge still has its channel and pulse, and with them the level its
    # end was held at.
    moves = [(0, 1900), (1000, 1100), (2000, 1900)]
    r = tool(str(work("late.log", synthetic(1000, moves, edges=True,
                                            edge_after=-200))), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    check("move 1 at -20.0 ms, channel 2 to 1900 us" in r.stdout, "move 1")
    check("move 3 at 1980.0 ms, channel 2 to 1900 us" in r.stdout
          and "(held before move 2)" in r.stdout, "move 3's level")
    # 31 ms apart is another command's: the edge is a move with no
    # channel, and the command a move at its own time.
    r = tool(str(work("far.log", synthetic(1000, moves, edges=True,
                                           edge_after=-310))), "--no-csv")
    check("move 1 at -31.0 ms, edge" in r.stdout, r.stdout[-900:])
    check("move 2 at 0.0 ms, channel 2 to 1900 us" in r.stdout
          and "move 6 at 2000.0 ms, channel 2 to 1900 us" in r.stdout,
          "the commands are moves")


def a_command_with_no_edge_line_is_a_move() -> None:
    # Three commands, an edge line for the first and the third: the
    # second is a move at its command line's time.
    lines = synthetic(1000, [(0, 1900), (1000, 1100), (2000, 1900)],
                      edges=True).splitlines()
    at = [k for k, line in enumerate(lines) if line.startswith("$E")][1]
    del lines[at]
    lines[-1] = lines[-1].replace("m=6", "m=5")
    r = tool(str(work("noedge.log", "\n".join(lines) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[-600:]}")
    check("move 1 at 12.0 ms, channel 2 to 1900 us" in r.stdout, "move 1")
    check("move 2 at 1000.0 ms, channel 2 to 1100 us" in r.stdout, "move 2")
    check("move 3 at 2012.0 ms, channel 2 to 1900 us: before 0.120 A, "
          "holding 0.120 A (held before move 2)" in r.stdout, "move 3")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["3/3", "3/3", "101.0", "113.0", "101.0"],
          f"{arrival[(1, '0.05')]}")
    # A command line is one edge's: of two edge lines within 30 ms of it
    # the nearer has its channel and pulse, the other none.
    lines = synthetic(1000, [(0, 1900), (1000, 1100)],
                      edges=True).splitlines()
    at = next(k for k, line in enumerate(lines) if line.startswith("$E"))
    t = int(lines[at].split("=")[1])
    lines.insert(at + 1, f"$E t={t + 100}")
    lines[-1] = lines[-1].replace("m=4", "m=5")
    r = tool(str(work("twoedge.log", "\n".join(lines) + "\n")), "--no-csv")
    check("move 1 at 12.0 ms, channel 2 to 1900 us" in r.stdout
          and "move 2 at 22.0 ms, edge" in r.stdout
          and "move 3 at 1012.0 ms, channel 2 to 1100 us" in r.stdout,
          r.stdout[-900:])


def edges_and_commands_are_paired_as_many_as_can_be() -> None:
    limit = 300
    pair = sense_trace.pair_near
    # Commands at 80 and 115 ms, edges at 100 and 140 ms: the nearest
    # pair of all, 100 with 115, leaves the edge at 140 ms none.
    check(pair([1000, 1400], [800, 1150], limit) == {0: 0, 1: 1},
          f"{pair([1000, 1400], [800, 1150], limit)}")
    # The same with the lists not in the order of their times.
    check(pair([1400, 1000], [1150, 800], limit) == {0: 0, 1: 1}, "order")
    # The limit itself pairs, 0.1 ms more does not.
    check(pair([1000], [700], limit) == {0: 0}, "at the limit")
    check(pair([1000], [699], limit) == {}, "past it")
    check(pair([1000], [1300], limit) == {0: 0}, "at the limit, after")
    check(pair([1000], [1301], limit) == {}, "past it, after")
    # With as many pairs either way, the least distance in sum: of two
    # edges at one command the nearer has it, and of two commands at one
    # edge the nearer.
    check(pair([1000, 1100], [1090], limit) == {1: 0}, "two edges")
    check(pair([1000], [900, 1020], limit) == {0: 1}, "two commands")
    # A chain: every line within the limit of two others, one way to pair
    # all three.
    check(pair([1000, 1250, 1500], [1240, 1490, 1740], limit)
          == {0: 0, 1: 1, 2: 2}, "the chain")
    # None of either.
    check(pair([], [5], limit) == {} and pair([5], [], limit) == {}, "none")
    # Lines at one time: each edge one command.
    check(sorted(pair([1000, 1000], [1000, 1000], limit).values()) == [0, 1],
          "equal times")
    # Through the tool: the commands' lines 20 ms and 25 ms before their
    # edges, 35 ms apart.
    out = synthetic(1000, [(0, 1900), (35, 1100), (1000, 1500)]
                    ).splitlines()
    # Edge lines at 20 ms and 60 ms, each behind the samples of before
    # it: 15 sample lines before the second command's line, and 25 after.
    second = next(k for k, line in enumerate(out)
                  if line.startswith("$C t=1350 "))
    out.insert(second + 26, "$E t=1600")
    out.insert(second - 15, "$E t=1200")
    out[-1] = out[-1].replace("m=3", "m=5")
    r = tool(str(work("chain.log", "\n".join(out) + "\n")), "--no-csv")
    check("move 1 at 20.0 ms, channel 2 to 1900 us" in r.stdout
          and "move 2 at 60.0 ms, channel 2 to 1100 us" in r.stdout
          and "move 3 at 1000.0 ms, channel 2 to 1500 us" in r.stdout,
          r.stdout[-1200:])
    check(", edge" not in r.stdout, "no edge without its command")


def commands_within_30_ms_are_not_replayed() -> None:
    # Two outputs commanded in one frame, and one alone 1 s later.
    lines = synthetic(1000, [(0, 1900), (1000, 1100)]).splitlines()
    at = next(k for k, line in enumerate(lines) if line.startswith("$C"))
    lines.insert(at + 1, lines[at].replace("ch=2", "ch=3"))
    lines[-1] = lines[-1].replace("m=2", "m=3")
    r = tool(str(work("frame.log", "\n".join(lines) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[-600:]}")
    for n, ch in ((1, 2), (2, 3)):
        check(f"move {n} at 0.0 ms, channel {ch} to 1900 us: another "
              "command within 30 ms, the current is of both, not replayed"
              in r.stdout, f"move {n}")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["1/3", "1/3", "-", "-", "113.0"],
          f"{arrival[(1, '0.05')]}")
    # 30 ms apart is together.  31 ms is not: both are replayed, the
    # first ends at the second, and the second starts inside the first's
    # current and does not come back to its level.
    for apart, want in ((30, ["1/3", "1/3", "-", "-", "113.0"]),
                        (31, ["2/3", "1/3", "cut", "cut", "113.0"])):
        text = synthetic(1000, [(0, 1900), (apart, 1100), (1000, 1500)])
        r = tool(str(work(f"apart{apart}.log", text)), "--no-csv")
        arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
        check(arrival[(1, "0.05")] == want,
              f"{apart} ms: {arrival[(1, '0.05')]}")


def the_traces_alone_say_which_settings_time_every_move() -> None:
    r = tool(str(ARGS.fixtures / "sense-trace-sim.log"), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    out = r.stdout
    check("not compared with the horn: no --servo-csv given. An arrival "
          "is when the current is back at the holding level, which can "
          "lead or lag the horn." in out, "said plainly")
    check("arrival less the horn" not in out
          and "median difference to the horn" not in out, "no comparison")
    every = ", ".join(f"filter {n} band {band} A" for n, band in SETTINGS)
    check("settings that see every move and time its arrival: " + every
          in out, "all nine")
    med = setting_rows(out, "median arrival, and the median distance from "
                            "filter 4 band 0.05 A")
    for n, band in SETTINGS:
        late = n - 1
        check(med[(n, band)] == [f"{500 + late}.0", "4", f"{late - 3:+d}.0",
                                 "4"],
              f"median at {n} {band}: {med[(n, band)]}")
    check("latest less earliest arrival of one move across the settings: "
          "median 7.0 ms, largest 7.0 ms over 4 move(s)" in out,
          "the spread")
    # A floor no move passes: no setting times every move.
    r = tool(str(ARGS.fixtures / "sense-trace-sim.log"), "--no-csv",
             "--floor", "1.5")
    check("settings that see every move and time its arrival: none"
          in r.stdout, "none")
    # Rows that pair with no move: said, and not compared.
    rows = (ARGS.fixtures / "sense-trace-sim.csv").read_text()
    r = tool(str(ARGS.fixtures / "sense-trace-sim.log"), "--no-csv",
             "--servo-csv", str(work("off.csv", rows)), "--csv-offset", "0")
    check("not compared with the horn: no row of the servo test's CSV "
          "file pairs with a move." in r.stdout, "no row pairs")


def a_trace_with_trigger_lines_not_written_is_not_replayed() -> None:
    lines = synthetic(1000, [(0, 1900), (1000, 1100)]).splitlines()
    lines[-1] = lines[-1].replace("ml=0", "ml=1")
    r = tool(str(work("ml.log", "\n".join(lines) + "\n")), "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: 1 trigger line(s) were not written: a move may be "
          "missing" in r.stdout
          and "no move of this trace is replayed" in r.stdout, "said")
    check("arrival, ms from the command" not in r.stdout
          and "settings that see every move" not in r.stdout,
          "no replay")


def a_move_with_records_missing_inside_it_is_not_replayed() -> None:
    lines = synthetic(1000, [(0, 1900), (1000, 1100)]).splitlines()
    # 3 records dropped 50 ms into the first move's burst.
    first = next(k for k, line in enumerate(lines) if line == "10,2000")
    at = first + 50
    body = (lines[:at] + ["$L n=3", "40,2000"] + lines[at + 4:-1])
    n = sum(1 for line in body if sense_trace.RE_S.match(line))
    body.append(f"$Z n=1 s={n} v=0 l=3 m=2 ml=0 e=t")
    r = tool(str(work("gap.log", "\n".join(body) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[-600:]}")
    check("move 1 at 0.0 ms, channel 2 to 1900 us: records are missing "
          "among its samples, not replayed" in r.stdout, "said")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["1/2", "1/2", "-", "113.0"],
          f"{arrival[(1, '0.05')]}")
    check("1 move(s) not replayed; they count as not seen below"
          in r.stdout, "counted")
    check("settings that see every move and time its arrival: none"
          in r.stdout, "no setting sees every move")


def gap(lines: list[str], at: int) -> list[str]:
    """@p lines with the 3 sample lines before line @p at dropped."""
    code = lines[at].split(",")[1]
    body = lines[:at - 3] + ["$L n=3", f"40,{code}"] + lines[at + 1:-1]
    n = sum(1 for line in body if sense_trace.RE_S.match(line))
    return body + [f"$Z n=1 s={n} v=0 l=3 m=2 ml=0 e=t"]


def records_missing_at_the_ends_of_a_moves_samples() -> None:
    lines = synthetic(1000, [(0, 1900), (1000, 1100)]).splitlines()
    cmd2 = next(k for k, line in enumerate(lines)
                if line.startswith("$C") and "us=1100" in line)
    # The last 3 samples before the second command: the end of the first
    # move's samples, and inside the 50 ms the second takes its level
    # from.
    r = tool(str(work("gap-end.log", "\n".join(gap(lines, cmd2 - 1)) + "\n")),
             "--no-csv")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["0/2", "0/2", "-", "-"],
          f"before the command: {arrival[(1, '0.05')]}")
    # The 3 samples before the first of the second move's 50 ms: the
    # first move's samples, and the edge of the second's.
    r = tool(str(work("gap-edge.log",
                      "\n".join(gap(lines, cmd2 - 50)) + "\n")), "--no-csv")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["0/2", "0/2", "-", "-"],
          f"at the window's edge: {arrival[(1, '0.05')]}")
    # 100 ms before the second command: the first move alone.
    r = tool(str(work("gap-one.log",
                      "\n".join(gap(lines, cmd2 - 100)) + "\n")), "--no-csv")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["1/2", "1/2", "-", "113.0"],
          f"the first alone: {arrival[(1, '0.05')]}")


def a_move_the_part_went_offline_in_is_not_replayed() -> None:
    lines = synthetic(1000, [(0, 1900), (1000, 1100)]).splitlines()
    first = next(k for k, line in enumerate(lines) if line == "10,2000")
    # Offline 50 ms into the first burst, back 30 ms of samples later.
    body = (lines[:first + 50] + ["$S on=0 rst=0"]
            + lines[first + 50:first + 80] + ["$S on=1 rst=0"]
            + lines[first + 80:])
    r = tool(str(work("offline.log", "\n".join(body) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[-500:]}")
    check("move 1 at 0.0 ms, channel 2 to 1900 us: the part's state "
          "changed inside it, not replayed" in r.stdout, "said")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["1/2", "1/2", "-", "113.0"],
          f"{arrival[(1, '0.05')]}")


def settings_are_compared_on_the_moves_both_time() -> None:
    # Two moves: a burst of 100 ms, and one of a single sample 0.04 A
    # above the level, which a mean of 4 samples does not lift past the
    # threshold of 0.020 A: the second is timed by filter 1 alone.
    lines = synthetic(1000, [(0, 1900), (1000, 1100)], high=400).splitlines()
    cmd2 = next(k for k, line in enumerate(lines)
                if line.startswith("$C") and "us=1100" in line)
    quiet = 0
    for k in range(cmd2, len(lines)):
        if lines[k] == "10,400":
            quiet += 1
            if quiet > 1:
                lines[k] = "10,300"
    r = tool(str(work("subset.log", "\n".join(lines) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[-500:]}")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")][:2] == ["2/2", "2/2"]
          and arrival[(4, "0.05")][:2] == ["1/2", "1/2"]
          and arrival[(8, "0.05")][:2] == ["1/2", "1/2"],
          f"{arrival[(1, '0.05')]} {arrival[(8, '0.05')]}")
    med = setting_rows(r.stdout, "median arrival, and the median distance "
                                 "from filter 4 band 0.05 A")
    # Filter 1 times both moves, the reference one of them: the distance
    # is over that one move, 1 ms, not the medians' 50.5 ms.
    check(med[(1, "0.05")] == ["63.5", "2", "-1.0", "1"],
          f"filter 1: {med[(1, '0.05')]}")
    check(med[(4, "0.05")] == ["114.0", "1", "+0.0", "1"],
          f"the reference: {med[(4, '0.05')]}")
    check(med[(8, "0.05")][1] == "1" and med[(8, "0.05")][3] == "1",
          f"filter 8: {med[(8, '0.05')]}")


def a_slewed_command_ends_at_its_d_line() -> None:
    # Three commands on a slewed channel: each $C line has the slew's
    # first pulse, each $D line the pulse it ended at.
    moves = [(0, 1501), (1000, 1799), (2000, 1101)]
    ends = {1501: 1900, 1799: 1100, 1101: 1900}
    lines = []
    for line in synthetic(1000, moves).splitlines():
        lines.append(line)
        m = sense_trace.RE_MARK.match(line)
        if m and m.group(1) == "C":
            # The last change before the frame the $C line is timed at.
            lines.append(f"$D t={int(m.group(2)) - 150} ch=2 "
                         f"us={ends[int(m.group(4))]}")
    lines[-1] = lines[-1].replace("m=3", "m=6")
    r = tool(str(work("slew.log", "\n".join(lines) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[-600:]}")
    check("move 1 at 0.0 ms, channel 2 to 1900 us" in r.stdout, "move 1")
    check("move 2 at 1000.0 ms, channel 2 to 1100 us" in r.stdout, "move 2")
    # Back at 1900 us: the level it was held at before move 2.
    check("move 3 at 2000.0 ms, channel 2 to 1900 us: before 0.120 A, "
          "holding 0.120 A (held before move 2)" in r.stdout,
          "move 3's level")
    # The lines in the order of their times, each $D line before its $C
    # line: the same moves.
    swapped = list(lines)
    for k in range(len(swapped) - 1):
        if swapped[k].startswith("$C") and swapped[k + 1].startswith("$D"):
            swapped[k], swapped[k + 1] = swapped[k + 1], swapped[k]
    check(swapped != lines, "swapped")
    s = tool(str(work("slew2.log", "\n".join(swapped) + "\n")), "--no-csv")
    check(s.returncode == 0 and s.stdout.splitlines()[1:]
          == r.stdout.splitlines()[1:], "the same report")
    # A $D line of another channel, and one with no command before it,
    # change no move.
    odd = list(lines)
    at = next(k for k, line in enumerate(odd) if line.startswith("$C"))
    odd.insert(at, "$D t=100 ch=2 us=1234")
    odd.insert(at + 2, odd[at + 1].replace("ch=2", "ch=7")
               .replace("$C", "$D").replace("us=1501", "us=1777"))
    odd[-1] = odd[-1].replace("m=6", "m=8")
    r = tool(str(work("slew3.log", "\n".join(odd) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[-600:]}")
    check("move 1 at 0.0 ms, channel 2 to 1900 us" in r.stdout
          and "1234" not in r.stdout and "1777" not in r.stdout, "move 1")


def a_move_with_no_end_reads_cut_and_one_unseen_unseen() -> None:
    # The second command 50 ms into the first move's burst; the third
    # with no burst at all.
    text = synthetic(1000, [(0, 1900), (63, 1100), (2000, 1900)])
    lines = text.splitlines()
    at = next(k for k, line in enumerate(lines)
              if line.startswith("$C") and "us=1900" in line and k > 100)
    quiet = lines[:at + 1] + ["10,300" if "," in line else line
                              for line in lines[at + 1:]]
    r = tool(str(work("cutmove.log", "\n".join(quiet) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["2/3", "1/3", "cut", "113.0", "cut"],
          f"{arrival[(1, '0.05')]}")


def the_servo_csv_pairs_by_time() -> None:
    log = str(ARGS.fixtures / "sense-trace-sim.log")
    rows = (ARGS.fixtures / "sense-trace-sim.csv").read_text().splitlines()
    # Two rows of four equally spaced moves fit one move along as well.
    two = work("two.csv", "\n".join([rows[0], rows[2], rows[3]]) + "\n")
    r = tool(log, "--servo-csv", str(two), "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: another offset between the two clocks pairs as many"
          in r.stdout, "the tie is said")
    check("more than one way" in r.stderr, r.stderr)
    # Set by hand: the CSV's clock is 12.345 s behind.
    r = tool(log, "--servo-csv", str(two), "--csv-offset", "12.345",
             "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    check("2 travel time(s), 2 paired with a move" in r.stdout, "paired")
    diff = setting_rows(r.stdout, "  arrival less the horn's travel time")
    check(diff[(4, "0.05")] == ["-", "+13.0", "+13.0", "-"],
          f"moves 2 and 3: {diff[(4, '0.05')]}")
    total = setting_rows(r.stdout, "all traces, 4 move(s):")
    check(total[(4, "0.05")][2:] == ["+13.0", "ms", "over", "2", "move(s)"],
          "the median over the two")
    # Two rows nearest to one move: with the offset given the nearer one
    # is the move's, and the count is of moves.  With the offset to find
    # either row may be the move's: two answers.
    twice = work("twice.csv", "\n".join(
        [rows[0], rows[1], rows[1].replace("10.055", "10.105")]
        + rows[2:]) + "\n")
    check(twice.read_text().count("\n") == 6, "a row more")
    for extra, code in (([], 1), (["--csv-offset", "12.345"], 0)):
        r = tool(log, "--servo-csv", str(twice), "--no-csv", *extra)
        check(r.returncode == code, f"{extra}: exit {r.returncode}")
        check("5 travel time(s), 4 paired with a move" in r.stdout,
              f"{extra}: {r.stdout.splitlines()[1]}")
        check(("PROBLEM: another offset" in r.stdout) == (code == 1),
              f"{extra}: the tie")
    diff = setting_rows(r.stdout, "  arrival less the horn's travel")
    check(diff[(1, "0.05")] == ["+10.0"] * 4, f"{diff[(1, '0.05')]}")
    # Spacings that differ between the files: commands 1.6 s apart in the
    # trace, rows 1.8 s apart.  Only an offset between the two that put a
    # row on a command pairs both.
    def shifted(row: str, by: float) -> str:
        cells = row.split(";")
        cells[0] = f"{float(cells[0]) + by:.3f}"
        return ";".join(cells)
    apart = work("apart.csv", "\n".join(
        [rows[0], shifted(rows[2], -0.1), shifted(rows[3], 0.1)]) + "\n")
    r = tool(log, "--servo-csv", str(apart), "--no-csv", "--csv-offset",
             "12.345")
    check("2 travel time(s), 2 paired with a move" in r.stdout, "by hand")
    r = tool(log, "--servo-csv", str(apart), "--no-csv", "--pair-ms", "120")
    check("2 travel time(s), 2 paired with a move" in r.stdout,
          f"found: {r.stdout.splitlines()[1]}")
    # An offset that pairs nothing, and a decimal comma.
    r = tool(log, "--servo-csv", str(two), "--csv-offset", "0", "--no-csv")
    check(r.returncode == 0 and "0 paired with a move" in r.stdout
          and "arrival less" not in r.stdout, "nothing paired")
    comma = work("comma.csv", "\n".join(
        [rows[0]] + [row.replace(".", ",") for row in rows[1:]]) + "\n")
    r = tool(log, "--servo-csv", str(comma), "--no-csv")
    check(r.returncode == 0 and "4 paired with a move" in r.stdout,
          "a decimal comma")
    # A file without the encoder's column.
    bare = work("bare.csv", "time (s);test;step\n1.0;a;b\n")
    r = tool(log, "--servo-csv", str(bare), "--no-csv")
    check(r.returncode != 0 and "travel angle (ms)" in r.stderr, r.stderr)


def the_offset_that_pairs_the_most_is_found_between_the_obvious() -> None:
    class At:
        def __init__(self, abs_s: float) -> None:
            self.abs_s = abs_s
            self.horn_ms = None
            self.horn_row = None
    # Commands at 0.22, 0.32, 0.49 and 0.85 s; rows whose commands lie at
    # 0, 0.09 and 0.14 s.  An offset of 0.27 s pairs all three, each with
    # a move of its own; no offset that puts a row on a command does.
    moves = [At(t) for t in (0.22, 0.32, 0.49, 0.85)]
    rows = [(c + 0.1 + 0.1, 100.0) for c in (0.0, 0.09, 0.14)]
    n, _ = sense_trace.pair_rows(moves, rows, 0.15, None)
    check(n == 3, f"{n} paired")
    check([mv.horn_row for mv in moves[:3]] == [0, 1, 2],
          f"{[mv.horn_row for mv in moves]}")


def an_offset_that_puts_rows_at_the_tolerance_itself_is_tried() -> None:
    class At:
        def __init__(self, abs_s: float) -> None:
            self.abs_s = abs_s
            self.horn_ms = None
            self.horn_row = None

    def rows_at(*cmds: float) -> list[tuple[float, float]]:
        # The command lies the travel time and the 100 ms hold before
        # the row: both exact in binary.
        return [(c + 0.5, 400.0) for c in cmds]
    # Commands at 0 and 1 s, rows whose commands lie at 0 and 0.75 s, 125
    # ms allowed: only the offset of 0.125 s pairs both, each at the
    # tolerance itself.
    moves = [At(0.0), At(1.0)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.75), 0.125, None)
    check(n == 2 and not tie, f"{n} paired, tie {tie}")
    check([mv.horn_row for mv in moves] == [0, 1], "each its own")
    # 1 ms less allowed: no offset pairs both.
    moves = [At(0.0), At(1.0)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.75), 0.124, None)
    check(n == 1 and tie, f"{n} paired, tie {tie}")
    # Times that are not exact in binary: 0.3 s apart and 0.1 s apart,
    # 100 ms allowed.
    moves = [At(0.1), At(0.4)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.7, 0.8), 0.1, None)
    check(n == 2 and not tie, f"{n} paired, tie {tie}")
    # An offset given by hand that puts a row at the tolerance pairs it.
    moves = [At(0.0), At(1.0)]
    n, _ = sense_trace.pair_rows(moves, rows_at(0.0, 0.75), 0.125, 0.125)
    check(n == 2, f"{n} paired by hand")


def offsets_that_give_rows_to_other_moves_are_two_answers() -> None:
    class At:
        def __init__(self, abs_s: float) -> None:
            self.abs_s = abs_s
            self.horn_ms = None
            self.horn_row = None

    def rows_at(*cmds: float) -> list[tuple[float, float]]:
        return [(c + 0.1 + 0.1, 100.0) for c in cmds]
    # Commands 50 ms apart, rows whose commands lie at 0, 0.05 and 0.10 s:
    # rows 1 and 2 fit as exactly as rows 2 and 3, at offsets 50 ms apart,
    # which is inside the 150 ms a pair may lie apart.
    moves = [At(0.0), At(0.05)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.05, 0.10), 0.15,
                                   None)
    check(n == 2 and tie, f"{n} paired, tie {tie}")
    # The other direction: more commands than rows.
    moves = [At(0.0), At(0.05), At(0.10)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.05), 0.15, None)
    check(n == 2 and tie, f"{n} paired, tie {tie}")
    # One row, two commands far apart: either may be its command.
    moves = [At(0.0), At(5.0)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0), 0.15, None)
    check(n == 1 and tie, f"{n} paired, tie {tie}")
    # As many rows as commands, 50 ms apart: one answer, however many
    # offsets inside the tolerance give it.
    moves = [At(0.0), At(0.05), At(0.10)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.05, 0.10), 0.15,
                                   None)
    check(n == 3 and not tie, f"{n} paired, tie {tie}")
    check([mv.horn_row for mv in moves] == [0, 1, 2], "each its own")
    # Two rows that both lie at one command: either may be the command's,
    # with other commands paired and with none.
    moves = [At(0.0), At(1.0), At(2.5)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.04, 1.0, 2.5),
                                   0.15, None)
    check(n == 3 and tie, f"{n} paired, tie {tie}")
    moves = [At(0.0)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.05), 0.15, None)
    check(n == 1 and tie, f"{n} paired, tie {tie}")
    # One row and one command: one answer.
    moves = [At(0.0)]
    n, tie = sense_trace.pair_rows(moves, rows_at(7.0), 0.15, None)
    check(n == 1 and not tie and moves[0].horn_row == 0,
          f"{n} paired, tie {tie}")
    # No row, and no command.
    check(sense_trace.pair_rows([At(0.0)], [], 0.15, None) == (0, False),
          "no row")
    check(sense_trace.pair_rows([], rows_at(0.0), 0.15, None) == (0, False),
          "no command")
    # An offset given by hand is the answer.
    moves = [At(0.0), At(0.05)]
    n, tie = sense_trace.pair_rows(moves, rows_at(0.0, 0.05, 0.10), 0.15,
                                   -0.05)
    check(n == 2 and not tie, f"{n} paired, tie {tie}")
    check([mv.horn_row for mv in moves] == [1, 2], "rows 2 and 3")


def pairing_arguments_are_checked() -> None:
    log = str(ARGS.fixtures / "sense-trace-sim.log")
    csv = str(ARGS.fixtures / "sense-trace-sim.csv")
    for bad in ("0", "-1", "nan", "inf", "-inf"):
        r = tool(log, "--servo-csv", csv, "--no-csv", f"--pair-ms={bad}")
        check(r.returncode == 2 and "--pair-ms is a time above 0 ms"
              in r.stderr, f"--pair-ms {bad}: exit {r.returncode}")
    for bad in ("nan", "inf", "-inf"):
        r = tool(log, "--servo-csv", csv, "--no-csv", f"--csv-offset={bad}")
        check(r.returncode == 2 and "--csv-offset is a number of seconds"
              in r.stderr, f"--csv-offset {bad}: exit {r.returncode}")
    # An offset with nothing to pair.
    r = tool(log, "--no-csv", "--csv-offset", "12.345")
    check(r.returncode == 2 and "--csv-offset needs --servo-csv"
          in r.stderr, f"exit {r.returncode}")
    # A negative offset and a small tolerance are values.
    r = tool(log, "--servo-csv", csv, "--no-csv", "--csv-offset", "-3",
             "--pair-ms", "0.5")
    check(r.returncode == 0 and "0 paired with a move" in r.stdout,
          f"exit {r.returncode}")
    # Cells that are no travel time: not a number, not finite, below 0.
    rows = pathlib.Path(csv).read_text().splitlines()

    def with_travel(row: str, travel: str) -> str:
        cells = row.split(";")
        cells[14] = travel
        return ";".join(cells)
    odd = work("odd.csv", "\n".join(
        [rows[0], with_travel(rows[1], "nan"), with_travel(rows[2], "inf"),
         with_travel(rows[3], "-5"), rows[4]]) + "\n")
    r = tool(log, "--servo-csv", str(odd), "--no-csv", "--csv-offset",
             "12.345")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    check("1 travel time(s), 1 paired with a move" in r.stdout,
          r.stdout.splitlines()[1])


def a_capital_end_reason_is_a_problem() -> None:
    text = synthetic(1000, [(0, 1900), (1000, 1100)])
    for letter, words in (("T", "its time"), ("K", "the console"),
                          ("S", "a changed set-up")):
        late = text.replace(" e=t\n", f" e={letter}\n")
        check(late != text, "the end line is there")
        r = tool(str(work(f"late{letter}.log", late)), "--no-csv")
        check(r.returncode == 1, f"e={letter}: exit {r.returncode}")
        check(f"ended by {words}" in r.stdout, f"e={letter}: the reason")
        check("PROBLEM: the sampling core was not seen past a trigger line "
              "or the end in time" in r.stdout, f"e={letter}: said")
        check("no move of this trace is replayed" in r.stdout,
              f"e={letter}: not replayed")
    # A letter that is no reason is no end line.
    r = tool(str(work("lateX.log", text.replace(" e=t\n", " e=X\n"))),
             "--no-csv")
    check(r.returncode == 1 and "no end line" in r.stdout, "e=X")


def a_restart_of_the_coprocessor_is_a_clock_of_its_own() -> None:
    # Two boots in one log: the second numbers its traces from 1 again
    # and its clock starts low.
    a = synthetic(900000, [(0, 1900), (1000, 1100), (2500, 1900)])
    b = synthetic(3000, [(0, 1900), (700, 1100), (1900, 1900)])
    traces, _ = sense_trace.parse_log(a + b)
    check([tr.boot for tr in traces] == [0, 1], "two boots")
    check(traces[1].t0_abs == 3000, "the second boot's own clock")
    # Trace numbers that run on are one boot.
    traces, _ = sense_trace.parse_log(a + b.replace("n=1 ", "n=2 "))
    check([tr.boot for tr in traces] == [0, 0], "one boot")

    def numbered(text: str, n: int, ms: int) -> str:
        return (text.replace("n=1 ", f"n={n} ")
                .replace(" ms=0 ", f" ms={ms} "))

    def boots_of(*parts: tuple[int, int]) -> list[int]:
        traces, _ = sense_trace.parse_log("".join(
            numbered(b, n, ms) for n, ms in parts))
        return [tr.boot for tr in traces]
    # Numbers missing between two traces are traces no console took: one
    # boot, by 1, by many and across the number's wrap from 65535 to 0.
    check(boots_of((1, 5000), (2, 9000), (7, 20000), (300, 90000))
          == [0, 0, 0, 0], "gaps")
    check(boots_of((65534, 5000), (65535, 9000), (0, 20000), (1, 30000))
          == [0, 0, 0, 0], "65535 to 0")
    check(boots_of((65530, 5000), (4, 9000)) == [0, 0], "a gap at the wrap")
    check(boots_of((1, 5000), (32768, 9000)) == [0, 0], "32767 on")
    # A number that is not past the last one's is a restart: the same,
    # one less, and 32768 on, which is 32768 back.
    check(boots_of((3, 5000), (3, 9000)) == [0, 1], "the same number")
    check(boots_of((3, 5000), (2, 9000)) == [0, 1], "a number back")
    check(boots_of((1, 5000), (32769, 9000)) == [0, 1], "32768 on")
    # A millisecond tick that lies before the last one's is a restart,
    # whatever the number: by 1 ms, and by 2^31 ms, which is as far on.
    check(boots_of((1, 5000), (2, 4999)) == [0, 1], "1 ms back")
    check(boots_of((1, 5000), (2, 5000)) == [0, 0], "the same tick")
    check(boots_of((1, 5000), (2, 4999 + (1 << 31))) == [0, 0],
          "2^31 ms less 1 on")
    check(boots_of((1, 5000), (2, 5000 + (1 << 31))) == [0, 1],
          "2^31 ms back")
    # The tick across its wrap is one boot.
    check(boots_of((1, (1 << 32) - 3000), (2, 2000)) == [0, 0],
          "the tick's wrap")
    check(boots_of((1, 5000), (2, 9000), (1, 3000), (3, 8000), (2, 9000))
          == [0, 0, 1, 1, 2], "two restarts")
    # Two traces of one boot 6 days apart: the 0.1 ms count wrapped once
    # between them, and the millisecond tick says so.
    day = 86400000
    first = numbered(a, 1, 5000)
    later_t = (900000 + 6 * day * 10) & 0xFFFFFFFF
    later = numbered(a.replace("t=900000 ", f"t={later_t} "), 2,
                     5000 + 6 * day)
    traces, _ = sense_trace.parse_log(first + later)
    check([tr.boot for tr in traces] == [0, 0], "one boot")
    check(traces[1].t0_abs - traces[0].t0_abs == 6 * day * 10,
          f"{traces[1].t0_abs - traces[0].t0_abs} apart")
    # 2 days apart: no wrap, with the tick and without it.
    later_t = 900000 + 2 * day * 10
    for ms in (5000 + 2 * day, 5000):
        later = numbered(a.replace("t=900000 ", f"t={later_t} "), 2, ms)
        traces, _ = sense_trace.parse_log(first + later)
        check(traces[1].t0_abs - traces[0].t0_abs == 2 * day * 10,
              f"ms={ms}: {traces[1].t0_abs - traces[0].t0_abs} apart")
    head = ("time (s);test;step;phase;command (us);position (us);set (V);"
            "voltage (V);limit (A);current (A);power (W);mode;travel (ms);"
            "angle (deg);travel angle (ms)")
    rows = [head]
    # The CSV's clock: 50 s at the first boot's first command, 80 s at
    # the second's.
    for base, at in ((50.0, (0.0, 1.0, 2.5)), (80.0, (0.0, 0.7, 1.9))):
        for cmd in at:
            rows.append(f"{base + cmd + 0.090 + 0.1:.3f}" + ";" * 14 + "90")
    r = tool(str(work("boots.log", a + b)), "--no-csv", "--servo-csv",
             str(work("boots.csv", "\n".join(rows) + "\n")))
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[:400]}")
    check("6 travel time(s), 6 paired with a move" in r.stdout,
          r.stdout.splitlines()[1])
    check("the coprocessor restarted 1 time(s) in the log" in r.stdout,
          "said")
    total = setting_rows(r.stdout, "all traces, 6 move(s):")
    check(total[(1, "0.05")][:3] == ["6/6", "6/6", "+23.0"],
          f"{total[(1, '0.05')]}")

    def csv_of(*blocks: tuple[float, tuple[float, ...]]) -> str:
        out = [head]
        for base, at in blocks:
            for cmd in at:
                out.append(f"{base + cmd + 0.090 + 0.1:.3f}" + ";" * 14
                           + "90")
        return "\n".join(out) + "\n"

    def no_row_paired(r: subprocess.CompletedProcess, what: str) -> None:
        check(r.returncode == 1, f"{what}: exit {r.returncode}")
        check("more than one way" in r.stderr, f"{what}: {r.stderr}")
        check("arrival less the horn" not in r.stdout, f"{what}: compared")
    # The first boot's rows are not in the file, and its two moves lie
    # as two of the second boot's: each boot pairs with the same rows.
    # No row is paired.
    short = synthetic(900000, [(0, 1900), (700, 1100)])
    r = tool(str(work("boots2.log", short + b)), "--no-csv", "--servo-csv",
             str(work("boots2.csv", csv_of((80.0, (0.0, 0.7, 1.9))))))
    no_row_paired(r, "the same rows")
    check("3 travel time(s), 0 paired with a move" in r.stdout
          and "PROBLEM: the rows that pair with one boot's moves do not "
          "lie after those of the boot before it" in r.stdout,
          r.stdout[:500])
    # The rows of the second boot before those of the first.
    r = tool(str(work("boots.log", a + b)), "--no-csv", "--servo-csv",
             str(work("boots3.csv", csv_of((50.0, (0.0, 0.7, 1.9)),
                                           (80.0, (0.0, 1.0, 2.5))))))
    no_row_paired(r, "the other order")
    check("6 travel time(s), 0 paired with a move" in r.stdout,
          r.stdout[:500])
    # Two boots with the same moves: either block of rows fits each.
    r = tool(str(work("boots4.log", a + a.replace("t=900000", "t=3000"))),
             "--no-csv", "--servo-csv",
             str(work("boots4.csv", csv_of((50.0, (0.0, 1.0, 2.5)),
                                           (80.0, (0.0, 1.0, 2.5))))))
    no_row_paired(r, "alike")
    check("6 travel time(s), 0 paired with a move" in r.stdout,
          r.stdout[:500])
    # An offset by hand is the first boot's: the second is found.
    r = tool(str(work("boots.log", a + b)), "--no-csv", "--servo-csv",
             str(work("boots.csv", "\n".join(rows) + "\n")),
             "--csv-offset", "40")
    check(r.returncode == 0 and "6 paired with a move" in r.stdout,
          f"by hand: exit {r.returncode}")


def a_floor_is_a_current_above_zero() -> None:
    log = str(ARGS.fixtures / "sense-trace-sim.log")
    for bad in ("0", "-0.01", "nan"):
        r = tool(log, "--floor", bad, "--no-csv")
        check(r.returncode == 2, f"--floor {bad}: exit {r.returncode}")
    # A floor above the travel current: no move is seen.
    r = tool(log, "--floor", "1.5", "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}")
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(4, "0.05")][:2] == ["0/4", "0/4"],
          f"{arrival[(4, '0.05')]}")
    check("threshold 1.500 A" in r.stdout, "the threshold is the floor")


def a_replay_that_cannot_run_is_exit_2() -> None:
    r = subprocess.run(
        [sys.executable, str(TOOL), "--replay",
         str(ARGS.work / "no-such-program"), "--no-csv",
         str(ARGS.fixtures / "sense-trace-sim.log")],
        capture_output=True, text=True)
    check(r.returncode == 2 and "the replay failed" in r.stderr,
          f"exit {r.returncode}: {r.stderr}")
    # The replay itself refuses what it cannot read.
    r = subprocess.run([str(ARGS.replay)], input="s 1 2 0\n",
                       capture_output=True, text=True)
    check(r.returncode == 2 and "not understood" in r.stderr, "a sample "
          "before a move")
    r = subprocess.run([str(ARGS.replay)],
                       input="m 100 30050 0 0 20000 50000 10 4\ns 90 0 0\n",
                       capture_output=True, text=True)
    check(r.returncode == 2 and "no end" in r.stderr, "a move left open")
    r = subprocess.run([str(ARGS.replay)],
                       input="m 100 30050 0 0 20000 50000 10 1\n"
                             "s 90 0 0\ns 110 500000 0\ns 120 500000 0\n"
                             "s 130 10000 0\ne\n",
                       capture_output=True, text=True)
    check(r.returncode == 0
          and r.stdout == "arrived 10 30 500000 500000 2 0\n", r.stdout)


CASES = [
    the_fixtures_are_what_the_generator_writes,
    the_report_on_the_fixture,
    cr_lf_and_lf_read_the_same,
    a_log_cut_off_inside_a_trace,
    a_missing_line_does_not_match_the_end_line,
    a_trace_that_starts_inside_another,
    dropped_records_are_counted_against_the_end_line,
    a_log_with_no_trace,
    another_version_and_a_trace_without_a_shunt,
    times_run_across_the_wrap_of_the_count,
    clipped_samples_are_said_and_time_no_arrival,
    edge_lines_are_the_moves_when_there_are_any,
    an_edge_takes_the_command_line_nearest_to_it,
    a_command_with_no_edge_line_is_a_move,
    edges_and_commands_are_paired_as_many_as_can_be,
    commands_within_30_ms_are_not_replayed,
    the_traces_alone_say_which_settings_time_every_move,
    a_trace_with_trigger_lines_not_written_is_not_replayed,
    a_move_with_records_missing_inside_it_is_not_replayed,
    records_missing_at_the_ends_of_a_moves_samples,
    a_move_the_part_went_offline_in_is_not_replayed,
    settings_are_compared_on_the_moves_both_time,
    a_slewed_command_ends_at_its_d_line,
    a_move_with_no_end_reads_cut_and_one_unseen_unseen,
    the_servo_csv_pairs_by_time,
    the_offset_that_pairs_the_most_is_found_between_the_obvious,
    an_offset_that_puts_rows_at_the_tolerance_itself_is_tried,
    offsets_that_give_rows_to_other_moves_are_two_answers,
    pairing_arguments_are_checked,
    a_capital_end_reason_is_a_problem,
    a_restart_of_the_coprocessor_is_a_clock_of_its_own,
    a_floor_is_a_current_above_zero,
    a_replay_that_cannot_run_is_exit_2,
]


def main() -> int:
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", type=pathlib.Path, required=True)
    ap.add_argument("--gen", type=pathlib.Path, required=True)
    ap.add_argument("--fixtures", type=pathlib.Path, required=True)
    ap.add_argument("--work", type=pathlib.Path, required=True)
    ARGS = ap.parse_args()
    ARGS.work.mkdir(parents=True, exist_ok=True)
    for case in CASES:
        try:
            case()
        except AssertionError as err:
            FAILED.append(case.__name__)
            print(f"FAIL  {case.__name__}\n      {err}")
        else:
            print(f"ok    {case.__name__}")
    print(f"\nsense_trace_tool: {len(CASES)} case(s), "
          f"{len(FAILED)} failure(s)")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
