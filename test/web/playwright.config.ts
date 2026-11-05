import { defineConfig } from '@playwright/test';

// The Wi-Fi page's file manager against test/host/wa_mock_server.py, which
// serves the page as the panel builds it (test/host/wa_page_dump.c) and
// answers the file API as the panel does.  One browser, one worker: the mock
// holds one card and the panel answers one request at a time.
//
// WA_PAGE_DUMP names the wa_page_dump binary of a host build
// (cmake --build test/host/build --target wa_page_dump).  E2E_PORT names
// another port, for a machine where 4180 is taken.

const port = Number(process.env.E2E_PORT ?? 4180);
const dump = process.env.WA_PAGE_DUMP ?? '../host/build/wa_page_dump';
const base = `http://127.0.0.1:${port}`;

export default defineConfig({
  testDir: '.',
  testMatch: '*.spec.ts',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  forbidOnly: !!process.env.CI,
  reporter: process.env.CI ? [['list'], ['github']] : 'list',
  use: {
    baseURL: base,
    browserName: 'chromium',
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  webServer: {
    command: `python3 ../host/wa_mock_server.py --dump "${dump}" --port ${port}`,
    url: `${base}/__mock/ready`,
    reuseExistingServer: !process.env.CI,
    timeout: 30_000,
  },
});
