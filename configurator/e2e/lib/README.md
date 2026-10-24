# Helpers for Playwright tests

Shared by the specs in `e2e/` and under `e2e/matrix/`. A spec imports
them with a relative path and the `.ts` ending:

```ts
import { SEL, expectSetup, openSetup, selectionOf } from '../lib/setup.ts';
import { expectNoHorizontalScroll, expectQuiet, watchPage } from '../lib/watch.ts';

test('the servo setup', async ({ page }) => {
  const seen = watchPage(page);              // before the page loads
  const selection = selectionOf({ functions: ['servo'], options: { escPower: 'pd' } });
  await openSetup(page, selection);
  await expectSetup(page, selection);        // ticks, part cards, remove controls, variant groups, table rows, drawn wires
  await expectNoHorizontalScroll(page);
  expectQuiet(seen);                         // no foreign request, no console error or warning, no failed request
});
```

## Files

| File | Exports | Does |
|---|---|---|
| `setup.ts` | `SEL` | The selectors of the page. A change of the markup is made here once. |
| | `FUNCTIONS`, `BASE_FUNCTIONS`, `OWNED` | The ids of the functions with a tick, of the functions that need no part, and of the parts under "Parts you own", from the catalogue. |
| | `selectionOf(part)` | A selection from `functions`, `owned` and `options`. What is left out is empty or the default. |
| | `hashOf(selection, lang?)` | The hash the page writes for a selection, with its number sign. With `lang`, the hash ends in `&l=<lang>`. |
| | `openHash(page, hash)`, `openSetup(page, selection, lang?)` | Loads the page from `about:blank` and waits for the drawing. |
| | `readSetup(page)` | The setup the page shows: language, hash, ticked functions, ticked parts, parts with a remove control, options, part cards as ids, wire numbers, count of drawn wires, notices, the note after a function leaves the selection, summary line. |
| | `expectedOf(selection)`, `expectSetup(page, selection)` | What the resolver gives for a selection, and the assertion that the page shows it. |
| | `stateOf(shown)` | A read setup without hash, language and texts: what a change of language keeps. |
| `watch.ts` | `watchPage(page)` | Records requests and WebSockets to another origin, console errors and warnings, uncaught errors, failed requests and responses with status 400 or more. |
| | `expectQuiet(seen)` | Asserts that the record is empty. |
| | `watchErrors(page)` | The short record `{ foreign, errors }` without console warnings and failed requests. |
| | `expectNoHorizontalScroll(page)` | Asserts that document and body are at most as wide as the viewport and that the page does not move sideways. |
| | `pageOrigin()` | The origin of `baseURL` of the running test. |
| `keyboard.ts` | `keyboardWalk(page, { back? })` | Presses Tab from the top of the document until the focus leaves it. Records for each stop: element, role and name from the accessibility tree of the browser, `:focus-visible`, outline width, the contrast of the outline colour blended over the background behind it, position against the viewport and the fixed bar. With `back`, walks the other way with Shift+Tab. |
| | `walkProblems(walk)` | One line per finding: a control the Tab key misses, an order other than the document, a positive `tabindex`, no name, no role, no `:focus-visible`, an outline under 2px or under 3:1. |
| | `hiddenStops(walk)` | The stops that lie outside the viewport or entirely under a fixed bar while they have the focus. |
| `axe.ts` | `axeOf(page)` | Runs axe-core at the tags `wcag2a`, `wcag2aa`, `wcag21a`, `wcag21aa`, `wcag22aa`. Gives one line per violated rule and the count of passed rules. |
| `contrast.ts` | `contrastOf(page)` | Measures each distinct pair of colours the page paints: text and SVG text against the background, wires and box outlines of the drawings, borders of buttons and text fields. A wire of the wiring picture, of its legend or of a colour sample counts with the colour of its core or of its casing, whichever stands out more. The plate under a wire number of the picture is not measured as an outline. The part drawings are images and are not measured: `npm run drawings:check` holds their colours. |
| | `contrastProblems(contrast)` | One line per pair under its minimum: 4.5:1 for text, 3:1 for text of at least 24px or bold text of at least 18.66px, 3:1 for wires, outlines and borders. A ratio is compared as measured and rounded to 3 decimals in the message alone. A colour the measurement cannot read is a finding. |
| `lib.spec.ts` | | Holds `contrast.ts`, `keyboard.ts` and `expectSetup` to documents with a known answer: text at 4.49999:1 and at 2.99995:1, 2 colours that differ by a fraction of a step, a focus outline at 5% opacity and one at 2.99:1, a page without a variant group. Runs as the project `lib` under `npm run e2e:matrix`. |

## Rules of use

- `readSetup` gives every id list in catalogue order and the wire numbers
  ascending. The order on the page is not part of what it reads.
- `readSetup` finds a part card by the name on it. A card whose name is no
  part name of the catalogue comes back as `?<name>`.
- `keyboardWalk` leaves the attribute `data-walk` on the controls of the page
  and moves the focus and the scroll position. Call it last, or load the page
  again.
- `keyboardWalk` reads the position of a stop directly after the key. A page
  that scrolls smoothly is walked with `reducedMotion: 'reduce'`.
- `contrastOf` scrolls the page and measures what is shown: open a `details`
  element first when its content counts.
- `contrastOf` reads plain background colours. A background image or a
  gradient is reported as not measured.
- The server port is 4173. `E2E_PORT=<port>` moves `vite preview` and
  `baseURL` to another port.

## Limits

- Chromium only. `keyboardWalk` reads role and name over the Chrome DevTools
  Protocol.
- `contrastOf` does not measure the native tick of a checkbox or a radio
  button, and no state other than the one shown (hover, pressed).
- `hiddenStops` treats every element with `position: fixed` that does not
  hold the focused control as a bar at the bottom.
