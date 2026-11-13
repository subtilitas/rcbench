# Video tooling: handover

State of the video work on branch `ccr-4bc6238f-kl3k21` as of 2026-11-13.
[README.md](README.md) is the reference for the tool; this page holds what
a new session needs beyond it.

## What exists

| Item | State |
| --- | --- |
| `tools/make_video.py`, `tools/video/player/` | renders a storyboard into an MP4 of 1920 x 1080 at 30 frames per second |
| `tools/video_audio.py` | sound track: music bed, event sounds, record brake at freezes, ducking under the narrator |
| `tools/video_voice.py` | narration with the Kokoro model, steps stretched to their sentences |
| `tools/video_cad.py`, `player/case_cad.js` | the case from its STEP assembly, 12 movable components |
| `storyboards/kst-rbmod.json` | programming a KST servo, 100.4s, mockup case, no narration |
| `storyboards/features.json` | feature tour, 755.92s, approved by the owner on 2026-11-13 |
| `storyboards/thumbnail.json` | thumbnail of the feature tour, `--stills 3` |
| `test/tools/test_make_video.py` | 16 cases; the storyboard check, the script, the freezes |

No pull request is open. CI has not been looked at for this branch.

## Files that are not in the tree

| File | Size | Source | Goes to |
| --- | --- | --- | --- |
| `rcbench_case.stp` | 65MB | the owner's CAD export | any path; input of `tools/video_cad.py` |
| `case.glb` | 35MB | `python3 tools/video_cad.py rcbench_case.stp`, 54s | `tools/video/cad/` |
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

## Open: build video for the rbmod rework

Requested on 2026-11-13, stopped by the owner before any storyboard was
written. The request: show which trace to open (the one below the
resistor), where the 5.6V clamp zener goes, where the 2.2kΩ pull-down
on M7's enable goes, and the remaining steps.

Known:

- `docs/Carrier.md`, "Rev 1 rework", lists three changes: cut the trace
  under R4 and run a wire from R4 to GPIO 15; a pull-down of 2kΩ to 5kΩ
  from M7's EN to GND; a 5.6V zener from VCC_PROGPORT_DCDC to GND,
  cathode at the rail.
- The tree has no board file for rbmod. The STEP assembly holds the
  board's copper (`rbmod_copper`, 26 solids), 8 resistors of size 1206,
  one axial resistor and one DO-34 diode.
- Seen from the display side with the headers at the bottom, the 8
  resistors sit in two groups of 4 over the two headers. R4 is the
  rightmost resistor of the right group (owner, 2026-11-13).
- The axial resistor and the DO-34 diode of the assembly are not the
  rework parts (owner, 2026-11-13). Their function is not known here.

Not known, and needed before a storyboard is written:

- where on the trace under R4 the cut is, and on which side of the board;
- which pad of R4 the wire starts at, and which pad of the RP2350-CAN
  module is GPIO 15;
- where the pull-down's two ends are soldered;
- where the zener's two ends are soldered, and which zener is fitted;
- whether the video shows further steps.

A reference settles these: photos of a reworked board from both sides,
or the board's KiCad files. The owner has named neither yet.

What the player lacks for this video:

- a view of the bare board: the components `lid`, `mcu`, `ina`, `buck`,
  `trigger` and `pdmini` hide at `parts` value 1, but the case stays;
- copper that stands out from the board: `rbmod_copper` is 0x2c6a58 on
  0x1f5446 in `case_cad.js`; 0xd08a3c reads well and was not committed,
  because it changes the look of `features`;
- marks drawn on the model: a cut line, a wire between two points, a
  part that is not in the assembly.

## Limits that still hold

- The Hitec views show a PD MINI row: the renderer has no carrier pose
  for them.
- The colours of the model are set by part name in `case_cad.js`; the
  STEP file has none.
- The sound and the voice were judged by the owner's ear, not measured
  beyond loudness (-19 LUFS, loudness units relative to full scale;
  peak -3dBFS, decibels relative to full scale).
