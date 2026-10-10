#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read the console log of a coprocessor built with SENSE_TRACE.

The SENSE_TRACE build prints INA3221 CH1's 1 ms samples on the USB console
(shared/sense/sense_trace.h has the line format).  This tool takes a
terminal capture of that console and, for each trace in it:

  1. checks the lines against the trace's end line: sample lines, voltage
     lines, trigger lines and the records the coprocessor says it dropped;
  2. writes one CSV file, `<log>-trace-<n>.csv`: time in ms from the
     trigger, current in A, and the bus voltage in V on the samples that
     have one;
  3. prints the noise of the samples before the first command, as they
     are and through a moving mean of 4 and of 8 samples;
  4. replays every move through shared/servo/servo_move.c with the filter
     at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10 A, and
     prints for each setting whether the move is seen and when it arrives.

A move is a trigger line: an edge line when the trace has any, a command
line otherwise.  A slewed command's destination is its $D line's pulse.
A trace with a problem -- lines that do not match its end line, no end
line, trigger lines that were not written -- is not replayed; its CSV file
is still written.  Neither is a move with records missing among its samples or
with a change of the part's state inside it: the rules count samples,
and the capture ends lost when the part goes offline.  A move's levels
are taken from the trace as the servo test takes them from its meter:

  level before   the mean of the 50 ms before the command
  holding level  the mean of the 200 ms before the command that last left
                 the destination's pulse width; the idle level, the mean
                 of the samples before the trace's first command, while
                 that end has not been held
  threshold      the largest of --floor (0.020 A), the largest distance of
                 a sample from the mean before the first command, and the
                 same over the 200 ms before this command

The window is 3005 ms and the settle count 10 samples, as the capture's.
A move whose samples end before the rules end it -- the next command, or
the trace's end -- reads `cut`.

From the traces alone the tool prints which settings see and time every
move, each setting's median arrival and its distance from the capture's
setting (filter 4, band 0.05 A), and how far apart the settings put one
move's arrival.  Without --servo-csv no arrival is compared with the
horn, and the report says so.

With --servo-csv, a servo test's CSV file recorded with the output encoder
on: each `travel angle (ms)` is paired with a move by time, and the
arrival less that travel time is printed per move, with the median per
setting.  The two files have different clocks.  The offset between them is
found from the moves themselves: the one that pairs the most rows with a
command, each within --pair-ms.  Equally spaced moves pair equally well
one move along; the tool says so when two offsets tie, and --csv-offset
sets it by hand.

The replay is a host program that links the repository's own code,
test/host/sense_trace_replay.c.  --replay names a built one; without it
the tool builds it with cmake into build-trace/.

Usage:
    python3 tools/sense_trace.py console.log
    python3 tools/sense_trace.py console.log --servo-csv SERVO003.CSV
    python3 tools/sense_trace.py console.log --out traces --floor 0.004

Exit code: 0; 1 when the log holds no trace, a trace does not match its
end line, or the servo test's rows pair with the moves at more than one
offset; 2 when the replay cannot be built or run.
"""

from __future__ import annotations

import argparse
import bisect
import math
import pathlib
import re
import statistics
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

FORMAT = 1
FILTERS = (1, 4, 8)
BANDS_A = (0.02, 0.05, 0.10)
# The capture's: SERVO_MOVE_TIMEOUT_MS + SENSE_CAP_LAG_MS, SENSE_CAP_SETTLE_N.
WINDOW_MS = 3005
SETTLE_N = 10
# A code at an end of the 13-bit range less a step: the sample is clipped,
# or one step from it.
CLIP_CODE = 4094
T_PER_MS = 10               # the trace counts 0.1 ms
RISE_MS = 50                # the level before a command
HOLD_MS = 200               # a holding level
EDGE_PAIR_MS = 30           # an edge line's command line lies this close
REF_FILTER = 4              # the capture's: SENSE_CAP_FILTER_N,
REF_BAND_A = 0.05           # SERVO_MOVE_BAND_A
SETTLE_HOLD_MS = 100        # the encoder finds a settle this long after it
# Added to a trace's times for the replay, which counts in 32 bits from 0:
# the samples from before the trigger stay above 0.
REPLAY_BASE = 1000000

RE_T = re.compile(r"^\$T v=(\d+) n=(\d+) trig=(cmd|edge|key) t=(\d+) "
                  r"ms=(\d+) len=(\d+)$")
RE_H = re.compile(r"^\$H dt_us=(\d+) shunt_uohm=(\d+) cfg=0x([0-9A-F]{4}) "
                  r"on=([01]) rst=(\d+)$")
RE_MARK = re.compile(r"^\$([CEKD]) t=(\d+)(?: ch=(\d+) us=(\d+))?$")
RE_S = re.compile(r"^(-?\d+),(-?\d+)$")
RE_V = re.compile(r"^v(-?\d+)$")
RE_STATE = re.compile(r"^\$S on=([01]) rst=(\d+)$")
RE_L = re.compile(r"^\$L n=(\d+)$")
RE_Z = re.compile(r"^\$Z n=(\d+) s=(\d+) v=(\d+) l=(\d+) m=(\d+) ml=(\d+) "
                  r"e=([tsk?])$")

END_WORDS = {"t": "its time", "s": "a changed set-up", "k": "the console"}


def s32(value: int) -> int:
    """@p value modulo 2^32 as a signed number."""
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & (1 << 31) else value


class Trace:
    """One trace: the lines from a $T line to its $Z line."""

    def __init__(self, number: int, trig: str, t0: int, t0_abs: int,
                 ms: int, length: int) -> None:
        self.number = number
        self.trig = trig
        self.t0 = t0                # as printed, modulo 2^32
        self.t0_abs = t0_abs        # 0.1 ms, unwrapped across the log
        self.ms = ms
        self.length = length
        self.version = FORMAT
        self.period_us = 0
        self.shunt_uohm = 0
        self.cfg = 0
        self.online = 0
        self.resets = 0
        self.have_setup = False
        # Per sample: time in 0.1 ms from the trigger, and the shunt code.
        self.t: list[int] = []
        self.code: list[int] = []
        self.mv: dict[int, int] = {}        # sample index -> bus mV
        self.n_volts = 0
        self.marks: list[tuple[str, int, int | None, int | None]] = []
        self.lost = 0
        self.gaps: list[int] = []           # samples read before each $L
        self.states: list[tuple[int, int, int]] = []
        self.end: dict[str, int | str] | None = None
        self.problems: list[str] = []

    def amps(self, code: int) -> float:
        """The current of a shunt code: 40 uV a step across the shunt."""
        return code * 40.0 / self.shunt_uohm

    def microamps(self, code: int) -> int:
        """As ina3221_current_ua() gives it: cut towards 0."""
        ua = abs(code) * 40000000 // self.shunt_uohm
        return ua if code >= 0 else -ua


def parse_log(text: str) -> tuple[list[Trace], int]:
    """The traces of a console log, and the lines that belong to none."""
    traces: list[Trace] = []
    cur: Trace | None = None
    other = 0
    last_t0: tuple[int, int] | None = None      # printed, unwrapped
    for raw in text.splitlines():
        line = raw.strip()
        m = RE_T.match(line)
        if m:
            if cur is not None:
                cur.problems.append("no end line: the next trace starts "
                                    "inside it")
            t0 = int(m.group(4))
            # Later traces lie later: the count runs on modulo 2^32.
            t0_abs = t0 if last_t0 is None else (
                last_t0[1] + ((t0 - last_t0[0]) & 0xFFFFFFFF))
            last_t0 = (t0, t0_abs)
            cur = Trace(int(m.group(2)), m.group(3), t0, t0_abs,
                        int(m.group(5)), int(m.group(6)))
            cur.version = int(m.group(1))
            if cur.version != FORMAT:
                cur.problems.append(f"format version {cur.version}; this "
                                    f"tool reads version {FORMAT}")
            traces.append(cur)
            continue
        if cur is None:
            other += 1
            continue
        if (m := RE_S.match(line)):
            prev = cur.t[-1] if cur.t else 0
            cur.t.append(prev + int(m.group(1)))
            cur.code.append(int(m.group(2)))
        elif (m := RE_V.match(line)):
            cur.n_volts += 1
            if cur.t:
                cur.mv[len(cur.t) - 1] = int(m.group(1))
        elif (m := RE_H.match(line)):
            cur.period_us = int(m.group(1))
            cur.shunt_uohm = int(m.group(2))
            cur.cfg = int(m.group(3), 16)
            cur.online = int(m.group(4))
            cur.resets = int(m.group(5))
            cur.have_setup = True
        elif (m := RE_MARK.match(line)):
            ch = int(m.group(3)) if m.group(3) is not None else None
            us = int(m.group(4)) if m.group(4) is not None else None
            cur.marks.append((m.group(1), s32(int(m.group(2)) - cur.t0),
                              ch, us))
        elif (m := RE_L.match(line)):
            cur.lost += int(m.group(1))
            cur.gaps.append(len(cur.t))
        elif (m := RE_STATE.match(line)):
            cur.states.append((len(cur.t), int(m.group(1)),
                               int(m.group(2))))
        elif (m := RE_Z.match(line)):
            cur.end = {"n": int(m.group(1)), "s": int(m.group(2)),
                       "v": int(m.group(3)), "l": int(m.group(4)),
                       "m": int(m.group(5)), "ml": int(m.group(6)),
                       "e": m.group(7)}
            cur = None
        else:
            other += 1
    if cur is not None:
        cur.problems.append("no end line: the log stops inside the trace")
    return traces, other


def check(tr: Trace) -> None:
    """The lines read against what the end line counts."""
    if not tr.have_setup:
        tr.problems.append("no $H line")
    elif tr.shunt_uohm == 0 and tr.code:
        tr.problems.append("samples with a shunt of 0")
    if tr.end is None:
        return
    end = tr.end
    if end["n"] != tr.number:
        tr.problems.append(f"the end line is trace {end['n']}'s")
    if end["ml"]:
        tr.problems.append(f"{end['ml']} trigger line(s) were not written: "
                           "a move may be missing")
    # The end line's counts stop at these values.
    for key, have, what, top in (
            ("s", len(tr.t), "sample", 99999999),
            ("v", tr.n_volts, "voltage", 9999999),
            ("m", len(tr.marks), "trigger", 9999),
            ("l", tr.lost, "missing-record", 99999999)):
        want = end[key]
        if min(have, top) != want:
            tr.problems.append(
                f"{have} {what} line(s) read, the end line counts {want}"
                if key != "l" else
                f"the $L lines count {have} missing record(s), the end "
                f"line {want}")


def write_csv(tr: Trace, path: pathlib.Path) -> None:
    """Time in ms from the trigger, current in A, bus voltage in V."""
    rows = ["time (ms);current (A);bus voltage (V)"]
    for k, (t, code) in enumerate(zip(tr.t, tr.code, strict=True)):
        volts = f"{tr.mv[k] / 1000.0:.3f}" if k in tr.mv else ""
        rows.append(f"{t / T_PER_MS:.1f};{tr.amps(code):.4f};{volts}")
    path.write_text("\n".join(rows) + "\n", encoding="ascii")


# --- noise and levels --------------------------------------------------------

def moving_mean(values: list[float], n: int) -> list[float]:
    """The mean of each sample and the n - 1 before it."""
    if n <= 1:
        return list(values)
    out = []
    total = 0.0
    for k, value in enumerate(values):
        total += value
        if k >= n:
            total -= values[k - n]
        if k >= n - 1:
            out.append(total / n)
    return out


def spread(values: list[float]) -> tuple[float, float, float]:
    """Mean, standard deviation, largest distance from the mean."""
    if not values:
        return 0.0, 0.0, 0.0
    mean = statistics.fmean(values)
    sd = statistics.pstdev(values) if len(values) > 1 else 0.0
    return mean, sd, max(abs(v - mean) for v in values)


class Move:
    """A command in a trace, and the levels its replay runs with."""

    def __init__(self, tr: Trace, t: int, ch: int | None,
                 us: int | None) -> None:
        self.tr = tr
        self.t = t                  # 0.1 ms from the trace's trigger
        self.ch = ch
        self.us = us
        self.rise_a: float | None = None
        self.ref_a = 0.0
        self.ref_from = ""
        self.move_a = 0.0
        self.first = 0              # samples fed: [first, last)
        self.last = 0
        self.why_not = ""           # why it is not replayed
        self.results: dict[tuple[int, float], dict] = {}
        self.horn_ms: float | None = None

    @property
    def abs_s(self) -> float:
        return (self.tr.t0_abs + self.t) / (T_PER_MS * 1000.0)


def window(tr: Trace, lo: int, hi: int) -> list[float]:
    """The currents of the samples with lo <= t < hi."""
    return [tr.amps(c) for t, c in zip(tr.t, tr.code, strict=True)
            if lo <= t < hi]


def find_moves(tr: Trace, floor_a: float) -> list[Move]:
    """The trace's moves with their levels; none for a trace without a
    shunt or without commands."""
    # A trace that does not match its end line has lines missing nobody
    # can place: the rules count samples, so nothing of it is replayed.
    if tr.shunt_uohm == 0 or tr.problems:
        return []
    edges = [m for m in tr.marks if m[0] == "E"]
    # A slewed command's line carries its first pulse; its $D line the one
    # it ended at, which is the end the servo is then held at.  The $D
    # line is the next one written for that channel after its $C line;
    # its time is the last pulse change, which can lie before the frame
    # time the $C line carries.
    cmds = []
    last: dict[int | None, list] = {}
    for mark in tr.marks:
        if mark[0] == "C":
            cmds.append(list(mark))
            last[mark[2]] = cmds[-1]
        elif mark[0] == "D" and mark[2] in last:
            last.pop(mark[2])[3] = mark[3]
    moves = []
    if edges:
        for _, t, _, _ in edges:
            # The command line of this edge: the nearest one, before or
            # after.  A command's time is computed and can lie a frame
            # late; the edge's is stamped.
            near = [c for c in cmds
                    if abs(c[1] - t) <= EDGE_PAIR_MS * T_PER_MS]
            near.sort(key=lambda c: abs(c[1] - t))
            ch, us = (near[0][2], near[0][3]) if near else (None, None)
            moves.append(Move(tr, t, ch, us))
    else:
        moves = [Move(tr, t, ch, us) for _, t, ch, us in cmds]
    moves.sort(key=lambda mv: mv.t)
    if not moves:
        return []

    idle = window(tr, -(1 << 62), moves[0].t)
    idle_a, _, idle_dev = spread(idle)
    hold: list[tuple[float, float] | None] = []
    for k, mv in enumerate(moves):
        rise = window(tr, mv.t - RISE_MS * T_PER_MS, mv.t)
        before = window(tr, mv.t - HOLD_MS * T_PER_MS, mv.t)
        hold.append(spread(before)[::2] if before else None)
        mv.rise_a = statistics.fmean(rise) if rise else None
        if not rise:
            mv.why_not = "no sample in the 50 ms before it"
        mv.first = next((i for i, t in enumerate(tr.t)
                         if t >= mv.t - RISE_MS * T_PER_MS), len(tr.t))
        nxt = moves[k + 1].t if k + 1 < len(moves) else None
        mv.last = len(tr.t) if nxt is None else next(
            (i for i, t in enumerate(tr.t) if t >= nxt), len(tr.t))
        # The rules count samples, not time: a move with records missing
        # among its samples, or at either end of them, is not judged.  The
        # capture ends lost when the part goes offline; so does a move
        # here.
        if any(mv.first <= gap <= mv.last for gap in tr.gaps):
            mv.why_not = "records are missing among its samples"
        elif any(mv.first <= at <= mv.last for at, _, _ in tr.states):
            mv.why_not = "the part's state changed inside it"
        # The destination's holding level: where a later-known hold at
        # that pulse width was left from, the idle level until then.
        mv.ref_a, mv.ref_from = idle_a, "idle level"
        for j in range(k, 0, -1):
            origin = moves[j - 1]
            if (mv.us is not None and origin.us == mv.us
                    and origin.ch == mv.ch and hold[j] is not None):
                mv.ref_a = hold[j][0]
                mv.ref_from = f"held before move {j + 1}"
                break
        dev = hold[k][1] if hold[k] is not None else 0.0
        mv.move_a = max(floor_a, idle_dev, dev)
    return moves


# --- the replay --------------------------------------------------------------

def build_replay() -> pathlib.Path:
    """sense_trace_replay, built from test/host into build-trace/."""
    build = REPO / "build-trace"
    try:
        subprocess.run(["cmake", "-S", str(REPO / "test" / "host"), "-B",
                        str(build), "-DCMAKE_BUILD_TYPE=Release"],
                       check=True, capture_output=True, text=True)
        subprocess.run(["cmake", "--build", str(build), "--target",
                        "sense_trace_replay"],
                       check=True, capture_output=True, text=True)
    except FileNotFoundError:
        print("sense_trace: cmake is not on PATH; build "
              "test/host and pass --replay", file=sys.stderr)
        sys.exit(2)
    except subprocess.CalledProcessError as err:
        print(f"sense_trace: building the replay failed:\n{err.stderr}",
              file=sys.stderr)
        sys.exit(2)
    return build / "sense_trace_replay"


def replay(binary: pathlib.Path, moves: list[Move]) -> None:
    """Every move through servo_move at every setting."""
    lines = []
    order = []
    for mv in moves:
        if mv.why_not:
            continue
        tr = mv.tr
        for n in FILTERS:
            for band in BANDS_A:
                lines.append(
                    f"m {mv.t + REPLAY_BASE} {WINDOW_MS * T_PER_MS} "
                    f"{round(mv.rise_a * 1e6)} {round(mv.ref_a * 1e6)} "
                    f"{round(mv.move_a * 1e6)} {round(band * 1e6)} "
                    f"{SETTLE_N} {n}")
                for k in range(mv.first, mv.last):
                    code = tr.code[k]
                    clip = (1 if code >= CLIP_CODE
                            else -1 if code <= -CLIP_CODE else 0)
                    lines.append(f"s {tr.t[k] + REPLAY_BASE} "
                                 f"{tr.microamps(code)} {clip}")
                lines.append("e")
                order.append((mv, (n, band)))
    if not order:
        return
    try:
        proc = subprocess.run([str(binary)], input="\n".join(lines) + "\n",
                              capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as err:
        detail = getattr(err, "stderr", "") or str(err)
        print(f"sense_trace: the replay failed: {detail}", file=sys.stderr)
        sys.exit(2)
    out = proc.stdout.split("\n")
    for (mv, setting), line in zip(order, out, strict=False):
        f = line.split()
        if len(f) != 7:
            print(f"sense_trace: the replay wrote {line!r}",
                  file=sys.stderr)
            sys.exit(2)
        state = f[0] if f[0] not in ("waiting", "moving") else "cut"
        mv.results[setting] = {
            "state": state,
            "seen": int(f[1]) >= 0,
            "moved_ms": int(f[1]) / T_PER_MS if int(f[1]) >= 0 else None,
            "arrive_ms": int(f[2]) / T_PER_MS if int(f[2]) >= 0 else None,
            "peak_a": int(f[3]) / 1e6,
            "mean_a": int(f[4]) / 1e6,
        }


# --- the servo test's CSV ----------------------------------------------------

def number(text: str) -> float | None:
    try:
        return float(text.strip().replace(",", "."))
    except ValueError:
        return None


def read_travel(path: pathlib.Path) -> list[tuple[float, float]]:
    """(row time in s, travel time in ms) for each `travel angle (ms)`."""
    out = []
    t_col = a_col = None
    for line in path.read_text(encoding="utf-8",
                               errors="replace").splitlines():
        cells = line.split(";")
        if t_col is None:
            if "travel angle (ms)" in cells and "time (s)" in cells:
                t_col = cells.index("time (s)")
                a_col = cells.index("travel angle (ms)")
            continue
        if len(cells) <= max(t_col, a_col):
            continue
        when, travel = number(cells[t_col]), number(cells[a_col])
        if when is not None and travel is not None:
            out.append((when, travel))
    if t_col is None:
        sys.exit(f"sense_trace: {path} has no `time (s)` and `travel angle "
                 "(ms)` columns: a servo test's CSV file recorded with "
                 "AS5600 on")
    return out


def pair_rows(moves: list[Move], rows: list[tuple[float, float]],
              pair_s: float, offset: float | None) -> tuple[int, bool]:
    """Give each move the travel time of the row that belongs to it.

    A row's command lies its travel time and the encoder's 100 ms hold
    before the row.  Returns the rows paired and whether another offset
    pairs as many.
    """
    if not moves or not rows:
        return 0, False
    cmd_csv = [when - (travel + SETTLE_HOLD_MS) / 1000.0
               for when, travel in rows]
    order = sorted(range(len(moves)), key=lambda i: moves[i].abs_s)
    cmd_log = [moves[i].abs_s for i in order]

    def pairs(off: float) -> list[tuple[int, int, float]]:
        got = []
        for j, c in enumerate(cmd_csv):
            at = bisect.bisect_left(cmd_log, c + off)
            near = [i for i in (at - 1, at) if 0 <= i < len(cmd_log)]
            i = min(near, key=lambda i: abs(cmd_log[i] - c - off))
            err = abs(cmd_log[i] - c - off)
            if err <= pair_s:
                got.append((j, i, err))
        return got

    def one_each(got: list[tuple[int, int, float]]) -> list:
        """A move takes one row: the nearest of those that chose it."""
        best: dict[int, tuple[int, int, float]] = {}
        for j, i, err in got:
            if i not in best or err < best[i][2]:
                best[i] = (j, i, err)
        return sorted(best.values())

    tie = False
    if offset is None:
        scored = []
        for c in cmd_csv:
            for m in cmd_log:
                got = one_each(pairs(m - c))
                scored.append((len(got), -sum(e for _, _, e in got),
                               m - c))
        if not scored:
            return 0, False
        best = max(scored)
        offset = best[2]
        tie = any(n == best[0] and abs(off - offset) > pair_s
                  for n, _, off in scored)
    got = one_each(pairs(offset))
    for j, i, _ in got:
        moves[order[i]].horn_ms = rows[j][1]
    return len(got), tie


# --- the report --------------------------------------------------------------

def fmt_ms(value: float | None, state: str) -> str:
    return f"{value:8.1f}" if value is not None else f"{state:>8}"


def report_noise(tr: Trace, until: int | None, out: list[str]) -> None:
    hi = until if until is not None else 1 << 62
    amps = window(tr, -(1 << 62), hi)
    if not amps:
        return
    what = ("before the first command" if until is not None
            else "of the whole trace")
    out.append(f"  noise {what}, {len(amps)} samples:")
    for n in FILTERS:
        mean, sd, dev = spread(moving_mean(amps, n))
        out.append(f"    filter {n}: mean {mean:.4f} A, standard deviation "
                   f"{sd:.4f} A, largest distance from the mean {dev:.4f} A")


def report_trace(tr: Trace, moves: list[Move], csv: pathlib.Path | None,
                 out: list[str]) -> None:
    span = (tr.t[-1] - tr.t[0]) / T_PER_MS if tr.t else 0.0
    out.append(f"trace {tr.number}: trigger {tr.trig} at tick {tr.ms} ms, "
               f"{len(tr.t)} samples over {span:.1f} ms, {tr.n_volts} "
               f"voltages, {tr.lost} records missing")
    if tr.have_setup:
        out.append(f"  shunt {tr.shunt_uohm / 1e6:g} Ohm, Configuration "
                   f"0x{tr.cfg:04X}, part "
                   f"{'online' if tr.online else 'not online'}, reset "
                   f"count {tr.resets}")
    if tr.end is not None:
        out.append(f"  ended by {END_WORDS.get(str(tr.end['e']), '?')}"
                   + (f"; {tr.end['ml']} trigger line(s) not written"
                      if tr.end["ml"] else ""))
    for at, online, resets in tr.states:
        out.append(f"  after sample {at}: part "
                   f"{'online' if online else 'not online'}, reset count "
                   f"{resets}")
    clipped = sum(1 for c in tr.code if abs(c) >= CLIP_CODE)
    if clipped:
        out.append(f"  {clipped} sample(s) at the end of the range: at or "
                   "past it")
    for p in tr.problems:
        out.append(f"  PROBLEM: {p}")
    if tr.problems:
        out.append("  no move of this trace is replayed")
    if not tr.problems:
        out.append("  counts match the end line")
    if csv is not None:
        out.append(f"  written: {csv}")
    if tr.shunt_uohm == 0:
        return
    report_noise(tr, moves[0].t if moves else None, out)
    if not moves:
        return
    out.append(f"  {len(moves)} move(s), time from the trace's trigger:")
    for k, mv in enumerate(moves):
        to = (f"channel {mv.ch} to {mv.us} us" if mv.us is not None
              else "edge")
        if mv.why_not:
            out.append(f"    move {k + 1} at {mv.t / T_PER_MS:.1f} ms, "
                       f"{to}: {mv.why_not}, not replayed")
            continue
        horn = (f", horn {mv.horn_ms:.0f} ms" if mv.horn_ms is not None
                else "")
        out.append(f"    move {k + 1} at {mv.t / T_PER_MS:.1f} ms, {to}: "
                   f"before {mv.rise_a:.3f} A, holding {mv.ref_a:.3f} A "
                   f"({mv.ref_from}), threshold {mv.move_a:.3f} A{horn}")
    head = "".join(f" {'move ' + str(k + 1):>8}" for k in range(len(moves)))
    total = len(moves)
    out.append(f"  arrival, ms from the command:\n"
               f"    filter  band A   seen  arrived{head}")
    for n in FILTERS:
        for band in BANDS_A:
            res = [mv.results.get((n, band)) for mv in moves]
            done = [r for r in res if r is not None]
            seen = sum(1 for r in done if r["seen"])
            arrived = sum(1 for r in done if r["arrive_ms"] is not None)
            cells = "".join(
                " " + (fmt_ms(r["arrive_ms"], r["state"]) if r is not None
                       else f"{'-':>8}") for r in res)
            seen_of = f"{seen}/{total}"
            arrived_of = f"{arrived}/{total}"
            out.append(f"    {n:>6}  {band:6.2f}  {seen_of:>5}  "
                       f"{arrived_of:>7}{cells}")
    if any(mv.horn_ms is not None for mv in moves):
        out.append("  arrival less the horn's travel time (AS5600), ms:\n"
                   f"    filter  band A{'':16}{head}")
        for n in FILTERS:
            for band in BANDS_A:
                cells = ""
                for mv in moves:
                    r = mv.results.get((n, band))
                    if (r is None or r["arrive_ms"] is None
                            or mv.horn_ms is None):
                        cells += f" {'-':>8}"
                    else:
                        cells += f" {r['arrive_ms'] - mv.horn_ms:+8.1f}"
                out.append(f"    {n:>6}  {band:6.2f}{'':16}{cells}")


def report_spread(moves: list[Move], out: list[str]) -> None:
    """What the traces alone decide: which settings time every move, and
    how far the settings' arrivals lie apart."""
    total = len(moves)
    full = []
    medians = {}
    for n in FILTERS:
        for band in BANDS_A:
            times = [mv.results[(n, band)]["arrive_ms"] for mv in moves
                     if (n, band) in mv.results
                     and mv.results[(n, band)]["arrive_ms"] is not None]
            if times:
                medians[(n, band)] = statistics.median(times)
            if len(times) == total:
                full.append((n, band))
    out.append("settings that see every move and time its arrival: "
               + (", ".join(f"filter {n} band {band:.2f} A"
                            for n, band in full) if full else "none"))
    ref = (REF_FILTER, REF_BAND_A)
    if medians:
        # A distance is taken move by move, over the moves both settings
        # time: two medians over different moves compare the moves.
        out.append(f"median arrival, and the median distance from filter "
                   f"{REF_FILTER} band {REF_BAND_A:.2f} A (the capture's "
                   "setting) over the moves both time:")
        out.append("    filter  band A   median ms  moves  distance ms  "
                   "moves")
        for (n, band), med in medians.items():
            timed = 0
            both = []
            for mv in moves:
                mine = mv.results.get((n, band), {}).get("arrive_ms")
                theirs = mv.results.get(ref, {}).get("arrive_ms")
                timed += mine is not None
                if mine is not None and theirs is not None:
                    both.append(mine - theirs)
            dist = (f"{statistics.median(both):+11.1f}" if both
                    else f"{'-':>11}")
            out.append(f"    {n:>6}  {band:6.2f}  {med:10.1f}  {timed:>5}  "
                       f"{dist}  {len(both):>5}")
    spreads = []
    for mv in moves:
        times = [r["arrive_ms"] for r in mv.results.values()
                 if r["arrive_ms"] is not None]
        if len(times) > 1:
            spreads.append(max(times) - min(times))
    if spreads:
        out.append(f"latest less earliest arrival of one move across the "
                   f"settings: median {statistics.median(spreads):.1f} ms, "
                   f"largest {max(spreads):.1f} ms over {len(spreads)} "
                   "move(s)")


def report_summary(moves: list[Move], have_horn: bool, asked: bool,
                   out: list[str]) -> None:
    if not moves:
        return
    total = len(moves)
    out.append(f"all traces, {total} move(s):")
    skipped = sum(1 for mv in moves if mv.why_not)
    if skipped:
        out.append(f"  {skipped} move(s) not replayed; they count as not "
                   "seen below")
    out.append("    filter  band A   seen  arrived"
               + ("  median difference to the horn" if have_horn else ""))
    for n in FILTERS:
        for band in BANDS_A:
            res = [(mv, mv.results.get((n, band))) for mv in moves]
            seen = sum(1 for _, r in res if r is not None and r["seen"])
            arrived = sum(1 for _, r in res
                          if r is not None and r["arrive_ms"] is not None)
            seen_of = f"{seen}/{total}"
            arrived_of = f"{arrived}/{total}"
            line = f"    {n:>6}  {band:6.2f}  {seen_of:>5}  {arrived_of:>7}"
            if have_horn:
                diffs = [r["arrive_ms"] - mv.horn_ms for mv, r in res
                         if r is not None and r["arrive_ms"] is not None
                         and mv.horn_ms is not None]
                line += (f"  {statistics.median(diffs):+8.1f} ms over "
                         f"{len(diffs)} move(s)" if diffs
                         else "  no move with both times")
            out.append(line)
    report_spread(moves, out)
    if not have_horn:
        out.append("not compared with the horn: "
                   + ("no row of the servo test's CSV file pairs with a "
                      "move" if asked else "no --servo-csv given")
                   + ". An arrival is when the current is back at the "
                   "holding level, which can lead or lag the horn.")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Check and evaluate a SENSE_TRACE console log.")
    ap.add_argument("log", type=pathlib.Path,
                    help="the terminal's capture of the console")
    ap.add_argument("--out", type=pathlib.Path,
                    help="directory for the CSV files (default: the log's)")
    ap.add_argument("--servo-csv", type=pathlib.Path,
                    help="a servo test's CSV file with `travel angle (ms)`")
    ap.add_argument("--csv-offset", type=float,
                    help="coprocessor time less CSV time, s (default: "
                         "found from the moves)")
    ap.add_argument("--pair-ms", type=float, default=150.0,
                    help="a row and a command pair within this (default "
                         "150 ms)")
    ap.add_argument("--floor", type=float, default=0.020,
                    help="the threshold's floor, A (default 0.020)")
    ap.add_argument("--replay", type=pathlib.Path,
                    help="a built test/host sense_trace_replay")
    ap.add_argument("--no-csv", action="store_true",
                    help="write no CSV files")
    args = ap.parse_args()
    if not math.isfinite(args.floor) or args.floor <= 0.0:
        ap.error("--floor is a current above 0 A")

    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as err:
        print(f"sense_trace: {err}", file=sys.stderr)
        return 1
    traces, other = parse_log(text)
    out = [f"{args.log}: {len(traces)} trace(s), {other} other line(s)"]
    if not traces:
        print("\n".join(out))
        print("sense_trace: no trace in the log", file=sys.stderr)
        return 1

    out_dir = args.out if args.out is not None else args.log.parent
    if not args.no_csv:
        out_dir.mkdir(parents=True, exist_ok=True)
    all_moves: list[Move] = []
    per_trace: list[tuple[Trace, list[Move], pathlib.Path | None]] = []
    names: dict[str, int] = {}
    for tr in traces:
        check(tr)
        moves = find_moves(tr, args.floor)
        all_moves += moves
        csv = None
        if not args.no_csv and tr.shunt_uohm != 0:
            name = f"{args.log.stem}-trace-{tr.number}"
            names[name] = names.get(name, 0) + 1
            if names[name] > 1:         # the coprocessor restarted
                name += f"-{names[name]}"
            csv = out_dir / f"{name}.csv"
            write_csv(tr, csv)
        per_trace.append((tr, moves, csv))

    if all_moves:
        replay(args.replay if args.replay is not None else build_replay(),
               all_moves)
    have_horn = False
    ambiguous = False
    if args.servo_csv is not None:
        rows = read_travel(args.servo_csv)
        paired, tie = pair_rows(all_moves, rows, args.pair_ms / 1000.0,
                                args.csv_offset)
        have_horn = paired > 0
        out.append(f"{args.servo_csv}: {len(rows)} travel time(s), "
                   f"{paired} paired with a move")
        if tie:
            out.append("  PROBLEM: another offset between the two clocks "
                       "pairs as many; set --csv-offset")
            ambiguous = True
    for tr, moves, csv in per_trace:
        report_trace(tr, moves, csv, out)
    report_summary(all_moves, have_horn, args.servo_csv is not None, out)
    print("\n".join(out))
    bad = sum(1 for tr in traces if tr.problems)
    if bad:
        print(f"sense_trace: {bad} trace(s) with a problem",
              file=sys.stderr)
    if ambiguous:
        print("sense_trace: the servo test's rows pair with the moves at "
              "more than one offset", file=sys.stderr)
    return 1 if bad or ambiguous else 0


if __name__ == "__main__":
    sys.exit(main())
