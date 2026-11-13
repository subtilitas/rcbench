#!/usr/bin/env python3
"""Render a storyboard under tools/video/storyboards/ into an MP4 video.

A storyboard is a JSON file: a list of scenes that show the bench's case as
a 3D model, a circuit board seen flat with rework drawn on it, the panel's
screens with pointer bubbles or with a caption and a spotlight, and a
recap.  The
screens are not recordings: each step names a view of the host renderer
(tools/render_ui.py), so a video shows what the display shows.

    python3 tools/make_video.py kst-rbmod                # build/video/*.mp4
    python3 tools/make_video.py kst-rbmod --stills 2.5,40
    python3 tools/make_video.py --check                  # every storyboard

The video carries a sound track that tools/video_audio.py computes from
the same storyboard: synthesizer pads and one quiet sound per event.
--no-sound leaves it out.  A storyboard with "voice" is narrated by
tools/video_voice.py, each step stretched to its sentence; --no-voice
leaves the narration out and the lengths as written.

A render needs a C compiler, Pillow, numpy for the sound, Node.js with the
playwright package and its Chromium, ffmpeg, and `npm install` run once in
tools/video/.
--check needs none of them: it reads every storyboard and fails on a field
the player would not draw.  tools/video/README.md lists the fields.

Attributes:
    VIDEO: The directory of the player, its packages and the storyboards.
    PANEL: The panel's display in pixels, the range of a step's target.
    FRAME: The video frame in pixels, the range of a label's position.
    BEDS: The music beds of tools/video_audio.py.
    UNITS: The components of the cad model that a scene can move.
    PART_FRAMES: The frames of one part that --keep keeps.
    ANCHORS: The named points of the case model a label can point at.
    CONNECTORS: The headers of the case model and their position count.
    FITS: The parts a board scene solders across two pads.
    INSETS: The cross-sections a board scene shows in a card.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import functools
import http.server
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading

REPO = pathlib.Path(__file__).resolve().parent.parent
VIDEO = REPO / "tools" / "video"
PANEL = (1024, 600)
FRAME = (1920, 1080)
ANCHORS = ("display", "rbmod", "pdmini", "J1", "J2", "xt60", "usbc",
           "estop", "side", "plug", "panel", "case", "mcu",
           "ina", "buck", "trigger")
UNITS = ("lid", "estop", "xt60", "usbc", "pdmini", "mcu", "ina",
         "buck", "trigger", "rbmod", "case")
CONNECTORS = {"J1": 4, "J2": 4}
BEDS = ("pads", "bach-prelude", "pachelbel-canon", "vivaldi-spring")
PART_FRAMES = 900
ACTIONS = ("tap", "hold")
STYLES = ("ok", "warn", "muted")
FITS = ("resistor", "zener")
INSETS = ("strip", "fill", "extract", "pins")


def _number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _span(item: dict, dur: float, where: str, errors: list) -> None:
    lo, hi = item.get("from"), item.get("to")
    if not (_number(lo) and _number(hi) and 0 <= lo < hi <= dur):
        errors.append(f"{where}: from/to must lie in 0 to {dur}s, in order")


def _keys(keys, size: int, where: str, errors: list) -> None:
    last = -1.0
    for key in keys if isinstance(keys, list) else [None]:
        if not (isinstance(key, list) and len(key) == size
                and _number(key[0]) and key[0] > last):
            errors.append(f"{where}: keys are [t, ...] with rising t")
            return
        last = key[0]


def _check_say(scene: dict, dur: float, where: str, errors: list) -> None:
    cues = scene.get("say", [])
    last = -1.0
    for cue in cues if isinstance(cues, list) else [None]:
        if not (isinstance(cue, dict) and isinstance(cue.get("text"), str)
                and _number(cue.get("at")) and last < cue["at"] < dur):
            errors.append(f"{where}: say holds {{at, text}} with rising at")
            break
        last = cue["at"]


def _check_case(scene: dict, where: str, errors: list) -> None:
    dur = scene.get("dur")
    if not (_number(dur) and dur > 0):
        errors.append(f"{where}: dur must be a positive number")
        return
    _keys(scene.get("camera"), 3, f"{where} camera", errors)
    if scene.get("model", "mockup") not in ("mockup", "cad"):
        errors.append(f"{where}: model must be mockup or cad")
    if "lid" in scene:
        _keys(scene["lid"], 2, f"{where} lid", errors)
    if "display" in scene and not isinstance(
            scene["display"].get("view"), str):
        errors.append(f"{where}: display needs a view")
    if "title" in scene:
        _span(scene["title"], dur, f"{where} title", errors)
        if not isinstance(scene["title"].get("heading"), str):
            errors.append(f"{where} title: heading is missing")
    for n, chip in enumerate(scene.get("chips", [])):
        _span(chip, dur, f"{where} chip {n + 1}", errors)
    for n, label in enumerate(scene.get("labels", [])):
        at = f"{where} label {n + 1}"
        _span(label, dur, at, errors)
        anchor = label.get("anchor")
        if not (anchor in ANCHORS or (isinstance(anchor, list)
                                      and len(anchor) == 3)):
            errors.append(f"{at}: anchor must be [x, y, z] in mm or one "
                          f"of {', '.join(ANCHORS)}")
        pos = label.get("at")
        if not (isinstance(pos, list) and len(pos) == 2
                and 0 <= pos[0] < FRAME[0] and 0 <= pos[1] < FRAME[1]):
            errors.append(f"{at}: at must be [x, y] inside the frame")
        if not isinstance(label.get("title"), str):
            errors.append(f"{at}: title is missing")
        if label.get("style", "ok") not in STYLES:
            errors.append(f"{at}: style must be one of {', '.join(STYLES)}")
    for name, keys in scene.get("parts", {}).items():
        if name not in UNITS:
            errors.append(f"{where} parts: {name} is not one of "
                          f"{', '.join(UNITS)}")
        _keys(keys, 2, f"{where} parts {name}", errors)
    _check_say(scene, dur, where, errors)
    for name in ("servo", "pinout"):
        part = scene.get(name)
        if part is None:
            continue
        count = CONNECTORS.get(part.get("connector"))
        chosen = part.get("position" if name == "servo" else "select")
        if count is None or not (isinstance(chosen, int)
                                 and 1 <= chosen <= count):
            errors.append(f"{where} {name}: connector must be one of "
                          f"{', '.join(CONNECTORS)} with a position in it")
    if "pinout" in scene:
        _span(scene["pinout"], dur, f"{where} pinout", errors)
    if "servo" in scene:
        plug = scene["servo"].get("plug")
        if not (_number(scene["servo"].get("appear"))
                and isinstance(plug, list) and len(plug) == 2
                and plug[0] < plug[1] <= dur):
            errors.append(f"{where} servo: needs appear and plug [t0, t1]")


def _place(value) -> bool:
    # A point of a board: a pad's name, or [x, y] in mm.
    return (isinstance(value, str) and "." in value) or (
        isinstance(value, list) and len(value) == 2
        and all(_number(v) for v in value))


def _pair(value, dur: float) -> bool:
    return (isinstance(value, list) and len(value) == 2
            and all(_number(v) for v in value)
            and 0 <= value[0] < value[1] <= dur)


def _check_board(scene: dict, where: str, errors: list) -> None:
    dur = scene.get("dur")
    if not (_number(dur) and dur > 0):
        errors.append(f"{where}: dur must be a positive number")
        return
    name = scene.get("board")
    if not (isinstance(name, str) and name
            and all(c.isalnum() or c in "-_" for c in name)):
        errors.append(f"{where}: board names a file under tools/video/cad/, "
                      "without its suffix")
    keys = scene.get("camera")
    _keys(keys, 3, f"{where} camera", errors)
    for key in keys if isinstance(keys, list) else []:
        if not (isinstance(key, list) and len(key) == 3 and _place(key[1])
                and isinstance(key[1], list) and _number(key[2])
                and key[2] > 0):
            errors.append(f"{where} camera: a key is [t, [x, y], w] with "
                          "the width w in mm above 0")
            break
    if "title" in scene:
        _span(scene["title"], dur, f"{where} title", errors)
        if not isinstance(scene["title"].get("heading"), str):
            errors.append(f"{where} title: heading is missing")
    for n, chip in enumerate(scene.get("chips", [])):
        _span(chip, dur, f"{where} chip {n + 1}", errors)
    for n, label in enumerate(scene.get("labels", [])):
        at = f"{where} label {n + 1}"
        _span(label, dur, at, errors)
        if not _place(label.get("anchor")):
            errors.append(f"{at}: anchor must be a pad's name or [x, y] "
                          "in mm")
        pos = label.get("at")
        if not (isinstance(pos, list) and len(pos) == 2
                and 0 <= pos[0] < FRAME[0] and 0 <= pos[1] < FRAME[1]):
            errors.append(f"{at}: at must be [x, y] inside the frame")
        if not isinstance(label.get("title"), str):
            errors.append(f"{at}: title is missing")
        if label.get("style", "ok") not in STYLES:
            errors.append(f"{at}: style must be one of {', '.join(STYLES)}")
    for n, tag in enumerate(scene.get("tags", [])):
        at = f"{where} tag {n + 1}"
        _span(tag, dur, at, errors)
        if not (_place(tag.get("at")) and isinstance(tag.get("text"), str)
                and tag.get("side", "r") in ("l", "r", "t", "b")):
            errors.append(f"{at}: needs text and at, a pad's name or "
                          "[x, y] in mm")
    for n, net in enumerate(scene.get("nets", [])):
        _span(net, dur, f"{where} net {n + 1}", errors)
        if not isinstance(net.get("net"), str):
            errors.append(f"{where} net {n + 1}: net is missing")
    for n, cut in enumerate(scene.get("cuts", [])):
        ends = cut.get("between")
        if not (isinstance(ends, list) and len(ends) == 2
                and all(_place(e) for e in ends)
                and _pair(cut.get("t"), dur)):
            errors.append(f"{where} cut {n + 1}: needs between, two points, "
                          "and t [t0, t1]")
    for n, wire in enumerate(scene.get("wires", [])):
        via = wire.get("via", [])
        if not (_place(wire.get("from")) and _place(wire.get("to"))
                and isinstance(via, list) and all(_place(v) for v in via)
                and ("gone" not in wire or _pair(wire["gone"], dur))
                and _pair(wire.get("t"), dur)):
            errors.append(f"{where} wire {n + 1}: needs from, to and "
                          "t [t0, t1]")
    _check_build(scene, dur, where, errors)
    _check_say(scene, dur, where, errors)
    for n, fit in enumerate(scene.get("fit", [])):
        if not (fit.get("kind") in FITS and _place(fit.get("a"))
                and _place(fit.get("b")) and _number(fit.get("t"))
                and 0 <= fit["t"] < dur):
            errors.append(f"{where} fit {n + 1}: needs kind, one of "
                          f"{', '.join(FITS)}, the pads a and b, and t")


def _names(value) -> bool:
    return (isinstance(value, list) and bool(value)
            and all(isinstance(v, str) for v in value))


def _check_build(scene: dict, dur: float, where: str, errors: list) -> None:
    # What a board scene needs to show a board being assembled.
    if not isinstance(scene.get("bare", False), bool):
        errors.append(f"{where}: bare is true or false")
    for n, mount in enumerate(scene.get("mount", [])):
        if not (_names(mount.get("refs")) and _number(mount.get("t"))
                and 0 <= mount["t"] < dur):
            errors.append(f"{where} mount {n + 1}: needs refs, a list of "
                          "parts, and t")
    for n, joint in enumerate(scene.get("solder", [])):
        if not ((_names(joint.get("refs")) or _names(joint.get("pads")))
                and isinstance(joint.get("back", False), bool)
                and _pair(joint.get("t"), dur)):
            errors.append(f"{where} solder {n + 1}: needs refs or pads, a "
                          "list, and t [t0, t1]")
    for n, plug in enumerate(scene.get("plugs", [])):
        if not (isinstance(plug.get("name"), str) and plug["name"]
                and "." not in plug["name"] and _place(plug.get("at"))
                and isinstance(plug["at"], list)
                and _number(plug.get("t")) and 0 <= plug["t"] < dur):
            errors.append(f"{where} plug {n + 1}: needs a name, at, "
                          "[x, y] in mm, and t")
    for n, inset in enumerate(scene.get("insets", [])):
        at = f"{where} inset {n + 1}"
        _span(inset, dur, at, errors)
        pos = inset.get("at")
        if not (inset.get("kind") in INSETS
                and isinstance(inset.get("title"), str)
                and isinstance(pos, list) and len(pos) == 2
                and 0 <= pos[0] < FRAME[0] and 0 <= pos[1] < FRAME[1]):
            errors.append(f"{at}: needs kind, one of {', '.join(INSETS)}, "
                          "a title and at, [x, y] inside the frame")


def board_faults(scene: dict, plan: dict) -> list:
    """Find what a board scene names and its board does not have.

    Args:
        scene: A board scene that passed check().
        plan: The board as tools/video_board.py wrote it.

    Returns:
        One line per pad or net that the board lacks.
    """
    pads = {pad["name"] for pad in plan["pads"]}
    pads |= {f"{plug['name']}.{pad}" for plug in scene.get("plugs", [])
             for pad in ("G", "D+", "D-", "V")}
    nets = {track["net"] for track in plan["tracks"]}
    named = [label["anchor"] for label in scene.get("labels", [])]
    named += [tag["at"] for tag in scene.get("tags", [])]
    for cut in scene.get("cuts", []):
        named += cut["between"]
    for wire in scene.get("wires", []):
        named += [wire["from"], wire["to"], *wire.get("via", [])]
    for fit in scene.get("fit", []):
        named += [fit["a"], fit["b"]]
    for joint in scene.get("solder", []):
        named += joint.get("pads", [])
    faults = [f"no pad {name}" for name in named
              if isinstance(name, str) and name not in pads]
    refs = {part["ref"] for part in plan["parts"]}
    faults += [f"no part {ref}"
               for item in scene.get("mount", []) + scene.get("solder", [])
               for ref in item.get("refs", []) if ref not in refs]
    faults += [f"no track of net {net['net']}"
               for net in scene.get("nets", []) if net["net"] not in nets]
    return faults


def _check_screen(scene: dict, steps: int, where: str, errors: list) -> None:
    if not scene.get("steps"):
        errors.append(f"{where}: steps is empty")
    for n, step in enumerate(scene.get("steps", [])):
        at = f"{where} step {n + 1}"
        for name in ("view", "title"):
            if not isinstance(step.get(name), str):
                errors.append(f"{at}: {name} is missing")
        action = step.get("action")
        if action is not None and action not in ACTIONS:
            errors.append(f"{at}: action must be one of "
                          f"{', '.join(ACTIONS)}")
        # The touch mark needs its lead-in: 0.75s for a tap, 2.45s for a
        # hold, after the 0.6s the bubble takes to appear.
        least = {"tap": 1.4, "hold": 3.1}.get(action, 0.9)
        if not (_number(step.get("dur")) and step["dur"] >= least):
            errors.append(f"{at}: dur must be at least {least}s")
        if not (isinstance(step.get("n"), int) and 1 <= step["n"] <= steps):
            errors.append(f"{at}: n must be a step from 1 to {steps}")
        target = step.get("target")
        if target is None and action is not None:
            errors.append(f"{at}: a {action} needs a target")
        if target is not None and not (
                isinstance(target, list) and len(target) == 2
                and 0 <= target[0] < PANEL[0]
                and 0 <= target[1] < PANEL[1]):
            errors.append(f"{at}: target must be [x, y] on the "
                          f"{PANEL[0]}x{PANEL[1]} display")
        if step.get("style", "ok") not in STYLES:
            errors.append(f"{at}: style must be one of {', '.join(STYLES)}")


def _check_tour(scene: dict, steps: int, where: str, errors: list) -> None:
    if not scene.get("steps"):
        errors.append(f"{where}: steps is empty")
    for n, step in enumerate(scene.get("steps", [])):
        at = f"{where} step {n + 1}"
        if not (isinstance(step.get("n"), int) and 1 <= step["n"] <= steps):
            errors.append(f"{at}: n must be a step from 1 to {steps}")
        if "card" in step:
            # A chapter card: its line is drawn by 1.25s.
            if not isinstance(step["card"].get("title"), str):
                errors.append(f"{at}: card needs a title")
            if not (_number(step.get("dur")) and step["dur"] >= 2.0):
                errors.append(f"{at}: dur must be at least 2.0s")
            continue
        for name in ("view", "title"):
            if not isinstance(step.get(name), str):
                errors.append(f"{at}: {name} is missing")
        marks = step.get("marks", [])
        for mark in marks if isinstance(marks, list) else [None]:
            if not (isinstance(mark, dict) and isinstance(mark.get("text"),
                                                          str)
                    and isinstance(mark.get("at"), list)
                    and len(mark["at"]) == 2
                    and 0 <= mark["at"][0] < PANEL[0]
                    and 0 <= mark["at"][1] < PANEL[1]
                    and mark.get("side", "r") in ("l", "r", "t", "b")):
                errors.append(f"{at}: a mark needs text and at, [x, y] on "
                              f"the {PANEL[0]}x{PANEL[1]} display")
        # The caption takes 0.6s to appear and each point 0.14s more; the
        # spotlight is drawn by 1.5s, and a mark every 0.38s from 1.0s.
        least = 2.5 if "focus" in step else 1.5
        least = max(least, 1.9 + 0.38 * len(marks)) if marks else least
        if not (_number(step.get("dur")) and step["dur"] >= least):
            errors.append(f"{at}: dur must be at least {least}s")
        if step.get("lang", "en") not in ("en", "de"):
            errors.append(f"{at}: lang must be en or de")
        focus = step.get("focus")
        if focus is not None and not (
                isinstance(focus, list) and len(focus) == 4
                and focus[0] >= 0 and focus[1] >= 0
                and focus[2] >= 24 and focus[3] >= 24
                and focus[0] + focus[2] <= PANEL[0]
                and focus[1] + focus[3] <= PANEL[1]):
            errors.append(f"{at}: focus must be [x, y, w, h] on the "
                          f"{PANEL[0]}x{PANEL[1]} display")
        if not isinstance(step.get("points", []), list):
            errors.append(f"{at}: points must be a list")


def check(board: dict) -> list:
    """Find what the player would not draw.

    Args:
        board: A storyboard as read from its JSON file.

    Returns:
        One line per fault; empty for a storyboard the player draws.
    """
    errors = []
    if board.get("lang") not in ("en", "de"):
        errors.append("lang must be en or de")
    steps = board.get("steps", 0)
    if not isinstance(steps, int) or steps < 0:
        errors.append("steps must be a count of 0 or more")
        steps = 0
    voice = board.get("voice")
    if voice is not None and not (
            isinstance(voice, dict)
            and (isinstance(voice.get("name"), str) or "blend" in voice)):
        errors.append("voice needs a name or a blend of the speaker's "
                      "voices")
    if board.get("music", "pads") not in BEDS:
        errors.append(f"music must be one of {', '.join(BEDS)}")
    gain = board.get("music_gain", 1.0)
    if not (_number(gain) and 0 <= gain <= 2):
        errors.append("music_gain is a number from 0 to 2")
    blend = (voice or {}).get("blend")
    if blend is not None and not (
            isinstance(blend, dict) and blend
            and all(_number(part) and part > 0 for part in blend.values())):
        errors.append("voice blend holds a positive part per voice")
    chapters = board.get("chapters")
    if chapters is not None and not (
            isinstance(chapters, list) and len(chapters) == steps
            and all(isinstance(name, str) for name in chapters)):
        errors.append("chapters must hold one name per step")
    scenes = board.get("scenes")
    if not isinstance(scenes, list) or not scenes:
        return errors + ["scenes is empty"]
    for n, scene in enumerate(scenes):
        where = f"scene {n + 1}"
        kind = scene.get("type") if isinstance(scene, dict) else None
        if kind == "case":
            _check_case(scene, where, errors)
        elif kind == "board":
            _check_board(scene, where, errors)
        elif kind == "screen":
            _check_screen(scene, steps, where, errors)
        elif kind == "tour":
            _check_tour(scene, steps, where, errors)
        elif kind == "end":
            if not (_number(scene.get("dur")) and scene["dur"] > 0
                    and isinstance(scene.get("heading"), str)):
                errors.append(f"{where}: needs dur and heading")
        else:
            errors.append(f"{where}: type must be case, board, screen, "
                          "tour or end")
    return errors


def views(board: dict) -> dict:
    """List the renderer views a storyboard shows.

    Args:
        board: A storyboard that passed check().

    Returns:
        The shots by file name without suffix, the view name with @de or
        @en where a step sets its own language.  Each holds the view, the
        screen name the storyboard gives or None, and the language.
    """
    found = {}
    for scene in board["scenes"]:
        if scene["type"] == "case" and "display" in scene:
            shot = scene["display"]
            found[shot["view"]] = (shot["view"], shot.get("screen"),
                                   board["lang"])
        if scene["type"] in ("screen", "tour"):
            for step in scene["steps"]:
                if "card" in step:
                    continue
                lang = step.get("lang")
                name = f"{step['view']}@{lang}" if lang else step["view"]
                found[name] = (step["view"],
                               step.get("screen", scene.get("screen")),
                               lang or board["lang"])
    return found


def storyboards() -> list:
    """List the committed storyboards.

    Returns:
        Their paths, sorted by name.
    """
    return sorted((VIDEO / "storyboards").glob("*.json"))


def load(path: pathlib.Path) -> dict:
    """Read a storyboard and stop on a fault in it.

    Args:
        path: The JSON file.

    Returns:
        The storyboard.
    """
    try:
        board = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as err:
        sys.exit(f"{path}: {err}")
    errors = check(board) if isinstance(board, dict) else ["not an object"]
    if errors:
        sys.exit("\n".join(f"{path.name}: {line}" for line in errors))
    return board


def render_views(board: dict, shots: pathlib.Path) -> None:
    """Draw the storyboard's views with the host renderer.

    Args:
        board: The storyboard.
        shots: The directory that takes one PNG per view.
    """
    import render_ui

    if not shutil.which("cc"):
        sys.exit("no C compiler on PATH")
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = render_ui.build(tmp)
        known = {**render_ui.SCREENS, **render_ui.FIT_ONLY}
        for name, (view, screen, lang) in views(board).items():
            # A view of render_ui.py's tables brings its screen and theme;
            # any other is a pose of the screen the storyboard names.
            theme = "dark"
            if view in known:
                _, screen, theme = known[view]
            if screen is None:
                sys.exit(f"view {view!r} is not in render_ui.py's tables: "
                         "the step or its scene must name the screen")
            try:
                image = render_ui.render_one(exe, tmp, view, screen, theme,
                                             lang)
            except subprocess.CalledProcessError:
                sys.exit(f"the renderer does not draw view {view!r} of "
                         f"screen {screen!r}")
            image.save(shots / f"{name}.png")


class _Handler(http.server.SimpleHTTPRequestHandler):
    """Serves the player, its packages, one storyboard and its shots."""

    def __init__(self, *args, board=None, shots=None, **kwargs):
        self._board = board
        self._shots = shots
        super().__init__(*args, directory=str(VIDEO / "player"), **kwargs)

    def translate_path(self, path):
        """Map a request path to a file.

        Args:
            path: The path of the request.

        Returns:
            The file the request reads.
        """
        path = path.split("?", 1)[0]
        if path == "/storyboard.json":
            return str(self._board)
        if path.startswith("/shots/"):
            return str(self._shots / pathlib.PurePosixPath(path).name)
        if path.startswith("/cad/"):
            # A board drawn by hand is in the tree; a converted one is not.
            name = pathlib.PurePosixPath(path).name
            drawn = VIDEO / "boards" / name
            return str(drawn if drawn.is_file() else VIDEO / "cad" / name)
        if path.startswith("/node_modules/"):
            return str(VIDEO / path.lstrip("/"))
        return super().translate_path(path)

    def log_message(self, format, *args):  # noqa: A002
        """Keep the request log off the terminal.

        Args:
            format: Unused.
            *args: Unused.
        """


def _plan(scene: dict) -> pathlib.Path:
    # A board drawn by hand is in the tree; a converted one is not.
    drawn = VIDEO / "boards" / f"{scene['board']}.json"
    return drawn if drawn.is_file() else VIDEO / "cad" / drawn.name


def _capture(cmd: list, board: dict, out: pathlib.Path, fps: int,
             jobs: int, keep: pathlib.Path | None) -> int:
    import concurrent.futures
    import hashlib

    import video_audio

    times = video_audio.scene_times(board)
    total = sum(length for _, length in times)
    frames = round(total * fps)
    where = out.parent if keep is None else keep
    where.mkdir(parents=True, exist_ok=True)

    def digest(value) -> str:
        return hashlib.sha256(json.dumps(value, sort_keys=True).encode()
                              ).hexdigest()[:12]

    # (first frame, end frame, file) per part.
    spans = []
    if keep is None:
        mark = digest(board)
        edges = [frames * k // jobs for k in range(jobs + 1)]
        spans = [(a, b, where / f"{mark}-{fps}-{a}-{b}.mp4")
                 for a, b in zip(edges, edges[1:], strict=False)]
    else:
        # With a directory to keep them in, parts of PART_FRAMES frames
        # within each scene, named by what the scene's frames show: the
        # scene, its place in the video, the video's length, the steps
        # before it, its board and the player.  A change to one scene
        # that leaves every length as it was captures that scene alone.
        code = digest([path.read_text(encoding="utf-8") for path in sorted(
            (VIDEO / "player").iterdir()) if path.is_file()])
        rest = {name: value for name, value in board.items()
                if name != "scenes"}
        before = 0
        for n, (scene, (start, length)) in enumerate(
                zip(board["scenes"], times, strict=True)):
            plan = ""
            if scene["type"] == "board":
                plan = _plan(scene).read_text(encoding="utf-8")
            mark = digest([rest, scene, start, total, n,
                           len(board["scenes"]), before, plan, code])
            first = round(start * fps)
            end = frames if n == len(times) - 1 else round(
                (start + length) * fps)
            for a in range(first, end, PART_FRAMES):
                b = min(end, a + PART_FRAMES)
                spans.append((a, b, where / f"{mark}-{fps}-{a}-{b}.mp4"))
            for chip in scene.get("chips", []):
                if _number(chip.get("n")):
                    before = max(before, chip["n"])
            for step in scene.get("steps", []):
                before = max(before, step.get("n", 0))

    def one(span):
        a, b, done = span
        if keep is not None and done.is_file():
            return 0
        part = done.with_suffix(".part.mp4")
        status = subprocess.run(cmd + ["video", str(part), str(fps),
                                       str(a), str(b)]).returncode
        if status == 0:
            part.replace(done)
        return status

    parts = [done for _, _, done in spans]
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        if any(pool.map(one, spans)):
            return 1
    listing = out.with_name("parts.txt")
    listing.write_text("".join(f"file '{part.resolve()}'\n"
                               for part in parts), encoding="utf-8")
    return subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "concat",
                           "-safe", "0", "-i", str(listing), "-c", "copy",
                           str(out)]).returncode


def main() -> int:
    """Check the storyboards or render one.

    Returns:
        The exit status: 0 on success, 1 on a fault.
    """
    ap = argparse.ArgumentParser()
    ap.add_argument("storyboard", nargs="?",
                    help="a name under tools/video/storyboards/ or a path")
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="the MP4 file, or the directory of --stills "
                         "(default: build/video/)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--stills", metavar="T,T,...",
                    help="write one PNG per time in seconds instead of "
                         "the video")
    ap.add_argument("--music", metavar="BED,BED,...",
                    help="the bed in place of the storyboard's; with more "
                         "than one, a video per bed, the bed in its name")
    ap.add_argument("--no-voice", action="store_true",
                    help="leave the narration out")
    ap.add_argument("--jobs", type=int, default=1,
                    help="capture the frames in this many parts at once")
    ap.add_argument("--keep", type=pathlib.Path, metavar="DIR",
                    help="keep the captured parts here; a run that was "
                         "cut off continues with the parts it lacks")
    ap.add_argument("--no-sound", action="store_true",
                    help="write the video without its sound track")
    ap.add_argument("--check", action="store_true",
                    help="read every storyboard and fail on a fault")
    args = ap.parse_args()

    if args.check:
        failed = False
        for path in storyboards():
            try:
                board = json.loads(path.read_text(encoding="utf-8"))
                errors = check(board)
            except ValueError as err:
                errors = [str(err)]
            for line in errors:
                print(f"{path.name}: {line}", file=sys.stderr)
            failed = failed or bool(errors)
        return 1 if failed else 0
    for bed in (args.music or "").split(","):
        if bed and bed not in BEDS:
            ap.error(f"--music takes {', '.join(BEDS)}")
    if not args.storyboard:
        ap.error("name a storyboard: "
                 + ", ".join(p.stem for p in storyboards()))

    path = pathlib.Path(args.storyboard)
    if not path.suffix:
        path = VIDEO / "storyboards" / f"{args.storyboard}.json"
    board = load(path)
    if any(scene.get("model") == "cad" for scene in board["scenes"]) \
            and not (VIDEO / "cad" / "case.glb").is_file():
        sys.exit("tools/video/cad/case.glb is missing; tools/video_cad.py "
                 "writes it from the case's STEP file")
    for n, scene in enumerate(board["scenes"]):
        if scene["type"] != "board":
            continue
        plan = _plan(scene)
        if not plan.is_file():
            sys.exit(f"tools/video/cad/{plan.name} is missing; "
                     "tools/video_board.py writes it from the board's "
                     "KiCad file")
        faults = board_faults(scene, json.loads(
            plan.read_text(encoding="utf-8")))
        if faults:
            sys.exit("\n".join(f"{path.name}: scene {n + 1}: {plan.name} "
                               f"has {line}" for line in faults))
    if not (VIDEO / "node_modules" / "three").is_dir():
        sys.exit("run `npm install` in tools/video/ first")
    for tool in ("node",) + (() if args.stills else ("ffmpeg",)):
        if not shutil.which(tool):
            sys.exit(f"no {tool} on PATH")

    out_dir = REPO / "build" / "video"
    if args.stills:
        out = args.output or out_dir / path.stem
        out.mkdir(parents=True, exist_ok=True)
    else:
        out = args.output or out_dir / f"{path.stem}.mp4"
        out.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as td:
        shots = pathlib.Path(td)
        clips = ()
        if "voice" in board and not args.no_voice and not args.no_sound:
            import video_voice

            board, clips = video_voice.narrate(board)
        played = shots / "storyboard.json"
        played.write_text(json.dumps(board), encoding="utf-8")
        render_views(board, shots)
        handler = functools.partial(_Handler, board=played, shots=shots)
        with http.server.ThreadingHTTPServer(("127.0.0.1", 0),
                                             handler) as server:
            threading.Thread(target=server.serve_forever,
                             daemon=True).start()
            url = f"http://127.0.0.1:{server.server_address[1]}/index.html"
            sound = not args.stills and not args.no_sound
            silent = shots / "silent.mp4" if sound else out
            cmd = ["node", str(VIDEO / "player" / "capture.js"), url]
            if args.stills:
                status = subprocess.run(
                    cmd + ["stills", str(out), args.stills]).returncode
            else:
                status = _capture(cmd, board, silent, args.fps,
                                  max(1, args.jobs), args.keep)
            server.shutdown()
        beds = args.music.split(",") if args.music else [None]
        outs = [out if len(beds) == 1 else
                out.with_name(f"{out.stem}-{bed}{out.suffix}")
                for bed in beds]
        for bed, target in zip(beds, outs, strict=True):
            if status != 0 or not sound:
                break
            import video_audio

            if bed is not None:
                board["music"] = bed
            track = shots / "track.wav"
            video_audio.render(board, track, clips)
            status = subprocess.run(
                ["ffmpeg", "-v", "error", "-y", "-i", str(silent),
                 "-i", str(track), "-c:v", "copy", "-c:a", "aac",
                 "-b:a", "192k", "-shortest", "-movflags", "+faststart",
                 str(target)]).returncode
        if sound:
            out = outs[-1] if len(outs) == 1 else out.parent
    if status == 0:
        print(out)
    return 1 if status else 0


if __name__ == "__main__":
    sys.exit(main())
