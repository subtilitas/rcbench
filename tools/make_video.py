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
    CACHE: The spoken lines and drawn views a later run reuses.
    PART_FRAMES: The most frames of one part that --keep keeps.
    VIEWS_KEPT: The sets of drawn views CACHE keeps.
    PART_LEAST: The size in bytes a part must exceed to be reused.
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

# The checks and what they share with the render; MINI_UNITS, STILLS and
# the others are kept as names of this module.
from video_check import (  # noqa: F401
    BEDS, CACHE, DIAL_BASE, MINI_UNITS, PART_FRAMES, PART_LEAST, REPO, RUNS,
    STILLS, VIDEO, VIEWS_KEPT, _number, board_faults, check, still,
    still_dirs, use_stills,
)


def views(board: dict) -> dict:
    """List the renderer views a storyboard shows.

    Args:
        board: A storyboard that passed check().

    Returns:
        The shots by file name without suffix, the view name with @de or
        @en where a step sets its own language and with ~ and the versions
        where a step sets them.  Each holds the view, the screen name the
        storyboard gives or None, the language and, where a step sets them, its
        versions.
    """
    found = {}
    for scene in board["scenes"]:
        if scene["type"] == "case" and "display" in scene:
            shot = scene["display"]
            found[shot["view"]] = (shot["view"], shot.get("screen"),
                                   board["lang"])
        if scene["type"] == "board" and scene.get("enter", {}).get("view"):
            view = scene["enter"]["view"]
            found[view] = (view, scene["enter"].get("screen"), board["lang"])
        for key in scene.get("panel", []) if scene["type"] == "case" else []:
            # A run's shots are drawn by runs(), not from a view's name;
            # the mini model's are stills.
            if not key[1].startswith(("live-", "rauto-")) \
                    and scene.get("model") != "mini":
                found[key[1]] = (key[1], key[2] if len(key) > 2 else None,
                                 board["lang"])
        if scene["type"] in ("screen", "tour"):
            for step in scene["steps"]:
                if "card" in step:
                    continue
                lang = step.get("lang")
                name = f"{step['view']}@{lang}" if lang else step["view"]
                versions = step.get("versions")
                if versions:
                    name += "~" + "-".join(versions)
                found[name] = (step["view"],
                               step.get("screen", scene.get("screen")),
                               lang or board["lang"],
                               *([versions] if versions else []))
    return found


def faces(board: dict) -> list:
    """List the faces of the Dial a storyboard shows.

    Args:
        board: A storyboard that passed check().

    Returns:
        The names of tools/render_dial.py's faces, sorted.
    """
    found = {key[1] for scene in board["scenes"]
             if scene["type"] == "case"
             for key in scene.get("faces", [])
             if not key[1].startswith("live-")}
    # A RUDDER AUTO run shows the Dial its title and the bench's state.
    if any("rauto" in scene for scene in board["scenes"]):
        found |= set(RAUTO_FACES[board["lang"] == "de"])
    return sorted(found)


# The Dial's faces of a RUDDER AUTO run, SAFE and ARMED: English, German.
RAUTO_FACES = (("other-rudder-auto-safe", "other-rudder-auto-armed"),
               ("other-ruder-auto-safe", "other-ruder-auto-armed"))
RUN_HZ = 10
DETENT_DEG = 22.5


def run_value(run: dict, t: float) -> float:
    """Read a motor run's throttle or a servo run's angle at a time.

    Args:
        run: A scene's "run" of kind motor or servo.
        t: Seconds from the scene's start.

    Returns:
        The value in whole percent or degrees: 0 before the first key, the
        last key's after it, on the line between two keys, and at a key
        with a third entry, a press, that key's from its time on.
    """
    keys = run["throttle" if run.get("kind", "motor") == "motor"
               else "angle"]
    start = run.get("arm", run["from"])
    value = 0.0
    for a, b in zip([[start, 0]] + keys, keys, strict=False):
        if t >= b[0]:
            value = b[1]
        elif t > a[0] and len(b) == 2:
            value = a[1] + (b[1] - a[1]) * (t - a[0]) / (b[0] - a[0])
            break
        else:
            break
    return float(round(value))


def run_turn(run: dict) -> list:
    """Derive the knob's angle from a run.

    Args:
        run: A scene's "run".

    Returns:
        Keys [t, degrees], a detent 22.5 degrees: a detent a percent of
        throttle or a degree of horn angle, none into a key that a press
        set; on a nav run the detents of each turn, over the 0.25s before
        its time.
    """
    kind = run.get("kind", "motor")
    if kind == "nav":
        angle, keys = 0.0, [[run["from"], 0.0]]
        for at, op in run["ops"]:
            if op[:1] == "t" and int(op[1:]) != 0:
                keys.append([max(keys[-1][0] + 0.01, at - 0.25), angle])
                angle += int(op[1:]) * DETENT_DEG
                keys.append([at, angle])
        return keys
    start = run.get("arm", run["from"])
    angle, keys = 0.0, [[start, 0.0]]
    last = [start, 0]
    for key in run["throttle" if kind == "motor" else "angle"]:
        if len(key) == 2:
            if key[1] != last[1] and keys[-1][0] < last[0]:
                keys.append([last[0], angle])
            angle += (key[1] - last[1]) * DETENT_DEG
            keys.append([key[0], angle])
        last = key
    return keys


def run_frames(run: dict) -> list:
    """List the moments of a run that are drawn.

    Args:
        run: A scene's "run".

    Returns:
        (t, view, screen, the renderer's variables) per moment: a motor or
        servo run RUN_HZ times a second, a nav run at its start and at
        each gesture.
    """
    kind = run.get("kind", "motor")
    frames = []
    if kind == "nav":
        done = ["t0"]
        for at, op in [[run["from"], None]] + run["ops"]:
            if op is not None:
                done.append(op)
            frames.append((at, run["view"], run.get("screen"),
                           {"RCBENCH_VIDEO_NAV": ",".join(done)}))
        return frames
    samples = []
    count = int(round((run["to"] - run["from"]) * RUN_HZ))
    for k in range(count + 1):
        t = run["from"] + k / RUN_HZ
        if kind == "servo":
            samples.append(run_value(run, t))
            frames.append((t, "servo-live", "servo", {
                "RCBENCH_VIDEO_SERVO": ",".join(f"{v:g}" for v in samples)}))
            continue
        armed = t >= run["arm"]
        if armed:
            # The panel polls at 20Hz: two samples a frame.
            for half in (0.5, 1.0):
                samples.append(run_value(run, t - (1.0 - half) / RUN_HZ))
        frames.append((t, "motor-live", "motor", {
            "RCBENCH_VIDEO_RUN": f"{int(armed)}:"
            + ",".join(f"{v:g}" for v in samples)}))
    return frames


def runs(board: dict, shots: pathlib.Path, tmp: pathlib.Path, panel,
         dial) -> None:
    """Draw each run and give its scene the shots, faces and turn.

    A run is the panel with the Dial beside it: at each of its moments
    the panel's screen, drawn by the panel's renderer with the Dial's
    gestures applied, and the Dial's face, drawn from the packets that
    renderer writes for it.

    Args:
        board: The storyboard; its scenes with a run gain "panel", "faces"
            and "turn" keys.
        shots: The directory that takes the shots.
        tmp: A folder for the renderers' files.
        panel: The panel's renderer, from render_ui.build().
        dial: The Dial's renderer, from render_dial.build().
    """
    import os

    import render_ui
    from PIL import Image

    names = ("RCBENCH_VIDEO_RUN", "RCBENCH_VIDEO_SERVO",
             "RCBENCH_VIDEO_NAV", "RCBENCH_VIDEO_DIAL")
    packets, ppm = tmp / "dial.txt", tmp / "dial.ppm"
    known = {**render_ui.SCREENS, **render_ui.FIT_ONLY}
    for n, scene in enumerate(board["scenes"]):
        run = scene.get("run") if scene["type"] == "case" else None
        if run is None:
            continue
        scene.setdefault("panel", [])
        scene.setdefault("faces", [])
        for k, (t, view, screen, given) in enumerate(run_frames(run)):
            for name in names:
                os.environ.pop(name, None)
            os.environ.update(given)
            os.environ["RCBENCH_VIDEO_DIAL"] = str(packets)
            theme = "dark"
            if view in known:
                _, screen, theme = known[view]
            name = f"live-{n}-{k}"
            render_ui.render_one(panel, tmp, view, screen, theme,
                                 board["lang"]).save(shots / f"{name}.png")
            subprocess.run([str(dial), str(ppm), "packets", board["lang"],
                            str(packets)], check=True)
            Image.open(ppm).convert("RGB").save(shots / f"dial-{name}.png")
            scene["panel"].append([round(t, 3), name])
            scene["faces"].append([round(t, 3), name])
        for name in names:
            os.environ.pop(name, None)
        scene["panel"].sort(key=lambda key: key[0])
        scene["faces"].sort(key=lambda key: key[0])
        scene["turn"] = run_turn(run)


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
    # A storyboard of another repository brings its stills beside it.
    use_stills(path.parent / "stills")
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
    import concurrent.futures
    import os

    import render_ui

    # The MiniRcBench's screens are stills: frames of its own host suite.
    for scene in board["scenes"]:
        # A window's page is a capture of a web page, a still as well.
        for window in scene.get("windows", []):
            for key in window.get("page", {}).get("shots", []):
                shutil.copy(still(key[1]), shots / f"{key[1]}.png")
        if scene.get("model") == "mini":
            for key in scene.get("panel", []):
                shutil.copy(still(key[1]), shots / f"{key[1]}.png")
    if not shutil.which("cc"):
        sys.exit("no C compiler on PATH")
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = render_ui.build(tmp)
        known = {**render_ui.SCREENS, **render_ui.FIT_ONLY}

        def draw(item):
            n, (name, (view, screen, lang, *rest)) = item
            versions = rest[0] if rest else None
            # A view of render_ui.py's tables brings its screen and theme;
            # any other is a pose of the screen the storyboard names.
            theme = "dark"
            if view in known:
                _, screen, theme = known[view]
            if screen is None:
                return (f"view {view!r} is not in render_ui.py's tables: "
                        "the step or its scene must name the screen")
            own = tmp / f"view-{n}"
            own.mkdir()
            try:
                image = render_ui.render_one(
                    exe, own, view, screen, theme, lang,
                    env={"RCBENCH_VIDEO_VERSIONS": ",".join(versions)}
                    if versions else None)
            except subprocess.CalledProcessError:
                return (f"the renderer does not draw view {view!r} of "
                        f"screen {screen!r}")
            image.save(shots / f"{name}.png")
            return None

        # One renderer process per view, as many at once as there are
        # cores.
        with concurrent.futures.ThreadPoolExecutor(
                os.cpu_count() or 1) as pool:
            for fault in pool.map(draw, enumerate(views(board).items())):
                if fault:
                    sys.exit(fault)
        wanted = faces(board)
        live = any("run" in scene for scene in board["scenes"])
        if wanted or live:
            # The Dial's faces come from its own renderer.
            import render_dial

            unknown = [name for name in wanted
                       if name not in render_dial.FACES
                       and render_dial.other(name) is None]
            if unknown:
                sys.exit(f"render_dial.py draws no face {unknown[0]!r}")
            dial = render_dial.build(tmp)
            for name in wanted:
                render_dial.render(dial, tmp, name, board["lang"]).save(
                    shots / f"dial-{name}.png")
            runs(board, shots, tmp, exe, dial)
        rauto(board, shots, tmp)


def rauto(board: dict, shots: pathlib.Path, tmp: pathlib.Path) -> None:
    """Draw a RUDDER AUTO run and give its scene the shots and the flap.

    test/host/video_rauto.c runs the run of the host suites and writes
    the panel's screen every 500ms and the surface's angle every 100ms.
    A scene with "rauto", {at, speed}, takes the frames as shots of its
    display from "at" on, "speed" times as fast as the run, and the
    angle as its "deflect" keys.

    Args:
        board: The storyboard; its scenes with "rauto" gain "panel" and
            "deflect" keys.
        shots: The directory that takes the frames, rauto-NNNN.png.
        tmp: A directory for the run's files.
    """
    import os

    from PIL import Image

    scenes = [scene for scene in board["scenes"] if "rauto" in scene]
    if not scenes:
        return
    build = REPO / "build" / "video_host"
    quiet = {"check": True, "stdout": subprocess.DEVNULL}
    subprocess.run(["cmake", "-S", str(REPO / "test" / "host"), "-B",
                    str(build), "-DCMAKE_BUILD_TYPE=Release"], **quiet)
    subprocess.run(["cmake", "--build", str(build), "--target",
                    "video_rauto", "-j", str(os.cpu_count() or 1)], **quiet)
    out = tmp / "rauto"
    out.mkdir()
    german = board["lang"] == "de"
    subprocess.run([str(build / "video_rauto"), str(out)]
                   + (["de"] if german else []), **quiet)
    for path in sorted(out.glob("rauto-*.ppm")):
        Image.open(path).convert("RGB").save(shots / f"{path.stem}.png")
    rows = [line.split(",") for line in (out / "track.csv").read_text(
        encoding="utf-8").splitlines()[1:]]
    start = int(rows[0][1])
    for scene in scenes:
        at, speed = scene["rauto"]["at"], scene["rauto"].get("speed", 1)
        panel, deflect, frame = [], [], None
        dial, state = [], None
        for n, (shot, ms, deg, _running, armed) in enumerate(rows):
            t = round(at + (int(ms) - start) / 1000 / speed, 3)
            if armed != state:
                state = armed
                dial.append([t, RAUTO_FACES[german][int(armed)]])
            if shot != frame:
                frame = shot
                panel.append([t, f"rauto-{int(shot):04d}"])
            # A key where the angle turns: the flap moves on lines.
            edge = n in (0, len(rows) - 1) or not (
                rows[n - 1][2] == deg == rows[n + 1][2])
            if edge:
                deflect.append([t, float(deg)])
        # A touch of the run takes its time, and a hold its length.
        for touch in scene.get("touches", []):
            if "run_ms" in touch:
                touch["at"] = round(at + touch.pop("run_ms") / 1000 / speed,
                                    3)
                if touch["action"] == "hold":
                    touch["for"] = round(2 / speed, 3)
        # The Dial's face follows the bench's state through the run.
        scene["faces"] = sorted(
            [key for key in scene.get("faces", []) if key[0] < at] + dial)
        scene["panel"] = sorted(scene.get("panel", []) + panel)
        scene["deflect"] = sorted(scene.get("deflect", []) + deflect)


def _views_key(board: dict) -> str:
    """Name what render_views() draws from.

    Args:
        board: The storyboard.

    Returns:
        A hash of the storyboard and of every file under the directories
        the two renderers build from, the tools and the stills.
    """
    import hashlib

    whole = hashlib.sha256(json.dumps(board, sort_keys=True).encode())
    roots = [REPO / "shared", REPO / "protocols", REPO / "test" / "host",
             *still_dirs()]
    files = [path for root in roots for path in root.rglob("*")
             if path.is_file()]
    files += list((REPO / "tools").glob("*.py"))
    for path in sorted(files):
        whole.update(str(path.relative_to(REPO)).encode())
        whole.update(hashlib.sha256(path.read_bytes()).digest())
    return whole.hexdigest()[:20]


def cached_views(board: dict, shots: pathlib.Path,
                 cache: pathlib.Path) -> dict:
    """Run render_views() once per storyboard and source tree.

    The shots and the storyboard a run's shots extend are kept under
    @p cache in a directory named by _views_key(); a later run with the
    same key copies them back.  The VIEWS_KEPT newest directories stay.

    Args:
        board: The storyboard.
        shots: The directory that takes one PNG per view.
        cache: The directory of kept views.

    Returns:
        The storyboard as render_views() leaves it.
    """
    kept = cache / _views_key(board)
    if (kept / "storyboard.json").is_file():
        for path in kept.glob("*.png"):
            shutil.copy(path, shots / path.name)
        return json.loads((kept / "storyboard.json").read_text(
            encoding="utf-8"))
    render_views(board, shots)
    part = kept.with_suffix(".part")
    shutil.rmtree(part, ignore_errors=True)
    part.mkdir(parents=True)
    for path in shots.glob("*.png"):
        shutil.copy(path, part / path.name)
    (part / "storyboard.json").write_text(json.dumps(board),
                                          encoding="utf-8")
    part.replace(kept)
    old = sorted((path for path in cache.iterdir()
                  if path.is_dir() and path.suffix != ".part"),
                 key=lambda path: path.stat().st_mtime, reverse=True)
    for path in old[VIEWS_KEPT:]:
        shutil.rmtree(path, ignore_errors=True)
    return board


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
        if path.startswith("/plots/"):
            return str(VIDEO / "plots" / pathlib.PurePosixPath(path).name)
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


def _holds_video(path: pathlib.Path) -> bool:
    """Tell whether a captured part can be used.

    Args:
        path: The part's file.

    Returns:
        True for a file of more than PART_LEAST bytes.
    """
    return path.is_file() and path.stat().st_size > PART_LEAST


def _capture(cmd: list, board: dict, out: pathlib.Path, fps: int,
             jobs: int, keep: pathlib.Path | None,
             shots: pathlib.Path) -> int:
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
        # With a directory to keep them in, parts of at most PART_FRAMES
        # frames and frames / jobs
        # within each scene, named by what the scene's frames show: the
        # scene, its place in the video, the video's length, the steps
        # before it, its board and the player.  A change to one scene
        # that leaves every length as it was captures that scene alone.
        code = digest([path.read_text(encoding="utf-8") for path in sorted(
            (VIDEO / "player").iterdir()) if path.is_file()])
        # What the player loads besides: the pictures of the displays
        # and the models.
        seen = digest([[path.name, hashlib.sha256(
            path.read_bytes()).hexdigest()] for path in sorted(
                list(shots.glob("*.png")) + list((VIDEO / "cad").glob(
                    "*.glb")))])
        # The bed and its level are heard, not seen.
        rest = {name: value for name, value in board.items()
                if name not in ("scenes", "music", "music_gain")}
        # Parts short enough that every job has one.
        size = max(1, min(PART_FRAMES, -(-frames // jobs)))
        before = 0
        for n, (scene, (start, length)) in enumerate(
                zip(board["scenes"], times, strict=True)):
            plan = ""
            if scene["type"] == "board":
                plan = _plan(scene).read_text(encoding="utf-8")
            if scene["type"] == "scope":
                plan = (VIDEO / "plots" / f"{scene['figure']}.json"
                        ).read_text(encoding="utf-8")
            mark = digest([rest, scene, start, total, n,
                           len(board["scenes"]), before, plan, code,
                           seen])
            first = round(start * fps)
            end = frames if n == len(times) - 1 else round(
                (start + length) * fps)
            for a in range(first, end, size):
                b = min(end, a + size)
                spans.append((a, b, where / f"{mark}-{fps}-{a}-{b}.mp4"))
            for chip in scene.get("chips", []):
                if _number(chip.get("n")):
                    before = max(before, chip["n"])
            for step in scene.get("steps", []):
                before = max(before, step.get("n", 0))

    def one(span):
        a, b, done = span
        # A part without video is one whose capture died: drawn again.
        if keep is not None and _holds_video(done):
            return 0
        part = done.with_suffix(".part.mp4")
        status = subprocess.run(cmd + ["video", str(part), str(fps),
                                       str(a), str(b)]).returncode
        if status == 0 and not _holds_video(part):
            print(f"{part.name}: the capture wrote no video",
                  file=sys.stderr)
            return 1
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
    ap.add_argument("--no-cache", action="store_true",
                    help="speak every line and draw every view again, "
                         "without reading or writing build/video/cache/")
    ap.add_argument("--no-sound", action="store_true",
                    help="write the video without its sound track")
    ap.add_argument("--check", action="store_true",
                    help="read every storyboard, or the one given by its "
                    "path, and fail on a fault")
    args = ap.parse_args()

    if args.check:
        failed = False
        named = pathlib.Path(args.storyboard or "")
        # A path names one storyboard, of this or of another repository.
        for path in [named] if named.suffix else storyboards():
            try:
                board = json.loads(path.read_text(encoding="utf-8"))
                use_stills(path.parent / "stills")
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
    if any(scene.get("model") in ("cad", "duo")
           for scene in board["scenes"]) \
            and not (VIDEO / "cad" / "case.glb").is_file():
        sys.exit("tools/video/cad/case.glb is missing; tools/video_cad.py "
                 "writes it from the case's STEP file")
    if any(scene.get("model") in ("dial", "duo")
           for scene in board["scenes"]) \
            and not (VIDEO / "cad" / "m5dial.glb").is_file():
        sys.exit("tools/video/cad/m5dial.glb is missing; tools/video_cad.py "
                 "--solids writes it from "
                 "hardware/mechanical/models/parts/m5dial.step")
    if board.get("dial_base"):
        for part in DIAL_BASE:
            if not (VIDEO / "cad" / f"{part}.glb").is_file():
                sys.exit(f"tools/video/cad/{part}.glb is missing; "
                         "tools/video_cad.py --solids writes it from "
                         f"hardware/mechanical/models/*/{part}.step")
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
    if any(scene.get("model") == "mini" for scene in board["scenes"]) \
            and not (VIDEO / "cad" / "minircbench.glb").is_file():
        sys.exit("tools/video/cad/minircbench.glb is missing; "
                 "tools/video_cad.py --parts writes it from MiniRcBench's "
                 "hardware/mechanical/models/parts/minircbench.stp")
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

            board, clips = video_voice.narrate(
                board, None if args.no_cache else CACHE / "voice")
        # A run's shots add keys to its scene: drawn before the player's
        # copy of the storyboard is written.
        if args.no_cache:
            render_views(board, shots)
        else:
            board = cached_views(board, shots, CACHE / "views")
        played = shots / "storyboard.json"
        played.write_text(json.dumps(board), encoding="utf-8")
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
                                  max(1, args.jobs), args.keep, shots)
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
