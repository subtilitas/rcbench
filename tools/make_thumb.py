#!/usr/bin/env python3
"""Write a video's thumbnail: one frame of the player and 1 to 3 lines.

A storyboard's ``thumb`` block names the frame and the text:

    "thumb": {"at": 95, "lines": ["AUTO", "*RUDDER"], "crop": [430, 90, 1400],
              "side": "l", "accent": "#ffb000", "size": 150}

- ``at``: the time in the video, in s, of the frame.
- ``lines``: 1 to 3 lines of text. A leading ``*`` gives a line the
  accent colour; the others are white. Each letter carries a dark rim.
- ``crop``: ``[x, y, w]``, the 16:9 part of the 1920 x 1080 frame that
  fills the thumbnail. Default: the whole frame.
- ``side``: ``l`` or ``r``, the side the text and its dark wedge stand
  on. Default ``l``.
- ``v``: ``t``, ``m`` or ``b``, the text at the top, in the middle or
  at the bottom. Default ``m``.
- ``accent``: the colour of the marked lines and of the bar 14 pixels
  high at the bottom, ``#rrggbb``. Default ``#ffb000``.
- ``size``: the text's height in pixels, 60 to 220. Default 132.
- ``of``: another storyboard's name; the frame comes from that video.
- ``scene``: a scene of its own in place of the video's; ``at`` is then
  a time in that scene.

The frame is drawn bare: without titles, labels, chips, captions, the
brand line and the progress bar. The thumbnail is 1280 x 720 pixels, a
PNG file, with the tag RCBENCH above the text.

Usage:
    python3 tools/make_thumb.py NAME [NAME ...] [-o DIR]
    python3 tools/make_thumb.py --all [-o DIR]

The files go to build/video/thumbs/NAME.png unless -o names a
directory. Needs Pillow, the font Inter Display Black or Inter Black,
and what tools/make_video.py --stills needs.

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

REPO = pathlib.Path(__file__).resolve().parent.parent
BOARDS = REPO / "tools" / "video" / "storyboards"
SIZE = (1280, 720)
INK = (5, 7, 11)
TAG = (34, 211, 238)
FONTS = ("InterDisplay-Black.otf", "Inter-Black.otf", "Inter-Black.ttf")


def font_file() -> pathlib.Path:
    """Find the thumbnail's font.

    Returns:
        The path of Inter Display Black or Inter Black.

    Raises:
        SystemExit: Neither font is installed.
    """
    roots = [pathlib.Path(p) for p in (
        "/usr/share/fonts", "/usr/local/share/fonts",
        str(pathlib.Path.home() / ".fonts"),
        str(pathlib.Path.home() / ".local/share/fonts"),
        "/Library/Fonts", "C:/Windows/Fonts")]
    for name in FONTS:
        for root in roots:
            if root.is_dir():
                found = sorted(root.rglob(name))
                if found:
                    return found[0]
    sys.exit("no Inter Display Black or Inter Black font: install the "
             "Inter family")


def bare_board(board: dict) -> dict:
    """The storyboard the thumbnail's frame is captured from.

    Args:
        board: The storyboard that holds the frame, as read from its file.

    Returns:
        A copy that the player draws bare, without sound; with the thumb
        block's own scene in place of the video's when it has one.
    """
    out = {k: v for k, v in board.items() if k not in ("thumb", "voice")}
    out["bare"] = True
    scene = board.get("thumb", {}).get("scene")
    if scene is not None:
        out.update(scenes=[scene], steps=0)
        out.pop("chapters", None)
    return out


def compose(frame, thumb: dict, font: pathlib.Path):
    """Lay the text over the frame.

    Args:
        frame: The frame, a Pillow image of 1920 x 1080 pixels.
        thumb: The storyboard's thumb block.
        font: The font file.

    Returns:
        The thumbnail, a Pillow image of 1280 x 720 pixels.
    """
    from PIL import Image, ImageDraw, ImageEnhance, ImageFont
    w, h = SIZE
    x, y, cw = thumb.get("crop", [0, 0, frame.width])
    im = frame.convert("RGB").crop(
        (round(x), round(y), round(x + cw), round(y + cw * 9 / 16)))
    im = im.resize(SIZE, Image.LANCZOS)
    im = ImageEnhance.Color(im).enhance(1.25)
    im = ImageEnhance.Contrast(im).enhance(1.08)
    left = thumb.get("side", "l") == "l"
    accent = tuple(int(thumb.get("accent", "#ffb000")[i:i + 2], 16)
                   for i in (1, 3, 5))
    # A dark wedge under the text, gone at 62% of the width.
    row = Image.new("L", (w, 1))
    for col in range(w):
        k = col / w if left else 1 - col / w
        row.putpixel((col, 0), int(230 * max(0.0, 1 - k / 0.62) ** 1.3))
    im = Image.composite(Image.new("RGB", SIZE, INK), im, row.resize(SIZE))
    draw = ImageDraw.Draw(im)
    size = round(thumb.get("size", 132))
    face = ImageFont.truetype(str(font), size)
    lines = thumb["lines"]
    step = round(size * 1.02)
    room = h - step * len(lines)
    top = {"t": 112, "m": room // 2 + 6,
           "b": room - 44}[thumb.get("v", "m")]
    for n, line in enumerate(lines):
        marked = line.startswith("*")
        text = line.lstrip("*")
        width = draw.textlength(text, font=face)
        draw.text((56 if left else w - 56 - width, top + n * step), text,
                  font=face, fill=accent if marked else (255, 255, 255),
                  stroke_width=max(8, size // 11), stroke_fill=INK)
    small = ImageFont.truetype(str(font), 34)
    width = draw.textlength("RCBENCH", font=small) + 28
    x0 = 56 if left else w - 56 - width
    draw.rounded_rectangle((x0, 40, x0 + width, 92), 10, fill=TAG)
    draw.text((x0 + 14, 46), "RCBENCH", font=small, fill=INK)
    draw.rectangle((0, h - 14, w, h), fill=accent)
    return im


def make(name: str, out_dir: pathlib.Path, font: pathlib.Path) -> int:
    """Write one storyboard's thumbnail.

    Args:
        name: The storyboard's name under tools/video/storyboards/.
        out_dir: The directory of the PNG file.
        font: The font file.

    Returns:
        0 on success, 1 on a fault.
    """
    from PIL import Image
    board = json.loads((BOARDS / f"{name}.json").read_text(encoding="utf-8"))
    thumb = board.get("thumb")
    if thumb is None:
        print(f"{name}: no thumb block", file=sys.stderr)
        return 1
    source = board
    if "of" in thumb:
        source = json.loads((BOARDS / f"{thumb['of']}.json").read_text(
            encoding="utf-8"))
        source = {**source, "thumb": {}}
    else:
        source = {**board, "thumb": thumb}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        # The stills a storyboard shows lie beside it.
        if (BOARDS / "stills").is_dir():
            (tmp / "stills").symlink_to(BOARDS / "stills")
        path = tmp / f"{name}.json"
        path.write_text(json.dumps(bare_board(source)), encoding="utf-8")
        at = f"{thumb['at']:g}"
        done = subprocess.run(
            [sys.executable, str(REPO / "tools" / "make_video.py"),
             str(path), "--no-voice", "--stills", at, "-o", str(tmp)],
            capture_output=True, text=True)
        still = tmp / f"still-{at}.png"
        if done.returncode or not still.is_file():
            print(f"{name}: no frame\n{done.stdout}{done.stderr}",
                  file=sys.stderr)
            return 1
        out_dir.mkdir(parents=True, exist_ok=True)
        compose(Image.open(still), thumb, font).save(out_dir / f"{name}.png")
    print(out_dir / f"{name}.png")
    return 0


def main(argv: list) -> int:
    """Write the thumbnails named on the command line.

    Args:
        argv: The arguments without the program's name.

    Returns:
        0 when every thumbnail is written, 1 otherwise.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("names", nargs="*", help="storyboards by name")
    ap.add_argument("--all", action="store_true",
                    help="every storyboard that has a thumb block")
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    default=REPO / "build" / "video" / "thumbs")
    args = ap.parse_args(argv)
    names = args.names
    if args.all:
        names = [p.stem for p in sorted(BOARDS.glob("*.json"))
                 if "thumb" in json.loads(p.read_text(encoding="utf-8"))]
    if not names:
        ap.error("name a storyboard or pass --all")
    if not shutil.which("node"):
        sys.exit("no node on PATH")
    font = font_file()
    return max(make(name, args.output, font) for name in names)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
