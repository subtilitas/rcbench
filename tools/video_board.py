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

A footprint whose name is that of a module under hardware/kicad/modules/
takes the module's board outline as its box and, as details, the parts
of the module's model, each a box or a circle with the colour name of the
model, the texts of the module's drawing, such as USB-C and CAN, on each
pad its number and under each pad the name of its pin where the drawing
does not print it.
A JST or Molex connector carries a short name of each pad's net, and a
3-row pin header the names of its rows.

A pad's name is REF.NUMBER, as in M1.20.  Where a footprint has more than
one pad of a number, they take a letter in the order of the board file:
M7.2a, M7.2b.

Attributes:
    LAYER: The copper layer whose tracks are drawn.
    MODULES: The directory of the module specs.
    SHORT: The short name of a net on a connector, as (pattern, name),
        first match.

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
MODULES = (pathlib.Path(__file__).resolve().parent.parent / "hardware"
           / "kicad" / "modules")
SHORT = (
    (r"HEARTBEAT", "HB"), (r"SCL", "SCL"), (r"SDA", "SDA"),
    (r"PDMINI_TX", "TX"), (r"PDMINI_RX", "RX"), (r"^GND$", "GND"),
    (r"^\+5V$", "5V"), (r"^\+3\.3V$", "3V3"), (r"^\+15V$", "15V"),
    (r"^VCC_PDMINI$", "V+"), (r"J9", "TAP"),
)
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


def modules(where: pathlib.Path = MODULES) -> dict:
    """Read the module specs.

    Args:
        where: The directory of the specs, one Python file a module.

    Returns:
        Per footprint name the module's board size, the footprint's origin
        on it, the bodies of its model, the texts of its drawing as
        (text, x, y, height in mm) and the pin names by pad number.
    """
    import importlib.util

    found = {}
    for path in sorted(where.glob("*.py")):
        spec = importlib.util.spec_from_file_location(path.stem, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        fp = getattr(mod, "FOOTPRINT", None)
        if fp:
            texts = []
            for item in fp.get("fab", []) + fp.get("silk", []):
                if item[0] == "text" and item[1:] not in texts:
                    texts.append(item[1:])
            names = {pin[2]: pin[1] for pin in getattr(
                mod, "SYMBOL", {}).get("pins", [])}
            pads = [(pad["num"], pad["x"], pad["y"]) for pad in fp["pads"]]
            found[fp["name"]] = (fp["board"], fp["origin"], getattr(
                mod, "MODEL", {}).get("bodies", []), texts, names, pads)
    return found


def convert(text: str, turn: int = 90, specs: dict | None = None) -> dict:
    """Lay a board file out as the player draws it.

    Args:
        text: The content of a .kicad_pcb file.
        turn: Degrees the drawing is turned clockwise: 0, 90, 180 or 270.
        specs: The modules as modules() returns them; None for none.

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
        details = []
        name = fp[1].split(":")[-1]
        if specs and name in specs:
            # The module's own board, and the parts on it, laid out from
            # the footprint's origin.
            (bw, bh), (ox, oy), bodies, texts, names, spads = specs[name]

            def spot(x, y, world=world, ox=ox, oy=oy):
                return world(x - ox, y - oy)

            pts = [spot(x, y) for x in (0, bw) for y in (0, bh)]
            for body in bodies:
                if body[0] == "box":
                    c = [spot(x, y) for x in (body[1], body[3])
                         for y in (body[2], body[4])]
                    details.append({
                        "box": [r3(min(p[0] for p in c)),
                                r3(min(p[1] for p in c)),
                                r3(max(p[0] for p in c)),
                                r3(max(p[1] for p in c))],
                        "color": body[6]})
                elif body[0] == "cyl":
                    cx, cy = spot(body[1], body[2])
                    details.append({"at": [r3(cx), r3(cy)], "r": body[3],
                                    "color": body[5]})
            # Where the pins get their names, the drawing's pad numbers
            # would lie under them.
            named = any(names.get(num) and names[num] not in
                        {said for said, *_ in texts} for num, _, _ in spads)
            for said, x, y, size in texts:
                if named and said.isdigit():
                    continue
                cx, cy = spot(x, y)
                details.append({"text": said, "at": [r3(cx), r3(cy)],
                                "size": size})
            # On each pad its number; under it, on the drawing, its
            # pin's name where the module's drawing does not print it.
            printed = {said for said, *_ in texts}
            for num, x, y in spads:
                px, py = spot(x, y)
                details.append({"text": num, "at": [r3(px), r3(py)],
                                "size": 0.8, "pad": True})
                said = names.get(num, "")
                said = said.split("_")[0] if said.startswith("GP") else said
                if not said or said in printed or names[num] in printed:
                    continue
                details.append({"text": said, "at": [r3(px), r3(py + 1.27)],
                                "size": 0.6, "name": True})
        elif re.search(r"JST|Molex", name):
            # A connector: a short name of each pad's net, on its housing.
            for pad in mine:
                said = next((short for pattern, short in SHORT
                             if re.search(pattern, pad["net"])), "")
                if said:
                    details.append({"text": said, "ink": "dark",
                                    "at": [r3(pad["x"] - 1.75), pad["y"]],
                                    "size": 0.65})
        elif "PinHeader_3x" in name and mine:
            # A 3-row header: its rows by what their nets carry.
            rows: dict = {}
            for pad in mine:
                rows.setdefault(pad["y"], []).append(pad)
            for y, row in rows.items():
                nets = {pad["net"] for pad in row}
                said = ("GND" if nets == {"GND"} else
                        "+" if all(n.startswith("VCC") for n in nets)
                        else "SIG")
                details.append({"text": said, "anchor": "end", "size": 0.8,
                                "at": [r3(min(p["x"] for p in row) - 1.4),
                                       y]})
        if pts:
            parts.append({"ref": ref, "value": props.get("Value", ""),
                          "details": details,
                          "back": _one(fp, "layer")[1].startswith("B."),
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
    out = convert(text, args.turn, modules())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(out), encoding="utf-8")
    print(f"{args.output}: {out['size'][0]} x {out['size'][1]}mm, "
          f"{len(out['tracks'])} tracks, {len(out['pads'])} pads, "
          f"{len(out['parts'])} parts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
