# Videos from storyboards

`tools/make_video.py` renders a storyboard into an MP4 video of
1920 x 1080 pixels at 30 frames per second. A storyboard is a JSON file
under `storyboards/`. It lists scenes; each scene has a type:

| Type | Shows |
| --- | --- |
| `case` | the bench in its case as a 3D model, with a camera path, the lid lifting, labels on named parts, a pin table and a servo being plugged in |
| `screen` | the panel's screens, one step after another, each with a bubble, a pointer and a tap or hold mark |
| `tour` | the panel's screens with an animated caption, a path to the screen, marks that name parts of the display, a spotlight, and chapter cards |
| `end` | a heading, a numbered recap and a note |

The video has a sound track, computed from the storyboard by
`tools/video_audio.py`: synthesizer pads, a sine bass and a plucked
arpeggio over four chords in D major, 8s a chord, and one quiet sound per
event (a scene change, a chapter card, a caption, a point, a mark, a
spotlight, a tap, a hold). It has no percussion. Its loudest sample is at
-6dBFS (decibels relative to full scale); `features` measures -18 LUFS
(loudness units relative to full scale) integrated. The balance between
the bed and the event sounds is set by calculation and has not been judged
by ear.

The screens are drawn by the host renderer (`tools/render_ui.py`) from the
panel's own UI (user interface) code. A step names a renderer view; no
screen is recorded or drawn by hand.

## Commands

```sh
cd tools/video && npm install && cd ../..          # once: three.js 0.170.0
python3 tools/make_video.py kst-rbmod              # build/video/kst-rbmod.mp4
python3 tools/make_video.py features               # build/video/features.mp4
python3 tools/make_video.py kst-rbmod --stills 2.5,40 -o /tmp/stills
python3 tools/make_video.py features --no-sound    # the picture alone
python3 tools/video_audio.py features -o /tmp/features.wav
python3 tools/make_video.py --check                # every storyboard
```

A render needs a C compiler, Pillow, numpy for the sound, Node.js with the
`playwright` package and its Chromium, and ffmpeg. `--check` needs only Python; the
pytest case `test/tools/test_make_video.py` runs it in CI (continuous
integration). The 100.4s of `kst-rbmod` are 3012 frames and the 432.4s of
`features` 12972; the render time on a software rasteriser is not measured.

## Files

| Path | Content |
| --- | --- |
| `../make_video.py` | checks a storyboard, renders its views, serves the player, starts the capture |
| `player/index.html`, `player.css`, `player.js` | draws the frame of a time `t`: `window.renderAt(t)` |
| `player/case3d.js` | the case model, its connectors and its named points |
| `player/capture.js` | steps the player through the frames and pipes them to ffmpeg |
| `storyboards/kst-rbmod.json` | programming a KST servo on the rbmod carrier, 8 steps, 100.4s |
| `storyboards/features.json` | every screen of the bench in 8 chapters: where it opens from and what it does, 432.4s |
| `../video_audio.py` | the sound track of a storyboard as a WAV file |

## Storyboard fields

Times are seconds from the start of the scene. Texts take `<b>` and
`<br>`.

Top level:

| Field | Meaning |
| --- | --- |
| `lang` | `en` or `de`: the language the renderer draws the screens in |
| `brand` | the line beside `rcbench` at the top left |
| `steps` | the count of step marks at the bottom left |
| `chapters` | optional: one name per step; the names replace the step marks, each with a bar that fills while its chapter runs |
| `words` | optional: `tap`, `hold`, `pos`, `gpio`, the player's own words, for another language |
| `scenes` | the scenes in order; each fades in and out over 0.45s |

A `case` scene:

| Field | Meaning |
| --- | --- |
| `dur` | length in s |
| `camera` | keys `[t, [x, y, z], [x, y, z]]`: position and target in mm |
| `lid` | optional keys `[t, mm]`: how far the display is lifted |
| `display` | optional `{view}`: the renderer view on the model's display |
| `title` | optional `{from, to, kicker, heading, text}` |
| `chips` | optional `{from, to, n, text}`: the chip at the top right; a numeric `n` also sets the step marks |
| `labels` | `{from, to, anchor, at, title, text}` and optional `n`, `size` (`sm`), `side` (`l`, `r`, `t`, `b`: where the pointer leaves the bubble), `style` (`ok`, `warn`, `muted`) |
| `pinout` | optional `{from, to, connector, select, subtitle}`: the pin table of `J1` or `J2` with one position marked |
| `servo` | optional `{connector, position, appear, plug: [t0, t1]}`: a servo whose plug moves 46mm into the position |

`anchor` is `[x, y, z]` in mm or a name: `display`, `rbmod`, `pdmini`,
`J1`, `J2`, `xt60`, `usbc`, `estop`, `side`, `plug`. `at` is the bubble's
top left corner on the frame in pixels. The model's origin is the centre
of the case floor; x runs along the 190mm side, y up, z towards the
connector wall.

A view in the tables of `tools/render_ui.py` brings its screen and its
theme. Any other view is a pose of `test/host/render_screen.c` that no
page shows; its step or its scene names the renderer's screen in `screen`
(`servo`, `motor`, `supply` and the others).

A `screen` scene has `steps`:

| Field | Meaning |
| --- | --- |
| `view` | the renderer's view name |
| `dur` | length in s: at least 0.9, 1.4 with a tap, 3.1 with a hold |
| `n`, `chip` | the step number and the chip's text |
| `title`, `text` | the bubble |
| `target` | optional `[x, y]` on the 1024 x 600 display: where the pointer ends and the touch lands |
| `action` | optional `tap` or `hold`; the hold ring fills in 2s |
| `style`, `tag` | optional: `ok` or `warn`, and a warning tag's text |

A `tour` scene has `steps`:

| Field | Meaning |
| --- | --- |
| `card` | `{title, text}`: a chapter card in place of a screen; the step then has only `dur` (at least 2.0) and `n` |
| `view` | the renderer's view name |
| `lang` | optional `en` or `de`: this step's screen in another language than the storyboard's |
| `dur` | length in s: at least 1.5, 2.5 with a spotlight, 1.9 plus 0.38 a mark |
| `n` | the chapter |
| `kicker`, `title`, `text` | the caption; the title's words rise in one after another, the kicker stays while consecutive steps share it |
| `path` | optional list: the way to the screen, `["HOME", "SERVO", "LIVE"]`, drawn as chips |
| `points` | optional list of lines under the text, each with a tick |
| `marks` | optional list of `{at: [x, y], text}` and optional `side` (`l`, `r`, `t`, `b`) and `d`, the line's length in pixels: a dot on the display with a name |
| `focus` | optional `[x, y, w, h]` on the 1024 x 600 display: the rest is dimmed from 0.9s |
| `tag` | optional amber tag, for a limit of what the screen shows |

An `end` scene has `dur`, `heading`, `items` and `note`.

## A video of another feature

1. Find the views the feature's pages show: the names in `SCREENS` of
   `tools/render_ui.py`, each a state of a screen posed in
   `test/host/render_screen.c`. `python3 tools/render_ui.py NAME -o
   /tmp/x.png --lang en` draws one.
2. A state no page shows is added as a pose in `render_screen.c`. The KST
   views take `-rbmod` after a state, `servo-kst-read-rbmod`, for that
   state on the carrier; `-idle`, `-portlist`, `-typed` and `-edited`
   exist for videos only, as does `motor-plot`, the plot of a run on
   MOTOR & ESC with an IOmcu answering.
3. Copy `storyboards/kst-rbmod.json`, replace the steps, and read each
   `target` from the view's PNG.
4. `--stills` at a few times shows the layout before a full render.

## Limits

- The case model is a mockup. Its outer size (190 x 120 x 34mm), the
  order of the connectors on the wall and J1's pin order follow the
  case's CAD (computer-aided design) views; every other dimension is
  estimated. J2's row order is assumed to match J1's and is not
  confirmed.
- One board is modelled: the rbmod carrier in this case.
- The XT60 socket's function is not stated in the tree; the video names
  the socket and its place only.
- The sound has not been judged by ear.
- The player draws 1920 x 1080 only.
