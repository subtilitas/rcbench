"""The tools are scripts in tools/, not a package: put them on the path.

SPDX-License-Identifier: MIT
"""

import faulthandler
import os
import pathlib
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools"))

# Seconds one case may run under the no_hang fixture.  The slowest case of
# test_check_protocols.py runs 2.3s on Linux x86-64.
CASE_LIMIT_S = 30

# A copy of the terminal's stderr, taken before the cases run: pytest
# captures descriptor 2 during a case, and a dump written there is lost
# when the process exits.
_STDERR = []


def pytest_configure(config):
    _STDERR.append(os.dup(2))


def pytest_unconfigure(config):
    os.close(_STDERR.pop())


@pytest.fixture
def no_hang():
    """A case that runs longer than CASE_LIMIT_S writes the stack of every
    thread to stderr and ends the test process with exit status 1, so a
    scan that loops forever fails the run instead of hanging it."""
    faulthandler.dump_traceback_later(CASE_LIMIT_S, exit=True,
                                      file=_STDERR[0])
    yield
    faulthandler.cancel_dump_traceback_later()


def skip_or_fail(reason):
    """Skip a case whose tool is missing; fail it when CI is set, because a
    skipped case exits 0 and the gate it stands for is then not run."""
    import os

    import pytest
    if os.environ.get("CI"):
        pytest.fail(f"{reason} (CI is set: a missing tool fails)")
    pytest.skip(reason)
