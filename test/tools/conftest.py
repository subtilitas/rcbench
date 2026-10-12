"""The tools are scripts in tools/, not a package: put them on the path.

SPDX-License-Identifier: MIT
"""

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools"))


def skip_or_fail(reason):
    """Skip a case whose tool is missing; fail it when CI is set, because a
    skipped case exits 0 and the gate it stands for is then not run."""
    import os

    import pytest
    if os.environ.get("CI"):
        pytest.fail(f"{reason} (CI is set: a missing tool fails)")
    pytest.skip(reason)
