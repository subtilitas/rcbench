#!/usr/bin/env python3
r"""Convert a KiCad board into the flat board the video player draws.

The player's board scene draws tools/video/cad/NAME.json: the size,
the tracks of the front copper, every pad with its net and the outline of
every footprint.  This tool writes that file from a .kicad_pcb file.  The
board file is not in the tree, and neither is the converted board.

    python3 tools/video_board.py rbmod.kicad_pcb \
        -o tools/video/cad/rbmod-rev1.json

The drawing is the board seen from its front, turned a quarter clockwise
by default, so that the edge at the board file's highest x lies at the
bottom.  All lengths are mm from the drawing's top left corner.

A pad's name is REF.NUMBER, as in M1.20.  Where a footprint has more than
one pad of a number, they take a letter in the order of the board file:
M7.2a, M7.2b.

Attributes:
    LAYER: The copper layer whose tracks are drawn.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import re
import sys

LAYER = "F.Cu"
_TOKEN = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+')


def parse(text: str) -> list:
    """Read an S-expression into nested lists.

    Args:
        text: The content of a KiCad file.

    Returns:
        The top list; a quoted string is its content, any other atom its
        text.
    """
    stack: list = [[]]
    for token in _TOKEN.findall(text):
        if token == "(":
            stack.append([])
        elif token == ")":
            done = stack.pop()
            stack[-1].append(done)
        elif token.startswith('"'):
            stack[-1].append(token[1:-1].replace('\\"', '"'))
        else:
            stack[-1].append(token)
    return stack[0][0]


def _all(node: list, name: str) -> list:
    return [n for n in node if isinstance(n, list) and n and n[0] == name]


def _one(node: list, name: str):
    found = _all(node, name)
    return found[0] if found else None


def _net(node: list) -> str:
    # (net "NAME") from KiCad 10, (net NUMBER "NAME") before it.
    net = _one(node, "net")
    return net[-1] if net and len(net) > 1 else ""


def _xy(node: list) -> tuple:
    return float(node[1]), float(node[2])


def convert(text: str, turn: int = 90) -> dict:
    """Lay a board file out as the player draws it.

    Args:
        text: The content of a .kicad_pcb file.
        turn: Degrees the drawing is turned clockwise: 0, 90, 180 or 270.

    Returns:
        The board: size, tracks, pads and parts.
    """
    board = parse(text)
    edge = []
    for kind in ("gr_line", "gr_rect"):
        for item in _all(board, kind):
            if (_one(item, "layer") or [None, None])[1] == "Edge.Cuts":
                edge += [_xy(_one(item, "start")), _xy(_one(item, "end"))]
    if not edge:
        sys.exit("the board has no outline of lines on Edge.Cuts")
    x0, y0 = min(p[0] for p in edge), min(p[1] for p in edge)
    x1, y1 = max(p[0] for p in edge), max(p[1] for p in edge)

    def place(x, y):
        # A quarter turn clockwise puts the highest x at the bottom.
        return {0: (x - x0, y - y0), 90: (y1 - y, x - x0),
                180: (x1 - x, y1 - y), 270: (y - y0, x1 - x)}[turn]

    def r3(v):
        return round(v, 3)

    tracks = []
    for seg in _all(board, "segment"):
        if _one(seg, "layer")[1] != LAYER:
            continue
        a, b = place(*_xy(_one(seg, "start"))), place(*_xy(_one(seg, "end")))
        tracks.append({"a": [r3(a[0]), r3(a[1])], "b": [r3(b[0]), r3(b[1])],
                       "w": float(_one(seg, "width")[1]), "net": _net(seg)})
    pads, parts = [], []
    for fp in _all(board, "footprint"):
        at = _one(fp, "at")
        fx, fy = float(at[1]), float(at[2])
        rot = math.radians(float(at[3]) if len(at) > 3 else 0.0)
        props = {p[1]: p[2] for p in _all(fp, "property")}
        ref = props.get("Reference", "?")

        def world(px, py, fx=fx, fy=fy, rot=rot):
            return place(fx + px * math.cos(rot) + py * math.sin(rot),
                         fy - px * math.sin(rot) + py * math.cos(rot))

        seen: dict = {}
        mine = []
        for pad in _all(fp, "pad"):
            pat = _one(pad, "at")
            x, y = world(float(pat[1]), float(pat[2]))
            # A pad's angle in the file holds the footprint's.
            angle = (float(pat[3]) if len(pat) > 3 else 0.0) + turn
            w, h = (float(v) for v in _one(pad, "size")[1:3])
            if round(angle) % 180 == 90:
                w, h = h, w
            drill = _one(pad, "drill")
            seen.setdefault(pad[1], []).append(len(mine))
            mine.append({"ref": ref, "num": pad[1], "x": r3(x), "y": r3(y),
                         "w": w, "h": h,
                         "round": pad[3] in ("circle", "oval"),
                         "drill": float(drill[-1]) if drill
                         and len(drill) > 1 and drill[-1] != "oval" else 0,
                         "net": _net(pad)})
        for num, where in seen.items():
            for k, n in enumerate(where):
                letter = "abcdefgh"[k] if len(where) > 1 else ""
                mine[n]["name"] = f"{ref}.{num}{letter}"
        pads += [p for p in mine if p["num"]]
        # The part's box: its courtyard, or its pads where it has none.
        pts = []
        for kind in ("fp_line", "fp_rect"):
            for item in _all(fp, kind):
                if _one(item, "layer")[1].endswith(".CrtYd"):
                    a, b = _xy(_one(item, "start")), _xy(_one(item, "end"))
                    pts += [world(a[0], a[1]), world(b[0], b[1]),
                            world(a[0], b[1]), world(b[0], a[1])]
        for item in _all(fp, "fp_poly"):
            if _one(item, "layer")[1].endswith(".CrtYd"):
                pts += [world(*_xy(p)) for p in _all(_one(item, "pts"), "xy")]
        if not pts:
            pts = [(p["x"] + sx * p["w"] / 2, p["y"] + sy * p["h"] / 2)
                   for p in mine for sx in (-1, 1) for sy in (-1, 1)]
        if pts:
            parts.append({"ref": ref, "value": props.get("Value", ""),
                          "footprint": fp[1].split(":")[-1],
                          "box": [r3(min(p[0] for p in pts)),
                                  r3(min(p[1] for p in pts)),
                                  r3(max(p[0] for p in pts)),
                                  r3(max(p[1] for p in pts))]})
    w, h = (x1 - x0, y1 - y0) if turn in (0, 180) else (y1 - y0, x1 - x0)
    return {"size": [r3(w), r3(h)], "layer": LAYER, "turn": turn,
            "tracks": tracks, "pads": pads, "parts": parts}


def main() -> int:
    """Convert one board.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("board", type=pathlib.Path, help="the .kicad_pcb file")
    ap.add_argument("-o", "--output", type=pathlib.Path, required=True,
                    help="the .json file under tools/video/cad/")
    ap.add_argument("--turn", type=int, default=90, choices=(0, 90, 180, 270),
                    help="degrees the drawing is turned clockwise")
    args = ap.parse_args()
    try:
        text = args.board.read_text(encoding="utf-8")
    except OSError as err:
        sys.exit(f"{args.board}: {err}")
    out = convert(text, args.turn)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(out), encoding="utf-8")
    print(f"{args.output}: {out['size'][0]} x {out['size'][1]}mm, "
          f"{len(out['tracks'])} tracks, {len(out['pads'])} pads, "
          f"{len(out['parts'])} parts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
