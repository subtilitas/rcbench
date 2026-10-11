# rcbench bench configurator

A static page for release 0.16.0 of rcbench. The visitor ticks what the bench
does. The page lists the parts, draws the wiring and prints a pin-to-pin table.
The functions stand in groups by the parts they need. A part of the result has
a remove control.
Without a tick it shows the base setup: panel, coprocessor and the 4 wires
between them.

- No server, no account, no storage. The selection lives in the URL hash.
- No run-time dependency. The page loads 3 files from its own origin and sends
  no request to another origin. A picture loads from the same origin when its
  part card comes near the viewport.
- English and German. The start language follows the browser.
- Light and dark theme follow the system setting. Print gives an A4 document.
- The page is served under the base path `/rcbench/`.

## Layout

| Path | Content |
|---|---|
| `contract.ts` | Types shared by the core and the page. |
| `core/` | Data file, resolver and checks. See `core/README.md`. |
| `src/core.ts` | The one import seam to the core: catalogue, resolver, default options, groups, removal. |
| `src/main.ts` | State, events, rendering. |
| `src/hash.ts` | URL hash codec. |
| `src/i18n.ts` | Chrome strings in English and German. |
| `src/names.ts` | Names of wire ends for the table and the drawing. |
| `src/draw/layout.ts` | Geometry of the drawing: modules, pads, lanes, wires, pills. |
| `src/draw/svg.ts` | SVG (Scalable Vector Graphics) markup of the drawing. |
| `src/view/` | Chooser, result document, table. |
| `src/export/` | PDF export of the resolved setup. See "PDF export". |
| `public/pictures/` | Pictures of the parts: photos as WebP, drawings as SVG. |
| `test/` | Unit tests, run by `node --test`. |
| `test/property/` | Properties of the hash codec, the markup and the texts over the whole selection space. |
| `test/corpus/` | Stored links with the setup each one gives. See `test/corpus/README.md`. |
| `e2e/smoke.spec.ts` | Playwright smoke test against `vite preview`. |
| `e2e/matrix/` | Playwright tests of 15 setups in 8 cells of viewport, language and theme, with the PDF export and the removal of a part, and on every stored link. |
| `e2e/lib/` | Helpers for Playwright tests. See `e2e/lib/README.md`. |
| `e2e/select.spec.ts` | Playwright test of the groups, the state words and the removal of a part. |
| `e2e/export.spec.ts` | Playwright test of the PDF export against `vite preview`. |
| `tools/size.ts` | Size check of the build. |
| `tools/shots.ts` | Screenshots of the build. |
| `tools/pdf-samples.ts` | 2 sample files of the PDF export. |
| `tools/font-subset.py` | Writes the 2 font files of the PDF export. |

## Commands

Node.js 24 is required.

```bash
cd configurator
npm ci
npm run typecheck   # tsc --noEmit
npm test            # unit tests
npm run build       # writes dist/
npm run size        # initial load at most 250 kB, dist/ without pictures at most 1 MB
npx playwright install chromium
npm run e2e         # smoke, chooser and export tests at 360 px and 1280 px width
npm run e2e:matrix  # matrix and corpus tests
npm run dev         # http://localhost:5173/rcbench/
node tools/shots.ts shots   # 24 PNG files: 2 widths, 2 themes, 2 languages, 3 selections
node tools/pdf-samples.ts pdf-samples   # 2 PDF files: fewest wires in English, most wires in German
```

## URL hash

`#v=1&f=<ids>&p=<ids>&o=<tokens>&l=<lang>`

| Key | Meaning |
|---|---|
| `v` | Format version, `1`. Another value gives the base setup and a notice. |
| `f` | Ids of the functions the visitor ticked, comma separated. A function that the parts of the setup bring is not stored. A function that needs no part has no tick; its id in a link is dropped without a notice. |
| `p` | Ids of parts the visitor owns. |
| `o` | Option tokens that differ from the default: `osoyoo`, `xt90`, `pd`. |
| `l` | `en` or `de`. Written when it differs from the browser language. |

Limits: 2000 characters per hash, 64 ids per key, 32 characters per id of
lowercase letters and digits. A longer hash gives the base setup and the
version notice. An unknown id is dropped and named in a notice.

## Chooser

Each group of functions is a `fieldset`. Its legend names the parts the group
needs beyond the base setup: "Needs: Servo + PD mini". A group whose parts are
all in the setup carries the tag "In this setup".

The first group is "Runs on the base setup". It holds the functions that need
no part. It is a list without controls: a row has no tick, is active in every
setup and reads "Included with the base setup.". The id of such a function is
never written into the link and never counts as selected by the visitor. The
removal of a part and of a tick never lists such a function as going off. The
group is absent when every function needs a part.

A function is active when the setup holds every part it needs and every option
it forces. Each row states its state in words:

| State | Text | Tick |
|---|---|---|
| Ticked by the visitor | "Selected by you." | yes |
| Active through the parts of the setup | "Included with these parts." | no |
| Runs on the base setup | "Included with the base setup." | none |
| Not active | "Adds: <parts>" or the reason | no |

Removing the tick of a function removes it from the selection. A note under
the row states whether the function stays active and which of its parts stay
or leave.

Every part of the result beyond the base setup has a button "Remove". "Parts
you own" removes a part by its tick. Every removal opens a question under the
control; no part leaves without the confirming answer. The question lists the
functions that go off with the state of each, the other parts that leave or
come in and the option that changes. For a part that takes nothing with it, it
states "No function goes off and no other part leaves." Its 2 answers are
"Remove part and these functions", or "Remove part" when no function goes off,
and "Keep". The focus moves to "Keep". "Keep" and the Escape key return the
focus to the control. The question has the role `alertdialog` and does not
trap the focus.

Parts of a removed function that stay in the setup become ticks under "Parts
you own".

## Drawing

The drawing is 970 px wide. Its height follows the modules of the setup.
Modules stand in a left and a right column, the coprocessor in the middle.
Every wire runs in right angles through numbered lanes and carries its wire
number in a pill at each module end.

`test/layout.test.ts` draws every distinct setup the resolver gives for every
subset of the functions that need a part and every subset of the selectable parts, in all 8
option combinations and both languages. It fails when:

- a wire has no path, leaves the canvas or runs outside a lane gutter,
- two wires of different nets share a line or run closer than 5 px,
- a pill covers a wire of another pin or another pill,
- a module text is wider than its module,
- a wire number names different ends in two selections.

## Workflow

`.github/workflows/pages.yml` runs on a pull request and on a push to `main`
that touch `configurator/` or the workflow file.

| Job | Does |
|---|---|
| `core` | Type check, tests, data check and coverage figure of `core/`. |
| `build` | Fails when `src/stub` exists or `src/core.ts` does not import `../core/index.ts`. Type check, unit tests, build, size check. Keeps `dist/` as artifact `configurator-dist` and 2 files of the PDF export as artifact `configurator-pdf-samples`, each for 14 days. |
| `smoke` | Playwright against the artifact of `build`: `e2e/smoke.spec.ts`, `e2e/select.spec.ts`, `e2e/export.spec.ts`. A failed test leaves its trace and screenshot as an artifact for 14 days. |
| `matrix` | Playwright against the artifact of `build` in 8 cells and on the hash corpus, in 4 shards. No retry. A failed test leaves its trace and screenshot as an artifact for 14 days. |
| `deploy` | Publishes the artifact to GitHub Pages. Runs on a push to `main` only, after the other jobs pass. |

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

## PDF export

"Save as PDF" writes the resolved setup as a PDF 1.7 file in the browser and
starts its download. The page sends no request for it except the one for the
export chunk on the first use.

| Property | Value |
|---|---|
| File name | `rcbench-setup-<8 hex digits>-<en or de>.pdf`. The digits are a 32 bit FNV-1a hash of the selection as the URL hash writes it, without the language. |
| Page | A4, 15 mm margins. Footer on every page: trademark notice, "Page x of y". |
| Sections | Title with firmware release, date and share link. Safety notes. Parts. Functions, enabled and not enabled. Wiring drawing and wire table. Build order. Parts outside a release, when the setup has one. |
| Text | Inter Regular and Bold, embedded as the subset of the glyphs the document uses. Text is selectable and searchable. |
| Drawing | The SVG of the page as vector paths and text, in the print colours, scaled to the text width and at most 1 page. |
| Wire table | The header row repeats on every page. No row is split. |
| Metadata | Title, document language, creator. Links: the share link, written without the ids of the functions that need no part. |
| Output | The same bytes for the same setup, language, share link and date. |
| Size | 44 kB for the smallest setup (4 pages), 64 kB for the largest (8 pages). |

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

The export chunk is 108 kB (51 kB gzip). The first load carries the button,
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

- The page draws 1 servo and 1 ESC (electronic speed controller).
- 8 of 19 parts have a picture. MATEK board, servo, ESC, PD charger, microSD card, motor simulator, phase tap
  and cables: no picture.
- Photo `02-pd-mini`: the left third is out of focus.
- The drawing has no zoom control. Below 1024 px width it scrolls sideways in its box.
- 14 wires use a lane that differs from the `route` in the data file: wires 10,
  14, 21, 22, 28, 29, 30, 45, 46, 47, 51, 56, 57 and 58. The table is `GEO.laneFix` in
  `src/draw/layout.ts`. With the lanes of the data file, wires of different
  nets share a line or a pill covers a wire of another pin. A lane changed in
  the data file for one of these wires has no effect on the drawing.
- The page does not show the pin defaults of the data file (`pinDefaults`).
- Tested in headless Chromium only. Firefox and Safari: not tested.
- Below 1024 px width the Tab key puts the focus on a control that lies
  entirely under the bottom bar (56 px) when the control is in the lowest
  56 px of the viewport. At 360 x 740 px this is the checkbox of the 7th
  function. The tests are in `e2e/matrix/keyboard.spec.ts`, marked `fixme`.
- Print output: not checked on paper.
- PDF export: the first use needs the network for the export chunk of 108 kB. Later uses work offline
  until the page reloads.
- PDF export: the file has no structure tags (not PDF/UA). A screen reader reads the text in page order
  and does not announce headings or table cells.
- PDF export: every part is listed once. The data file holds no quantity field; a count is part of the
  part name.
- PDF export: the drawing is scaled by 0.526 to the text width. Its text is 5.8 pt to 6.8 pt. The wire table
  after it repeats every wire at 8 pt.
- PDF export: the page before a drawing that needs a full page ends where the drawing no longer fits.
- PDF export: download tested in headless Chromium only. Safari on iOS and Chrome on Android: not tested.
  The status line keeps a link to the file for a browser that blocks the automatic download.
- PDF export: a character outside the 282 of the font files stops the export with the error message.
