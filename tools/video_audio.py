#!/usr/bin/env python3
"""Synthesise the sound track of a storyboard for tools/make_video.py.

The track is computed, not recorded: a bed of soft synthesizer pads, a
sine bass and a plucked arpeggio over four chords in D major, and one
quiet sound per event of the storyboard.  There is no percussion.  The
same storyboard gives the same samples: the noise comes from a seeded
generator.

    python3 tools/video_audio.py features -o /tmp/features.wav

A storyboard's "music" names the bed: "pads", the default, or
"bach-prelude", the Prelude in C major BWV 846 of J. S. Bach (1685 to
1750) from the first book of the Well-Tempered Clavier, played by the
same soft voices at one bar every 4s.  The composition is in the public
domain; the notes are set down here from the piece's harmony, 5 a bar,
and no recording is used.  Bars 33 and 34 keep the pattern of the other
bars in place of Bach's two free bars, so that the piece loops.

"pachelbel-canon" is the Canon in D major of Johann Pachelbel (1653 to
1706): its ground bass of 8 notes, 2s a note, under the chords and three
of the violin's variations, which enter one after another.  It is in the
public domain as well, and set down here in the same way.

"vivaldi-spring" is the opening of "Spring", the concerto RV 269 of
Antonio Vivaldi (1678 to 1741), first movement: its two themes in E
major on a bowed voice with the third below, over a bass plucked on
every eighth, 0.42s an eighth.  It is in the public domain.  The notes are
set down from memory of the violin part and are not checked against a
score.

"bach-cello" is the Prelude of the Suite No. 1 in G major for cello
solo, BWV 1007, of J. S. Bach: its 41 bars of sixteenth notes on a
plucked voice, 4s a bar, over each bar's lowest note as a quiet pad, then
the closing chord.  The composition is in the public domain.  The notes
are those of the Mutopia Project's edition Mutopia-2018/01/19-517
(typeset by Andreas Scherer from Schirmer, 1916), which its typesetter
placed in the public domain; no recording is used.

"beethoven-moonlight" is the opening of the Sonata quasi una fantasia
in C sharp minor, Op. 27 No. 2, of Ludwig van Beethoven (1770 to 1827),
first movement: bars 1 to 4, their triplets on a plucked voice, 0.37s a
note, over the bass octaves, as a loop, then the C sharp minor chord of
bar 5.  The composition is in the public domain.  The notes are read
from the first edition (Vienna: Cappi, 1802; scan 15808 of the
International Music Score Library Project); no recording is used.

While a case scene holds a component in the air, the music stops as a
record does under a hand: its speed falls to SLOW over BRAKE_S, pitch
and all, and comes back over SPIN_S when the component moves on.
freezes() lists those times.

events() needs Python only.  render() needs numpy.

Attributes:
    RATE: Samples per second of the track.
    BAR_S: Seconds one chord lasts.
    BEAT_S: Seconds between two notes of the arpeggio.
    CHORDS: The loop: a bass note and four pad notes per chord, as MIDI
        (Musical Instrument Digital Interface) note numbers.
    LEVEL: The loudest sample, as a part of full scale.
    PRELUDE: The 5 notes of each bar of BWV 846, as MIDI note numbers.
    PRELUDE_BAR_S: Seconds one bar of the prelude lasts.
    CANON: The canon's ground: a bass note and three pad notes per
        chord.
    CANON_LINES: Three variations of the canon: 8 notes, 8 notes and 32.
    CANON_NOTE_S: Seconds one note of the ground lasts.
    CELLO: The Prelude of BWV 1007 bar by bar: the MIDI note of each
        sixteenth, 0 where the note before is held.
    CELLO_END: Its closing chord.
    CELLO_BAR_S: Seconds one bar of it lasts.
    MOON: Bars 1 to 4 of Beethoven's Op. 27 No. 2, first movement: per
        bar its 4 triplets and the bass octave of each half bar.
    MOON_END: The chord of bar 5 that closes the bed.
    MOON_NOTE_S: Seconds one triplet note of it lasts.
    SPRING: The opening of "Spring" from Vivaldi's Four Seasons, RV 269,
        first movement, in E major: per bar the melody as (MIDI note,
        length in eighths) and the bass note of each half bar.  Bars 0
        to 2 are the first theme, bars 3 to 8 the second with its two
        endings.  Set down from memory of the violin part, not checked
        against a score.
    SPRING_ORDER: The bars in the order they are played, as one round.
    SPRING_EIGHTH_S: Seconds one eighth note of it lasts.
    BEDS: The names a storyboard's "music" takes.
    SLOW: The music's speed during a freeze, as a part of full speed.
    BRAKE_S: Seconds the music takes to fall to SLOW.
    SPIN_S: Seconds it takes to return to full speed.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import wave

RATE = 44100
BAR_S = 8.0
BEAT_S = 0.5
LEVEL = 0.7
PRELUDE = (
    (60, 64, 67, 72, 76), (60, 62, 69, 74, 77), (59, 62, 67, 74, 77),
    (60, 64, 67, 72, 76), (60, 64, 69, 76, 81), (60, 62, 66, 69, 74),
    (59, 62, 67, 74, 79), (59, 60, 64, 67, 72), (57, 60, 64, 67, 72),
    (50, 57, 62, 66, 72), (55, 59, 62, 67, 71), (55, 58, 64, 67, 73),
    (53, 57, 62, 69, 74), (53, 56, 62, 65, 71), (52, 55, 60, 67, 72),
    (52, 53, 57, 60, 65), (50, 53, 57, 60, 65), (43, 50, 55, 59, 65),
    (48, 52, 55, 60, 64), (48, 55, 58, 60, 64), (41, 53, 57, 60, 64),
    (42, 48, 57, 60, 63), (44, 53, 59, 60, 62), (43, 53, 55, 59, 62),
    (43, 52, 55, 60, 64), (43, 50, 55, 60, 65), (43, 50, 55, 59, 65),
    (43, 51, 57, 60, 66), (43, 52, 55, 60, 67), (43, 50, 55, 60, 65),
    (43, 50, 55, 59, 65), (36, 48, 55, 58, 64), (36, 48, 53, 57, 60),
    (36, 47, 55, 59, 65), (36, 48, 52, 55, 60),
)
PRELUDE_BAR_S = 4.0
CANON = (
    (50, (57, 62, 66)), (45, (57, 61, 64)), (47, (59, 62, 66)),
    (42, (57, 61, 66)), (43, (59, 62, 67)), (38, (57, 62, 66)),
    (43, (59, 62, 67)), (45, (57, 61, 64)),
)
CANON_LINES = (
    (78, 76, 74, 73, 71, 69, 71, 73),
    (74, 73, 71, 69, 67, 66, 67, 64),
    (62, 66, 69, 67, 66, 62, 66, 64, 62, 59, 62, 69, 67, 71, 69, 67,
     66, 62, 64, 73, 74, 78, 81, 69, 71, 67, 69, 66, 62, 74, 74, 73),
)
CANON_NOTE_S = 2.0
_A = ((68, 1), (68, 1), (68, 1), (66, .5), (64, .5), (71, 3), (71, .5),
      (69, .5))
_B = ((71, 1), (69, .5), (68, .5), (69, 1), (71, 1), (73, 2), (71, 1),
      (64, 1))
SPRING = (
    (_A, (40, 40)), (_A, (40, 40)),
    (((68, 1), (69, .5), (71, .5), (69, 1), (68, 1), (66, 1), (63, 1),
      (59, 1), (64, 1)), (40, 47)),
    (_B, (40, 45)), (_B, (40, 45)),
    (((73, 1), (71, 1), (69, 1), (68, 1), (66, 1.5), (64, .5), (66, 2)),
     (45, 47)),
    (_B, (40, 45)), (_B, (40, 45)),
    (((73, 1), (71, 1), (69, 1), (68, 1), (66, 1), (63, 1), (64, 1),
      (64, 1)), (45, 47)),
)
SPRING_ORDER = (0, 1, 2, 0, 1, 2, 3, 4, 5, 6, 7, 8)
SPRING_EIGHTH_S = 0.42
CELLO = (
    (43, 50, 59, 57, 59, 50, 59, 50, 43, 50, 59, 57, 59, 50, 59, 50),
    (43, 52, 60, 59, 60, 52, 60, 52, 43, 52, 60, 59, 60, 52, 60, 52),
    (43, 54, 60, 59, 60, 54, 60, 54, 43, 54, 60, 59, 60, 54, 60, 54),
    (43, 55, 59, 57, 59, 55, 59, 55, 43, 55, 59, 57, 59, 55, 59, 54),
    (43, 52, 59, 57, 59, 55, 54, 55, 52, 55, 54, 55, 47, 50, 49, 47),
    (49, 55, 57, 55, 57, 55, 57, 55, 49, 55, 57, 55, 57, 55, 57, 55),
    (54, 57, 62, 61, 62, 57, 55, 57, 54, 57, 55, 57, 50, 54, 52, 50),
    (40, 47, 55, 54, 55, 47, 55, 47, 40, 47, 55, 54, 55, 47, 55, 47),
    (40, 49, 50, 52, 50, 49, 47, 45, 55, 54, 52, 62, 61, 59, 57, 55),
    (54, 52, 50, 62, 57, 62, 54, 57, 50, 52, 54, 57, 55, 54, 52, 50),
    (56, 50, 53, 52, 53, 50, 56, 50, 59, 50, 53, 52, 53, 50, 56, 50),
    (48, 52, 57, 59, 60, 57, 52, 50, 48, 52, 57, 59, 60, 57, 54, 52),
    (51, 54, 51, 54, 57, 54, 57, 54, 51, 54, 51, 54, 57, 54, 57, 54),
    (55, 54, 52, 55, 54, 55, 57, 54, 55, 54, 52, 50, 48, 47, 45, 43),
    (42, 48, 50, 48, 50, 48, 50, 48, 42, 48, 50, 48, 50, 48, 50, 48),
    (43, 47, 53, 52, 53, 47, 53, 47, 43, 47, 53, 52, 53, 47, 53, 47),
    (43, 48, 52, 50, 52, 48, 52, 48, 43, 48, 52, 50, 52, 48, 52, 48),
    (43, 54, 60, 59, 60, 54, 60, 54, 43, 54, 60, 59, 60, 54, 60, 54),
    (43, 50, 59, 57, 59, 55, 54, 52, 50, 48, 47, 45, 43, 42, 40, 38),
    (37, 45, 52, 54, 55, 52, 54, 55, 37, 45, 52, 54, 55, 52, 54, 55),
    (36, 45, 50, 52, 54, 50, 52, 54, 36, 45, 50, 52, 54, 50, 52, 54),
    (36, 45, 50, 54, 57, 61, 62, 0, 0, 45, 47, 48, 50, 52, 54, 55),
    (57, 54, 50, 52, 54, 55, 57, 59, 60, 57, 54, 55, 57, 59, 60, 62),
    (63, 62, 61, 62, 62, 60, 59, 60, 60, 57, 54, 52, 50, 45, 47, 48),
    (38, 45, 50, 54, 57, 59, 60, 57, 59, 55, 50, 48, 47, 43, 45, 47),
    (38, 43, 47, 50, 55, 57, 59, 55, 61, 58, 57, 58, 58, 57, 56, 57),
    (57, 55, 54, 55, 55, 52, 49, 47, 45, 49, 52, 55, 57, 61, 62, 61),
    (62, 57, 54, 52, 54, 57, 50, 54, 45, 50, 49, 47, 45, 43, 42, 40),
    (38, 0, 60, 59, 57, 55, 54, 52, 50, 60, 59, 57, 55, 54, 52, 50),
    (48, 59, 57, 55, 54, 52, 50, 48, 47, 57, 55, 54, 52, 50, 48, 47),
    (45, 55, 54, 52, 54, 57, 50, 57, 52, 57, 54, 57, 55, 57, 52, 57),
    (54, 57, 50, 57, 55, 57, 52, 57, 54, 57, 50, 57, 55, 57, 52, 57),
    (54, 57, 50, 57, 52, 57, 54, 57, 55, 57, 57, 57, 59, 57, 50, 57),
    (57, 57, 59, 57, 60, 57, 50, 57, 59, 57, 60, 57, 62, 57, 59, 57),
    (60, 57, 59, 57, 60, 57, 57, 57, 59, 57, 57, 57, 59, 57, 55, 57),
    (57, 57, 55, 57, 57, 57, 54, 57, 55, 57, 54, 57, 55, 57, 52, 57),
    (54, 57, 50, 52, 53, 50, 54, 50, 55, 50, 56, 50, 57, 50, 58, 50),
    (59, 50, 60, 50, 61, 50, 62, 50, 63, 50, 64, 50, 65, 50, 66, 50),
    (67, 59, 50, 59, 67, 59, 67, 59, 67, 59, 50, 59, 67, 59, 67, 59),
    (67, 57, 50, 57, 67, 57, 67, 57, 67, 57, 50, 57, 67, 57, 67, 57),
    (66, 60, 50, 60, 66, 60, 66, 60, 66, 60, 50, 60, 66, 60, 66, 60),
)
CELLO_END = (43, 59, 67)
CELLO_BAR_S = 4.0
MOON = (
    (((56, 61, 64),) * 4, ((37, 49),) * 2),
    (((56, 61, 64),) * 4, ((35, 47),) * 2),
    (((57, 61, 64),) * 2 + ((57, 62, 66),) * 2, ((33, 45), (30, 42))),
    (((56, 60, 66), (56, 61, 64), (56, 61, 63), (54, 60, 63)),
     ((32, 44),) * 2),
)
MOON_END = (37, 44, 49, 52, 56, 61)
MOON_NOTE_S = 0.37
BEDS = ("pads", "bach-prelude", "pachelbel-canon", "vivaldi-spring",
        "bach-cello", "beethoven-moonlight")
SLOW = 0.05
BRAKE_S = 1.1
SPIN_S = 0.7
CHORDS = (
    (38, (57, 61, 64, 66)),     # D maj7
    (42, (57, 61, 64, 68)),     # F sharp m7
    (43, (59, 62, 66, 69)),     # G maj7
    (45, (57, 59, 64, 69)),     # A sus2
)


def scene_times(board: dict) -> list:
    """Lay the scenes on the timeline as the player does.

    Args:
        board: A storyboard that passed make_video.check().

    Returns:
        One (start, length) in seconds per scene.
    """
    times, start = [], 0.0
    for scene in board["scenes"]:
        if scene["type"] in ("screen", "tour"):
            length = sum(step["dur"] for step in scene["steps"])
        else:
            length = scene["dur"]
        times.append((start, length))
        start += length
    return times


def freezes(board: dict) -> list:
    """List when a case scene holds a component in the air.

    Args:
        board: A storyboard that passed make_video.check().

    Returns:
        (start, end) in seconds from the video's start, merged where
        two components' holds overlap, sorted.
    """
    holds = []
    for scene, (start, _) in zip(board["scenes"], scene_times(board),
                                 strict=True):
        for keys in scene.get("parts", {}).values():
            for a, b in zip(keys, keys[1:], strict=False):
                if a[1] == b[1] and 0 < a[1] < 1:
                    holds.append((start + a[0], start + b[0]))
    merged = []
    for a, b in sorted(holds):
        if merged and a <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], b))
        else:
            merged.append((a, b))
    return merged


def events(board: dict) -> list:
    """List the sounds of a storyboard.

    Args:
        board: A storyboard that passed make_video.check().

    Returns:
        (time in s, kind, number) per sound, sorted by time.  The kinds
        are whoosh, low, chime, pop, tick, swell, tap, hold, done and
        final; the number is a count that sets a tick's pitch, else 0.
    """
    found = []
    for scene, (start, _) in zip(board["scenes"], scene_times(board),
                                 strict=True):
        if start > 0:
            found.append((start - 0.25, "whoosh", 0))
        kind = scene["type"]
        if kind == "case":
            if "title" in scene:
                found.append((start + scene["title"]["from"], "chime", 0))
            for label in scene.get("labels", []):
                found.append((start + label["from"] + 0.1, "pop", 0))
            lid = scene.get("lid", [])
            for a, b in zip(lid, lid[1:], strict=False):
                if a[1] != b[1]:
                    found.append((start + a[0], "low", 0))
            # A component of the model takes its seat: a soft tap.
            for keys in scene.get("parts", {}).values():
                if keys and keys[-1][1] == 0:
                    found.append((start + keys[-1][0] - 0.08, "tap", 0))
            # The Dial's knob clicks once a detent, 16 a turn.
            turn = scene.get("turn", [])
            for a, b in zip(turn, turn[1:], strict=False):
                clicks = min(32, round(abs(b[1] - a[1]) / 22.5))
                for k in range(clicks):
                    found.append((start + a[0] + (k + 0.5) / clicks
                                  * (b[0] - a[0]), "tick", 3))
            for key in scene.get("faces", [])[1:]:
                if not key[1].startswith("live-"):
                    found.append((start + key[0], "pop", 0))
            # A nav run's PRESS is a tap, its HOLD a low sound.
            for at, op in scene.get("run", {}).get("ops", []):
                if op in ("p", "h"):
                    found.append((start + at, "tap" if op == "p" else "low",
                                  0))
            if "pinout" in scene:
                found.append((start + scene["pinout"]["from"], "pop", 0))
            if "servo" in scene:
                found.append((start + scene["servo"]["plug"][1] - 0.15,
                              "tap", 0))
        elif kind == "board":
            if "title" in scene:
                found.append((start + scene["title"]["from"], "chime", 0))
            for label in scene.get("labels", []):
                found.append((start + label["from"] + 0.1, "pop", 0))
            for n, tag in enumerate(scene.get("tags", [])):
                found.append((start + tag["from"] + 0.05, "tick", n % 5))
            for net in scene.get("nets", []):
                found.append((start + net["from"], "swell", 0))
            # A cut is a low sound; a wire and a part end with a tap.
            for cut in scene.get("cuts", []):
                found.append((start + cut["t"][0], "low", 0))
            for wire in scene.get("wires", []):
                found.append((start + wire["t"][1], "tap", 0))
            for fit in scene.get("fit", []):
                found.append((start + fit["t"] + 0.6, "tap", 0))
            # A part takes its place with a tap; a row of joints is
            # one rising sound, an inset a pop.
            for mount in scene.get("mount", []):
                found.append((start + mount["t"] + 0.55, "tap", 0))
            for joint in scene.get("solder", []):
                found.append((start + joint["t"][0], "swell", 0))
            for inset in scene.get("insets", []):
                found.append((start + inset["from"] + 0.1, "pop", 0))
        elif kind == "scope":
            if "title" in scene:
                found.append((start + scene["title"]["from"], "chime", 0))
            for label in scene.get("labels", []):
                found.append((start + label["from"] + 0.1, "pop", 0))
            # A panel comes with a rising sound, a current with a tick.
            for panel in scene["panels"]:
                if panel.get("from", 0) > 0:
                    found.append((start + panel["from"], "swell", 0))
                for n, flow in enumerate(panel.get("flow", [])):
                    found.append((start + flow["from"], "tick", n % 5))
        elif kind == "end":
            found.append((start + 0.3, "final", 0))
        at = start
        for step in scene.get("steps", []):
            if "card" in step:
                found.append((at - 0.2, "whoosh", 0))
                found.append((at + 0.25, "chime", 0))
            else:
                found.append((at + 0.25, "pop", 0))
            if kind == "tour" and "card" not in step:
                # The caption's lines: kicker, path, two for the title,
                # the text, then the points, 0.14s apart.
                first = 4 + (1 if step.get("path") else 0)
                for n in range(len(step.get("points", []))):
                    found.append((at + 0.5 + 0.14 * (first + n), "tick", n))
                for n in range(len(step.get("marks", []))):
                    found.append((at + 1.05 + 0.38 * n, "tick", n + 2))
                if "focus" in step:
                    found.append((at + 0.9, "swell", 0))
            if step.get("action") == "tap":
                found.append((at + step["dur"] - 0.47, "tap", 0))
            if step.get("action") == "hold":
                found.append((at + step["dur"] - 2.45, "hold", 0))
                found.append((at + step["dur"] - 0.42, "done", 0))
            at += step["dur"]
    return sorted(f for f in found if f[0] >= 0)


def render(board: dict, path: pathlib.Path, clips=()) -> float:
    """Write the track as a 16-bit stereo WAV file.

    Args:
        board: A storyboard that passed make_video.check().
        path: The file to write.
        clips: The narration from video_voice.narrate(): (seconds, mono
            samples at RATE) per line.  The bed is lowered by 11dB under
            a line and by 4dB throughout.

    Returns:
        The track's length in seconds.
    """
    try:
        import numpy as np
    except ImportError:
        sys.exit("the sound track needs numpy: pip install numpy")

    total = sum(length for _, length in scene_times(board))
    n = int(total * RATE)
    # The music's speed at each sample and the place in the music it
    # plays: the bed is written at full speed and read along this.
    speed = np.ones(n)
    t = np.arange(n) / RATE
    for a, b in freezes(board):
        down = np.clip((t - a) / BRAKE_S, 0.0, 1.0)
        up = np.clip((t - (b - SPIN_S)) / SPIN_S, 0.0, 1.0)
        # A hand on a record: fast at first, then slower to a stop.
        dip = (1.0 - (1.0 - down) ** 2.2) * (1.0 - up ** 2)
        speed = np.minimum(speed, 1.0 - (1.0 - SLOW) * dip)
    place = np.cumsum(speed) / RATE
    span = float(place[-1])
    rng = np.random.default_rng(2350)
    music = np.zeros((2, n + RATE * 4))
    fx = np.zeros((2, n + RATE * 4))

    def hz(note):
        return 440.0 * 2.0 ** ((note - 69) / 12.0)

    def put(buf, t, sig, pan=0.0, gain=1.0):
        i = int(t * RATE)
        if i < 0 or i >= buf.shape[1]:
            return
        sig = sig[: buf.shape[1] - i]
        buf[0, i:i + len(sig)] += sig * gain * (1.0 - max(0.0, pan))
        buf[1, i:i + len(sig)] += sig * gain * (1.0 + min(0.0, pan))

    def tone(freq, length, partials=((1, 1.0),)):
        t = np.arange(int(length * RATE)) / RATE
        return sum(a * np.sin(2 * np.pi * freq * k * t)
                   for k, a in partials), t

    def swell(length, attack, release):
        t = np.arange(int(length * RATE)) / RATE
        env = np.minimum(1.0, t / attack)
        env = np.minimum(env, np.clip((length - t) / release, 0.0, 1.0))
        return 0.5 - 0.5 * np.cos(np.pi * env)

    def noise(length, low, high):
        spec = np.fft.rfft(rng.standard_normal(int(length * RATE)))
        f = np.fft.rfftfreq(int(length * RATE), 1.0 / RATE)
        spec *= np.exp(-((np.log2(np.maximum(f, 1.0) / np.sqrt(low * high))
                          / np.log2(high / low)) ** 2) * 4.0)
        out = np.fft.irfft(spec, int(length * RATE))
        return out / np.max(np.abs(out))

    # The bed: per chord a pad of three detuned voices a note, a sine
    # bass and a plucked note every BEAT_S.  A pad overlaps the next by
    # 3s, so no chord starts on an edge.
    soft = tuple((k, k ** -1.7) for k in range(1, 7))
    pluck = ((1, 1.0), (2, 0.28), (3, 0.08))
    order = (0, 2, 1, 3, 2, 3, 1, 2)
    bed = board.get("music", "pads")
    prelude, canon = bed == "bach-prelude", bed == "pachelbel-canon"
    bars = int(span / BAR_S) + 1 if bed == "pads" else 0
    # The canon: the ground alone once, then the variations in turn over
    # it, the long ones on a soft sustained voice, the third plucked.
    rounds = int(span / (8 * CANON_NOTE_S)) + 1 if canon else 0
    for rnd in range(rounds):
        r0 = rnd * 8 * CANON_NOTE_S
        with_lines = ((), (0,), (0, 1), (1, 2), (0, 2))[
            0 if rnd == 0 else 1 + (rnd - 1) % 4]
        for k, (bass, notes) in enumerate(CANON):
            t0 = r0 + k * CANON_NOTE_S
            env = swell(CANON_NOTE_S + 1.6, 0.7, 1.7)
            for note in notes:
                for cents, pan in ((-5, -0.6), (6, 0.6)):
                    sig, _ = tone(hz(note) * 2 ** (cents / 1200),
                                  CANON_NOTE_S + 1.6, soft)
                    put(music, t0, sig * env, pan, 0.024)
            sig, _ = tone(hz(bass), CANON_NOTE_S + 1.6,
                          ((1, 1.0), (2, 0.25), (3, 0.06)))
            put(music, t0, sig * env, 0.0, 0.085)
            for line in with_lines:
                if line == 2:
                    for q in range(4):
                        note = CANON_LINES[2][k * 4 + q]
                        sig, t = tone(hz(note), 1.8, pluck)
                        sig *= np.exp(-t / 0.4) * np.minimum(1.0, t / 0.012)
                        at = t0 + q * CANON_NOTE_S / 4
                        put(music, at, sig, 0.45, 0.06)
                        put(music, at + 0.375, sig, -0.5, 0.018)
                    continue
                sig, t = tone(hz(CANON_LINES[line][k]), CANON_NOTE_S + 0.9,
                              ((1, 1.0), (2, 0.22), (3, 0.07)))
                sig *= swell(CANON_NOTE_S + 0.9, 0.3, 1.0)
                sig *= 1.0 + 0.05 * np.sin(2 * np.pi * 4.6 * t)
                put(music, t0, sig, -0.35 if line == 0 else 0.35,
                    0.05 if line == 0 else 0.04)
    # The prelude: each bar's 5 notes as Bach breaks them, 1 2 3 4 5 3 4 5
    # twice, on plucked voices; the bar's lowest note held under them and
    # its chord as a quiet pad.  The last bar is the closing C major.
    step = PRELUDE_BAR_S / 16.0
    count = int((span - 6.0) / PRELUDE_BAR_S) if prelude else 0
    for bar in range(count):
        t0 = 1.0 + bar * PRELUDE_BAR_S
        notes = PRELUDE[-1] if bar == count - 1 else PRELUDE[
            bar % (len(PRELUDE) - 1)]
        env = swell(PRELUDE_BAR_S + 2.0, 1.2, 2.4)
        for j, note in enumerate(notes):
            for cents, pan in ((-5, -0.6), (6, 0.6)):
                sig, t = tone(hz(note) * 2 ** (cents / 1200),
                              PRELUDE_BAR_S + 2.0, soft)
                put(music, t0, sig * env, pan, 0.016 if j else 0.03)
        sig, _ = tone(hz(notes[0] - 12), PRELUDE_BAR_S + 2.0,
                      ((1, 1.0), (2, 0.18)))
        put(music, t0, sig * env, 0.0, 0.05)
        for k in range(16):
            note = notes[(0, 1, 2, 3, 4, 2, 3, 4)[k % 8]]
            long = 1.4 if k % 8 < 2 else 0.45
            sig, t = tone(hz(note), 2.4, pluck)
            sig *= np.exp(-t / long) * np.minimum(1.0, t / 0.012)
            pan = -0.5 + (note - 36) / 45.0
            put(music, t0 + k * step, sig, pan, 0.085)
            put(music, t0 + k * step + 0.375, sig, -pan, 0.02)
    # The cello prelude: every sixteenth plucked, a held note ringing
    # longer, over the bar's lowest note as a quiet pad.  The piece
    # loops over its 41 bars; the last bar played is the closing chord.
    count_c = int((span - 6.0) / CELLO_BAR_S) if bed == "bach-cello" else 0
    for bar in range(count_c):
        t0 = 1.0 + bar * CELLO_BAR_S
        if bar == count_c - 1:
            for note in CELLO_END:
                for cents, pan in ((-5, -0.6), (6, 0.6)):
                    sig, _ = tone(hz(note) * 2 ** (cents / 1200), 5.0, soft)
                    put(music, t0, sig * swell(5.0, 0.05, 3.4), pan, 0.03)
                sig, t = tone(hz(note), 3.0, pluck)
                sig *= np.exp(-t / 1.6) * np.minimum(1.0, t / 0.012)
                put(music, t0, sig, 0.0, 0.07)
            break
        row = CELLO[bar % len(CELLO)]
        low = min(note for note in row if note)
        env = swell(CELLO_BAR_S + 2.0, 1.2, 2.4)
        for cents, pan in ((-5, -0.6), (6, 0.6)):
            sig, _ = tone(hz(low) * 2 ** (cents / 1200), CELLO_BAR_S + 2.0,
                          soft)
            put(music, t0, sig * env, pan, 0.03)
        sig, _ = tone(hz(low), CELLO_BAR_S + 2.0, ((1, 1.0), (2, 0.18)))
        put(music, t0, sig * env, 0.0, 0.04)
        for k, note in enumerate(row):
            if not note:
                continue
            held = 0
            while k + held + 1 < 16 and not row[k + held + 1]:
                held += 1
            sig, t = tone(hz(note), 2.4, pluck)
            sig *= np.exp(-t / (0.5 + 0.3 * held)) * np.minimum(1.0,
                                                                t / 0.012)
            pan = -0.5 + (note - 36) / 36.0
            at = t0 + k * CELLO_BAR_S / 16.0
            put(music, at, sig, pan, 0.085)
            put(music, at + 0.375, sig, -pan, 0.02)
    # The Moonlight opening: the triplets plucked, each half bar's octave
    # in the bass held under them and the triplet's chord as a quiet pad.
    bar_m = 12 * MOON_NOTE_S
    count_m = (int((span - 6.0) / bar_m)
               if bed == "beethoven-moonlight" else 0)
    for bar in range(count_m):
        t0 = 1.0 + bar * bar_m
        if bar == count_m - 1:
            for note in MOON_END:
                for cents, pan in ((-5, -0.6), (6, 0.6)):
                    sig, _ = tone(hz(note) * 2 ** (cents / 1200), 5.0, soft)
                    put(music, t0, sig * swell(5.0, 0.05, 3.4), pan, 0.022)
                sig, t = tone(hz(note), 3.0, pluck)
                sig *= np.exp(-t / 1.6) * np.minimum(1.0, t / 0.012)
                put(music, t0, sig, 0.0, 0.05)
            break
        groups, basses = MOON[bar % len(MOON)]
        for half, octave in enumerate(basses):
            h0 = t0 + half * bar_m / 2
            env = swell(bar_m / 2 + 1.6, 0.5, 1.8)
            for note in octave:
                sig, _ = tone(hz(note), bar_m / 2 + 1.6,
                              ((1, 1.0), (2, 0.3), (3, 0.08)))
                put(music, h0, sig * env, 0.0, 0.05)
            for note in groups[half * 2]:
                for cents, pan in ((-5, -0.6), (6, 0.6)):
                    sig, _ = tone(hz(note) * 2 ** (cents / 1200),
                                  bar_m / 2 + 1.6, soft)
                    put(music, h0, sig * env, pan, 0.014)
        for k, group in enumerate(groups):
            for j, note in enumerate(group):
                sig, t = tone(hz(note), 2.4, pluck)
                sig *= np.exp(-t / 0.7) * np.minimum(1.0, t / 0.014)
                pan = -0.3 + 0.3 * j
                at = t0 + (k * 3 + j) * MOON_NOTE_S
                put(music, at, sig, pan, 0.08)
                put(music, at + 0.375, sig, -pan, 0.018)
    # Spring: the melody on a bowed voice with the third below it, the
    # bass plucked on every eighth and its chord as a quiet pad.  The
    # first bar holds only the upbeat; the last 3s are a held E major.
    spring = bed == "vivaldi-spring"
    bar_s = 8 * SPRING_EIGHTH_S
    bow = ((1, 1.0), (2, 0.45), (3, 0.22), (4, 0.1), (5, 0.05))
    scale = (64, 66, 68, 69, 71, 73, 75)

    def third_below(note):
        k = scale.index(64 + (note - 64) % 12) - 2
        return note - (note - 64) % 12 + scale[k % 7] - 64 - (12 if k < 0
                                                              else 0)

    def bowed(note, length, at, pan, gain):
        sig, tt = tone(hz(note), length + 0.12, bow)
        sig *= np.minimum(1.0, tt / 0.03) * np.clip(
            (length + 0.12 - tt) / 0.12, 0.0, 1.0)
        sig *= 1.0 + 0.04 * np.sin(2 * np.pi * 5.5 * tt)
        put(music, at, sig, pan, gain)

    count_s = int((span - 3.0) / bar_s) - 1 if spring else 0
    if spring:
        bowed(64, SPRING_EIGHTH_S * 0.9, bar_s - SPRING_EIGHTH_S, -0.2, 0.03)
        for note in (40, 52, 64, 68, 71, 76):
            sig, _ = tone(hz(note), 3.6, soft)
            put(music, (count_s + 1) * bar_s, sig * swell(3.6, 0.05, 2.6),
                0.0, 0.018)
    for bar in range(count_s):
        t0 = (bar + 1) * bar_s
        line, basses = SPRING[SPRING_ORDER[bar % len(SPRING_ORDER)]]
        at = t0
        for note, eighths in line:
            length = eighths * SPRING_EIGHTH_S
            # The last upbeat is left out: the held chord follows.
            if bar < count_s - 1 or at + length < t0 + bar_s - 0.01:
                bowed(note, length * 0.92, at, -0.2, 0.03)
                bowed(third_below(note), length * 0.92, at, 0.3, 0.013)
            at += length
        for half, bass in enumerate(basses):
            h0 = t0 + half * bar_s / 2
            # E, A and B major: the root, its third and its fifth.
            for note in (bass + 12, bass + 16, bass + 19):
                sig, _ = tone(hz(note + 12), bar_s / 2 + 0.3, soft)
                put(music, h0, sig * swell(bar_s / 2 + 0.3, 0.1, 0.3), 0.0,
                    0.007)
            for k in range(4):
                sig, tt = tone(hz(bass), 0.5, pluck)
                sig *= np.exp(-tt / 0.16) * np.minimum(1.0, tt / 0.008)
                put(music, h0 + k * SPRING_EIGHTH_S, sig, 0.0,
                    0.042 if k == 0 else 0.027)
    for bar in range(bars):
        t0 = bar * BAR_S
        bass, notes = CHORDS[bar % len(CHORDS)]
        if bar == bars - 1 or t0 > span - 9.0:
            bass, notes = CHORDS[0]
        env = swell(BAR_S + 3.0, 2.6, 3.2)
        for j, note in enumerate(notes):
            for cents, pan in ((-5, -0.6), (0, 0.0), (6, 0.6)):
                sig, t = tone(hz(note) * 2 ** (cents / 1200), BAR_S + 3.0,
                              soft)
                wob = 1.0 + 0.12 * np.sin(2 * np.pi * (0.11 + 0.03 * j) * t
                                          + j + bar)
                put(music, t0, sig * env * wob, pan, 0.030)
        sig, _ = tone(hz(bass), BAR_S + 3.0, ((1, 1.0), (2, 0.18)))
        put(music, t0, sig * env, 0.0, 0.07)
        sig, _ = tone(hz(notes[1] + 24), BAR_S + 3.0)
        put(music, t0, sig * env * env, 0.3, 0.012)
        if t0 < 5.0 or t0 > span - 10.0:
            continue
        for beat in range(int(BAR_S / BEAT_S)):
            note = notes[order[beat % len(order)]] + 12
            sig, t = tone(hz(note), 1.6, pluck)
            sig *= np.exp(-t / 0.32) * np.minimum(1.0, t / 0.012)
            vel = 0.050 * (0.7 + 0.3 * ((beat * 5 + bar * 3) % 4) / 3.0)
            tb = t0 + beat * BEAT_S
            put(music, tb, sig, -0.5, vel)
            put(music, tb + 0.375, sig, 0.6, vel * 0.42)
            put(music, tb + 0.75, sig, -0.6, vel * 0.18)

    # Read the bed along its place, which lowers its pitch with its
    # speed, and fade it where it nearly stands.
    at = place * RATE
    low = np.minimum(at.astype(int), music.shape[1] - 2)
    part = at - low
    warped = np.zeros_like(music)
    for ch in range(2):
        warped[ch, :n] = ((music[ch, low] * (1.0 - part)
                           + music[ch, low + 1] * part)
                          * np.clip((speed - SLOW) / 0.25, 0.0, 1.0))
    # A storyboard's "music_gain" sets the bed against the event sounds
    # and the narrator.
    music = warped * float(board.get("music_gain", 1.0))

    def chord_at(t):
        t = float(place[min(n - 1, max(0, int(t * RATE)))])
        if spring:
            return (64, 68, 71, 76)
        if canon:
            notes = CANON[int(max(t, 0.0) / CANON_NOTE_S) % len(CANON)][1]
            return notes + (notes[0] + 12,)
        if prelude:
            bar = int(max(t - 1.0, 0.0) / PRELUDE_BAR_S)
            if bar >= count - 1:
                return PRELUDE[-1][1:]
            return PRELUDE[bar % (len(PRELUDE) - 1)][1:]
        return CHORDS[int(max(t, 0.0) / BAR_S) % len(CHORDS)][1]

    bell = ((1, 1.0), (2.76, 0.16), (5.4, 0.05))
    for t, kind, num in events(board):
        notes = chord_at(t)
        if kind == "whoosh":
            sig = noise(0.9, 500.0, 5000.0) * swell(0.9, 0.55, 0.35) ** 2
            put(fx, t, sig, 0.0, 0.085)
        elif kind == "low":
            sig = noise(1.6, 90.0, 700.0) * swell(1.6, 0.9, 0.7) ** 2
            put(fx, t, sig, 0.0, 0.16)
        elif kind in ("chime", "final"):
            run = (0, 2, 3) if kind == "chime" else (0, 1, 2, 3, 0)
            for k, idx in enumerate(run):
                up = 24 if k < 4 else 36
                sig, tt = tone(hz(notes[idx] + up), 2.6, bell)
                sig *= np.exp(-tt / 0.75) * np.minimum(1.0, tt / 0.006)
                put(fx, t + 0.11 * k, sig, (-0.5, 0.5, 0.0)[k % 3], 0.075)
        elif kind == "pop":
            tt = np.arange(int(0.16 * RATE)) / RATE
            f = hz(notes[0] + 12) * (1.0 + 0.5 * np.minimum(1, tt / 0.08))
            sig = np.sin(2 * np.pi * np.cumsum(f) / RATE)
            sig *= np.exp(-tt / 0.045) * np.minimum(1.0, tt / 0.004)
            put(fx, t, sig, 0.25, 0.11)
        elif kind == "tick":
            sig, tt = tone(hz(notes[num % 4] + 36), 0.25)
            sig *= np.exp(-tt / 0.035) * np.minimum(1.0, tt / 0.003)
            put(fx, t, sig, 0.4 - 0.2 * (num % 3), 0.05)
        elif kind == "swell":
            tt = np.arange(int(0.7 * RATE)) / RATE
            f = hz(notes[0]) * (1.0 + 0.335 * tt / 0.7)
            sig = np.sin(2 * np.pi * np.cumsum(f) / RATE)
            put(fx, t, sig * swell(0.7, 0.4, 0.3), -0.2, 0.05)
        elif kind == "tap":
            sig, tt = tone(hz(notes[2] + 24), 0.2, ((1, 1.0), (2, 0.3)))
            sig *= np.exp(-tt / 0.03) * np.minimum(1.0, tt / 0.002)
            put(fx, t, sig, -0.2, 0.13)
        elif kind == "hold":
            tt = np.arange(int(2.0 * RATE)) / RATE
            f = hz(notes[0]) * 2.0 ** (tt / 2.0)
            sig = np.sin(2 * np.pi * np.cumsum(f) / RATE)
            put(fx, t, sig * swell(2.0, 0.5, 0.08) * (0.4 + 0.3 * tt),
                -0.2, 0.045)
        elif kind == "done":
            for k, idx in enumerate((0, 2)):
                sig, tt = tone(hz(notes[idx] + 24), 1.2, bell)
                sig *= np.exp(-tt / 0.3) * np.minimum(1.0, tt / 0.004)
                put(fx, t + 0.09 * k, sig, 0.2, 0.09)

    # A room: 2.4s of decaying noise, convolved in one transform.
    ir_n = int(2.4 * RATE)
    size = 1 << int(np.ceil(np.log2(music.shape[1] + ir_n)))
    out = np.zeros_like(music)
    decay = np.exp(-np.arange(ir_n) / (0.55 * RATE))
    for ch in range(2):
        ir = noise(2.4, 200.0, 6000.0) * decay
        ir /= np.sqrt(np.sum(ir ** 2))
        dry = music[ch] + 0.8 * fx[ch]
        wet = np.fft.irfft(np.fft.rfft(dry, size) * np.fft.rfft(ir, size),
                           size)[: music.shape[1]]
        out[ch] = music[ch] + fx[ch] + 0.33 * wet
    out = out[:, :n]
    if len(clips):
        voice = np.zeros(n + RATE)
        for at, clip in clips:
            i = int(at * RATE)
            voice[i:i + len(clip)] += clip[: len(voice) - i]
        voice = voice[:n]
        # Where a line is, widened by 0.3s and smoothed over 0.4s.
        there = (np.abs(voice) > 0.02).astype(float)
        wide = int(0.3 * RATE)
        csum = np.cumsum(np.concatenate(([0.0], there)))
        idx = np.arange(n)
        there = (csum[np.minimum(n, idx + wide)]
                 - csum[np.maximum(0, idx - wide)]) > 0
        win = np.hanning(int(0.4 * RATE))
        # In one transform: a direct convolution of the whole track with
        # the 17640 samples of the window takes hours.
        kern = win / win.sum()
        size = 1 << int(np.ceil(np.log2(n + len(kern))))
        full = np.fft.irfft(np.fft.rfft(there.astype(float), size)
                            * np.fft.rfft(kern, size), size)
        off = (len(kern) - 1) // 2
        duck = np.clip(full[off:off + n], 0.0, 1.0)
        out *= 0.63 * (1.0 - 0.72 * duck)
        # The voice's level follows the bed's; a silent bed has none.
        peak = float(np.max(np.abs(out)))
        out += 0.9 * voice * (peak / 0.20 if peak > 0 else 1.0)
    t = np.arange(n) / RATE
    out *= np.minimum(1.0, t / 1.5) * np.clip((total - t) / 3.5, 0.0, 1.0)
    # The level: the loudest sample at LEVEL of full scale, no limiter.
    out *= LEVEL / max(1e-9, float(np.max(np.abs(out))))
    data = (out.T * 32767.0).astype("<i2")
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(data.tobytes())
    return total


def main() -> int:
    """Write the sound track of one storyboard.

    Returns:
        The exit status.
    """
    import make_video

    ap = argparse.ArgumentParser()
    ap.add_argument("storyboard")
    ap.add_argument("-o", "--output", type=pathlib.Path, required=True)
    args = ap.parse_args()
    path = pathlib.Path(args.storyboard)
    if not path.suffix:
        path = make_video.VIDEO / "storyboards" / f"{args.storyboard}.json"
    board = json.loads(path.read_text(encoding="utf-8"))
    errors = make_video.check(board)
    if errors:
        sys.exit("\n".join(errors))
    print(f"{render(board, args.output):.1f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
