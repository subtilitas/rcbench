import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ManifestError, compareTags, defaultRelease, kindOf, readIndex, readManifest } from '../src/manifest.ts';

const sha = 'a'.repeat(64);
const image = (over: Record<string, unknown> = {}): Record<string, unknown> => ({
  board: 'dial', chip: 'esp32s3', file: 'rcbench-dial-merged-0.23.15.bin', size: 981888, sha256: sha, offset: 0, format: 'bin', ...over,
});
const manifest = (over: Record<string, unknown> = {}): Record<string, unknown> => ({
  format: 1, tag: 'v0.23.15', kind: 'testing', published: '2026-11-18T21:49:31Z', images: [image()], ...over,
});
const bad = (f: () => unknown, re: RegExp): void => assert.throws(f, (e: unknown) => e instanceof ManifestError && re.test(e.message));

test('odd minor numbers are testing releases, even minor numbers stable ones', () => {
  assert.equal(kindOf('v0.23.15'), 'testing');
  assert.equal(kindOf('v0.22.4'), 'stable');
  assert.equal(kindOf('v1.0.0'), 'stable');
  assert.equal(kindOf('0.23.1'), null);
  assert.equal(kindOf('v0.23.01'), null);
});

test('the index lists the releases newest first, whatever order the file has', () => {
  const r = readIndex({ format: 1, releases: [
    { tag: 'v0.22.9', kind: 'stable', published: '2026-10-01T00:00:00Z' },
    { tag: 'v0.23.15', kind: 'testing', published: '2026-11-18T00:00:00Z' },
    { tag: 'v0.23.2', kind: 'testing', published: '2026-10-20T00:00:00Z' },
  ] });
  assert.deepEqual(r.map((x) => x.tag), ['v0.23.15', 'v0.23.2', 'v0.22.9']);
  assert.equal(defaultRelease(r)?.tag, 'v0.22.9');
  assert.equal(defaultRelease(r.filter((x) => x.kind === 'testing'))?.tag, 'v0.23.15');
  assert.equal(defaultRelease([]), undefined);
  assert.ok(compareTags('v0.23.10', 'v0.23.9') < 0);
});

test('an index of another format, a kind that contradicts the tag and a tag twice are refused', () => {
  bad(() => readIndex({ format: 2, releases: [] }), /format/);
  bad(() => readIndex({ format: 1, releases: [{ tag: 'v0.23.1', kind: 'stable', published: '2026-01-01' }] }), /kind/);
  bad(() => readIndex({ format: 1, releases: [{ tag: 'latest', kind: 'stable', published: '2026-01-01' }] }), /tag/);
  const r = { tag: 'v0.22.1', kind: 'stable', published: '2026-01-01' };
  bad(() => readIndex({ format: 1, releases: [r, r] }), /twice/);
  bad(() => readIndex([]), /not an object/);
});

test('a manifest reads into its images, sorted in the order of the board list', () => {
  const m = readManifest(manifest({ images: [
    image(),
    image({ board: 'panel', chip: 'esp32p4', file: 'rcbench-panel-p4-merged-v0.23.15.bin' }),
    image({ board: 'iomcu', chip: 'rp2350', format: 'uf2', file: 'rcbench-iomcu-install-v0.23.15.uf2', erase: [{ offset: 0x1ff000, size: 0x1fd000 }] }),
  ] }), 'v0.23.15');
  assert.deepEqual(m.images.map((i) => i.board), ['panel', 'iomcu', 'dial']);
  assert.deepEqual(m.images[1]!.erase, [{ offset: 0x1ff000, size: 0x1fd000 }]);
  assert.deepEqual(m.images[0]!.erase, []);
});

test('a manifest for another tag is refused', () => {
  bad(() => readManifest(manifest(), 'v0.23.14'), /tag v0.23.15, v0.23.14 expected/);
});

test('the checks of an image entry', () => {
  const one = (over: Record<string, unknown>): unknown => readManifest(manifest({ images: [image(over)] }), 'v0.23.15');
  bad(() => one({ chip: 'esp32c6' }), /board dial is an esp32s3/);
  bad(() => one({ chip: 'esp8266' }), /unknown/);
  bad(() => one({ file: '../index.html' }), /plain file name/);
  bad(() => one({ file: 'a/b.bin' }), /plain file name/);
  bad(() => one({ size: 0 }), /size/);
  bad(() => one({ size: 64 * 1024 * 1024 }), /size/);
  bad(() => one({ sha256: 'A'.repeat(64) }), /sha256/);
  bad(() => one({ offset: 0x10000 }), /offset/);
  bad(() => one({ format: 'uf2' }), /format/);
  bad(() => one({ erase: [{ offset: 0, size: 4096 }] }), /erase/);
  bad(() => one({ board: 'iomcu', chip: 'rp2350', format: 'uf2', erase: [{ offset: 100, size: 4096 }] }), /aligned/);
  bad(() => readManifest(manifest({ images: [image(), image()] }), 'v0.23.15'), /twice/);
});

test('an image for a board this page does not know stays out of the list', () => {
  const m = readManifest(manifest({ images: [image(), image({ board: 'node-future', chip: 'esp32c6' })] }), 'v0.23.15');
  assert.deepEqual(m.images.map((i) => i.board), ['dial']);
});
