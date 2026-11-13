# Video tooling: handover

State of the video work on branch `claude/new-session-7wgr7m` as of
2026-11-13. The branch holds `ccr-4bc6238f-kl3k21` merged into `main`.
[README.md](README.md) is the reference for the tool; this page holds what
a new session needs beyond it.

## Start here

1. Read this page, then [README.md](README.md).
2. `rbmod-rework` is rendered once and waits for the owner's review
   ([below](#the-rbmod-rework-video)).
3. No pull request is open.
4. `rcbench-video-handover.zip`, sent to the owner on 2026-11-13, holds
   this page, the sources of the tool as plain files, a git bundle of
   the branch, `kst-rbmod.mp4`, the Bach version of the feature tour,
   the thumbnail, the owner's STEP archive and the converted model. The
   Pachelbel version was sent beside it as a file of its own: an upload
   takes 400MiB at most. Neither holds the speech model or
   `node_modules/`.

## What exists

| Item | State |
| --- | --- |
| `tools/make_video.py`, `tools/video/player/` | renders a storyboard into an MP4 of 1920 x 1080 at 30 frames per second |
| `tools/video_audio.py` | sound track: music bed, event sounds, record brake at freezes, ducking under the narrator |
| `tools/video_voice.py` | narration with the Kokoro model, steps stretched to their sentences |
| `tools/video_cad.py`, `player/case_cad.js` | the case from its STEP assembly, 12 movable components |
| `storyboards/kst-rbmod.json` | programming a KST servo, 100.4s, mockup case, no narration |
| `storyboards/features.json` | feature tour, 755.92s, approved by the owner on 2026-11-13 |
| `tools/video_board.py`, `player/board.js` | a KiCad board drawn flat, with a net lit up, a cut, a wire and a part across two pads |
| `storyboards/rbmod-rework.json` | the rev 1 rework in 4 steps, 83s, no narration |
| `storyboards/rbmod-build.json` | the build of a rev 1 carrier and the panel's wiring in 13 steps, 342.81s, narrated in the voice of `features`, bed `vivaldi-spring` at half level |
| `storyboards/thumbnail.json` | thumbnail of the feature tour, `--stills 3` |
| `test/tools/test_make_video.py` | 25 cases; the storyboard check, the script, the freezes |

No pull request is open. CI has not been looked at for this branch.

## Files that are not in the tree

| File | Size | Source | Goes to |
| --- | --- | --- | --- |
| `rcbench_case.stp` | 65MB | the owner's CAD export | any path; input of `tools/video_cad.py` |
| `case.glb` | 35MB | `python3 tools/video_cad.py rcbench_case.stp`, 54s | `tools/video/cad/` |
| `rbmod.kicad_pcb` of rev 1 | 251kB | repository `subtilitas/kicad`, `projects/rcbench/rcbench_modular/rbmod/rbmod-backups/rbmod-2026-10-25_215831.zip` | any path; input of `tools/video_board.py` |
| `rbmod-rev1.json` | 40kB | `python3 tools/video_board.py rbmod.kicad_pcb -o tools/video/cad/rbmod-rev1.json` | `tools/video/cad/` |
| `kokoro-v1.0.onnx`, `voices-v1.0.bin` | 354MB | github.com/thewh1teagle/kokoro-onnx, release `model-files-v1.0` | `tools/video/tts/` |
| `node_modules/` | | `npm install` in `tools/video/` | `tools/video/` |

Python packages: `pillow numpy kokoro-onnx cadquery-ocp==8.0.1.1.0`.
Node needs `playwright` with its Chromium. ffmpeg and a C compiler are
needed.

## Rendering

```sh
python3 tools/make_video.py features --jobs 12 --keep build/video/parts \
    --music bach-prelude,pachelbel-canon
```

| Machine | Jobs | Time for `features` |
| --- | --- | --- |
| cloud container, 4 cores | 3 | about 60 min |
| the owner's server, 48 cores | 12 | 43 min |

- The capture is the slow part; the 3D opening is its slowest section.
- `--keep` names the parts by a hash of the narrated storyboard. A change
  that leaves every step length as it was reuses all parts: only the
  narration, the sound tracks and the mux run, about 10 min.
- A narration change alters step lengths. To keep the picture, set the
  changed step's `dur` to its length before the change, as the
  signal-wire step of `features` does (18.65).
- `RCBENCH_VIDEO_GL` selects a GPU renderer. It is untested; the server's
  7 MI50 cards were not used.
- The server session "RCBench 261111" has a worktree with everything
  installed and has rendered `features` three times.

## Decisions the owner made for `features`

- Scope: the rbmod carrier in its case only. No board wired by hand, no
  INA228 page. Hitec MOVE SERVO and FIND STOP are left out:
  `docs/HitecMove.md` states they are refused on the carrier.
- RECEIVER, BATTERY and BALANCE: chapter 5, "In development", nothing
  more.
- Music: the Bach prelude is preferred; both beds are rendered.
- Voice: a blend of `af_heart` 0.6 and `af_nicole` 0.4 at speed 0.94.
- Wording: "electronic speed controller" in the opening and on the
  chapter 2 card, "the controller" after that; "CAN bus" as two words
  (written `can, bus` for the speaker); "AM32" written `A-M 32`.
- The narration states what a screen is for before what it shows.
- The model leaves the assembly's power bank out.
- German narration is parked. Piper (`thorsten`, `kerstin`) and
  Chatterbox Multilingual were tried and rejected by ear; Chatterbox
  also misread "XT60" and "74".

## The rbmod rework video

`storyboards/rbmod-rework.json`: a case scene of 9s, a board scene of 67s
and a recap of 7s. Rendered on the cloud container with 3 jobs.

Which board is rev 1:

- The rev 1 order is `production/jlcpcb1.0.zip` of the KiCad project,
  gerbers of 2026-10-25 22:01, 4 copper layers (owner, 2026-11-13).
- The backup `rbmod-2026-10-25_215831.zip` holds the board file of that
  order: each end of its 143 track segments is a coordinate of the
  gerber of its layer.
- `rbmod.kicad_pcb` at the project's first commit (`08a8d74`,
  2026-10-26) and the STEP assembly of the case hold a later layout, with
  PROGP03 on GPIO 15.

What rev 1 holds, and what the owner did (2026-11-13):

- None of the eight 120Ω resistors R1 to R8 is fitted. A front track of
  0.3mm joins the two pads of each footprint and carries the signal.
- PROGP03 runs from module pad 31 (GPIO 26) to R1's inner pad, between
  R1's pads, and from R1's outer pad to J1. R1 is the end footprint of
  the row, next to J1 pin 1. `docs/Carrier.md` names this resistor R4 and
  states a 120Ω series resistor on every signal; neither holds for a
  rev 1 board as built.
- Step 1: the track between R1's pads is scratched open. Step 2: a wire
  runs from module pad 20 (GP15) to R1's outer pad.
- Step 3: 2.2kΩ from M7's EN pad to the GND pad beside it. Step 4: a
  5.6V zener from M7's OUT+ to GND, cathode on OUT+. Both are soldered on
  top of the module.
- D1 (3V zener) and R9 (4.7kΩ) are the input of TAP on GPIO 22 and J9.

How the modules are mounted (owner, 2026-11-13):

- On straight 2.54mm pin strips with the plastic between module and
  carrier; without it the through-hole parts can short out. The pins are
  soldered on both sides. The modules sit level because the display
  leaves little headroom.
- The large holes of the PD trigger (M6, 2) and of the INA3221 (M5, 9)
  take a wire through both boards and solder.
- J6, on the back, takes no connector. Four wires are soldered to its
  pads and run to a USB-C plug board that goes into the PD mini: 15V on
  V, GND on G, TX (GPIO 7) on D-, RX (GPIO 6) on D+. The owner asked for
  the polarity and TX and RX to be checked against the PD mini; the
  check is `docs/Screens.md` ("TX connects to module DM, RX to DP") and
  the module's note that it sends on DP.
- The panel's back is shown redrawn, not as the maker's photo, with the
  wiring to the carrier; the wires of the 12-pin plug that are not used
  are taken out of the plug.
- The panel's supply: J7 on the carrier (5V twice, GND twice) to the
  5V and GND pins of the panel's RS485 header. The CAN cable carries H,
  L and GND only; its 5V wire is taken out.
- The narrator said "G P I O" with pauses; abbreviations are written
  with hyphens, `G-P-I-O`, `G-P 15`, `J-S-T`, `P-D`.
- Music of `rbmod-build`: Vivaldi's Spring, at the owner's wish. Its
  notes are from memory. At 0.3s an eighth and full level the owner
  found it too fast and much too loud; it plays at 0.42s an eighth and
  `music_gain` 0.5, not yet judged.
- The narration of `rbmod-build` has not been heard or checked with a
  speech recogniser.

Not known:

- the order of the build steps on the owner's bench; the video's order
  is by part height, with the cut at R1 first;
- the zener's type and package on the owner's board; the video draws an
  axial part and names 2W or 5W as `docs/Carrier.md` does;
- whether the seven other bridged footprints stay as they are in a
  later revision.

## Limits that still hold

- The Hitec views show a PD MINI row: the renderer has no carrier pose
  for them.
- The colours of the model are set by part name in `case_cad.js`; the
  STEP file has none.
- The sound and the voice were judged by the owner's ear, not measured
  beyond loudness (-19 LUFS, loudness units relative to full scale;
  peak -3dBFS, decibels relative to full scale).
