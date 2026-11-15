#!/usr/bin/env python3
"""Render the display node's screen on the host and save it as PNG images.

The screen is drawn by firmware/node/main/node_view.c, the code the
display node's firmware runs, from the fixed models in
test/host/render_node.c.  A PNG (Portable Network Graphics) image is
written for each scene, 320 x 240 as the node's frame holds it: English
(the node without a link) into docs/img/node-<scene>.png, German (linked
to a panel set to German) into docs/img/de/node-<scene>.png.

    python3 tools/render_node.py                 # every scene, both languages
    python3 tools/render_node.py hold -o /tmp/h.png --lang de
    python3 tools/render_node.py --check         # the committed images match

--check compares pixels, not encoded bytes, and fails on any difference.
That every string fits where it is drawn is test_node_view's, in the host
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
    "test/host/render_node.c",
    "firmware/node/main/node_view.c",
    *sorted(str(p.relative_to(REPO))
            for p in (REPO / "shared" / "gfx").glob("*.c")),
]

INCLUDES = [
    "firmware/node/main",
    "shared/gfx/include",
    "protocols/rcnode/include",
]

# The scenes, in render_node.c's order: name -> what it shows.
SCENES = {
    "values": "a run's values with their highest and lowest",
    "no-sensor": "no window from the INA228",
    "hold": "a hold of 1.2s: the bar at 60%, the reset of all ahead",
}

LANGS = {"en": ".", "de": "de"}


def build(tmp: pathlib.Path) -> pathlib.Path:
    """Build the screen's renderer.

    Args:
        tmp: The folder the program goes into.

    Returns:
        The program's path.
    """
    exe = tmp / "render_node"
    cmd = ["cc", "-O2", "-g", *STRICT, *FP_FLAGS, "-o", str(exe)]
    cmd += [str(REPO / s) for s in SOURCES]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    cmd += ["-lm"]
    subprocess.run(cmd, check=True)
    return exe


def render(exe: pathlib.Path, tmp: pathlib.Path, name: str, lang: str):
    """Render one scene.

    Args:
        exe: The renderer.
        tmp: A folder for the PPM (portable pixmap) file.
        name: The scene, a key of SCENES.
        lang: "en" or "de".

    Returns:
        The scene as an RGB (red, green, blue) image.
    """
    from PIL import Image
    ppm = tmp / f"{name}-{lang}.ppm"
    subprocess.run([str(exe), str(ppm), name, lang], check=True)
    return Image.open(ppm).convert("RGB")


def compare(out: pathlib.Path, shown: str, image) -> bool:
    """Compare a render with its committed image.

    Args:
        out: The committed image.
        shown: Its name in the messages.
        image: The render.

    Returns:
        True when they match.
    """
    from PIL import Image
    if not out.exists():
        print(f"{out} does not exist; run tools/render_node.py",
              file=sys.stderr)
        return False
    committed = Image.open(out).convert("RGB")
    if committed.size != image.size:
        print(f"{shown} is {committed.size}, the render is {image.size}",
              file=sys.stderr)
        return False
    a = committed.tobytes()
    b = image.tobytes()
    if a != b:
        diff = sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])
        print(f"{shown} is out of date: {diff:,} pixels differ",
              file=sys.stderr)
        return False
    print(f"{shown} matches")
    return True


def main() -> int:
    """Render the scenes, or check them with --check.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("scenes", nargs="*",
                    help=f"subset of {', '.join(SCENES)} (default: all)")
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="write a single scene here instead of docs/img/")
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
        from PIL import Image  # noqa: F401
    except ImportError:
        sys.exit("Pillow is required: pip install pillow")

    wanted = args.scenes or list(SCENES)
    for name in wanted:
        if name not in SCENES:
            sys.exit(f"unknown scene {name!r}; expected one of "
                     f"{', '.join(SCENES)}")
    langs = list(LANGS) if args.lang == "all" else [args.lang]
    if args.output and (len(wanted) != 1 or len(langs) != 1):
        sys.exit("--output takes exactly one scene and one --lang")

    failed = False
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp)
        for lang in langs:
            for name in wanted:
                out = args.output or (args.dir / LANGS[lang]
                                      / f"node-{name}.png")
                shown = f"{LANGS[lang]}/{out.name}".lstrip("./")
                image = render(exe, tmp, name, lang)
                if args.check:
                    failed |= not compare(out, shown, image)
                    continue
                out.parent.mkdir(parents=True, exist_ok=True)
                image.save(out)
                print(f"wrote {out}")
    if failed:
        print("\nRun tools/render_node.py and review the new images.",
              file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
