// Screenshots of the built page for a look by eye. Run after `npm run build`:
//
//   node tools/shots.ts <output directory>
//
// Writes one full-page PNG per width (360 px, 1280 px), theme (light, dark),
// language (en, de) and selection (base, servo, full): 24 files.

import { mkdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { chromium } from '@playwright/test';
import { preview } from 'vite';
import { CATALOGUE } from '../src/core.ts';

const out = resolve(process.argv[2] ?? 'shots');
mkdirSync(out, { recursive: true });

const SELECTIONS: Record<string, string> = {
  base: 'v=1',
  servo: 'v=1&f=servo',
  full: `v=1&f=${CATALOGUE.functions.map((f) => f.id).join(',')}`,
};

const server = await preview({ preview: { port: 4174, strictPort: true } });
const browser = await chromium.launch();
try {
  for (const width of [360, 1280]) {
    for (const theme of ['light', 'dark'] as const) {
      const context = await browser.newContext({ viewport: { width, height: 800 }, colorScheme: theme });
      const page = await context.newPage();
      for (const lang of ['en', 'de']) {
        for (const [name, hash] of Object.entries(SELECTIONS)) {
          await page.goto(`http://localhost:4174/rcbench/#${hash}&l=${lang}`);
          await page.reload();
          await page.locator('svg.drawing').waitFor();
          const file = join(out, `${name}-${width}-${theme}-${lang}.png`);
          await page.screenshot({ path: file, fullPage: true });
          console.log(file);
        }
      }
      await context.close();
    }
  }
} finally {
  await browser.close();
  await server.close();
}
