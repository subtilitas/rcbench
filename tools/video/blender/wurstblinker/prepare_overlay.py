"""Make the player's 2D layer capturable: a player copy that draws no 3D.

Usage:

    python3 prepare_overlay.py REPO NARRATED_DIR OUT_DIR

Copies REPO/tools/video/player into OUT_DIR with two edits: player.js
skips renderer.render() (the matrices stay current, so the labels still
point at the model), and index.html hides the background and the WebGL
canvas.  Adds the narrated storyboard.json, its shots and a link to
REPO/tools/video/node_modules.  Serve OUT_DIR with
`python3 -m http.server PORT --bind 127.0.0.1` and capture it with
overlay_capture.js.

SPDX-License-Identifier: MIT
"""

import pathlib
import shutil
import sys

repo, narrated, out = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
shutil.rmtree(out, ignore_errors=True)
shutil.copytree(repo / "tools/video/player", out)
js = (out / "player.js").read_text(encoding="utf-8")
old = "  renderer.render(m.scene, m.cam);"
if js.count(old) != 1:
    sys.exit("player.js: the render call is not where this script expects")
new = "  m.scene.updateMatrixWorld(true); m.cam.updateMatrixWorld(true);"
(out / "player.js").write_text(js.replace(old, new), encoding="utf-8")
html = (out / "index.html").read_text(encoding="utf-8")
style = (
    '<style id="ovl">html,body{background:transparent!important}'
    "#bg,#grid,#gl{display:none!important}</style>\n"
)
(out / "index.html").write_text(
    html.replace("</head>", style + "</head>", 1), encoding="utf-8"
)
shutil.copy(narrated / "storyboard.json", out / "storyboard.json")
shutil.copytree(narrated / "shots", out / "shots")
(out / "node_modules").symlink_to(repo / "tools/video/node_modules")
print("RESULT overlay", out)
