#!/usr/bin/env python3
"""Render the M5Stack Dial's faces on the host and save them as PNG images.

The faces are drawn by shared/dial, the code the Dial's firmware runs,
from the fixed models in test/host/dial_models.h.  A PNG (Portable
Network Graphics) image is written for each face, 240 x 240 as the Dial's
frame holds it, English into docs/img/dial-<name>.png and German into
docs/img/de/dial-<name>.png.

    python3 tools/render_dial.py                 # every face, both languages
    python3 tools/render_dial.py motor-armed -o /tmp/m.png --lang de
    python3 tools/render_dial.py --check         # the committed images match

--check compares pixels, not encoded bytes, and fails on any difference.
That every string fits where it is drawn is test_dial_fit's, in the host
suite.

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

# The flags of the libraries in test/host/CMakeLists.txt, with -Werror.
STRICT = ["-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wconversion",
          "-Wno-sign-conversion"]

# No fused multiply-add, as in render_ui.py: the committed images are
# those of separate multiply and add.
FP_FLAGS = ["-ffp-contract=off"]

SOURCES = [
    "test/host/render_dial.c",
    "shared/dial/dial_scene.c",
    "shared/dial/dial_draw.c",
    "shared/dial/dial_text.c",
    "shared/dial/dial_rings.c",
    "shared/ui/ui_theme.c",
    "protocols/dialknob/dialknob.c",
    "protocols/dialknob/dk_face.c",
    "protocols/dialknob/dk_track.c",
    *sorted(str(p.relative_to(REPO))
            for p in (REPO / "shared" / "gfx").glob("*.c")),
]

INCLUDES = [
    "shared/dial/include",
    "shared/gfx/include",
    "shared/ui/include",
    "shared/text/include",
    "protocols/dialknob/include",
    "test/host",
]

# The faces, in dial_models.h's order: name -> what it shows.
FACES = {
    "navigation": "a list on SETUP, the focused row in brackets",
    "value-set": "a setting's value being set",
    "servo-live": "SERVO, ARMED",
    "motor-armed": "MOTOR & ESC, ARMED, the run clock in the pill",
    "motor-safe": "MOTOR & ESC, SAFE, readings at half strength",
    "link-lost": "no panel packet for 500ms: LINK LOST, no EVENT is sent",
    "link-ended": "LINK LOST held for 500ms more: the Dial ended the link",
    "waiting": "connected, no panel packet yet",
    "bind": "no bound panel",
    "other": "a screen the knob does nothing on",
}

LANGS = {"en": ".", "de": "de"}


def build(tmp: pathlib.Path) -> pathlib.Path:
    """Build the face renderer.

    Args:
        tmp: The folder the program goes into.

    Returns:
        The program's path.
    """
    exe = tmp / "render_dial"
    cmd = ["cc", "-O2", "-g", *STRICT, *FP_FLAGS, "-o", str(exe)]
    cmd += [str(REPO / s) for s in SOURCES]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    cmd += ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def render(exe: pathlib.Path, tmp: pathlib.Path, name: str, lang: str):
    """Render one face.

    Args:
        exe: The renderer.
        tmp: A folder for the PPM (portable pixmap) file.
        name: The face, a key of FACES.
        lang: "en" or "de".

    Returns:
        The face as an RGB (red, green, blue) image.
    """
    from PIL import Image
    ppm = tmp / f"{name}-{lang}.ppm"
    subprocess.run([str(exe), str(ppm), name, lang], check=True)
    return Image.open(ppm).convert("RGB")


def main() -> int:
    """Render the faces, or check them with --check.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("faces", nargs="*",
                    help=f"subset of {', '.join(FACES)} (default: all)")
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="write a single face here instead of docs/img/")
    ap.add_argument("--dir", type=pathlib.Path,
                    default=REPO / "docs" / "img")
    ap.add_argument("--lang", choices=sorted(LANGS) + ["all"], default="all",
                    help="language to render (default: all; German images "
                         "go to docs/img/de/)")
    ap.add_argument("--check", action="store_true",
                    help="fail if any render differs from the committed "
                         "image")
    args = ap.parse_args()

    if not shutil.which("cc"):
        sys.exit("no C compiler on PATH")
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required: pip install pillow")

    wanted = args.faces or list(FACES)
    for name in wanted:
        if name not in FACES:
            sys.exit(f"unknown face {name!r}; expected one of "
                     f"{', '.join(FACES)}")
    langs = list(LANGS) if args.lang == "all" else [args.lang]
    if args.output and (len(wanted) != 1 or len(langs) != 1):
        sys.exit("--output takes exactly one face and one --lang")

    failed = False
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp)
        for lang in langs:
            for name in wanted:
                out = args.output or (args.dir / LANGS[lang]
                                      / f"dial-{name}.png")
                shown = f"{LANGS[lang]}/{out.name}".lstrip("./")
                image = render(exe, tmp, name, lang)
                if not args.check:
                    out.parent.mkdir(parents=True, exist_ok=True)
                    image.save(out)
                    print(f"wrote {out}")
                    continue
                if not out.exists():
                    print(f"{out} does not exist; run tools/render_dial.py",
                          file=sys.stderr)
                    failed = True
                    continue
                committed = Image.open(out).convert("RGB")
                if committed.size != image.size:
                    print(f"{shown} is {committed.size}, the render is "
                          f"{image.size}", file=sys.stderr)
                    failed = True
                    continue
                a = committed.tobytes()
                b = image.tobytes()
                if a != b:
                    diff = sum(1 for i in range(0, len(a), 3)
                               if a[i:i + 3] != b[i:i + 3])
                    print(f"{shown} is out of date: {diff:,} pixels differ",
                          file=sys.stderr)
                    failed = True
                else:
                    print(f"{shown} matches")
    if failed:
        print("\nRun tools/render_dial.py and review the new images.",
              file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
