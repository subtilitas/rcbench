"""skip_or_fail, import_or_fail: skip locally, fail in CI.

A missing tool or module skips a case outside CI and fails it when CI is
set.

SPDX-License-Identifier: MIT
"""

import pytest
from _pytest.outcomes import Failed, Skipped

from conftest import import_or_fail, skip_or_fail


def test_skips_outside_ci(monkeypatch):
    """Skips outside CI."""
    monkeypatch.delenv("CI", raising=False)
    with pytest.raises(Skipped):
        skip_or_fail("cc is not on PATH")


def test_fails_in_ci(monkeypatch):
    """Fails in CI."""
    monkeypatch.setenv("CI", "true")
    with pytest.raises(Failed, match="cc is not on PATH"):
        skip_or_fail("cc is not on PATH")


def test_a_missing_module_skips_outside_ci(monkeypatch):
    """Skips outside CI."""
    monkeypatch.delenv("CI", raising=False)
    with pytest.raises(Skipped):
        import_or_fail("no_such_module_xyz", "needs xyz")


def test_a_missing_module_fails_in_ci(monkeypatch):
    """Fails in CI."""
    monkeypatch.setenv("CI", "true")
    with pytest.raises(Failed, match="needs xyz"):
        import_or_fail("no_such_module_xyz", "needs xyz")


def test_a_module_that_is_there_is_returned(monkeypatch):
    """Returns the module, in CI and outside it."""
    import json
    monkeypatch.setenv("CI", "true")
    assert import_or_fail("json", "needs json") is json
