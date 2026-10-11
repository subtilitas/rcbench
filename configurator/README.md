# rcbench bench configurator

A static page for release 0.16.0 of rcbench. The visitor ticks what the bench
does. The page lists the parts, draws the wiring and prints a pin-to-pin table.
Without a tick it shows the base setup: panel, coprocessor and the 4 wires
between them.

- No server, no account, no storage. The selection lives in the URL hash.
- No run-time dependency. The page loads 3 files from its own origin and sends
  no request to another origin.
- English and German. The start language follows the browser.
- Light and dark theme follow the system setting. Print gives an A4 document.
- The page is served under the base path `/rcbench/`.

## Layout

| Path | Content |
|---|---|
| `contract.ts` | Types shared by the core and the page. |
| `core/` | Data file, resolver and checks. See `core/README.md`. |
| `src/core.ts` | The one import seam to the core: catalogue, resolver, default options. |
| `src/main.ts` | State, events, rendering. |
| `src/hash.ts` | URL hash codec. |
| `src/i18n.ts` | Chrome strings in English and German. |
| `src/names.ts` | Names of wire ends for the table and the drawing. |
| `src/draw/layout.ts` | Geometry of the drawing: modules, pads, lanes, wires, pills. |
| `src/draw/svg.ts` | SVG (Scalable Vector Graphics) markup of the drawing. |
| `src/view/` | Chooser, result document, table. |
| `test/` | Unit tests, run by `node --test`. |
| `e2e/smoke.spec.ts` | Playwright smoke test against `vite preview`. |
| `tools/size.ts` | Size check of the build. |
| `tools/shots.ts` | Screenshots of the build. |

## Commands

Node.js 24 is required.

```bash
cd configurator
npm ci
npm run typecheck   # tsc --noEmit
npm test            # unit tests
npm run build       # writes dist/
npm run size        # initial load at most 250 kB, dist/ at most 1 MB
npx playwright install chromium
npm run e2e         # smoke test at 360 px and 1280 px width
npm run dev         # http://localhost:5173/rcbench/
node tools/shots.ts shots   # 24 PNG files: 2 widths, 2 themes, 2 languages, 3 selections
```

## URL hash

`#v=1&f=<ids>&p=<ids>&o=<tokens>&l=<lang>`

| Key | Meaning |
|---|---|
| `v` | Format version, `1`. Another value gives the base setup and a notice. |
| `f` | Function ids, comma separated. |
| `p` | Ids of parts the visitor owns. |
| `o` | Option tokens that differ from the default: `osoyoo`, `xt90`, `pd`. |
| `l` | `en` or `de`. Written when it differs from the browser language. |

Limits: 2000 characters per hash, 64 ids per key, 32 characters per id of
lowercase letters and digits. A longer hash gives the base setup and the
version notice. An unknown id is dropped and named in a notice.

## Drawing

The drawing is 970 px wide. Its height follows the modules of the setup.
Modules stand in a left and a right column, the coprocessor in the middle.
Every wire runs in right angles through numbered lanes and carries its wire
number in a pill at each module end.

`test/layout.test.ts` draws every distinct setup the resolver gives for every
subset of the functions and every subset of the selectable parts, in all 8
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
| `build` | Fails when `src/stub` exists or `src/core.ts` does not import `../core/index.ts`. Type check, unit tests, build, size check. Keeps `dist/` as artifact `configurator-dist` for 14 days. |
| `smoke` | Playwright against the artifact of `build`. |
| `deploy` | Publishes the artifact to GitHub Pages. Runs on a push to `main` only, after the 3 other jobs pass. |

The `deploy` job needs the repository setting "Pages source: GitHub Actions".

## Limits

- The page draws 1 servo and 1 ESC (electronic speed controller).
- Part cards show a hatched placeholder. The repository holds no part photos.
- The drawing has no zoom control. Below 1024 px width it scrolls sideways in its box.
- 11 wires use a lane that differs from the `route` in the data file: wires 10,
  14, 21, 22, 28, 29, 30, 45, 46, 47 and 51. The table is `GEO.laneFix` in
  `src/draw/layout.ts`. With the lanes of the data file, wires of different
  nets share a line or a pill covers a wire of another pin. A lane changed in
  the data file for one of these wires has no effect on the drawing.
- The page does not show the pin defaults of the data file (`pinDefaults`).
- Tested in headless Chromium only. Firefox and Safari: not tested.
- Print output: not checked on paper.
