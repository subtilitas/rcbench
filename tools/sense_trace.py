#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read the console log of a coprocessor built with SENSE_TRACE.

The SENSE_TRACE build prints INA3221 CH1's 1 ms samples on the USB console
(shared/sense/sense_trace.h has the line format).  This tool takes a
terminal capture of that console and, for each trace in it:

  1. reads every line against the format, and checks the lines against
     the trace's end line: sample lines, voltage lines, trigger lines and
     the records the coprocessor says it dropped;
  2. writes one CSV file, `<log>-trace-<n>.csv`: time in ms from the
     trigger, current in A, and the bus voltage in V on the samples that
     have one;
  3. prints the noise of the samples before the first command, as they
     are and through a moving mean of 4 and of 8 samples;
  4. replays every move through shared/servo/servo_move.c with the filter
     at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10 A, and
     prints for each setting whether the move is seen and when it arrives.

A move is a command line, at the time of its edge line when the trace has
one within 30 ms of it; an edge line with no command line that near is a
move too.  An edge line is one command's and a command line one edge's:
as many of them are paired as can be, and of the ways to pair that many
the one with the least distance in sum.  A slewed command's destination is
its $D line's pulse.
Commands within 30 ms of each other change the current together: none of
them is replayed.  A move that follows another output's move before that
one arrived carries its current too; the tool does not tell these apart,
and the earlier move reads `cut`.
A line of a trace is a whole line of version 2: every field of its type,
in its order, nothing before, between or after them, each number in the
range the coprocessor holds it in and written as the coprocessor writes it.
A line ends LF or CR LF.  Between a trace's $T and $Z line stand its lines
and the coprocessor's other console lines, which start `rcbench-iomcu:`;
anything else there is damage.  So are a second $H line, a line before the
$H line, a trace length or sample period version 2 does not write, a
sample timed before the one before it, a second voltage for one sample, a
$S line that repeats the state before it, and a line of a trace outside a
trace.
A trace with a problem -- damage, lines that do not match its end line, no
end line, trigger lines that were not written -- is not replayed; its CSV
file is still written.

The format has no check value.  A line cut inside its last number is a
whole line with another number, and the tool reads it as one: a sample's
code, a voltage, a command's pulse width, an edge's or a key's time, the
reset count of a $H or $S line.  A cut anywhere else, and a lost sample,
voltage or trigger line, is told.  A lost $S line is told when the next
one repeats the state.  A log that stops between two traces is a log with
fewer traces.

A move's levels are taken from the trace as the servo test takes them from
its meter:

  level before   the mean of the 50 ms before the command
  holding level  the mean of the 200 ms before the command that last left
                 the destination's pulse width; the idle level, the mean
                 of the samples before the trace's first command, while
                 that end has not been held
  threshold      the largest of --floor (0.020 A), the largest distance of
                 a sample from the mean before the first command, and the
                 same over the 200 ms before this command

Each level is taken from the samples of one window, and each sample as
the replay takes it: a clipped one as the last value before that end of
the range.  The report says how many of a move's level samples are
clipped.  A window with no sample, with records missing in it or at its
ends, or with a change of the part's state in it gives no level, and no
move that needs the level is replayed; the report names the window:

  before the first command   every move's threshold, and the holding level
                             of a move to a pulse width no move left
  the 50 ms before a move    its level before
  the 200 ms before a move   its threshold, and the holding level of each
                             later move back to the pulse width it left
  a move's samples           from 50 ms before it to the next move or the
                             trace's end: what the replay is fed

The rules count samples, and the capture ends lost when the part goes
offline.  The window is 3005 ms and the settle count 10 samples, as the
capture's.
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
one move along; the tool says so when two offsets pair as many rows and
not the same rows with the same moves, and --csv-offset sets it by hand.
A log with a restart of the coprocessor in it has a clock for each boot:
each boot's moves are paired on their own, and the rows of a later boot
lie after those of an earlier one.  Where they do not, no row is paired
and the tool says so.  A restart is a $T line whose millisecond tick
lies before the last one's, or whose trace number is not past the last
one's; both count modulo their range, 2^32 ms and 65536, and the later
of two is the one less than half the range on: 24.8 days, 32768 traces.
Numbers that are missing are no restart: a trace with no console has a
number and no line.  A restart after which neither goes back is not told.

The replay is a host program that links the repository's own code,
test/host/sense_trace_replay.c.  --replay names a built one; without it
the tool builds it with cmake into build-trace/.

Usage:
    python3 tools/sense_trace.py console.log
    python3 tools/sense_trace.py console.log --servo-csv SERVO003.CSV
    python3 tools/sense_trace.py console.log --out traces --floor 0.004

--floor is 0.000001 A to 1000 A, --pair-ms above 0 ms to 60000 ms,
--csv-offset within 1e9 s of 0.

Exit code: 0; 1 when the log cannot be read or holds no trace, a trace
has a problem (damage, it does not match its end line, or its end line's
reason is a capital letter), a line of a trace stands outside a trace,
the servo test's rows pair with the moves in more than one way, or two
boots' rows lie among each other; 2 when the replay cannot be built or
run or answers another number of moves than it was sent, the servo
test's CSV file cannot be read, a CSV file cannot be written, or an
argument is refused.
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

FORMAT = 2
FILTERS = (1, 4, 8)
BANDS_A = (0.02, 0.05, 0.10)
# The capture's: SERVO_MOVE_TIMEOUT_MS + SENSE_CAP_LAG_MS, SENSE_CAP_SETTLE_N.
WINDOW_MS = 3005
SETTLE_N = 10
# The end codes of the 13-bit range: a clipped sample, at or past that end.
# The capture judges one as the last value before the end.
CLIP_HI = 4095
CLIP_LO = -4096
CLIP_AS = 4094
T_PER_MS = 10               # the trace counts 0.1 ms
RISE_MS = 50                # the level before a command
HOLD_MS = 200               # a holding level
EDGE_PAIR_MS = 30           # an edge line's command line lies this close
PAIR_ROUND_S = 1e-9         # a row at --pair-ms from its command is within
                            # it, whatever the sums that got there rounded
TOGETHER_MS = 30            # commands this close are one change of current
REF_FILTER = 4              # the capture's: SENSE_CAP_FILTER_N,
REF_BAND_A = 0.05           # SERVO_MOVE_BAND_A
SETTLE_HOLD_MS = 100        # the encoder finds a settle this long after it
# A move's time in the replay, which counts in 32 bits from 0; its samples
# are timed from it, and those of the 50 ms before it stay above 0.
REPLAY_BASE = 1000000
# The ranges of the numbers on the command line.  The replay takes a
# current as whole uA.
FLOOR_MIN_A = 0.000001
FLOOR_MAX_A = 1000.0
PAIR_MAX_MS = 60000.0
OFFSET_MAX_S = 1e9

# What version 2 writes, shared/sense/sense_trace.c: a trace's length by
# its trigger, and the sample period.
TRACE_MS = {"cmd": 4000, "edge": 4000, "key": 10000}
PERIOD_US = 1000
# The coprocessor's other console lines start with this; one can stand
# between two lines of a trace.
CONSOLE = "rcbench-iomcu:"

# A line's grammar: every field it has, in its order, and nothing else.
# A number is written without a sign it does not need and without leading
# zeros; N is one from 0 up, INT one with a sign.
N = r"(0|[1-9][0-9]*)"
INT = r"(0|-?[1-9][0-9]*)"
U16 = (0, 0xFFFF)
U32 = (0, 0xFFFFFFFF)
I32 = (-(1 << 31), (1 << 31) - 1)
# The kind, the line, and each number's range as the coprocessor holds it
# (None: text).  A sample's code is the 13-bit range.  The ranges keep
# every line within the 62 characters the console writes before CR LF.
GRAMMAR = (
    ("s", re.compile(rf"{INT},{INT}"), (I32, (-4096, 4095))),
    ("v", re.compile(rf"v{INT}"), (I32,)),
    ("C", re.compile(rf"\$([CD]) t={N} ch={N} us={N}"),
     (None, U32, U16, U16)),
    ("E", re.compile(rf"\$([EK]) t={N}"), (None, U32)),
    ("S", re.compile(rf"\$S on=([01]) rst={N}"), ((0, 1), (0, 255))),
    ("L", re.compile(rf"\$L n={N}"), ((1, 0xFFFFFF),)),
    ("T", re.compile(rf"\$T v=([0-9]) n={N} trig=(cmd|edge|key) t={N} "
                     rf"ms={N} len={N}"),
     ((0, 9), U16, None, U32, U32, (0, 99999))),
    ("H", re.compile(rf"\$H dt_us={N} shunt_uohm={N} cfg=0x([0-9A-F]{{4}}) "
                     rf"on=([01]) rst={N}"),
     ((0, 9999), U32, None, (0, 1), (0, 255))),
    ("Z", re.compile(rf"\$Z n={N} s={N} v={N} l={N} m={N} ml={N} "
                     r"e=([tskTSK])"),
     (U16, (0, 99999999), (0, 9999999), (0, 99999999), (0, 9999),
      (0, 9999), None)),
)

# A capital letter: the coprocessor wrote a line without having seen its
# sampling core past that line's time.
END_WORDS = {"t": "its time", "s": "a changed set-up", "k": "the console"}


def s32(value: int) -> int:
    """@p value modulo 2^32 as a signed number."""
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & (1 << 31) else value


def read_line(line: str) -> tuple[str, tuple] | None:
    """A whole line of a trace as its kind and its fields; None for
    anything else: a line cut short or with more than its fields, or a
    number out of its range."""
    for kind, regex, ranges in GRAMMAR:
        m = regex.fullmatch(line)
        if m is None:
            continue
        fields: list[int | str] = []
        for text, limits in zip(m.groups(), ranges, strict=True):
            if limits is None:
                fields.append(text)
                continue
            value = int(text)
            if not limits[0] <= value <= limits[1]:
                return None
            fields.append(value)
        return kind, tuple(fields)
    return None


class Trace:
    """One trace: the lines from a $T line to its $Z line."""

    def __init__(self, number: int, trig: str, t0: int, t0_abs: int,
                 ms: int, length: int) -> None:
        self.number = number
        self.trig = trig
        self.t0 = t0                # as printed, modulo 2^32
        self.t0_abs = t0_abs        # 0.1 ms, unwrapped across the log
        self.boot = 0               # restarts of the coprocessor before it
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
        # What the lines themselves say is wrong: (line number, text) of
        # the lines that are no line of a trace, and counts.
        self.bad: list[tuple[int, str]] = []
        self.early = 0              # lines before the $H line
        self.setups = 0             # $H lines
        self.back = 0               # samples timed before the one before
        self.same_state = 0         # $S lines that change nothing
        self.two_volts = 0          # voltages behind a sample that has one
        self.state: tuple[int, int] | None = None

    def amps(self, code: int) -> float:
        """The current of a shunt code: 40 uV a step across the shunt."""
        return code * 40.0 / self.shunt_uohm

    def microamps(self, code: int) -> int:
        """As ina3221_current_ua() gives it: cut towards 0."""
        ua = abs(code) * 40000000 // self.shunt_uohm
        return ua if code >= 0 else -ua

    def judged(self, k: int) -> tuple[int, int]:
        """Sample @p k as the capture judges it: the current in uA, and 1
        for a sample at or past the top of the range, -1 for one at or
        past the bottom, 0 for a value.  A clipped sample is the last
        value before that end.  Every level and the replay take a sample
        from here."""
        code = self.code[k]
        clip = 1 if code >= CLIP_HI else -1 if code <= CLIP_LO else 0
        return self.microamps(CLIP_AS * clip if clip else code), clip


def parse_log(text: str) -> tuple[list[Trace], int, list[tuple[int, str]]]:
    """The traces of a console log, the lines that belong to none, and
    (line number, text) of the lines of a trace that stand outside one.

    A line ends LF or CR LF.  Inside a trace a line is a whole line of the
    format, or one of the coprocessor's other console lines, or it is
    counted as damage in Trace.bad; nothing is taken off a line before it
    is read."""
    traces: list[Trace] = []
    cur: Trace | None = None
    other = 0
    stray: list[tuple[int, str]] = []
    last_t0: tuple[int, int] | None = None      # printed, unwrapped
    last_n = 0
    last_ms = 0
    boot = 0
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    for no, raw in enumerate(lines, 1):
        line = raw[:-1] if raw.endswith("\r") else raw
        got = read_line(line)
        kind, f = got if got is not None else (None, ())
        if kind == "T":
            if cur is not None:
                cur.problems.append("no end line: the next trace starts "
                                    "inside it")
            version, number, trig, t0, ms, length = f
            # Within a boot the millisecond tick and the trace number
            # both run on, modulo 2^32 and 65536, the number by more than
            # one past a trace no console took.  A tick that lies before
            # the last one's, or a number that is not past the last
            # one's, is a restart, and its clock a new one.
            ms_on = (ms - last_ms) & 0xFFFFFFFF
            n_on = (number - last_n) & 0xFFFF
            if last_t0 is not None and (ms_on >= 1 << 31
                                        or not 0 < n_on < 1 << 15):
                boot += 1
                last_t0 = None
            last_n, last_ms = number, ms
            if last_t0 is None:
                t0_abs = t0
            else:
                # Later traces lie later.  The 0.1 ms count wraps every
                # 4.97 days; the millisecond tick says how often it did
                # between the two.
                on = (t0 - last_t0[0]) & 0xFFFFFFFF
                wraps = max(0, round((ms_on * T_PER_MS - on) / (1 << 32)))
                t0_abs = last_t0[1] + on + (wraps << 32)
            last_t0 = (t0, t0_abs)
            cur = Trace(number, trig, t0, t0_abs, ms, length)
            cur.boot = boot
            cur.version = version
            if cur.version != FORMAT:
                cur.problems.append(f"format version {cur.version}; this "
                                    f"tool reads version {FORMAT}")
            traces.append(cur)
            continue
        if cur is None:
            # A line of a trace with no start line before it: the start
            # line is lost, or cut short.
            if got is not None or line.startswith("$"):
                stray.append((no, line))
            else:
                other += 1
            continue
        if got is None:
            if line.startswith(CONSOLE):
                other += 1
            else:
                cur.bad.append((no, line))
            continue
        if kind == "H":
            cur.setups += 1
            if cur.setups > 1:
                continue
            (cur.period_us, cur.shunt_uohm, cfg, cur.online,
             cur.resets) = f
            cur.cfg = int(cfg, 16)
            cur.have_setup = True
            cur.state = (cur.online, cur.resets)
            continue
        if not cur.have_setup:
            cur.early += 1
        if kind == "s":
            if cur.t and f[0] < 0:
                cur.back += 1
            prev = cur.t[-1] if cur.t else 0
            cur.t.append(prev + f[0])
            cur.code.append(f[1])
        elif kind == "v":
            cur.n_volts += 1
            if cur.t:
                if len(cur.t) - 1 in cur.mv:
                    cur.two_volts += 1
                cur.mv[len(cur.t) - 1] = f[0]
        elif kind == "C":
            cur.marks.append((f[0], s32(f[1] - cur.t0), f[2], f[3]))
        elif kind == "E":
            cur.marks.append((f[0], s32(f[1] - cur.t0), None, None))
        elif kind == "L":
            cur.lost += f[0]
            cur.gaps.append(len(cur.t))
        elif kind == "S":
            # The coprocessor writes the line for a state that is another
            # than the one before it.
            if (f[0], f[1]) == cur.state:
                cur.same_state += 1
            cur.state = (f[0], f[1])
            cur.states.append((len(cur.t), f[0], f[1]))
        else:
            cur.end = dict(zip(("n", "s", "v", "l", "m", "ml", "e"), f,
                               strict=True))
            cur = None
    if cur is not None:
        cur.problems.append("no end line: the log stops inside the trace")
    return traces, other, stray


def shown(line: str) -> str:
    """A damaged line for the report: its first 40 characters."""
    return repr(line if len(line) <= 40 else line[:40] + "...")


def check(tr: Trace) -> None:
    """The lines read against the format and against what the end line
    counts."""
    if tr.bad:
        no, line = tr.bad[0]
        tr.problems.append(
            f"{len(tr.bad)} line(s) that are no whole line of a trace, the "
            f"first is line {no}: {shown(line)}")
    if not tr.have_setup:
        tr.problems.append("no $H line")
    elif tr.shunt_uohm == 0 and tr.code:
        tr.problems.append("samples with a shunt of 0")
    if tr.early:
        tr.problems.append(f"{tr.early} line(s) before the $H line")
    if tr.setups > 1:
        tr.problems.append(f"{tr.setups} $H lines")
    if tr.version == FORMAT:
        if tr.length != TRACE_MS[tr.trig]:
            tr.problems.append(
                f"a {tr.trig} trace of {tr.length} ms; version {FORMAT} "
                f"writes {TRACE_MS[tr.trig]} ms")
        if tr.have_setup and tr.period_us != PERIOD_US:
            tr.problems.append(
                f"a sample period of {tr.period_us} us; version {FORMAT} "
                f"writes {PERIOD_US} us")
    if tr.back:
        tr.problems.append(f"{tr.back} sample line(s) timed before the "
                           "sample before them")
    if tr.two_volts:
        tr.problems.append(f"{tr.two_volts} voltage line(s) behind a "
                           "sample that has one")
    if tr.same_state:
        tr.problems.append(f"{tr.same_state} $S line(s) that change "
                           "nothing: a line before them is missing")
    if tr.end is None:
        return
    end = tr.end
    if end["n"] != tr.number:
        tr.problems.append(f"the end line is trace {end['n']}'s")
    if end["ml"]:
        tr.problems.append(f"{end['ml']} trigger line(s) were not written: "
                           "a move may be missing")
    if str(end["e"]).isupper():
        tr.problems.append("the sampling core was not seen past a trigger "
                           "line or the end in time: a sample of before it "
                           "may be missing or stand behind the line")
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
        self.clipped = 0            # clipped samples in its levels
        self.results: dict[tuple[int, float], dict] = {}
        self.horn_ms: float | None = None
        self.horn_row: int | None = None

    @property
    def abs_s(self) -> float:
        return (self.tr.t0_abs + self.t) / (T_PER_MS * 1000.0)


class Level:
    """The samples of one window a level is taken from: lo <= t < hi."""

    def __init__(self, tr: Trace, lo: int | None, hi: int | None) -> None:
        # A trace that is replayed has its samples in the order of time.
        self.first = 0 if lo is None else bisect.bisect_left(tr.t, lo)
        self.last = (len(tr.t) if hi is None
                     else bisect.bisect_left(tr.t, hi))
        got = [tr.judged(k) for k in range(self.first, self.last)]
        self.amps = [ua / 1e6 for ua, _ in got]
        self.clipped = {self.first + k for k, (_, clip) in enumerate(got)
                        if clip}
        self.why = ""
        if not self.amps:
            self.why = "no sample"
        elif broken(self.first, self.last, tr.gaps):
            self.why = "records are missing"
        elif broken(self.first, self.last, [at for at, _, _ in tr.states]):
            self.why = "the part's state changed"

    @property
    def mean(self) -> float:
        return statistics.fmean(self.amps)

    @property
    def dev(self) -> float:
        return spread(self.amps)[2]


def broken(first: int, last: int, marks: list[int]) -> bool:
    """Whether a $L or $S line stands among the samples [first, last) or
    at either end of them.  A line is kept as the number of samples before
    it.  At the ends counts: the records that are missing there can be the
    window's, and the rules count samples."""
    return any(first <= at <= last for at in marks)


def pair_near(a: list[int], b: list[int], limit: int) -> dict[int, int]:
    """Index in @p a to index in @p b for times no more than @p limit
    apart, each index in one pair at most: as many pairs as there can be,
    and of the ways to pair that many the least distance in sum.

    Both lists are taken in the order of their times.  Two pairs that
    cross can be uncrossed without a pair farther apart than the farther
    of the two and with no more distance in sum, so the pairs looked at
    keep that order.
    """
    ia = sorted(range(len(a)), key=lambda k: (a[k], k))
    ib = sorted(range(len(b)), key=lambda k: (b[k], k))
    # best[i][j]: (pairs, -distance, step) for the first i of a and the
    # first j of b.
    best = [[(0, 0, "")] * (len(ib) + 1) for _ in range(len(ia) + 1)]
    for i in range(len(ia) + 1):
        for j in range(len(ib) + 1):
            if i == 0 and j == 0:
                continue
            tries = []
            if i > 0 and j > 0:
                far = abs(a[ia[i - 1]] - b[ib[j - 1]])
                if far <= limit:
                    n, d, _ = best[i - 1][j - 1]
                    tries.append((n + 1, d - far, "pair"))
            if i > 0:
                tries.append((*best[i - 1][j][:2], "a"))
            if j > 0:
                tries.append((*best[i][j - 1][:2], "b"))
            best[i][j] = max(tries, key=lambda t: t[:2])
    out: dict[int, int] = {}
    i, j = len(ia), len(ib)
    while i > 0 or j > 0:
        step = best[i][j][2]
        if step == "pair":
            out[ia[i - 1]] = ib[j - 1]
            i, j = i - 1, j - 1
        elif step == "a":
            i -= 1
        else:
            j -= 1
    return out


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
    # line's time is the last pulse change, which can lie before the frame
    # time its $C line carries, and the lines stand in the order of their
    # times: the $D line is of the latest command of its channel that
    # lies no more than a frame's lateness after it, whichever of the two
    # lines comes first.
    cmds = [list(m) for m in tr.marks if m[0] == "C"]
    ended: set[int] = set()
    for mark in tr.marks:
        if mark[0] != "D":
            continue
        mine = [k for k, c in enumerate(cmds)
                if c[2] == mark[2]
                and c[1] <= mark[1] + EDGE_PAIR_MS * T_PER_MS]
        if mine:
            k = max(mine, key=lambda k: cmds[k][1])
            if k not in ended:
                ended.add(k)
                cmds[k][3] = mark[3]
    # An edge line and a command line near it, before or after, are one
    # move at the edge's time: a command's time is computed and can lie a
    # frame late, the edge's is stamped.  A command line is one edge's at
    # most, and no edge goes without a command that pairing the lines
    # another way would give it; a command with no edge line is a move at
    # its own time.
    of_edge = pair_near([e[1] for e in edges], [c[1] for c in cmds],
                        EDGE_PAIR_MS * T_PER_MS)
    moves = []
    for ke, e in enumerate(edges):
        ch, us = cmds[of_edge[ke]][2:4] if ke in of_edge else (None, None)
        moves.append(Move(tr, e[1], ch, us))
    moves += [Move(tr, c[1], c[2], c[3]) for kc, c in enumerate(cmds)
              if kc not in of_edge.values()]
    moves.sort(key=lambda mv: mv.t)
    if not moves:
        return []
    # Commands this close change the current together: no sample is one's
    # and not the other's.
    together = set()
    for k in range(len(moves) - 1):
        if moves[k + 1].t - moves[k].t <= TOGETHER_MS * T_PER_MS:
            together.update((k, k + 1))

    # Every level is a mean or a largest distance over one window of
    # samples, each taken as the replay takes it: a clipped sample as the
    # last value before that end of the range.  A window with no sample,
    # with records missing in it or at its ends, or with a change of the
    # part's state in it gives no level, and each move that needs the
    # level is not replayed.  The windows and what needs them:
    #
    #   idle       the samples before the first move: every move's
    #              threshold, and the holding level of a move to a pulse
    #              width no move left before
    #   rise k     the 50 ms before move k: its level before
    #   hold k     the 200 ms before move k: its threshold, and the
    #              holding level of each later move back to the pulse
    #              width move k left
    #   samples k  from 50 ms before move k to the next move, or the
    #              trace's end: what the replay is fed
    idle = Level(tr, None, moves[0].t)
    hold: list[Level] = []
    for k, mv in enumerate(moves):
        rise = Level(tr, mv.t - RISE_MS * T_PER_MS, mv.t)
        hold.append(Level(tr, mv.t - HOLD_MS * T_PER_MS, mv.t))
        mv.first = rise.first
        nxt = moves[k + 1].t if k + 1 < len(moves) else None
        mv.last = (len(tr.t) if nxt is None
                   else bisect.bisect_left(tr.t, nxt))
        # The destination's holding level: where the move that last left
        # that pulse width left it from, the idle level when none did.
        ref, mv.ref_from = idle, "idle level"
        ref_where = "before the first command"
        for j in range(k, 0, -1):
            origin = moves[j - 1]
            if (mv.us is not None and origin.us == mv.us
                    and origin.ch == mv.ch):
                ref = hold[j]
                mv.ref_from = f"held before move {j + 1}"
                ref_where = (f"in the 200 ms before move {j + 1}, its "
                             "holding level")
                break
        needs = ((idle, "before the first command"),
                 (rise, f"in the {RISE_MS} ms before it"),
                 (hold[k], f"in the {HOLD_MS} ms before it"),
                 (ref, ref_where))
        whys = [f"{level.why} {where}" for level, where in needs
                if level.why]
        if k in together:
            mv.why_not = (f"another command within {TOGETHER_MS} ms, the "
                          "current is of both")
        elif whys:
            mv.why_not = whys[0]
        # The rules count samples, not time: a move with records missing
        # among its samples, or at either end of them, is not judged.  The
        # capture ends lost when the part goes offline; so does a move
        # here.
        elif broken(mv.first, mv.last, tr.gaps):
            mv.why_not = "records are missing among its samples"
        elif broken(mv.first, mv.last, [at for at, _, _ in tr.states]):
            mv.why_not = "the part's state changed inside it"
        if mv.why_not:
            continue
        mv.rise_a = rise.mean
        mv.ref_a = ref.mean
        mv.move_a = max(floor_a, idle.dev, hold[k].dev)
        mv.clipped = len(set().union(*(level.clipped
                                       for level, _ in needs)))
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
                # Times go to the replay from the move's own: none is
                # below 0, wherever the trace's trigger lies.
                lines.append(
                    f"m {REPLAY_BASE} {WINDOW_MS * T_PER_MS} "
                    f"{round(mv.rise_a * 1e6)} {round(mv.ref_a * 1e6)} "
                    f"{round(mv.move_a * 1e6)} {round(band * 1e6)} "
                    f"{SETTLE_N} {n}")
                for k in range(mv.first, mv.last):
                    ua, clip = tr.judged(k)
                    lines.append(f"s {tr.t[k] - mv.t + REPLAY_BASE} "
                                 f"{ua} {clip}")
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
    out = proc.stdout.splitlines()
    if len(out) != len(order):
        print(f"sense_trace: the replay wrote {len(out)} line(s) for "
              f"{len(order)} move(s) and settings", file=sys.stderr)
        sys.exit(2)
    for (mv, setting), line in zip(order, out, strict=True):
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
    """A cell's number; None for anything else, `nan` and `inf` too."""
    try:
        value = float(text.strip().replace(",", "."))
    except ValueError:
        return None
    return value if math.isfinite(value) else None


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
        if when is not None and travel is not None and travel >= 0.0:
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
    pairs as many rows and not the same rows with the same moves.
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
            if err <= pair_s + PAIR_ROUND_S:
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
        # The pairing changes only where a row comes to a command's
        # tolerance or passes from one command to the next: every such
        # place is tried, where a row lies at the tolerance itself, and
        # every offset between two of them once, with the offsets that
        # put a row on a command.
        marks = set()
        for c in cmd_csv:
            marks.update(m - c - pair_s for m in cmd_log)
            marks.update(m - c + pair_s for m in cmd_log)
            marks.update((a + b) / 2 - c for a, b in
                         zip(cmd_log, cmd_log[1:], strict=False))
        edges = sorted(marks)
        offs = sorted({m - c for c in cmd_csv for m in cmd_log}
                      | set(edges)
                      | {(a + b) / 2 for a, b in
                         zip(edges, edges[1:], strict=False)})
        scored = []
        for off in offs:
            got = one_each(pairs(off))
            scored.append((len(got), -sum(e for _, _, e in got), off,
                           frozenset((j, i) for j, i, _ in got)))
        best = max(scored, key=lambda s: s[:3])
        offset = best[2]
        # Another offset that pairs as many is the same answer when it
        # gives the same rows to the same moves, however far apart the two
        # offsets lie, and a second answer when it does not, however
        # close.
        tie = any(n == best[0] and which != best[3]
                  for n, _, _, which in scored)
    got = one_each(pairs(offset))
    for j, i, _ in got:
        moves[order[i]].horn_ms = rows[j][1]
        moves[order[i]].horn_row = j
    return len(got), tie


# --- the report --------------------------------------------------------------

def fmt_ms(value: float | None, state: str) -> str:
    return f"{value:8.1f}" if value is not None else f"{state:>8}"


def report_noise(tr: Trace, until: int | None, out: list[str]) -> None:
    amps = Level(tr, None, until).amps
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
        out.append("  ended by "
                   f"{END_WORDS.get(str(tr.end['e']).lower(), '?')}"
                   + (f"; {tr.end['ml']} trigger line(s) not written"
                      if tr.end["ml"] else ""))
    for at, online, resets in tr.states:
        out.append(f"  after sample {at}: part "
                   f"{'online' if online else 'not online'}, reset count "
                   f"{resets}")
    clipped = sum(1 for c in tr.code if c >= CLIP_HI or c <= CLIP_LO)
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
                   f"({mv.ref_from}), threshold {mv.move_a:.3f} A{horn}"
                   + (f"; {mv.clipped} clipped sample(s) in these levels, "
                      "each taken as the last value before the end of the "
                      "range" if mv.clipped else ""))
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
                    help="coprocessor time less CSV time, s, for the "
                         "log's first boot (default: found from the "
                         "moves)")
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
    # Each number has a range: the replay takes currents as whole uA, and
    # no time of a log or a CSV file is past the limits here.
    if not FLOOR_MIN_A <= args.floor <= FLOOR_MAX_A:
        ap.error(f"--floor is a current from {FLOOR_MIN_A:.6f} A to "
                 f"{FLOOR_MAX_A:g} A")
    if not 0.0 < args.pair_ms <= PAIR_MAX_MS:
        ap.error(f"--pair-ms is a time above 0 ms, {PAIR_MAX_MS:g} ms at "
                 "most")
    if (args.csv_offset is not None
            and not abs(args.csv_offset) <= OFFSET_MAX_S):
        ap.error(f"--csv-offset is a number of seconds, {OFFSET_MAX_S:g} s "
                 "from 0 at most")
    if args.csv_offset is not None and args.servo_csv is None:
        ap.error("--csv-offset needs --servo-csv")

    try:
        # Bytes: a line's end is read as the console wrote it.
        text = args.log.read_bytes().decode("utf-8", errors="replace")
    except OSError as err:
        print(f"sense_trace: {err}", file=sys.stderr)
        return 1
    rows: list[tuple[float, float]] = []
    if args.servo_csv is not None:
        try:
            rows = read_travel(args.servo_csv)
        except OSError as err:
            print(f"sense_trace: {err}", file=sys.stderr)
            return 2
    traces, other, stray = parse_log(text)
    out = [f"{args.log}: {len(traces)} trace(s), {other} other line(s)"]
    if stray:
        no, line = stray[0]
        out.append(f"PROBLEM: {len(stray)} line(s) of a trace outside a "
                   f"trace, the first is line {no}: {shown(line)}; a trace's "
                   "start line is missing or damaged")
    if not traces:
        print("\n".join(out))
        print("sense_trace: no trace in the log", file=sys.stderr)
        return 1

    out_dir = args.out if args.out is not None else args.log.parent
    try:
        if not args.no_csv:
            out_dir.mkdir(parents=True, exist_ok=True)
    except OSError as err:
        print(f"sense_trace: {err}", file=sys.stderr)
        return 2
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
            try:
                write_csv(tr, csv)
            except OSError as err:
                print(f"sense_trace: {err}", file=sys.stderr)
                return 2
        per_trace.append((tr, moves, csv))

    if all_moves:
        replay(args.replay if args.replay is not None else build_replay(),
               all_moves)
    have_horn = False
    ambiguous = False
    if args.servo_csv is not None:
        n_rows = len(rows)
        # Each boot of the coprocessor has a clock of its own: its moves
        # are paired on their own, each boot with every row.  The file's
        # rows and the log's boots are both in the order of time, so a
        # later boot's rows lie after an earlier boot's; where they do
        # not, a boot took rows of another and nothing tells which.
        paired, tie, mixed = 0, False, False
        boots = sorted({mv.tr.boot for mv in all_moves})
        last_row = -1
        for boot in boots:
            mine = [mv for mv in all_moves if mv.tr.boot == boot]
            n, t = pair_rows(mine, rows, args.pair_ms / 1000.0,
                             args.csv_offset if boot == boots[0] else None)
            taken = [mv.horn_row for mv in mine if mv.horn_row is not None]
            if taken:
                mixed = mixed or min(taken) <= last_row
                last_row = max(last_row, *taken)
            paired += n
            tie = tie or t
        if mixed:
            for mv in all_moves:
                mv.horn_ms = mv.horn_row = None
            paired = 0
        have_horn = paired > 0
        if len(boots) > 1:
            out.append(f"the coprocessor restarted {len(boots) - 1} time(s) "
                       "in the log: each boot is paired on its own clock")
        out.append(f"{args.servo_csv}: {n_rows} travel time(s), "
                   f"{paired} paired with a move")
        if mixed:
            out.append("  PROBLEM: the rows that pair with one boot's moves "
                       "do not lie after those of the boot before it; no "
                       "row is paired. Give each boot's part of the log "
                       "with its part of the CSV file")
            ambiguous = True
        elif tie:
            out.append("  PROBLEM: another offset between the two clocks "
                       "pairs as many rows, and not the same rows with the "
                       "same moves; set --csv-offset")
            ambiguous = True
    for tr, moves, csv in per_trace:
        report_trace(tr, moves, csv, out)
    report_summary(all_moves, have_horn, args.servo_csv is not None, out)
    print("\n".join(out))
    bad = sum(1 for tr in traces if tr.problems)
    if bad:
        print(f"sense_trace: {bad} trace(s) with a problem",
              file=sys.stderr)
    if stray:
        print(f"sense_trace: {len(stray)} line(s) of a trace outside a "
              "trace", file=sys.stderr)
    if ambiguous:
        print("sense_trace: the servo test's rows pair with the moves in "
              "more than one way", file=sys.stderr)
    return 1 if bad or stray or ambiguous else 0


if __name__ == "__main__":
    sys.exit(main())
