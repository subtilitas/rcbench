# Wurstblinker video in Blender

These scripts render the storyboard `wurstblinker`
(`tools/video/storyboards/wurstblinker.json`) with Blender instead of the
browser player. The 3D scenes come from the two STEP models
`hardware/mechanical/models/parts/wurstblinker_node.step` (16 solids) and
`wurstblinker_kinetic.step` (18 solids), drawn in the cartoon look of
`tools/video/player/wurst3d.js`. The 2D layer (titles, chips, labels, the
screen tours and the end card) is the player's own, captured without its
3D canvas. Blender's sequencer lays the layers and the sound together.

The video is 115.34s long: 3460 frames of 1920 x 1080 pixels at 30
frames per second (fps), 2588 of them 3D (frames 1-1004 and 1638-3221).
Its 3D scenes use 3 models: `wurst` (the node), `wurst-dog` (the node on
a 620mm mast over a dog) and `wurst-kinetic` (the kinetic node with the
traffic light). The MP4
is not in the tree.

## The look

| Feature | Blender | Player (`wurst3d.js`) |
| --- | --- | --- |
| Tones | 3 per colour, picked by n.l at the ramp's steps; colours computed in Python and emitted | `MeshToonMaterial`, ramp 110/190/255 |
| Light | ambient 1.5, sun 2.4 from (-160, 300, 260), no specular | the same |
| Tone mapping | three.js's ACES filmic curve at exposure 1.15, applied to each tone in Python; view transform Standard | `ACESFilmicToneMapping`, exposure 1.15 |
| Outline | inverted hull 1.8mm out along the vertex normals, back faces culled, 0x10131c | the same |
| Discs | 0xfff1c2: radius 360mm at (90, 150, -140); with `wurst-kinetic` also 300mm at (1150, 150, -160); with `wurst-dog` also 560mm at (120, -330, -150) | the same |
| Ground shadow | opacity 0.78 in linear light, inside +-300mm and 10mm to 900mm from the sun | `ShadowMaterial` 0.5 on the sRGB canvas, shadow camera +-300mm, 10mm to 900mm |

Measured against the player's stills at 57.6s and 58.64s (the dog up and
down), 75.5s (the shot), 76.08s (the hit), 76.98s and 80.0s (the sausage
on the ground), 96.4s and 111.0s: 97.7% to 100% of the pixels are within
8 of 255 of the player's, the mean difference is 0.1 to 2.7 of 255. A
part's colour differs by at most 1 of 255; the shadow over the disc reads
115 against 116.

Geometry: `build_wurst.py` reads the solids in the STEP's order and rounds
what the cartoon draws round. The sausage and the tank become capsules of
their solid's size, the antenna's tip and the lamp balls, the 2 clips and
the barrel's 2 rings tori; the boxes get the edge radii of `wurst3d.js`.
The sausage's knots, the gauge's needle, the barrel's dark bore, the 3
strokes and the 4 puffs come from `wurst3d.js`, and so do the parts that
have no STEP model: the mast and its foot, the dog (body, head, snout,
nose, eye, 2 ears, 4 legs, tail) and the traffic light (foot, pole,
housing 60 x 170 x 50mm, 3 lamps of 18mm radius). The barrel's rings take
the player's 0x5f6a80; the STEP gives them 0x232833.

Motion, per frame from `wurst_timeline.py`: the camera; the arm turning
about the servo's shaft (STEP x = 40mm, z = 128mm, axis along y) by
-90° x `swing`; the lamp's glow; the strokes while `swing` > 0.85; in
`wurst-dog` the dog 260mm x `swing` up from the ground 620mm below the
stand, tilted -0.35 - 0.45 x `swing` rad, the ground with it. The shot,
with u = min(k / 0.6, 1) and v = max((k - 0.6) / 0.4, 0): x = 120 + 1000u
- 260v, height 134 - 14u² + 160v - 266v², 1 turn over v, hidden at k = 0.
At k = 0.6 its end meets the traffic light, which then swings about its
foot by -0.12rad x e^(-3v) x sin(14v); amber takes over from red. The
puffs are scaled by sin(πk / 0.2) for k < 0.2.

## Files

| File | Does |
| --- | --- |
| `narrate.py` | narration, stretched storyboard, screen shots and sound track, as `make_video.py` makes them |
| `wurst_timeline.py` | the state of every frame, as `player.js` computes it |
| `build_wurst.py` | the Blender scene: models, materials, outlines, disc, ground, camera, keys |
| `render_frames.py` | renders a frame range to PNG; skips complete frames |
| `prepare_overlay.py` | a copy of the player that draws no 3D, for the 2D layer |
| `overlay_capture.js` | captures the 2D layer per frame, or its background (Playwright, SwiftShader) |
| `assemble.py` | Blender's sequencer: background, 3D, 2D and sound into the MP4 |

## Rebuild

Needs Blender 5.3.0, ffmpeg, Node.js 24 with `npm install` run in
`tools/video/`, a Python 3.12 with `cadquery-ocp==8.0.1.1.0` and numpy
for the STEP conversion, and a Python with `kokoro-onnx`, `misaki` and
numpy for the narration (`tools/video_voice.py`). Paths below are from
the repository's root; `W` is a work directory outside the tree.

```
W=/tmp/wurst; D=tools/video/blender/wurstblinker; mkdir -p $W
for m in node kinetic; do
  python3 tools/video_cad.py --solids \
    hardware/mechanical/models/parts/wurstblinker_$m.step -o $W/wurstblinker_$m.glb
done
python3 $D/narrate.py . wurstblinker $W/en
python3 $D/wurst_timeline.py $W/en/storyboard.json $W/timeline.json
cat > $W/cfg.json <<J
{"timeline": "$W/timeline.json", "glb_node": "$W/wurstblinker_node.glb",
 "glb_kinetic": "$W/wurstblinker_kinetic.glb", "out": "$W/wurst.blend"}
J
B="blender -b --factory-startup --python-exit-code 1"
$B --python $D/build_wurst.py -- $W/cfg.json
for r in 1-1004 1638-3221; do
  $B $W/wurst.blend --python $D/render_frames.py -- $W/frames $r
done
python3 $D/prepare_overlay.py . $W/en $W/overlay
(cd $W/overlay && python3 -m http.server 8751 --bind 127.0.0.1) &
NODE_PATH=tools/video/node_modules node $D/overlay_capture.js \
  http://127.0.0.1:8751/ frames $W/ovl 0 3460
NODE_PATH=tools/video/node_modules node $D/overlay_capture.js \
  http://127.0.0.1:8751/ bg $W/bg.png
$B --python $D/assemble.py -- $W/timeline.json $W/bg.png $W/frames \
  $W/ovl $W/en/audio.m4a $W/wurstblinker.mp4
```

`overlay_capture.js` takes a frame range, so the 2D layer can be split
over several processes. The 3D frame ranges follow from the timeline:
`build_wurst.py` prints them as `segments`. The sequencer re-encodes the
sound; `ffmpeg -i video.mp4 -i $W/en/audio.m4a -map 0:v -map 1:a -c copy`
puts the narrated track back unchanged.

Measured times:

| Step | Setup | Time |
| --- | --- | --- |
| Narration, 16 lines | CPU | 105s |
| Scene build | one Blender process | under 2s |
| 3D frames, 2588 | 7 AMD Instinct MI50 cards, one Blender process a card on its own frame range, EEVEE on Vulkan, 4 threads, 32 samples | 332s (0.77s to 0.88s a frame on average per card, 1.51s the slowest frame, at most 2844MB of video memory a card) |
| 2D layer, 3460 frames | 8 Chromium processes on SwiftShader | 88s |
| Assembly | one Blender process, 8 threads | 303s |

## Current limitations

- The pipe and the gauge have the STEP's sizes: the pipe ends at 123mm,
  where `wurst3d.js` draws it to 133mm.
- EEVEE leaves about 0.46 of the sun's light in a shadow; the ground's
  shader treats everything under 0.55 of the lit value as full shadow
  and ramps to no shadow at 0.9.
- The lamp's glow blends two tone-mapped colours by the glow's value; the
  player tone maps the sum. The two agree at 0 and 1; between them the
  difference is not measured.
- Outside the 3D scenes Blender holds the last 3D frame's state; those
  frames are not rendered.
