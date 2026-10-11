// Size check of the built page. Run after `npm run build`.
//
// Initial: index.html and every file it references. It holds no picture.
// Total: every file of dist/ outside the picture folder.
// The picture folder is reported and has no budget; its figure is in README.md.
// Exit code 1 when a budget is exceeded or the build is missing.

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const INITIAL_BUDGET = 250_000;
const TOTAL_BUDGET = 1_000_000;
const BASE = '/rcbench/';
const PICTURES = 'pictures';

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
const isPicture = (path: string): boolean => relative(dist, path).split(/[\\/]/)[0] === PICTURES;
const pictures = all.filter(isPicture);
const total = all.filter((path) => !isPicture(path)).reduce((sum, path) => sum + size(path), 0);
const pictureBytes = pictures.reduce((sum, path) => sum + size(path), 0);
const initialPictures = referenced.filter(isPicture);

for (const path of all) console.log(`${kb(size(path)).padStart(10)}  ${relative(dist, path)}`);
console.log(`initial ${kb(initial)} of ${kb(INITIAL_BUDGET)}`);
console.log(`total   ${kb(total)} of ${kb(TOTAL_BUDGET)}, without the picture folder`);
console.log(`pictures ${kb(pictureBytes)} in ${pictures.length} files`);

if (initialPictures.length > 0) {
  console.error(`index.html references a picture: ${initialPictures.map((path) => relative(dist, path)).join(', ')}`);
  process.exit(1);
}
if (initial > INITIAL_BUDGET || total > TOTAL_BUDGET) {
  console.error('The build exceeds its size budget.');
  process.exit(1);
}
