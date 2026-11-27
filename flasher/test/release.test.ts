import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { BOARDS } from '../src/boards.ts';
import { readIndex, readManifest } from '../src/manifest.ts';
import type { Manifest, ReleaseEntry } from '../src/manifest.ts';
import { boardDefault, releaseFor, releasesFor, visitRelease } from '../src/release.ts';

// fw/ of the Pages site on 2026-11-23: v0.23.11 to v0.23.16 (testing) and
// v0.22.1 (stable, the IOmcu image alone).
const dir = new URL('./pages-fw/', import.meta.url);
const json = (name: string): unknown => JSON.parse(readFileSync(new URL(name, dir), 'utf8'));
const RELEASES = readIndex(json('index.json'));
const MANIFESTS = new Map<string, Manifest>(RELEASES.map((r) => [r.tag, readManifest(json(`${r.tag}.manifest.json`), r.tag)]));

const only = (tags: string[]): ReleaseEntry[] => RELEASES.filter((r) => tags.includes(r.tag));
const simple = (visit: string | null) => ({ visit, chosen: null, advanced: false });

test('the fixture is the Pages data of 2026-11-23', () => {
  assert.deepEqual(RELEASES.map((r) => `${r.tag} ${r.kind}`), [
    'v0.23.16 testing', 'v0.23.15 testing', 'v0.23.14 testing', 'v0.23.13 testing',
    'v0.23.12 testing', 'v0.23.11 testing', 'v0.22.1 stable']);
  assert.deepEqual(MANIFESTS.get('v0.22.1')!.images.map((i) => i.board), ['iomcu']);
  assert.equal(MANIFESTS.get('v0.23.16')!.images.length, 8);
  assert.deepEqual(releasesFor('dial', RELEASES, MANIFESTS).map((r) => r.tag), ['v0.23.16', 'v0.23.15', 'v0.23.14', 'v0.23.13']);
  assert.deepEqual(releasesFor('core2350b', RELEASES, MANIFESTS), []);
});

test('one release for the visit: the panel image\'s default, written to every board it carries', () => {
  const visit = visitRelease(RELEASES, MANIFESTS);
  assert.equal(visit, 'v0.23.16');
  // The K518's image comes with the releases after the fixture's.
  for (const b of BOARDS.filter((x) => !x.planned && x.id !== 'dial-k518')) assert.equal(releaseFor(b.id, RELEASES, MANIFESTS, simple(visit)), 'v0.23.16', b.id);
  assert.equal(releaseFor('dial-k518', RELEASES, MANIFESTS, simple(visit)), null);
  // The per-board rule alone gives the IOmcu the stable v0.22.1; the visit
  // release keeps panel and IOmcu on one release.
  assert.equal(boardDefault('iomcu', RELEASES, MANIFESTS), 'v0.22.1');
  assert.equal(releaseFor('core2350b', RELEASES, MANIFESTS, simple(visit)), null);
});

test('a board the visit release does not carry falls back to the per-board rule', () => {
  // v0.23.16 without its C6 image: a release that leaves one image out.
  const m = MANIFESTS.get('v0.23.16')!;
  const without = new Map(MANIFESTS);
  without.set('v0.23.16', { ...m, images: m.images.filter((i) => i.board !== 'c6') });
  const visit = visitRelease(RELEASES, without);
  assert.equal(visit, 'v0.23.16');
  assert.equal(releaseFor('c6', RELEASES, without, simple(visit)), 'v0.23.15');
  assert.equal(releaseFor('dial', RELEASES, without, simple(visit)), 'v0.23.16');
});

test('without a release that carries a panel image every board takes the per-board rule', () => {
  const list = only(['v0.22.1']);
  assert.equal(visitRelease(list, MANIFESTS), null);
  assert.equal(releaseFor('iomcu', list, MANIFESTS, simple(null)), 'v0.22.1');
  assert.equal(releaseFor('panel', list, MANIFESTS, simple(null)), null);
  assert.equal(releaseFor('dial', list, MANIFESTS, simple(null)), null);
});

test('on the release set before the nodes the visit release is v0.23.12 and the nodes have none', () => {
  const list = only(['v0.23.12', 'v0.23.11', 'v0.22.1']);
  const visit = visitRelease(list, MANIFESTS);
  assert.equal(visit, 'v0.23.12');
  assert.equal(releaseFor('iomcu', list, MANIFESTS, simple(visit)), 'v0.23.12');
  assert.equal(releaseFor('c6', list, MANIFESTS, simple(visit)), 'v0.23.12');
  assert.equal(releaseFor('node-pwr', list, MANIFESTS, simple(visit)), null);
});

test('Advanced writes the picked release while it carries the board; simple mode ignores a pick', () => {
  const visit = visitRelease(RELEASES, MANIFESTS);
  const pick = (chosen: string, advanced: boolean) => ({ visit, chosen, advanced });
  assert.equal(releaseFor('iomcu', RELEASES, MANIFESTS, pick('v0.22.1', true)), 'v0.22.1');
  assert.equal(releaseFor('iomcu', RELEASES, MANIFESTS, pick('v0.22.1', false)), 'v0.23.16');
  assert.equal(releaseFor('panel', RELEASES, MANIFESTS, pick('v0.22.1', true)), 'v0.23.16');
  assert.equal(releaseFor('c6', RELEASES, MANIFESTS, pick('v0.23.11', true)), 'v0.23.11');
  assert.equal(releaseFor('dial', RELEASES, MANIFESTS, pick('v0.23.11', true)), 'v0.23.16');
  assert.equal(releaseFor('dial', RELEASES, MANIFESTS, pick('v9.9.9', true)), 'v0.23.16');
});

test('a stable release with a panel image becomes the visit release over newer testing ones', () => {
  const p = MANIFESTS.get('v0.23.16')!;
  const withStable = new Map(MANIFESTS);
  const stable: ReleaseEntry = { tag: 'v0.24.0', kind: 'stable', published: '2026-11-22T00:00:00Z' };
  withStable.set('v0.24.0', { ...stable, images: p.images.filter((i) => i.board === 'panel' || i.board === 'iomcu') });
  const testing: ReleaseEntry = { tag: 'v0.25.0', kind: 'testing', published: '2026-11-23T00:00:00Z' };
  withStable.set('v0.25.0', { ...testing, images: p.images });
  const list = [testing, stable, ...RELEASES];
  const visit = visitRelease(list, withStable);
  assert.equal(visit, 'v0.24.0');
  assert.equal(releaseFor('iomcu', list, withStable, simple(visit)), 'v0.24.0');
  // The dial is not in v0.24.0: the per-board rule finds no stable release
  // with a Dial image and takes the newest, v0.25.0.
  assert.equal(releaseFor('dial', list, withStable, simple(visit)), 'v0.25.0');
});
