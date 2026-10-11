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
  await expectSetup(page, selection);        // ticks, part cards, remove controls, table rows, drawn wires
  await expectNoHorizontalScroll(page);
  expectQuiet(seen);                         // no foreign request, no console error or warning, no failed request
});
```

## Files

| File | Exports | Does |
|---|---|---|
| `setup.ts` | `SEL` | The selectors of the page. A change of the markup is made here once. |
| | `FUNCTIONS`, `OWNED` | The function ids and the ids of the parts under "Parts you own", from the catalogue. |
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
| `keyboard.ts` | `keyboardWalk(page, { back? })` | Presses Tab from the top of the document until the focus leaves it. Records for each stop: element, role and name from the accessibility tree of the browser, `:focus-visible`, outline width and outline contrast, position against the viewport and the fixed bar. With `back`, walks the other way with Shift+Tab. |
| | `walkProblems(walk)` | One line per finding: a control the Tab key misses, an order other than the document, a positive `tabindex`, no name, no role, no `:focus-visible`, an outline under 2 px or under 3:1. |
| | `hiddenStops(walk)` | The stops that lie outside the viewport or entirely under a fixed bar while they have the focus. |
| `axe.ts` | `axeOf(page)` | Runs axe-core at the tags `wcag2a`, `wcag2aa`, `wcag21a`, `wcag21aa`, `wcag22aa`. Gives one line per violated rule and the count of passed rules. |
| `contrast.ts` | `contrastOf(page)` | Measures each distinct pair of colours the page paints: text and SVG text against the background, wires and box outlines of the drawings, borders of buttons and text fields. |
| | `contrastProblems(contrast)` | One line per pair under its minimum: 4.5:1 for text, 3:1 for text of at least 24 px or bold text of at least 18.66 px, 3:1 for wires, outlines and borders. A colour the measurement cannot read is a finding. |

## Rules of use

- `readSetup` gives every id list in catalogue order and the wire numbers
  ascending. The order on the page is not part of what it reads.
- `readSetup` finds a part card by the name on it. A card whose name is no
  part name of the catalogue comes back as `?<name>`.
- `keyboardWalk` leaves the attribute `data-walk` on the controls of the page
  and moves the focus and the scroll position. Call it last, or load the page
  again.
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
