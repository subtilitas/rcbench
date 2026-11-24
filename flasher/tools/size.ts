// Size check of the built page. Run after `npm run build`.
//
// Initial: index.html and every file it references, the load before a click.
// Total: every file of dist/, esptool-js and its flasher stubs included. The
// firmware images under fw/ are not part of dist/: the deploy adds them.
// Exit code 1 when a budget is exceeded or the build is missing.

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

export const INITIAL_BUDGET = 120_000;
export const TOTAL_BUDGET = 600_000;
const BASE = '/rcbench/flash/';

const dist = fileURLToPath(new URL('../dist/', import.meta.url));

function files(dir: string): string[] {
  const out: string[] = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) out.push(...files(path));
    else out.push(path);
  }
  return out;
}

let all: string[];
let html: string;
try {
  all = files(dist);
  html = readFileSync(join(dist, 'index.html'), 'utf8');
} catch {
  console.error('dist/index.html is missing. Run `npm run build` first.');
  process.exit(1);
}

const size = (path: string): number => statSync(path).size;
const kb = (bytes: number): string => `${(bytes / 1000).toFixed(1)} kB`;
const referenced = [...html.matchAll(/(?:src|href)="([^"]+)"/g)]
  .map((m) => m[1] as string)
  .filter((ref) => ref.startsWith(BASE))
  .map((ref) => join(dist, ref.slice(BASE.length)));
const initial = size(join(dist, 'index.html')) + referenced.reduce((sum, path) => sum + size(path), 0);
const total = all.reduce((sum, path) => sum + size(path), 0);

for (const path of all) console.log(`${kb(size(path)).padStart(10)}  ${relative(dist, path)}`);
console.log(`initial ${kb(initial)} of ${kb(INITIAL_BUDGET)}`);
console.log(`total   ${kb(total)} of ${kb(TOTAL_BUDGET)}`);
if (all.some((path) => relative(dist, path).split(/[\\/]/)[0] === 'fw')) {
  console.error('dist/ holds a fw/ folder: firmware images come from the deploy, not from the build.');
  process.exit(1);
}
if (initial > INITIAL_BUDGET || total > TOTAL_BUDGET) {
  console.error('The build exceeds its size budget.');
  process.exit(1);
}
