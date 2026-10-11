import { defineConfig } from '@playwright/test';

// One browser, one file. The server is `vite preview` on the built page.
export default defineConfig({
  testDir: 'e2e',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  reporter: 'list',
  use: {
    baseURL: 'http://localhost:4173/rcbench/',
    browserName: 'chromium',
  },
  webServer: {
    command: `"${process.execPath}" node_modules/vite/bin/vite.js preview`,
    url: 'http://localhost:4173/rcbench/',
    reuseExistingServer: !process.env.CI,
    timeout: 30_000,
  },
});
