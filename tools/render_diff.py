#!/usr/bin/env python3
"""List the views whose render differs between a base commit and the tree.

Every view of render_ui.py's SCREENS and FIT_ONLY is drawn in English and
in German twice: once by the renderer of the base commit, checked out in a
temporary git worktree, and once by the renderer of the working tree,
uncommitted changes included.  Each tree draws the views its own
render_ui.py lists, with its own build() and render_one().  The RGB bytes
of the two images of a view are compared through their SHA-256 digests.

    python3 tools/render_diff.py                 # against origin/main
    python3 tools/render_diff.py HEAD~1          # against another commit
    python3 tools/render_diff.py --check         # exit 1 on any difference
    python3 tools/render_diff.py --keep /tmp/rd  # and keep every PNG image

The output names each view as <lang>/<view> in four groups: views whose
pixels differ, views only the working tree has, views only the base has,
and views a renderer failed to draw.  A failed view exits 1; with --check
any view in the first three groups does too.  --keep writes every image as
PNG (Portable Network Graphics) into base/ and head/ under the folder.

The run builds the renderer twice and draws 2 x 2 x (SCREENS + FIT_ONLY)
images, -j at a time in each tree (default: half the processors).

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent

# Variables that steer render_screen.c's video views; a run draws the views
# as the goldens are drawn, without them.
VIDEO_ENV = "RCBENCH_VIDEO_"

# The manifest a tree's render writes: {"en/view": digest} and the views
# that failed.
MANIFEST = "manifest.json"


def render_tree(tree: pathlib.Path, out: pathlib.Path, jobs: int,
                keep: pathlib.Path | None = None) -> int:
    """Draw every view of @p tree in every language into a manifest.

    Imports @p tree's tools/render_ui.py, builds its renderer in @p out and
    writes @p out/manifest.json: the SHA-256 digest of each view's size and
    RGB bytes under "views", and the views that failed under "failed".

    Args:
        tree: The repository tree whose renderer draws.
        out: An empty folder for the renderer and the manifest.
        jobs: Views drawn at once.
        keep: A folder for the PNG image of each view, or None.

    Returns:
        0, also when views failed; they are in the manifest.
    """
    sys.path.insert(0, str(tree / "tools"))
    import render_ui  # noqa: PLC0415  the tree's own copy

    for name in [k for k in os.environ if k.startswith(VIDEO_ENV)]:
        del os.environ[name]
    exe = render_ui.build(out)
    views = {**render_ui.SCREENS, **render_ui.FIT_ONLY}
    jobs_list = [(lang, name) for lang in render_ui.LANGS for name in views]

    def one(job):
        lang, name = job
        _, screen, theme = views[name]
        work = out / "work" / f"{lang}-{name}"
        work.mkdir(parents=True)
        try:
            img = render_ui.render_one(exe, work, name, screen, theme, lang)
            digest = hashlib.sha256(
                f"{img.size[0]}x{img.size[1]}\n".encode()
                + img.tobytes()).hexdigest()
            if keep is not None:
                path = keep / lang / f"{name}.png"
                path.parent.mkdir(parents=True, exist_ok=True)
                img.save(path)
            return f"{lang}/{name}", digest
        except (subprocess.CalledProcessError, OSError) as exc:
            return f"{lang}/{name}", f"failed: {exc}"
        finally:
            shutil.rmtree(work, ignore_errors=True)

    digests = {}
    failed = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for key, value in pool.map(one, jobs_list):
            if value.startswith("failed: "):
                failed[key] = value[len("failed: "):]
            else:
                digests[key] = value
    (out / MANIFEST).write_text(json.dumps({"views": digests,
                                            "failed": failed}, indent=1))
    return 0


def compare(base: dict, head: dict) -> dict:
    """Group the views of two manifests.

    Args:
        base: The base tree's manifest.
        head: The working tree's manifest.

    Returns:
        Sorted lists under "differ", "head_only", "base_only", "failed"
        (each failed view as "base: <view>" or "head: <view>") and the count
        of identical views under "same".
    """
    bv, hv = base["views"], head["views"]
    both = bv.keys() & hv.keys()
    differ = sorted(k for k in both if bv[k] != hv[k])
    failed = sorted([f"base: {k}" for k in base["failed"]]
                    + [f"head: {k}" for k in head["failed"]])
    gone = set(base["failed"]) | set(head["failed"])
    return {
        "differ": differ,
        "head_only": sorted(hv.keys() - bv.keys() - gone),
        "base_only": sorted(bv.keys() - hv.keys() - gone),
        "failed": failed,
        "same": len(both) - len(differ),
    }


@contextlib.contextmanager
def base_worktree(repo: pathlib.Path, rev: str, dest: pathlib.Path):
    """Check @p rev out in a detached worktree at @p dest for the block.

    The worktree is removed and pruned afterwards, also on an error.

    Args:
        repo: The repository.
        rev: Any commit name git takes.
        dest: A path that does not exist yet.

    Yields:
        The full commit hash.
    """
    sha = subprocess.run(["git", "-C", str(repo), "rev-parse", "--verify",
                          f"{rev}^{{commit}}"], check=True, text=True,
                         capture_output=True).stdout.strip()
    subprocess.run(["git", "-C", str(repo), "worktree", "add", "--detach",
                    "--quiet", str(dest), sha], check=True)
    try:
        yield sha
    finally:
        subprocess.run(["git", "-C", str(repo), "worktree", "remove",
                        "--force", str(dest)], check=False)
        subprocess.run(["git", "-C", str(repo), "worktree", "prune"],
                       check=False)


def run(repo: pathlib.Path, rev: str, jobs: int,
        keep: pathlib.Path | None = None) -> tuple[str, dict]:
    """Render both trees and compare them.

    Args:
        repo: The repository; its working tree is the head.
        rev: The base commit.
        jobs: Views drawn at once in each tree.
        keep: A folder for the PNG images, base/ and head/, or None.

    Returns:
        The base's commit hash and compare()'s groups.
    """
    with tempfile.TemporaryDirectory(prefix="render_diff-") as td:
        tmp = pathlib.Path(td)
        with base_worktree(repo, rev, tmp / "base") as sha:
            procs = {}
            for side, tree in (("base", tmp / "base"), ("head", repo)):
                out = tmp / f"out-{side}"
                out.mkdir()
                cmd = [sys.executable, str(pathlib.Path(__file__).resolve()),
                       "--render", str(tree), str(out), "-j", str(jobs)]
                if keep is not None:
                    cmd += ["--keep", str(keep / side)]
                procs[side] = (subprocess.Popen(cmd), out)
            status = {side: p.wait() for side, (p, _) in procs.items()}
            for side, code in status.items():
                if code != 0:
                    raise RuntimeError(f"the {side} tree's render exited "
                                       f"{code}")
            manifests = {side: json.loads((out / MANIFEST).read_text())
                         for side, (_, out) in procs.items()}
    return sha, compare(manifests["base"], manifests["head"])


def report(rev: str, sha: str, groups: dict) -> str:
    """The text of a run's result.

    Args:
        rev: The base as given.
        sha: Its commit hash.
        groups: compare()'s groups.

    Returns:
        The lines to print.
    """
    titles = (("differ", "pixels differ"),
              ("head_only", "only in the working tree"),
              ("base_only", f"only in {rev}"),
              ("failed", "failed to render"))
    lines = [f"render_diff: {rev} ({sha[:9]}) against the working tree"]
    for key, title in titles:
        if groups[key]:
            lines.append(f"{title} ({len(groups[key])}):")
            lines += [f"  {v}" for v in groups[key]]
    lines.append(f"{groups['same']} identical, {len(groups['differ'])} "
                 f"differ, {len(groups['head_only'])} new, "
                 f"{len(groups['base_only'])} gone, "
                 f"{len(groups['failed'])} failed")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    """Compare the renders, or draw one tree with --render.

    Args:
        argv: The arguments; None takes sys.argv.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser(
        description="List the views whose render differs from a commit.")
    ap.add_argument("base", nargs="?", default="origin/main",
                    help="the commit to compare with (default: origin/main)")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when any view differs, is new or is gone")
    ap.add_argument("-j", "--jobs", type=int,
                    default=max(1, (os.cpu_count() or 2) // 2),
                    help="views drawn at once in each tree")
    ap.add_argument("--keep", type=pathlib.Path,
                    help="write every image as PNG into base/ and head/ here")
    ap.add_argument("--render", nargs=2, metavar=("TREE", "OUT"),
                    type=pathlib.Path, help=argparse.SUPPRESS)
    args = ap.parse_args(argv)

    if args.render:
        return render_tree(args.render[0], args.render[1], args.jobs,
                           args.keep)
    if not shutil.which("cc"):
        sys.exit("no C compiler on PATH")
    try:
        sha, groups = run(REPO, args.base, args.jobs, args.keep)
    except (subprocess.CalledProcessError, RuntimeError) as exc:
        print(f"render_diff: {exc}", file=sys.stderr)
        return 1
    print(report(args.base, sha, groups))
    if groups["failed"]:
        return 1
    if args.check and (groups["differ"] or groups["head_only"]
                       or groups["base_only"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
