// Writes public/licenses.txt: the licence of every package the build bundles,
// with its full text, and the BSD-3-Clause terms of pico-sdk and picotool
// that the PICOBOOT client follows. The page links the file in its footer.
//
//   node tools/licenses.ts           write the file
//   node tools/licenses.ts --check   exit 1 when the file differs from the
//                                    output, or a bundled package has no entry
//                                    in BUNDLED below
//
// The bundled packages are the entries of package-lock.json without "dev".
// Each needs an entry in BUNDLED with its version: a dependency update fails
// the check until its licence is looked at again.

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const OUT = `${root}public/licenses.txt`;

interface Entry {
  version: string;
  licence: string;
  holder: string;
  /** Licence files inside the package, with an optional line range to cut out. */
  texts: { file: string; title: string; lines?: [number, number] }[];
  note?: string;
}

const BUNDLED: Record<string, Entry> = {
  'esptool-js': {
    version: '0.7.1',
    licence: 'Apache-2.0',
    holder: 'Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd. (README of the package)',
    texts: [{ file: 'LICENSE', title: 'Apache License 2.0' }],
    note: 'The package ships no NOTICE file. It carries the flasher stub images of esptool\'s stub_flasher/2 set,\n'
      + '  built from github.com/espressif/esp-flasher-stub, Apache-2.0 OR MIT at the user\'s option, by Espressif.\n'
      + '  The page loads the stub of the detected chip only.',
  },
  pako: {
    version: '2.2.0',
    licence: 'MIT AND Zlib',
    holder: 'Copyright (C) 2014-2017 by Vitaly Puzrin and Andrei Tuputcyn; the zlib port: (C) 1995-2013 Jean-loup Gailly and Mark Adler',
    texts: [
      { file: 'LICENSE', title: 'MIT License' },
      { file: 'lib/zlib/deflate.js', title: 'zlib License (header of lib/zlib/deflate.js)', lines: [3, 20] },
    ],
  },
  'atob-lite': {
    version: '2.0.0',
    licence: 'MIT',
    holder: 'Hugh Kennedy (author in package.json; the licence file carries no copyright line)',
    texts: [{ file: 'LICENSE.md', title: 'MIT License' }],
  },
};

function bundled(): [string, string][] {
  const lock = JSON.parse(readFileSync(`${root}package-lock.json`, 'utf8')) as { packages: Record<string, { version?: string; dev?: boolean }> };
  return Object.entries(lock.packages)
    .filter(([path, p]) => path !== '' && !p.dev)
    .map(([path, p]) => [path.replace(/^.*node_modules\//, ''), p.version ?? '']);
}

function text(): string {
  const errors: string[] = [];
  const pkgs = bundled().sort(([a], [b]) => a.localeCompare(b));
  const head: string[] = [
    'Licences of the code in the rcbench web flasher',
    '===============================================',
    '',
    'The page is part of rcbench, MIT License: LICENSE in the repository,',
    'github.com/subtilitas/rcbench. Its build bundles these libraries:',
    '',
  ];
  const body: string[] = [];
  for (const [name, version] of pkgs) {
    const e = BUNDLED[name];
    if (!e) { errors.push(`${name} ${version} is bundled and has no entry in BUNDLED`); continue; }
    if (e.version !== version) { errors.push(`${name} is ${version} in package-lock.json, ${e.version} in BUNDLED`); continue; }
    head.push(`${name} ${version}`, `  Licence: ${e.licence}`, `  ${e.holder}`);
    if (e.note) head.push(`  ${e.note}`);
    head.push('');
    for (const t of e.texts) {
      let s = readFileSync(`${root}node_modules/${name}/${t.file}`, 'utf8');
      if (t.lines) s = s.split('\n').slice(t.lines[0] - 1, t.lines[1]).map((l) => l.replace(/^\/\/ ?/, '')).join('\n');
      body.push(`==== ${name} ${version}: ${t.title} ====`, '', s.replace(/\s+$/, ''), '');
    }
  }
  head.push(
    'PICOBOOT client (src/picoboot.ts) and UF2 reader (src/uf2.ts)',
    '  Written for this page. The command values, the command layout and the',
    '  family IDs follow pico-sdk 2.3.0 (boot/picoboot.h, boot/picoboot_constants.h,',
    '  boot/uf2.h) and the order of a transfer follows picotool 2.3.0',
    '  (picoboot_connection.c): BSD-3-Clause,',
    '  Copyright (c) 2020 Raspberry Pi (Trading) Ltd.',
    '',
  );
  body.push('==== pico-sdk 2.3.0 and picotool 2.3.0: BSD 3-Clause License ====', '',
    readFileSync(`${root}tools/pico-BSD-3-Clause.txt`, 'utf8').replace(/\s+$/, ''), '');
  if (errors.length) {
    for (const e of errors) console.error(e);
    process.exit(1);
  }
  return [...head, '', ...body].join('\n');
}

const out = text();
if (process.argv.includes('--check')) {
  let have = '';
  try { have = readFileSync(OUT, 'utf8'); } catch { /* missing counts as drift */ }
  if (have !== out) {
    console.error('public/licenses.txt differs from the output of tools/licenses.ts. Run `npm run licenses`.');
    process.exit(1);
  }
  console.log('public/licenses.txt is current.');
} else {
  writeFileSync(OUT, out);
  console.log(`wrote ${OUT}`);
}
