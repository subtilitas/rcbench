r"""Render frames of the open .blend to OUT/NNNNN.png (RGBA).

Usage:

    blender -b X.blend --factory-startup --python-exit-code 1 \
        --python render_frames.py -- OUT FIRST-LAST

A run that was cut off continues where it stopped: each frame is written
to a temporary file and renamed into place, and a frame whose PNG ends
with its IEND chunk is not rendered again.

SPDX-License-Identifier: MIT
"""

import os
import sys
import time

import bpy

out, spec = sys.argv[sys.argv.index("--") + 1 :][:2]
a, b = map(int, spec.split("-"))
os.makedirs(out, exist_ok=True)
sc = bpy.context.scene


def complete(path):
    """Tell whether a file holds a whole PNG.

    Args:
        path: The file.

    Returns:
        True when the PNG signature starts it and the IEND chunk ends it.
    """
    try:
        with open(path, "rb") as h:
            head = h.read(8)
            h.seek(0, 2)
            if h.tell() < 20:
                return False
            h.seek(-12, 2)
            return head == b"\x89PNG\r\n\x1a\n" and (
                h.read(12) == b"\x00\x00\x00\x00IEND\xaeB`\x82"
            )
    except OSError:
        return False


t0, n = time.time(), 0
for f in range(a, b + 1):
    dst = os.path.join(out, f"{f:05d}.png")
    if complete(dst):
        continue
    sc.frame_set(f)
    tmp = f"{dst}.{os.getpid()}.tmp.png"
    sc.render.filepath = tmp
    bpy.ops.render.render(write_still=True)
    os.replace(tmp, dst)
    n += 1
wall = time.time() - t0
print("RESULT frames", n, "of", b - a + 1, f"wall {wall:.1f}s", flush=True)
