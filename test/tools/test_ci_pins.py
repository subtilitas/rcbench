"""ci.yml: the pinned analyser and the jobs a merge waits for.

SPDX-License-Identifier: MIT
"""

import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
CI = (REPO / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")


def job(name):
    """Return the text of one job of ci.yml.

    Args:
        name: The job's id, as under `jobs:`.

    Returns:
        The lines from the id to the next job's id.
    """
    m = re.search(rf"^  {re.escape(name)}:\n(.*?)(?=^  [a-z][\w-]*:\n|\Z)",
                  CI, re.M | re.S)
    assert m, f"no job {name} in ci.yml"
    return m.group(1)


def test_clang_tidy_is_pinned_by_release_and_hash():
    """The runner's package moves with its image; the wheel does not."""
    analysis = job("analysis")
    assert re.search(r"CLANG_TIDY_VERSION: \d+\.\d+\.\d+\n", analysis)
    assert re.search(r"CLANG_TIDY_SHA256: [0-9a-f]{64}\n", analysis)
    assert "--require-hashes" in analysis
    assert not re.search(r"apt-get install[^\n]*clang-tidy", analysis)


@pytest.mark.parametrize("page", ["CONTRIBUTING.md", "STATUS.md",
                                  "docs/Building.md", "docs/Building-de.md"])
def test_the_pages_name_the_pinned_clang_tidy_release(page):
    """A local run with another release finds other things."""
    [version] = re.findall(r"CLANG_TIDY_VERSION: ([\d.]+)", CI)
    text = (REPO / page).read_text(encoding="utf-8")
    stated = set(re.findall(r"clang-tidy(?: |==)(\d+\.\d+\.\d+)", text))
    assert stated == {version}


def test_no_required_job_waits_for_codecov():
    """The coverage gate runs offline; an outage of Codecov blocks nothing.

    host-tests is the job the branch ruleset requires; it needs the five
    host jobs, and none of them uploads.
    """
    needs = re.search(r"needs: \[([^\]]*)\]", job("host-tests")).group(1)
    names = [n.strip() for n in needs.split(",")]
    assert "coverage-upload" not in names
    for name in names:
        assert "codecov/codecov-action" not in job(name), name
    assert "coverage.py --check" in job("host-coverage")
    assert "codecov/codecov-action" in job("coverage-upload")
