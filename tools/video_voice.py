#!/usr/bin/env python3
"""Speak a storyboard's narration for tools/make_video.py.

The speaker is Kokoro, an open text-to-speech model of 82 million
parameters that runs on this machine through the kokoro-onnx package: no
text leaves it.  The model's two files are not in the tree.  They go
into tools/video/tts/, or the directory RCBENCH_TTS_DIR names:

    kokoro-v1.0.onnx   325,532,387 bytes
    voices-v1.0.bin     28,214,398 bytes

from github.com/thewh1teagle/kokoro-onnx, release model-files-v1.0.

A "voice" with "engine": "piper" is spoken by Piper through the
piper-tts package, by the voice its "name" names, de_DE-thorsten-high
for German.  Its two files go into the same directory:

    de_DE-thorsten-high.onnx        113,895,201 bytes
    de_DE-thorsten-high.onnx.json         4,875 bytes

from huggingface.co/rhasspy/piper-voices, de/de_DE/thorsten/high.  A
storyboard with "lang": "de" is written out for a German speaker by
SPELL_DE in place of SPELL.

Kokoro reads phonemes.  For a voice with "lang" en-us or en-gb they come
from misaki, the grapheme-to-phoneme converter Kokoro was trained with:
its American or its British dictionary, and espeak-ng for a word the
dictionary does not hold.  Any other "lang" is phonemized by kokoro-onnx
through espeak-ng.  A word ending in n't keeps its full vowel ("isn't"
as ˈɪzənt, not ˈɪzᵊnt), since Kokoro drops the reduced one, and a
letter said on its own keeps the full stop after it ("I M U.").

Every clip, Kokoro's and Piper's, is cut to its sound: the samples
before the first and after the last one above TRIM_DB of the clip's
peak go, except TRIM_LEAD_S before and TRIM_TAIL_S after.

A storyboard with "voice" is spoken.  A step of a screen or tour scene
says its "say", or its title and text; a chapter card its number, title
and text.  A case scene says the cues of its "say" list at their times,
a board scene the same, and an end scene its "say".  A step that is
shorter than its sentence is stretched.  A case or board scene whose cue
runs into the next one is stretched as well: every time of the scene
moves with the cues.

    python3 tools/video_voice.py features            # the lines, as spoken
    python3 tools/video_voice.py features --unknown  # words misaki lacks

script() needs Python only.  narrate() needs numpy, kokoro-onnx, misaki
0.9.4 without its "en" extra, spacy with en_core_web_sm 3.8.0, num2words,
espeakng-loader and phonemizer, and the model.  --unknown needs
misaki and spacy.

Attributes:
    RATE: Samples per second of the clips narrate() returns.
    LEAD_S: Seconds from a step's start to its first word.
    TAIL_S: Seconds a step lasts after its last word.
    TRIM_DB: The level, in dB relative to a clip's peak, below which the
        ends of a clip count as silence.
    TRIM_LEAD_S: Seconds a clip keeps before its first sample above
        TRIM_DB.
    TRIM_TAIL_S: Seconds a clip keeps after its last sample above
        TRIM_DB.
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
TRIM_DB = -40.0
TRIM_LEAD_S = 0.02
TRIM_TAIL_S = 0.04
# The voices' "lang" values misaki reads, and whether its British
# dictionary does.
MISAKI = {"en-us": False, "en-gb": True}
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
    # "Chaservo servo" would be said "Cha servo servo".
    (r"\bChaservo( servo)?(s?)\b", r"Cha servo\2"),
    (r"\bHitec\b|\bHITEC\b", "High tech"),
    (r"\bDShot\b", "D shot"),
    (r"\brpm\b", "R P M"),
    (r"\bUSB-PD\b", "U S B P D"),
    (r"\bUSB-C\b", "U S B C"),
    (r"\bPD mini\b", "P D mini"),
    # Said letter by letter only when written whole and in capitals.
    (r"\bINA219\b", "INA 2 19"),
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
    # misaki's dictionary lacks "kilohm" and its fallback says "kai-lohm".
    (r"\b[Kk]ilohm(s?)\b", r"kilo ohm\1"),
    (r"\bCAN bus\b", "can, bus"),
    (r"\bCAN\b", "can"),
    (r"\bZIP\b", "zip"),
    (r"\bWI-FI\b", "Wi-Fi"),
    (r"[›·]", ","),
    (r"\s+", " "),
)

# What a German speaker is given, as SPELL for an English one.
SPELL_DE = (
    (r"<[^>]+>", " "),
    (r"(\d),(\d)", r"\1 Komma \2"),
    (r"(\d) ?µs\b|(\d) ?us\b", r"\1\2 Mikrosekunden"),
    (r"(\d) ?s\b", r"\1 Sekunden"),
    (r"(\d) ?mm\b", r"\1 Millimeter"),
    (r"(\d) ?V\b", r"\1 Volt"),
    (r"(\d) ?%", r"\1 Prozent"),
    (r"°", " Grad"),
    (r" & ", " und "),
    (r"\bIOmcu\b", "I O M C U"),
    (r"\brcbench\b|\bRCBench\b", "R C Bench"),
    (r"\bKST\b", "K S T"),
    (r"\bChaservo(-Servo)?(s?)\b", r"Tscha-Servo\2"),
    (r"\bHitec\b|\bHITEC\b", "Hai-Teck"),
    (r"\bX10\b", "X 10"),
    (r"\bM5Stack Dial\b", "M 5 Stack Dail"),
    (r"\bDial\b", "Dail"),
    (r"\bRUDER AUTO\b|\bRuder Auto\b", "Ruder Auto"),
    (r"\bARM \+ START\b", "Arm und Start"),
    (r"\bSETUP\b|\bSetup\b", "Sett-ap"),
    (r"\bLIVE\b|\bLive\b", "Laif"),
    (r"\bRESERVE\b|\bReserve\b", "Resärwe"),
    (r"\bGecko Tape\b", "Gekko Tehp"),
    (r"\bSchubstange\b", "Schub Stange"),
    (r"\b(IMU|PWM|SD|USB)\b", lambda m: " ".join(m.group(1))),
    (r"[›·]", ","),
    (r"\s+", " "),
)

# Words in capitals the speaker says letter by letter.
LETTERS = ("INA",)


def spoken(text: str, lang: str = "en") -> str:
    """Turn a caption into what the speaker is given.

    Args:
        text: A title, a text or a "say" of a storyboard.
        lang: The storyboard's language, "en" or "de".

    Returns:
        The text with markup removed, units and names written as they
        are said, and a word in capitals of 3 letters or more, a screen's
        name, in lower case with a capital.
    """
    for pattern, said in SPELL_DE if lang == "de" else SPELL:
        text = re.sub(pattern, said, text)
    text = re.sub(r"\b[A-Z]{3,}\b", lambda m: m.group(0) if m.group(0)
                  in LETTERS else m.group(0).capitalize(), text)
    return text.strip()


def _line(step: dict) -> str:
    if "say" in step:
        return step["say"]
    if "card" in step:
        card = step["card"]
        word = "Kapitel" if step.get("lang") == "de" else "Chapter"
        return f"{word} {step['n']}. {card['title']}. {card.get('text', '')}"
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
    lines, lang = [], board.get("lang", "en")
    for i, scene in enumerate(board["scenes"]):
        if scene["type"] in ("case", "board", "scope"):
            for cue in scene.get("say", []):
                lines.append((i, None, cue["at"], spoken(cue["text"], lang)))
        elif scene["type"] == "end":
            if "say" in scene:
                lines.append((i, None, 0.6, spoken(scene["say"], lang)))
        else:
            for j, step in enumerate(scene["steps"]):
                lines.append((i, j, LEAD_S, spoken(
                    _line({**step, "lang": lang}), lang)))
    return lines


def _stretch_scope(scene: dict, move) -> None:
    """Move every time of a scope scene.

    Args:
        scene: The scene; its dur is moved already.
        move: Gives the new time of an old one.
    """
    def span(item):
        if "from" in item:
            item["from"], item["to"] = move(item["from"]), move(item["to"])

    def keys(track):
        for key in track:
            key[0] = move(key[0])

    for item in scene.get("labels", []) + scene.get("chips", []):
        span(item)
    if "title" in scene:
        span(scene["title"])
    for panel in scene["panels"]:
        span(panel)
        if "view" in panel:
            keys(panel["view"])
        for name in ("traces", "lines", "bands", "flow", "meters"):
            for item in panel.get(name, []):
                span(item)
                for track in ("reveal", "value"):
                    if track in item:
                        keys(item[track])
        for name in ("show", "set", "lit"):
            for track in panel.get(name, {}).values():
                keys(track)


def _stretch(scene: dict, old: list, new: list) -> None:
    def move(t):
        n = 0
        while n < len(old) - 2 and t > old[n + 1]:
            n += 1
        # A first cue at 0 leaves no time before it.
        if old[n + 1] == old[n]:
            return round(new[n], 2)
        return round(new[n] + (t - old[n]) * (new[n + 1] - new[n])
                     / (old[n + 1] - old[n]), 2)

    scene["dur"] = move(scene["dur"])
    if scene["type"] == "scope":
        _stretch_scope(scene, move)
        return
    for key in scene["camera"]:
        key[0] = move(key[0])
    for name in ("lid", "faces", "turn", "panel", "flip", "spin", "deflect"):
        for key in scene.get(name, []):
            key[0] = move(key[0])
    if "rauto" in scene:
        scene["rauto"]["at"] = move(scene["rauto"]["at"])
    for touch in scene.get("touches", []):
        if "at" in touch:
            touch["at"] = move(touch["at"])
    if "run" in scene:
        run = scene["run"]
        for name in ("from", "arm", "to"):
            if name in run:
                run[name] = move(run[name])
        for name in ("throttle", "angle", "ops"):
            for key in run.get(name, []):
                key[0] = move(key[0])
    for keys in scene.get("parts", {}).values():
        for key in keys:
            key[0] = move(key[0])
    spans = scene.get("labels", []) + scene.get("chips", [])
    spans += [scene[name] for name in ("title", "pinout") if name in scene]
    # A board scene: its tags, nets and insets, the spans of its cuts,
    # wires and joints, and the times its parts take their place.
    for name in ("tags", "nets", "insets"):
        spans += scene.get(name, [])
    for span in spans:
        span["from"], span["to"] = move(span["from"]), move(span["to"])
    for name in ("cuts", "wires", "solder"):
        for item in scene.get(name, []):
            item["t"] = [move(t) for t in item["t"]]
            if "gone" in item:
                item["gone"] = [move(t) for t in item["gone"]]
    for name in ("fit", "mount", "plugs"):
        for item in scene.get(name, []):
            item["t"] = move(item["t"])
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


def trim(clip, rate: int, db: float = TRIM_DB, lead: float = TRIM_LEAD_S,
         tail: float = TRIM_TAIL_S):
    """Cut the silence at both ends of a clip.

    Args:
        clip: The samples as a numpy array.
        rate: Their samples per second.
        db: The level, relative to the clip's peak, a sample has to
            exceed to count as sound.
        lead: Seconds kept before the first such sample.
        tail: Seconds kept after the last one.

    Returns:
        The clip from @p lead before its first sound to @p tail after its
        last; a clip without a sample above zero is returned whole.
    """
    import numpy as np

    level = np.abs(np.asarray(clip))
    peak = float(level.max()) if len(level) else 0.0
    if peak <= 0.0:
        return clip
    loud = np.flatnonzero(level > peak * 10 ** (db / 20))
    start = max(0, int(loud[0]) - int(round(lead * rate)))
    end = min(len(clip), int(loud[-1]) + 1 + int(round(tail * rate)))
    return clip[start:end]


def phonemes(text: str, g2p) -> str:
    """Turn a line into the phonemes Kokoro reads.

    Args:
        text: The line as spoken(), in English.
        g2p: A misaki en.G2P, or a callable like it that returns the
            phoneme string and the tokens with their "text", "phonemes"
            and "whitespace".

    Returns:
        The tokens' phonemes joined with their whitespace; a word ending
        in n't has the full vowel ə in place of the reduced ᵊ, a letter
        said on its own keeps the full stop after it, and a token without
        phonemes is left out.
    """
    out = []
    for token in g2p(text)[1]:
        said = token.phonemes or ""
        if re.search(r"n['’]t$", token.text, re.IGNORECASE):
            said = said.replace("ᵊnt", "ənt")
        # misaki reads "U." and "U-S-B." as initials and drops the stop,
        # and the pause with it: "I M U. Tap" runs on as one sentence.
        if re.search(r"(^|-)[A-Za-z]\.$", token.text) \
                and not said.endswith("."):
            said += "."
        out.append(said + token.whitespace)
    return "".join(out).strip()


def unknown(texts: list, british: bool = False) -> list:
    """Find the words misaki's dictionary cannot read.

    Args:
        texts: The lines as spoken().
        british: Whether to ask the British dictionary.

    Returns:
        (word, line) per word that misaki, without a fallback, leaves
        without phonemes, in the order of the lines.
    """
    from misaki import en

    g2p = en.G2P(trf=False, british=british, fallback=None, unk="\u2753")
    found = []
    for text in texts:
        for token in g2p(text)[1]:
            if token.phonemes is None or "\u2753" in token.phonemes:
                found.append((token.text, text))
    return found


_SPEAKER = None
_G2P = {}


def _misaki(british: bool):
    """Load misaki's English converter once per worker.

    Args:
        british: Whether to load the British dictionary.

    Returns:
        An en.G2P with espeak-ng as the fallback for unknown words.
    """
    if british not in _G2P:
        from misaki import en, espeak

        _G2P[british] = en.G2P(
            trf=False, british=british, unk="",
            fallback=espeak.EspeakFallback(british=british))
    return _G2P[british]


def _start(files: list, threads: int) -> None:
    """Load the model in a worker process.

    Args:
        files: The model and the voices file.
        threads: The threads one worker's model runs on.
    """
    global _SPEAKER
    if files[0].name != "kokoro-v1.0.onnx":
        from piper import PiperVoice

        _SPEAKER = PiperVoice.load(str(files[0]), str(files[1]))
        return
    import onnxruntime
    from kokoro_onnx import Kokoro

    options = onnxruntime.SessionOptions()
    options.intra_op_num_threads = threads
    options.inter_op_num_threads = 1
    session = onnxruntime.InferenceSession(
        str(files[0]), options, providers=["CPUExecutionProvider"])
    _SPEAKER = Kokoro.from_session(session, str(files[1]))


def _speak(job: tuple):
    """Speak one line at RATE.

    Args:
        job: (text, voice) with the storyboard's "voice" object.

    Returns:
        The samples as a numpy array, peak 1, cut by trim().
    """
    import numpy as np

    text, voice = job
    if voice.get("engine") == "piper":
        from piper import SynthesisConfig

        # A higher speed is a shorter length scale.
        chunks = list(_SPEAKER.synthesize(text, SynthesisConfig(
            length_scale=1.0 / voice.get("speed", 1.0))))
        raw = np.concatenate([c.audio_int16_array for c in chunks]).astype(
            float)
        rate = chunks[0].sample_rate
        return _to_rate(trim(raw, rate), rate)
    # A blend is the weighted mean of the voices' style vectors.
    style = voice.get("name")
    if "blend" in voice:
        whole = sum(voice["blend"].values())
        style = sum(_SPEAKER.get_voice_style(name) * (part / whole)
                    for name, part in voice["blend"].items())
    lang = voice.get("lang", "en-us")
    if lang in MISAKI:
        text = phonemes(text, _misaki(MISAKI[lang]))
    # kokoro-onnx cuts each batch at -60dB itself and adds the pause the
    # punctuation asks for between batches; trim() cuts the ends further.
    raw, rate = _SPEAKER.create(text, voice=style,
                                speed=voice.get("speed", 1.0),
                                lang=lang, is_phonemes=lang in MISAKI)
    return _to_rate(trim(np.asarray(raw, dtype=float), rate), rate)


def _to_rate(raw, rate: int):
    """Bring a clip to RATE.

    Args:
        raw: The samples as a numpy array of floats.
        rate: Their samples per second.

    Returns:
        The samples at RATE, peak 1.
    """
    import numpy as np

    # To RATE by padding the spectrum: no filter to design.
    size = int(round(len(raw) * RATE / rate))
    spec = np.fft.rfft(raw)
    wide = np.zeros(size // 2 + 1, dtype=complex)
    wide[: len(spec)] = spec[: len(wide)]
    out = np.fft.irfft(wide, size) * (size / len(raw))
    return out / max(1e-9, float(np.max(np.abs(out))))


def narrate(board: dict, cache: pathlib.Path | None = None) -> tuple:
    """Speak a storyboard and make room for each line.

    Lines are spoken in parallel worker processes.  With @p cache, each
    spoken line is kept there as a .npy file named by a hash of its
    text, the voice, the model files' sizes, this module's source and
    the versions of the speech packages, and a later run reads it back
    instead of speaking it.

    Args:
        board: A storyboard that passed make_video.check(), with "voice".
        cache: The directory that keeps the spoken lines, or None.

    Returns:
        The storyboard with each step and end scene long enough for its
        line, and the clips: (seconds from the video's start, samples at
        RATE as a numpy array) per line.
    """
    import concurrent.futures
    import hashlib
    import json

    import importlib.metadata

    piper = board["voice"].get("engine") == "piper"
    packages = ["piper-tts"] if piper else ["kokoro-onnx"]
    if not piper and board["voice"].get("lang", "en-us") in MISAKI:
        packages.append("misaki")
    try:
        import numpy as np
        if piper:
            import piper as _piper  # noqa: F401
        else:
            import kokoro_onnx  # noqa: F401
            if "misaki" in packages:
                import misaki.en  # noqa: F401
                import misaki.espeak  # noqa: F401
        versions = [importlib.metadata.version(name) for name in packages]
    except ImportError:
        sys.exit("the narration needs numpy and kokoro-onnx with misaki, "
                 "or piper-tts for a voice of Piper: pip install numpy "
                 "kokoro-onnx misaki==0.9.4 spacy num2words "
                 "espeakng-loader phonemizer piper-tts, and "
                 "en_core_web_sm 3.8.0 for spacy")
    files = [model_dir() / "kokoro-v1.0.onnx", model_dir() / "voices-v1.0.bin"]
    if piper:
        name = board["voice"]["name"]
        files = [model_dir() / f"{name}.onnx",
                 model_dir() / f"{name}.onnx.json"]
    for path in files:
        if not path.is_file():
            sys.exit(f"{path} is missing; tools/video_voice.py names the "
                     "release that holds it")
    voice = board["voice"]
    lines = script(board)
    source = pathlib.Path(__file__).read_text(encoding="utf-8")
    sizes = [path.stat().st_size for path in files]

    def kept(text) -> pathlib.Path | None:
        if cache is None:
            return None
        key = hashlib.sha256(json.dumps(
            [text, voice, RATE, sizes, source, versions]).encode()
        ).hexdigest()[:20]
        return cache / f"{key}.npy"

    spoken = {}
    for _, _, _, text in lines:
        path = kept(text)
        if path is not None and path.is_file():
            spoken[text] = np.load(path)
    todo = sorted({text for _, _, _, text in lines} - set(spoken))
    if todo:
        cpus = os.cpu_count() or 1
        workers = max(1, min(len(todo), cpus // 4, 12))
        with concurrent.futures.ProcessPoolExecutor(
                workers, initializer=_start,
                initargs=(files, max(1, cpus // workers))) as pool:
            for text, clip in zip(todo, pool.map(
                    _speak, [(text, voice) for text in todo]), strict=True):
                spoken[text] = clip
                path = kept(text)
                if path is not None:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    np.save(path.with_suffix(".tmp.npy"), clip)
                    path.with_suffix(".tmp.npy").replace(path)

    board = copy.deepcopy(board)
    spoken_lines = [(i, j, at, spoken[text]) for i, j, at, text in lines]
    for i, j, at, clip in spoken_lines:
        scene, need = board["scenes"][i], at + len(clip) / RATE + TAIL_S
        if j is not None:
            step = scene["steps"][j]
            step["dur"] = round(max(step["dur"], need), 2)
        elif scene["type"] == "end":
            scene["dur"] = round(max(scene["dur"], need), 2)
    # A case or board scene whose line runs into the next cue is
    # stretched: the
    # cues keep their gaps or get the room their lines need, and every
    # other time of the scene follows them.
    for i, scene in enumerate(board["scenes"]):
        if (scene["type"] not in ("case", "board", "scope")
                or not scene.get("say")):
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
                if scene["type"] in ("case", "board", "scope") else [0.6])
        for (_, clip), at in zip(mine, cues, strict=False):
            clips.append((start + at, clip))
        start += scene["dur"]
    return board, clips


def main() -> int:
    """Print what a storyboard says, one line per line.

    With --unknown, print each word misaki's dictionary cannot read and
    the line it stands in instead.

    Returns:
        The exit status: with --unknown, 1 when a word is found.
    """
    import make_video

    ap = argparse.ArgumentParser()
    ap.add_argument("storyboard")
    ap.add_argument("--unknown", action="store_true",
                    help="list the words misaki reads only through its "
                         "fallback, with their lines")
    args = ap.parse_args()
    path = pathlib.Path(args.storyboard)
    if not path.suffix:
        path = make_video.VIDEO / "storyboards" / f"{args.storyboard}.json"
    board = make_video.load(path)
    texts = [text for _, _, _, text in script(board)]
    if not args.unknown:
        print("\n".join(texts))
        return 0
    if board.get("lang", "en") != "en":
        sys.exit("--unknown reads English storyboards only")
    british = MISAKI.get(board.get("voice", {}).get("lang"), False)
    found = unknown(texts, british)
    for word, text in found:
        print(f"{word}\t{text}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
