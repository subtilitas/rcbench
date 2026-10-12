// Every place that names the release reads it from configurator/package.json
// or equals it. This module checks the places that cannot read it:
//
//   - the firmware header shared/link/include/rcbench_version.h, whose
//     MAJOR, MINOR and PATCH define the firmware release;
//   - the release numbers that the Markdown files of the configurator state
//     in prose ("release 0.17.0", "the tag `v0.17.0`").
//
// A release bump changes package.json and the header in the same pull request.
// The functions return findings; main() in check.ts prints them.

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

/** "major.minor.patch" from the three defines of the header, or null when one is missing. */
export function headerVersion(header: string): string | null {
  const part = (name: string): string | null => {
    const m = new RegExp(`^\\s*#\\s*define\\s+RCBENCH_VERSION_${name}\\s+(\\d+)\\b`, 'm').exec(header);
    return m === null ? null : m[1]!;
  };
  const parts = [part('MAJOR'), part('MINOR'), part('PATCH')];
  return parts.some((p) => p === null) ? null : parts.join('.');
}

const FIX = 'change configurator/package.json ("version") and shared/link/include/rcbench_version.h (RCBENCH_VERSION_MAJOR, _MINOR, _PATCH) in the same pull request';

/**
 * The release of package.json against the firmware header. `header` is the
 * text of the header, or null when the file is absent.
 */
export function checkHeader(version: string, header: string | null): Finding[] {
  if (header === null) return [err('release-source', `${VERSION_HEADER} cannot be read; the release of the configurator is not compared with the firmware`)];
  const fw = headerVersion(header);
  if (fw === null) return [err('release-source', `${VERSION_HEADER} defines no RCBENCH_VERSION_MAJOR, _MINOR and _PATCH`)];
  if (fw !== version) {
    return [err('release-source', `configurator/package.json is ${version}, rcbench_version.h is ${fw}; ${FIX}`)];
  }
  return [];
}

/**
 * Release numbers in prose: "release X.Y.Z" and "vX.Y.Z" must equal the
 * release of package.json.
 */
export function checkDocs(version: string, docs: ReadonlyArray<{ file: string; text: string }>): Finding[] {
  const out: Finding[] = [];
  const re = /\b(?:[Rr]elease\s+|v)(\d+\.\d+\.\d+)\b/g;
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

/** Both checks on the files of this tree. */
export function checkReleaseSources(): Finding[] {
  const header = readOrNull(join(CONFIGURATOR_DIR, VERSION_HEADER));
  const docs = RELEASE_DOCS.map((file) => ({ file, text: readOrNull(join(CONFIGURATOR_DIR, file)) ?? '' }));
  return [...checkHeader(VERSION, header), ...checkDocs(VERSION, docs)];
}
