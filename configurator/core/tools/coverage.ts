// Line coverage of the resolver, written into README.md.
//
//   node tools/coverage.ts           runs the suite and writes the figure
//   node tools/coverage.ts --check   runs the suite and fails when the figure
//                                    in README.md differs; writes nothing
//
// Exit codes: 0 figure current or written, 1 drift or a failing suite,
// 2 unknown argument.

import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

/** The files whose line coverage the README states, relative to core/. */
export const MEASURED: ReadonlyArray<string> = ['resolve.ts', 'cond.ts', 'groups.ts', 'sim.ts'];

export const BEGIN = '<!-- coverage:begin -->';
export const END = '<!-- coverage:end -->';

export type FileCoverage = { file: string; found: number; hit: number };

/** Reads the `SF`, `LF` and `LH` records of an lcov report. Paths become relative to `root`. */
export function parseLcov(report: string, root: string): FileCoverage[] {
  const out: FileCoverage[] = [];
  let file: string | null = null;
  let found = 0;
  let hit = 0;
  for (const line of report.split('\n')) {
    if (line.startsWith('SF:')) {
      file = relative(root, resolve(root, line.slice(3))).split(sep).join('/');
      found = 0;
      hit = 0;
    } else if (line.startsWith('LF:')) {
      found = Number(line.slice(3));
    } else if (line.startsWith('LH:')) {
      hit = Number(line.slice(3));
    } else if (line === 'end_of_record' && file !== null) {
      out.push({ file, found, hit });
      file = null;
    }
  }
  return out;
}

/** Percent with one decimal, rounded down, so 100.0 means every line. */
export function percent(hit: number, found: number): string {
  if (found <= 0) return '0.0';
  return (Math.floor((hit * 1000) / found) / 10).toFixed(1);
}

/** The block between the two markers. Throws when a measured file is not in the report. */
export function figure(report: FileCoverage[]): string {
  const rows = MEASURED.map((name) => {
    const entry = report.find((r) => r.file === name);
    if (!entry) throw new Error(`the coverage report has no record for ${name}`);
    return `| \`${name}\` | ${entry.hit} | ${entry.found} | ${percent(entry.hit, entry.found)}% |`;
  });
  return [
    BEGIN,
    '| File | Lines run | Lines | Line coverage |',
    '| --- | ---: | ---: | ---: |',
    ...rows,
    END,
  ].join('\n');
}

/** The README with the block replaced, or null when the markers are missing or out of order. */
export function replaceFigure(readme: string, block: string): string | null {
  const start = readme.indexOf(BEGIN);
  const end = readme.indexOf(END);
  if (start < 0 || end < start) return null;
  return readme.slice(0, start) + block + readme.slice(end + END.length);
}

function main(argv: string[]): number {
  const unknown = argv.filter((a) => a !== '--check');
  if (unknown.length > 0) {
    console.error(`unknown argument: ${unknown.join(' ')}`);
    return 2;
  }
  const check = argv.includes('--check');
  const root = join(dirname(fileURLToPath(import.meta.url)), '..');

  const run = spawnSync(
    process.execPath,
    ['--test', '--experimental-test-coverage', '--test-reporter=lcov', '--test-reporter-destination=stdout', 'test/*.test.ts'],
    { cwd: root, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 },
  );
  if (run.status !== 0) {
    console.error(run.stderr);
    console.error(`the test suite fails (exit ${run.status}); no coverage figure`);
    return 1;
  }

  let block: string;
  try {
    block = figure(parseLcov(run.stdout, root));
  } catch (e) {
    console.error((e as Error).message);
    return 1;
  }

  const path = join(root, 'README.md');
  const readme = readFileSync(path, 'utf8');
  const next = replaceFigure(readme, block);
  if (next === null) {
    console.error(`README.md has no ${BEGIN} ... ${END} block`);
    return 1;
  }
  console.log(block.split('\n').slice(1, -1).join('\n'));
  if (next === readme) {
    console.log('README.md states this figure');
    return 0;
  }
  if (check) {
    console.error('README.md states another figure; run "npm run coverage" and commit the file');
    return 1;
  }
  writeFileSync(path, next);
  console.log('README.md written');
  return 0;
}

if (fileURLToPath(import.meta.url) === process.argv[1]) {
  process.exitCode = main(process.argv.slice(2));
}
