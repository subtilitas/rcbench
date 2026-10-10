# SPDX-License-Identifier: MIT
"""test/host/fuzz_parity.py: a run that compares nothing does not pass."""

import pathlib
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "test" / "host"))
import fuzz_parity as f  # noqa: E402


@pytest.mark.parametrize("count, want", [(1, 1), (19, 1), (20, 1), (39, 1),
                                         (40, 2), (1500, 75)])
def test_each_verdict_is_reached_once_at_least(count, want):
    assert f.least(count) == want


@pytest.mark.parametrize("count", ["0", "-1"])
def test_a_count_under_1_is_refused(count, capsys):
    with pytest.raises(SystemExit) as e:
        f.main(["no-such-binary", "--count", count])
    assert e.value.code == 2
    assert "--count" in capsys.readouterr().err


def test_a_run_in_which_a_verdict_is_missing_fails(monkeypatch, capsys):
    for ok, bad, code in ((0, 5, 1), (5, 0, 1), (1, 4, 0)):
        monkeypatch.setattr(f, "run_seed",
                            lambda *a, ok=ok, bad=bad: ([], ok, bad))
        assert f.main(["no-such-binary", "--seed", "1", "--count", "5"]) \
            == code
    assert "too few cases" in capsys.readouterr().out
