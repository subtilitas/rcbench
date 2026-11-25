#!/usr/bin/env python3
"""Check a storyboard for fields the video player would not draw.

check() reads one storyboard and returns a message per fault: a scene type
the player does not know, a time outside its scene, a name of a view, a
part, a pad or a still that does not exist.  tools/make_video.py --check
runs it on every storyboard; tools/video/README.md lists the fields.
board_faults() holds a board scene to the board it draws.

Attributes:
    REPO: The repository.
    STILLS: The directory of the stills a model's display shows.  A
        storyboard of another repository brings its own in stills/
        beside it (use_stills()).
    MINI_UNITS: The components of the mini model that a scene can move.
    PART_FRAMES: The most frames of one part that --keep keeps.
    VIEWS_KEPT: The sets of drawn views the cache keeps.
    PART_LEAST: The size in bytes a part must exceed to be reused.
    CACHE: The spoken lines and drawn views a later run reuses.
    ACTIONS: The actions a touch can be.
    STYLES: The styles of a label.
    SCOPE_STYLES: The styles of a trace, a line or a band of a plot.
    TOUCH_ON: The screens a touch can be on.
    WINDOW_ITEMS: The kinds of item a window holds.
    DIAL_BASE: The two printed parts of the Dial's hand-held base.
    DIAL_UNITS: The components of the dial model that a scene can move.
    RUNS: The kinds of run a scene's panel and Dial can follow.
    VIDEO: The directory of the player, its packages and the storyboards.
    PANEL: The panel's display in pixels, the range of a step's target.
    FRAME: The video frame in pixels, the range of a label's position.
    BEDS: The music beds of tools/video_audio.py.
    UNITS: The components of the cad model that a scene can move.
    ANCHORS: The named points of the case model a label can point at.
    CONNECTORS: The headers of the case model and their position count.
    FITS: The parts a board scene solders across two pads.
    INSETS: The cross-sections a board scene shows in a card.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import json
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parent.parent
VIDEO = REPO / "tools" / "video"
CACHE = REPO / "build" / "video" / "cache"
PANEL = (1024, 600)
FRAME = (1920, 1080)
ANCHORS = ("wing", "servo", "hinge", "rod", "flap", "edge", "imu", "tape",
           "horn", "lead", "port", "display", "rbmod", "pdmini", "J1", "J2",
           "xt60", "usbc",
           "estop", "side", "plug", "panel", "case", "mcu",
           "ina", "buck", "trigger", "knob", "bezel", "usb", "power",
           "portA", "portB", "body", "dial", "glass", "board", "card",
           "buttons", "shell", "back", "ports", "esc", "tmt", "plugs",
           "screws", "cell", "boost", "keys", "stamp", "nut", "top",
           "bottom", "pad")
MINI_UNITS = ("shell", "buttons", "ina", "board", "cell", "boost",
              "plugs", "screws")
STILLS = VIDEO / "stills"
_still_dirs = [STILLS]


def use_stills(directory: pathlib.Path | None = None) -> None:
    """Name the directory of a storyboard's own stills.

    Args:
        directory: A directory searched before STILLS, such as stills/
            beside a storyboard of another repository; None, or a path
            that is no directory, leaves STILLS alone.
    """
    _still_dirs[:] = ([directory] if directory is not None
                      and directory.is_dir() else []) + [STILLS]


def still(name: str) -> pathlib.Path | None:
    """Find a still.

    Args:
        name: Its name without the suffix.

    Returns:
        NAME.png in the storyboard's own stills or in STILLS, or None.
    """
    for directory in _still_dirs:
        path = directory / f"{name}.png"
        if path.is_file():
            return path
    return None


def still_dirs() -> list:
    """List the directories still() searches.

    Returns:
        The storyboard's own stills, where named, then STILLS.
    """
    return list(_still_dirs)
UNITS = ("lid", "estop", "xt60", "usbc", "pdmini", "mcu", "ina",
         "buck", "trigger", "rbmod", "case")
CONNECTORS = {"J1": 4, "J2": 4}
BEDS = ("pads", "bach-prelude", "pachelbel-canon", "vivaldi-spring",
        "bach-cello", "beethoven-moonlight")
PART_FRAMES = 900
VIEWS_KEPT = 4
PART_LEAST = 1024
ACTIONS = ("tap", "hold", "press", "down", "release")
STYLES = ("ok", "warn", "muted")
SCOPE_STYLES = ("", "ok", "warn", "muted", "red", "white", "violet")
FITS = ("resistor", "zener", "cap", "link")
INSETS = ("strip", "fill", "extract", "pins", "latch")


RUNS = ("motor", "servo", "nav")


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


def _check_run(scene: dict, dur: float, where: str, errors: list) -> None:
    run = scene["run"]
    kind = run.get("kind", "motor") if isinstance(run, dict) else None
    if kind not in RUNS or scene.get("model") not in ("dial", "duo",
                                                      "rudder"):
        errors.append(f"{where} run: needs the dial, duo or rudder model "
                      f"and a kind, one of {', '.join(RUNS)}")
        return
    if not (_number(run.get("from")) and _number(run.get("to"))
            and 0 <= run["from"] < run["to"] <= dur):
        errors.append(f"{where} run: from and to lie in the scene, in order")
        return
    keys = run.get({"motor": "throttle", "servo": "angle",
                    "nav": "ops"}[kind])
    rising = (isinstance(keys, list) and keys
              and all(isinstance(k, list) and len(k) >= 2
                      and _number(k[0]) for k in keys)
              and all(a[0] < b[0] for a, b in zip(keys, keys[1:],
                                                  strict=False)))
    if not rising:
        errors.append(f"{where} run: its keys are [t, ...] with rising t")
    elif kind == "nav":
        if not (isinstance(run.get("view"), str)
                and all(len(k) == 2 and isinstance(k[1], str)
                        and (k[1] in ("p", "h")
                             or (k[1][:1] == "t"
                                 and k[1][1:].lstrip("-").isdigit()))
                        for k in keys)):
            errors.append(f"{where} run: nav needs a view and ops [t, op], "
                          "an op being p, h or t and a count of detents")
    elif not all(len(k) in (2, 3) and _number(k[1]) for k in keys):
        errors.append(f"{where} run: a key is [t, value]")
    elif kind == "motor" and not (
            _number(run.get("arm"))
            and run["from"] <= run["arm"] < run["to"]
            and all(0 <= k[1] <= 100 for k in keys)):
        errors.append(f"{where} run: motor needs arm between from and to "
                      "and a throttle of 0 to 100")


TOUCH_ON = {"panel": ((1024, 600), ("mockup", "cad", "duo", "rudder")),
            "dial": ((240, 240), ("dial", "duo", "rudder"))}


def _check_touch(scene: dict, touch, where: str, errors: list) -> None:
    """Check one touch of a 3D scene.

    Args:
        scene: The case scene.
        touch: {on, xy, action} with "at", seconds into the scene, or
            "run_ms", milliseconds into the scene's rauto run.
        where: The touch's name for a message.
        errors: The list that takes the messages.
    """
    if not isinstance(touch, dict) or touch.get("on") not in TOUCH_ON:
        errors.append(f"{where}: on must be panel or dial")
        return
    (w, h), models = TOUCH_ON[touch["on"]]
    xy = touch.get("xy")
    if scene.get("model", "mockup") not in models:
        errors.append(f"{where}: the model shows no {touch['on']}")
    if not (isinstance(xy, list) and len(xy) == 2 and all(map(_number, xy))
            and 0 <= xy[0] <= w and 0 <= xy[1] <= h):
        errors.append(f"{where}: xy is a pixel of the {w} x {h} screen")
    if touch.get("action") not in ("tap", "hold"):
        errors.append(f"{where}: action must be tap or hold")
    if "run_ms" in touch:
        if not (_number(touch["run_ms"]) and touch["run_ms"] >= 0
                and "rauto" in scene and "at" not in touch):
            errors.append(f"{where}: run_ms is a time of the scene's rauto "
                          "run, in place of at")
    elif not (_number(touch.get("at")) and 0 <= touch["at"] < scene["dur"]):
        errors.append(f"{where}: at must lie within the scene")
    if "for" in touch and not (_number(touch["for"]) and touch["for"] > 0
                               and touch.get("action") == "hold"):
        errors.append(f"{where}: for is a hold's time in s, above 0")


def _check_case(scene: dict, where: str, errors: list) -> None:
    dur = scene.get("dur")
    if not (_number(dur) and dur > 0):
        errors.append(f"{where}: dur must be a positive number")
        return
    _keys(scene.get("camera"), 3, f"{where} camera", errors)
    if scene.get("model", "mockup") not in ("mockup", "cad", "dial", "duo",
                                            "mini", "part", "rudder"):
        errors.append(f"{where}: model must be mockup, cad, dial, duo, "
                      "mini, part or rudder")
    if scene.get("model") == "part" and not (
            isinstance(scene.get("part"), str) and scene["part"]
            and all(c.isalnum() or c in "-_" for c in scene["part"])):
        errors.append(f"{where}: part names a model under tools/video/cad/, "
                      "without its suffix")
    known = set()
    for n, window in enumerate(scene.get("windows", [])):
        _check_window(window, dur, f"{where} window {n + 1}", errors, known)
    mini = scene.get("model") == "mini"
    auto = scene.get("rauto")
    if auto is not None and not (
            scene.get("model") == "rudder" and isinstance(auto, dict)
            and _number(auto.get("at")) and 0 <= auto["at"] < dur
            and _number(auto.get("speed", 1)) and auto.get("speed", 1) > 0):
        errors.append(f"{where}: rauto needs the rudder model, at within "
                      "the scene and a speed above 0")
    for name in ("faces", "turn", "flip", "spin", "deflect"):
        if name in scene:
            _keys(scene[name], 2, f"{where} {name}", errors)
    for n, touch in enumerate(scene.get("touches", [])):
        _check_touch(scene, touch, f"{where} touch {n + 1}", errors)
    for key in scene.get("faces", []):
        if not (isinstance(key, list) and len(key) == 2
                and isinstance(key[1], str)):
            errors.append(f"{where} faces: a key is [t, the face's name]")
            break
    last = -1.0
    for key in scene.get("panel", []):
        if not (isinstance(key, list) and len(key) in (2, 3)
                and _number(key[0]) and key[0] > last
                and all(isinstance(v, str) for v in key[1:])):
            errors.append(f"{where} panel: a key is [t, view] or "
                          "[t, view, screen] with rising t")
            break
        last = key[0]
        if mini and still(key[1]) is None:
            errors.append(f"{where} panel: no still {key[1]}.png beside "
                          "the storyboard or under tools/video/stills")
    if "run" in scene:
        _check_run(scene, dur, where, errors)
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
        pixel = (isinstance(anchor, dict) and list(anchor) == ["pixel"]
                 and isinstance(anchor["pixel"], list)
                 and len(anchor["pixel"]) == 2
                 and all(map(_number, anchor["pixel"]))
                 and 0 <= anchor["pixel"][0] <= 1024
                 and 0 <= anchor["pixel"][1] <= 600
                 and scene.get("model", "mockup") in TOUCH_ON["panel"][1])
        if not (pixel or (isinstance(anchor, str) and anchor in ANCHORS)
                or (isinstance(anchor, list) and len(anchor) == 3)):
            errors.append(f"{at}: anchor must be [x, y, z] in mm, "
                          "{pixel: [x, y]} of the 1024 x 600 display or "
                          f"one of {', '.join(ANCHORS)}")
        pos = label.get("at")
        if not (isinstance(pos, list) and len(pos) == 2
                and 0 <= pos[0] < FRAME[0] and 0 <= pos[1] < FRAME[1]):
            errors.append(f"{at}: at must be [x, y] inside the frame")
        if not isinstance(label.get("title"), str):
            errors.append(f"{at}: title is missing")
        if label.get("style", "ok") not in STYLES:
            errors.append(f"{at}: style must be one of {', '.join(STYLES)}")
    for name, keys in scene.get("parts", {}).items():
        model = scene.get("model")
        known = MINI_UNITS if mini else DIAL_UNITS if model == "dial" else (
            "imu", "tape", "skin", "lead", "stand", "tip") \
            if model == "rudder" else UNITS
        if name not in known:
            errors.append(f"{where} parts: {name} is not one of "
                          f"{', '.join(known)}")
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
    tilt = scene.get("tilt")
    if tilt is not None and not (
            isinstance(tilt, list) and len(tilt) == 2
            and all(_number(v) and -45 <= v <= 45 for v in tilt)):
        errors.append(f"{where}: tilt is [rx, ry] in degrees, each from "
                      "-45 to 45")
    enter = scene.get("enter")
    if enter is not None and not (
            isinstance(enter, dict) and _within(enter.get("t"), dur)
            and isinstance(enter.get("view", ""), str)):
        errors.append(f"{where}: enter needs t, [t0, t1] within the scene, "
                      "and takes a view")
    known = set()
    for n, window in enumerate(scene.get("windows", [])):
        _check_window(window, dur, f"{where} window {n + 1}", errors, known)


def _within(value, dur: float) -> bool:
    return (isinstance(value, list) and len(value) == 2
            and all(_number(v) for v in value)
            and 0 <= value[0] < value[1] <= dur)


WINDOW_ITEMS = ("h", "p", "log", "file", "button", "bar", "drive", "dialog")


def _check_window(window: dict, dur: float, where: str,
                  errors: list, known: set) -> None:
    if isinstance(window, dict) and window.get("kind") == "desktop":
        if not (_within([window.get("from"), window.get("to")], dur)
                and _place(window.get("at")) and all(
                    _number(window.get(k)) and window[k] > 0
                    for k in ("w", "h"))):
            errors.append(f"{where}: a desktop needs from, to, at, w and h")
        return
    if not (isinstance(window, dict)
            and _within([window.get("from"), window.get("to")], dur)
            and _place(window.get("at")) and isinstance(window["at"], list)
            and _number(window.get("w")) and 200 <= window["w"] <= 1920
            and window.get("kind", "browser") in ("browser", "folder",
                                                  "explorer")
            and isinstance(window.get("url") or window.get("title"), str)):
        errors.append(f"{where}: needs from and to within the scene, at, "
                      "[x, y], w from 200 to 1920, kind browser, folder or "
                      "explorer, "
                      "and a url or a title")
        return
    page = window.get("page")
    if page is not None:
        shots = page.get("shots") if isinstance(page, dict) else None
        if not (_number(page.get("w") if isinstance(page, dict) else None)
                and isinstance(shots, list) and shots and all(
                    isinstance(k, list) and len(k) == 2 and _number(k[0])
                    and isinstance(k[1], str) for k in shots)):
            errors.append(f"{where}: page needs w, the page's width, and "
                          "shots, keys [t, name]")
            return
        for key in shots:
            if still(key[1]) is None:
                errors.append(f"{where}: no still {key[1]}.png beside the "
                              "storyboard or under tools/video/stills")
    ids = set(known) | {r["id"] for r in window.get("rows", [])
                        if isinstance(r, dict) and "id" in r}
    if "id" in window:
        ids.add(window["id"])
    known.update(ids)
    for item in window.get("side", []) + window.get("body", []):
        kinds = [k for k in WINDOW_ITEMS if isinstance(item, dict)
                 and k in item]
        if len(kinds) != 1:
            errors.append(f"{where}: an item is one of "
                          f"{', '.join(WINDOW_ITEMS)}")
            continue
        if "id" in item:
            ids.add(item["id"])
            known.add(item["id"])
        for key in ("get", "bar"):
            if key in item and not _within(item[key], dur):
                errors.append(f"{where}: {key} is [t0, t1] within the "
                              "scene")
        if kinds[0] == "dialog":
            rows = item.get("rows")
            if not (isinstance(rows, list) and rows and all(
                    isinstance(r, dict) and isinstance(r.get("text"), str)
                    for r in rows)):
                errors.append(f"{where}: a dialog needs rows, each with "
                              "text")
                continue
            ids.update(r["id"] for r in rows if "id" in r)
    last = 0.0
    for key in window.get("pointer", []):
        if not (isinstance(key, list) and len(key) in (2, 3)
                and _number(key[0]) and last <= key[0] <= dur
                and (isinstance(key[1], list) and page is not None
                     and _place(key[1])
                     or isinstance(key[1], str) and key[1] in ids)
                and (len(key) == 2 or key[2] in ("click", "grab", "drop"))):
            errors.append(f"{where} pointer: a key is [t, id] or [t, id, "
                          "op] with rising t, an item's id and op click, "
                          "grab or drop")
            continue
        last = key[0]


def _check_scope(scene: dict, where: str, errors: list) -> None:
    dur = scene.get("dur")
    if not (_number(dur) and dur > 0):
        errors.append(f"{where}: dur must be a positive number")
        return
    name = scene.get("figure")
    path = VIDEO / "plots" / f"{name}.json"
    if not (isinstance(name, str) and name
            and all(c.isalnum() or c in "-_" for c in name)
            and path.is_file()):
        errors.append(f"{where}: figure names a file under "
                      "tools/video/plots/, without its suffix")
        return
    figure = json.loads(path.read_text(encoding="utf-8"))
    if "title" in scene:
        _span(scene["title"], dur, f"{where} title", errors)
        if not isinstance(scene["title"].get("heading"), str):
            errors.append(f"{where} title: heading is missing")
    for n, chip in enumerate(scene.get("chips", [])):
        _span(chip, dur, f"{where} chip {n + 1}", errors)
    panels = scene.get("panels")
    if not (isinstance(panels, list) and panels
            and all(isinstance(p, dict) for p in panels)):
        errors.append(f"{where}: panels is a list of plots and circuits")
        return
    known = {}
    for n, panel in enumerate(panels):
        at = f"{where} panel {n + 1}"
        box = panel.get("box")
        if not (isinstance(box, list) and len(box) == 4
                and all(_number(v) for v in box) and box[0] >= 0
                and box[1] >= 0 and box[2] > 0 and box[3] > 0
                and box[0] + box[2] <= FRAME[0]
                and box[1] + box[3] <= FRAME[1]):
            errors.append(f"{at}: box must be [x, y, w, h] inside the frame")
        if not isinstance(panel.get("id"), str) or panel["id"] in known:
            errors.append(f"{at}: id must be a name of its own")
            continue
        if "from" in panel or "to" in panel:
            _span(panel, dur, at, errors)
        if panel.get("kind") == "plot":
            known[panel["id"]] = None
            _check_plot(panel, figure, dur, at, errors)
        elif panel.get("kind") == "circuit":
            drawn = figure.get("circuits", {}).get(panel.get("circuit"))
            if drawn is None:
                errors.append(f"{at}: circuit is not in the figure")
                continue
            known[panel["id"]] = drawn
            _check_circuit(panel, drawn, dur, at, errors)
        else:
            errors.append(f"{at}: kind must be plot or circuit")
    for n, label in enumerate(scene.get("labels", [])):
        at = f"{where} label {n + 1}"
        _span(label, dur, at, errors)
        anchor = label.get("anchor")
        fine = isinstance(anchor, list) and len(anchor) in (2, 3)
        if fine and isinstance(anchor[0], str):
            drawn = known.get(anchor[0], False)
            fine = ((len(anchor) == 3 and drawn is None
                     and _number(anchor[1]) and _number(anchor[2]))
                    or (len(anchor) == 2 and isinstance(drawn, dict)
                        and anchor[1] in drawn["nodes"]))
        elif fine:
            fine = len(anchor) == 2 and all(_number(v) for v in anchor)
        if not fine:
            errors.append(f"{at}: anchor must be [x, y], [plot, x, y] or "
                          "[circuit, node]")
        pos = label.get("at")
        if not (isinstance(pos, list) and len(pos) == 2
                and 0 <= pos[0] < FRAME[0] and 0 <= pos[1] < FRAME[1]):
            errors.append(f"{at}: at must be [x, y] inside the frame")
        if not isinstance(label.get("leader", True), bool):
            errors.append(f"{at}: leader must be true or false")
        if not isinstance(label.get("title"), str):
            errors.append(f"{at}: title is missing")
        if label.get("style", "ok") not in STYLES:
            errors.append(f"{at}: style must be one of {', '.join(STYLES)}")
    _check_say(scene, dur, where, errors)


def _check_plot(panel: dict, figure: dict, dur: float, at: str,
                errors: list) -> None:
    view = panel.get("view")
    _keys(view, 2, f"{at} view", errors)
    for key in view if isinstance(view, list) else []:
        if not (isinstance(key, list) and len(key) == 2
                and isinstance(key[1], list) and len(key[1]) == 2
                and all(_number(v) for v in key[1]) and key[1][0] < key[1][1]):
            errors.append(f"{at} view: a key is [t, [x0, x1]] in ns, in "
                          "order")
            break
    y = panel.get("y")
    if not (isinstance(y, list) and len(y) == 2
            and all(_number(v) for v in y) and y[0] < y[1]):
        errors.append(f"{at}: y must be [low, high]")
    traces = panel.get("traces")
    if not (isinstance(traces, list) and traces):
        errors.append(f"{at}: traces is empty")
        return
    for n, trace in enumerate(traces):
        name = f"{at} trace {n + 1}"
        if trace.get("series") not in figure["series"]:
            errors.append(f"{name}: series is not in the figure")
        if "on" in trace and trace["on"] not in figure["series"]:
            errors.append(f"{name}: on is not a series of the figure")
        if trace.get("style", "") not in SCOPE_STYLES:
            errors.append(f"{name}: style must be one of "
                          f"{', '.join(SCOPE_STYLES[1:])}")
        if "from" in trace or "to" in trace:
            _span(trace, dur, name, errors)
        if "reveal" in trace:
            _keys(trace["reveal"], 2, f"{name} reveal", errors)
    for kind in ("lines", "bands"):
        for n, item in enumerate(panel.get(kind, [])):
            name = f"{at} {kind[:-1]} {n + 1}"
            if "from" in item or "to" in item:
                _span(item, dur, name, errors)
            if item.get("style", "") not in SCOPE_STYLES:
                errors.append(f"{name}: style must be one of "
                              f"{', '.join(SCOPE_STYLES[1:])}")
            if kind == "bands" and not (
                    isinstance(item.get("x"), list) and len(item["x"]) == 2
                    and all(_number(v) for v in item["x"])):
                errors.append(f"{name}: x must be [x0, x1]")
            if kind == "lines" and _number(item.get("x")) == _number(
                    item.get("y")):
                errors.append(f"{name}: needs x or y")


def _check_circuit(panel: dict, drawn: dict, dur: float, at: str,
                   errors: list) -> None:
    parts = {item["id"] for item in drawn["items"]}
    for name in ("show", "set", "lit"):
        for part, keys in panel.get(name, {}).items():
            if part not in parts:
                errors.append(f"{at} {name}: {part} is not a part of the "
                              "circuit")
            _keys(keys, 2, f"{at} {name} {part}", errors)
    for n, flow in enumerate(panel.get("flow", [])):
        _span(flow, dur, f"{at} flow {n + 1}", errors)
        if flow.get("path") not in drawn["paths"]:
            errors.append(f"{at} flow {n + 1}: path is not in the circuit")
    for n, meter in enumerate(panel.get("meters", [])):
        _span(meter, dur, f"{at} meter {n + 1}", errors)
        if meter.get("node") not in drawn["nodes"]:
            errors.append(f"{at} meter {n + 1}: node is not in the circuit")
        _keys(meter.get("value"), 2, f"{at} meter {n + 1} value", errors)


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
        # A press lands 0.4s before its step ends; a release lifts 0.9s
        # into its step.
        least = {"tap": 1.4, "hold": 3.1, "press": 1.4,
                 "release": 1.4}.get(action, 0.9)
        if not (_number(step.get("dur")) and step["dur"] >= least):
            errors.append(f"{at}: dur must be at least {least}s")
        if not (isinstance(step.get("n"), int) and 1 <= step["n"] <= steps):
            errors.append(f"{at}: n must be a step from 1 to {steps}")
        target = step.get("target")
        if target is None and action is not None:
            errors.append(f"{at}: a {action} needs a target")
        for name in ("target", "point"):
            place = step.get(name)
            if place is not None and not (
                    isinstance(place, list) and len(place) == 2
                    and 0 <= place[0] < PANEL[0]
                    and 0 <= place[1] < PANEL[1]):
                errors.append(f"{at}: {name} must be [x, y] on the "
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
        versions = step.get("versions")
        if versions is not None and not (
                isinstance(versions, list) and len(versions) == 3
                and all(isinstance(v, str)
                        and re.fullmatch(r"\d{1,3}\.\d{1,3}\.\d{1,3}", v)
                        for v in versions)):
            errors.append(f"{at}: versions must be three of the form "
                          "X.Y.Z: running, newer, older")
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


#: The two printed parts of the Dial's hand-held base, in tools/video/cad/.
DIAL_BASE = ("dial_base_top", "dial_base_bottom", "m5dial_nut")
#: What stands apart in a scene of the Dial in its base.
DIAL_UNITS = ("upper", "dial", "nut", "cell", "pad", "screws")


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
    if not isinstance(board.get("dial_base", False), bool):
        errors.append("dial_base is true or false")
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
        elif kind == "scope":
            _check_scope(scene, where, errors)
        elif kind == "screen":
            _check_screen(scene, steps, where, errors)
        elif kind == "tour":
            _check_tour(scene, steps, where, errors)
        elif kind == "end":
            if not (_number(scene.get("dur")) and scene["dur"] > 0
                    and isinstance(scene.get("heading"), str)):
                errors.append(f"{where}: needs dur and heading")
        else:
            errors.append(f"{where}: type must be case, board, scope, "
                          "screen, tour or end")
    return errors
