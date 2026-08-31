# Contributing to rcbench

This is a hobby project with strong opinions, and most of them are written down
so you do not have to guess. Read this before a large change — several of the
rules below will fail your build if you meet them by surprise, and one of them
is a licensing rule that cannot be fixed after the fact.

## The one rule that cannot be undone

**Protocols are implemented from published specifications, never from another
implementation.**

Nearly every open implementation of the protocols this bench speaks — ESC
serial, receiver buses, telemetry — is GPL or AGPL, and this repository is MIT.
A decoder written by reading GPL source cannot be relicensed by rewording it,
and the damage is not visible in a diff. So:

- work from the specification, a datasheet, or a capture of the wire;
- if you have read a GPL implementation of the thing you are writing, say so in
  the pull request, and expect the answer to be that somebody else writes it;
- permissive references may be read for confirmation — PX4's decoders (BSD) and
  the MIT reference code for SRXL2, JETI EX Bus, DShot and DroneCAN — and even
  then the code here is written independently.

If a protocol has no published description, it does not get implemented. That
has already cost this project BLHeli_32's parameters and JETI's remote
configuration, and both are recorded rather than quietly worked around.

## Safety-relevant code

Anything that can make an output move is held to a higher standard than the
rest, and the reasoning is in [the Safety page](docs/Safety.md).

- **Fail safe by absence.** The correct behaviour when a signal stops is to
  stop driving. Never require a message to arrive in order to be safe.
- **Refuse and clamp are different answers.** A configuration mistake — an
  endpoint no servo can take — is *refused*, atomically, leaving the previous
  value. A command outside its range is *clamped*, because commands arrive many
  times a second and refusing one gives an output that stops following.
- **A stop latches.** Nothing re-arms on its own, including the link coming
  back.
- **The coprocessor does not ask permission** to protect hardware. It acts and
  reports afterwards.

## What CI will check

All of it runs locally, and all of it is fast.

```bash
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build && ctest --test-dir test/host/build --output-on-failure

python3 tools/coverage.py --check     # coverage floors, and the STATUS.md table
python3 tools/check_docs.py           # links, translations, SPDX, the suite list
python3 tools/render_ui.py --check    # the committed screenshots still match
python3 tools/frame_cost.py --check-doc
python3 tools/gen_font.py --check
```

Specifically, a change fails if:

- **coverage drops below 94% overall, or any single file below 85%.** The
  per-file floor exists because a healthy total once hid a screen at 73%.
- **a screen's render changes** and the committed golden images were not
  regenerated (`tools/render_ui.py`). Review the new images; do not just commit
  them.
- **drawing code exceeds its cache-line budget.** The panel is bandwidth-bound,
  not CPU-bound — see [Performance](docs/Performance.md). Draw row-major:
  horizontal lines are free, vertical ones are not.
- **a source file has no SPDX line**, or a documentation page has no German
  counterpart, or a link goes nowhere.
- **the suite list or the module tree in `STATUS.md` disagrees with what CMake
  builds.**

The suite also runs under AddressSanitizer and UBSan in CI
(`-DENABLE_SANITIZERS=ON`). If you touch a parser, run it that way first; the
code here is fed hostile bytes by design.

## Where code goes

**`shared/` is pure C with no vendor SDK** — no ESP-IDF, no pico-sdk, no
FreeRTOS types. It compiles into the panel firmware, the coprocessor firmware
and the host suite from one directory. Everything that *decides* something
belongs there, because that is what makes it testable on a laptop.

**`firmware/` is wiring.** If you find yourself writing a rule in a firmware
file, it probably belongs in `shared/` with a test.

## Style

- **Comments say why, not what.** Every file opens with the decision behind its
  shape — the failure it prevents, the alternative that was tried. A comment
  restating the code is noise; a comment naming the bug that shaped it is the
  most valuable line in the file.
- **Tests are sentences.** `a_refused_write_changes_nothing`, not `test_write_3`.
  Where a test encodes a specific bug, say so in a comment above it.
- **Commit messages explain the change**, at whatever length that takes. The
  history is the project's memory.
- Four spaces, no tabs. Match the file you are in.

## Documentation

`docs/` is published to the wiki on every push to `main`, in **English and
German**. Every page needs both, linked by the switch at the top — the check
enforces it. Write German as German rather than translating sentence by
sentence, and leave technical vocabulary in English: protocol names, mode
names, register and field names, `framed packets`, `One-Wire-Bootloader`,
`DShot Special Commands`.

The wiki is a manual for someone using the bench. `STATUS.md` is the running
record of what is built and why — design arguments go there, not into the
manual.

## Pull requests

One slice per pull request, with CI green. Say what changed and why; if you
found something surprising on the way, that belongs in the description too —
several of the best comments in this codebase started as a sentence in a PR.
