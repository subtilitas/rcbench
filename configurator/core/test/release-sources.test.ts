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
  CONFIGURATOR_DIR, RELEASE_DOCS, VERSION_HEADER, checkDocs, checkHeader, checkLock, checkReleaseSources, compareVersions, gitHasTag, headerVersion,
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

const hdr = (v: string): string => {
  const [a, b, c] = v.split('.');
  return `#define RCBENCH_VERSION_MAJOR ${a}\n#define RCBENCH_VERSION_MINOR ${b}\n#define RCBENCH_VERSION_PATCH ${c}\n`;
};

test('compareVersions orders numerically per field', () => {
  assert.equal(compareVersions('0.17.0', '0.17.0'), 0);
  assert.equal(compareVersions('0.9.0', '0.10.0'), -1);
  assert.equal(compareVersions('1.0.0', '0.99.99'), 1);
});

test('equal releases give no finding', () => {
  assert.deepEqual(checkHeader('0.17.0', hdr('0.17.0'), () => true), []);
});

test('a header one release ahead with no tag is a warning that names the order', () => {
  const f = checkHeader('0.17.0', hdr('0.18.0'), () => false);
  assert.equal(f.length, 1);
  assert.equal(f[0]!.level, 'warn');
  assert.match(f[0]!.msg, /firmware header is 0\.18\.0, the configurator data describes 0\.17\.0: bump configurator\/package\.json after the tag v0\.18\.0 exists/);
});

test('a header ahead whose tag exists is an error', () => {
  const asked: string[] = [];
  const f = checkHeader('0.17.0', hdr('0.18.0'), (tag) => { asked.push(tag); return true; });
  assert.deepEqual(asked, ['v0.18.0']);
  assert.equal(f.length, 1);
  assert.equal(f[0]!.level, 'error');
  assert.match(f[0]!.msg, /tag v0\.18\.0 exists, configurator\/package\.json is 0\.17\.0/);
});

test('package.json ahead of the header is an error, also with the tag present', () => {
  for (const exists of [true, false]) {
    const f = checkHeader('0.18.0', hdr('0.17.0'), () => exists);
    assert.equal(f.length, 1);
    assert.equal(f[0]!.level, 'error');
    assert.match(f[0]!.msg, /package\.json is 0\.18\.0, ahead of rcbench_version\.h 0\.17\.0/);
  }
});

test('the real git lookup finds an existing tag and not an invented one', () => {
  assert.equal(gitHasTag(CONFIGURATOR_DIR, 'v999.0.0'), false);
});

test('package-lock.json: both root version fields must equal package.json', () => {
  const lock = (top: string, root: string): string => JSON.stringify({ version: top, packages: { '': { version: root } } });
  assert.deepEqual(checkLock('0.17.0', lock('0.17.0', '0.17.0')), []);
  const top = checkLock('0.17.0', lock('0.16.0', '0.17.0'));
  assert.equal(top.length, 1);
  assert.match(top[0]!.msg, /package-lock\.json version is 0\.16\.0, package\.json is 0\.17\.0/);
  const root = checkLock('0.17.0', lock('0.17.0', '0.16.0'));
  assert.equal(root.length, 1);
  assert.match(root[0]!.msg, /packages\[""\]\.version is 0\.16\.0/);
  assert.equal(checkLock('0.17.0', lock('0.1.0', '0.2.0')).length, 2);
  assert.equal(checkLock('0.17.0', '{}').length, 2);
  assert.equal(checkLock('0.17.0', 'x').length, 1);
  assert.equal(checkLock('0.17.0', null).length, 1);
});

test('the JSON form of the release in prose is checked', () => {
  const f = checkDocs('0.17.0', [{ file: 'd.md', text: 'x\n "release": "0.16.0",\n' }]);
  assert.equal(f.length, 1);
  assert.match(f[0]!.msg, /^d\.md:2 names release 0\.16\.0/);
  assert.deepEqual(checkDocs('0.17.0', [{ file: 'd.md', text: ' "release": "0.17.0",' }]), []);
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
