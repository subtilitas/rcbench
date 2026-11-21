import { defineConfig } from '@playwright/test';

// One browser, Chromium: the page runs in Chrome and Edge only. The server is
// `vite preview` on the built page, on port 4174 (E2E_PORT names another).
// Web Serial and WebUSB are fakes (e2e/fake-devices.js); the firmware folder
// fw/ is served by the tests from synthetic images (test/fixtures.ts).
//
// desktop  1280 x 800px, English
// phone    360 x 740px, German, dark

const port = Number(process.env.E2E_PORT ?? 4174);
const base = `http://localhost:${port}/rcbench/flash/`;

export default defineConfig({
  testDir: 'e2e',
  fullyParallel: true,
  workers: 4,
  retries: 0,
  forbidOnly: !!process.env.CI,
  reporter: process.env.CI ? [['list'], ['github']] : 'list',
  use: {
    baseURL: base,
    browserName: 'chromium',
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: [
    { name: 'desktop', use: { viewport: { width: 1280, height: 800 }, locale: 'en-GB', colorScheme: 'light' } },
    { name: 'phone', use: { viewport: { width: 360, height: 740 }, locale: 'de-DE', colorScheme: 'dark', hasTouch: true } },
  ],
  webServer: {
    command: `"${process.execPath}" node_modules/vite/bin/vite.js preview --port ${port} --strictPort`,
    url: base,
    reuseExistingServer: !process.env.CI,
    timeout: 30_000,
  },
});
