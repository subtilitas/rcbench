import { defineConfig } from '@playwright/test';
import type { Project } from '@playwright/test';

// One browser. The server is `vite preview` on the built page, on port 4173.
// E2E_PORT names another port, for a machine where 4173 is taken.
//
// smoke    e2e/smoke.spec.ts, as a visitor with default settings.
// corpus   e2e/matrix/corpus.spec.ts, each stored link once.
// matrix   the other specs of e2e/matrix in 8 cells: phone (360 x 740 px) and
//          desktop (1280 x 800 px), English and German, light and dark, each
//          with reduced motion.

const port = Number(process.env.E2E_PORT ?? 4173);
const base = `http://localhost:${port}/rcbench/`;

const matrix: Project[] = [];
for (const phone of [true, false]) {
  for (const lang of ['en', 'de'] as const) {
    for (const theme of ['light', 'dark'] as const) {
      matrix.push({
        name: `matrix-${phone ? 'phone' : 'desktop'}-${lang}-${theme}`,
        testMatch: 'matrix/*.spec.ts',
        testIgnore: 'matrix/corpus.spec.ts',
        metadata: { phone, lang, theme },
        use: {
          viewport: phone ? { width: 360, height: 740 } : { width: 1280, height: 800 },
          hasTouch: phone,
          locale: lang === 'de' ? 'de-DE' : 'en-GB',
          colorScheme: theme,
          reducedMotion: 'reduce',
        },
      });
    }
  }
}

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
    { name: 'smoke', testMatch: 'smoke.spec.ts', fullyParallel: false },
    { name: 'corpus', testMatch: 'matrix/corpus.spec.ts', use: { locale: 'en-GB', reducedMotion: 'reduce' } },
    ...matrix,
  ],
  webServer: {
    command: `"${process.execPath}" node_modules/vite/bin/vite.js preview --port ${port} --strictPort`,
    url: base,
    reuseExistingServer: !process.env.CI,
    timeout: 30_000,
  },
});
