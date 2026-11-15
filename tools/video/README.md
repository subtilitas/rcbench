# Videos from storyboards

`tools/make_video.py` renders a storyboard into an MP4 video of
1920 x 1080 pixels at 30 frames per second. A storyboard is a JSON file
under `storyboards/`. It lists scenes; each scene has a type:

| Type | Shows |
| --- | --- |
| `case` | the MiniRcBench from its assembly, its components taking their places, its display showing stills and the whole bench rolling over to its back; or the bench in its case as a 3D model, with a camera path, the lid lifting, labels on named parts, a pin table and a servo being plugged in; or the M5Stack Dial as a 3D model, its display showing its faces and its knob turning, alone or beside the bench with both displays following one simulated run |
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
python3 tools/make_video.py dial                   # build/video/dial.mp4
python3 tools/make_video.py minircbench-build      # build/video/minircbench-build.mp4
python3 tools/make_video.py kst-rbmod --stills 2.5,40 -o /tmp/stills
python3 tools/make_video.py features --jobs 3      # frames in 3 parts at once
python3 tools/make_video.py features --jobs 3 --keep build/video/parts
                                                   # parts of 900 frames are kept per
                                                   # scene; a run cut off continues
                                                   # from them, and a changed scene
                                                   # is captured alone
                                                   # a changed player file, display
                                                   # picture or model captures all
                                                   # scenes again
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
| `player/dial3d.js` | the M5Stack Dial from its STEP model: one mesh a solid, the knob ring turning, two faces cross-fading on the glass |
| `player/duo3d.js` | the case and the Dial in one scene: the case's display takes shots of the panel's screen, the Dial's display its faces |
| `player/mini3d.js` | the MiniRcBench from its assembly: one group a component in its solids' colours, the case and its buttons coloured here, the two male servo connectors and the 4 screws drawn as plain solids, the display showing a still |
| `player/board.js` | a board drawn flat as SVG, and the rework on it |
| `../video_board.py` | converts a KiCad board file into `cad/NAME.json`, the board `board.js` draws |
| `boards/p4-7b-back.json` | the back of the 7 inch ESP32-P4 display board, drawn by hand in the format `video_board.py` writes: its connectors, modules and sockets as boxes, the pads of the 4-pin and 12-pin headers |
| `boards/minircbench-wiring.json` | the parts of a MiniRcBench laid out flat: the rear of the RP2350-Touch-LCD-2.8 with its sockets, the 5V boost board, the INA219 board, the cell and the two male servo connectors, with the pads the wires go to |
| `stills/mini-*.png` | 12 frames of the MiniRcBench's 240 x 320 display, drawn by that project's host suite |
| `player/capture.js` | steps the player through the frames and pipes them to ffmpeg |
| `storyboards/kst-rbmod.json` | programming a KST servo on the rbmod carrier, 8 steps, 100.4s |
| `storyboards/features.json` | every screen of the bench in 8 chapters: where it opens from and what it does on the rbmod carrier, narrated, 755.92s |
| `storyboards/rbmod-rework.json` | the rework of a rev 1 rbmod carrier in 4 steps: the track cut at R1, the wire from GP15, the pull-down on M7's EN and the zener on M7's output; 83s, no narration |
| `storyboards/rbmod-build.json` | building a rev 1 rbmod carrier in 13 steps, the 4 rework steps among them: the bare board, D1 and R9, the connectors, the headers, the 5 modules on pin strips, the large holes closed with wire and solder, the 4 wires from J6's pads to a USB-C plug board for the PD mini, and the panel's back with the heartbeat wires of header P3, the CAN cable and the supply on the RS485 header; narrated, 347.32s, Pachelbel's Canon as its bed at half level |
| `storyboards/dial.json` | bringing the M5Stack Dial into use in 7 steps: the Dial, its ring nut, the cell 102540 and a foam pad into the two parts of the printed base and its 4 screws M3 x 12, flashing over USB, the bind on SETUP > WIRELESS > DIAL, the menu and the SET of a value, the knob on MOTOR & ESC and on SERVO LIVE, and what the Dial does not do; the bench and the Dial stand in one 3D scene and show the same screen, run and value; narrated, 295.69s, Pachelbel's Canon at half level |
| `storyboards/minircbench-build.json` | building a MiniRcBench in 10 steps: the parts, the firmware, the boost board's input on the battery socket's pins, the 0Ω resistor off the enable pad, the pull-down and IO28 on it, the INA219 on the I2C socket, the 5.6V zener and the 470µF capacitor across the boost board's output, the 5V path through the sensor to both male servo connectors, the signals and the 9 unused wires taken out of the 12-pin plug, the check before the cell, the case, and the settings; narrated, 505.57s, Pachelbel's Canon at half level |
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
| `product` | optional: the name at the top left in place of `rcbench` |
| `dial_base` | optional, `true` or `false`, the default: the Dial of the models `dial` and `duo` sits in its printed hand-held base, `dial_base_top` and `dial_base_bottom` of `hardware/mechanical/models/`, with the ring nut `m5dial_nut` under the top part |
| `brand` | the line beside the product's name at the top left |
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
| `model` | optional: `mockup`, the default, `cad`, the assembly converted by `tools/video_cad.py`, `dial`, the M5Stack Dial converted by `tools/video_cad.py --solids`, `duo`, the case of `cad` with the Dial 128mm right of its middle, or `mini`, the MiniRcBench converted by `tools/video_cad.py --parts` |
| `flip` | optional, `mini` model: keys `[t, degrees]`, the bench rolled over about its long axis; 180 shows its back. `dial` model with `dial_base`: the Dial in its base rolled over; 180 shows the foot |
| `faces` | optional, `dial` and `duo` models: keys `[t, name]`, a face of `tools/render_dial.py` on the Dial's display from `t` on, cross-faded over 0.5s |
| `panel` | optional, `dial` and `duo` models: keys `[t, view]` or `[t, view, screen]`, the panel's screen from `t` on: on the case's display with `duo`, as a flat shot left of the Dial with `dial`. With the `mini` model a key is `[t, name]`, the still `stills/NAME.png` on the display from `t` on |
| `run` | optional, `dial` and `duo` models: the panel and the Dial following each other from `from` to `to`. The panel's renderer applies the Dial's gestures and writes the packets the panel sends a Dial; the Dial's renderer draws its face from them. The shots go into `panel` and `faces`, and `turn` follows the run. `kind` is `motor`, the default: `arm` and `throttle`, keys `[t, percent]` joined by lines and `[t, percent, "press"]` for a value set at once; drawn 10 times a second from one simulated run, SAFE before `arm`, a detent a percent. `servo`: `angle`, keys `[t, degrees]` as `throttle`'s, on SERVO LIVE while ARMED, a detent a degree. `nav`: `view`, an optional `screen` and `ops`, keys `[t, op]` with `t` and a count of detents, `p` a PRESS or `h` a HOLD, on a SAFE bench; drawn at each gesture |
| `turn` | optional, `dial` and `duo` models: keys `[t, degrees]`, the knob ring's angle, clockwise seen from the front; the sound track clicks once per 22.5°, a detent |
| `camera` | keys `[t, [x, y, z], [x, y, z]]`: position and target in mm |
| `lid` | optional keys `[t, mm]`: how far the display is lifted |
| `parts` | optional, `cad` model only: keys `[t, k]` per component, `k` from 1, out of sight along the component's way in, to 0, seated. A value held between two keys freezes the component in the air while the camera moves on. Components: `case`, `estop`, `xt60`, `usbc`, `pdmini`, `rbmod`, `mcu`, `ina`, `buck`, `trigger`, `lid`. The `mini` model's: `shell`, `buttons`, `plugs`, `boost`, `ina`, `cell`, `board`, `screws`. The `dial` model's, with `dial_base`: `upper`, the top part with the Dial and the nut, `dial` and `nut` within it, `cell`, `pad`, `screws` |
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
`trigger`, and on the `dial` model `display`, `knob`, `bezel`, `usb`, `power`, `portA`, `portB` and `body`, on the `duo` model the case's and `dial`, `knob` and `bezel`, on the `mini` model `display`, `glass`, `board`, `keys`, `usbc`, `card`, `buttons`, `shell`, `back`, `ports`, `esc`, `tmt`, `plugs`, `screws`, `cell`, `boost` and `ina`. An anchor follows its component while it moves. A label's
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
| `wires` | optional `{from, to, t: [t0, t1]}` and optional `via`, a list of points, `color`, `loose`, `plain`, `thin`, `ends`, `small` and `gone`: a wire drawn from one point to the other, with a solder joint at each end, with `loose` at its start only, with `plain` at neither, with `ends` `a` or `b` at that end only. A `thin` wire is 0.39mm wide in place of 0.7mm, with joints of 55%; `small` names the ends, `a`, `b` or `ab`, whose joint is half as large. `gone` is `[t0, t1]`: the wire is pulled out along its way over that span |
| `plugs` | optional `{name, at, t}`: a USB-C plug board beside the board from `t` on, its 4 pads in a column at `at`, 2mm apart; they are named `NAME.G`, `NAME.D+`, `NAME.D-` and `NAME.V` |
| `bare` | optional `true`: a part is hidden until its `mount`; the footprints of size 1206 always show |
| `mount` | optional `{refs, t}`: the parts of these references come down onto their places from `t` on |
| `solder` | optional `{refs, t: [t0, t1]}` or `{pads, t: [t0, t1]}` and optional `back`: a joint on every pad of the parts, or on the pads named, one after another over the span. With `back` the joint is on the other side of the board and shows as a ring that fades |
| `insets` | optional `{from, to, kind, at, title}` and optional `text`, `good`, `bad`, `upper`, `lower`, `color`: a card at `at` with a cross-section. `strip` draws a module on the plastic of its pin strips, soldered on both sides, beside a module lying on the carrier; `fill` draws a wire through a large hole of both boards, soldered on both sides; `extract` draws a plug housing cut open, a needle on the contact's tongue and the wire leaving to the back; `pins` draws one pin strip per entry of `rows`, `[count, text]`, with its count; `latch` draws a 1.0mm plug cut open, a needle lifting the housing's latch behind a contact and the wire leaving to the back |
| `fit` | optional `{kind, a, b, t}`: a `resistor`, a `zener` or a `cap` soldered across two pads from `t` on; a zener's band is at `a`; a `cap` is a radial electrolytic capacitor lying beyond `b`, its plus lead on `a`, its stripe on the minus lead's side, with `text` on its can; a resistor with `text` `2.2 kΩ` carries its colour bands, as does one of `10 kΩ`; a `link` is a 0402 chip resistor, 1.0 x 0.5mm, that lies between `a` and `b` until `t` and is then taken off |

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
  courtyard as a plain box. A footprint named as a module under
  `hardware/kicad/modules/` takes that module's board outline, the parts
  of its model (sockets, chips, buttons, terminals) and the texts of its
  drawing (`USB-C`, `CAN`, `EN`), all at the accuracy the module's spec
  states, on each pad its number and under each pad the name of its pin
  where the drawing does not print it. A JST or Molex connector carries a short name of each
  pad's net (`HB`, `SCL`, `5V`), a 3-row header the names of its rows. An axial part is drawn between its pads: a
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
  CAN cable's H and L end at the RP2350-CAN's terminal. The panel's
  supply and its only ground are the carrier's J7 (5V twice, GND twice)
  on the 5V and GND pins of the RS485 header (owner, 2026-11-13).
  [FirstRun](../../docs/FirstRun.md) names a third link wire, GND, for a
  bench without the carrier.
- How a wire leaves a PH2.0 plug is described from the JST PH contact's
  form: a lance on the contact stands in a window of the housing, is
  pressed down with a needle and the wire is pulled out to the back. It
  is not tried on the cable the panel comes with. Which way a JST connector faces is
  not shown.
- In `rbmod-rework` and `rbmod-build` the places of the pull-down and of the zener on M7
  follow the module's footprint, whose EN pad is placed within about
  0.5mm. The parts are drawn as axial parts; the zener's package on a
  reworked board is not stated in the tree.
- The `dial` model is `hardware/mechanical/models/m5dial.step`, which is in
  the tree; the converted model (0.9MB) is not:

  ```sh
  python3 tools/video_cad.py --solids hardware/mechanical/models/m5dial.step \
      -o tools/video/cad/m5dial.glb
  ```

  The Dial stands on its rear parts, without its ring nut and without a
  panel. With `dial_base` it sits in its printed base, whose two parts
  and the ring nut are converted the same way and are not in the tree
  either:

  ```sh
  for part in dial_base_top dial_base_bottom m5dial_nut; do
      python3 tools/video_cad.py --solids \
          hardware/mechanical/models/$part.step -o tools/video/cad/$part.glb
  done
  ```

  The cell, the foam pad and the 4 screws are plain solids drawn by
  `dial3d.js` from the numbers of `hardware/mechanical/README.md`; the
  cell's wires are two stubs and its plug, the Dial's battery socket and
  the Stamp-S3 coming off are not drawn. No base is printed, so the
  assembly the video shows is not tried.
  Its faces are the fixed models of `tools/render_dial.py`, so the
  readings do not follow the knob outside a `run`. A run's numbers are
  the panel's models' (`telemetry_sim`, `servo_sim`, `supply_sim`), posed
  by the panel's renderer (`motor-live`, `servo-live`) and sent to the
  Dial's as the panel's own packets (`RCBENCH_VIDEO_DIAL`, `packets`); no
  hardware is measured. On SERVO LIVE the Dial's measured angle reads
  `--`: no angle source is posed. The rail's readings there are the
  supply model's, given as an INA3221 channel's. The Stamp button, the cell and the
  MX1.25 socket are not in the model; `dial` names them without showing
  them.
- The `mini` model is MiniRcBench's assembly
  `hardware/mechanical/models/minircbench.stp`, 55.7 x 76.7 x 24.7mm,
  which is in that repository, not in this one; the converted model
  (1.8MB) is not in the tree:

  ```sh
  python3 tools/video_cad.py --parts minircbench.stp \
      -o tools/video/cad/minircbench.glb
  ```

  The assembly carries no colours. `--parts` takes each component's
  from the STEP file of the same name beside the assembly, solid by
  solid; the case and its buttons have none and are coloured in
  `mini3d.js`. The assembly holds no servo connectors, screws, wires or
  tape: the connectors and screws are plain solids drawn by `mini3d.js`,
  the wires and the tape are not shown in 3D. The cell's lead is cut
  off at the cell: it is straight in the model and would leave the
  case.
- `minircbench-build` shows the build the owner stated on 2026-11-14
  and 2026-11-15: the boost board's input soldered to the battery
  socket's pins, its enable pad on GP28 with 10kΩ to ground, a male servo
  connector with the switched 5V in each of the case's two openings, the
  INA219 in the boost board's OUT+, JST-SH leads in the 12-pin and the
  I2C socket, a 0Ω resistor of size 0402 between the boost board's EN pad and IN+ that is taken off before the pull-down goes on (stated on 2026-11-15), no fuse, a cell with its own protection circuit (stated on 2026-11-15), one copper plane from the boost board's IN- to its OUT- (stated on 2026-11-15), both small boards on double-sided tape, a 470µF electrolytic capacitor of 10V or more and a 5.6V zener diode across the boost board's output. The zener is rated 2W to 5W; its package is not stated and it is drawn as an axial part. Its own
  statements, found nowhere else: the order of the steps, the wire
  colours, the pull-down's place between EN and IN-, the daisy chain from the TMT to the
  ESC connector, the cell's lead running over the board, and screws of
  M2.5 x 6, taken from the case's 2.2mm under the head and the
  standoffs' 4.0mm. The sockets' places and the prints (BAT + and -,
  the 12 names of the 12-pin socket, GND 3V3 SDA SCL at the I2C socket)
  are read from Waveshare's photo of the board's rear and its pin
  drawing; which side of each socket the plug enters is taken from the
  solder pins in that photo. How a contact leaves a JST-SH plug is
  described from the housing's form and is not tried on the lead the
  board comes with. No part of the build is measured: MiniRcBench
  states that no INA219 and no boost board has been on its board.
- The stills are frames of MiniRcBench's host suite (`cd test && make`
  writes `shot_*.ppm`) at its commit 8c7ace3. `mini-gp28` and
  `mini-supply-sense` come from two frame dumps added to
  `test/test_ui.cpp` for this video and not in that repository: SERVO
  SUPPLY at GP28, and the same with CURRENT SENSE at INA219 and its
  caption FOUND. A change of MiniRcBench's screens does not reach the
  stills.
- `minircbench-build` says that the servo tester feeds its servo from
  the boost board. The owner stated on 2026-11-15 that the firmware is
  to do so; the release is not named. MiniRcBench at its commit 25b5a36
  does not do that: its servo tester never drives the
  enable pin and shows `GP28 SUPPLY STAYS OFF HERE.`; the KST and Hitec
  programmers switch the supply. The still `mini-servo-supply` shows
  that line and is not used in the video.
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
