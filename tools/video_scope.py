#!/usr/bin/env python3
r"""Write the figure of a video's scope scenes: simulated and captured traces.

The player's scope scene draws tools/video/plots/NAME.json: traces as
polylines, a circuit as a list of drawn parts, and the figures of the
model.  This tool writes hitec-precharge.json, the D series signal wire
of the Hitec programmer (protocols/hitec/README.md, "Receive by precharge
and sample"), from a circuit that ngspice simulates:

    python3 tools/video_scope.py
    python3 tools/video_scope.py --capture pre=read.csv --rate 200e6

The circuit: the pad's driver as a switch of RDRV to 3.3V, the line's
capacitance CLINE to ground, the servo's pull-down RPD to ground, the
servo's output as a switch of RSRV to ground, and, where a run fits it,
the pull-up RPU from the line to the servo supply VSERVO.  RPD and RPU
are the values of the protocol reference.  RDRV, CLINE, VTH and RSRV are
fitted to two measurements on a D485HW.  The reference's table: the pad
reads a released line high 40 to 213ns after the release and low at 267ns,
and a precharge of 1 system clock cycle fails at 213ns.  The logic analyser
capture testbench/captures/hitec32 (200MHz, threshold 1.4V): a precharge on
a released line reads 260 to 290ns above 1.4V, and one against a servo that
pulls low reads 0 to 20ns or does not appear.

--capture NAME=FILE adds a logic analyser trace: a sigrok CSV export of
one channel, one sample a line, at --rate samples a second.  Its edges
are kept, in ns from the first sample.

Attributes:
    PLOTS: The directory of the figures.
    CLOCK_HZ: The IOmcu's system clock.
    PERIOD, PRECHARGE, WAIT: The sampler's period, precharge and wait
        after the release, in system clock cycles (hitec_pre_timing() at
        150MHz).
    BIT_NS: One bit of the D series, 115,200bit/s.
    MODEL: The circuit's values.
    MEASURED: The pass window of the protocol reference, in ns after the
        release, and the pulses of the capture at the analyser's
        threshold.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

PLOTS = pathlib.Path(__file__).resolve().parent / "video" / "plots"
CLOCK_HZ = 150e6
PERIOD, PRECHARGE, WAIT = 163, 15, 17
BIT_NS = 1e9 / 115200
MODEL = {"rdrv": 50.0, "cline": 72e-12, "rpd": 3000.0, "rsrv": 37.0,
         "rpu": 2200.0, "vio": 3.3, "vservo": 5.0, "vth": 1.1}
MEASURED = {"pass": [40.0, 213.3], "fail": 266.7,
            "pass_cycles": [6, 32], "fail_cycles": 40,
            "probe_v": 1.4, "pulse_ns": [260.0, 290.0],
            "low_pulse_ns": [0.0, 20.0], "period_ns": 1086.5}
_CYCLE = 1e9 / CLOCK_HZ
_EDGE = 2.0     # a switch's control ramp, ns


def frame_levels(byte: int) -> list:
    """Give the line levels of one inverted 8N1 byte of a D series answer.

    Args:
        byte: The byte, 0 to 255.

    Returns:
        10 levels, start bit first: True where the servo leaves the line
        released (a high level), False where it pulls it low.
    """
    bits = [(byte >> n) & 1 for n in range(8)]
    return [True] + [bit == 0 for bit in bits] + [False]


def _pwl(points: list) -> str:
    return "PWL(" + " ".join(f"{t:.3f}n {v:g}" for t, v in points) + ")"


def _steps(levels: list, start: float, width: float, rest: float) -> list:
    """Give the points of a control voltage that follows spans of a level.

    Args:
        levels: (level) per span, 0 or 1.
        start: The first span's start, ns.
        width: A span's length, ns.
        rest: The level before and after.

    Returns:
        (t, v) points with a ramp of _EDGE at each change.
    """
    points, last = [(0.0, rest)], rest
    for n, level in enumerate(list(levels) + [rest]):
        if level != last:
            at = start + n * width
            points += [(at, last), (at + _EDGE, level)]
            last = level
    return points


def netlist(drive: list, servo: list, stop: float, pullup: bool) -> str:
    """Write the circuit for one run.

    Args:
        drive: (t, v) points of the pad driver's control, 1 drives high.
        servo: (t, v) points of the servo output's control, 1 pulls low.
        stop: The end of the run, ns.
        pullup: Whether RPU is fitted from the line to the servo supply.

    Returns:
        The ngspice deck; it writes time and the line's voltage to
        out.txt.
    """
    m = MODEL
    lines = [
        "* Hitec D series signal wire",
        f"Vio vio 0 {m['vio']}",
        f"Vsv vsv 0 {m['vservo']}",
        f"Vd cd 0 {_pwl(drive)}",
        f"Vs cs 0 {_pwl(servo)}",
        "Sdrv vio line cd 0 swd",
        "Ssrv line 0 cs 0 sws",
        f"Cl line 0 {m['cline']}",
        f"Rpd line 0 {m['rpd']}",
        f".model swd sw(vt=0.5 vh=0 ron={m['rdrv']} roff=1e9)",
        f".model sws sw(vt=0.5 vh=0 ron={m['rsrv']} roff=1e9)",
    ]
    if pullup:
        lines.append(f"Rpu vsv line {m['rpu']}")
    lines += [
        ".control",
        "set wr_singlescale",
        "set numdgt=7",
        f"tran 0.5n {stop:.1f}n 0 0.5n uic",
        "wrdata out.txt v(line)",
        ".endc",
        ".end",
    ]
    return "\n".join(lines) + "\n"


def simulate(deck: str) -> list:
    """Run ngspice on a deck.

    Args:
        deck: What netlist() wrote.

    Returns:
        (ns, V) per time point.
    """
    with tempfile.TemporaryDirectory() as tmp:
        (pathlib.Path(tmp) / "run.cir").write_text(deck, encoding="utf-8")
        done = subprocess.run(["ngspice", "-b", "run.cir"], cwd=tmp,
                              capture_output=True, text=True, check=False)
        out = pathlib.Path(tmp) / "out.txt"
        if done.returncode != 0 or not out.is_file():
            sys.exit("ngspice failed:\n" + done.stdout[-2000:]
                     + done.stderr[-2000:])
        rows = [line.split() for line in out.read_text(
            encoding="utf-8").splitlines()]
    return [(float(t) * 1e9, float(v)) for t, v in rows]


def thin(points: list, tol: float) -> list:
    """Drop the points a straight line between their neighbours replaces.

    Args:
        points: (x, y) with rising x.
        tol: The largest error in y the result may have.

    Returns:
        The kept points, the first and the last among them.
    """
    # A swinging door: the slopes from the last kept point that stay
    # within tol of every point passed since narrow until none is left.
    kept = [points[0]]
    x0, y0 = points[0]
    lo, hi = float("-inf"), float("inf")
    for n in range(1, len(points)):
        x, y = points[n]
        if x <= x0:
            continue
        slope = (y - y0) / (x - x0)
        if not lo <= slope <= hi:
            x0, y0 = points[n - 1]
            kept.append((x0, y0))
            lo, hi = float("-inf"), float("inf")
        lo = max(lo, (y - tol - y0) / (x - x0))
        hi = min(hi, (y + tol - y0) / (x - x0))
    kept.append(points[-1])
    return kept


def value(points: list, at: float) -> float:
    """Read a trace at a time.

    Args:
        points: (x, y) with rising x.
        at: The time.

    Returns:
        y at that time, between the two points around it.
    """
    lo, hi = 0, len(points) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if points[mid][0] <= at:
            lo = mid
        else:
            hi = mid
    (x0, y0), (x1, y1) = points[lo], points[hi]
    return y0 if x1 == x0 else y0 + (y1 - y0) * (at - x0) / (x1 - x0)


def _pulses(count: int, cycles: int = PRECHARGE, first: float = 0.0) -> list:
    points = [(0.0, 0.0)] if first > 0 else []
    for n in range(count):
        at = first + n * PERIOD * _CYCLE
        points += [(at, 0), (at + _EDGE, 1), (at + cycles * _CYCLE, 1),
                   (at + cycles * _CYCLE + _EDGE, 0)]
    return points


def _series(points: list, unit: str = "V", tol: float = 0.003) -> dict:
    kept = thin(points, tol)
    return {"unit": unit, "x": [round(x, 2) for x, _ in kept],
            "y": [round(y, 4) for _, y in kept]}


def _square(edges: list, end: float) -> dict:
    """Give a logic level as a trace.

    Args:
        edges: (t, level) per change, the first at the trace's start.
        end: The trace's end, ns.

    Returns:
        A series of 0 and 1 with a vertical step at each change.
    """
    xs, ys = [], []
    for n, (at, level) in enumerate(edges):
        if n:
            xs.append(at)
            ys.append(edges[n - 1][1])
        xs.append(at)
        ys.append(level)
    xs.append(end)
    ys.append(edges[-1][1])
    return {"unit": "", "x": [round(x, 2) for x in xs], "y": ys}


def cycle(cycles: int, low: bool) -> list:
    """Simulate one sample period.

    Args:
        cycles: The precharge, in system clock cycles.
        low: Whether the servo pulls the line low throughout.

    Returns:
        (ns, V), the precharge starting at 100ns.
    """
    stop = 100 + PERIOD * _CYCLE
    return simulate(netlist(_pulses(1, cycles, 100.0),
                            [(0.0, 1.0 if low else 0.0)], stop, False))


def window() -> dict:
    """Read the simulated line at the measured window's settings.

    Returns:
        The voltage at each (precharge cycles, wait cycles) the protocol
        reference measures, and whether it reads high against VTH; under
        "pulse" and "low_pulse" the ns one precharge stays above the
        analyser's threshold on a released line and against the servo.
    """
    found = {}
    for cycles in (1, 2, 3, 15, 30):
        run = cycle(cycles, False)
        for wait in (6, 28, 32, 40):
            volts = value(run, 100 + (cycles + wait) * _CYCLE)
            found[f"{cycles}/{wait}"] = [round(volts, 3),
                                         volts > MODEL["vth"]]
    # What the analyser sees of one precharge: the time above its
    # threshold, on a released line and against the servo.
    for name, low in (("pulse", False), ("low_pulse", True)):
        run = cycle(PRECHARGE, low)
        found[name] = round(sum(
            b[0] - a[0] for a, b in zip(run, run[1:], strict=False)
            if a[1] > MEASURED["probe_v"]), 1)
    return found


def faults(found: dict) -> list:
    """Compare the simulated window with the measured one.

    Args:
        found: What window() returned.

    Returns:
        A message per setting where the model reads another level than
        the measurement.
    """
    want = {"1/6": True, "1/28": True, "30/6": True, "30/28": True,
            "15/6": True, "15/28": True, "2/32": True, "3/32": True,
            "15/32": True, "1/32": False, "15/40": False, "30/40": False}
    wrong = [f"precharge {key.split('/')[0]} cycles, sample "
             f"{key.split('/')[1]} cycles after the release: the model "
             f"reads {'high' if found[key][1] else 'low'}"
             for key, high in want.items() if found[key][1] != high]
    lo, hi = MEASURED["pulse_ns"]
    if not lo <= found["pulse"] <= hi:
        wrong.append(f"a precharge on a released line reads "
                     f"{found['pulse']}ns above {MEASURED['probe_v']}V, "
                     f"measured {lo:g} to {hi:g}ns")
    if found["low_pulse"] > MEASURED["low_pulse_ns"][1]:
        wrong.append(f"a precharge against the servo reads "
                     f"{found['low_pulse']}ns above {MEASURED['probe_v']}V, "
                     f"measured up to {MEASURED['low_pulse_ns'][1]:g}ns")
    return wrong


def answer(byte: int, mode: str) -> tuple:
    """Simulate one answer byte on the line.

    Args:
        byte: The byte the servo sends.
        mode: "pullup" with RPU fitted, "bare" with neither a pull-up nor
            a precharge, or "precharge".

    Returns:
        The line as (ns, V), and the samples as (ns, level) for
        "precharge", else an empty list.  The start bit begins at 2 bit
        times.
    """
    levels = frame_levels(byte)
    start = 2 * BIT_NS
    stop = 13 * BIT_NS
    servo = _steps([0 if high else 1 for high in levels], start, BIT_NS, 1)
    count = int(stop / (PERIOD * _CYCLE)) if mode == "precharge" else 0
    drive = _pulses(count, PRECHARGE, 300.0) if count else [(0.0, 0.0)]
    run = simulate(netlist(drive, servo, stop, mode == "pullup"))
    samples = []
    for n in range(count):
        at = 300.0 + n * PERIOD * _CYCLE + (PRECHARGE + WAIT) * _CYCLE
        samples.append((round(at, 2), int(value(run, at) > MODEL["vth"])))
    return run, samples


def circuit() -> dict:
    """Give the drawing of the signal wire's circuit.

    Returns:
        Its size, its parts by name, the points labels attach to and the
        paths a current is drawn along, in the drawing's own units.
    """
    top, line, gnd, pad = 60, 250, 440, 210
    return {
        "size": [1000, 500],
        "items": [
            {"id": "mcu", "t": "box", "r": [20, 20, 330, 460],
             "text": "IOmcu · RP2350 · GPIO 13"},
            {"id": "servo", "t": "box", "r": [690, 110, 290, 370],
             "text": "Hitec D series servo"},
            {"id": "vio", "t": "rail", "at": [pad, top + 40],
             "text": "3.3 V"},
            {"id": "drv", "t": "sw", "a": [pad, top + 40], "b": [pad, line],
             "text": "pad driver"},
            {"id": "in", "t": "buf", "a": [pad, line], "b": [pad, gnd - 40],
             "text": "input"},
            {"id": "wire", "t": "wire",
             "p": [[pad, line], [880, line]], "text": "signal wire",
             "at": [370, line + 34]},
            {"id": "pad", "t": "dot", "at": [pad, line]},
            {"id": "vsv", "t": "rail", "at": [450, top], "text": "5.0 V"},
            {"id": "rpu", "t": "res", "a": [450, top], "b": [450, line],
             "text": "2.2 kΩ"},
            {"id": "cl", "t": "cap", "a": [570, line], "b": [570, gnd],
             "text": "72 pF"},
            {"id": "rpd", "t": "res", "a": [750, line], "b": [750, gnd],
             "text": "3 kΩ"},
            {"id": "srv", "t": "sw", "a": [880, line], "b": [880, gnd],
             "text": "output", "side": "r"},
            {"id": "gnd", "t": "wire", "p": [[570, gnd], [880, gnd]]},
            {"id": "g", "t": "gnd", "at": [640, gnd]},
        ],
        "nodes": {"pad": [pad, line], "drv": [pad, 175], "in": [pad, 345],
                  "wire": [370, line], "rpu": [450, 155], "cl": [570, 345],
                  "rpd": [750, 345], "srv": [880, 345],
                  "line": [520, line]},
        "paths": {
            "charge": [[pad, top + 40], [pad, line], [570, line],
                       [570, gnd]],
            "decay": [[570, line], [750, line], [750, gnd]],
            "pullup": [[450, top], [450, line], [750, line], [750, gnd]],
            "low": [[570, line], [880, line], [880, gnd]],
            "fight": [[pad, top + 40], [pad, line], [880, line],
                      [880, gnd]],
        },
    }


def capture(path: pathlib.Path, rate: float) -> dict:
    """Read a logic analyser capture.

    Args:
        path: A CSV file with one sample of one channel per line, 0 or 1,
            as sigrok-cli -O csv writes it; lines of other text are
            skipped.
        rate: Samples a second.

    Returns:
        The trace as a series, in ns from the first sample.
    """
    edges, last, count = [], None, 0
    for line in path.read_text(encoding="utf-8").splitlines():
        cell = line.strip().split(",")[-1].strip()
        if cell not in ("0", "1"):
            continue
        level = int(cell)
        if level != last:
            edges.append((count * 1e9 / rate, level))
            last = level
        count += 1
    if not edges:
        sys.exit(f"{path}: no sample")
    return _square(edges, count * 1e9 / rate)


def figure() -> dict:
    """Simulate every trace of the figure.

    Returns:
        The figure the player's scope scene draws.
    """
    found = window()
    wrong = faults(found)
    if wrong:
        sys.exit("the model does not reproduce the measured window:\n  "
                 + "\n  ".join(wrong))
    series = {}
    for name, cycles, low in (("cycle", PRECHARGE, False),
                              ("cycle-low", PRECHARGE, True),
                              ("cycle-1", 1, False)):
        series[name] = _series(cycle(cycles, low))
    # The line after the release, from the release on.
    release = 100 + PRECHARGE * _CYCLE
    decay = [(t - release, v) for t, v in cycle(PRECHARGE, False)
             if t >= release]
    series["decay"] = _series(decay)
    series["drive"] = _square(
        [(0.0, 0), (100.0, 1), (release, 0)], 100 + PERIOD * _CYCLE)
    byte = 0x69
    for mode in ("pullup", "bare", "precharge"):
        run, samples = answer(byte, mode)
        series[f"byte-{mode}"] = _series(run, tol=0.006)
        if samples:
            edges = [(0.0, samples[0][1])]
            for at, level in samples:
                if level != edges[-1][1]:
                    edges.append((at, level))
            series["byte-samples"] = _square(edges, 13 * BIT_NS)
            series["byte-points"] = {
                "unit": "", "x": [at for at, _ in samples],
                "y": [level for _, level in samples]}
    levels = frame_levels(byte)
    series["byte-servo"] = _square(
        [(0.0, 0)] + [(2 * BIT_NS + n * BIT_NS, int(high))
                      for n, high in enumerate(levels)], 13 * BIT_NS)
    return {
        "name": "hitec-precharge",
        "model": MODEL,
        "timing": {"clock_hz": CLOCK_HZ, "period_ns": PERIOD * _CYCLE,
                   "precharge_ns": PRECHARGE * _CYCLE,
                   "wait_ns": WAIT * _CYCLE, "bit_ns": round(BIT_NS, 2),
                   "byte": byte},
        "measured": MEASURED,
        "window": found,
        "series": series,
        "circuits": {"line": circuit()},
    }


def main() -> int:
    """Write the figure.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-o", "--out", type=pathlib.Path,
                    default=PLOTS / "hitec-precharge.json")
    ap.add_argument("--capture", action="append", default=[],
                    metavar="NAME=FILE",
                    help="add a logic analyser trace as series capture-NAME")
    ap.add_argument("--rate", type=float, default=200e6,
                    help="samples a second of the captures")
    args = ap.parse_args()
    if shutil.which("ngspice") is None:
        sys.exit("ngspice is not installed")
    drawn = figure()
    # A capture already in the figure stays unless it is given again.
    if args.out.is_file():
        old = json.loads(args.out.read_text(encoding="utf-8"))
        for name, trace in old["series"].items():
            if name.startswith("capture-"):
                drawn["series"][name] = trace
        if "captures" in old:
            drawn["captures"] = old["captures"]
    for item in args.capture:
        name, _, path = item.partition("=")
        if not path:
            sys.exit("--capture takes NAME=FILE")
        drawn["series"][f"capture-{name}"] = capture(pathlib.Path(path),
                                                     args.rate)
        drawn.setdefault("captures", {})[name] = {"rate_hz": args.rate}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(drawn, separators=(",", ":")) + "\n",
                        encoding="utf-8")
    sizes = {name: len(trace["x"]) for name, trace in drawn["series"].items()}
    print(f"{args.out}: {args.out.stat().st_size} bytes, points {sizes}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
