# Configurator core

The core of the rcbench bench configurator: one data file, one resolver and the
checks on both. It has no runtime dependency and no build step. Node.js 24 runs
the TypeScript files directly.

The data describes release 0.19.0. Every record carries a source: a file and
line at the tag `v0.19.0`, a section of the build guide, a photo taken for
the build guide, or a dated statement of the project owner. A fact that no source confirms is marked as unknown in the
data and is not asserted.

## Files

| File | Content |
| --- | --- |
| `../contract.ts` | The types shared by the core and the page. |
| `../release.ts` | `VERSION` and `TAG`, read from the `version` field of `../package.json`. The data, the page and the checks import them. |
| `catalogue.ts` | The data: 20 parts of which 2 are test objects and 1 is modelled, 40 pads, 66 wires, 24 functions of which 12 run on every setup, 7 rules, 43 check entries, 8 power sources, 5 stages, 7 safety notes, 9 pin defaults, 12 pictures on 8 parts. All text is in English and German. |
| `resolve.ts` | `resolve(catalogue, selection)` returns the setup: parts with the reason for each, functions, options, wires, check entries, power sources. |
| `cond.ts` | Evaluates the condition of a wire, a rule or a check entry. |
| `groups.ts` | `groups(catalogue)` groups the functions by the bench hardware they need. `removalEffect` and `deselectEffect` return what leaves a setup with a part or a function. |
| `buy.ts` | `buyRows(part, options)` returns the rows of the parts list that apply to a set of options; `brought(part)` tells whether the visitor brings the part. |
| `sim.ts` | The motor simulator: the values a visitor chooses (1S to 6S, the E12 values from 100 Ω to 4.7 kΩ, 6 ratings, delta or star), `simHeat(sim)` with the current and the power of each resistor, `withSim(catalogue, sim)` with the part named and listed for the values. See "Motor simulator". |
| `help.ts` | `FUNCTION_HELP`: what each of the 12 functions with a tick does, in English and German. The help bubble of the chooser opens with it. |
| `index.ts` | The entry point: `CATALOGUE`, `DEFAULT_OPTIONS`, `resolve`, the exports of `groups.ts`, `buy.ts` and `sim.ts`, `FUNCTION_HELP` and the default export `core`. |
| `tools/check.ts` | The static checks on the data. |
| `tools/release-sources.ts` | The check of `../package.json` against `RCBENCH_VERSION_*` in `shared/link/include/rcbench_version.h` and against the release numbers in the Markdown files of the configurator. `tools/check.ts` runs it. |
| `tools/check-annotate.sh` | Runs `tools/check.ts --strict` in a GitHub Actions job and shows each warning as an annotation and in the summary of the run. |
| `tools/pictures.ts` | The check of the picture records against the files of `../public/pictures/`. |
| `tools/coverage.ts` | Measures line coverage and keeps the figure below current. |
| `test/` | The test suite for `node:test`. |

## Bench hardware and test objects

A part with `testObject: true` is a device that the visitor connects and
tests: the servo and the ESC. It is not hardware of the bench. Every other
part is bench hardware.

| Field | Meaning |
| --- | --- |
| `Part.testObject` | The part is a test object. It is neither locked nor selectable, and no rule adds it. |
| `Part.modelled` | The part is modelled: the data states what it has to do, not which product does it. Its name gives the required range, its role the values a source states and "not stated" for the rest. It has no picture. The page and the PDF tag it "Modelled part: no product chosen". 1 part: `dcsup`, the DC supply of the Osoyoo breakout board. |
| `Part.yours` | The visitor brings the part: its spec follows the test object. 4 parts: the test objects `servo` and `esc`, the servo supply `ssup` and the ESC battery `batt`. A test object is brought with or without the field. The modelled DC supply `dcsup` is not brought: the parts list gives its spec as a row to buy. |
| `Part.buy` | The rows of the parts list. See "Parts list". |
| `Fn.needs` | The bench hardware the function needs. It holds no test object. |
| `Fn.drives` | The test object the function acts on. The resolver adds it with the function, so its pins and wires are in the setup. |

9 functions drive a test object. 3 of them need no bench hardware beyond the
base setup: `servo` (drive a servo), `servoprog` (program a KST or Chaservo
servo) and `esc` (drive an ESC). The tick of such a function connects the
test object.

A power source is one record of `powerSources`: what it feeds (`logic` or the
id of a test object), voltage and current where a source states them,
advantages, drawbacks and unknowns. A setup lists the sources of the logic
and of each connected test object. `used` is true for a source that the wires
of the setup use. Sources with the same `choice` stand for one part of the
wiring: `lipo-2s` and `own-supply` are both the servo supply (`ssup`). A setup
uses one of them and does not state which; the page and the PDF mark both
"In this setup: one of these". No other 2 sources of one test object are in
use in one setup.

| Source | Feeds |
| --- | --- |
| `usb` | logic |
| `copro-5v`, `strip-v`, `pd-servo`, `lipo-2s`, `own-supply` | servo |
| `esc-battery`, `pd-esc` | ESC |

The option `servoPower` is `external` (default) or `bench`. With `bench` the
plus of the servo is on pad 40 (VBUS, 5 V) of the coprocessor, or on the V
pin of the strip of the Osoyoo breakout board. The check entry
`servo-bench-supply` is in every such setup: the source at the V pin depends
on the jumper V Selector (5V from the converter of the DC input, 3V3 from the
regulator on VSYS). The current limit of pad 40 is not stated; the check entry
`servo-5v-pin` says so. The currents of the strip and whether USB alone powers
the 3V3 position are not measured; the check entry `strip-supply` says so. The ESC
takes signal and GND from the bench in every setup, never a supply.

On the V pin of the strip the jumper sits on 5V, and position 5V takes its
power from the DC input of the board alone. The rule `osoyoo-dc` adds the part
`dcsup`, a DC supply of 7 to 12 V, under the condition of that warning: a
servo, no PD mini, `servoPower: bench`, `connection: osoyoo`. Its wires 65
(plus, net `osoyoo_vin`) and 66 (minus, net `osoyoo_vin_neg`) are stubs to the
DC jack, the VIN terminal or the header H4. The plus is PWRIN at all 3. The
minus is VIN- at the VIN terminal and at H4, and GND of the board at the DC
jack (schematic REV 1.0: DC1 pin 1 PWRIN, pins 2 and 3 GND). The jack takes a
5.5 x 2.1 mm plug, centre positive. The product page names the socket
DC005-2.1: the 2.1 mm pin follows from that name; the 5.5 mm outer diameter is
the usual size of that socket family and is not stated by the vendor. The jack
has no reverse-polarity protection: the SI2302 (Q3) joins VIN- to GND and
protects the VIN terminal and H4 only. The gate of Q3 sits at PWRIN through
R7, so above 12 V its gate-source voltage exceeds the ±12 V of its datasheet.
The current the supply must deliver is not stated.

The option `servoPort` is `outputs` (default) or `prog`. It is never shown
and the hash holds no token for it: the function `servoprog` forces `prog`.
With `outputs` the servo signal is wire 7, from pad 4 (GP2), a slot of
SETUP → OUTPUTS. With `prog` it is wire 64, from pad 17 (GP13), the
programming port of the coprocessor (`PROG_PORT_DEFAULT_PIN` in
`shared/outputs/include/prog_port.h`), and SETTINGS → DUT → PWM TARGET is
PROG PORT, so the other servo functions drive the same pin. The servo
programmer needs the panel and the coprocessor alone. With the PD mini, the
PD mini feeds the servo and CONNECT sets its voltage, 4.8 to 6.0 V or up to
8.4 V with HV SERVO on, and its current limit, 50 to 3000 mA. Without it the servo takes the supply that
`servoPower` names; the check entry `prog-supply-own` states that the
programming voltage and the current limit are then stored and not applied.

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
2. Parts needed by the chosen functions, and the test object each of them drives.
3. Owned parts. A test object and the T-plug (`usbtap`, which only the rule `pd-tplug` adds) are not selectable and go to `ignored`; the page removes the id of a former tick from the owned parts before it resolves, so none is reported.
4. Options forced by a chosen function. Functions are read in catalogue order; the first one that forces an option names it.
5. Rules, once each, in array order.
6. Wires, check entries and power sources whose condition holds.
7. Functions: `enabled` is true when no needed part is missing, every forced option holds and the test object the function drives is connected. A function with `needs: []` and without `drives` runs on the panel and the coprocessor alone and is enabled in every setup.

## Groups and removal

`groups.ts` is pure and reads the catalogue through `resolve` alone. It holds
no list of functions or parts.

`groups(catalogue)` returns one group per set of bench hardware that a
function needs beyond the base setup. The base setup is the locked parts. A
test object is never part of a group key. Each function is in exactly one
group. Order: the group without a part first, then by the count of parts,
then by the catalogue position of the parts. The group without a part holds
the functions every setup runs and the functions that drive a test object and
need nothing else. `alwaysOn(catalogue, fn)` tells the 2 kinds apart,
`plainDrive(catalogue, partId)` returns the function whose tick connects a
test object and forces no option, and `testObjects(catalogue)` lists the test objects. A group
also names:

| Field | Content |
| --- | --- |
| `variants` | Parts of the group that are one of 2 or more boards, with the option that picks the board. |
| `also` | Parts the rules add under every option combination. |
| `oneOf` | Alternative sets of parts the rules add, one per option combination, when they differ. |

`removalEffect(catalogue, selection, partId)` returns the selection without
the part and the difference of the 2 setups. It does not change its input.

| Field | Content |
| --- | --- |
| `refused` | `null`, or why no removal exists: `unknown` id, `base` part, `testObject` (a servo or an ESC leaves with the last function that drives it, not as a part), `absent` from the setup, `held` by a rule that no removal ends. |
| `selection` | The selection after the removal. A refused removal returns the selection as given. |
| `functionsOff` | Functions that go off, each with `chosen`: true for a function of the selection, false for one the parts brought. |
| `functionsOn` | Functions that come on with a part the rules add or with an option that changes. |
| `partsOut`, `partsIn` | Other parts that leave, and parts the rules add. |
| `partsKept` | Parts of a dropped function that stay; the selection holds them as owned parts. |
| `objectsOut` | Test objects that leave: no function that drives them stays. |
| `options` | Options that both setups show and that differ. |

The steps of a removal:

1. The part leaves the owned parts. Every chosen function that needs it leaves the selection.
2. Other selectable parts of a dropped function stay as owned parts. The test object of a dropped function stays connected: the function that drives it without a further part enters the selection.
3. A part that a rule still adds leaves by a change of what its rules read: another value of an option, or the removal of a part. An option that a function forces changes with a part of that function.
4. The search is breadth first with option values before parts. The result is a smallest set of changes. The resolver decides whether a change releases the part.
5. The removal is `held` when no set of changes releases the part. A rule on the absence of a part holds its part: a removal adds no part.

`removalIsPlain(effect)` is true when the part leaves and nothing else
changes. `deselectEffect(catalogue, selection, functionId)` returns the
selection without the function, whether the function stays on, which of
its parts stay or leave, and in `objectsOut` the test objects that leave.

## Commands

Run from `configurator/core` with Node.js 24.

```sh
npm run check                 # static checks; the tree at v0.19.0 is read when git and the tag are present
npm run check -- --strict     # as above; fails when git or the tag is absent
npm run check -- --wires      # prints the wire table as Markdown
npm test                      # the test suite
npm run coverage              # runs the suite and writes the figure into this file
npm run coverage -- --check   # fails when the figure in this file differs; writes nothing
```

Exit codes of `check`: 0 no error, 1 at least one error, 2 unknown argument.
A warning does not change the exit code. Exit codes of `coverage`: 0 figure
current or written, 1 drift or a failing suite, 2 unknown argument.

`--strict` needs the tag `v0.19.0` in the clone. A shallow clone without tags
fails it.

The type check needs the TypeScript compiler and the Node.js type
declarations. Neither is a dependency of this folder:

```sh
npm install --no-save --no-package-lock --prefix "$TMP_TOOLS" typescript@7.0.2 @types/node@24.19.2
"$TMP_TOOLS/node_modules/.bin/tsc" --noEmit -p tsconfig.json \
  --typeRoots "$TMP_TOOLS/node_modules/@types" --types node
```

## Parts list

Every part has rows in `buy`; the page and the PDF list them under "Parts to
buy". A row:

| Field | Meaning |
| --- | --- |
| `maker` | The maker's name as `../../NOTICE` and the trademark list of `../../README.md` write it. `null` for a generic part and for a modelled part. |
| `order` | The ordering name as the maker writes it, in both languages. Without a maker it is the spec, for example "Resistor 2.2 kΩ, 0.25 W". |
| `check` | One short line in English and German: what to check before ordering, the variant traps. |
| `page` | The maker's product page, https, no fragment, no tracking parameter. Absent when no maker page is known. |
| `qty` | Pieces, 1 to 99. 1 when absent. |
| `only` | The options the row applies to. A part with a `variantName` has a row of its own for each variant. |
| `id` | The name of the row within its part: 1 to 12 lowercase letters and digits, unique within the part. At most 1 row of a part has none. The "have it" tick of the page names the row in the URL hash as `<part>.<id>`, or as `<part>` for the row without an id (`rowToken` in `buy.ts`). |

Row ids: `copro` has the row without an id and `osoyoo`; `usb` has `cable`
and `charger`; `wire` has `pair`, `jumper` and `dmm`; `ina228` has `matek`
and `xt90`; `tap` has `resistor` and `zener`; `usbtap` has `board` and
`cable`. Every other part has 1 row without an id. A row added later takes a
new id, so the tokens of the other rows stay.

A test object and a part with `yours: true` are brought: their rows name no
maker and no page. A modelled part names no maker and no page either: its
row gives the required spec.

5 rows have a maker page: Waveshare ESP32-S3-Touch-LCD-7, Waveshare RP2350-CAN,
Osoyoo Breakout Board for Pico series, WeAct Studio PD Power Mini V1 Buck (the
maker's GitHub repository) and Matek I2C-INA-BM. Each answered with HTTP 200 on
2026-10-13. The DAOKAI INA3221 board has no known maker page.

## Motor simulator

The part `sim` is 3 resistors on the phase wires of the ESC. The data file
names it for the default values: 470 Ω, 2 W, delta. `withSim(catalogue, sim)`
returns the catalogue with the name of the part and the order name and the
check line of its row for other values; every other record is the same
object, and the default values return the catalogue itself. The values change
no wire, no rule and no function, and the resolver does not read them.

| Value | Offered | Default |
| --- | --- | --- |
| `cells` | 1 to 6, at `CELL_VOLTS` = 4.2 V per cell | 4 |
| `ohms` | 100, 120, 150, 180, 220, 270, 330, 390, 470, 560, 680, 820, 1000, 1200, 1500, 1800, 2200, 2700, 3300, 3900, 4700 | 470 |
| `watts` | 0.25, 0.5, 1, 2, 3, 5 | 2 |
| `wiring` | `delta`, `star` | `delta` |

`simHeat(sim)` takes 1 commutation step held at the full pack voltage V:

- delta: the resistor across the driven pair carries V/R and takes V²/R, the
  other 2 carry V/(2R) and take V²/(4R) each;
- star: 2 resistors carry V/(2R) and take V²/(4R) each, the third carries no
  current.

Its `level` is `over` above the rating, `hint` from `HINT_SHARE` (0.5) of the
rating, else `ok`. The current limit of the ESC is not modelled.

`test/sim.test.ts` holds the formulas to both check figures (470 Ω at 6S in
delta: 53.6 mA, 1.35 W; 450 Ω at 16.8 V in star: 18.7 mA, 0.157 W), to both
wirings at 1S and 6S, to the sum of the 3 powers over all 1512 combinations,
to the grid points next to both thresholds, and every name and row to the
text rules.

## Checks

`tools/check.ts` reports each finding with the id of the record.

| Check | Fails when |
| --- | --- |
| Ids | An id is used twice, or the wire numbers are not unique and ascending. |
| Wire ends | A wire end is not a pad or a declared pin of a part, or a power wire ends on a signal pad. |
| References | A function, rule, condition, power source or stage names an id that does not exist. A function needs a test object, or drives a part that is none. A rule adds a test object. A test object is locked or selectable, has no function that drives it on the base setup, or has no power source. A power source feeds neither the logic nor a test object, has no advantage or no drawback, or holds a range that is not ordered. No power source feeds the logic. |
| Text | A string is empty in one language, starts or ends with white space, or holds an exclamation mark, a listed marketing word, a character outside the allowed set or a tag other than `<b>` in a safety note. |
| Help | A function with a tick has no entry in `FUNCTION_HELP`, an entry names a function of the base setup or no function, or its text breaks a rule of the row Text. |
| Sources | A record has no source, a file or line does not exist at the tag, the line is empty, the line does not hold the text the anchor names, or a build guide section, a photo or the date of an owner statement is not known, or a document source (`doc`) has no title, no https address or no place in the document. |
| Pins | The pin table breaks one of the 7 rules below, or a wire and the pin table disagree. |
| Pictures | A picture record names a file that `../public/pictures/` does not hold, a photo is not a WebP file or carries Exif or XMP metadata, a thumb is not 480 px wide, the pixel size of the thumb or the drawing differs from the record, an alt text is empty in one language, more than 2 pictures of a part apply to one set of options, a drawing holds a script, an event attribute, a raster image, a data URL or a reference outside the file, or the folder holds a file that no part names. |
| Routes | A slot order is used twice in a column of the drawing, or a lane number is outside its range (left 0 to 18, right 0 to 22, outer 0 to 7, channel 0 to 6). |
| Parts list | A set of options gives a part no row, a variant of a part has no row of its own, `only` names an option value outside the type, a brought part names a maker or a page, a page is not https, does not parse, has a fragment or a tracking parameter, a page has no maker, a quantity is not a whole number from 1 to 99, `yours` is set on a locked or selectable part, more than 1 row of a part has no `id`, an `id` occurs twice in a part, or an `id` is not 1 to 12 lowercase letters and digits. |
| Tree | The pads, the reserved pins, the heartbeat pin or a pin default differ from the firmware sources at the tag. |

Each repository anchor in `catalogue.ts` is `repo(path, line, text)`. The text
is 4 characters or more of the cited line, and it states the fact the record
takes from that line. The check compares it with the file at the tag, with each
run of white space read as one space. The texts are the export `ANCHOR_TEXTS`;
they are not part of `CATALOGUE`. Build guide sections, photos and owner
statements are checked by id, number and date only.

A release tag newer than `v0.19.0` is a warning. `tools/check-annotate.sh`
runs the check and shows each warning as an annotation and in the summary of
the run. The Configurator workflow runs it and fails on an error. The Release
workflow runs it with `--warn-only` on every tag push: an error is a warning
there as well, and no release waits for the check.

The data moves to a release in the first pull request after the tag of that
release: the anchors are read at the tag, so the tag exists first. That pull
request changes `RELEASE_TAG` in `tools/check.ts`, `TAG` and `release` in
`catalogue.ts`, the `tag` type in `../contract.ts`, `RELEASE` in
`../src/i18n.ts`, `release` in `../drawings/anchors.json` and `version` in
`../package.json`, and sets each anchor to the line that holds its text at
the new tag.

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

- 256 sets of the 8 selectable parts times 16 option combinations: 4096 setups.
- 4096 sets of the 12 functions with a tick times 16 option combinations: 65536 setups. The option combinations hold `servoPort: outputs`; the function `servoprog` sets `prog`.
- 2000 mixed selections from a fixed seed.

Each setup is checked for: the same result on a second call, no exception, no
pin of a part on two nets, no power wire on a pad, every wire end a declared
pin of a part that is present, catalogue order of every output array, the
check entry `servo-bench-supply` exactly when a wire of the servo supply
ends on a pin of the bench, and the part `dcsup` with its wires 65 and 66
exactly when the servo takes the V pin of the strip of the Osoyoo breakout
board. One power wire ends on a pad by intent: the plus of the servo on pad 40
(VBUS) under `servoPower: bench`.
The sweep refuses a catalogue with more than 16 selectable parts or 16
functions that need a part.

Two wires of different nets never hold the same lane of a gutter. Two wires of
one net may: wires 19 and 63, the 2 halves of the power path of the PD mini,
both run in outer lane 4.

The data is checked for its power paths:

- A part that a rule adds for another part has a wire to that part, or a wire
  on a net other than ground that a wire of that part is on. A net may reach
  that net through a part outside the data: the test lists such bridges in
  `THROUGH`. 1 bridge: `osoyoo_vin` to `strip_v`, the XL1509 converter of the
  Osoyoo breakout board with the jumper on 5V.
- A part with `between` has a wire to each of the 2 parts it names. A part whose
  role text says "sits between" carries `between`. The T-plug names the PD
  charger and the PD mini: wire 19 runs from the charger to its socket IN,
  wire 63 from its socket OUT to the USB-C socket of the PD mini. The current
  rating the cable of wire 63 needs is not stated.

`test/groups.test.ts` runs every removal the data file allows:

- 4096 sets of functions times 2 values of `escPower`, each part of each setup removed.
- 256 sets of owned parts times 2 values of `escPower`, each part of each setup removed.
- 1500 mixed selections from a fixed seed under all 16 option combinations.
- 4096 sets of functions, each function deselected.

Each removal is checked for: no refusal for a part of the bench beyond the
base setup, the refusal `testObject` for a servo or an ESC, the part gone, the reported lists equal to the difference of the 2 setups, a cause
for every function that goes off or comes on, and the function set restored
when the part and the parts that left with it are added back. The suite takes
about 27 s on one core of the build host.

Further tests cover the empty selection, the full selection, each part and
each function alone, unknown ids, a missing German string, a broken source
anchor and a data file that differs from the firmware tables.

`test/property/` holds the properties of the resolver and of the data file.
`resolver.test.ts` walks the same 2 sweeps and 25 000 mixed selections from
the seed `0x20261011`, and refuses a space of more than 100 000 cases per
sweep. Each setup is checked for:

1. A second call and any order of the input give the same setup.
2. A part that a function, a tick or the lock holds never leaves the setup
   when a function or a part is added. A part that leaves is held by rules
   alone, is not selectable, and the setup names what replaces it.
3. A function that is enabled stays enabled when a function or a part is
   added, unless a function forces the other value of an option, and then
   the option names that function.
4. Every part names at least one reason, and each reason is true.
5. Every function that is not enabled names the parts that are missing, or
   an option it forces has another value.
6. Every wire ends on a pad, a CAN (Controller Area Network) terminal, or a
   pin of a part in the setup that exists under the options of the setup.
7. No pin and no pad carries two nets.
8. No two wires join the same two ends, and no pin has two stubs.
9. The two entry modes agree: the functions a set of parts enables resolve
   to parts of that set, plus replacement parts the setup names.
10. A setup holds released parts only, and no id of the list "not in a
    release".

`catalogue.test.ts` checks the data file without the firmware tree: unique
ids, ids that fit the URL hash, references that resolve, parts that are
reachable, English and German for every text, no word of the list in
`words.ts`, a source for every record.

## Coverage

Line coverage of the resolver, the groups and the motor simulator, measured by `node --test --experimental-test-coverage`.
`tools/check.ts` and `catalogue.ts` are not part of the figure.

<!-- coverage:begin -->
| File | Lines run | Lines | Line coverage |
| --- | ---: | ---: | ---: |
| `resolve.ts` | 215 | 215 | 100.0 % |
| `cond.ts` | 51 | 51 | 100.0 % |
| `groups.ts` | 416 | 416 | 100.0 % |
| `sim.ts` | 120 | 120 | 100.0 % |
<!-- coverage:end -->

## Unknown in the data

- Pins marked unconfirmed: `GPO` of the AS5600 board: no source at `v0.19.0` shows its print. `LP` of the INA3221 board: both sides of the board print GND, GND, VPU; the function of the 3 pads is not confirmed.
- Emergency stop switch: the tab names C, NO, NC come from the wiring diagram of the seller, per an owner statement of 2026-10-11. No measurement is stated. The check entry `estop-tabs` is of kind `measure`.
- The pin for the servo signal (GP2) is a proposal of this data file. The repository names no servo pin.
- The pins of the PD mini serial lines (GP7 transmit, GP6 receive) come from an owner statement of 2026-10-11. The firmware default for both is -1 (off). The direction, transmit to D− and receive from D+, is from `shared/settings/settings.c`.
- The INA228 module with XT90 plugs has its shunt in the minus line and both lines of the ESC supply on its 2 plugs, per the same owner statement. With this module the data holds no direct wire between the minus of the source and the minus of the ESC: wire 56 (battery) or wire 57 (PD mini) ends on the pin BAT− of the module and wire 58 runs from the pin ESC− to the ESC. The wires 15 and 23 belong to the MATEK board and to a setup without the sensor. The check entry `ina228-xt90-pads` states the pads and the 2 rails.
- INA228 module with XT90 plugs and ESC power B: G of the module is the ESC end of the shunt and is wired to GND of the coprocessor. A connection between the minus output of the PD mini and GND of the coprocessor is a second path beside the shunt. The share of the ESC current that the sensor reads is not measured. The check entry `ina228-xt90-pd` is of kind `unknown`.
- With the INA3221 on the sensor bus the sources disagree on the 2 pull-up resistors of 2.2 kΩ: the build guide lists none, `docs/FirstRun.md` lists both. The data follows the build guide and states the disagreement.
- Servo on pad 40 (VBUS): the current limit of the pin is not stated. "Small servo" carries no number.
- Osoyoo breakout board: the jumper V Selector (header H3) sets the V pins of all 40 strips: 3V3 from the regulator U1 (input VSYS), 5V from the XL1509 converter U9 (input DC jack, VIN terminal or 2-pin header H4). The sources are the schematic REV 1.0 (file of 2024-12-03), the XL1509 datasheet Rev 2.0 and the product page of 2024-12-02, cited by the source kind `doc`. The currents 2 A at 5 V and 500 mA at 3.3 V are vendor values, not measured. The DC input range differs between the print (7 to 16 V), the vendor (7 to 12 V recommended, 7 to 18 V limit) and the chip (7 to 40 V). With USB alone and the jumper on 3V3 the schematic and the product page disagree; not measured.
- DC supply of the Osoyoo breakout board (`dcsup`): the current it must deliver is not stated. The product page rates the output of the board at 2 A at 5 V; no source states the input current.
- DC jack of the Osoyoo breakout board: no reverse-polarity protection. A centre-negative plug puts the reverse voltage through the switch OFF/ON on the input of the XL1509 converter. The SI2302 (Q3) protects the VIN terminal and H4 only.
- DC jack of the Osoyoo breakout board, plug size: the product page names the socket DC005-2.1. The 2.1 mm pin follows from that name. The 5.5 mm outer diameter is the usual size of that socket family; no vendor source states it.
- SI2302 (Q3) of the Osoyoo breakout board above 12 V: its gate sits at PWRIN through R7 with no clamp and its source at GND (schematic REV 1.0), so its gate-source voltage is the input voltage at every input. The datasheet linked on the product page rates the gate-source voltage at ±12 V (absolute maximum). Above 12 V Q3 is outside that rating; the print of 16 V and the vendor limit of 18 V are above it. The data keeps the range at 7 to 12 V.
- ESC STICK programming: `docs/StickProgramming.md` at `v0.19.0` runs it on ESC power B (PD mini). An owner statement of 2026-10-11 says that programming an ESC needs no power. The data keeps the PD mini as needed bench hardware; what powers the ESC during programming is stated as unknown in the check entry `esc-programming-power`.
- 9 check entries of kind `unknown` state each open point in the text of a setup.
