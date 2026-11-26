"""Per-frame state of the Wurstblinker video, as player.js computes it.

Reads the storyboard as make_video.py hands it to the player (narration
stretch applied) and writes one entry per video frame i, t = i / FPS:

- scene, lt, a: scene index, time in the scene, and the 3D layer's
  opacity (player.js fades its WebGL canvas over FADE seconds at each
  scene change);
- is3d: the scene is a "wurst" or "wurst-kinetic" case scene;
- model: "wurst" or "wurst-kinetic" (3D scenes only);
- cam, target: camera position and look-at point, mm, three.js axes;
- swing, fire: wurst3d.js setSwing(k) and setFire(k), 0 to 1.

Usage:

    python3 wurst_timeline.py STORYBOARD.json OUT.json

SPDX-License-Identifier: MIT
"""

import json
import sys

FPS = 30
FADE = 0.45
MODELS = ("wurst", "wurst-kinetic")


def ss(x):
    """Smoothstep of x clamped to 0..1.

    Args:
        x: The input.

    Returns:
        3x^2 - 2x^3 of the clamped input.
    """
    x = min(1.0, max(0.0, x))
    return x * x * (3 - 2 * x)


def ramp(t, a, b):
    """Smoothstep from 0 at time a to 1 at time b.

    Args:
        t: The time.
        a: The start.
        b: The end.

    Returns:
        The ramp's value at t.
    """
    return ss((t - a) / (b - a))


def track(keys, t):
    """Evaluate a key track the way player.js does.

    Args:
        keys: [time, value] pairs, in time order.
        t: The time in the scene.

    Returns:
        The value, smoothstepped between the keys around t.
    """
    if not keys:
        return 0.0
    if len(keys) == 1:
        return float(keys[0][1])
    i = 0
    while i < len(keys) - 2 and t > keys[i + 1][0]:
        i += 1
    a, b = keys[i], keys[i + 1]
    span = b[0] - a[0]
    f = ss((t - a[0]) / span) if span else (1.0 if t >= b[0] else 0.0)
    return a[1] + (b[1] - a[1]) * f


def scene_times(board):
    """Start and length of each scene.

    Args:
        board: The storyboard.

    Returns:
        A list of (start, length) in seconds, and the total length.
    """
    out, acc = [], 0.0
    for sc in board["scenes"]:
        if sc["type"] in ("screen", "tour"):
            dur = sum(st["dur"] for st in sc["steps"])
        else:
            dur = sc["dur"]
        out.append((acc, dur))
        acc += dur
    return out, acc


def frames(board):
    """Compute the state of every video frame.

    Args:
        board: The storyboard.

    Returns:
        The list of per-frame dicts and the total length in seconds.
    """
    times, total = scene_times(board)
    out = []
    for i in range(round(total * FPS)):
        t = i / FPS
        n = 0
        while n < len(times) - 1 and t >= times[n + 1][0]:
            n += 1
        sc = board["scenes"][n]
        lt = t - times[n][0]
        dur = times[n][1]
        last = n == len(times) - 1
        a = (1.0 if n == 0 else ramp(lt, 0, FADE)) * (
            1.0 if last else 1 - ramp(lt, dur - FADE, dur)
        )
        st = {
            "i": i,
            "t": round(t, 6),
            "scene": n,
            "lt": round(lt, 6),
            "a": a,
            "is3d": sc["type"] == "case" and sc.get("model") in MODELS,
        }
        if st["is3d"]:
            st["model"] = sc["model"]
            k = sc["camera"]
            j = 0
            while j < len(k) - 2 and lt > k[j + 1][0]:
                j += 1
            f = 0.0
            if len(k) > 1:
                f = ss((lt - k[j][0]) / (k[j + 1][0] - k[j][0]))
            j2 = min(j + 1, len(k) - 1)
            st["cam"] = [
                k[j][1][c] + (k[j2][1][c] - k[j][1][c]) * f for c in range(3)
            ]
            st["target"] = [
                k[j][2][c] + (k[j2][2][c] - k[j][2][c]) * f for c in range(3)
            ]
            st["swing"] = track(sc.get("swing"), lt)
            st["fire"] = track(sc.get("fire"), lt)
        out.append(st)
    return out, total


if __name__ == "__main__":
    board = json.load(open(sys.argv[1]))
    fr, total = frames(board)
    json.dump(
        {"total": total, "fps": FPS, "frames": fr}, open(sys.argv[2], "w")
    )
    n3d = sum(f["is3d"] for f in fr)
    print(f"RESULT {len(fr)} frames, {total:.2f}s, 3D frames {n3d}")
