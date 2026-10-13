import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { CATALOGUE } from '../../core/catalogue.ts';
import { checkAll, checkDrift, filesOf } from '../check.ts';
import { ANCHORS_FILE, SVG_DIR, paletteTable, readFiles, readSvgs, writeFiles } from '../cli.ts';
import { PALETTE } from '../wires.ts';

const cli = fileURLToPath(new URL('../cli.ts', import.meta.url));
const good = filesOf(CATALOGUE);

function temp(): { dir: string; svg: string; anchors: string } {
  const dir = mkdtempSync(join(tmpdir(), 'drawings-'));
  return { dir, svg: join(dir, 'out'), anchors: join(dir, 'anchors.json') };
}

test('the committed drawings and the anchor table are the output of the generator', () => {
  assert.deepEqual(checkAll(CATALOGUE, readFiles(), good), []);
  assert.equal(readFileSync(ANCHORS_FILE, 'utf8'), good.anchors);
  assert.deepEqual(Object.keys(readSvgs(SVG_DIR)).sort(), Object.keys(good.svgs).sort());
});

test('the README of the page states the size of the drawing folder', () => {
  const svgs = Object.values(readSvgs(SVG_DIR));
  const bytes = svgs.reduce((n, s) => n + Buffer.byteLength(s), 0);
  const readme = readFileSync(new URL('../../README.md', import.meta.url), 'utf8');
  assert.ok(readme.includes(`Drawing folder: ${svgs.length} files, ${(bytes / 1000).toFixed(1)} kB.`), `${svgs.length} files, ${(bytes / 1000).toFixed(1)} kB`);
});

test('the README of the drawings carries the palette table and the counts of the generator', () => {
  const readme = readFileSync(new URL('../README.md', import.meta.url), 'utf8');
  assert.ok(readme.includes(paletteTable()));
  const anchors = JSON.parse(good.anchors) as { files: Record<string, { part: string | null; pins: object }> };
  const records = Object.values(anchors.files);
  const pins = records.reduce((n, r) => n + Object.keys(r.pins).length, 0);
  const outside = records.filter((r) => r.part === null).length;
  assert.ok(readme.includes(`- ${records.length} drawings: ${records.length - outside} of the ${CATALOGUE.parts.length} parts of the data file`));
  assert.ok(readme.includes(`${outside} of\n  boards outside it`));
  assert.ok(readme.includes(`each of the ${pins} pins`));
  assert.ok(readme.includes(`1 of ${Object.keys(PALETTE).length} colours`));
});

test('the check mode passes on the committed files and writes nothing', () => {
  const r = spawnSync(process.execPath, [cli, '--check'], { encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  assert.match(r.stdout, /^drawings: 24 drawings in 2 languages, 179 anchors, \d+ wires, 0 findings\n$/);
  assert.doesNotMatch(r.stdout, /written/);
});

test('the palette mode prints 1 row per colour', () => {
  const r = spawnSync(process.execPath, [cli, '--palette'], { encoding: 'utf8' });
  assert.equal(r.status, 0);
  assert.equal(r.stdout, paletteTable());
  assert.equal(r.stdout.trim().split('\n').length, 2 + Object.keys(PALETTE).length);
  assert.match(r.stdout, /\| black \| `#15191f` \| `#c9d1dc` \| 17\.64 \(core\) \| 16\.17 \(core\) \| 11\.21 \(casing\) /);
});

test('writing and reading round-trips, and a file the generator does not write is removed', () => {
  const t = temp();
  try {
    assert.deepEqual(readFiles(t.svg, t.anchors), { anchors: '', svgs: {} });
    mkdirSync(join(t.svg, 'de'), { recursive: true });
    writeFileSync(join(t.svg, 'old.svg'), '<svg/>');
    writeFileSync(join(t.svg, 'de', 'old.svg'), '<svg/>');
    mkdirSync(join(t.svg, 'de', 'archive', 'deep'), { recursive: true });
    writeFileSync(join(t.svg, 'de', 'archive', 'old.svg'), '<svg/>');
    writeFileSync(join(t.svg, 'de', 'archive', 'deep', 'old.svg'), '<svg/>');
    writeFileSync(join(t.svg, 'keep.txt'), 'x');
    // The extension in upper and mixed case is an SVG file too: the page build copies it like the others.
    writeFileSync(join(t.svg, 'upper.SVG'), '<svg/>');
    writeFileSync(join(t.svg, 'de', 'mixed.Svg'), '<svg/>');
    assert.deepEqual(Object.keys(readSvgs(t.svg)).sort(), ['de/archive/deep/old.svg', 'de/archive/old.svg', 'de/mixed.Svg', 'de/old.svg', 'old.svg', 'upper.SVG']);
    const extra = checkDrift(good, readFiles(t.svg, t.anchors)).map((x) => x.msg);
    assert.ok(extra.some((m) => m.startsWith('de/archive/old.svg')), extra.join('; '));
    assert.ok(extra.some((m) => m.startsWith('de/archive/deep/old.svg')));
    assert.ok(extra.some((m) => m.startsWith('upper.SVG is not a drawing of the generator')), extra.join('; '));
    assert.ok(extra.some((m) => m.startsWith('de/mixed.Svg is not a drawing of the generator')));
    // A name that only ends in the letters is no SVG file.
    writeFileSync(join(t.svg, 'notsvg'), 'x');
    assert.ok(!('notsvg' in readSvgs(t.svg)));
    assert.equal(writeFiles(good, t.svg, t.anchors), 49);
    assert.ok(!existsSync(join(t.svg, 'upper.SVG')));
    assert.ok(!existsSync(join(t.svg, 'de', 'mixed.Svg')));
    assert.ok(existsSync(join(t.svg, 'notsvg')));
    assert.deepEqual(readFiles(t.svg, t.anchors), good);
    assert.ok(!existsSync(join(t.svg, 'old.svg')));
    assert.ok(!existsSync(join(t.svg, 'de', 'old.svg')));
    assert.ok(!existsSync(join(t.svg, 'de', 'archive', 'old.svg')));
    assert.ok(!existsSync(join(t.svg, 'de', 'archive', 'deep', 'old.svg')));
    assert.ok(existsSync(join(t.svg, 'keep.txt')));
    assert.equal(writeFiles(good, t.svg, t.anchors), 49);
    assert.deepEqual(readFiles(t.svg, t.anchors), good);
  } finally {
    rmSync(t.dir, { recursive: true, force: true });
  }
});

test('files that drifted on disk fail the check', () => {
  const t = temp();
  try {
    writeFiles(good, t.svg, t.anchors);
    writeFileSync(join(t.svg, 'tap.svg'), `${good.svgs['tap.svg']}\n`);
    rmSync(join(t.svg, 'de', 'usb.svg'));
    const f = checkAll(CATALOGUE, readFiles(t.svg, t.anchors), good);
    assert.ok(f.some((x) => x.id === 'drift' && x.msg.startsWith('tap.svg differs')));
    assert.ok(f.some((x) => x.id === 'drift' && x.msg.startsWith('de/usb.svg is missing')));
    assert.ok(f.some((x) => x.id === 'svg' && x.msg.startsWith('de/usb.svg: file is missing')));
  } finally {
    rmSync(t.dir, { recursive: true, force: true });
  }
});
