// The release the wizard writes. Pure: no browser code.
//
// Simple mode writes one release for the whole visit: the default release of
// the panel image (the newest stable release with a panel image, else the
// newest with one). A board that release does not carry falls back to the
// per-board rule: the newest stable release with an image for the board,
// else the newest with one. Advanced writes the release the visitor picked
// while it carries the board.

import { defaultRelease } from './manifest.ts';
import type { Manifest, ReleaseEntry } from './manifest.ts';

/** The board whose default release sets the release of the visit. */
export const VISIT_BOARD = 'panel';

/** Releases whose manifest carries an image for the board, in the order given (newest first). */
export function releasesFor(board: string, releases: readonly ReleaseEntry[], manifests: ReadonlyMap<string, Manifest>): ReleaseEntry[] {
  return releases.filter((r) => manifests.get(r.tag)?.images.some((i) => i.board === board));
}

/** The per-board rule: the newest stable release with an image for the board, else the newest with one. */
export function boardDefault(board: string, releases: readonly ReleaseEntry[], manifests: ReadonlyMap<string, Manifest>): string | null {
  return defaultRelease(releasesFor(board, releases, manifests))?.tag ?? null;
}

/** The release of the visit: the panel image's default release; null when no release carries a panel image. */
export function visitRelease(releases: readonly ReleaseEntry[], manifests: ReadonlyMap<string, Manifest>): string | null {
  return boardDefault(VISIT_BOARD, releases, manifests);
}

export interface ReleaseChoice {
  /** The release of the visit, from `visitRelease`. */
  visit: string | null;
  /** The release the visitor picked in Advanced; null for none. */
  chosen: string | null;
  advanced: boolean;
}

/**
 * The release the page writes to a board: in Advanced the picked release
 * while it carries the board; else the release of the visit while it carries
 * the board; else the per-board rule. Null when no release carries the board.
 */
export function releaseFor(board: string, releases: readonly ReleaseEntry[], manifests: ReadonlyMap<string, Manifest>, c: ReleaseChoice): string | null {
  const list = releasesFor(board, releases, manifests);
  const carries = (tag: string | null): tag is string => tag !== null && list.some((r) => r.tag === tag);
  if (c.advanced && carries(c.chosen)) return c.chosen;
  if (carries(c.visit)) return c.visit;
  return defaultRelease(list)?.tag ?? null;
}
