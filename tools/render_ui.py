#!/usr/bin/env python3
"""Render the tester screen on the host and save it as a PNG.

The UI code has no ESP-IDF dependency, so the host build links the same
rasteriser, the same fonts and the same layout code the panel runs.  What
comes out is what the display shows -- which is how this screen was designed
without a board attached.

    python3 tools/render_ui.py                   # docs/img/screen.png
    python3 tools/render_ui.py overview -o /tmp/overview.png
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
    "shared/settings/settings.c",
    "shared/logfile/log_numbers.c",
    "shared/logfile/log_csv.c",
    "shared/logfile/log_fields.c",
    "shared/bench/bench_state.c",
    "shared/bench/telemetry_sim.c",
    "shared/servo/servo_sim.c",
    "shared/sbus/sbus.c",
    "shared/bench/throttle.c",
]

# Every screen gets a committed screenshot; CI checks them all.
# name -> (committed file, screen the renderer knows, theme)
SCREENS = {
    "splash":     ("splash.png",     "splash",     "dark"),
    "overview":   ("overview.png",   "overview",   "dark"),
    # The same menu in the other theme.  Committed because a palette change
    # that only breaks one theme is the kind that ships.
    "overview-light": ("overview-light.png", "overview", "light"),
    "motor":      ("motor.png",      "motor",      "dark"),
    "servo":      ("servo.png",      "servo",      "dark"),
    "analyser":   ("analyser.png",   "analyser",   "dark"),
    "logs":       ("logs.png",       "logs",       "dark"),
    "analyser-failsafe": ("analyser-failsafe.png", "analyser", "dark"),
    "programmer-protocols": ("programmer-protocols.png", "programmer", "dark"),
    "programmer-idle": ("programmer-idle.png", "programmer", "dark"),
    "programmer-params": ("programmer-params.png", "programmer", "dark"),
    "programmer-dirty": ("programmer-dirty.png", "programmer", "dark"),
    "programmer-am32": ("programmer-am32.png", "programmer", "dark"),
    "logs-import":("logs-import.png","logs",       "dark"),
    "logs-plot":  ("logs-plot.png",  "logs",       "dark"),
    "setup":      ("setup.png",      "setup",      "dark"),
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
    "shared/link/include",
    "shared/logfile/include",
    "shared/settings/include",
    "shared/servo/include",
    "shared/sbus/include",
]


def build(tmp: pathlib.Path) -> pathlib.Path:
    exe = tmp / "render_screen"
    cmd = ["cc", "-O2", "-g", "-Wall", "-Wextra", "-o", str(exe)]
    cmd += [str(REPO / s) for s in SOURCES]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    cmd += ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def render_one(exe, tmp, name, screen, theme):
    ppm = tmp / f"{name}.ppm"
    # The view name as well as the screen: three log goldens are all the
    # same screen in different states, and the renderer needs to know
    # which state to drive it into.
    subprocess.run([str(exe), str(ppm), screen, theme, name],
                   check=True)
    from PIL import Image
    return Image.open(ppm).convert("RGB")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("screens", nargs="*",
                    help=f"subset of {', '.join(SCREENS)} (default: all)")
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="write a single screen here instead of docs/img/")
    ap.add_argument("--dir", type=pathlib.Path,
                    default=REPO / "docs" / "img")
    ap.add_argument("--check", action="store_true",
                    help="fail if any render differs from the committed image")
    args = ap.parse_args()

    if not shutil.which("cc"):
        sys.exit("no C compiler on PATH")

    wanted = args.screens or list(SCREENS)
    for name in wanted:
        if name not in SCREENS:
            sys.exit(f"unknown screen {name!r}; expected one of "
                     f"{', '.join(SCREENS)}")
    if args.output and len(wanted) != 1:
        sys.exit("--output takes exactly one screen")

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        sys.exit("Pillow is required: pip install pillow")

    failed = False
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp)

        for name in wanted:
            filename, screen, theme = SCREENS[name]

            out = args.output or (args.dir / filename)
            rendered = render_one(exe, tmp, name, screen, theme)

            if args.check:
                # A golden image is the right regression test for a renderer:
                # the unit tests cover what a screen decides, this covers what
                # it looks like.  Compare pixels rather than encoded bytes --
                # PNG encoders differ between versions, and that is not a UI
                # change.
                if not out.exists():
                    print(f"{out} does not exist; run tools/render_ui.py",
                          file=sys.stderr)
                    failed = True
                    continue
                committed = Image.open(out).convert("RGB")
                if committed.size != rendered.size:
                    print(f"{out} is {committed.size}, render is {rendered.size}",
                          file=sys.stderr)
                    failed = True
                    continue
                a = committed.tobytes()
                b = rendered.tobytes()
                if a != b:
                    diff = sum(1 for i in range(0, len(a), 3)
                               if a[i:i + 3] != b[i:i + 3])
                    print(f"{out.name} is out of date: {diff:,} pixels differ",
                          file=sys.stderr)
                    failed = True
                else:
                    print(f"{out.name} matches")
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
