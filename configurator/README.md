# rcbench bench configurator

A static page for release 0.22.1 of rcbench. The visitor ticks what the bench
does. The page lists the parts, draws the wiring and prints a pin-to-pin table.
The functions stand in groups by the bench hardware they need. A part of the
result beyond the base setup has a remove control.
Without a tick it shows the base setup: panel, IOmcu and the 4 wires
between them.

A servo and an ESC (electronic speed controller) are test objects: devices the
visitor connects and tests. They are not hardware of the bench. The page lists
them under "What you connect and test", never as a part the bench needs.

The page states once, in its footer, that rcbench is under development and
that its functions are not verified on hardware. No function, part or power
source repeats that. "Not in a release" on a part is a different fact and
stays on the part.

- No server and no account. The selection lives in the URL hash. The browser
  keeps the selection and the chosen language in `localStorage`, on the
  visitor's device. See "Stored state".
- No run-time dependency. The page loads 3 files from its own origin and sends
  no request to another origin. A picture loads from the same origin when its
  part card comes near the viewport. A link to a maker page or a web search
  leaves the page on a click only; see "Parts to buy".
- English and German. The start language follows the browser.
- Light and dark theme follow the system setting. Print gives an A4 document.
- The footer of the page states that product and company names belong to their
  owners and links to the repository's
  [NOTICE](https://github.com/subtilitas/rcbench/blob/main/NOTICE) as
  "Trademark notice", in German "Markenhinweis". Besides the links of the
  parts list it is the 1 link of the page to another origin. The page loads
  nothing from it. The Tab key reaches it as the last control of the result.
- Below 1024 px width a bar is fixed to the bottom of the viewport. Its
  height is 57 px with labels of 1 line; the labels wrap at a large text
  size and the bar grows. The page keeps the rendered height of the bar plus
  16 px free at its lower edge (`padding-bottom`, `scroll-padding-bottom`): a
  control that takes the focus scrolls to above the bar.
- The page is served under the base path `/rcbench/`.

## Layout

| Path | Content |
|---|---|
| `contract.ts` | Types shared by the core and the page. |
| `core/` | Data file, resolver and checks. See `core/README.md`. |
| `src/core.ts` | The one import seam to the core: catalogue, resolver, default options, groups, removal. |
| `src/main.ts` | State, events, rendering. |
| `src/hash.ts` | URL hash codec. |
| `src/store.ts` | The state the browser keeps between visits: read, check, write. |
| `src/i18n.ts` | Chrome strings in English and German. |
| `src/names.ts` | Names of wire ends for the table and the drawing. |
| `src/draw/layout.ts` | Geometry of the drawing: modules, pads, lanes, wires, pills. |
| `src/draw/svg.ts` | SVG (Scalable Vector Graphics) markup of the drawing. |
| `src/diagram/scene.ts` | Wiring picture: the parts and wires of a setup, load-current class of a wire. |
| `src/diagram/layout.ts` | Wiring picture: columns, part places, lane widths. |
| `src/diagram/route.ts` | Wiring picture: the route of every wire in right angles. |
| `src/diagram/picture.ts` | Wiring picture: SVG markup of the page and its legend. |
| `src/diagram/export.ts` | Wiring picture: SVG markup inside the element subset of the PDF painter. |
| `src/diagram/view.ts`, `control.ts` | Wiring picture: pan and zoom arithmetic; the listeners of the page. |
| `src/view/` | Chooser, result document, table, parts list (`buy.ts`: rows, search link, text and CSV), heat calculation of the motor simulator (`sim.ts`). |
| `src/export/` | PDF export of the resolved setup. See "PDF export". |
| `public/pictures/` | Pictures of the parts: photos as WebP, drawings as SVG. |
| `drawings/` | Generator and check of the part drawings, anchor table, wire palette, and the panel drawing of the IOmcu board (`panel.json`). See `drawings/README.md`. |
| `public/drawings/` | The part drawings, written by `npm run drawings`: 23 SVG files in English, 23 in German under `de/`. |
| `test/`, `drawings/test/` | Unit tests, run by `node --test`. |
| `test/property/` | Properties of the hash codec, the markup and the texts over the whole selection space. |
| `test/corpus/` | Stored links with the setup each one gives. See `test/corpus/README.md`. |
| `e2e/smoke.spec.ts` | Playwright smoke test against `vite preview`. |
| `e2e/matrix/` | Playwright tests of 15 setups in 8 cells of viewport, language and theme, with the PDF export and the removal of a part, and on every stored link. |
| `e2e/lib/` | Helpers for Playwright tests. See `e2e/lib/README.md`. |
| `e2e/select.spec.ts` | Playwright test of the groups, the state words and the removal of a part. |
| `e2e/help.spec.ts` | Playwright test of the help bubbles: keyboard focus, pointer, Escape, a tap at 360 px, German text, one bubble at a time after a click on a mark and between the focus of a tick and the pointer on another mark, every bubble whole inside the chooser at 1280 × 800 px, the top of every bubble and its end after a scroll at 1100 × 450 px and 1024 × 500 px, every bubble above the bar at 360 px. |
| `e2e/store.spec.ts` | Playwright test of the stored state: reload, visit without a hash, change of language, link against stored state, a store that is blocked, full or of another shape. |
| `e2e/buy.spec.ts` | Playwright test of the parts list: rows, owned parts, variants, copy buttons, links and no request to Google before a click. |
| `e2e/sim.spec.ts` | Playwright test of the motor simulator by keyboard alone, in English and German at 360 px and 1280 px: the 4 inputs, the figures, the warning and the hint, the link, the part name, the row to buy, both copies, a link with a value the page does not offer, "Start over". |
| `e2e/export.spec.ts` | Playwright test of the PDF export against `vite preview`. |
| `e2e/diagram.spec.ts` | Playwright test of the wiring picture. Writes screenshots of 3 setups to `test-results/diagram-shots/`. |
| `tools/size.ts` | Size check of the build. |
| `tools/shots.ts` | Screenshots of the build. |
| `tools/pdf-samples.ts` | 2 sample files of the PDF export. |
| `tools/font-subset.py` | Writes the 2 font files of the PDF export. |

## Release number

`package.json` holds the release in its `version` field. `release.ts` exports
it as `VERSION` and as the tag `TAG` (`v` plus the version). The data file,
the page header, the trademark notice, the print head, the source links and
the checks import them. The anchor table `drawings/anchors.json` is written
by `npm run drawings` from the data file, and `npm run drawings:check` fails
when it names another release.

The release moves in 2 steps, because the data check reads the files at the
git tag and the tag exists only after the release pull request is merged:

1. In the release pull request, set `RCBENCH_VERSION_MAJOR`, `_MINOR` and
   `_PATCH` in `shared/link/include/rcbench_version.h`. `npm run check` in
   `core/` then reports a warning: the header is ahead of the configurator
   data. The warning does not fail `--strict`.
2. After the tag `vX.Y.Z` exists, set `version` in `configurator/package.json`
   (and the 2 `version` fields at the top of `package-lock.json`), run
   `npm run drawings` to rewrite `drawings/anchors.json`, and replace the
   release number in the prose of the 3 README files of the configurator.

`core/tools/release-sources.ts`, run by `npm run check` in `core/`, compares
`package.json` with the header and with `package-lock.json`:

| State | Level |
|---|---|
| `package.json` equals the header | none |
| Header ahead, tag of the header version absent | warning |
| Header ahead, tag of the header version present | error |
| `package.json` ahead of the header | error |
| A `version` field of `package-lock.json` differs from `package.json` | error |
| A README names another release, in prose or as `"release": "X.Y.Z"` | error |

The Configurator workflow runs the check on a change of the header or of
the configurator. Source links point at the git tag `TAG`; the data check
reads the files at that tag, so the tag must exist in the clone.

## Commands

Node.js 24 is required.

```bash
cd configurator
npm ci
npm run typecheck   # tsc --noEmit
npm test            # unit tests
npm run drawings    # writes public/drawings/, drawings/anchors.json and drawings/panel.json
npm run drawings:check   # fails on a finding or on drift, writes nothing
npm run build       # runs the drawings check, then writes dist/
npm run size        # initial load at most 250 kB, dist/ without pictures at most 1 MB
npx playwright install chromium
npm run e2e         # smoke, chooser and export tests at 360 px and 1280 px width
npm run e2e:matrix  # matrix and corpus tests
npm run dev         # http://localhost:5173/rcbench/
node tools/shots.ts shots   # 24 PNG files: 2 widths, 2 themes, 2 languages, 3 selections
node tools/pdf-samples.ts pdf-samples   # 2 PDF files: fewest wires in English, most wires in German
```

## URL hash

`#v=1&f=<ids>&p=<ids>&o=<tokens>&h=<rows>&m=<tokens>&l=<lang>`

| Key | Meaning |
|---|---|
| `v` | Format version, `1`. Another value gives the base setup and a notice. |
| `f` | Ids of the functions the visitor ticked, comma separated. A function that the parts of the setup bring is not stored. A function that needs no part has no tick; its id in a link is dropped without a notice. |
| `p` | Ids of parts the visitor owns. A test object is no such part: the page writes none. In a link, `p=servo` is read as `f=servo` and `p=esc` as `f=esc`, without a notice. The T-plug (`usbtap`) is no such part either: only the PD mini brings it. A link with `p=usbtap` loads and the id is dropped without a notice, like the id of a function that has no tick. Copy link, the print header and the PDF name the link without the id; the address of the page keeps it until a change. An id of that spelling under `f` or `o` keeps its notice. The PD mini of the same setup still brings the T-plug; without the PD mini the setup holds none. Any other id the catalogue does not offer as a tick keeps the notice. |
| `o` | Option tokens that differ from the default: `osoyoo`, `xt90`, `pd`, `servo5v`. |
| `h` | Row tokens of the "have it" ticks of "Parts to buy", comma separated, in catalogue order. A token is the part id for the row of a part without a row id (`panel`), else the part id, a dot and the row id (`wire.dmm`). A token that names no row to buy of the setup is dropped and named in the notice: an unknown row, a part outside the setup, a part ticked under "Parts you own", a part the visitor brings, a row of the other variant. A link without `h` has no "have it" tick. |
| `m` | Values of the motor simulator that differ from the default, in the order cells, resistance, rating, wiring. See "Motor simulator". A token the page does not offer is dropped and named in the notice; its value stays the default. A second token of the same value, as in `m=6s,3s`, is dropped and named: the first counts. A link without `m` has the default values. |
| `l` | `en` or `de`. Written when it differs from the browser language. |

Copy link, the field it shows when the clipboard refuses, the print header
and the PDF name the same link. A link that names a function without a tick,
a test object under `p`, `p=usbtap` or a row token under `h` that the page
dropped is written again from its selection: those ids leave, a test object
stands as the tick of its function, and the key `l` stays as the link has it. Every other link is named as it stands in
the address. The address that the browser prints in its own page margin is
the address of the page.

Limits: 2000 characters per hash, 64 ids per key, 32 characters per id of
lowercase letters and digits; a row token under `h` adds a dot and a row id
of 1 to 12 lowercase letters and digits. A longer hash gives the base setup
and the version notice. An unknown id is dropped and named in a notice.

The row token stays when the catalogue adds a row to a part: each row of a
part with more than 1 row carries an id of its own (`Buy.id`), and at most 1
row of a part has none. A token never counts rows by position.

A link without `servo5v` keeps the external servo supply. `test/fixtures/links-v1.json`
holds 1192 links without that token and the parts, wires, active functions
and variants each resolves to; `test/hash.test.ts` fails when one differs.
The fixture lists the power path of the PD mini as wire 19 alone. The test
reads wire 63, the second half of that path, apart: it stands in a setup
exactly when wire 19 does.

## Stored state

The page keeps 1 entry in `localStorage`, under the key
`rcbench.configurator`:

```json
{"v":1,"lang":"de","setup":"v=1&f=servo,esc&o=osoyoo"}
```

| Field | Meaning |
|---|---|
| `v` | Format version, `1`. An entry with another value is not read. |
| `lang` | `en` or `de`: the language the visitor picked with the language buttons. `null` before the first pick. |
| `setup` | The selection in the format of the URL hash, without the key `l`: ids, option tokens, row tokens and the tokens of the motor simulator, no text of a language. `null` before the first change. |

The page writes `setup` on every change of the selection: a tick, a variant,
a "have it" tick, a value of the motor simulator, the removal of a part, "Start over". It writes `lang` on a click on a
language button. It writes nothing when it opens, so a link that is opened
and not changed leaves the stored setup as it is.

Precedence when the page opens and when the hash changes:

| State | Source, first match |
|---|---|
| Setup | A hash with a key other than `l`; the stored `setup`; the base setup. |
| Language | The key `l` of the hash; the stored `lang`; the browser language. |

A setup that comes from the store is written into the hash, so the link to
copy and the PDF name it. The key `l` of the hash still compares with the
browser language, not with the stored one.

The entry is checked on every read. An entry of more than 4096 characters, no
JSON object, another `v`: the page reads none and shows the base setup in the
browser language. A `lang` other than `en` and `de` reads as `null`. A `setup`
of more than 2000 characters or of another hash version reads as `null`. An
id the catalogue does not know is left out of a stored setup without a
notice. A browser that blocks `localStorage` or refuses a write leaves the
page at work: the selection stays in the hash and no message is shown.

2 tabs share the entry. The tab that changes last sets what the next visit
without a hash shows. An open tab does not follow a change made in another.

The view of the wiring (picture or schematic), the open state of "Parts you
own", the zoom of the picture and a marked wire are not stored.

## Chooser

Each group of functions is a `fieldset`. Its legend names the bench hardware
the group needs beyond the base setup: "Needs: PD mini". No legend names a
servo or an ESC. A group whose parts are all in the setup carries the tag
"In this setup". The 8 groups: base setup, PD mini, INA3221, AS5600, INA228,
microSD card, emergency stop switch, motor simulator + phase tap.

The first group is "Runs on the base setup". It has 2 parts:

- A list of the 12 functions every setup runs. A row has no tick and reads
  "Included with the base setup.". The id of such a function is never written
  into the link and never counts as selected by the visitor. The removal of a
  part and of a tick never lists such a function as going off.
- 3 rows with a tick, under the sentence "These functions need no further
  part of the bench. A tick connects the test object.": "Drive a servo",
  "Program a KST or Chaservo servo" and "Drive an ESC".

A function is active when the setup holds every part it needs and every option
it forces, and the test object it drives is connected. Each row states its
state in words:

| State | Text | Tick |
|---|---|---|
| Ticked by the visitor | "Selected by you." | yes |
| Active through the parts of the setup | "Included with these parts." (German: "Enthalten bei diesen Teilen.") | no |
| Active through a connected test object | "Active: Servo is connected." | no |
| Runs on the base setup | "Included with the base setup." | none |
| Not active, needs the test object only | "Connects: Servo." | no |
| Not active | "Adds: <parts>" or the reason | no |

A part carries one reason line. A part that a rule adds along with a bench part reads "Included with these parts: PD mini." (German: "Enthalten bei diesen Teilen: PD mini."), as the T-plug and the charger do. A part that a rule adds for another cause, such as an option or a test object, reads "Needed by: ESC power B." (German: "Nötig für: ESC-Versorgung B."). A part that a function needs reads "Needed by: <function>."

4 variants follow the setup: connection to the IOmcu, INA228 board, ESC
power and servo power. Servo power is A, the visitor's own supply (default),
or B, from the bench for a small servo: the 5 V pin of the IOmcu (pad
40), or the V pin of the strip of the Osoyoo breakout board. With B the result
and the PDF carry a note: the servo draws from the supply of the logic, which
is prone to problems. On the V pin of the strip the jumper V Selector sits on
5V, and the setup adds the DC supply of the board, 7 to 12 V DC, with its 2
leads, wires 65 and 66. It is a modelled part: the page and the PDF tag it
"Modelled part: no product chosen", and its text gives the range and "not
stated" for the current. "Small" carries no number: no source states a current
limit. An ESC takes signal and GND only, in every variant.

"Program a KST or Chaservo servo" (`servoprog`) needs the panel and the
IOmcu alone. Its tick moves the servo signal from wire 7 (pad 4, GP2,
a slot of SETUP → OUTPUTS) to wire 64 (pad 17, GP13), the programming port
of an IOmcu that has stored no other pin for it. The result carries 3
notes and 1 unknown: SERVO TESTER on the PROG PORT entry of SETUP → OUTPUTS
is PROG PORT; with the PD mini, CONNECT sets the servo supply, 4.8 to 6.0 V or up to 8.4 V with HV
SERVO on, and 50 to 3000 mA; without the PD mini the servo takes the supply
of the servo power variant, and the programming voltage and current limit
are stored and not applied; the level of the servo's reply on GP13 is not
measured. The pin is no visible variant and the hash holds no token for it:
the function sets it.

Every function with a tick and every part under "Parts you own" has a help
bubble. Its first line says what the item does: the text of `core/help.ts`
for a function, the `role` of the part in the data file for a part. The
lines after it say what the item needs, derived by `src/view/help.ts` from
the resolver in the variants of the page:

- "Needs: ..." lists the bench parts the setup with the item alone holds
  beyond the base setup. A part that only a rule adds carries the parts whose
  absence the rule asks for: "2 × 2.2 kΩ (only without INA3221)".
- "Connects: ..." names the test object the item connects.
- "Variant: ..." names the variant the function sets.
- A part names its supply wires, the wires of class logic and power that
  end on it, with the note of the wire: "Supply, 3.3 V: IOmcu 3V3
  (pad 36).", and for the PD mini its power input, "PD in: T-plug OUT, USB-C
  socket." and the note of the USB-C cable.
- Without a further part the line reads "Needs no further part of the
  bench.".

The functions of the base setup, the test objects and the parts that are not
selectable have no bubble. The bubble opens when its tick has the keyboard
focus and when the pointer rests on the round "i" mark at the right end of
the row, 44 px wide. The pointer can move from the mark into the bubble. A
click or tap on the mark opens or closes the bubble and never changes the
tick; a tap on the label still toggles it. A press anywhere else
(`pointerdown`), Escape and the loss of the focus close it. One bubble shows
at a time: the focus of a tick and the pointer entering a mark close a
bubble that a click opened on another row, and keep the bubble of every
other row hidden (`data-quiet`) until the focus or the pointer comes back
to it. A click that closes a bubble
keeps it closed while the pointer rests on the mark; the pointer leaving and
entering the mark again opens it. The mark has
`cursor: pointer`: iOS Safari delivers a tap on an element without it and
without a handler of its own to no click listener of the document. The mark
and the bubble take no focus, so the Tab order is that of the ticks. Each
tick names its bubble in `aria-describedby`; the bubble has the role
`tooltip`. The bubble spans the width of the row, so it stays inside a
viewport of 360 px. It opens under the row, and over it when the room under
the row is less than the height of the bubble and the room over it holds the
whole bubble. The room is the viewport above the bar of the narrow layout,
cut by the box of the chooser, which scrolls on its own at 1024 px and wider.
The page places a shown bubble again when the page or the chooser scrolls or
the window changes size. A bubble taller than both rooms opens under the
row: its top shows, and the chooser or the page scrolls down to its end. At
1280 × 800 px every bubble shows whole, in English and German; at
1100 × 450 px and 1024 × 500 px the top of every bubble shows.

Removing the tick of a function removes it from the selection. A note under
the row states whether the function stays active, which of its parts stay
or leave, and whether its test object stays connected.

Every part of the result beyond the base setup has a button "Remove". A test
object has none: it leaves with the tick of the last function that drives it.
"Parts you own" removes a part by its tick. It holds no row for a servo or an
ESC. Every removal opens a question under the
control; no part leaves without the confirming answer. The question lists the
functions that go off with the state of each, the other parts that leave or
come in and the option that changes. For a part that takes nothing with it, it
states "No function goes off and no other part leaves." Its 2 answers are
"Remove part and these functions", or "Remove part" when no function goes off,
and "Keep". The focus moves to "Keep". "Keep" and the Escape key return the
focus to the control. The question has the role `alertdialog` and does not
trap the focus.

Parts of a removed function that stay in the setup become ticks under "Parts
you own". Its test object stays connected by the tick of "Drive a servo" or
"Drive an ESC".

## Result

| Heading | Content |
|---|---|
| Parts | The bench hardware of the setup, each with the reason it is there. The summary counts these parts. The motor simulator carries its heat calculation. See "Motor simulator". |
| What you connect and test | The connected test objects, each with the functions that use it. Without one, a sentence says so. |
| Parts to buy | What to order for the setup, with quantity, maker, ordering name, check line, maker page, web search and a "have it" tick, and the copy buttons. Under "You have", the rows with that tick. Under "You bring", the test objects and their supplies. See "Parts to buy". |
| Power sources | The sources of the logic and of each connected test object, from `powerSources` of the data file: voltage and current where stated, advantages, drawbacks, unknowns. "In this setup" marks a source the wires of the setup use; "In this setup: one of these" marks the 2 external servo supplies, of which the setup uses one. |

The footer holds the state of the project and the trademark notice.

## Motor simulator

The motor simulator is 3 resistors on the 3 phase wires of the ESC. Its card
under "Parts" holds 4 inputs and the current and the power per resistor. The
values name the part and its row to buy on the page, in the PDF and in both
copies of the parts list: "Motor simulator, self-built: 3 resistors in delta,
470 Ω, 2 W" and "3 × Resistor 470 Ω, 2 W". They change no wire and no
function.

| Input | Values | Default | Tokens in `m` |
|---|---|---|---|
| LiPo pack | 1S to 6S at 4.2 V per cell: 4.2 V to 25.2 V | 4S, 16.8 V | `1s` to `6s` |
| Resistance | The 21 E12 values from 100 Ω to 4.7 kΩ | 470 Ω | RKM code: `100r` to `820r`, `1k`, `1k2` to `4k7` |
| Rating | 0.25 W, 0.5 W, 1 W, 2 W, 3 W, 5 W | 2 W | `0w25`, `0w5`, `1w`, `2w`, `3w`, `5w` |
| Wiring | Delta, star | Delta | `delta`, `star` |

The default pack is 4S: the 1 ESC reading of `docs/DShot.md` ran on a 4S
pack, and ESC power B, the PD mini, gives at most 20 V, which 4S (16.8 V)
stays under and 5S (21.0 V) exceeds.

Load case: one commutation step held at the full pack voltage V. A sensorless
ESC cannot commutate on resistors, so it can hold a step. R is the resistance
of 1 resistor.

| Wiring | Hottest | Others |
|---|---|---|
| Delta | 1 resistor across the driven pair: V/R and V²/R | 2 in series across V: V/(2R) and V²/(4R) each |
| Star | 2 in series across V: V/(2R) and V²/(4R) each | 1: no current |

Check figures: 470 Ω at 6S (25.2 V) in delta give 53.6 mA and 1.35 W.
450 Ω at 16.8 V in star give 18.7 mA and 0.157 W; 450 Ω is no E12 value and
the page does not offer it.

The card shows:

- the hottest resistor: current in mA, power in W and the share of the rating
  in whole percent, rounded down, and rounded up above the rating;
- the other resistors;
- above the rating, a warning (`#sim-over`): "Above the rating of 2 W. Choose
  a higher rating or a higher resistance.";
- from 50 % of the rating up to the rating, a hint (`#sim-hint`). It states
  its basis: the rating holds only up to the ambient temperature that the
  datasheet of the resistor states, and a resistor near its rating runs hot.
  It names no derating figure. The 50 % threshold is a choice of the page,
  not a figure of a maker;
- the load case, and 1 sentence: the ESC also limits the current, and that
  is not modelled;
- with star, how the resistors join, and that the drawing shows delta.

Numbers carry 3 significant figures, a voltage 1 decimal. German uses the
decimal comma.

A change of an input writes the link and the stored setup, keeps the focus
on the input and announces the hottest resistor and the warning or the hint
in the live region. "Start over" sets the default values. The "have it"
token `sim` names no resistance and no rating: a change of either takes the
"have it" tick off the row of the resistors, a change of pack or wiring
keeps it. The PDF lists the
pack, the figures, the warning or the hint and the load case under the part.
Its file name follows the values. Print leaves out the 4 inputs: the name of
the part and the load case name the values.

`core/sim.ts` holds the values and the formulas. `src/view/sim.ts` holds the
text and the markup.

## Parts to buy

The section "Parts to buy", in German "Teile zum Kaufen", lists the rows of
`buy` in the data file for the parts of the setup, in catalogue order. See
"Parts list" in `core/README.md`.

| Element | Content |
|---|---|
| Disclaimer | "Parts list as of release v<release>. Makers change products without notice. Check every part against its check line before ordering. No warranty." The release is the `release` field of the data file. |
| Row | Quantity ("2 ×"), maker and ordering name, "Any maker" for a generic row, "Modelled part: no product chosen" for a modelled part, the check line, the links "Maker page" and "Web search", then the tick "Have it" ("Habe ich"). |
| You have | Heading "You have" ("Hast du"), shown while a row has the tick "Have it". Each row with quantity, maker and ordering name and the ticked checkbox; removing the tick returns the row to the rows to buy. |
| You bring | The test objects and the parts with `yours: true`: servo, servo supply, ESC and motor, ESC battery. Each with its check line, without a link and without a tick. |
| Copy as text | Heading with the release, the setup link of Copy link, the disclaimer, the rows to buy with their links, the rows under "You have" by quantity and name, the rows to bring. Lines end in LF. |
| Copy as CSV | A header row in the page language, then 1 record per row to buy. Columns: quantity, maker, ordering name, check, maker page, search link. Every field in double quotes, a double quote doubled, records end in CR LF (RFC 4180). The rows under "You have" and the rows to bring are not in it. |

The row of the motor simulator names the resistance and the rating of its
card. See "Motor simulator".

A part ticked under "Parts you own" (`p` in the hash) is left out. A part
that a function brings in stays in the list; its tick under "Parts you own"
shows the reason, not ownership. A variant (`o=osoyoo`, `o=xt90`) lists the
row of that variant: the Osoyoo breakout board adds a row, the XT90 module
replaces the Matek board.

Each row to buy has a tick "Have it": an `<input type="checkbox">` inside a
`<label>`, 22 × 22 px, in the tab order after the links of its row. Its
accessible name is "Have it: <maker and ordering name>". A tick moves the row
under "You have" and writes its row token under `h` in the hash; the focus
moves with the checkbox. The live region says "Moved to "You have"." or
"Back under "Parts to buy"." with the counts of the setup. A part with 3 rows
(`wire`) has 3 ticks: the multimeter can be ticked while the wires stay to
buy. A tick leaves the hash when its row stops being a row to buy: the part
leaves the setup, gets a tick under "Parts you own", or the variant gives the
part another row. Print hides the ticks.

The search link is built when the list is rendered:
`https://www.google.com/search?q=` and the percent-encoded query. The query is
`"<maker>" "<ordering name>"`; without a maker it is the spec, split at ", ",
each term in double quotes. A double quote inside a term is dropped. The
characters `! ' ( ) *` are percent-encoded as well. Example: the pull-up
resistor gives
`https://www.google.com/search?q=%22Resistor%202.2%20k%CE%A9%22%20%220.25%20W%22`.

Every link of the list opens in a new tab with `rel="noopener noreferrer"`.
The page embeds, preloads and prefetches nothing of Google or a maker; the
first request to either follows a click. When the clipboard refuses, the
copy buttons show the text in a read-only field, as Copy link does. The field
holds the CSV with LF line ends: a text field stores no CR.

The list names makers in plain text, without logos or pictures of the maker.
Every maker of the list is named in `NOTICE` and in the trademark list of the
repository README; `core/test/buy.test.ts` fails when one is missing.

## Drawing

The drawing is 970 px wide. Its height follows the modules of the setup.
Modules stand in a left and a right column, the IOmcu in the middle.
Every wire runs in right angles through numbered lanes and carries its wire
number in a pill at each module end.

`test/layout.test.ts` draws every distinct setup the resolver gives for every
subset of the 12 functions with a tick and every subset of the 8 selectable parts, in all 16
option combinations and both languages. It fails when:

- a wire has no path, leaves the canvas or runs outside a lane gutter,
- two wires of different nets share a line or run closer than 5 px,
- a pill covers a wire of another pin or another pill,
- a module text is wider than its module,
- a wire number names different ends in two selections.

## Wiring picture

The result shows the wiring in 2 views behind a switch: the picture and the
schematic of the section "Drawing". The picture is the first view. Both views
are in the document at all times; print shows both, the picture on a page of
its own, fitted to the page width. The wire table is the reference: a wire
number in the picture names the row with the same number.

The picture places the part drawings of `public/drawings/` in columns, the
IOmcu in the middle column, and routes every wire from wire point to
wire point of `drawings/anchors.json`.

| Rule | Value |
|---|---|
| Wire shape | Horizontal and vertical segments only. Corner radius 2 px. |
| Leaving a pin | Perpendicular to the edge of the part the pin sits on. |
| Parallel wires | Side by side in lanes, 5 px clear between the edges of 2 wires. |
| Crossing | At a right angle. 2 wires of different numbers share no segment. |
| Width | 7 px for a wire that carries load current, 3 px for every other wire, 2 px for a lead of a component. |
| Load current | A wire of class `power` in the data file: 25 of 66 wires. The phase wires 48 to 51 and wire 52 are 3 px: the data file states no current for them. |
| Colour | Supply plus red, supply minus black with a light casing, signals in the other colours of `drawings/wires.ts`. No signal is red or black. |
| Wire number | In a chip at both ends of the wire. |
| Wires on 1 pin | Side by side along the edge of the part, up to 12 px from the wire point. |
| Cable | A wire on the net `pd_in` is a USB-C cable between 2 sockets: a grey sheath of 7 px around a red core of 3 px. The nets are the table `CABLE_NETS` in `drawings/wires.ts`. |
| Between | A part with `between` in the data file, with both named parts on its side of the IOmcu, makes that side 3 columns wide: the first named part alone in the outermost column, the part in the middle column, the second named part in the inner column. The T-plug stands between the PD charger and the PD mini. |
| Component | A wire of kind `component` in the data file is drawn as the component: a body of 12 px by 20 px on 2 leads with right-angle bends, 1 wire number, and a text of up to 2 lines. Between 2 pins of 1 part the body stands beside that part and the picture is 192 px wider for a text of 18 characters. The band is at end `a` of the wire. |
| Not drawn | A wire with 1 end (`b: null`), such as the address bridge 31 on the INA3221 board. Its table row carries the note. |

The legend below the picture names both widths ("power line", "signal") and
both supply colours, in English and German.

Controls: the buttons zoom in, zoom out and fit; the same on the keys `+`,
`-` and `0` with the focus on the picture box; the arrow keys move the
picture. A drag with the mouse or 1 finger moves the zoomed picture; at fit,
1 finger on the picture scrolls the page up and down. 2 fingers zoom, Ctrl and
the wheel zoom. Tab from the box reaches the wires; the arrow keys, Home and
End walk them; Enter or Space marks a wire and its table row. The wire button
of a table row marks the wire in the picture. The page does not scroll
sideways at 360 px width: the picture moves inside its box.

`test/diagram.test.ts` routes every distinct setup of the resolver sweep and
fails when a segment is not horizontal or vertical, 2 wires share a segment
or run closer than their lane gap, a load wire is not 7 px or a signal wire
is.

`src/diagram/export.ts` gives the picture inside the element subset of the
PDF painter: parts as outlines with their wire points and pin names, wires in
the widths and colours of the page. `test/diagram-export.test.ts` paints it
for every 97th setup of the export test sample and its 2 ends, in both
languages. The PDF export shows the schematic, not
the picture: scaled to the 510 pt text width of an A4 page, the 12 px text of
the picture measures 6.1 pt for the setup with the fewest wires and 2.0 pt
for the setup with the most (picture 880 px to 3104 px wide). A landscape
page gives at most 2.9 pt for the widest picture. The swap is 1 line in
`src/export/diagram.ts`.

## Workflow

`.github/workflows/pages.yml` runs on a pull request and on a push to `main`
that touch `configurator/`, the workflow file, or the panel's web help
(`help/`, `docs/img/`, `tools/help_site.py`).

| Job | Does |
|---|---|
| `core` | Type check, tests, data check and coverage figure of `core/`. |
| `build` | Fails when `src/stub` exists or `src/core.ts` does not import `../core/index.ts`. Type check, unit tests, drawings check, build, size check. Keeps `dist/` as artifact `configurator-dist` and 2 files of the PDF export as artifact `configurator-pdf-samples`, each for 14 days. |
| `smoke` | Playwright against the artifact of `build`: `e2e/smoke.spec.ts`, `e2e/select.spec.ts`, `e2e/export.spec.ts`, `e2e/diagram.spec.ts`. Keeps the screenshots of the wiring picture as artifact `configurator-diagram-shots` for 14 days. A failed test leaves its trace and screenshot as an artifact for 14 days. |
| `matrix` | Playwright against the artifact of `build` in 8 cells and on the hash corpus, in 4 shards. No retry. A failed test leaves its trace and screenshot as an artifact for 14 days. |
| `help` | Builds the panel's web help with `tools/help_site.py`, which runs every check of the help first. Keeps it as artifact `help-site` for 14 days. |
| `deploy` | Publishes the artifact to GitHub Pages, with `help-site` in its `help/` folder. Runs on a push to `main` only, after the other jobs pass. The size budget covers the configurator's files only. |

The `deploy` job needs the repository setting "Pages source: GitHub Actions".

## Pictures

A part of the data file names its pictures in `pictures`. `core/tools/check.ts`
holds each record to the files of `public/pictures/`.

| Kind | Files | A part card shows |
|---|---|---|
| `photo` | `<file>.webp` and `<file>-thumb.webp`, 480 px wide | the thumb, as a link to `<file>.webp` |
| `drawing` | `<file>.svg` | the drawing, as a link to the same file |

- A card shows at most 2 pictures, at most 320 px wide on screen and 40 mm wide in print.
- A part without a picture shows no image and no frame.
- A picture with `only` shows for the named option value: the Osoyoo breakout
  board, the INA228 module with XT90 plugs.
- The photos hold no Exif and no XMP metadata. The check fails on a file that does.
- No part uses the kind `drawing`.
- `test/pictures.test.ts` compares the alt texts of the 2 INA3221 photos with
  the header, the bridge and the pad names of the data file.
- The pictures are not part of the size budget. The initial load holds no
  picture: `npm run size` fails when `index.html` references one. Each image
  is `loading="lazy"`; the browser fetches it when its card comes near the
  viewport (Chromium: 1250 px, 2500 px on a slow connection). The smoke test
  fails at 360 px width when a picture lies in the first screen, when a
  picture more than 3000 px below it loads before a scroll, or when a full
  file loads without a tap.

Picture folder: 24 files, 1916.8 kB.

`test/pictures.test.ts` fails when this line differs from the folder.

## Part drawings

`public/drawings/` holds 1 drawing per part and board of the data file, and 2
drawings of boards outside it (INA219, ADXL345). `drawings/anchors.json` gives
the wire point of every pin in px of the drawing. The files are committed.
`npm run drawings:check` fails when they differ from the output of the
generator or from the data file. The wiring picture shows them. They
count against the 1 MB of the size budget.

Drawing folder: 48 files, 459.1 kB.

`drawings/test/cli.test.ts` fails when this line differs from the folder.

## PDF export

"Save as PDF" writes the resolved setup as a PDF 1.7 file in the browser and
starts its download. The page sends no request for it except the one for the
export chunk on the first use.

| Property | Value |
|---|---|
| File name | `rcbench-setup-<8 hex digits>-<en or de>.pdf`. The digits are a 32 bit FNV-1a hash of the selection and the "have it" ticks as the URL hash writes them, without the language. |
| Page | A4, 15 mm margins. Footer on every page: the sentence on product and company names of the page footer, without the link to `NOTICE`, and "Page x of y". |
| Sections | Title with firmware release, date, share link and the state of the project. Safety notes. Parts. What you connect and test. Parts to buy, with the disclaimer, what you have and what you bring. Power sources. Functions, enabled and not enabled. Wiring drawing and wire table. Build order. Parts outside a release, when the setup has one. |
| Text | Inter Regular and Bold, embedded as the subset of the glyphs the document uses. Text is selectable and searchable. |
| Drawing | The SVG of the page as vector paths and text, in the print colours, scaled to the text width and at most 1 page. |
| Wire table | The header row repeats on every page. No row is split. |
| Metadata | Title, document language, creator. |
| Links | The share link, written without the ids of the functions every setup runs and with a test object as the tick of its function. In the parts list, each maker page and each web search, as a link annotation over its line. |
| Output | The same bytes for the same setup, language, share link and date. |
| Size | 49 kB for the smallest setup (5 pages), 83 kB for the largest (12 pages). |

Parts of `src/export/`:

| File | Content |
|---|---|
| `button.ts` | State of the button and the status line. Part of the first load. |
| `setup-pdf.ts` | Entry of the export chunk: style sheet text, font files, `buildPdf`. |
| `model.ts` | Setup to ordered sections and table rows. |
| `layout.ts` | Sections to pages: line breaks, page breaks, footer. |
| `diagram.ts` | The wiring drawing as SVG text. The one place that names the drawing module. |
| `svg.ts`, `css.ts` | SVG to PDF operators, with the rules of `src/style.css` and its print colours. |
| `font.ts`, `pdf.ts` | TrueType reader and PDF writer. |
| `font/` | `Inter-Regular-subset.ttf` (22488 bytes), `Inter-Bold-subset.ttf` (22620 bytes), `OFL.txt`. |

The export chunk is 113.98 kB (52.34 kB gzip). The first load carries the button,
its strings and the chunk loader: 3.2 kB (1.1 kB gzip).

The font is Inter 4.001 by The Inter Project Authors under the SIL Open Font
License 1.1, text in `src/export/font/OFL.txt`. `tools/font-subset.py` writes
both files from the variable font: 282 characters each.

The SVG painter supports the elements and attributes the drawing uses, listed
at the top of `src/export/svg.ts`. Any other element or attribute stops the
export with the error message of the page; no drawing is written with a part
left out.

The build order comes from the stages of the data file: each stage with its
title and its wire numbers, then every check of the setup. A check of the data
file names no stage, so the checks follow the stages as one list.

## Limits

- The page draws 1 servo and 1 ESC.
- Motor simulator: the drawing shows delta and no resistance; its resistors carry no colour bands. In star the
  pin labels still read "corner A" to "corner C".
- Motor simulator: the current limit of the ESC, the tolerance of the resistance and the temperature of a
  resistor are not modelled.
- Servo power from the bench: the current limit of pad 40 is not stated. The output currents of the
  Osoyoo breakout board are vendor values (2 A at 5 V, 500 mA at 3.3 V), not measured.
  The V pins of all 40 strips follow the jumper V Selector; with USB alone and the jumper on 3V3 the schematic and
  the product page disagree.
- DC supply of the Osoyoo breakout board: the DC jack (plug 5.5 x 2.1 mm, centre positive) has no reverse-polarity
  protection. A centre-negative plug reverses the input of the XL1509 converter. The SI2302 protects the VIN terminal
  and H4 only.
- DC jack of the Osoyoo breakout board: the product page names the socket DC005-2.1. The 2.1 mm pin follows from that
  name. The 5.5 mm outer diameter is the usual size of that socket family and is not stated by the vendor.
- Osoyoo breakout board above 12 V at any input: the gate of the SI2302 (Q3) sits at PWRIN through R7, and its
  gate-source voltage exceeds the ±12 V rating of its datasheet. The print of 16 V and the vendor limit of 18 V are
  above that.
- What powers an ESC during programming other than ESC STICK is not stated.
- 8 of 20 parts have a picture. MATEK board, servo, ESC, PD charger, microSD card, motor simulator, phase tap,
  DC supply of the Osoyoo breakout board and cables: no picture.
- Photo `02-pd-mini`: the left third is out of focus.
- The schematic has no zoom control. Below 1024 px width it scrolls sideways in its box.
- Wiring picture: wire 31 ends on no wire point and is not drawn; the table lists it.
- Wiring picture: a wire number can lie over the bent lead of a component.
- Wiring picture: the pinch gesture is tested through synthetic touch events only, not on a phone.
- Wiring picture: the colours are not checked against a colour-vision simulation. Width, wire number
  and table carry the same facts.
- Wiring picture in the PDF export: not shown. See "Wiring picture".
- 16 wires use a lane that differs from the `route` in the data file: wires 10,
  14, 21, 22, 28, 29, 30, 45, 46, 47, 51, 56, 57, 58, 59 and 60. The table is `GEO.laneFix` in
  `src/draw/layout.ts`. With the lanes of the data file, wires of different
  nets share a line or a pill covers a wire of another pin. A lane changed in
  the data file for one of these wires has no effect on the drawing.
- The page does not show the pin defaults of the data file (`pinDefaults`).
- Tested in headless Chromium only. Firefox and Safari: not tested.
- Print output: not checked on paper.
- PDF export: the first use needs the network for the export chunk of 113.98 kB. Later uses work offline
  until the page reloads.
- PDF export: the file has no structure tags (not PDF/UA). A screen reader reads the text in page order
  and does not announce headings or table cells.
- PDF export and page: the section "Parts" lists every part once; a count is part of the part name. The
  quantity field `qty` belongs to the rows of "Parts to buy".
- Parts to buy: a "have it" tick that leaves the hash because its row stopped being a row to buy does not
  come back when the row returns. Example: `h=ina228.matek`, then the variant XT90 module, then the variant
  Matek board again: the Matek row stands among the rows to buy without a tick.
- Parts to buy: the maker pages answered with HTTP 200 on 2026-10-13; the page does not check them at run
  time. The PD mini page is the maker's GitHub repository. The DAOKAI INA3221 board and the XT90 INA228
  module have no maker page. The maker of the XT90 module is not known.
- Parts to buy: the Matek page names the I2C-INA-BM with an INA228 or an INA238; the bench refuses the
  INA238. The check line names the chip to look for.
- Parts to buy: what a web search returns is not checked. A search for a spec quotes each term and finds
  only pages that hold every term.
- PDF export: the drawing is scaled by 0.526 to the text width. Its text is 5.8 pt to 6.8 pt. The wire table
  after it repeats every wire at 8 pt.
- PDF export: the page before a drawing that needs a full page ends where the drawing no longer fits.
- PDF export: download tested in headless Chromium only. Safari on iOS and Chrome on Android: not tested.
  The status line keeps a link to the file for a browser that blocks the automatic download.
- PDF export: a character outside the 282 of the font files stops the export with the error message.
