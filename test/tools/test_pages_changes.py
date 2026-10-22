"""pages_changes.py: a full run of pages.yml or an API-only one.

SPDX-License-Identifier: MIT
"""

import pathlib
import re

import pytest

import pages_changes as pc

REPO = pathlib.Path(__file__).resolve().parent.parent.parent

# The paths of merged pull requests #345, #239 and #233: their only full-run
# inputs are files directly in a FULL_PATHS folder.
PR_345 = """
.github/workflows/ci.yml CHANGELOG.md NOTICE README-de.md README.md
SECURITY.md STATUS.md configurator/README.md docs/Bringup-de.md
docs/Building-de.md docs/Building.md docs/DShot-de.md docs/FirstRun-de.md
docs/FirstRun.md docs/Home-de.md docs/Home.md docs/Link-de.md docs/Link.md
docs/Manifest-de.md docs/Manifest.md docs/OpenYGE-de.md docs/OpenYGE.md
docs/Safety-de.md docs/Safety.md docs/Servo-de.md docs/Servo.md
docs/ServoProgrammer-de.md firmware/iomcu/include/iomcu_pins.h
firmware/iomcu/src/main.c firmware/panel/CMakeLists.txt
firmware/panel/main/knob_task.c protocols/dshot/README.md
protocols/dshot/include/dshot.h protocols/kst/README.md
shared/servo/include/servo_limit.h shared/servo/include/servo_sync.h
shared/ui/include/ui_plot.h testbench/README.md
""".split()
PR_239 = """
CHANGELOG.md STATUS.md docs/Link-de.md docs/Link.md docs/Screens-de.md
docs/Screens.md docs/img/splash.png firmware/iomcu/src/main.c
firmware/iomcu/src/pd_uart.c firmware/iomcu/src/pd_uart.h
firmware/panel/main/main.c shared/bench/include/pdmini.h
shared/bench/include/supply_link.h shared/bench/pdmini.c
shared/bench/supply_link.c shared/link/include/link_pages.h
shared/outputs/include/supply_page.h shared/outputs/supply_page.c
shared/settings/settings.c test/host/test_supply_link.c
test/host/test_supply_page.c
""".split()
PR_233 = """
CHANGELOG.md STATUS.md docs/Link-de.md docs/Link.md docs/img/splash.png
firmware/iomcu/CMakeLists.txt firmware/iomcu/src/main.c
firmware/iomcu/src/out_store.c firmware/iomcu/src/out_store.h
firmware/iomcu/src/pd_uart.c firmware/iomcu/src/pd_uart.h
firmware/iomcu/src/pd_uart.pio shared/bench/include/pdmini.h
shared/bench/pdmini.c shared/link/include/link_can.h
shared/link/include/link_pages.h shared/link/link_can.c
shared/outputs/CMakeLists.txt shared/outputs/include/supply_page.h
shared/outputs/supply_page.c test/host/CMakeLists.txt
test/host/test_link_can.c test/host/test_pdmini.c
test/host/test_supply_page.c tools/coverage.py
""".split()


@pytest.mark.parametrize("path, full", [
    ("configurator/package.json", True),
    ("configurator/package-lock.json", True),
    ("configurator/vite.config.ts", True),
    ("configurator/README.md", True),
    ("configurator/src/app.ts", True),
    ("docs/img/x.png", True),
    ("docs/img/de/x.png", True),
    ("help/content/en/home.md", True),
    (".github/workflows/pages.yml", True),
    ("shared/ui/include/ui_screen.h", True),
    ("shared/link/include/rcbench_version.h", True),
    ("tools/render_ui.py", True),
    ("shared/x.c", False),
    ("firmware/panel/main/main.c", False),
    ("Doxyfile", False),
    ("tools/install_doxygen.sh", False),
    ("docs/Building.md", False),
    ("configurator", False),
    ("configurator.md", False),
    ("tools/render_ui.pyc", False),
    ("shared/ui/include/ui_screen.h.orig", False),
])
def test_a_folder_entry_matches_every_path_under_it_and_a_file_itself(
        path, full):
    """A folder entry is a prefix, top-level files included."""
    assert pc.is_full_path(path) is full


@pytest.mark.parametrize("paths", [PR_345, PR_239, PR_233],
                         ids=["345", "239", "233"])
def test_the_merged_pull_requests_with_top_level_inputs_run_full(paths):
    """configurator/README.md and docs/img/splash.png start a full run."""
    assert pc.needs_full(paths)


def test_c_sources_alone_run_api_only():
    """A change of the C sources, the Doxyfile and the docs is API only."""
    assert not pc.needs_full(["shared/x.c", "firmware/panel/main/main.c",
                              "Doxyfile", "docs/Building.md"])


def runs(*items):
    """Make a workflow-runs answer.

    Args:
        *items: (head_sha, conclusion) pairs, newest first.

    Returns:
        The answer as the GitHub API gives it.
    """
    return {"workflow_runs": [{"head_sha": sha, "conclusion": conclusion}
                              for sha, conclusion in items]}


@pytest.mark.parametrize("answer, reachable, expected", [
    (runs(("b", "success"), ("a", "success")), {"a", "b"}, "b"),
    (runs(("c", "failure"), ("b", "success")), {"b", "c"}, "b"),
    (runs(("b", "success")), set(), None),
    (runs(), {"a"}, None),
    ({}, {"a"}, None),
])
def test_a_push_counts_from_the_newest_successful_run(answer, reachable,
                                                      expected):
    """A push counts from the newest successful run's commit.

    None when there is none or it is not an ancestor of HEAD.
    """
    assert pc.pick_base(answer, lambda sha: sha in reachable) == expected


def fake_diff(paths):
    """Make a paths_since that returns fixed paths and records its call.

    Args:
        paths: The paths to return.

    Returns:
        The function and the list its calls are appended to.
    """
    calls = []

    def paths_since(since, merge_base):
        calls.append((since, merge_base))
        return paths
    return paths_since, calls


def test_a_push_of_c_sources_after_a_successful_run_is_api_only():
    """The diff runs from the last successful run's commit, two dots."""
    paths_since, calls = fake_diff(["shared/x.c"])
    full, _ = pc.decide("push", "", runs(("s", "success")),
                        lambda sha: True, paths_since)
    assert (full, calls) == (False, [("s", False)])


def test_a_push_with_no_successful_run_is_full():
    """No base: everything is tested."""
    paths_since, calls = fake_diff([])
    full, reason = pc.decide("push", "", runs(("f", "failure")),
                             lambda sha: True, paths_since)
    assert full and calls == []
    assert "no reachable" in reason


def test_a_pull_request_counts_from_the_merge_base(monkeypatch):
    """Three dots from the base commit."""
    monkeypatch.setattr(pc, "_exists", lambda sha: sha == "base")
    paths_since, calls = fake_diff(["configurator/package.json"])
    full, _ = pc.decide("pull_request", "base", {}, lambda sha: True,
                        paths_since)
    assert (full, calls) == (True, [("base", True)])


def test_another_event_is_full():
    """A manual start tests everything."""
    paths_since, calls = fake_diff([])
    assert pc.decide("workflow_dispatch", "", {}, lambda sha: True,
                     paths_since)[0]
    assert calls == []


def test_full_paths_are_the_workflow_triggers_less_the_api_sources():
    """FULL_PATHS is the trigger list of pages.yml less the API sources.

    Every other trigger path starts the configurator tests and the
    Playwright matrix; a path missing from FULL_PATHS would skip them.
    """
    text = (REPO / ".github" / "workflows" / "pages.yml").read_text(
        encoding="utf-8")
    triggers = text.split("pull_request:", 1)[1].split("push:", 1)[0]
    paths = {p.replace("/**", "/")
             for p in re.findall(r"- '([^']+)'", triggers)}
    assert set(pc.FULL_PATHS) == paths - {"Doxyfile",
                                          "tools/install_doxygen.sh"}
