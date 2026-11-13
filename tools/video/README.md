# Videos from storyboards

`tools/make_video.py` renders a storyboard into an MP4 video of
1920 x 1080 pixels at 30 frames per second. A storyboard is a JSON file
under `storyboards/`. It lists scenes; each scene has a type:

| Type | Shows |
| --- | --- |
| `case` | the bench in its case as a 3D model, with a camera path, the lid lifting, labels on named parts, a pin table and a servo being plugged in |
| `board` | a circuit board seen flat from its front, with a view that moves over it, labels and tags on pads, parts taking their place, solder joints, a cross-section in a card, a net lit up, a track cut, a wire and a part soldered across two pads |
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
python3 tools/make_video.py rbmod-rework           # build/video/rbmod-rework.mp4
python3 tools/make_video.py rbmod-build            # build/video/rbmod-build.mp4
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

The frames are drawn by SwiftShader, a software rasteriser. With
`RCBENCH_VIDEO_GL` set to Chromium flags, such as
`--use-angle=vulkan --enable-features=Vulkan`, they are drawn on a GPU
(graphics processing unit); each capture job prints the renderer it got.
A GPU render is not tested, and its pixels can differ from SwiftShader's,
so one video takes one of the two.

A render needs a C compiler, Pillow, numpy for the sound, `kokoro-onnx`
and its model for the narration, Node.js with the `playwright` package and
its Chromium, and ffmpeg. `--check` needs only Python; the
pytest case `test/tools/test_make_video.py` runs it in CI (continuous
integration). The 100.4s of `kst-rbmod` are 3012 frames. `features` is
755.92s with its narration, 22678 frames. On a software rasteriser its render
takes about 60 min with 3 jobs on 4 cores and 43 min with 12 jobs on 48 cores.

## Files

| Path | Content |
| --- | --- |
| `../make_video.py` | checks a storyboard, renders its views, serves the player, starts the capture |
| `player/index.html`, `player.css`, `player.js` | draws the frame of a time `t`: `window.renderAt(t)` |
| `player/case3d.js` | the case as a mockup of boxes, its connectors and its named points |
| `player/case_cad.js` | the case from its CAD (computer-aided design) assembly: colours by part name, merged meshes, the same named points |
| `../video_cad.py` | converts the case's STEP assembly into `cad/case.glb`, the model `case_cad.js` loads |
| `player/board.js` | a board drawn flat as SVG, and the rework on it |
| `../video_board.py` | converts a KiCad board file into `cad/NAME.json`, the board `board.js` draws |
| `boards/p4-7b-back.json` | the back of the 7 inch ESP32-P4 display board, drawn by hand in the format `video_board.py` writes: its connectors, modules and sockets as boxes, the pads of the 4-pin and 12-pin headers |
| `player/capture.js` | steps the player through the frames and pipes them to ffmpeg |
| `storyboards/kst-rbmod.json` | programming a KST servo on the rbmod carrier, 8 steps, 100.4s |
| `storyboards/features.json` | every screen of the bench in 8 chapters: where it opens from and what it does on the rbmod carrier, narrated, 755.92s |
| `storyboards/rbmod-rework.json` | the rework of a rev 1 rbmod carrier in 4 steps: the track cut at R1, the wire from GP15, the pull-down on M7's EN and the zener on M7's output; 83s, no narration |
| `storyboards/rbmod-build.json` | building a rev 1 rbmod carrier in 13 steps, the 4 rework steps among them: the bare board, D1 and R9, the connectors, the headers, the 5 modules on pin strips, the large holes closed with wire and solder, the 4 wires from J6's pads to a USB-C plug board for the PD mini, and the panel's back with the heartbeat wires of header P3, the CAN cable and the supply on the RS485 header; narrated, 342.81s, Vivaldi's Spring as its bed at half level |
| `storyboards/thumbnail.json` | the thumbnail of `features`: the case with its display lifted and the title; `--stills 3` writes it |
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
| `music` | optional: `pads`, the default bed; `bach-prelude`, J. S. Bach's Prelude in C major BWV 846 on the same voices, one bar every 4s; or `pachelbel-canon`, Johann Pachelbel's Canon in D major, its ground bass at 2s a note under three of the violin's variations. `vivaldi-spring` is the opening of Antonio Vivaldi's "Spring" RV 269 in E major, 0.42s an eighth note: its two themes on a bowed voice with the third below, over a plucked bass; its notes are set down from memory of the violin part and are not checked against a score. All three compositions are in the public domain and no recording is used; the notes are set down in `tools/video_audio.py`. Bars 33 and 34 of the prelude keep the pattern of the other bars so that the piece loops |
| `music_gain` | optional, 0 to 2, 1 by default: the bed's level against the event sounds and the narrator |
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
| `parts` | optional, `cad` model only: keys `[t, k]` per component, `k` from 1, out of sight along the component's way in, to 0, seated. A value held between two keys freezes the component in the air while the camera moves on. Components: `case`, `estop`, `xt60`, `usbc`, `pdmini`, `rbmod`, `mcu`, `ina`, `buck`, `trigger`, `lid` |
| `display` | optional `{view}`: the renderer view on the model's display |
| `title` | optional `{from, to, kicker, heading, text}` |
| `chips` | optional `{from, to, n, text}`: the chip at the top right; a numeric `n` also sets the step marks |
| `labels` | `{from, to, anchor, at, title, text}` and optional `n`, `size` (`sm`), `side` (`l`, `r`, `t`, `b`: where the pointer leaves the bubble), `style` (`ok`, `warn`, `muted`) |
| `pinout` | optional `{from, to, connector, select, subtitle}`: the pin table of `J1` or `J2` with one position marked |
| `say` | optional list of `{at, text}`: what the narrator says in this scene and when; a line that runs into the next is an error |
| `servo` | optional `{connector, position, appear, plug: [t0, t1]}`: a servo whose plug moves 46mm into the position |

`anchor` is `[x, y, z]` in mm or a name: `display`, `rbmod`, `pdmini`,
`J1`, `J2`, `xt60`, `usbc`, `estop`, `side`, `plug`, and on the `cad`
model also `panel`, `case`, `mcu`, `ina`, `buck` and
`trigger`. An anchor follows its component while it moves. A label's
optional `glow` names a component that is lit while the label shows. `at` is the bubble's
top left corner on the frame in pixels. The model's origin is the centre
of the case floor; x runs along the 190mm side, y up, z towards the
connector wall.

A `board` scene:

| Field | Meaning |
| --- | --- |
| `dur` | length in s |
| `board` | the name of a board under `boards/` or `cad/`, without `.json` |
| `camera` | keys `[t, [x, y], w]`: the point of the board at the frame's centre and the width of the frame in mm |
| `title`, `chips`, `say` | as in a `case` scene; the board is dimmed to 35% under a title |
| `labels` | as in a `case` scene; `anchor` is a pad's name or `[x, y]` in mm |
| `tags` | optional `{from, to, at, text}` and optional `side` (`l`, `r`, `t`, `b`), `d`, the line's length in pixels, and `style`: a name beside a pad or a point |
| `nets` | optional `{from, to, net}` and optional `style` (`warn`): the front tracks and the pads of a net lit up |
| `cuts` | optional `{between: [a, b], t: [t0, t1]}` and optional `gap` in mm, 0.7 by default: the track is opened halfway between two points |
| `wires` | optional `{from, to, t: [t0, t1]}` and optional `via`, a list of points, `color`, `loose`, `plain` and `gone`: a wire drawn from one point to the other, with a solder joint at each end, with `loose` at its start only, with `plain` at neither. `gone` is `[t0, t1]`: the wire is pulled out along its way over that span |
| `plugs` | optional `{name, at, t}`: a USB-C plug board beside the board from `t` on, its 4 pads in a column at `at`, 2mm apart; they are named `NAME.G`, `NAME.D+`, `NAME.D-` and `NAME.V` |
| `bare` | optional `true`: a part is hidden until its `mount`; the footprints of size 1206 always show |
| `mount` | optional `{refs, t}`: the parts of these references come down onto their places from `t` on |
| `solder` | optional `{refs, t: [t0, t1]}` or `{pads, t: [t0, t1]}` and optional `back`: a joint on every pad of the parts, or on the pads named, one after another over the span. With `back` the joint is on the other side of the board and shows as a ring that fades |
| `insets` | optional `{from, to, kind, at, title}` and optional `text`, `good`, `bad`, `upper`, `lower`, `color`: a card at `at` with a cross-section. `strip` draws a module on the plastic of its pin strips, soldered on both sides, beside a module lying on the carrier; `fill` draws a wire through a large hole of both boards, soldered on both sides; `extract` draws a plug housing cut open, a needle on the contact's tongue and the wire leaving to the back |
| `fit` | optional `{kind, a, b, t}`: a `resistor` or a `zener` soldered across two pads from `t` on; a zener's band is at `a`; a resistor with `text` `2.2 kΩ` carries its colour bands |

A board's lengths are mm from the top left corner of its drawing. A pad's
name is its reference and number, `M1.20`; where a footprint has several
pads of one number they take a letter in the order of the board file,
`M7.2a` and `M7.2b`. A render stops on a pad or a net that the board does
not have; `--check` does not read the board.

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

- The `cad` model is the case's STEP assembly, 172 x 109 x 37mm; the assembly's
  power bank is left out. The file has no colours: `case_cad.js` sets them
  by part name, and a part without a name is dark grey, or green where
  it is a plate. The STEP file (65MB) and the converted model (35MB)
  are not in the tree:

  ```sh
  pip install cadquery-ocp==8.0.1.1.0
  python3 tools/video_cad.py rcbench_case.stp     # tools/video/cad/case.glb
  ```

  The `cad` model has no servo and no pin table: `servo` and `pinout`
  need the mockup.
- A board is not in the tree. `rbmod-rework` draws `cad/rbmod-rev1.json`,
  converted from the board file that matches the gerbers of 2026-10-25,
  the rev 1 order:

  ```sh
  python3 tools/video_board.py rbmod.kicad_pcb -o tools/video/cad/rbmod-rev1.json
  ```

  The drawing holds the front copper's tracks, the pads and each part's
  courtyard as a plain box. An axial part is drawn between its pads: a
  diode with its band at pad 1, a resistor of value `4k7` with its colour
  bands. Zones, the inner and back copper, the
  silkscreen and the parts on a module are not drawn. The case scene that
  opens `rbmod-rework` shows the assembly's carrier, which has a later
  layout.
- `rbmod-build` states the order of the steps and that the connectors,
  the headers, D1 and R9 are soldered on the back; neither is stated
  elsewhere in the tree. The mounting of the modules on straight pin
  strips with the plastic between module and carrier, soldered on both
  sides, the wire through the large holes of M5 and M6, and J6 left
  without a connector, its pads wired to the PD mini, are the owner's
  statements of 2026-11-13. The plug board's pads follow
  [Screens](../../docs/Screens.md): the IOmcu's TX, GPIO 7, on D-, its
  RX, GPIO 6, on D+; 15V on V and GND on G follow J6's nets. The order
  of the pads on a plug board is its print's, not the drawing's.
- The panel's back is drawn from the maker's photo of the board:
  positions within about 2mm, the header labels as read from that photo,
  those of P3's last two and P4's fourth pin not read with certainty.
  Which of the two 12-pin headers is P3 follows from IO28 on it. The
  CAN cable's H, L and GND end at the RP2350-CAN's terminal. The panel's
  supply is the carrier's J7 (5V twice, GND twice) on the 5V and GND pins
  of the RS485 header (owner, 2026-11-13).
- How a wire leaves a PH2.0 plug is described from the JST PH contact's
  form: a lance on the contact stands in a window of the housing, is
  pressed down with a needle and the wire is pulled out to the back. It
  is not tried on the cable the panel comes with. Which way a JST connector faces is
  not shown.
- In `rbmod-rework` and `rbmod-build` the places of the pull-down and of the zener on M7
  follow the module's footprint, whose EN pad is placed within about
  0.5mm. The parts are drawn as axial parts; the zener's package on a
  reworked board is not stated in the tree.
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
- RECEIVER, BATTERY and BALANCE are shown as in development, without
  their state of work, which the README's feature table holds.
- The player draws 1920 x 1080 only.
