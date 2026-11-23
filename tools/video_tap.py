#!/usr/bin/env python3
r"""Write the figure of the phase tap video: one beep from lead to record.

The player's scope scenes draw tools/video/plots/phase-tap.json.  This
tool writes it in three steps:

    python3 tools/video_tap.py

1. A model of the motor lead while an ESC (electronic speed controller)
   beeps: bursts of its PWM (pulse-width modulation) carrier at the tone's
   rate, at the supply voltage.
2. ngspice simulates the tap of protocols/phase_tap/README.md on that
   lead: 4.7kΩ in series, a 3.0V zener to ground and 8.2kΩ to ground at
   the pin.
3. The edges of the simulated pin go through the capture's hold-off and
   the detector of protocols/phase_tap/tone.c, compiled for the host and
   run in 1ms passes as the IOmcu's core 1 runs it, with the settings of
   the TONE page's defaults.  Its beeps and windows go into the figure.

The beep lengths, pitches and pauses are those measured on a Flycolor
FlyDragon Lite 40A at 7.6V (STATUS.md, "Stick programming's beep
timing").  The carrier, its duty, the share of a tone period the lead is
driven for, the pin's capacitance and the input threshold are model
values: MODEL names each.

Attributes:
    REPO: The repository.
    TICK_HZ: The capture's tick, a quarter of the 150MHz system clock.
    HOLD_NS: The capture's hold-off.
    MODEL: The values of the lead, the tap and the pin.
    SHORT, LONG: A short and a long beep, (ms, Hz), as measured.
    GAP_MS, GROUP_MS: The pause between two beeps of a group and between
        two groups, as measured.
    GROUP_GAP_MS: The silence that ends a group, the setting's default.
    DEFAULTS: The TONE page's default settings.

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

import video_scope

REPO = pathlib.Path(__file__).resolve().parent.parent
TICK_HZ = 37_500_000
HOLD_NS = 8000
MODEL = {"supply": 7.6, "carrier_hz": 12000.0, "duty": 0.25, "driven": 0.5,
         "rs": 4700.0, "rpd": 8200.0, "vz": 3.0, "cpin": 10e-12,
         "vth": 1.1, "edge_ns": 200.0}
SHORT, LONG = (127.9, 2090.0), (383.7, 1866.0)
GAP_MS, GROUP_MS = 148.0, 3230.0
GROUP_GAP_MS = 700.0
DEFAULTS = {"f_min_hz": 400, "f_max_hz": 6500, "split_pct": 8, "gap_ms": 3,
            "min_periods": 3}
_DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include "tone.h"

/* Edges as "ns level" lines on stdin; passes of 1ms as core 1 makes them. */
int main(int argc, char **argv)
{
    static tone_t d;
    tone_cfg_t c;
    tone_holdoff_t h;
    tone_edge_t out[2];
    tone_beep_t b;
    tone_window_t w;
    unsigned long long ns, end_ns = strtoull(argv[1], NULL, 10);
    unsigned long long last_window = ~0ull, now = 0, kept = 0, taken = 0;
    int level, have = 0;
    const unsigned long long hz = strtoull(argv[2], NULL, 10);

    tone_cfg_defaults(&c, (uint32_t)hz);
    c.f_min_hz = (uint32_t)atoi(argv[3]);
    c.f_max_hz = (uint32_t)atoi(argv[4]);
    c.split_pct = (uint32_t)atoi(argv[5]);
    c.gap_us = (uint32_t)atoi(argv[6]) * 1000u;
    c.min_periods = (uint32_t)atoi(argv[7]);
    c.hold_ns = (uint32_t)atoi(argv[8]);
    if (!tone_init(&d, &c)) { return 2; }
    tone_holdoff_init(&h, (uint32_t)hz, c.hold_ns);
    have = scanf("%llu %d", &ns, &level) == 2;
    for (now = 1000000ull; now <= end_ns; now += 1000000ull) {
        const uint64_t tick = now * hz / 1000000000ull;
        while (have && ns <= now) {
            const size_t n = tone_holdoff_edge(&h, ns * hz / 1000000000ull,
                                               level != 0, out);
            taken++;
            for (size_t i = 0; i < n; i++) {
                kept++;
                printf("E %llu %d\n", (unsigned long long)out[i].t,
                       out[i].level ? 1 : 0);
                tone_edge(&d, out[i].t, out[i].level);
            }
            have = scanf("%llu %d", &ns, &level) == 2;
        }
        const size_t n = tone_holdoff_advance(&h, tick, out);
        for (size_t i = 0; i < n; i++) {
            kept++;
            printf("E %llu %d\n", (unsigned long long)out[i].t,
                   out[i].level ? 1 : 0);
            tone_edge(&d, out[i].t, out[i].level);
        }
        tone_advance(&d, tone_holdoff_horizon(&h, tick));
        while (tone_next_beep(&d, &b)) {
            printf("B %llu %llu %.2f %.1f %u %u %llu\n",
                   (unsigned long long)b.start, (unsigned long long)b.end,
                   (double)b.freq_hz, (double)b.carrier_hz, b.bursts,
                   b.periods, now / 1000000ull);
        }
        if (tone_window(&d, &w) && w.index != last_window) {
            last_window = w.index;
            printf("W %llu %d %.2f %u %u\n", (unsigned long long)w.index,
                   w.present ? 1 : 0, (double)w.freq_hz, w.periods,
                   w.bursts);
        }
    }
    printf("S %llu %llu %u %u\n", taken, kept, tone_stats(&d)->beeps,
           tone_stats(&d)->rejected);
    return 0;
}
"""


def beep_pulses(start_ns: float, length_ms: float, tone_hz: float) -> list:
    """Give the carrier pulses of one beep on the motor lead.

    Args:
        start_ns: The beep's first rise.
        length_ms: The beep's length.
        tone_hz: Its pitch.

    Returns:
        (rise ns, fall ns) per pulse: in every tone period the lead is
        driven for MODEL["driven"] of it, chopped at the carrier.
    """
    period = 1e9 / tone_hz
    carrier = 1e9 / MODEL["carrier_hz"]
    high = carrier * MODEL["duty"]
    pulses = []
    for n in range(round(length_ms * 1e6 / period)):
        at = start_ns + n * period
        k = 0
        while k * carrier + high <= period * MODEL["driven"]:
            pulses.append((at + k * carrier, at + k * carrier + high))
            k += 1
    return pulses


def group(count: int, start_ns: float) -> tuple:
    """Give the beeps of one group: an item or a value of the menu.

    Args:
        count: The number the group sounds, 1 to 9: short beeps up to 4,
            from 5 on one long beep and count - 5 short ones.
        start_ns: The group's start.

    Returns:
        ((start ns, ms, Hz) per beep, the group's end in ns).
    """
    kinds = [SHORT] * count if count < 5 else [LONG] + [SHORT] * (count - 5)
    beeps, at = [], start_ns
    for length, tone in kinds:
        beeps.append((at, length, tone))
        at += (length + GAP_MS) * 1e6
    return beeps, at - GAP_MS * 1e6


def netlist(pulses: list, stop: float, supply: float) -> str:
    """Write the circuit of the tap for one run.

    Args:
        pulses: (rise ns, fall ns) of the lead.
        stop: The end of the run, ns.
        supply: The lead's high level, V.

    Returns:
        The ngspice deck; it writes the time, the lead and the pin to
        out.txt.
    """
    m = MODEL
    points = [(0.0, 0.0)]
    for rise, fall in pulses:
        points += [(rise, 0.0), (rise + m["edge_ns"], supply),
                   (fall, supply), (fall + m["edge_ns"], 0.0)]
    return "\n".join([
        "* Phase tap",
        f"Vph lead 0 {video_scope._pwl(points)}",
        f"Rs lead pin {m['rs']}",
        f"Rpd pin 0 {m['rpd']}",
        f"Cp pin 0 {m['cpin']}",
        "Dz 0 pin dz",
        f".model dz d(bv={m['vz']} ibv=1m rs=10 cjo=20p)",
        ".control",
        "set wr_singlescale",
        "set numdgt=7",
        f"tran 20n {stop:.1f}n 0 20n",
        "wrdata out.txt v(lead) v(pin)",
        ".endc",
        ".end",
    ]) + "\n"


def simulate(pulses: list, stop: float, supply: float) -> tuple:
    """Run ngspice on the tap.

    Args:
        pulses: (rise ns, fall ns) of the lead.
        stop: The end of the run, ns.
        supply: The lead's high level, V.

    Returns:
        The lead and the pin, each (ns, V) per time point.
    """
    with tempfile.TemporaryDirectory() as tmp:
        (pathlib.Path(tmp) / "run.cir").write_text(
            netlist(pulses, stop, supply), encoding="utf-8")
        done = subprocess.run(["ngspice", "-b", "run.cir"], cwd=tmp,
                              capture_output=True, text=True, check=False)
        out = pathlib.Path(tmp) / "out.txt"
        if done.returncode != 0 or not out.is_file():
            sys.exit("ngspice failed:\n" + done.stdout[-2000:]
                     + done.stderr[-2000:])
        rows = [line.split() for line in out.read_text(
            encoding="utf-8").splitlines()]
    lead = [(float(r[0]) * 1e9, float(r[1])) for r in rows]
    pin = [(float(r[0]) * 1e9, float(r[2])) for r in rows]
    return lead, pin


def crossings(trace: list, level: float) -> list:
    """Find where a trace crosses a level.

    Args:
        trace: (ns, V) with rising ns.
        level: The level, V.

    Returns:
        (ns, 1 for a rise through the level or 0 for a fall) per crossing.
    """
    found = []
    for (x0, y0), (x1, y1) in zip(trace, trace[1:], strict=False):
        if (y0 > level) != (y1 > level):
            at = x0 + (x1 - x0) * (level - y0) / (y1 - y0)
            found.append((at, int(y1 > level)))
    return found


def detect(edges: list, end_ns: float) -> dict:
    """Run the capture's hold-off and the detector on edges.

    Args:
        edges: (ns, level after the edge) in time order.
        end_ns: How long the passes go on.

    Returns:
        "edges": the (tick, level) the hold-off lets through; "beeps":
        per beep its first rise and last edge in ticks, its pitch, its
        carrier, its bursts and periods, and the pass in ms that handed
        it out; "windows": (index, tone held, Hz, periods, bursts) per
        8ms window; "taken", "kept", "reported" and "rejected" counts.
    """
    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp) / "driver.c"
        src.write_text(_DRIVER, encoding="utf-8")
        exe = pathlib.Path(tmp) / "driver"
        tap = REPO / "protocols" / "phase_tap"
        subprocess.run(["cc", "-O2", "-std=c11", "-I", str(tap / "include"),
                        str(src), str(tap / "tone.c"), "-lm", "-o", str(exe)],
                       check=True)
        d = DEFAULTS
        done = subprocess.run(
            [str(exe), str(int(end_ns)), str(TICK_HZ), str(d["f_min_hz"]),
             str(d["f_max_hz"]), str(d["split_pct"]), str(d["gap_ms"]),
             str(d["min_periods"]), str(HOLD_NS)],
            input="".join(f"{int(ns)} {level}\n" for ns, level in edges),
            capture_output=True, text=True, check=True)
    found = {"edges": [], "beeps": [], "windows": []}
    for line in done.stdout.splitlines():
        kind, *cells = line.split()
        if kind == "E":
            found["edges"].append((int(cells[0]), int(cells[1])))
        elif kind == "B":
            found["beeps"].append({
                "start": int(cells[0]), "end": int(cells[1]),
                "hz": float(cells[2]), "carrier_hz": float(cells[3]),
                "bursts": int(cells[4]), "periods": int(cells[5]),
                "pass_ms": int(cells[6])})
        elif kind == "W":
            found["windows"].append((int(cells[0]), int(cells[1]),
                                     float(cells[2]), int(cells[3]),
                                     int(cells[4])))
        else:
            (found["taken"], found["kept"], found["reported"],
             found["rejected"]) = (int(c) for c in cells)
    return found


def counted(beeps: list) -> list:
    """Sort beeps into groups by the silence between them.

    Args:
        beeps: The detector's beeps.

    Returns:
        (count, first rise ns, last edge ns) per group: a silence of
        GROUP_GAP_MS ends a group, and a beep of at least twice the short
        beep's length counts 5.
    """
    groups = []
    for beep in beeps:
        start, end = (beep["start"] * 1e9 / TICK_HZ,
                      beep["end"] * 1e9 / TICK_HZ)
        worth = 5 if end - start >= 2 * SHORT[0] * 1e6 else 1
        if groups and start - groups[-1][2] < GROUP_GAP_MS * 1e6:
            groups[-1] = (groups[-1][0] + worth, groups[-1][1], end)
        else:
            groups.append((worth, start, end))
    return groups


def circuit() -> dict:
    """Give the drawing of the tap.

    Returns:
        Its size, parts, the points labels attach to and the paths a
        current is drawn along, in the drawing's own units.
    """
    line, gnd = 200, 420
    return {
        "size": [1000, 500],
        "items": [
            {"id": "esc", "t": "box", "r": [20, 60, 250, 400],
             "text": "ESC · one motor lead"},
            {"id": "mcu", "t": "box", "r": [720, 60, 260, 400],
             "text": "IOmcu · GPIO 22"},
            {"id": "vbat", "t": "rail", "at": [140, 110], "text": "supply"},
            {"id": "sw", "t": "sw", "a": [140, 110], "b": [140, line],
             "text": "drive"},
            {"id": "lead", "t": "dot", "at": [140, line]},
            {"id": "w1", "t": "wire", "p": [[140, line], [330, line]],
             "text": "motor lead", "at": [250, line + 34]},
            {"id": "rs", "t": "res", "a": [330, line], "b": [500, line],
             "text": "4.7 kΩ", "label": [415, line - 30]},
            {"id": "w2", "t": "wire", "p": [[500, line], [850, line]]},
            {"id": "pin", "t": "dot", "at": [640, line]},
            {"id": "dz", "t": "zener", "a": [560, gnd], "b": [560, line],
             "text": "3.0 V"},
            {"id": "rpd", "t": "res", "a": [640, line], "b": [640, gnd],
             "text": "8.2 kΩ"},
            {"id": "in", "t": "buf", "a": [850, line], "b": [850, gnd - 60],
             "text": "input", "side": "r"},
            {"id": "gnd", "t": "wire", "p": [[560, gnd], [640, gnd]]},
            {"id": "g", "t": "gnd", "at": [600, gnd]},
        ],
        "nodes": {"lead": [140, line], "sw": [140, 155], "rs": [415, line],
                  "dz": [560, 310], "rpd": [640, 310], "pin": [640, line],
                  "in": [850, 290], "wire": [250, line]},
        "paths": {
            "drive": [[140, 110], [140, line], [640, line], [640, gnd]],
            "clamp": [[330, line], [560, line], [560, gnd]],
        },
    }


def figure() -> dict:
    """Simulate the tap and run the detector.

    Returns:
        The figure the player's scope scenes draw.
    """
    m = MODEL
    series = {}
    # One group of 3 short beeps, as item 3 of the menu sounds it.
    lead_in = 50e6
    beeps, group_end = group(3, lead_in)
    pulses = [p for at, length, tone in beeps
              for p in beep_pulses(at, length, tone)]
    # ngspice: the first 3 tone periods of the first beep, at 7.6V and 20V.
    span = 3e9 / SHORT[1] + 0.25e6
    near = [(rise - lead_in + 0.1e6, fall - lead_in + 0.1e6)
            for rise, fall in pulses if fall - lead_in < span]
    for name, supply in (("", m["supply"]), ("-20v", 20.0)):
        lead, pin = simulate(near, span, supply)
        series["lead" + name] = video_scope._series(lead, tol=0.02)
        series["pin" + name] = video_scope._series(pin, tol=0.008)
        if not name:
            crossed = crossings(pin, m["vth"])
    series["level"] = video_scope._square(
        [(0.0, 0)] + [(at, level) for at, level in crossed], span)
    series["rises"] = {"unit": "", "x": [round(at, 1) for at, level
                                         in crossed if level],
                       "y": [1] * sum(level for _, level in crossed)}
    series["falls"] = {"unit": "", "x": [round(at, 1) for at, level
                                         in crossed if not level],
                       "y": [0] * sum(1 - level for _, level in crossed)}
    # The whole group through hold-off and detector.  The pin follows the
    # lead by the delay the simulation gives at the threshold.
    delay = crossed[0][0] - near[0][0]
    edges = sorted([(rise + delay, 1) for rise, _ in pulses]
                   + [(fall + delay, 0) for _, fall in pulses])
    end = group_end + 1.2e9
    found = detect(edges, end)
    tick_ns = 1e9 / TICK_HZ
    series["group-level"] = video_scope._square(
        [(0.0, 0)] + [(tick * tick_ns, level)
                      for tick, level in found["edges"]], end)
    marks = [(0.0, 0)]
    for beep in found["beeps"]:
        marks += [(beep["start"] * tick_ns, 1), (beep["end"] * tick_ns, 0)]
    series["group-beeps"] = video_scope._square(marks, end)
    held = [(index * 8e6 + 8e6, hz / 1000.0)
            for index, present, hz, _, _ in found["windows"] if present]
    series["windows"] = {"unit": "kHz", "x": [x for x, _ in held],
                         "y": [round(y, 4) for _, y in held]}
    # Burst starts of the first beep, from the kept edges: a rise after a
    # low of 1.25 carrier periods or more.
    carrier = 1e9 / m["carrier_hz"]
    starts, last_fall = [], None
    for tick, level in found["edges"]:
        at = tick * tick_ns
        if level and (last_fall is None or at - last_fall >= 1.25 * carrier):
            starts.append(at)
        if not level:
            last_fall = at
    series["burst-starts"] = {
        "unit": "", "x": [round(at - lead_in + 0.1e6, 1) for at in starts
                          if at - lead_in < span],
        "y": [1] * sum(1 for at in starts if at - lead_in < span)}
    # A menu pass as the run hears it: items 1 to 4, then item 3 again
    # counted and selected.
    plan, at = [], 0.5e9
    for count in (1, 2, 3, 4):
        made, stop = group(count, at)
        plan += made
        at = stop + GROUP_MS * 1e6
    menu = [(0.0, 0)]
    for start, length, _ in plan:
        menu += [(start, 1), (start + length * 1e6, 0)]
    series["menu"] = video_scope._square(menu, at)
    groups = counted(found["beeps"])
    return {
        "name": "phase-tap",
        "model": m,
        "measured": {"short_ms": SHORT[0], "short_hz": SHORT[1],
                     "long_ms": LONG[0], "long_hz": LONG[1],
                     "gap_ms": GAP_MS, "group_ms": GROUP_MS},
        "timing": {"tick_hz": TICK_HZ, "hold_ns": HOLD_NS,
                   "group_gap_ms": GROUP_GAP_MS, "defaults": DEFAULTS,
                   "lead_in_ns": lead_in, "span_ns": span,
                   "group_end_ns": group_end},
        "detector": {
            "taken": found["taken"], "kept": found["kept"],
            "reported": found["reported"], "rejected": found["rejected"],
            "beeps": [{"start_ms": round(b["start"] * tick_ns / 1e6, 3),
                       "len_ms": round((b["end"] - b["start"]) * tick_ns
                                       / 1e6, 1),
                       "hz": round(b["hz"], 1),
                       "carrier_hz": round(b["carrier_hz"]),
                       "bursts": b["bursts"], "periods": b["periods"],
                       "pass_ms": b["pass_ms"]} for b in found["beeps"]],
            "groups": [{"count": count, "start_ms": round(a / 1e6, 1),
                        "end_ms": round(b / 1e6, 1)}
                       for count, a, b in groups],
            "first_ticks": found["edges"][:8],
        },
        "series": series,
        "circuits": {"tap": circuit()},
    }


def faults(drawn: dict) -> list:
    """Compare the detector's beeps with the beeps the model sounded.

    Args:
        drawn: What figure() returned.

    Returns:
        A message per difference: the number of beeps, a length more than
        1ms or a pitch more than 1% from the measured ones, a group that
        does not count 3.
    """
    beeps, wrong = drawn["detector"]["beeps"], []
    if len(beeps) != 3:
        wrong.append(f"{len(beeps)} beeps reported, 3 sounded")
    for n, beep in enumerate(beeps):
        if abs(beep["len_ms"] - SHORT[0]) > 1.0:
            wrong.append(f"beep {n + 1}: {beep['len_ms']}ms, "
                         f"sounded {SHORT[0]}ms")
        if abs(beep["hz"] - SHORT[1]) > SHORT[1] / 100:
            wrong.append(f"beep {n + 1}: {beep['hz']}Hz, "
                         f"sounded {SHORT[1]:g}Hz")
    counts = [g["count"] for g in drawn["detector"]["groups"]]
    if counts != [3]:
        wrong.append(f"groups count {counts}, sounded [3]")
    return wrong


def main() -> int:
    """Write the figure.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-o", "--out", type=pathlib.Path,
                    default=video_scope.PLOTS / "phase-tap.json")
    args = ap.parse_args()
    for tool in ("ngspice", "cc"):
        if shutil.which(tool) is None:
            sys.exit(f"{tool} is not installed")
    drawn = figure()
    wrong = faults(drawn)
    if wrong:
        sys.exit("the detector does not report what the model sounded:\n  "
                 + "\n  ".join(wrong))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(drawn, separators=(",", ":")) + "\n",
                        encoding="utf-8")
    sizes = {name: len(trace["x"]) for name, trace in drawn["series"].items()}
    print(f"{args.out}: {args.out.stat().st_size} bytes, points {sizes}")
    print(json.dumps(drawn["detector"], indent=1)[:1500])
    return 0


if __name__ == "__main__":
    sys.exit(main())
