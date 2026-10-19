#!/usr/bin/env python3
"""Name the kind of a release from its tag: testing or stable.

The minor number decides.  An odd minor number is a testing release
(0.21.x, 0.23.x, 1.3.x), published as a GitHub pre-release and never
marked latest.  An even minor number is a stable release (0.22.x, 1.4.x),
published as a full release and marked latest.  The patch number carries
fixes in either kind.

    tools/release_kind.py TAG [PUBLISHED ...]

prints three lines for $GITHUB_OUTPUT and exits 0:

    kind=testing        kind=stable
    prerelease=true     prerelease=false
    latest=false        latest=true

PUBLISHED are the tags of the full releases, not pre-releases and not
drafts, that GitHub holds already.  A stable tag older than one of them is
published with latest=false, so a late build of an older tag never moves
releases/latest back; the same tag again, a retry, keeps latest=true.  A
published tag out of form is left out of that comparison.

A TAG out of form prints the reason on stderr and exits 1, and exit 2 is
no TAG.  The form is the one the panel's update check
reads (shared/fwupdate/fw_release.c): `v`, then MAJOR.MINOR.PATCH, digits
only, each number from 0 to 65535 in at most 5 digits and without a
leading zero, and nothing after it.

release.yml runs it in its version job, before anything is built, and
publishes by its lines.  github.com/subtilitas/rcbench/releases/latest names
the newest release that is not a pre-release: the newest stable release.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import re
import sys

# The largest value of one number of the tag: a 16-bit field on the panel.
PART_MAX = 65535

_PART = r"(0|[1-9][0-9]{0,4})"
_TAG = re.compile(rf"v{_PART}\.{_PART}\.{_PART}")


def parse(tag: str) -> tuple[int, int, int]:
    """(MAJOR, MINOR, PATCH) of `tag`; ValueError when it is out of form."""
    m = _TAG.fullmatch(tag)
    if m is None:
        raise ValueError(f"{tag!r} is not vMAJOR.MINOR.PATCH")
    parts = tuple(int(p) for p in m.groups())
    if any(p > PART_MAX for p in parts):
        raise ValueError(f"{tag!r} has a number above {PART_MAX}")
    return parts  # type: ignore[return-value]


def is_testing(tag: str) -> bool:
    """True for a testing release: an odd minor number."""
    return parse(tag)[1] % 2 == 1


def is_latest(tag: str, published: list[str] = ()) -> bool:
    """True when `tag` is a stable release at or above every tag in
    `published`; tags out of form there are left out."""
    if is_testing(tag):
        return False
    own = parse(tag)
    for other in published:
        try:
            theirs = parse(other)
        except ValueError:
            continue
        if theirs > own:
            return False
    return True


def outputs(tag: str, published: list[str] = ()) -> list[str]:
    """The lines for $GITHUB_OUTPUT."""
    testing = is_testing(tag)
    latest = is_latest(tag, published)
    return [
        f"kind={'testing' if testing else 'stable'}",
        f"prerelease={'true' if testing else 'false'}",
        f"latest={'true' if latest else 'false'}",
    ]


def main(argv: list[str]) -> int:
    if not argv:
        print("usage: tools/release_kind.py TAG [PUBLISHED ...]",
              file=sys.stderr)
        return 2
    try:
        lines = outputs(argv[0], argv[1:])
    except ValueError as e:
        print(f"release_kind: {e}", file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
