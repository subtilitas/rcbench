"""render_diff.py: the views whose render differs between two trees.

The grouping of two manifests, the report, and a whole run on a small git
repository whose tools/render_ui.py draws plain images without a compiler.

SPDX-License-Identifier: MIT
"""

import shutil
import subprocess
import textwrap

import pytest
from conftest import import_or_fail

import render_diff as rd

import_or_fail("PIL", "Pillow is required: pip install pillow")

# A render_ui.py with the parts render_diff.py takes: SCREENS, FIT_ONLY,
# LANGS, build() and render_one().  View "b" is drawn in COLOR, "bad"
# fails, every other view is black.
FAKE = textwrap.dedent('''\
    import subprocess

    SCREENS = {SCREENS}
    FIT_ONLY = {FIT_ONLY}
    LANGS = {{"en": ".", "de": "de"}}
    COLOR = {COLOR}


    def build(tmp):
        return tmp / "exe"


    def render_one(exe, tmp, name, screen, theme, lang="en"):
        from PIL import Image
        if name == "bad":
            raise subprocess.CalledProcessError(1, "render_screen")
        return Image.new("RGB", (4, 2), COLOR if name == "b" else (0, 0, 0))
''')


def manifest(views, failed=()):
    """A manifest of @p views ({key: digest}) and @p failed keys."""
    return {"views": dict(views), "failed": {k: "exit 1" for k in failed}}


def test_identical_manifests_have_nothing_to_list():
    """Two equal manifests list nothing and count every view."""
    m = manifest({"en/a": "1", "de/a": "1"})
    groups = rd.compare(m, m)
    assert groups == {"differ": [], "head_only": [], "base_only": [],
                      "failed": [], "same": 2}


def test_a_changed_view_is_listed_per_language():
    """A view whose digest moved is listed in each language it moved in."""
    base = manifest({"en/a": "1", "de/a": "1", "en/b": "2", "de/b": "2"})
    head = manifest({"en/a": "1", "de/a": "9", "en/b": "2", "de/b": "2"})
    groups = rd.compare(base, head)
    assert groups["differ"] == ["de/a"]
    assert groups["same"] == 3


def test_new_gone_and_failed_views_are_grouped():
    """A view only one side has, and one that failed, each in its group."""
    base = manifest({"en/a": "1", "en/old": "2"}, failed=["en/x"])
    head = manifest({"en/a": "1", "en/new": "3"}, failed=["en/old"])
    groups = rd.compare(base, head)
    assert groups["head_only"] == ["en/new"]
    assert groups["base_only"] == []
    assert groups["failed"] == ["base: en/x", "head: en/old"]


def test_the_report_names_every_group_and_the_counts():
    """The report has a line per listed view and the totals."""
    groups = {"differ": ["en/a"], "head_only": ["de/n"], "base_only": [],
              "failed": [], "same": 5}
    text = rd.report("origin/main", "0123456789abcdef", groups)
    assert text.splitlines() == [
        "render_diff: origin/main (012345678) against the working tree",
        "pixels differ (1):",
        "  en/a",
        "only in the working tree (1):",
        "  de/n",
        "5 identical, 1 differ, 1 new, 0 gone, 0 failed",
    ]


def write_fake(repo, screens, fit_only, color):
    """Write the fake render_ui.py into @p repo's tools/."""
    (repo / "tools").mkdir(exist_ok=True)
    (repo / "tools" / "render_ui.py").write_text(FAKE.format(
        SCREENS=repr(screens), FIT_ONLY=repr(fit_only), COLOR=repr(color)))


def git(repo, *args):
    """Run git in @p repo with a fixed author."""
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=t",
                    "-c", "user.email=t@t", *args], check=True,
                   capture_output=True)


def test_a_run_compares_the_base_commit_with_the_working_tree(tmp_path,
                                                              no_hang):
    """A whole run: one view redrawn, one added, one gone, one failing.

    The base commit's views are drawn in a worktree that is gone after the
    run.
    """
    if shutil.which("git") is None:
        pytest.skip("git is not on PATH")
    repo = tmp_path / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    view = ("x.png", "s", "dark")
    write_fake(repo, {"a": view, "b": view}, {"c": (None, "s", "dark")},
               (1, 2, 3))
    git(repo, "add", ".")
    git(repo, "commit", "-q", "-m", "base")
    write_fake(repo, {"a": view, "b": view, "d": view, "bad": view}, {},
               (4, 5, 6))

    keep = tmp_path / "keep"
    sha, groups = rd.run(repo, "HEAD", 2, keep)

    assert len(sha) == 40
    assert groups == {
        "differ": ["de/b", "en/b"],
        "head_only": ["de/d", "en/d"],
        "base_only": ["de/c", "en/c"],
        "failed": ["head: de/bad", "head: en/bad"],
        "same": 2,
    }
    assert (keep / "base" / "en" / "b.png").exists()
    assert (keep / "head" / "de" / "d.png").exists()
    listed = subprocess.run(["git", "-C", str(repo), "worktree", "list"],
                            check=True, capture_output=True,
                            text=True).stdout
    assert len(listed.splitlines()) == 1


def test_an_unknown_base_fails(tmp_path):
    """A base git does not know ends the run with git's error."""
    if shutil.which("git") is None:
        pytest.skip("git is not on PATH")
    repo = tmp_path / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    with pytest.raises(subprocess.CalledProcessError):
        rd.run(repo, "no-such-commit", 1)
