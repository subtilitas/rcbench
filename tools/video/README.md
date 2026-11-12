# Videos from storyboards

`tools/make_video.py` renders a storyboard into an MP4 video of
1920 x 1080 pixels at 30 frames per second. A storyboard is a JSON file
under `storyboards/`. It lists scenes; each scene has a type:

| Type | Shows |
| --- | --- |
| `case` | the bench in its case as a 3D model, with a camera path, the lid lifting, labels on named parts, a pin table and a servo being plugged in |
| `screen` | the panel's screens, one step after another, each with a bubble, a pointer and a tap or hold mark |
| `end` | a heading, a numbered recap and a note |

The screens are drawn by the host renderer (`tools/render_ui.py`) from the
panel's own UI (user interface) code. A step names a renderer view; no
screen is recorded or drawn by hand.

## Commands

```sh
cd tools/video && npm install && cd ../..          # once: three.js 0.170.0
python3 tools/make_video.py kst-rbmod              # build/video/kst-rbmod.mp4
python3 tools/make_video.py kst-rbmod --stills 2.5,40 -o /tmp/stills
python3 tools/make_video.py --check                # every storyboard
```

A render needs a C compiler, Pillow, Node.js with the `playwright`
package and its Chromium, and ffmpeg. `--check` needs only Python; the
pytest case `test/tools/test_make_video.py` runs it in CI (continuous
integration). The 100s of `kst-rbmod` are 3012 frames; the render time on
a software rasteriser is not measured.

## Files

| Path | Content |
| --- | --- |
| `../make_video.py` | checks a storyboard, renders its views, serves the player, starts the capture |
| `player/index.html`, `player.css`, `player.js` | draws the frame of a time `t`: `window.renderAt(t)` |
| `player/case3d.js` | the case model, its connectors and its named points |
| `player/capture.js` | steps the player through the frames and pipes them to ffmpeg |
| `storyboards/kst-rbmod.json` | programming a KST servo on the rbmod carrier, 8 steps, 100.4s |

## Storyboard fields

Times are seconds from the start of the scene. Texts take `<b>` and
`<br>`.

Top level:

| Field | Meaning |
| --- | --- |
| `lang` | `en` or `de`: the language the renderer draws the screens in |
| `brand` | the line beside `rcbench` at the top left |
| `steps` | the count of step marks at the bottom left |
| `words` | optional: `tap`, `hold`, `pos`, `gpio`, the player's own words, for another language |
| `scenes` | the scenes in order; each fades in and out over 0.45s |

A `case` scene:

| Field | Meaning |
| --- | --- |
| `dur` | length in s |
| `camera` | keys `[t, [x, y, z], [x, y, z]]`: position and target in mm |
| `lid` | optional keys `[t, mm]`: how far the display is lifted |
| `display` | optional `{view, screen}`: the renderer view on the model's display |
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

A `screen` scene has `screen`, the renderer's screen name (`servo`,
`supply`, `outputs` and the others `tools/render_ui.py` lists), and
`steps`:

| Field | Meaning |
| --- | --- |
| `view` | the renderer's view name |
| `dur` | length in s: at least 0.9, 1.4 with a tap, 3.1 with a hold |
| `n`, `chip` | the step number and the chip's text |
| `title`, `text` | the bubble |
| `target` | optional `[x, y]` on the 1024 x 600 display: where the pointer ends and the touch lands |
| `action` | optional `tap` or `hold`; the hold ring fills in 2s |
| `style`, `tag` | optional: `ok` or `warn`, and a warning tag's text |

An `end` scene has `dur`, `heading`, `items` and `note`.

## A video of another feature

1. Find the views the feature's pages show: the names in `SCREENS` of
   `tools/render_ui.py`, each a state of a screen posed in
   `test/host/render_screen.c`. `python3 tools/render_ui.py NAME -o
   /tmp/x.png --lang en` draws one.
2. A state no page shows is added as a pose in `render_screen.c`. The KST
   views take `-rbmod` after a state, `servo-kst-read-rbmod`, for that
   state on the carrier; `-idle`, `-portlist`, `-typed` and `-edited`
   exist for videos only.
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
- The video has no sound.
- The player draws 1920 x 1080 only.
