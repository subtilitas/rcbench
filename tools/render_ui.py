#!/usr/bin/env python3
"""Render every screen on the host and save it as a PNG (Portable Network
Graphics) image.

The UI (user interface) code has no ESP-IDF (Espressif Internet-of-Things
Development Framework) dependency, so the host build links the rasteriser,
the fonts and the layout code the panel runs.  The output is what the display
shows.

    python3 tools/render_ui.py                   # every screen into docs/img/
    python3 tools/render_ui.py overview -o /tmp/overview.png --lang en
    python3 tools/render_ui.py --check           # the committed images match
    python3 tools/render_ui.py --fit             # every string fits

Every screen is rendered in English into docs/img/ and in German into
docs/img/de/.  --fit builds the renderer with GFX_TEXT_TRACE, draws every
view in both languages and fails when a string is wider than its box, is cut
at the edge of the area it is drawn in, runs past the shape it is printed
on, overlaps another string, or is painted over by a fill drawn after it.
The English overflows in KNOWN_OVERFLOWS are listed and do not fail; one of
them that no longer overflows fails until it is taken off the list.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent

SOURCES = [
    "test/host/render_screen.c",
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
    "shared/ui/ui_text.c",
    "shared/ui/ui_text_de.c",
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
    "shared/ui/outputs_screen.c",
    "shared/ui/picker_screen.c",
    "shared/ui/busfault_screen.c",
    # The ESC profiles and the stick programmer the PROGRAMMER screen's
    # ESC STICK class lists and runs.
    "shared/esc/esc_profiles_gen.c",
    "shared/esc/esc_json.c",
    "shared/esc/esc_registry.c",
    "shared/esc/esc_stick.c",
    "shared/esc/esc_sim.c",
    # Generated artwork: pure data, so the screenshot is the real photograph.
    "firmware/iomcu/src/art_rp2350_can.c",
    "shared/ui/log_viewer_screen.c",
    "shared/ui/settings_screen.c",
    "shared/settings/settings.c",
    "shared/logfile/log_numbers.c",
    "shared/logfile/log_csv.c",
    "shared/logfile/log_fields.c",
    "shared/logfile/log_name.c",
    "shared/bench/bench_state.c",
    "shared/bench/telemetry_sim.c",
    "shared/bench/supply.c",
    "shared/servo/servo_sim.c",
    "shared/servo/servo_sweep.c",
    "shared/servo/servo_move.c",
    "shared/servo/servo_test.c",
    "shared/servo/servo_report.c",
    "shared/sbus/sbus.c",
    "shared/outputs/outputs.c",
    "shared/outputs/outputs_pages.c",
    "shared/outputs/out_pwm_map.c",
    "shared/outputs/out_bind.c",
    "shared/can/can_selftest.c",
    "shared/link/link_can.c",
]

# Every screen gets a committed screenshot; CI (continuous integration)
# checks them all.
# name -> (committed file, screen the renderer knows, theme)
SCREENS = {
    "splash":     ("splash.png",     "splash",     "dark"),
    "overview":   ("overview.png",   "overview",   "dark"),
    # The same menu in the other theme, committed so a palette change that
    # breaks one theme is caught.
    "overview-light": ("overview-light.png", "overview", "light"),
    "motor":      ("motor.png",      "motor",      "dark"),
    "motor-held": ("motor-held.png", "motor-held", "dark"),
    # The TABLE pane with an INA228 as BENCH's source, the ESC's own
    # telemetry beside it.
    "motor-table": ("motor-table.png", "motor", "dark"),
    "servo":      ("servo.png",      "servo",      "dark"),
    # The INA3221 as the servo rail's meter: its name over the plot and
    # CH1's windows in the CURRENT row, the line and the plot; and the same
    # with the last window clipped, its current in the warning colour.
    "servo-ina":      ("servo-ina.png",      "servo", "dark"),
    "servo-clipped":  ("servo-clipped.png",  "servo", "dark"),
    "servo-settings": ("servo-settings.png", "servo", "dark"),
    "servo-test":     ("servo-test.png",     "servo", "dark"),
    "servo-limits":   ("servo-limits.png",   "servo", "dark"),
    "servo-warning":  ("servo-warning.png",  "servo", "dark"),
    "servo-name":     ("servo-name.png",     "servo", "dark"),
    # The DUT page with the output encoder on, and its angle in the
    # MEASURED row.
    "servo-encoder":  ("servo-encoder.png",  "servo", "dark"),
    "servo-hv":       ("servo-hv.png",       "servo", "dark"),
    "servo-run":      ("servo-run.png",      "servo", "dark"),
    "servo-result":   ("servo-result.png",   "servo", "dark"),
    # A run that read the INA3221's CH1 windows: the meter named in the
    # result box, the line and the plot.
    "servo-result-ina": ("servo-result-ina.png", "servo", "dark"),
    "servo-sweep":    ("servo-sweep.png",    "servo", "dark"),
    "servo-paused":   ("servo-paused.png",   "servo", "dark"),
    "analyser":   ("analyser.png",   "analyser",   "dark"),
    "logs":       ("logs.png",       "logs",       "dark"),
    "analyser-failsafe": ("analyser-failsafe.png", "analyser", "dark"),
    "programmer-protocols": ("programmer-protocols.png", "programmer", "dark"),
    "programmer-idle": ("programmer-idle.png", "programmer", "dark"),
    "programmer-params": ("programmer-params.png", "programmer", "dark"),
    "programmer-dirty": ("programmer-dirty.png", "programmer", "dark"),
    "programmer-am32": ("programmer-am32.png", "programmer", "dark"),
    "programmer-stick": ("programmer-stick.png", "programmer", "dark"),
    "programmer-stick-items": ("programmer-stick-items.png", "programmer",
                               "dark"),
    "programmer-stick-timing": ("programmer-stick-timing.png", "programmer",
                                "dark"),
    "programmer-stick-warning": ("programmer-stick-warning.png",
                                 "programmer", "dark"),
    "programmer-stick-run": ("programmer-stick-run.png", "programmer",
                             "dark"),
    # The run's page with the phase tap enabled and running, its readout
    # under the current line.
    "programmer-stick-tone": ("programmer-stick-tone.png", "programmer",
                              "dark"),
    "programmer-stick-done": ("programmer-stick-done.png", "programmer",
                              "dark"),
    "programmer-stick-aborted": ("programmer-stick-aborted.png",
                                 "programmer", "dark"),
    "programmer-stick-failed": ("programmer-stick-failed.png",
                                "programmer", "dark"),
    "programmer-stick-find": ("programmer-stick-find.png", "programmer",
                              "dark"),
    "programmer-stick-found": ("programmer-stick-found.png", "programmer",
                               "dark"),
    "programmer-stick-hand-list": ("programmer-stick-hand-list.png",
                                   "programmer", "dark"),
    "programmer-stick-hand-info": ("programmer-stick-hand-info.png",
                                   "programmer", "dark"),
    "programmer-stick-hand": ("programmer-stick-hand.png", "programmer",
                              "dark"),
    "programmer-stick-hand-warning": ("programmer-stick-hand-warning.png",
                                      "programmer", "dark"),
    "programmer-stick-hand-prompt": ("programmer-stick-hand-prompt.png",
                                     "programmer", "dark"),
    "programmer-stick-hand-after": ("programmer-stick-hand-after.png",
                                    "programmer", "dark"),
    "programmer-stick-hand-steps": ("programmer-stick-hand-steps.png",
                                    "programmer", "dark"),
    "programmer-stick-hand-end": ("programmer-stick-hand-end.png",
                                  "programmer", "dark"),
    "programmer-stick-hand-locked": ("programmer-stick-hand-locked.png",
                                     "programmer", "dark"),
    "logs-import":("logs-import.png","logs",       "dark"),
    "logs-plot":  ("logs-plot.png",  "logs",       "dark"),
    "logs-delete":("logs-delete.png","logs",       "dark"),
    "setup":      ("setup.png",      "setup",      "dark"),
    "setup-dirty":("setup-dirty.png","setup",      "dark"),
    # INTERFACES with both current monitors enabled, at the top of the list
    # and drawn on to the INA3221's rows and the bus's pins.
    "setup-interfaces": ("setup-interfaces.png", "setup", "dark"),
    "setup-sensors": ("setup-sensors.png", "setup", "dark"),
    # The same rows with the pins the coprocessor has not taken: the mark on
    # Sensor SDA and Sensor SCL, and on the INA3221 that is off for them.
    "setup-unheld": ("setup-unheld.png", "setup", "dark"),
    # The same list past them, to the phase tap's rows, the tap enabled.
    "setup-tap": ("setup-tap.png", "setup", "dark"),
    "outputs":    ("outputs.png",    "outputs",    "dark"),
    "outputs-protocol": ("outputs-protocol.png", "outputs", "dark"),
    "outputs-held": ("outputs-held.png", "outputs", "dark"),
    "outputs-full": ("outputs-full.png", "outputs", "dark"),
    # The longest protocol name, DSHOT600 BIDIR, in the closed list.
    "outputs-bidir": ("outputs-bidir.png", "outputs", "dark"),
    # OFF picked: every bound pin names its protocol, the longest among them.
    "outputs-off": ("outputs-off.png", "outputs", "dark"),
    # The binding last read, after a read that failed.
    "outputs-unread": ("outputs-unread.png", "outputs", "dark"),
    # The same after a read of pages no binding describes, with the key
    # that unbinds every pin.
    "outputs-odd": ("outputs-odd.png", "outputs", "dark"),
    "picker": ("picker.png", "picker", "dark"),
    "picker-drawn": ("picker-drawn.png", "picker", "dark"),
    "busfault": ("busfault.png", "busfault", "dark"),
    "supply": ("supply.png", "supply", "dark"),
    "supply-settings": ("supply-settings.png", "supply", "dark"),
    "supply-keypad": ("supply-keypad.png", "supply", "dark"),
    "supply-confirm": ("supply-confirm.png", "supply", "dark"),
    "busfault-silent": ("busfault-silent.png", "busfault", "dark"),
    "busfault-lost": ("busfault-lost.png", "busfault", "dark"),
    "setup-light":("setup-light.png","setup",      "light"),
    "battery":    ("battery.png",    "battery",    "dark"),
    "balance":    ("balance.png",    "balance",    "dark"),
    "balance-rig": ("balance-rig.png", "balance", "dark"),
    "balance-aircraft": ("balance-aircraft.png", "balance", "dark"),
    "balance-edf": ("balance-edf.png", "balance", "dark"),
    "programmer": ("programmer.png", "programmer", "dark"),
}
INCLUDES = [
    "shared/gfx/include",
    "shared/touch/include",
    "shared/ui/include",
    "shared/bench/include",
    "shared/outputs/include",
    "shared/link/include",
    "shared/can/include",
    "shared/logfile/include",
    "shared/settings/include",
    "shared/servo/include",
    "shared/esc/include",
    "shared/sbus/include",
]


def build(tmp: pathlib.Path, trace: bool = False) -> pathlib.Path:
    exe = tmp / ("render_trace" if trace else "render_screen")
    cmd = ["cc", "-O2", "-g", "-Wall", "-Wextra", "-o", str(exe)]
    if trace:
        cmd += ["-DGFX_TEXT_TRACE"]
    cmd += [str(REPO / s) for s in SOURCES]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    cmd += ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def render_one(exe, tmp, name, screen, theme, lang="en", trace=None):
    ppm = tmp / f"{name}-{lang}.ppm"
    # The view name as well as the screen: three log goldens are all the
    # same screen in different states, and the renderer needs to know
    # which state to drive it into.
    cmd = [str(exe), str(ppm), screen, theme, name, lang]
    if trace is not None:
        cmd.append(str(trace))
    subprocess.run(cmd, check=True)
    from PIL import Image
    return Image.open(ppm).convert("RGB")


# ------------------------------------------------------------- the fit check

# A string's cell box carries a little empty space above and below the ink,
# so two lines that touch are not counted as overlapping until they share
# more than this many rows.
SLACK_Y = 2

# A filled shape smaller than this on either side is a mark or a rule, not
# something a label sits inside.
MIN_SHAPE = 8

# Views that open a drop-down over the screen: the list covers the labels
# beside it by design, so a fill drawn over a string is not a finding there.
OVERLAY_VIEWS = {"outputs-protocol"}

# English overflows the layout has today: (view, string, finding), word for
# word as --fit prints them.  Each is a defect of the screen, listed here so
# the check holds every other string; none is a decision that the overflow
# is wanted.  An entry that --fit no longer finds fails, so the list cannot
# outlive its defects.
#
# SETUP shows 36 cells of a help line and two English help lines are 38 and
# 55 characters: Capacity's and Rated kV's.  The log import's column range
# is 18 characters in a 140 px box.
_CUT = "is cut at the edge of its area (252 to %d, area 236 to 546)"
KNOWN_OVERFLOWS = {
    (view, text, _CUT % right)
    for view in ("setup", "setup-dirty", "setup-light")
    for text, right in (
        ("Used for the remaining-charge estimate", 556),
        ("Used when the ESC reports none; 0 shows the field empty", 692),
    )
} | {
    ("logs-import", "2392.0..14639.0rpm", "is 144 px wide in a 140 px box"),
}


class Rect:
    def __init__(self, x, y, w, h):
        self.x, self.y, self.w, self.h = x, y, w, h

    @property
    def r(self):
        return self.x + self.w

    @property
    def b(self):
        return self.y + self.h

    def inter(self, o):
        x0, y0 = max(self.x, o.x), max(self.y, o.y)
        x1, y1 = min(self.r, o.r), min(self.b, o.b)
        if x1 <= x0 or y1 <= y0:
            return None
        return Rect(x0, y0, x1 - x0, y1 - y0)

    def covers(self, o):
        return (self.x <= o.x and self.y <= o.y and self.r >= o.r
                and self.b >= o.b)

    def holds(self, px, py):
        return self.x <= px < self.r and self.y <= py < self.b


def read_trace(path: pathlib.Path):
    """The traced frame: its strings and shapes in drawing order, and the
    table IDs looked up while the view was set up and drawn."""
    items, used = [], set()
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("U "):
            used.add(line[2:].strip())
        elif line == "F":
            items = []
        elif line.startswith("B "):
            v = [int(t) for t in line.split()[1:9]]
            items.append(("B", Rect(*v[0:4]), Rect(*v[4:8])))
        elif line.startswith("T "):
            parts = line.split(" ", 13)
            v = [int(t) for t in parts[1:13]]
            box = Rect(*v[8:12]) if v[10] >= 0 else None
            items.append(("T", Rect(*v[0:4]), Rect(*v[4:8]), box,
                          parts[13]))
    return items, used


def ink(t, s):
    """The part of a string's box its characters cover: the box less the
    cells of its leading and trailing spaces, which a padded label carries."""
    n = len(s)
    if n == 0 or not s.strip():
        return None
    cell = t.w / n
    lead = n - len(s.lstrip(" "))
    trail = n - len(s.rstrip(" "))
    x0 = t.x + round(cell * lead)
    x1 = t.r - round(cell * trail)
    return Rect(x0, t.y, x1 - x0, t.h)


def painted_over(items, i, vis, s):
    """The first fill drawn after string i that covers part of its ink, as
    a Rect, unless the same string is drawn again in the same place after
    that fill."""
    t = items[i][1]
    k = ink(t, s)
    if k is None:
        return None
    k = k.inter(vis)
    if k is None or k.h <= 2 * SLACK_Y:
        return None
    core = Rect(k.x, k.y + SLACK_Y, k.w, k.h - 2 * SLACK_Y)
    for j in range(i + 1, len(items)):
        b = items[j]
        if b[0] != "B":
            continue
        hit = core.inter(b[1])
        if hit is None or b[2].inter(hit) is None:
            continue
        redrawn = any(items[m][0] == "T" and items[m][4] == s
                      and items[m][1].x == t.x and items[m][1].y == t.y
                      for m in range(j + 1, len(items)))
        if not redrawn:
            return b[2].inter(hit)
    return None


def fit_problems(items, overlay=False):
    """What overflows, as (string, problem) pairs.  Width only: a language
    changes how long a string is, not how tall.  With @p overlay, a string
    a later fill covers is not a finding."""
    problems = []
    drawn = []          # (index, visible rect, string)
    for i, it in enumerate(items):
        if it[0] != "T":
            continue
        _, t, clip, box, s = it
        if t.y + t.h <= clip.y or t.y >= clip.b:
            continue    # wholly above or below its clip: not drawn at all
        if box is not None and t.w > box.w:
            problems.append((s, "is %d px wide in a %d px box"
                             % (t.w, box.w)))
        elif t.x < clip.x or t.r > clip.r:
            problems.append((s, "is cut at the edge of its area "
                             "(%d to %d, area %d to %d)"
                             % (t.x, t.r, clip.x, clip.r)))
        # The shape it is printed on: the last one drawn under its start.
        px, py = t.x + min(4, max(t.w // 2, 0)), t.y + t.h // 2
        for j in range(i - 1, -1, -1):
            b = items[j]
            if (b[0] == "B" and b[1].w >= MIN_SHAPE and b[1].h >= MIN_SHAPE
                    and b[1].holds(px, py)):
                if t.r > b[1].r and b[1].r <= clip.r:
                    problems.append((s, "runs %d px past the shape it sits "
                                     "on" % (t.r - b[1].r)))
                break
        vis = t.inter(clip)
        if vis is None:
            continue
        over = None if overlay else painted_over(items, i, vis, s)
        if over is not None:
            problems.append((s, "is painted over from x=%d to %d by a fill "
                             "drawn after it" % (over.x, over.r)))
        core = Rect(vis.x, vis.y + SLACK_Y, vis.w, max(vis.h - 2 * SLACK_Y, 0))
        for k, other, os_ in drawn:
            hit = core.inter(other)
            if hit is None:
                continue
            # Painted over between the two is not drawn under the second.
            hidden = any(items[j][0] == "B" and items[j][1].covers(hit)
                         for j in range(k + 1, i))
            if not hidden:
                problems.append((s, "overlaps %r" % os_))
        drawn.append((i, Rect(vis.x, vis.y + SLACK_Y, vis.w,
                              max(vis.h - 2 * SLACK_Y, 0)), s))
    return problems


def judge(findings, known, views):
    """Tag every finding and name the stale waivers.

    @p findings is [(lang, view, string, why)].  A finding fails in either
    language unless @p known holds it word for word: a German finding that
    equals a known English one is the same string in the same place -- a
    number from the data, say -- and is the layout's, not the
    translation's.  Returns ([(tag, lang, view, string, why)], [stale])
    where tag is "FAIL" or "known" and stale is every entry of @p known for
    a view in @p views that English did not produce."""
    english = {(v, s, w) for lang, v, s, w in findings if lang == "en"}
    judged = [("known" if (view, s, why) in known else "FAIL",
               lang, view, s, why) for lang, view, s, why in findings]
    stale = sorted(k for k in known if k[0] in views and k not in english)
    return judged, stale


def fit(views, langs) -> int:
    """Render every view in every language with the trace on and report
    every string that overflows.  An overflow fails in either language,
    except the English ones in KNOWN_OVERFLOWS and their German copies.  A
    table string with no declared width has to be drawn by some view, or
    nothing measures it."""
    failed = False
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp, trace=True)
        ids = {}
        out = subprocess.run([str(exe), "--ids"], check=True,
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            _, name, cells = line.split()
            ids[name] = int(cells)
        seen = {lang: set() for lang in langs}
        findings = []
        for lang in sorted(langs, key=lambda x: x != "en"):
            for name in views:
                filename, screen, theme = SCREENS[name]
                trace = tmp / f"{name}-{lang}.trace"
                render_one(exe, tmp, name, screen, theme, lang, trace)
                items, used = read_trace(trace)
                seen[lang] |= used
                for s, why in fit_problems(items, name in OVERLAY_VIEWS):
                    findings.append((lang, name, s, why))
        judged, stale = judge(findings, KNOWN_OVERFLOWS, set(views))
        for tag, lang, name, s, why in judged:
            print(f"{tag} {lang} {name}: {s!r} {why}")
            failed = failed or tag == "FAIL"
        if "en" in langs:
            for name, s, why in stale:
                print(f"FAIL en {name}: {s!r} is in KNOWN_OVERFLOWS and "
                      f"no longer {why}; take it off the list")
                failed = True
        for lang in langs:
            if lang == "en":
                continue
            unmeasured = sorted(n for n, c in ids.items()
                                if c == 0 and n not in seen[lang])
            for n in unmeasured:
                print(f"FAIL {lang}: {n} has no declared width and no view "
                      "draws it")
                failed = True
    if not failed:
        known = sum(1 for tag, *_ in judged if tag == "known")
        print("every string fits" if not known else
              f"every string fits, except the {known} known overflows above")
    return 1 if failed else 0


# The languages a golden is committed in, and where each set lives.
LANGS = {"en": ".", "de": "de"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("screens", nargs="*",
                    help=f"subset of {', '.join(SCREENS)} (default: all)")
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="write a single screen here instead of docs/img/")
    ap.add_argument("--dir", type=pathlib.Path,
                    default=REPO / "docs" / "img")
    ap.add_argument("--lang", choices=sorted(LANGS) + ["all"], default="all",
                    help="the language to render (default: every one; "
                         "German goes to docs/img/de/)")
    ap.add_argument("--check", action="store_true",
                    help="fail if any render differs from the committed image")
    ap.add_argument("--fit", action="store_true",
                    help="fail if a string overflows where it is drawn, in "
                         "either language")
    args = ap.parse_args()

    if not shutil.which("cc"):
        sys.exit("no C compiler on PATH")

    wanted = args.screens or list(SCREENS)
    for name in wanted:
        if name not in SCREENS:
            sys.exit(f"unknown screen {name!r}; expected one of "
                     f"{', '.join(SCREENS)}")
    langs = list(LANGS) if args.lang == "all" else [args.lang]
    if args.output and (len(wanted) != 1 or len(langs) != 1):
        sys.exit("--output takes exactly one screen and one --lang")

    if args.fit:
        return fit(wanted, langs)

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        sys.exit("Pillow is required: pip install pillow")

    failed = False
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp)

        for lang in langs:
            for name in wanted:
                filename, screen, theme = SCREENS[name]

                out = args.output or (args.dir / LANGS[lang] / filename)
                rendered = render_one(exe, tmp, name, screen, theme, lang)
                shown = f"{LANGS[lang]}/{out.name}".lstrip("./")

                if args.check:
                    # The unit tests cover what a screen decides; the golden
                    # image covers what it looks like.  Compare pixels rather
                    # than encoded bytes: PNG encoders differ between
                    # versions.
                    if not out.exists():
                        print(f"{out} does not exist; run tools/render_ui.py",
                              file=sys.stderr)
                        failed = True
                        continue
                    committed = Image.open(out).convert("RGB")
                    if committed.size != rendered.size:
                        print(f"{out} is {committed.size}, "
                              f"render is {rendered.size}",
                              file=sys.stderr)
                        failed = True
                        continue
                    a = committed.tobytes()
                    b = rendered.tobytes()
                    if a != b:
                        diff = sum(1 for i in range(0, len(a), 3)
                                   if a[i:i + 3] != b[i:i + 3])
                        print(f"{shown} is out of date: {diff:,} pixels "
                              "differ", file=sys.stderr)
                        failed = True
                    else:
                        print(f"{shown} matches")
                else:
                    out.parent.mkdir(parents=True, exist_ok=True)
                    rendered.save(out)
                    print(f"wrote {out}")

    if failed:
        print("\nRun tools/render_ui.py and review the new images.",
              file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
