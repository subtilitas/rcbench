// The release index and the per-release manifests that the deploy writes to
// fw/ beside the page (tools/flash_manifest.py), read and checked.

import { BOARDS, CHIPS, boardById } from './boards.ts';
import type { Chip } from './boards.ts';
import type { EraseRange } from './rp2350.ts';

export const FORMAT = 1;
/** The largest image the page accepts: 32MB, the largest flash of the boards. */
export const MAX_IMAGE = 32 * 1024 * 1024;
const TAG = /^v(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})$/;
const FILE = /^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/;
const SHA256 = /^[0-9a-f]{64}$/;

export type Kind = 'stable' | 'testing';

export interface ReleaseEntry {
  tag: string;
  kind: Kind;
  published: string;
}

export interface ImageEntry {
  board: string;
  chip: Chip;
  file: string;
  size: number;
  sha256: string;
  offset: number;
  format: 'bin' | 'uf2';
  erase: EraseRange[];
}

export interface Manifest {
  tag: string;
  kind: Kind;
  published: string;
  images: ImageEntry[];
}

export class ManifestError extends Error {}

const fail = (where: string, what: string): never => { throw new ManifestError(`${where}: ${what}`); };
const isObj = (x: unknown): x is Record<string, unknown> => typeof x === 'object' && x !== null && !Array.isArray(x);
const isUint = (x: unknown): x is number => typeof x === 'number' && Number.isInteger(x) && x >= 0;

/** Major, minor and patch of a tag; null when it is out of form. */
export function parseTag(tag: string): [number, number, number] | null {
  const m = TAG.exec(tag);
  return m ? [Number(m[1]), Number(m[2]), Number(m[3])] : null;
}

/** The kind the minor number names: odd is a testing release, even a stable one. */
export function kindOf(tag: string): Kind | null {
  const v = parseTag(tag);
  return v ? (v[1] % 2 === 1 ? 'testing' : 'stable') : null;
}

/** Newer tag first. */
export function compareTags(a: string, b: string): number {
  const x = parseTag(a)!;
  const y = parseTag(b)!;
  return y[0] - x[0] || y[1] - x[1] || y[2] - x[2];
}

function readRelease(x: unknown, where: string): ReleaseEntry {
  if (!isObj(x)) return fail(where, 'not an object');
  const tag = x.tag;
  if (typeof tag !== 'string' || !parseTag(tag)) return fail(where, `tag ${String(tag)} is not vMAJOR.MINOR.PATCH`);
  if (x.kind !== kindOf(tag)) return fail(where, `kind ${String(x.kind)} does not match ${tag}`);
  if (typeof x.published !== 'string' || Number.isNaN(Date.parse(x.published))) return fail(where, 'published is no date');
  return { tag, kind: x.kind as Kind, published: x.published };
}

/** Reads fw/index.json: the releases on the page, newest first. */
export function readIndex(json: unknown): ReleaseEntry[] {
  if (!isObj(json)) return fail('index', 'not an object');
  if (json.format !== FORMAT) return fail('index', `format ${String(json.format)}, ${FORMAT} expected`);
  if (!Array.isArray(json.releases)) return fail('index', 'releases is not a list');
  const out = json.releases.map((r, i) => readRelease(r, `index release ${i}`));
  const tags = new Set(out.map((r) => r.tag));
  if (tags.size !== out.length) return fail('index', 'a tag appears twice');
  return out.sort((a, b) => compareTags(a.tag, b.tag));
}

function readImage(x: unknown, where: string): ImageEntry | null {
  if (!isObj(x)) return fail(where, 'not an object');
  const { board, chip, file, size, sha256, offset, format } = x;
  if (typeof board !== 'string' || !/^[a-z0-9-]{1,32}$/.test(board)) return fail(where, 'board is no id');
  if (typeof chip !== 'string' || !CHIPS.includes(chip as Chip)) return fail(where, `chip ${String(chip)} is unknown`);
  const known = boardById(board);
  if (known && known.chip !== chip) return fail(where, `board ${board} is an ${known.chip}, not an ${chip}`);
  if (typeof file !== 'string' || !FILE.test(file)) return fail(where, 'file is no plain file name');
  if (!isUint(size) || size === 0 || size > MAX_IMAGE) return fail(where, `size ${String(size)}`);
  if (typeof sha256 !== 'string' || !SHA256.test(sha256)) return fail(where, 'sha256 is not 64 lowercase hex digits');
  if (offset !== 0) return fail(where, `offset ${String(offset)}: the page writes merged images at 0x0`);
  const want = chip === 'rp2350' ? 'uf2' : 'bin';
  if (format !== want) return fail(where, `format ${String(format)}, ${want} expected for ${chip}`);
  const erase: EraseRange[] = [];
  if (x.erase !== undefined) {
    if (chip !== 'rp2350' || !Array.isArray(x.erase)) return fail(where, 'erase is for an RP2350 image and a list');
    for (const e of x.erase) {
      if (!isObj(e) || !isUint(e.offset) || !isUint(e.size) || e.size === 0
        || e.offset % 4096 !== 0 || e.size % 4096 !== 0) return fail(where, 'erase range is not 4096-byte aligned');
      erase.push({ offset: e.offset, size: e.size });
    }
  }
  // A board of a later page is no error: the image stays out of the lists.
  if (!known) return null;
  return { board, chip: chip as Chip, file, size, sha256, offset, format: want, erase };
}

/** Reads fw/<tag>/manifest.json and holds it to the tag it was fetched for. */
export function readManifest(json: unknown, tag: string): Manifest {
  if (!isObj(json)) return fail('manifest', 'not an object');
  if (json.format !== FORMAT) return fail('manifest', `format ${String(json.format)}, ${FORMAT} expected`);
  const rel = readRelease(json, 'manifest');
  if (rel.tag !== tag) return fail('manifest', `tag ${rel.tag}, ${tag} expected`);
  if (!Array.isArray(json.images)) return fail('manifest', 'images is not a list');
  const images: ImageEntry[] = [];
  const boards = new Set<string>();
  json.images.forEach((x, i) => {
    const img = readImage(x, `manifest image ${i}`);
    if (!img) return;
    if (boards.has(img.board)) fail('manifest', `board ${img.board} twice`);
    boards.add(img.board);
    images.push(img);
  });
  images.sort((a, b) => BOARDS.findIndex((x) => x.id === a.board) - BOARDS.findIndex((x) => x.id === b.board));
  return { ...rel, images };
}

/** The release the page selects first: the newest stable one, else the newest. */
export function defaultRelease(releases: readonly ReleaseEntry[]): ReleaseEntry | undefined {
  return releases.find((r) => r.kind === 'stable') ?? releases[0];
}
