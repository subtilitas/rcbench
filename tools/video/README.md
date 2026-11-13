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
spotlight, a tap, a hold). While a case scene holds a component in the
air, the music stops as a record does under a hand: its speed and pitch
fall to 5% over 1.1s and return over 0.7s. It has no percussion. Its loudest sample is at
-3dBFS (decibels relative to full scale); `features` measures -20 LUFS
(loudness units relative to full scale) integrated. The balance between
the bed and the event sounds is set by calculation and has not been judged
by ear.

A storyboard with `voice` is narrated by `tools/video_voice.py`. The
speaker is Kokoro, an open text-to-speech model of 82 million parameters
that runs on the machine through the `kokoro-onnx` package; no text is
sent to a service. Each step says its title and text, or its `say`; a
step shorter than its sentence is stretched, and the bed is lowered by
11dB under a line. The model's two files, 354MB together, are not in the
tree: `kokoro-v1.0.onnx` and `voices-v1.0.bin` from the release
`model-files-v1.0` of `github.com/thewh1teagle/kokoro-onnx` go into
`tools/video/tts/` or the directory `RCBENCH_TTS_DIR` names. A speech
recogniser (Whisper small.en) returned the lines of `features` with
their words and names; the voice has not been judged by ear.

The screens are drawn by the host renderer (`tools/render_ui.py`) from the
panel's own UI (user interface) code. A step names a renderer view; no
screen is recorded or drawn by hand.

## Commands

```sh
cd tools/video && npm install && cd ../..          # once: three.js 0.170.0
python3 tools/make_video.py kst-rbmod              # build/video/kst-rbmod.mp4
python3 tools/make_video.py features               # build/video/features.mp4
python3 tools/make_video.py kst-rbmod --stills 2.5,40 -o /tmp/stills
python3 tools/make_video.py features --jobs 3      # frames in 3 parts at once
python3 tools/make_video.py features --jobs 3 --keep build/video/parts
                                                   # parts of 900 frames are kept; a
                                                   # run cut off continues from them
python3 tools/make_video.py features --music bach-prelude,pachelbel-canon
                                                   # one video per bed
python3 tools/make_video.py features --no-voice    # no narrator, lengths as written
python3 tools/make_video.py features --no-sound    # the picture alone
python3 tools/video_voice.py features              # the lines as spoken
python3 tools/video_audio.py features -o /tmp/features.wav
python3 tools/make_video.py --check                # every storyboard
```

A render needs a C compiler, Pillow, numpy for the sound, `kokoro-onnx`
and its model for the narration, Node.js with the `playwright` package and
its Chromium, and ffmpeg. `--check` needs only Python; the
pytest case `test/tools/test_make_video.py` runs it in CI (continuous
integration). The 100.4s of `kst-rbmod` are 3012 frames. `features` is
694s with its narration, 20814 frames; the render time on a software rasteriser is not measured.

## Files

| Path | Content |
| --- | --- |
| `../make_video.py` | checks a storyboard, renders its views, serves the player, starts the capture |
| `player/index.html`, `player.css`, `player.js` | draws the frame of a time `t`: `window.renderAt(t)` |
| `player/case3d.js` | the case as a mockup of boxes, its connectors and its named points |
| `player/case_cad.js` | the case from its CAD (computer-aided design) assembly: colours by part name, merged meshes, the same named points |
| `../video_cad.py` | converts the case's STEP assembly into `cad/case.glb`, the model `case_cad.js` loads |
| `player/capture.js` | steps the player through the frames and pipes them to ffmpeg |
| `storyboards/kst-rbmod.json` | programming a KST servo on the rbmod carrier, 8 steps, 100.4s |
| `storyboards/features.json` | every screen of the bench in 8 chapters: where it opens from and what it does on the rbmod carrier, narrated, 694s |
| `../video_audio.py` | the sound track of a storyboard as a WAV file |
| `../video_voice.py` | the narration: the lines, the speaker, the room each line needs |

## Storyboard fields

Times are seconds from the start of the scene. Texts take `<b>` and
`<br>`.

Top level:

| Field | Meaning |
| --- | --- |
| `lang` | `en` or `de`: the language the renderer draws the screens in |
| `brand` | the line beside `rcbench` at the top left |
| `steps` | the count of step marks at the bottom left |
| `music` | optional: `pads`, the default bed; `bach-prelude`, J. S. Bach's Prelude in C major BWV 846 on the same voices, one bar every 4s; or `pachelbel-canon`, Johann Pachelbel's Canon in D major, its ground bass at 2s a note under three of the violin's variations. Both compositions are in the public domain and no recording is used; the notes are set down in `tools/video_audio.py`. Bars 33 and 34 of the prelude keep the pattern of the other bars so that the piece loops |
| `voice` | optional `{name, lang, speed}`: the narrator, a voice of Kokoro such as `af_heart` with `en-us`, or `bm_george` with `en-gb`. `blend` in place of `name` mixes voices by weight: `{"af_heart": 0.6, "af_nicole": 0.4}` |
| `chapters` | optional: one name per step; the names replace the step marks, each with a bar that fills while its chapter runs |
| `words` | optional: `tap`, `hold`, `pos`, `gpio`, the player's own words, for another language |
| `scenes` | the scenes in order; each fades in and out over 0.45s |

A `case` scene:

| Field | Meaning |
| --- | --- |
| `dur` | length in s |
| `model` | optional: `mockup`, the default, or `cad`, the assembly converted by `tools/video_cad.py` |
| `camera` | keys `[t, [x, y, z], [x, y, z]]`: position and target in mm |
| `lid` | optional keys `[t, mm]`: how far the display is lifted |
| `parts` | optional, `cad` model only: keys `[t, k]` per component, `k` from 1, out of sight along the component's way in, to 0, seated. A value held between two keys freezes the component in the air while the camera moves on. Components: `case`, `bank`, `estop`, `xt60`, `usbc`, `pdmini`, `rbmod`, `mcu`, `ina`, `buck`, `trigger`, `lid` |
| `display` | optional `{view}`: the renderer view on the model's display |
| `title` | optional `{from, to, kicker, heading, text}` |
| `chips` | optional `{from, to, n, text}`: the chip at the top right; a numeric `n` also sets the step marks |
| `labels` | `{from, to, anchor, at, title, text}` and optional `n`, `size` (`sm`), `side` (`l`, `r`, `t`, `b`: where the pointer leaves the bubble), `style` (`ok`, `warn`, `muted`) |
| `pinout` | optional `{from, to, connector, select, subtitle}`: the pin table of `J1` or `J2` with one position marked |
| `say` | optional list of `{at, text}`: what the narrator says in this scene and when; a line that runs into the next is an error |
| `servo` | optional `{connector, position, appear, plug: [t0, t1]}`: a servo whose plug moves 46mm into the position |

`anchor` is `[x, y, z]` in mm or a name: `display`, `rbmod`, `pdmini`,
`J1`, `J2`, `xt60`, `usbc`, `estop`, `side`, `plug`, and on the `cad`
model also `panel`, `powerbank`, `case`, `mcu`, `ina`, `buck` and
`trigger`. An anchor follows its component while it moves. A label's
optional `glow` names a component that is lit while the label shows. `at` is the bubble's
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
| `say` | optional: what the narrator says in place of the title and the text |

An `end` scene has `dur`, `heading`, `items`, `note` and an optional
`say`.

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

- The `cad` model is the case's STEP assembly, 172 x 129 x 55mm with the
  power bank under it. The file has no colours: `case_cad.js` sets them
  by part name, and a part without a name is dark grey, or green where
  it is a plate. The STEP file (65MB) and the converted model (35MB)
  are not in the tree:

  ```sh
  pip install cadquery-ocp==8.0.1.1.0
  python3 tools/video_cad.py rcbench_case.stp     # tools/video/cad/case.glb
  ```

  The `cad` model has no servo and no pin table: `servo` and `pinout`
  need the mockup.
- The `mockup` model is estimated: 190 x 120 x 34mm against the 172 x
  129mm of the assembly. The order of the connectors on the wall and
  J1's pin order follow the case. J2's row order is assumed to match
  J1's and is not confirmed.
- One board is modelled: the rbmod carrier in this case. `features` shows
  that build only: the board wired by hand, its pin drawing and the
  Hitec MOVE SERVO, which needs a PD mini on the port, are not in it.
- The XT60 socket's function is not stated in the tree; the video names
  the socket and its place only.
- The sound and the narrator's voice have not been judged by ear.
- BALANCE is shown as to be done; RECEIVER and BATTERY are shown without
  their state of work, which the README's feature table holds.
- The player draws 1920 x 1080 only.
