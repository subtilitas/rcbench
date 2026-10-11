// Command line of the part drawings.
//
//   node drawings/cli.ts            writes public/drawings/** and drawings/anchors.json
//   node drawings/cli.ts --check    writes nothing; exit code 1 on a finding or on drift
//   node drawings/cli.ts --palette  prints the contrast table of the wire palette

import { mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { CATALOGUE } from '../core/catalogue.ts';
import { checkAll, filesOf } from './check.ts';
import type { Files } from './check.ts';
import { BACKGROUNDS, PALETTE, ratio } from './wires.ts';

const here = dirname(fileURLToPath(import.meta.url));
/** Folder of the SVG files. Vite copies `public/` into the page. */
export const SVG_DIR = join(here, '..', 'public', 'drawings');
export const ANCHORS_FILE = join(here, 'anchors.json');

/** Every SVG file below a folder at any depth, by its path with forward slashes. */
export function readSvgs(dir: string): Record<string, string> {
  const out: Record<string, string> = {};
  const walk = (folder: string, prefix: string): void => {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
      if (entry.isDirectory()) walk(join(folder, entry.name), `${prefix}${entry.name}/`);
      else if (entry.name.endsWith('.svg')) out[`${prefix}${entry.name}`] = readFileSync(join(folder, entry.name), 'utf8');
    }
  };
  if (existsSync(dir)) walk(dir, '');
  return out;
}

export function readFiles(svgDir: string = SVG_DIR, anchorsFile: string = ANCHORS_FILE): Files {
  return { anchors: existsSync(anchorsFile) ? readFileSync(anchorsFile, 'utf8') : '', svgs: readSvgs(svgDir) };
}

/** Writes the files and removes every SVG the generator does not write. Returns the number of files written. */
export function writeFiles(files: Files, svgDir: string = SVG_DIR, anchorsFile: string = ANCHORS_FILE): number {
  for (const path of Object.keys(readSvgs(svgDir))) {
    if (!(path in files.svgs)) rmSync(join(svgDir, path));
  }
  for (const [path, svg] of Object.entries(files.svgs)) {
    mkdirSync(dirname(join(svgDir, path)), { recursive: true });
    writeFileSync(join(svgDir, path), svg);
  }
  writeFileSync(anchorsFile, files.anchors);
  return Object.keys(files.svgs).length + 1;
}

/** The contrast table of the palette as Markdown. */
export function paletteTable(): string {
  const lines = [
    `| Colour | Core | Casing | ${Object.entries(BACKGROUNDS).map(([n, c]) => `${n} ${c}`).join(' | ')} |`,
    `|---|---|---|${Object.keys(BACKGROUNDS).map(() => '---:').join('|')}|`,
  ];
  for (const [name, { core, casing }] of Object.entries(PALETTE)) {
    const cells = Object.values(BACKGROUNDS).map((bg) => {
      const rc = ratio(core, bg);
      if (casing === null) return rc.toFixed(2);
      const rk = ratio(casing, bg);
      return rc >= rk ? `${rc.toFixed(2)} (core)` : `${rk.toFixed(2)} (casing)`;
    });
    lines.push(`| ${name} | \`${core}\` | ${casing === null ? 'none' : `\`${casing}\``} | ${cells.join(' | ')} |`);
  }
  return `${lines.join('\n')}\n`;
}

function main(argv: string[]): number {
  if (argv.includes('--palette')) {
    process.stdout.write(paletteTable());
    return 0;
  }
  const expected = filesOf(CATALOGUE);
  if (!argv.includes('--check')) {
    const n = writeFiles(expected);
    console.log(`drawings: ${n} files written`);
  }
  const findings = checkAll(CATALOGUE, readFiles(), expected);
  for (const f of findings) console.error(`FAIL ${f.id}: ${f.msg}`);
  const anchors = JSON.parse(expected.anchors) as { files: Record<string, { pins: object }> };
  const pins = Object.values(anchors.files).reduce((n, r) => n + Object.keys(r.pins).length, 0);
  console.log(`drawings: ${Object.keys(anchors.files).length} drawings in 2 languages, ${pins} anchors, ${CATALOGUE.wires.length} wires, ${findings.length} findings`);
  return findings.length > 0 ? 1 : 0;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) process.exit(main(process.argv.slice(2)));
