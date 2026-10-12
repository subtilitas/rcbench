"""tools/research/session.py: the check does not skip silently in CI, and the
owner self-test runs on its own fixture repository.

SPDX-License-Identifier: MIT
"""

import importlib.util
import pathlib
import sys

import pytest

HERE = (pathlib.Path(__file__).resolve().parent.parent.parent
        / "tools" / "research")


@pytest.fixture(scope="module")
def session():
    sys.path.insert(0, str(HERE))
    spec = importlib.util.spec_from_file_location(
        "research_session", HERE / "session.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_owner_selftest_reads_its_own_plan_not_the_working_repository(
        session, monkeypatch):
    # A plan path the working repository does not have: the self-test writes
    # and commits the plan it reads in its throwaway repository.
    monkeypatch.setattr(session, "PLAN_REL",
                        "hardware/docs/Fixture-plan.md")
    assert session.owner_selftest() == []


def _check(session, monkeypatch, capsys, ci):
    monkeypatch.setattr(session.shutil, "which", lambda _name: None)
    if ci:
        monkeypatch.setenv("CI", "true")
    else:
        monkeypatch.delenv("CI", raising=False)
    code = session.cmd_check(None)
    return code, capsys.readouterr().out


def test_check_fails_without_node_when_CI_is_set(session, monkeypatch, capsys):
    code, out = _check(session, monkeypatch, capsys, ci=True)
    assert code == 1
    assert "node is not installed and CI is set" in out


def test_check_skips_the_dry_run_without_node_outside_CI(
        session, monkeypatch, capsys):
    code, out = _check(session, monkeypatch, capsys, ci=False)
    assert code == 0
    assert "not run: node is not installed" in out
