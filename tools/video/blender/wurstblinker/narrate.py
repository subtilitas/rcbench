"""Narrate a storyboard as make_video.py does, without a browser capture.

Runs the steps of make_video.main() that come before the capture: the
narration (video_voice.narrate, lines kept in
REPO/build/video/cache/voice), the drawn views (make_video.cached_views,
kept in REPO/build/video/cache/views) and the sound track
(video_audio.render) from the storyboard those two leave.

Usage (a Python with kokoro-onnx, misaki and numpy):

    python3 narrate.py REPO NAME OUT_DIR

Writes, in OUT_DIR:

- storyboard.json: stretched, as the player gets it;
- shots/*.png: the screens of the tours;
- track.wav and audio.m4a (AAC 192kbit/s, as make_video.py muxes it);
- clips.json: start and length in seconds of each narration clip.

Prints the scene start times of the stretched storyboard.

SPDX-License-Identifier: MIT
"""

import json
import pathlib
import shutil
import subprocess
import sys

repo = pathlib.Path(sys.argv[1]).resolve()
name = sys.argv[2]
out = pathlib.Path(sys.argv[3]).resolve()
sys.path.insert(0, str(repo / "tools"))
import make_video  # noqa: E402
import video_audio  # noqa: E402
import video_voice  # noqa: E402

board = make_video.load(
    repo / "tools" / "video" / "storyboards" / f"{name}.json"
)
board, clips = video_voice.narrate(board, make_video.CACHE / "voice")
shots = out / "shots"
shutil.rmtree(shots, ignore_errors=True)
shots.mkdir(parents=True)
board = make_video.cached_views(board, shots, make_video.CACHE / "views")
(out / "storyboard.json").write_text(json.dumps(board), encoding="utf-8")
rate = getattr(video_voice, "RATE", None) or video_audio.RATE
(out / "clips.json").write_text(
    json.dumps([[float(t), len(c) / rate] for t, c in clips]),
    encoding="utf-8",
)
length = video_audio.render(board, out / "track.wav", clips)
subprocess.run(
    ["ffmpeg", "-v", "error", "-y", "-i", str(out / "track.wav"),
     "-c:a", "aac", "-b:a", "192k", str(out / "audio.m4a")],
    check=True,
)
times = video_audio.scene_times(board)
for n, (start, dur) in enumerate(times):
    kind = board["scenes"][n]["type"]
    print(f"RESULT scene {n + 1} {kind} start {start:.3f}s dur {dur:.3f}s")
total = sum(d for _, d in times)
print(f"RESULT total {total:.3f}s track {length:.3f}s")
