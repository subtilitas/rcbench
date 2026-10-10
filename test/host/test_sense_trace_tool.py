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
alike; the grammar of every line type, with each field missing, doubled,
out of its range and out of its order; lines that are no line of a trace
inside a trace and a trace's lines outside one; line ends; a second $H
line and one after a record; a trace length and a sample period version 2
does not write; samples out of the order of time and two voltages for
one; a $S line that changes nothing; clipped samples, missing records and
a state change in each window a move's levels come from, and a trace with
no sample before its first command; a replay that answers fewer moves
than sent; files that cannot be read or written; the range of every
number on the command line; and every line of a log with every line type
cut at every byte, which is damage or changes nothing, but for a cut
inside a line's last number.

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


def is_sample(line: str) -> bool:
    got = sense_trace.read_line(line)
    return got is not None and got[0] == "s"


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
                  if is_sample(line.decode("ascii")))
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
    # A sample line and a voltage line with no trace around them are a
    # trace's lines whose start line is lost.
    check("0 trace(s), 1 other line(s)" in r.stdout, r.stdout)
    check("PROBLEM: 2 line(s) of a trace outside a trace, the first is "
          "line 2: '10,300'" in r.stdout, r.stdout)
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
    traces, other, _ = sense_trace.parse_log(
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
    traces, _, _ = sense_trace.parse_log(two)
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
    n = sum(1 for line in body if is_sample(line))
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
    n = sum(1 for line in body if is_sample(line))
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
    # 100 ms before the second command: the first move's samples, and
    # the 200 ms the second takes its threshold from.
    r = tool(str(work("gap-hold.log",
                      "\n".join(gap(lines, cmd2 - 100)) + "\n")), "--no-csv")
    check("move 2 at 1000.0 ms, channel 2 to 1100 us: records are missing "
          "in the 200 ms before it, not replayed" in r.stdout,
          r.stdout[-900:])
    arrival = setting_rows(r.stdout, "  arrival, ms from the command:")
    check(arrival[(1, "0.05")] == ["0/2", "0/2", "-", "-"],
          f"in the 200 ms: {arrival[(1, '0.05')]}")
    # 250 ms before the second command: the first move alone.
    r = tool(str(work("gap-one.log",
                      "\n".join(gap(lines, cmd2 - 250)) + "\n")), "--no-csv")
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
        got = sense_trace.read_line(line)
        if got is not None and got[1][0] == "C":
            # The last change before the frame the $C line is timed at.
            lines.append(f"$D t={got[1][1] - 150} ch=2 "
                         f"us={ends[got[1][3]]}")
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
    for bad in ("0", "-1", "nan", "inf", "-inf", "60000.001", "1e308"):
        r = tool(log, "--servo-csv", csv, "--no-csv", f"--pair-ms={bad}")
        check(r.returncode == 2 and "--pair-ms is a time above 0 ms"
              in r.stderr, f"--pair-ms {bad}: exit {r.returncode}")
    for bad in ("nan", "inf", "-inf", "1e300", "-1.1e9"):
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
    traces, _, _ = sense_trace.parse_log(a + b)
    check([tr.boot for tr in traces] == [0, 1], "two boots")
    check(traces[1].t0_abs == 3000, "the second boot's own clock")
    # Trace numbers that run on are one boot.
    traces, _, _ = sense_trace.parse_log(a + b.replace("n=1 ", "n=2 "))
    check([tr.boot for tr in traces] == [0, 0], "one boot")

    def numbered(text: str, n: int, ms: int) -> str:
        return (text.replace("n=1 ", f"n={n} ")
                .replace(" ms=0 ", f" ms={ms} "))

    def boots_of(*parts: tuple[int, int]) -> list[int]:
        traces, _, _ = sense_trace.parse_log("".join(
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
    traces, _, _ = sense_trace.parse_log(first + later)
    check([tr.boot for tr in traces] == [0, 0], "one boot")
    check(traces[1].t0_abs - traces[0].t0_abs == 6 * day * 10,
          f"{traces[1].t0_abs - traces[0].t0_abs} apart")
    # 2 days apart: no wrap, with the tick and without it.
    later_t = 900000 + 2 * day * 10
    for ms in (5000 + 2 * day, 5000):
        later = numbered(a.replace("t=900000 ", f"t={later_t} "), 2, ms)
        traces, _, _ = sense_trace.parse_log(first + later)
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
    # The replay takes the threshold as whole uA: below 1 uA it is 0,
    # and past 1000 A it is no current of a servo.
    for bad in ("0", "-0.01", "nan", "inf", "-inf", "1e30", "1000.1",
                "0.0000009"):
        r = tool(log, f"--floor={bad}", "--no-csv")
        check(r.returncode == 2 and "--floor is a current from 0.000001 A "
              "to 1000 A" in r.stderr,
              f"--floor {bad}: exit {r.returncode}: {r.stderr}")
    for good in ("0.000001", "1000"):
        r = tool(log, "--floor", good, "--no-csv")
        check(r.returncode == 0, f"--floor {good}: exit {r.returncode}")
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


# --- the format's grammar ----------------------------------------------------

# The longest line of each type, and each number at an end of its range.
WHOLE = {
    "$T v=2 n=65535 trig=edge t=4294967295 ms=4294967295 len=99999": "T",
    "$T v=0 n=0 trig=key t=0 ms=0 len=0": "T",
    "$H dt_us=9999 shunt_uohm=4294967295 cfg=0xFFFF on=1 rst=255": "H",
    "$H dt_us=0 shunt_uohm=0 cfg=0x0000 on=0 rst=0": "H",
    "$C t=0 ch=65535 us=65535": "C",
    "$D t=4294967295 ch=0 us=0": "C",
    "$E t=0": "E",
    "$K t=4294967295": "E",
    "$S on=0 rst=255": "S",
    "$S on=1 rst=0": "S",
    "$L n=1": "L",
    "$L n=16777215": "L",
    "$Z n=65535 s=99999999 v=9999999 l=99999999 m=9999 ml=9999 e=K": "Z",
    "$Z n=0 s=0 v=0 l=0 m=0 ml=0 e=t": "Z",
    "-2147483648,-4096": "s",
    "2147483647,4095": "s",
    "0,0": "s",
    "v-2147483648": "v",
    "v2147483647": "v",
    "v0": "v",
}

# No line of a trace: a field missing, one too many, fields in another
# order, a number out of its range or written another way, a character
# before, between or after the fields.
NOT_WHOLE = [
    # $T
    "$T v=2 n=1 trig=cmd t=1 ms=1",
    "$T v=2 n=1 trig=cmd t=1 ms=1 len=",
    "$T v=2 n=1 trig=cmd t=1 len=4000",
    "$T v=2 n=1 trig=cmd t=1 ms=1 len=4000 x=1",
    "$T v=2 n=1 trig=cmd t=1 ms=1 len=4000 ",
    " $T v=2 n=1 trig=cmd t=1 ms=1 len=4000",
    "$T  v=2 n=1 trig=cmd t=1 ms=1 len=4000",
    "$T n=1 v=2 trig=cmd t=1 ms=1 len=4000",
    "$T v=10 n=1 trig=cmd t=1 ms=1 len=4000",
    "$T v=2 n=65536 trig=cmd t=1 ms=1 len=4000",
    "$T v=2 n=01 trig=cmd t=1 ms=1 len=4000",
    "$T v=2 n=1 trig=tap t=1 ms=1 len=4000",
    "$T v=2 n=1 trig=CMD t=1 ms=1 len=4000",
    "$T v=2 n=1 trig=cmd t=4294967296 ms=1 len=4000",
    "$T v=2 n=1 trig=cmd t=1 ms=4294967296 len=4000",
    "$T v=2 n=1 trig=cmd t=1 ms=-1 len=4000",
    "$T v=2 n=1 trig=cmd t=1 ms=1 len=100000",
    # $H
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 rst=0",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=0 rst=0",
    "$H dt_us=10000 shunt_uohm=100000 cfg=0x4007 on=1 rst=0",
    "$H dt_us=1000 shunt_uohm=4294967296 cfg=0x4007 on=1 rst=0",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4a07 on=1 rst=0",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x407 on=1 rst=0",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x04007 on=1 rst=0",
    "$H dt_us=1000 shunt_uohm=100000 cfg=4007 on=1 rst=0",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=2 rst=0",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=256",
    "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=00",
    # $C and $D carry a channel and a pulse width
    "$C t=1",
    "$C t=1 ch=2",
    "$C t=1 ch=2 us=",
    "$C t=1 us=1500",
    "$C t=1 us=1500 ch=2",
    "$C t=1 ch=2 us=1500 us=1500",
    "$C t=1 ch=65536 us=1500",
    "$C t=1 ch=2 us=65536",
    "$C t=4294967296 ch=2 us=1500",
    "$C t=1 ch=2 us=01500",
    "$C t=1 ch=2 us=-1500",
    "$C t=1 ch=2 us=1500 ",
    "$C t=1  ch=2 us=1500",
    "$D t=1",
    "$D t=1 ch=2",
    "$D t=1 ch=2 us=",
    "$D t=1 ch=2 us=65536",
    # $E and $K carry a time and nothing else
    "$E",
    "$E t=",
    "$E t=-1",
    "$E t=01",
    "$E t=4294967296",
    "$E t=1 ch=2",
    "$E t=1 ch=2 us=1500",
    "$K",
    "$K t=",
    "$K t=1 ch=2 us=1500",
    "$K t=1 ",
    # $S
    "$S",
    "$S on=1",
    "$S on=1 rst=",
    "$S rst=0 on=1",
    "$S on=2 rst=0",
    "$S on=1 rst=256",
    "$S on=1 rst=0 x",
    # $L
    "$L",
    "$L n=",
    "$L n=0",
    "$L n=16777216",
    "$L n=03",
    "$L n=3 n=3",
    # $Z
    "$Z n=1 s=1 v=0 l=0 m=0 ml=0",
    "$Z n=1 s=1 v=0 l=0 m=0 ml=0 e=",
    "$Z n=1 s=1 v=0 l=0 m=0 e=t",
    "$Z n=1 s=1 v=0 l=0 m=0 ml=0 e=t e=t",
    "$Z n=1 s=1 v=0 l=0 m=0 ml=0 e=?",
    "$Z n=1 s=1 v=0 l=0 m=0 ml=0 e=X",
    "$Z n=1 s=1 v=0 l=0 m=0 ml=0 e=tt",
    "$Z n=65536 s=1 v=0 l=0 m=0 ml=0 e=t",
    "$Z n=1 s=100000000 v=0 l=0 m=0 ml=0 e=t",
    "$Z n=1 s=1 v=10000000 l=0 m=0 ml=0 e=t",
    "$Z n=1 s=1 v=0 l=100000000 m=0 ml=0 e=t",
    "$Z n=1 s=1 v=0 l=0 m=10000 ml=0 e=t",
    "$Z n=1 s=1 v=0 l=0 m=0 ml=10000 e=t",
    "$Z n=1 s=01 v=0 l=0 m=0 ml=0 e=t",
    # sample lines
    "10",
    "10,",
    ",300",
    "10,-",
    "-,300",
    "10,300,1",
    "10, 300",
    "10,300 ",
    " 10,300",
    "010,300",
    "10,0300",
    "-0,300",
    "10,-0",
    "+10,300",
    "10.0,300",
    "1e1,300",
    "10,4096",
    "10,-4097",
    "2147483648,0",
    "-2147483649,0",
    # voltage lines
    "v",
    "v-",
    "v06000",
    "v-0",
    "v+6000",
    "v2147483648",
    "v-2147483649",
    "V6000",
    "v6000 ",
    "v 6000",
    "v6000,1",
    # no type of line
    "",
    " ",
    "$",
    "$X t=1",
    "$t v=2 n=1 trig=cmd t=1 ms=1 len=4000",
    "hello",
    "10;300",
]


def judged(text: str) -> tuple[bool, list]:
    """What the tool makes of a log, short of the replay: whether it
    reports damage, and every value a report is made from.  The replay's
    answer follows from the moves' levels and samples."""
    traces, other, stray = sense_trace.parse_log(text)
    damaged = bool(stray) or not traces
    result: list = [other]
    for tr in traces:
        sense_trace.check(tr)
        damaged = damaged or bool(tr.problems)
        moves = sense_trace.find_moves(tr, 0.020)
        result.append((
            tr.number, tr.trig, tr.t0_abs, tr.boot, tr.ms, tr.length,
            tr.version, tr.period_us, tr.shunt_uohm, tr.cfg, tr.online,
            tr.resets, tuple(tr.t), tuple(tr.code),
            tuple(sorted(tr.mv.items())), tr.n_volts, tuple(tr.marks),
            tr.lost, tuple(tr.gaps), tuple(tr.states),
            tuple(sorted(tr.end.items())) if tr.end else None,
            tuple((mv.t, mv.ch, mv.us, mv.why_not, mv.rise_a, mv.ref_a,
                   mv.ref_from, mv.move_a, mv.first, mv.last, mv.clipped)
                  for mv in moves)))
    return damaged, result


def with_line(text: str, at: int, *lines: str) -> str:
    """@p text with @p lines put in before its line @p at, from 0."""
    body = text.splitlines()
    return "\n".join(body[:at] + list(lines) + body[at:]) + "\n"


def counted(lines: list[str], lost: int = 0) -> list[str]:
    """@p lines with the end line counting what stands before it."""
    kinds = [sense_trace.read_line(line) for line in lines[:-1]]
    n = {k: sum(1 for got in kinds if got is not None and got[0] in k)
         for k in ("s", "v", "CE")}
    return lines[:-1] + [f"$Z n=1 s={n['s']} v={n['v']} l={lost} "
                         f"m={n['CE']} ml=0 e=t"]


def every_line_type_has_one_grammar() -> None:
    for line, kind in WHOLE.items():
        got = sense_trace.read_line(line)
        check(got is not None and got[0] == kind, f"{line!r}: {got}")
    base = synthetic(1000, [(0, 1900), (1000, 1100)])
    check(not judged(base)[0], "the trace itself is whole")
    for line in NOT_WHOLE:
        check(sense_trace.read_line(line) is None, f"{line!r} is read")
        # Inside a trace it is damage, whatever the counts say.
        traces, _, _ = sense_trace.parse_log(with_line(base, 3, line))
        sense_trace.check(traces[0])
        check(any("1 line(s) that are no whole line of a trace, the first "
                  f"is line 4: {sense_trace.shown(line)}" in p
                  for p in traces[0].problems),
              f"{line!r} inside a trace: {traces[0].problems}")
        check(sense_trace.find_moves(traces[0], 0.020) == [],
              f"{line!r}: replayed")


def the_longest_line_of_each_type_is_62_characters() -> None:
    # The console's line is 64 bytes with CR LF.  The ranges of the
    # numbers keep each type within it: no line the grammar reads is
    # longer.
    longest = {}
    for line, kind in WHOLE.items():
        longest[kind] = max(longest.get(kind, 0), len(line))
    check(longest == {"T": 61, "H": 59, "C": 25, "E": 15, "S": 15, "L": 13,
                      "Z": 61, "s": 17, "v": 12}, f"{longest}")
    # One more digit anywhere is out of a range.
    for line in WHOLE:
        for k, c in enumerate(line):
            if c.isdigit() and line[k - 1] in "=,v-" and c != "0":
                more = line[:k] + c + line[k:]
                check(sense_trace.read_line(more) is None
                      or len(more) <= 62, f"{more!r} is read")


def a_line_that_is_no_line_of_a_trace_is_damage() -> None:
    lines = fixture_log().splitlines()
    move = next(k for k, line in enumerate(lines) if line.startswith("$C"))
    text = "\n".join(lines[:move] + ["hello", ""] + lines[move:]) + "\n"
    r = tool(str(work("foreign.log", text)), "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("PROBLEM: 2 line(s) that are no whole line of a trace, the first "
          f"is line {move + 1}: 'hello'" in r.stdout, r.stdout[-900:])
    check("no move of this trace is replayed" in r.stdout
          and "arrival, ms from the command" not in r.stdout, "not replayed")
    check("1 trace(s) with a problem" in r.stderr, r.stderr)
    # A long one is shown by its first 40 characters.
    r = tool(str(work("long.log", with_line(fixture_log(), move, "x" * 90))),
             "--no-csv")
    check(f"line {move + 1}: '{'x' * 40}...'" in r.stdout, r.stdout[-900:])
    # The coprocessor's own console line inside a trace is none of the
    # trace's and no damage.
    r = tool(str(work("console.log", with_line(
        synthetic(1000, [(0, 1900)]), 30, "rcbench-iomcu: CAN up"))),
        "--no-csv")
    check(r.returncode == 0 and "1 trace(s), 1 other line(s)" in r.stdout,
          f"exit {r.returncode}: {r.stdout[:200]}")


def a_command_line_without_its_channel_and_pulse_is_damage() -> None:
    text = synthetic(1000, [(0, 1900), (1000, 1100)], edges=True)
    whole = "$C t=1000 ch=2 us=1900"
    check(whole in text, "the command line is there")
    edge = "$E t=1120"
    check(edge in text, "the edge line is there")
    for was, cut in ((whole, "$C t=1000"), (whole, "$C t=1000 ch=2"),
                     (whole, "$C t=1000 ch=2 us="),
                     (whole, "$D t=1000"), (whole, "$D t=1000 ch=2"),
                     (edge, "$E t=1120 ch=2 us=1900"),
                     (edge, "$K t=1120 ch=2 us=1900"),
                     (edge, "$E t=1120 ch=2")):
        at = text.splitlines().index(was) + 1
        r = tool(str(work("mark.log", text.replace(was, cut))), "--no-csv")
        check(r.returncode == 1, f"{cut!r}: exit {r.returncode}")
        check("PROBLEM: 1 line(s) that are no whole line of a trace, the "
              f"first is line {at}: {cut!r}" in r.stdout,
              f"{cut!r}: {r.stdout[:700]}")
        # The line is no trigger line: the end line counts one more.
        check("PROBLEM: 3 trigger line(s) read, the end line counts 4"
              in r.stdout, f"{cut!r}: counted")
        check("no move of this trace is replayed" in r.stdout
              and "arrival, ms from the command" not in r.stdout,
              f"{cut!r}: replayed")


def a_line_end_is_lf_or_cr_lf_and_nothing_is_taken_off_a_line() -> None:
    text = synthetic(1000, [(0, 1900)])

    def run(name: str, raw: str) -> subprocess.CompletedProcess:
        return tool(str(work(name, raw)), "--no-csv")
    whole = run("eol-lf.log", text)
    check(whole.returncode == 0, f"exit {whole.returncode}")
    # The last line without its end is the same line.
    r = run("eol-open.log", text[:-1])
    check(r.returncode == 0
          and r.stdout.splitlines()[1:] == whole.stdout.splitlines()[1:],
          f"no last line end: exit {r.returncode}")
    # CR CR LF, as a terminal that translates twice writes it.
    r = run("eol-crcrlf.log", text.replace("\n", "\r\r\n"))
    check(r.returncode == 1 and "outside a trace" in r.stdout,
          f"CR CR LF: exit {r.returncode}")
    # CR alone ends no line.
    r = run("eol-cr.log", text.replace("\n", "\r"))
    check(r.returncode == 1 and "no trace in the log" in r.stderr,
          f"CR: exit {r.returncode}")
    one = text.replace("10,300\n", "10,300\r\r\n", 1)
    r = run("eol-one.log", one)
    check(r.returncode == 1 and "1 line(s) that are no whole line of a "
          "trace, the first is line 4: '10,300\\r'" in r.stdout,
          f"one CR CR LF: {r.stdout[:500]}")
    for name, was, bad in (("lead", "10,300\n", " 10,300\n"),
                           ("trail", "10,300\n", "10,300 \n"),
                           ("tab", "10,300\n", "10,300\t\n"),
                           ("end", " e=t\n", " e=t \n"),
                           ("head", "$H dt_us", " $H dt_us")):
        check(was in text, f"{name}: the line is there")
        r = run(f"eol-{name}.log", text.replace(was, bad, 1))
        check(r.returncode == 1 and "no whole line of a trace" in r.stdout,
              f"{name}: exit {r.returncode}")


def a_trace_has_one_set_up_line_before_its_records() -> None:
    text = synthetic(1000, [(0, 1900)])
    lines = text.splitlines()
    r = tool(str(work("h2.log", with_line(text, 40, lines[1]))), "--no-csv")
    check(r.returncode == 1 and "PROBLEM: 2 $H lines" in r.stdout,
          f"two: exit {r.returncode}")
    # The set-up line after the first sample.
    late = "\n".join([lines[0], lines[2], lines[1]] + lines[3:]) + "\n"
    r = tool(str(work("hlate.log", late)), "--no-csv")
    check(r.returncode == 1
          and "PROBLEM: 1 line(s) before the $H line" in r.stdout,
          f"late: exit {r.returncode}")
    check("no move of this trace is replayed" in r.stdout, "not replayed")


def a_trace_length_and_a_sample_period_are_version_2s() -> None:
    text = synthetic(1000, [(0, 1900)])
    for was, bad, said in (
            (" len=4000\n", " len=400\n",
             "PROBLEM: a cmd trace of 400 ms; version 2 writes 4000 ms"),
            (" len=4000\n", " len=10000\n",
             "PROBLEM: a cmd trace of 10000 ms; version 2 writes 4000 ms"),
            (" trig=cmd ", " trig=key ",
             "PROBLEM: a key trace of 4000 ms; version 2 writes 10000 ms"),
            (" trig=cmd ", " trig=edge ", None),
            ("dt_us=1000 ", "dt_us=100 ",
             "PROBLEM: a sample period of 100 us; version 2 writes 1000 us"),
            ("dt_us=1000 ", "dt_us=2000 ",
             "PROBLEM: a sample period of 2000 us; version 2 writes 1000 "
             "us")):
        check(was in text, f"{was!r} is there")
        r = tool(str(work("len.log", text.replace(was, bad))), "--no-csv")
        if said is None:
            check(r.returncode == 0, f"{bad!r}: exit {r.returncode}")
            continue
        check(r.returncode == 1 and said in r.stdout,
              f"{bad!r}: exit {r.returncode}: {r.stdout[:400]}")
        check("no move of this trace is replayed" in r.stdout,
              f"{bad!r}: replayed")


def samples_lie_in_the_order_of_time_with_one_voltage_each() -> None:
    lines = synthetic(1000, [(0, 1900)]).splitlines()
    # The first sample's time is from the trigger and lies before it.
    check(lines[2] == "-600,300", lines[2])
    back = lines[:30] + ["-10,300"] + lines[31:]
    r = tool(str(work("back.log", "\n".join(back) + "\n")), "--no-csv")
    check(r.returncode == 1 and "PROBLEM: 1 sample line(s) timed before the "
          "sample before them" in r.stdout, f"exit {r.returncode}")
    same = lines[:30] + ["0,300"] + lines[31:]
    r = tool(str(work("same.log", "\n".join(same) + "\n")), "--no-csv")
    check(r.returncode == 0, f"the same time: exit {r.returncode}")
    two = counted(lines[:30] + ["v6000", "v6001"] + lines[30:])
    r = tool(str(work("v2.log", "\n".join(two) + "\n")), "--no-csv")
    check(r.returncode == 1 and "PROBLEM: 1 voltage line(s) behind a sample "
          "that has one" in r.stdout, f"exit {r.returncode}")
    # A voltage before the first sample line is its sample's, which the
    # trace starts after.
    first = counted(lines[:2] + ["v6000"] + lines[2:30] + ["v6001"]
                    + lines[30:])
    r = tool(str(work("v0.log", "\n".join(first) + "\n")), "--no-csv")
    check(r.returncode == 0 and "2 voltages" in r.stdout,
          f"exit {r.returncode}: {r.stdout[:300]}")


def a_state_line_that_changes_nothing_is_damage() -> None:
    text = synthetic(1000, [(0, 1900)])
    # The set-up line says online, reset count 0.
    r = tool(str(work("s-same.log", with_line(text, 400, "$S on=1 rst=0"))),
             "--no-csv")
    check(r.returncode == 1 and "PROBLEM: 1 $S line(s) that change nothing: "
          "a line before them is missing" in r.stdout,
          f"exit {r.returncode}")
    # Off and on again, with the $S line between the two lost.
    r = tool(str(work("s-lost.log", with_line(
        text, 400, "$S on=0 rst=0", "$S on=0 rst=0"))), "--no-csv")
    check(r.returncode == 1 and "$S line(s) that change nothing" in r.stdout,
          f"exit {r.returncode}")
    r = tool(str(work("s-two.log", with_line(
        text, 400, "$S on=0 rst=0", "$S on=1 rst=1"))), "--no-csv")
    check(r.returncode == 0, f"two changes: exit {r.returncode}")


def lines_of_a_trace_outside_a_trace_are_damage() -> None:
    lines = fixture_log().splitlines()
    start = next(k for k, line in enumerate(lines) if line.startswith("$T"))
    end = next(k for k, line in enumerate(lines) if line.startswith("$Z"))
    # The first trace's start line cut short: its lines stand outside a
    # trace, and the second trace is read.
    cut = lines[:start] + ["$T v=2 n=1 trig=key"] + lines[start + 1:]
    r = tool(str(work("stray.log", "\n".join(cut) + "\n")), "--no-csv")
    check(r.returncode == 1, f"exit {r.returncode}")
    check("1 trace(s), 2 other line(s)" in r.stdout, r.stdout[:200])
    check(f"PROBLEM: {end - start + 1} line(s) of a trace outside a trace, "
          f"the first is line {start + 1}: '$T v=2 n=1 trig=key'; a trace's "
          "start line is missing or damaged" in r.stdout, r.stdout[:400])
    check(f"{end - start + 1} line(s) of a trace outside a trace"
          in r.stderr, r.stderr)
    check("trace 2:" in r.stdout and "counts match the end line" in r.stdout,
          "the second trace")
    # The start line lost whole.
    r = tool(str(work("stray2.log",
                      "\n".join(lines[:start] + lines[start + 1:]) + "\n")),
             "--no-csv")
    check(r.returncode == 1
          and f"PROBLEM: {end - start} line(s) of a trace outside a trace"
          in r.stdout, f"exit {r.returncode}")
    # A line that starts as a trace's does after the last trace too.
    r = tool(str(work("stray3.log", fixture_log() + "$Z n=3\n")), "--no-csv")
    check(r.returncode == 1
          and "PROBLEM: 1 line(s) of a trace outside a trace" in r.stdout,
          f"exit {r.returncode}")


# --- the windows a move's levels are taken from ------------------------------

def moves_of(text: str) -> list:
    traces, _, stray = sense_trace.parse_log(text)
    check(len(traces) == 1 and not stray, "one trace")
    sense_trace.check(traces[0])
    check(not traces[0].problems, str(traces[0].problems))
    return sense_trace.find_moves(traces[0], 0.020)


def clipped_samples_in_a_level_are_the_last_value_before_the_end() -> None:
    lines = synthetic(1000, [(0, 1900), (1000, 1100)]).splitlines()
    cmd1 = next(k for k, line in enumerate(lines) if line.startswith("$C"))
    cmd2 = next(k for k, line in enumerate(lines)
                if line.startswith("$C") and "us=1100" in line)
    # 0.1 Ohm: a code is 0.4 mA.  The end codes are 4095 and -4096; the
    # capture judges a sample at either as 4094 steps towards that end.
    for code, last in ((4095, 4094), (-4096, -4094)):
        # The 50 ms before the first command clipped: 50 of the 60 samples
        # before it, which are the idle level's and the 200 ms's too.
        body = (lines[:cmd1 - 50] + [f"10,{code}"] * 50 + lines[cmd1:])
        moves = moves_of("\n".join(body) + "\n")
        a = last * 0.0004
        idle = (10 * 0.12 + 50 * a) / 60
        dev = max(abs(a - idle), abs(0.12 - idle))
        mv = moves[0]
        check(mv.why_not == "", mv.why_not)
        check(abs(mv.rise_a - a) < 1e-9, f"{code}: before {mv.rise_a}")
        check(abs(mv.ref_a - idle) < 1e-9, f"{code}: holding {mv.ref_a}")
        check(abs(mv.move_a - dev) < 1e-9, f"{code}: threshold {mv.move_a}")
        check(mv.clipped == 50, f"{code}: {mv.clipped} clipped")
        # The second move: its threshold has the idle level's distance.
        check(abs(moves[1].move_a - dev) < 1e-9 and moves[1].clipped == 50,
              f"{code}: move 2 {moves[1].move_a} {moves[1].clipped}")
        # The 200 ms before the second command, 5 samples of them: the
        # level the first move is held at is not the second's destination,
        # so the second's levels alone have them.
        body = (lines[:cmd2 - 100] + [f"10,{code}"] * 5
                + lines[cmd2 - 95:])
        moves = moves_of("\n".join(body) + "\n")
        hold = (195 * 0.12 + 5 * a) / 200
        check(moves[0].clipped == 0, f"{code}: move 1 {moves[0].clipped}")
        check(moves[1].clipped == 5
              and abs(moves[1].move_a - abs(a - hold)) < 1e-9
              and abs(moves[1].rise_a - 0.12) < 1e-9,
              f"{code}: move 2 {moves[1].move_a}")
    # The levels and the replay take one sample one way: the level before
    # is the samples the replay is fed first.
    body = lines[:cmd1 - 50] + ["10,4095"] * 50 + lines[cmd1:]
    r = tool(str(work("clip-level.log", "\n".join(body) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stderr}")
    check("move 1 at 0.0 ms, channel 2 to 1900 us: before 1.638 A, holding "
          "1.385 A (idle level), threshold 1.265 A; 50 clipped sample(s) in "
          "these levels, each taken as the last value before the end of the "
          "range" in r.stdout, r.stdout[:1200])
    r = tool(str(work("clip-none.log", "\n".join(lines) + "\n")), "--no-csv")
    check("clipped sample(s) in these levels" not in r.stdout, "none said")


def three_moves() -> tuple[list[str], list[int]]:
    """A trace of three moves, out, back and out, and the lines of its
    command lines."""
    lines = synthetic(1000, [(0, 1900), (1000, 1100),
                             (2000, 1900)]).splitlines()
    return lines, [k for k, line in enumerate(lines)
                   if line.startswith("$C")]


def whys(text: str) -> list[str]:
    return [mv.why_not for mv in moves_of(text)]


def records_missing_in_a_level_window_stop_each_move_that_needs_it() -> None:
    lines, cmd = three_moves()
    check(whys("\n".join(lines) + "\n") == ["", "", ""], "whole")

    def lost(at: int) -> list[str]:
        """Why each move is not replayed with the 3 samples before line
        @p at dropped."""
        code = lines[at].split(",")[1]
        body = counted(lines[:at - 3] + ["$L n=3", f"40,{code}"]
                       + lines[at + 1:], lost=3)
        return whys("\n".join(body) + "\n")
    # Among the samples before the first command, before the 50 ms: the
    # idle level, which every move's threshold has.
    idle = "records are missing before the first command"
    check(lost(cmd[0] - 54) == [idle, idle, idle], f"{lost(cmd[0] - 54)}")
    # Before the first sample of all: the start of those samples.
    body = counted(lines[:2] + ["$L n=3"] + lines[2:], lost=3)
    check(whys("\n".join(body) + "\n") == [idle, idle, idle],
          "before the trace")
    # 100 ms before the second command: outside its 50 ms and its samples,
    # inside its 200 ms; the third move returns to the pulse width the
    # second left and takes its holding level from the same window.
    check(lost(cmd[1] - 100) == [
        "records are missing among its samples",
        "records are missing in the 200 ms before it",
        "records are missing in the 200 ms before move 2, its holding "
        "level"], f"{lost(cmd[1] - 100)}")
    # At the start of the second move's 200 ms.
    check(lost(cmd[1] - 200)[1:] == [
        "records are missing in the 200 ms before it",
        "records are missing in the 200 ms before move 2, its holding "
        "level"], f"{lost(cmd[1] - 200)}")
    # Before the 200 ms: the first move alone.
    check(lost(cmd[1] - 205) == ["records are missing among its samples",
                                 "", ""], f"{lost(cmd[1] - 205)}")
    # In the second move's 50 ms.
    check(lost(cmd[1] - 20)[1] == "records are missing in the 50 ms before "
          "it", f"{lost(cmd[1] - 20)}")
    # 100 ms before the third command: the second move's samples and the
    # third's 200 ms.
    check(lost(cmd[2] - 100) == [
        "", "records are missing among its samples",
        "records are missing in the 200 ms before it"],
        f"{lost(cmd[2] - 100)}")
    # The report says it, and counts the moves as not seen.
    code = lines[cmd[1] - 100].split(",")[1]
    body = counted(lines[:cmd[1] - 103] + ["$L n=3", f"40,{code}"]
                   + lines[cmd[1] - 99:], lost=3)
    r = tool(str(work("gap-ref.log", "\n".join(body) + "\n")), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[:500]}")
    check("move 3 at 2000.0 ms, channel 2 to 1900 us: records are missing "
          "in the 200 ms before move 2, its holding level, not replayed"
          in r.stdout, r.stdout[:1500])
    check("3 move(s) not replayed; they count as not seen below"
          in r.stdout, "counted")


def a_state_change_in_a_level_window_stops_each_move_that_needs_it() -> None:
    lines, cmd = three_moves()

    def changed(at: int) -> list[str]:
        body = (lines[:at] + ["$S on=0 rst=0", "$S on=1 rst=1"]
                + lines[at:])
        return whys("\n".join(body) + "\n")
    idle = "the part's state changed before the first command"
    check(changed(cmd[0] - 55) == [idle, idle, idle],
          f"{changed(cmd[0] - 55)}")
    check(changed(cmd[1] - 100) == [
        "the part's state changed inside it",
        "the part's state changed in the 200 ms before it",
        "the part's state changed in the 200 ms before move 2, its holding "
        "level"], f"{changed(cmd[1] - 100)}")
    check(changed(cmd[1] - 20)[1] == "the part's state changed in the 50 ms "
          "before it", f"{changed(cmd[1] - 20)}")
    check(changed(cmd[1] - 210) == ["the part's state changed inside it",
                                    "", ""], f"{changed(cmd[1] - 210)}")
    # Before the first sample of all: the start of those samples.
    check(changed(2) == [idle, idle, idle], f"{changed(2)}")


def a_trace_that_starts_at_its_first_command_has_no_idle_level() -> None:
    lines = synthetic(1000, [(0, 1900), (1000, 1100)]).splitlines()
    cmd1 = next(k for k, line in enumerate(lines) if line.startswith("$C"))
    check(lines[cmd1 + 1] == "10,300", lines[cmd1 + 1])
    body = counted(lines[:2] + [lines[cmd1], "0,300"] + lines[cmd1 + 2:])
    text = "\n".join(body) + "\n"
    # No sample before the first command: no idle level, and no
    # threshold for either move.
    check(whys(text) == ["no sample before the first command"] * 2,
          f"{whys(text)}")
    r = tool(str(work("no-idle.log", text)), "--no-csv")
    check(r.returncode == 0, f"exit {r.returncode}: {r.stdout[:500]}")
    check("move 2 at 1000.0 ms, channel 2 to 1100 us: no sample before the "
          "first command, not replayed" in r.stdout, r.stdout[:900])
    check("arrival, ms from the command" in r.stdout
          and "2 move(s) not replayed" in r.stdout, "the table")
    # One sample before it is a level.
    body = counted(lines[:2] + ["-10,300", lines[cmd1]] + lines[cmd1 + 1:])
    check(whys("\n".join(body) + "\n") == ["", ""], "one sample")


def a_replay_that_answers_fewer_moves_than_sent_is_exit_2() -> None:
    log = str(ARGS.fixtures / "sense-trace-sim.log")
    for name, script, lines in (
            ("one", "cat > /dev/null\n"
                    "echo 'arrived 10 30 500000 500000 2 0'\n", 1),
            ("none", "cat > /dev/null\n", 0)):
        fake = work(f"replay-{name}.sh", "#!/bin/sh\n" + script)
        fake.chmod(0o755)
        r = subprocess.run([sys.executable, str(TOOL), "--replay", str(fake),
                            "--no-csv", log], capture_output=True, text=True)
        check(r.returncode == 2 and f"the replay wrote {lines} line(s) for "
              "36 move(s) and settings" in r.stderr,
              f"{name}: exit {r.returncode}: {r.stderr}")


def a_file_that_cannot_be_read_or_written_is_exit_2() -> None:
    log = str(ARGS.fixtures / "sense-trace-sim.log")
    r = tool(log, "--no-csv", "--servo-csv", str(ARGS.work / "no-such.csv"))
    check(r.returncode == 2 and r.stderr.startswith("sense_trace: ")
          and "Traceback" not in r.stderr and r.stdout == "",
          f"--servo-csv: exit {r.returncode}: {r.stderr}")
    # A directory below a file.
    below = work("a-file", "x\n") / "below"
    r = tool(log, "--out", str(below))
    check(r.returncode == 2 and r.stderr.startswith("sense_trace: ")
          and "Traceback" not in r.stderr,
          f"--out: exit {r.returncode}: {r.stderr}")


# --- a line cut at each byte -------------------------------------------------

def every_type_log() -> str:
    """A log with every type of line: a key trace with voltages, state
    changes and dropped records, and a command trace with an edge line, a
    slewed command and a console line inside it."""
    key = ["$T v=2 n=7 trig=key t=50000 ms=5000 len=10000",
           "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=12",
           "$K t=50012"]
    for k in range(40):
        key.append(f"{-300 if k == 0 else 10},{300 + k % 7}")
        if k % 10 == 3:
            key.append(f"v{6012 + k}")
        if k == 12:
            key.append("$S on=0 rst=12")
        if k == 15:
            key.append("$S on=1 rst=13")
        if k == 25:
            key += ["$L n=12", "130,301"]
    n = {kind: sum(1 for line in key
                   if sense_trace.read_line(line)[0] == kind)
         for kind in "sv"}
    key.append(f"$Z n=7 s={n['s']} v={n['v']} l=12 m=1 ml=0 e=k")
    cmd = ["$T v=2 n=8 trig=cmd t=90000 ms=9000 len=4000",
           "$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=13"]
    for ms in range(-60, 130):
        if ms == 0:
            cmd += ["$C t=90000 ch=2 us=1900", "$E t=90120"]
        if ms == 5:
            cmd.append("rcbench-iomcu: CAN up")
        if ms == 20:
            cmd.append("$D t=89990 ch=2 us=1950")
        cmd.append(f"{-600 if ms == -60 else 10},"
                   f"{2000 if 13 <= ms < 53 else 300 + ms % 3}")
        if ms % 50 == 0:
            cmd.append(f"v{6000 + ms}")
    n = {kind: sum(1 for line in cmd
                   if (sense_trace.read_line(line) or "x")[0] == kind)
         for kind in "sv"}
    cmd.append(f"$Z n=8 s={n['s']} v={n['v']} l=0 m=3 ml=0 e=t")
    return "\n".join(["rcbench-iomcu: SENSE_TRACE", *key,
                      "rcbench-iomcu: between", *cmd,
                      "rcbench-iomcu: after"]) + "\n"


def last_number_cut(whole: str, cut: str) -> bool:
    """Whether @p cut is the line @p whole with digits off its last
    number and nothing else changed: a whole line of the same type."""
    a = sense_trace.read_line(whole)
    b = sense_trace.read_line(cut)
    return (a is not None and b is not None and a[0] == b[0]
            and a[1][:-1] == b[1][:-1] and isinstance(a[1][-1], int)
            and str(abs(a[1][-1])).startswith(str(abs(b[1][-1]))))


def a_line_cut_at_any_byte_is_damage_or_changes_nothing() -> None:
    text = every_type_log()
    damaged, whole = judged(text)
    check(not damaged, "the log is whole")
    moves = whole[2][-1]
    check(len(whole) == 3 and len(moves) == 1 and moves[0][3] == ""
          and moves[0][2] == 1950, f"two traces, one move: {moves}")
    lines = text.splitlines()
    kinds = {(sense_trace.read_line(line) or "-")[0] for line in lines}
    check(kinds == set("THCESLZsv-"), f"every type: {sorted(kinds)}")
    # Each line cut to each shorter length, its line end kept: the tool
    # reports damage or makes the same of the log.  The format has no
    # check value, so one kind of cut is neither: digits off the last
    # number of a line whose value nothing else in the trace repeats.
    unseen: dict[str, int] = {}
    cuts = 0
    for at, line in enumerate(lines):
        for k in range(len(line)):
            cuts += 1
            got = judged("\n".join(lines[:at] + [line[:k]]
                                   + lines[at + 1:]) + "\n")
            if got[0] or got[1] == whole:
                continue
            check(last_number_cut(line, line[:k]),
                  f"line {at + 1} {line!r} cut to {line[:k]!r} is read as "
                  "another log")
            kind = sense_trace.read_line(line)[0]
            unseen[kind] = unseen.get(kind, 0) + 1
    # What cannot be told: sample codes, voltages, the pulse widths of
    # $C and $D, the times of $E and $K, the reset counts of $H and $S.
    # A cut of $T's length, $L's count and every number of $Z is told.
    check(set(unseen) == set("svCESH"), f"{unseen}")
    check(sum(unseen.values()) == UNSEEN_CUTS and cuts == ALL_CUTS,
          f"{sum(unseen.values())} of {cuts} cuts: {unseen}")
    # The log stopping at each byte: damage, the same, or a log that
    # stops between its two traces, which is a log of one trace, or
    # after them, which is a log with a console line fewer.
    between = text.index("rcbench-iomcu: between")
    second = text.index("$T v=2 n=8")
    after = text.index("rcbench-iomcu: after")
    for k in range(len(text)):
        got = judged(text[:k])
        if got[0] or got[1] == whole:
            continue
        check((between - 1 <= k <= second and len(got[1]) == 2
               and got[1][1] == whole[1])
              or (k >= after - 1 and got[1][1:] == whole[1:]),
              f"the log cut at byte {k} is read as another log")
    check(judged(text[:-1])[1] == whole, "without the last line end")


# The cuts of every_type_log()'s lines, and those of them no check tells.
ALL_CUTS = 1924
UNSEEN_CUTS = 544


def a_fixture_line_cut_at_any_byte_is_no_other_line() -> None:
    # Every line of the fixture at every length: no whole line, or the
    # line with digits off its last number.  Never a line of another
    # type, and never one with another field changed.
    lines = fixture_log().splitlines()
    seen: set[str] = set()
    inside = False
    last = 0
    for line in lines:
        if line.startswith("$T"):
            inside = True
        if line in seen:
            continue
        seen.add(line)
        whole = sense_trace.read_line(line)
        check(whole is not None or not inside
              or line.startswith("rcbench-iomcu:"), f"{line!r}")
        for k in range(len(line)):
            cut = sense_trace.read_line(line[:k])
            if cut is None:
                continue
            check(last_number_cut(line, line[:k]),
                  f"{line!r} cut to {line[:k]!r} is another line")
            last += 1
    check(len(seen) == 118 and last == 292, f"{len(seen)} lines, {last}")
    # The fixture's lines that are no sample and no voltage, cut at each
    # byte in the log itself: damage or the same, but for the last number
    # of $C, $K and $H.
    text = fixture_log()
    damaged, whole = judged(text)
    check(not damaged, "the fixture is whole")
    for at, line in enumerate(lines):
        got = sense_trace.read_line(line)
        if got is not None and got[0] in "sv":
            continue
        for k in range(len(line)):
            cut = judged("\n".join(lines[:at] + [line[:k]]
                                   + lines[at + 1:]) + "\n")
            if cut[0] or cut[1] == whole:
                continue
            check(got is not None and got[0] in "CEH"
                  and last_number_cut(line, line[:k]),
                  f"line {at + 1} {line!r} cut to {line[:k]!r} is read as "
                  "another log")


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
    every_line_type_has_one_grammar,
    the_longest_line_of_each_type_is_62_characters,
    a_line_that_is_no_line_of_a_trace_is_damage,
    a_command_line_without_its_channel_and_pulse_is_damage,
    a_line_end_is_lf_or_cr_lf_and_nothing_is_taken_off_a_line,
    a_trace_has_one_set_up_line_before_its_records,
    a_trace_length_and_a_sample_period_are_version_2s,
    samples_lie_in_the_order_of_time_with_one_voltage_each,
    a_state_line_that_changes_nothing_is_damage,
    lines_of_a_trace_outside_a_trace_are_damage,
    clipped_samples_in_a_level_are_the_last_value_before_the_end,
    records_missing_in_a_level_window_stop_each_move_that_needs_it,
    a_state_change_in_a_level_window_stops_each_move_that_needs_it,
    a_trace_that_starts_at_its_first_command_has_no_idle_level,
    a_replay_that_answers_fewer_moves_than_sent_is_exit_2,
    a_file_that_cannot_be_read_or_written_is_exit_2,
    a_line_cut_at_any_byte_is_damage_or_changes_nothing,
    a_fixture_line_cut_at_any_byte_is_no_other_line,
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
