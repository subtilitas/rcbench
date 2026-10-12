"""skip_or_fail: a missing tool skips a case locally and fails it in CI.

SPDX-License-Identifier: MIT
"""

import pytest
from _pytest.outcomes import Failed, Skipped

from conftest import skip_or_fail


def test_skips_outside_ci(monkeypatch):
    monkeypatch.delenv("CI", raising=False)
    with pytest.raises(Skipped):
        skip_or_fail("cc is not on PATH")


def test_fails_in_ci(monkeypatch):
    monkeypatch.setenv("CI", "true")
    with pytest.raises(Failed, match="cc is not on PATH"):
        skip_or_fail("cc is not on PATH")
