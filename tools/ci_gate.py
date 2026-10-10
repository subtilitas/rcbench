#!/usr/bin/env python3
"""Let a publishing workflow go on only when CI passed on its commit.

docs.yml publishes the wiki and release.yml publishes the firmware.  Each
starts on its own trigger, at the same moment as the CI (continuous
integration) run for the same commit, so its first act is to wait for that
run and to stop unless it passed.

    tools/ci_gate.py --repo OWNER/REPO --sha COMMIT

It asks GitHub for the runs of the CI workflow on COMMIT, every 30 s:

- one of them passed: exit 0;
- every one of them ended and none passed: exit 1;
- one is queued or running: wait, at most 5400 s from the start;
- there is none: wait for one to appear, at most 600 s.

A run that is re-run counts as running again.  Only runs started by a push
or by hand count: a pull request's run is listed under the head commit of
its branch and tests that commit merged into the base, which is another
tree.  The query goes through the `gh` command, which reads its token from
GH_TOKEN and needs `actions: read`.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time

WORKFLOW = "ci.yml"
# The events whose run tests the commit it is listed under.
EVENTS = ("push", "workflow_dispatch")
POLL_S = 30
APPEAR_S = 600
TIMEOUT_S = 5400


def verdict(runs: list[dict], waited_s: float, appear_s: float = APPEAR_S,
            timeout_s: float = TIMEOUT_S) -> tuple[str, str]:
    """("pass" | "fail" | "wait", why) for the CI runs on one commit after
    @p waited_s seconds; a run is {"status": ..., "conclusion": ...}."""
    if any(r.get("conclusion") == "success" for r in runs):
        return "pass", "CI passed on this commit"
    if not runs:
        if waited_s >= appear_s:
            return "fail", (f"no CI run exists for this commit after "
                            f"{appear_s:.0f} s")
        return "wait", "no CI run for this commit yet"
    running = [r for r in runs if r.get("status") != "completed"]
    if not running:
        ended = ", ".join(sorted({str(r.get("conclusion")) for r in runs}))
        return "fail", f"CI ended on this commit without a pass: {ended}"
    if waited_s >= timeout_s:
        return "fail", f"CI has not ended after {timeout_s:.0f} s"
    return "wait", f"{len(running)} CI run(s) queued or running"


def ci_runs(repo: str, sha: str, workflow: str) -> list[dict]:
    """The runs of @p workflow on @p sha, as GitHub lists them."""
    proc = subprocess.run(
        ["gh", "api", f"repos/{repo}/actions/workflows/{workflow}/runs"
                      f"?head_sha={sha}&per_page=100"],
        capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"ci_gate: gh api failed: {proc.stderr.strip()}")
    return on_this_tree(json.loads(proc.stdout).get("workflow_runs", []))


def on_this_tree(runs: list[dict]) -> list[dict]:
    """The runs that tested the commit itself, cut down to what verdict()
    reads."""
    return [{"status": r.get("status"), "conclusion": r.get("conclusion")}
            for r in runs if r.get("event") in EVENTS]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", required=True, help="OWNER/REPO")
    ap.add_argument("--sha", required=True, help="the full commit id")
    ap.add_argument("--workflow", default=WORKFLOW)
    args = ap.parse_args(argv)
    # Both reach a URL; neither is taken on trust.
    if not re.fullmatch(r"[0-9a-f]{40}", args.sha):
        sys.exit("ci_gate: --sha is not a full commit id")
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", args.repo):
        sys.exit("ci_gate: --repo is not OWNER/REPO")

    started = time.monotonic()
    said = None
    while True:
        state, why = verdict(ci_runs(args.repo, args.sha, args.workflow),
                             time.monotonic() - started)
        if why != said:
            print(f"ci_gate: {args.sha[:7]}: {why}", flush=True)
            said = why
        if state == "pass":
            return 0
        if state == "fail":
            print(f"::error::{why}; nothing is published from "
                  f"{args.sha[:7]}.")
            return 1
        time.sleep(POLL_S)


if __name__ == "__main__":
    raise SystemExit(main())
