#!/usr/bin/env python3
"""Decide whether a run of pages.yml is a full one or an API-only one.

A full run tests the configurator (core, page checks, smoke, the
Playwright matrix) and builds the help and the API reference.  An API-only
run builds the configurator page without its checks, the help and the API
reference.  A run is a full one when a changed path is one of FULL_PATHS:
the configurator, the help, their inputs and the workflow itself.  An
entry ending in / is a folder and matches every path under it; any other
entry matches itself only.

    tools/pages_changes.py --event push --runs RUNS_JSON
    tools/pages_changes.py --event pull_request --base SHA

On a push, the changes are counted from the head commit of the newest
successful push run of pages.yml on main (RUNS_JSON is the answer of
`GET /repos/{repo}/actions/workflows/pages.yml/runs?branch=main&event=push
&status=success`), so a push after a failed or cancelled run is tested
against what was last published.  On a pull request they are counted from
the merge base with the base commit.  A run with no such commit, or one
that is not an ancestor of HEAD, is a full one, as is any other event.

Prints `full=true` or `full=false` for $GITHUB_OUTPUT, and the reason on
standard error.

SPDX-License-Identifier: MIT
"""

import argparse
import json
import pathlib
import subprocess
import sys
from collections.abc import Callable, Iterable

# The trigger paths of pages.yml less the sources of the API reference.
# test/tools/test_pages_changes.py holds the two lists equal.
FULL_PATHS = (
    "configurator/",
    "shared/link/include/rcbench_version.h",
    ".github/workflows/pages.yml",
    "help/",
    "docs/img/",
    "tools/help_site.py",
    "tools/render_ui.py",
    "shared/ui/include/ui_screen.h",
)


def is_full_path(path: str) -> bool:
    """Tell whether a changed path starts a full run.

    Args:
        path: A path relative to the repository root.

    Returns:
        True when a folder entry of FULL_PATHS is a prefix of it or a file
        entry equals it.
    """
    return any(path.startswith(entry) if entry.endswith("/")
               else path == entry for entry in FULL_PATHS)


def needs_full(paths: Iterable[str]) -> bool:
    """Tell whether a set of changed paths starts a full run.

    Args:
        paths: The changed paths.

    Returns:
        True when any of them is_full_path.
    """
    return any(is_full_path(path) for path in paths)


def pick_base(runs: dict, reachable: Callable[[str], bool]) -> str | None:
    """Pick the commit a push's changes are counted from.

    Args:
        runs: The workflow-runs answer of the GitHub API, newest first.
        reachable: Tells whether a commit is an ancestor of HEAD.

    Returns:
        The head commit of the newest successful run, or None when there
        is none or it is not reachable.
    """
    for run in runs.get("workflow_runs", []):
        if run.get("conclusion") == "success" and run.get("head_sha"):
            sha = run["head_sha"]
            return sha if reachable(sha) else None
    return None


def git(*args: str) -> subprocess.CompletedProcess:
    """Run git in the current directory.

    Args:
        *args: The git arguments.

    Returns:
        The finished process, output as text; a non-zero exit is not
        raised.
    """
    return subprocess.run(("git",) + args, capture_output=True, text=True,
                          check=False)


def is_ancestor(sha: str) -> bool:
    """Tell whether a commit exists and is an ancestor of HEAD.

    Args:
        sha: The commit.

    Returns:
        True when `git merge-base --is-ancestor` says so.
    """
    return git("merge-base", "--is-ancestor", sha, "HEAD").returncode == 0


def changed_paths(since: str, merge_base: bool) -> list[str]:
    """List the paths changed between a commit and HEAD.

    Args:
        since: The older commit.
        merge_base: Count from the merge base of since and HEAD.

    Returns:
        Both sides of every change; a rename is a deletion and an addition.
    """
    spec = f"{since}...HEAD" if merge_base else f"{since}..HEAD"
    out = git("diff", "--name-only", "--no-renames", "--no-ext-diff", spec)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip())
    return [line for line in out.stdout.splitlines() if line]


def decide(event: str, base: str, runs: dict,
           reachable: Callable[[str], bool],
           paths_since: Callable[[str, bool], list[str]]) -> tuple[bool, str]:
    """Decide a run.

    Args:
        event: The GitHub event name.
        base: The pull request's base commit; unused on a push.
        runs: The workflow-runs answer for a push; unused otherwise.
        reachable: Tells whether a commit is an ancestor of HEAD.
        paths_since: Lists the changed paths since a commit (changed_paths).

    Returns:
        Whether the run is a full one, and why.
    """
    if event == "push":
        since = pick_base(runs, reachable)
        if since is None:
            return True, "no reachable successful push run of pages.yml"
        paths = paths_since(since, False)
    elif event == "pull_request":
        if not base or not _exists(base):
            return True, "the base commit is not in the clone"
        paths = paths_since(base, True)
    else:
        return True, f"event {event or '?'}"
    hits = [path for path in paths if is_full_path(path)]
    if hits:
        return True, f"{len(hits)} path(s) of FULL_PATHS changed, " \
            f"first {hits[0]}"
    return False, f"{len(paths)} path(s) changed, none of FULL_PATHS"


def _exists(sha: str) -> bool:
    """Tell whether a commit is in the clone.

    Args:
        sha: The commit.

    Returns:
        True when git can read it as a commit.
    """
    return git("cat-file", "-e", f"{sha}^{{commit}}").returncode == 0


def main(argv: list[str] | None = None) -> int:
    """Print the decision for $GITHUB_OUTPUT.

    Args:
        argv: The arguments; sys.argv[1:] when None.

    Returns:
        0.
    """
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--event", required=True)
    parser.add_argument("--base", default="")
    parser.add_argument("--runs", type=pathlib.Path)
    args = parser.parse_args(argv)
    runs: dict = {}
    if args.runs is not None:
        try:
            runs = json.loads(args.runs.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            runs = {}
    try:
        full, reason = decide(args.event, args.base, runs, is_ancestor,
                              changed_paths)
    except RuntimeError as error:
        full, reason = True, f"git diff failed: {error}"
    print(f"full={'true' if full else 'false'}")
    print(f"pages_changes: {reason}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
