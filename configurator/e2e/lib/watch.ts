// Records what a page does that it must not: a request to another origin, a
// console error or warning, a page error, a failed request.

import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';

export type Watch = {
  /** Requests and WebSockets to another origin than the page. */
  foreign: string[];
  /** Console messages of type "error" and "warning", and uncaught errors, each with its kind in front. */
  console: string[];
  /** Requests that fail, and responses with a status of 400 or more. */
  failed: string[];
  /** The count of requests to the origin of the page. */
  local: number;
};

export const QUIET = { foreign: [], console: [], failed: [] };

/** The origin of `baseURL` of the running test. */
export function pageOrigin(): string {
  return new URL(test.info().project.use.baseURL ?? 'http://localhost:4173').origin;
}

/**
 * Starts the record. Call it before the page loads. `origin` is the origin of
 * the page under test; the default is the origin of `baseURL`.
 */
export function watchPage(page: Page, origin = pageOrigin()): Watch {
  const seen: Watch = { foreign: [], console: [], failed: [], local: 0 };
  const local = (address: string): boolean => {
    const url = new URL(address);
    // data: and blob: carry their content, about: loads nothing.
    return url.protocol === 'data:' || url.protocol === 'blob:' || url.protocol === 'about:' || url.origin === origin;
  };
  page.on('request', (r) => {
    if (local(r.url())) seen.local += 1;
    else seen.foreign.push(`${r.method()} ${r.url()}`);
  });
  page.on('websocket', (w) => {
    if (!local(w.url().replace(/^ws/, 'http'))) seen.foreign.push(`WebSocket ${w.url()}`);
  });
  page.on('requestfailed', (r) => seen.failed.push(`${r.url()} ${r.failure()?.errorText ?? ''}`.trim()));
  page.on('response', (r) => {
    if (r.status() >= 400) seen.failed.push(`${r.status()} ${r.url()}`);
  });
  page.on('pageerror', (e) => seen.console.push(`uncaught: ${String(e)}`));
  page.on('console', (m) => {
    if (m.type() === 'error' || m.type() === 'warning') seen.console.push(`${m.type()}: ${m.text()}`);
  });
  return seen;
}

/**
 * The short record: every request to another origin, every uncaught error and
 * every console message of type "error". A test compares it with
 * `{ foreign: [], errors: [] }`.
 */
export function watchErrors(page: Page, origin = pageOrigin()): { foreign: string[]; errors: string[] } {
  const seen: { foreign: string[]; errors: string[] } = { foreign: [], errors: [] };
  page.on('request', (r) => {
    const url = new URL(r.url());
    if (url.protocol !== 'data:' && url.origin !== origin) seen.foreign.push(r.url());
  });
  page.on('pageerror', (e) => seen.errors.push(String(e)));
  page.on('console', (m) => {
    if (m.type() === 'error') seen.errors.push(m.text());
  });
  return seen;
}

/** Asserts that the record holds no foreign request, no console error or warning and no failed request. */
export function expectQuiet(seen: Watch): void {
  expect({ foreign: seen.foreign, console: seen.console, failed: seen.failed }).toEqual(QUIET);
}

/** Asserts that the document is not wider than the viewport, so the page does not scroll sideways. */
export async function expectNoHorizontalScroll(page: Page): Promise<void> {
  const size = await page.evaluate(() => ({
    document: document.documentElement.scrollWidth,
    body: document.body.scrollWidth,
    viewport: document.documentElement.clientWidth,
  }));
  expect(size.document, `the document is ${size.document} px wide in a viewport of ${size.viewport} px`).toBeLessThanOrEqual(size.viewport);
  expect(size.body, `the body is ${size.body} px wide in a viewport of ${size.viewport} px`).toBeLessThanOrEqual(size.viewport);
  // The page does not move sideways when asked to.
  const moved = await page.evaluate(() => {
    const before = window.scrollX;
    window.scrollTo(10_000, window.scrollY);
    const after = window.scrollX;
    window.scrollTo(before, window.scrollY);
    return after;
  });
  expect(moved, 'the page scrolls sideways').toBe(0);
}
