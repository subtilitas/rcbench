r"""Assemble the video in Blender's sequencer: background, 3D, 2D, sound.

Strips, bottom to top:

1. the player's background (one image for every frame);
2. Blender's 3D frames, one image strip per run of 3D frames, alpha
   over, the strip's opacity keyed per frame to the timeline's "a"
   (player.js fades its WebGL canvas so);
3. the player's 2D layer (overlay frames), alpha over;
4. the narration and music track (a sound strip).

Images are straight alpha and blend in the sequencer's sRGB space.  View
transform Standard, look None, exposure 0: the sequencer's pixels go out
unchanged.  Frame rate and size come from the timeline (30fps,
1920 x 1080 pixels).

Usage (headless, no GPU needed):

    blender -b --factory-startup --python-exit-code 1 \
        --python assemble.py -- TIMELINE BG FRAMES3D OVERLAY AUDIO OUT \
        [FIRST-LAST] [--png DIR]

OUT is an MP4: H.264 at constant rate factor 17 (CRF env overrides),
yuv420p, AAC 192kbit/s.  FIRST-LAST (1-based) assembles a span; the
sound strip then starts at FIRST's time.  --png DIR writes the span as
PNG frames instead of the MP4.  Prints RESULT lines: frames, the sound
strip's length against the picture's, the output's path.

SPDX-License-Identifier: MIT
"""

import json
import os
import sys

import bpy


def args():
    """Return the arguments after the command line's "--"."""
    return sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []


def result(*parts):
    """Print one greppable RESULT line.

    Args:
        *parts: The words of the line.
    """
    print("RESULT", *parts, flush=True)


def set_view_transform(scene, wanted=("Standard",)):
    """Set the colour view transform inside try/except.

    Headless Blender under-reports the enum's values.

    Args:
        scene: The scene.
        wanted: The names to try, in order.

    Returns:
        The name Blender accepted.

    Raises:
        TypeError: Blender accepts none of them.
    """
    for name in wanted:
        try:
            scene.view_settings.view_transform = name
            return name
        except TypeError:
            pass
    raise TypeError(f"view transform: none of {wanted}")


a = args()
png_dir = None
if "--png" in a:
    k = a.index("--png")
    png_dir = a[k + 1]
    a = a[:k] + a[k + 2 :]
TL, BG, F3D, OVL, AUDIO, OUT = a[:6]
tl = json.load(open(TL))
FR = tl["frames"]
FPS = int(tl.get("fps", 30))
N = len(FR)
first, last = map(int, a[6].split("-")) if len(a) > 6 else (1, N)

sc = bpy.context.scene
se = sc.sequence_editor_create()
strips = se.strips
r = sc.render
r.resolution_x, r.resolution_y, r.resolution_percentage = 1920, 1080, 100
r.fps, r.fps_base = FPS, 1.0
r.use_sequencer = True
r.use_compositing = False
r.film_transparent = False
sc.frame_start, sc.frame_end = first, last
result("view_transform", set_view_transform(sc))
sc.view_settings.look = "None"
sc.view_settings.exposure = 0.0
sc.view_settings.gamma = 1.0
result("sequencer_colorspace", sc.sequencer_colorspace_settings.name)


def image_strip(name, folder, files, channel, start):
    """Add an image sequence strip, straight alpha, alpha over.

    Args:
        name: The strip's name.
        folder: The directory of the images.
        files: The image file names, one a frame.
        channel: The sequencer channel.
        start: The first frame.

    Returns:
        The strip.
    """
    s = strips.new_image(
        name=name,
        filepath=os.path.join(folder, files[0]),
        channel=channel,
        frame_start=start,
    )
    for f in files[1:]:
        s.elements.append(f)
    s.frame_final_duration = len(files)
    s.alpha_mode = "STRAIGHT"
    s.blend_type = "ALPHA_OVER"
    return s


# 1 background
bgs = strips.new_image(
    name="bg", filepath=os.path.abspath(BG), channel=1, frame_start=first
)
bgs.frame_final_duration = last - first + 1
bgs.blend_type = "REPLACE"

# 2 the 3D layer, per run of 3D frames within the span
runs, cur = [], None
for f in FR[first - 1 : last]:
    n = f["i"] + 1
    if f["is3d"]:
        if cur and cur[-1] == n - 1:
            cur.append(n)
        else:
            cur = [n]
            runs.append(cur)
for k, run in enumerate(runs):
    files = [f"{n:05d}.png" for n in run]
    missing = [x for x in files if not os.path.exists(os.path.join(F3D, x))]
    if missing:
        raise SystemExit(
            f"missing 3D frames in {F3D}: {missing[:5]} ({len(missing)})"
        )
    s = image_strip(f"3d_{k}", os.path.abspath(F3D), files, 2, run[0])
    vals = [FR[n - 1]["a"] for n in run]
    for j, n in enumerate(
        run
    ):  # keys where the opacity changes, and around each change
        if (
            j == 0
            or j == len(run) - 1
            or vals[j] != vals[j - 1]
            or vals[j] != vals[j + 1]
        ):
            s.blend_alpha = vals[j]
            s.keyframe_insert("blend_alpha", frame=n)
    result("3d_run", run[0], run[-1], "faded_frames", sum(v < 1 for v in vals))

# 3 the 2D layer
files = [f"{n:05d}.png" for n in range(first, last + 1)]
missing = [x for x in files if not os.path.exists(os.path.join(OVL, x))]
if missing:
    raise SystemExit(
        f"missing overlay frames in {OVL}: {missing[:5]} ({len(missing)})"
    )
image_strip("overlay", os.path.abspath(OVL), files, 3, first)

# 4 sound: the track starts at frame 1 (time 0); a span starts it at its
# own time.
snd = strips.new_sound(
    name="track", filepath=os.path.abspath(AUDIO), channel=4, frame_start=1
)
if first > 1:
    snd.frame_offset_start = first - 1
    snd.frame_start = 1
snd_len = snd.frame_final_duration
result(
    "sound",
    AUDIO,
    "frames",
    snd_len,
    "seconds",
    round(snd_len / FPS, 3),
    "picture_frames",
    N,
    "seconds",
    round(N / FPS, 3),
    "difference_frames",
    snd_len - N + (first - 1 if first > 1 else 0),
)

if png_dir:
    r.image_settings.file_format = "PNG"
    r.image_settings.color_mode = "RGB"
    r.filepath = os.path.join(os.path.abspath(png_dir), "#####")
else:
    r.image_settings.media_type = (
        "VIDEO"  # Blender 5.x: movie formats need the media type first
    )
    r.image_settings.file_format = "FFMPEG"
    r.ffmpeg.format = "MPEG4"
    r.ffmpeg.codec = "H264"
    r.ffmpeg.constant_rate_factor = "CUSTOM"
    r.ffmpeg.custom_constant_rate_factor = int(os.environ.get("CRF", 17))
    r.ffmpeg.ffmpeg_preset = "GOOD"
    r.ffmpeg.audio_codec = "AAC"
    r.ffmpeg.audio_bitrate = 192
    r.ffmpeg.audio_channels = "STEREO"
    r.ffmpeg.audio_mixrate = 44100
    r.filepath = os.path.abspath(OUT)
    r.use_file_extension = False
result("render", first, last, "to", r.filepath)
bpy.ops.render.render(animation=True)
result("done", r.filepath)
