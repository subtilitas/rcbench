// Every place that names the release reads it from configurator/package.json
// or equals it. This module checks the places that cannot read it:
//
//   - the firmware header shared/link/include/rcbench_version.h, whose
//     MAJOR, MINOR and PATCH define the firmware release;
//   - the release numbers that the Markdown files of the configurator state
//     in prose ("release 0.17.0", "the tag `v0.17.0`").
//
// The release pull request moves the header to the new version before the tag
// exists; package.json moves after the tag exists, because the data check
// reads the files at that tag. A header one release ahead of package.json,
// with no tag for the header version yet, is therefore a warning. The
// functions return findings; main() in check.ts prints them.

import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { VERSION } from '../../release.ts';
import type { Finding } from './check.ts';

const here = dirname(fileURLToPath(import.meta.url));

/** The configurator folder. */
export const CONFIGURATOR_DIR = join(here, '..', '..');

/** The firmware release header, relative to the configurator folder. */
export const VERSION_HEADER = '../shared/link/include/rcbench_version.h';

/** The Markdown files of the configurator that state a release number in prose. */
export const RELEASE_DOCS = ['README.md', 'core/README.md', 'drawings/README.md'];

function err(id: string, msg: string): Finding {
  return { level: 'error', id, msg };
}

function warn(id: string, msg: string): Finding {
  return { level: 'warn', id, msg };
}

/** -1, 0 or 1 for "a.b.c" against "d.e.f"; numeric per field. */
export function compareVersions(a: string, b: string): number {
  const x = a.split('.').map(Number);
  const y = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) {
    const d = (x[i] ?? 0) - (y[i] ?? 0);
    if (d !== 0) return d < 0 ? -1 : 1;
  }
  return 0;
}

/** True when the git repository that holds `dir` has the tag. False without git. */
export function gitHasTag(dir: string, tag: string): boolean {
  try {
    execFileSync('git', ['-C', dir, 'rev-parse', '--quiet', '--verify', `refs/tags/${tag}^{commit}`], {
      stdio: ['ignore', 'ignore', 'ignore'],
    });
    return true;
  } catch {
    return false;
  }
}

/** "major.minor.patch" from the three defines of the header, or null when one is missing. */
export function headerVersion(header: string): string | null {
  const part = (name: string): string | null => {
    const m = new RegExp(`^\\s*#\\s*define\\s+RCBENCH_VERSION_${name}\\s+(\\d+)\\b`, 'm').exec(header);
    return m === null ? null : m[1]!;
  };
  const parts = [part('MAJOR'), part('MINOR'), part('PATCH')];
  return parts.some((p) => p === null) ? null : parts.join('.');
}

const FIX = 'change configurator/package.json ("version") to the release of shared/link/include/rcbench_version.h (RCBENCH_VERSION_MAJOR, _MINOR, _PATCH) once its tag exists';

/**
 * The release of package.json against the firmware header. `header` is the
 * text of the header, or null when the file is absent. `tagExists` tells
 * whether the git tag of a release exists.
 *
 * - equal: no finding;
 * - header ahead, tag of the header version absent: warning, the release PR
 *   moved the header and the tag is not made yet;
 * - header ahead, tag present: error, the configurator is behind a published
 *   release;
 * - package.json ahead of the header: error.
 */
export function checkHeader(version: string, header: string | null, tagExists: (tag: string) => boolean = () => false): Finding[] {
  if (header === null) return [err('release-source', `${VERSION_HEADER} cannot be read; the release of the configurator is not compared with the firmware`)];
  const fw = headerVersion(header);
  if (fw === null) return [err('release-source', `${VERSION_HEADER} defines no RCBENCH_VERSION_MAJOR, _MINOR and _PATCH`)];
  const order = compareVersions(version, fw);
  if (order === 0) return [];
  if (order > 0) {
    return [err('release-source', `configurator/package.json is ${version}, ahead of rcbench_version.h ${fw}; ${FIX}`)];
  }
  if (tagExists(`v${fw}`)) {
    return [err('release-source', `the tag v${fw} exists, configurator/package.json is ${version}; ${FIX}`)];
  }
  return [warn('release-source', `the firmware header is ${fw}, the configurator data describes ${version}: bump configurator/package.json after the tag v${fw} exists`)];
}

/** One "version" field of package-lock.json must equal the release. */
export function checkLock(version: string, lock: string | null): Finding[] {
  if (lock === null) return [err('release-source', 'configurator/package-lock.json cannot be read; its release is not compared with package.json')];
  let json: { version?: unknown; packages?: { ''?: { version?: unknown } } };
  try {
    json = JSON.parse(lock) as typeof json;
  } catch {
    return [err('release-source', 'configurator/package-lock.json is not valid JSON')];
  }
  const out: Finding[] = [];
  const fields: Array<[string, unknown]> = [['version', json.version], ['packages[""].version', json.packages?.['']?.version]];
  for (const [name, value] of fields) {
    if (value !== version) {
      out.push(err('release-source', `configurator/package-lock.json ${name} is ${String(value)}, package.json is ${version}`));
    }
  }
  return out;
}

/**
 * Release numbers in prose: "release X.Y.Z", "vX.Y.Z" and the JSON form
 * "release": "X.Y.Z" must equal the release of package.json.
 */
export function checkDocs(version: string, docs: ReadonlyArray<{ file: string; text: string }>): Finding[] {
  const out: Finding[] = [];
  const re = /(?:\b(?:[Rr]elease\s+|v)|"release"\s*:\s*")(\d+\.\d+\.\d+)\b/g;
  for (const { file, text } of docs) {
    text.split('\n').forEach((line, i) => {
      for (const m of line.matchAll(re)) {
        if (m[1] !== version) {
          out.push(err('release-source', `${file}:${i + 1} names release ${m[1]}, package.json is ${version}`));
        }
      }
    });
  }
  return out;
}

function readOrNull(path: string): string | null {
  try {
    return readFileSync(path, 'utf8');
  } catch {
    return null;
  }
}

/** All checks on the files of this tree. */
export function checkReleaseSources(): Finding[] {
  const header = readOrNull(join(CONFIGURATOR_DIR, VERSION_HEADER));
  const docs = RELEASE_DOCS.map((file) => ({ file, text: readOrNull(join(CONFIGURATOR_DIR, file)) ?? '' }));
  const lock = readOrNull(join(CONFIGURATOR_DIR, 'package-lock.json'));
  return [
    ...checkHeader(VERSION, header, (tag) => gitHasTag(CONFIGURATOR_DIR, tag)),
    ...checkLock(VERSION, lock),
    ...checkDocs(VERSION, docs),
  ];
}
