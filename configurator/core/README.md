# Configurator core

The core of the rcbench bench configurator: one data file, one resolver and the
checks on both. It has no runtime dependency and no build step. Node.js 24 runs
the TypeScript files directly.

The data describes release 0.16.0. Every record carries a source: a file and
line at the tag `v0.16.0`, a section of the build guide, a photo taken for
the build guide, or a dated statement of the project owner. A fact that no source confirms is marked as unknown in the
data and is not asserted.

## Files

| File | Content |
| --- | --- |
| `../contract.ts` | The types shared by the core and the page. |
| `catalogue.ts` | The data: 19 parts, 40 pads, 55 wires, 11 functions, 6 rules, 20 check entries, 5 stages, 7 safety notes, 8 pin defaults, 12 pictures on 8 parts. All text is in English and German. |
| `resolve.ts` | `resolve(catalogue, selection)` returns the setup: parts with the reason for each, functions, options, wires, check entries. |
| `cond.ts` | Evaluates the condition of a wire, a rule or a check entry. |
| `index.ts` | The entry point: `CATALOGUE`, `DEFAULT_OPTIONS`, `resolve` and the default export `core`. |
| `tools/check.ts` | The static checks on the data. |
| `tools/pictures.ts` | The check of the picture records against the files of `../public/pictures/`. |
| `tools/coverage.ts` | Measures line coverage and keeps the figure below current. |
| `test/` | The test suite for `node:test`. |

## Resolver

`resolve` is pure: no input or output, no clock, no random number. It returns a
setup for every input. Ids that the catalogue does not hold, parts that are not
selectable and option values outside the type go to `ignored`. Output arrays
are in catalogue order.

`CATALOGUE`, `DEFAULT_OPTIONS`, `OPTION_VALUES` and `OPTION_KEYS` are frozen. A
setup holds parts, pins and wires of the catalogue by reference, so a write to
one of these records throws a `TypeError`. The arrays of a setup are its own.

The order of work:

1. Locked parts.
2. Parts needed by the chosen functions.
3. Owned parts.
4. Options forced by a chosen function. Functions are read in catalogue order; the first one that forces an option names it.
5. Rules, once each, in array order.
6. Wires and check entries whose condition holds.
7. Functions: `enabled` is true when no needed part is missing and every forced option holds.

`conflicts` is always empty: the data holds no pair of parts that exclude each
other.

## Commands

Run from `configurator/core` with Node.js 24.

```sh
npm run check                 # static checks; the tree at v0.16.0 is read when git and the tag are present
npm run check -- --strict     # as above; fails when git or the tag is absent
npm run check -- --wires      # prints the wire table as Markdown
npm test                      # the test suite
npm run coverage              # runs the suite and writes the figure into this file
npm run coverage -- --check   # fails when the figure in this file differs; writes nothing
```

Exit codes of `check`: 0 no error, 1 at least one error, 2 unknown argument.
A warning does not change the exit code. Exit codes of `coverage`: 0 figure
current or written, 1 drift or a failing suite, 2 unknown argument.

`--strict` needs the tag `v0.16.0` in the clone. A shallow clone without tags
fails it.

The type check needs the TypeScript compiler and the Node.js type
declarations. Neither is a dependency of this folder:

```sh
npm install --no-save --no-package-lock --prefix "$TMP_TOOLS" typescript@7.0.2 @types/node@24.19.2
"$TMP_TOOLS/node_modules/.bin/tsc" --noEmit -p tsconfig.json \
  --typeRoots "$TMP_TOOLS/node_modules/@types" --types node
```

## Checks

`tools/check.ts` reports each finding with the id of the record.

| Check | Fails when |
| --- | --- |
| Ids | An id is used twice, or the wire numbers are not unique and ascending. |
| Wire ends | A wire end is not a pad or a declared pin of a part, or a power wire ends on a signal pad. |
| References | A function, rule, condition or stage names an id that does not exist. |
| Text | A string is empty in one language, starts or ends with white space, or holds an exclamation mark, a listed marketing word, a character outside the allowed set or a tag other than `<b>` in a safety note. |
| Sources | A record has no source, a file or line does not exist at the tag, the line is empty, the line does not hold the text the anchor names, or a build guide section, a photo or the date of an owner statement is not known. |
| Pins | The pin table breaks one of the 7 rules below, or a wire and the pin table disagree. |
| Pictures | A picture record names a file that `../public/pictures/` does not hold, a photo is not a WebP file or carries Exif or XMP metadata, a thumb is not 480 px wide, the pixel size of the thumb or the drawing differs from the record, an alt text is empty in one language, more than 2 pictures of a part apply to one set of options, a drawing holds a script, an event attribute, a raster image, a data URL or a reference outside the file, or the folder holds a file that no part names. |
| Routes | A slot order is used twice in a column of the drawing, or a lane number is outside its range (left 0 to 16, right 0 to 20, outer 0 to 7, channel 0 to 4). |
| Tree | The pads, the reserved pins, the heartbeat pin or a pin default differ from the firmware sources at the tag. |

Each repository anchor in `catalogue.ts` is `repo(path, line, text)`. The text
is 4 characters or more of the cited line, and it states the fact the record
takes from that line. The check compares it with the file at the tag, with each
run of white space read as one space. The texts are the export `ANCHOR_TEXTS`;
they are not part of `CATALOGUE`. Build guide sections, photos and owner
statements are checked by id, number and date only.

A release tag newer than `v0.16.0` is a warning.

The 7 pin rules:

1. A signal is on a General Purpose Input/Output (GPIO) pin of its allowed set, and not on a reserved pad (GP8 to GP12).
2. A GPIO carries one signal.
3. The clock line of the sensor bus (SCL) is the data line (SDA) plus 1. SDA is one of GP0, GP4, GP6, GP14, GP16, GP18, GP20, GP26, or -1 for off.
4. Two outputs do not share a pulse-width modulation (PWM) slice.
5. An output does not share a compare register with another signal. The partner of GP0 to GP5 is the same pin plus 16.
6. The tap pin is not GP26, GP27 or GP28.
7. The heartbeat pin is GP3 and fixed.

## Tests

The suite runs 2 full sweeps and 1 seeded sample through the resolver:

- 2048 sets of owned parts times 8 option combinations: 16384 setups.
- 2048 sets of functions times 8 option combinations: 16384 setups.
- 2000 mixed selections from a fixed seed.

Each setup is checked for: the same result on a second call, no exception, no
pin of a part on two nets, no power wire on a signal pad, every wire end a
declared pin of a part that is present, catalogue order of every output array.
The sweep refuses a catalogue with more than 16 selectable parts or 16
functions.

Further tests cover the empty selection, the full selection, each part and
each function alone, unknown ids, a missing German string, a broken source
anchor and a data file that differs from the firmware tables.

## Coverage

Line coverage of the resolver, measured by `node --test --experimental-test-coverage`.
`tools/check.ts` and `catalogue.ts` are not part of the figure.

<!-- coverage:begin -->
| File | Lines run | Lines | Line coverage |
| --- | ---: | ---: | ---: |
| `resolve.ts` | 189 | 189 | 100.0 % |
| `cond.ts` | 51 | 51 | 100.0 % |
<!-- coverage:end -->

## Unknown in the data

- Pins marked unconfirmed: `GPO` of the AS5600 board: no source at `v0.16.0` shows its print. `LP` of the INA3221 board: both sides of the board print GND, GND, VPU; the function of the 3 pads is not confirmed.
- Emergency stop switch: the tab names C, NO, NC come from the wiring diagram of the seller, per an owner statement of 2026-10-11. No measurement is stated. The check entry `estop-tabs` is of kind `measure`.
- Parts with the state "not run on hardware": INA3221 board, AS5600 board, pull-up resistors, INA228 board, emergency stop switch, stick simulator lead, tap lead, T-plug of the PD mini.
- Functions with the state "not run on hardware": servo current, angle, ESC power, stick programming, tap, emergency stop.
- The pin for the servo signal (GP2) is a proposal of this data file. The repository names no servo pin.
- The pins of the PD mini serial lines (GP7 transmit, GP6 receive) come from an owner statement of 2026-10-11. The firmware default for both is -1 (off). The direction, transmit to D− and receive from D+, is from `shared/settings/settings.c`.
- The INA228 module with XT90 plugs has its shunt in the minus line and both lines of the ESC supply on its 2 plugs, per the same owner statement. The data holds one set of power wires for both boards: the sensor in the plus line and the minus of the ESC as a direct wire. The check entry `ina228-xt90-pads` states the difference.
- With the INA3221 on the sensor bus the sources disagree on the 2 pull-up resistors of 2.2 kΩ: the build guide lists none, `docs/FirstRun.md` lists both. The data follows the build guide and states the disagreement.
- 6 check entries of kind `unknown` state each open point in the text of a setup.
