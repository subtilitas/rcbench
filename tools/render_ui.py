#!/usr/bin/env python3
"""Render the tester screen on the host and save it as a PNG.

The UI code has no ESP-IDF dependency, so the host build links the same
rasteriser, the same fonts and the same layout code the panel runs.  What
comes out is what the display shows -- which is how this screen was designed
without a board attached.

    python3 tools/render_ui.py                   # docs/img/screen.png
    python3 tools/render_ui.py --seconds 8 --disarmed -o /tmp/idle.png
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
    "components/gfx/gfx.c",
    "components/gfx/gfx_font8x16.c",
    "components/gfx/gfx_font16x28.c",
    "components/gfx/gfx_font_num24x30.c",
    "components/settings/settings.c",
    "components/ui/ui_theme.c",
    "components/ui/ui_widgets.c",
    "components/ui/ui_icons.c",
    "components/ui/ui_router.c",
    "components/ui/splash_screen.c",
    "components/ui/overview_screen.c",
    "components/ui/stub_screen.c",
    "components/ui/settings_screen.c",
    "components/ui/tester_ui.c",
    "components/ui/log_viewer_screen.c",
    "components/ui/servo_prog_screen.c",
    "components/logfile/log_numbers.c",
    "components/logfile/log_csv.c",
    "components/logfile/log_fields.c",
    "main/telemetry_sim.c",
]

# Every screen gets a committed screenshot; CI checks them all.
SCREENS = {
    "splash":    ("splash.png",     26.0, 1),
    "overview":  ("overview.png",   26.0, 1),
    "tester":    ("screen.png",     26.0, 1),
    # The same screen disarmed.  It was committed and referenced by nothing,
    # which meant --check could never notice it going stale.
    "tester-idle": ("screen-idle.png", 26.0, 0),
    "servo":     ("servo.png",      26.0, 1),
    "logviewer": ("log-viewer.png", 26.0, 1),
    "logimport": ("log-import.png", 26.0, 1),
    "logplot":   ("log-plot.png",   26.0, 1),
    "logger":    ("data-logger.png", 26.0, 1),
    "escprog":   ("esc-prog.png",   26.0, 1),
    "servoprog": ("servo-prog.png", 26.0, 1),
    # The Prog-Box tab, which is the only one that shows all twelve.
    "servoprog-box": ("servo-prog-box.png", 26.0, 1),
    "settings":  ("settings.png",   26.0, 1),
    # The light theme only differs by palette, so one screen proves it.
    "settings-light": ("settings-light.png", 26.0, 1),
}
INCLUDES = [
    "components/gfx/include",
    "components/touch/include",
    "components/settings/include",
    "components/ui/include",
    "components/logfile/include",
    "main",
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


def render_one(exe, tmp, screen, seconds, armed):
    ppm = tmp / f"{screen}.ppm"
    subprocess.run([str(exe), str(ppm), screen, str(seconds), str(armed)],
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
    ap.add_argument("--seconds", type=float, default=None,
                    help="override how much simulated run fills the trace")
    ap.add_argument("--disarmed", action="store_true")
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
            filename, seconds, armed = SCREENS[name]
            if args.seconds is not None:
                seconds = args.seconds
            if args.disarmed:
                armed = 0

            out = args.output or (args.dir / filename)
            rendered = render_one(exe, tmp, name, seconds, armed)

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
