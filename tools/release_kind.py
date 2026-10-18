#!/usr/bin/env python3
"""Name the kind of a release from its tag: testing or usable.

The minor number decides.  An odd minor number is a testing release
(0.21.x, 0.23.x, 1.3.x), published as a GitHub pre-release and never
marked latest.  An even minor number is a usable release (0.22.x, 1.4.x),
published as a full release and marked latest.  The patch number carries
fixes in either kind.

    tools/release_kind.py TAG

prints three lines for $GITHUB_OUTPUT and exits 0:

    kind=testing        kind=usable
    prerelease=true     prerelease=false
    latest=false        latest=true

A tag out of form prints the reason on stderr and exits 1, and exit 2 is
a wrong number of arguments.  The form is the one the panel's update check
reads (shared/fwupdate/fw_release.c): `v`, then MAJOR.MINOR.PATCH, digits
only, each number from 0 to 65535 in at most 5 digits and without a
leading zero, and nothing after it.

release.yml runs it in its version job, before anything is built, and
publishes by its lines.  github.com/subtilitas/rcbench/releases/latest names
the newest release that is not a pre-release: the newest usable release.

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


def outputs(tag: str) -> list[str]:
    """The lines for $GITHUB_OUTPUT."""
    testing = is_testing(tag)
    return [
        f"kind={'testing' if testing else 'usable'}",
        f"prerelease={'true' if testing else 'false'}",
        f"latest={'false' if testing else 'true'}",
    ]


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print("usage: tools/release_kind.py TAG", file=sys.stderr)
        return 2
    try:
        lines = outputs(argv[0])
    except ValueError as e:
        print(f"release_kind: {e}", file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
