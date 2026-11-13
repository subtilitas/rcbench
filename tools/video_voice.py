#!/usr/bin/env python3
"""Speak a storyboard's narration for tools/make_video.py.

The speaker is Kokoro, an open text-to-speech model of 82 million
parameters that runs on this machine through the kokoro-onnx package: no
text leaves it.  The model's two files are not in the tree.  They go
into tools/video/tts/, or the directory RCBENCH_TTS_DIR names:

    kokoro-v1.0.onnx   325,532,387 bytes
    voices-v1.0.bin     28,214,398 bytes

from github.com/thewh1teagle/kokoro-onnx, release model-files-v1.0.

A storyboard with "voice" is spoken.  A step of a screen or tour scene
says its "say", or its title and text; a chapter card its number, title
and text.  A case scene says the cues of its "say" list at their times,
and an end scene its "say".  A step that is shorter than its sentence is
stretched.  A case scene whose cue runs into the next one is stretched as
well: every time of the scene moves with the cues.

    python3 tools/video_voice.py features          # the lines, as spoken

script() needs Python only.  narrate() needs numpy, kokoro-onnx and the
model.

Attributes:
    RATE: Samples per second of the clips narrate() returns.
    LEAD_S: Seconds from a step's start to its first word.
    TAIL_S: Seconds a step lasts after its last word.
    SPELL: What is said for a name the speaker would not read as meant,
        as (pattern, replacement) in order.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import copy
import os
import pathlib
import re
import sys

RATE = 44100
LEAD_S = 0.45
TAIL_S = 0.75
SPELL = (
    (r"<[^>]+>", " "),
    (r"SEARCH \+ PROGRAM", "search and program"),
    (r"HS-5xxx and HS-7xxx", "H S 5000 and H S 7000"),
    (r"1024 x 600", "1024 by 600"),
    (r"(\d) s hold\b", r"\1 second hold"),
    (r"(\d) s\b", r"\1 seconds"),
    (r" & ", " and "),
    (r"(\d) V\b", r"\1 volts"),
    (r"(\d) mm\b", r"\1 millimetres"),
    (r"\bmm\b", "millimetres"),
    (r"(\d) GHz\b", r"\1 gigahertz"),
    (r"(\d) Hz\b", r"\1 hertz"),
    (r"(\d) Mbit/s", r"\1 megabit per second"),
    (r"°", " degrees"),
    (r"\bIOmcu\b", "I O M C U"),
    (r"\brcbench\b", "R C bench"),
    (r"\brbmod\b", "R B mod"),
    (r"\bESCape32\b", "escape 32"),
    (r"\bESCs\b", "electronic speed controllers"),
    (r"\bESC\b", "electronic speed controller"),
    (r"\bKST\b", "K S T"),
    (r"\bChaservo\b", "Cha servo"),
    (r"\bHitec\b|\bHITEC\b", "High tech"),
    (r"\bDShot\b", "D shot"),
    (r"\brpm\b", "R P M"),
    (r"\bUSB-PD\b", "U S B P D"),
    (r"\bUSB-C\b", "U S B C"),
    (r"\bPD mini\b", "P D mini"),
    (r"\bINA228\b", "I N A 228"),
    (r"\bINA3221\b", "I N A 3221"),
    (r"\bAS5600\b", "A-S 5600"),
    (r"\bESP32-C6\b", "E S P 32 C 6"),
    (r"\bESP32-P4\b", "E S P 32 P 4"),
    (r"\bRP2350\b", "R P 23 50"),
    (r"\bAM32\b", "A-M 32"),
    (r"\bBLHeli_S\b", "B L Heli S"),
    (r"\bXT60\b", "X T 60"),
    (r"\bS\.BUS\b", "S bus"),
    (r"\bWPA2\b", "W P A 2"),
    (r"\bJ1\b", "J 1"),
    (r"\bJ2\b", "J 2"),
    (r"\bPROG 5V\b", "prog 5 volt"),
    (r"\bkV\b", "K V"),
    (r"\bHV\b", "high voltage"),
    (r"\b(IMU|CSV|PNG|PWM|PPM|EDF|SD|HTTPS)\b",
     lambda m: " ".join(m.group(1))),
    (r"\bCAN bus\b", "can, bus"),
    (r"\bCAN\b", "can"),
    (r"\bZIP\b", "zip"),
    (r"\bWI-FI\b", "Wi-Fi"),
    (r"[›·]", ","),
    (r"\s+", " "),
)


def spoken(text: str) -> str:
    """Turn a caption into what the speaker is given.

    Args:
        text: A title, a text or a "say" of a storyboard.

    Returns:
        The text with markup removed, units and names written as they
        are said, and a word in capitals of 3 letters or more, a screen's
        name, in lower case with a capital.
    """
    for pattern, said in SPELL:
        text = re.sub(pattern, said, text)
    text = re.sub(r"\b[A-Z]{3,}\b", lambda m: m.group(0).capitalize(), text)
    return text.strip()


def _line(step: dict) -> str:
    if "say" in step:
        return step["say"]
    if "card" in step:
        card = step["card"]
        return f"Chapter {step['n']}. {card['title']}. {card.get('text', '')}"
    title = step["title"].rstrip(".")
    return f"{title}. {step.get('text', '')}"


def script(board: dict) -> list:
    """List what a storyboard says.

    Args:
        board: A storyboard that passed make_video.check().

    Returns:
        (scene index, step index or None, seconds from the scene's or
        the step's start, the text as spoken) per line, in order.
    """
    lines = []
    for i, scene in enumerate(board["scenes"]):
        if scene["type"] in ("case", "board"):
            for cue in scene.get("say", []):
                lines.append((i, None, cue["at"], spoken(cue["text"])))
        elif scene["type"] == "end":
            if "say" in scene:
                lines.append((i, None, 0.6, spoken(scene["say"])))
        else:
            for j, step in enumerate(scene["steps"]):
                lines.append((i, j, LEAD_S, spoken(_line(step))))
    return lines


def _stretch(scene: dict, old: list, new: list) -> None:
    def move(t):
        n = 0
        while n < len(old) - 2 and t > old[n + 1]:
            n += 1
        return round(new[n] + (t - old[n]) * (new[n + 1] - new[n])
                     / (old[n + 1] - old[n]), 2)

    scene["dur"] = move(scene["dur"])
    for key in scene["camera"]:
        key[0] = move(key[0])
    for key in scene.get("lid", []):
        key[0] = move(key[0])
    for keys in scene.get("parts", {}).values():
        for key in keys:
            key[0] = move(key[0])
    spans = scene.get("labels", []) + scene.get("chips", [])
    spans += [scene[name] for name in ("title", "pinout") if name in scene]
    for span in spans:
        span["from"], span["to"] = move(span["from"]), move(span["to"])
    if "servo" in scene:
        servo = scene["servo"]
        servo["appear"] = move(servo["appear"])
        servo["plug"] = [move(t) for t in servo["plug"]]
    for cue in scene["say"]:
        cue["at"] = move(cue["at"])


def model_dir() -> pathlib.Path:
    """Find the directory of the model's files.

    Returns:
        RCBENCH_TTS_DIR, or tools/video/tts/.
    """
    here = pathlib.Path(__file__).resolve().parent / "video" / "tts"
    return pathlib.Path(os.environ.get("RCBENCH_TTS_DIR", here))


def narrate(board: dict) -> tuple:
    """Speak a storyboard and make room for each line.

    Args:
        board: A storyboard that passed make_video.check(), with "voice".

    Returns:
        The storyboard with each step and end scene long enough for its
        line, and the clips: (seconds from the video's start, samples at
        RATE as a numpy array) per line.
    """
    try:
        import numpy as np
        from kokoro_onnx import Kokoro
    except ImportError:
        sys.exit("the narration needs numpy and kokoro-onnx: "
                 "pip install numpy kokoro-onnx")
    files = [model_dir() / "kokoro-v1.0.onnx", model_dir() / "voices-v1.0.bin"]
    for path in files:
        if not path.is_file():
            sys.exit(f"{path} is missing; tools/video_voice.py names the "
                     "release that holds it")
    voice = board["voice"]
    speaker = Kokoro(str(files[0]), str(files[1]))

    # A blend is the weighted mean of the voices' style vectors.
    style = voice.get("name")
    if "blend" in voice:
        whole = sum(voice["blend"].values())
        style = sum(speaker.get_voice_style(name) * (part / whole)
                    for name, part in voice["blend"].items())

    def say(text):
        raw, rate = speaker.create(text, voice=style,
                                   speed=voice.get("speed", 1.0),
                                   lang=voice.get("lang", "en-us"))
        raw = np.asarray(raw, dtype=float)
        # To RATE by padding the spectrum: no filter to design.
        size = int(round(len(raw) * RATE / rate))
        spec = np.fft.rfft(raw)
        wide = np.zeros(size // 2 + 1, dtype=complex)
        wide[: len(spec)] = spec[: len(wide)]
        out = np.fft.irfft(wide, size) * (size / len(raw))
        return out / max(1e-9, float(np.max(np.abs(out))))

    board = copy.deepcopy(board)
    spoken_lines = [(i, j, at, say(text)) for i, j, at, text in script(board)]
    for i, j, at, clip in spoken_lines:
        scene, need = board["scenes"][i], at + len(clip) / RATE + TAIL_S
        if j is not None:
            step = scene["steps"][j]
            step["dur"] = round(max(step["dur"], need), 2)
        elif scene["type"] == "end":
            scene["dur"] = round(max(scene["dur"], need), 2)
    # A case scene whose line runs into the next cue is stretched: the
    # cues keep their gaps or get the room their lines need, and every
    # other time of the scene follows them.
    for i, scene in enumerate(board["scenes"]):
        if scene["type"] != "case" or not scene.get("say"):
            continue
        mine = [(at, clip) for k, j, at, clip in spoken_lines if k == i]
        old = [0.0] + [at for at, _ in mine] + [scene["dur"]]
        new = [0.0, mine[0][0]]
        for n, (at, clip) in enumerate(mine):
            gap = old[n + 2] - at
            new.append(round(new[-1] + max(gap, len(clip) / RATE + 0.5), 2))
        if new != old:
            _stretch(scene, old, new)
    clips, start = [], 0.0
    for i, scene in enumerate(board["scenes"]):
        mine = [(j, clip) for k, j, _, clip in spoken_lines if k == i]
        if scene["type"] in ("screen", "tour"):
            t = start
            for (_, clip), step in zip(mine, scene["steps"], strict=True):
                clips.append((t + LEAD_S, clip))
                t += step["dur"]
            start = t
            continue
        cues = ([cue["at"] for cue in scene.get("say", [])]
                if scene["type"] == "case" else [0.6])
        for (_, clip), at in zip(mine, cues, strict=False):
            clips.append((start + at, clip))
        start += scene["dur"]
    return board, clips


def main() -> int:
    """Print what a storyboard says, one line per line.

    Returns:
        The exit status.
    """
    import make_video

    ap = argparse.ArgumentParser()
    ap.add_argument("storyboard")
    args = ap.parse_args()
    path = pathlib.Path(args.storyboard)
    if not path.suffix:
        path = make_video.VIDEO / "storyboards" / f"{args.storyboard}.json"
    for _, _, _, text in script(make_video.load(path)):
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
