// The release of package.json against the firmware header and the prose of
// the Markdown files.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';

import { TAG, VERSION } from '../../release.ts';
import { CATALOGUE } from '../catalogue.ts';
import { RELEASE_TAG } from '../tools/check.ts';
import {
  CONFIGURATOR_DIR, RELEASE_DOCS, VERSION_HEADER, checkDocs, checkHeader, checkReleaseSources, headerVersion,
} from '../tools/release-sources.ts';

const HEADER = '#define RCBENCH_VERSION_MAJOR 0\n#define RCBENCH_VERSION_MINOR 17\n#define RCBENCH_VERSION_PATCH 0\n';

test('the release comes from package.json and feeds the data and the tag', () => {
  const pkg = JSON.parse(readFileSync(join(CONFIGURATOR_DIR, 'package.json'), 'utf8')) as { version: string };
  assert.match(pkg.version, /^\d+\.\d+\.\d+$/);
  assert.equal(VERSION, pkg.version);
  assert.equal(TAG, `v${pkg.version}`);
  assert.equal(CATALOGUE.release, pkg.version);
  assert.equal(RELEASE_TAG, TAG);
});

test('the anchor table of the drawings names the release of package.json', () => {
  const anchors = JSON.parse(readFileSync(join(CONFIGURATOR_DIR, 'drawings', 'anchors.json'), 'utf8')) as { release: string };
  assert.equal(anchors.release, VERSION);
});

test('headerVersion reads the three defines and rejects a header without one', () => {
  assert.equal(headerVersion(HEADER), '0.17.0');
  assert.equal(headerVersion(HEADER.replace(/#define RCBENCH_VERSION_PATCH 0\n/, '')), null);
  assert.equal(headerVersion(''), null);
});

test('package.json and the firmware header agree in this tree', () => {
  assert.deepEqual(checkReleaseSources(), []);
});

test('a header of another release is an error that names both files', () => {
  const other = HEADER.replace('MINOR 17', 'MINOR 18');
  const f = checkHeader('0.17.0', other);
  assert.equal(f.length, 1);
  assert.equal(f[0]!.level, 'error');
  assert.match(f[0]!.msg, /package\.json is 0\.17\.0, rcbench_version\.h is 0\.18\.0/);
  assert.match(f[0]!.msg, /configurator\/package\.json.*shared\/link\/include\/rcbench_version\.h/);
  assert.deepEqual(checkHeader('0.17.0', HEADER), []);
});

test('an absent or unparsable header is an error', () => {
  assert.equal(checkHeader('0.17.0', null).length, 1);
  assert.equal(checkHeader('0.17.0', '#define OTHER 1\n').length, 1);
  assert.ok(VERSION_HEADER.endsWith('rcbench_version.h'));
});

test('prose that names another release is an error with file and line', () => {
  const docs = [{ file: 'README.md', text: 'ok\nA page for release 0.16.0 of rcbench, tag `v0.17.0`.\n' }];
  const f = checkDocs('0.17.0', docs);
  assert.equal(f.length, 1);
  assert.match(f[0]!.msg, /^README\.md:2 names release 0\.16\.0, package\.json is 0\.17\.0$/);
  assert.deepEqual(checkDocs('0.17.0', [{ file: 'x.md', text: 'Release 0.17.0 and v0.17.0; 0.16.0 alone, 4.2 ms' }]), []);
});

test('the Markdown files of the configurator state the release of package.json', () => {
  const docs = RELEASE_DOCS.map((file) => ({ file, text: readFileSync(join(CONFIGURATOR_DIR, file), 'utf8') }));
  assert.deepEqual(checkDocs(VERSION, docs), []);
});
