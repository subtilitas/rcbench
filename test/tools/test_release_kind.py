"""release_kind.py: an odd minor number is a testing release, published as
a pre-release; an even one is a usable release, marked latest.

SPDX-License-Identifier: MIT
"""

import subprocess
import sys

import pytest

import release_kind as rk

TOOL = rk.__file__


def run(*args):
    return subprocess.run([sys.executable, TOOL, *args],
                          capture_output=True, text=True, check=False)


@pytest.mark.parametrize("tag, kind", [
    ("v0.21.0", "testing"),
    ("v0.21.7", "testing"),
    ("v0.22.0", "usable"),
    ("v0.22.3", "usable"),
    ("v1.3.0", "testing"),
    ("v1.0.0", "usable"),
    ("v0.0.0", "usable"),
    ("v0.1.0", "testing"),
    ("v0.65535.0", "testing"),
    ("v65535.65534.65535", "usable"),
])
def test_the_minor_number_names_the_kind(tag, kind):
    testing = kind == "testing"
    assert rk.is_testing(tag) is testing
    assert rk.outputs(tag) == [
        f"kind={kind}",
        f"prerelease={'true' if testing else 'false'}",
        f"latest={'false' if testing else 'true'}",
    ]


def test_the_patch_number_does_not_change_the_kind():
    for patch in range(0, 12):
        assert rk.is_testing(f"v0.21.{patch}")
        assert not rk.is_testing(f"v0.22.{patch}")


@pytest.mark.parametrize("tag", [
    "",
    "v",
    "0.21.0",            # no v
    "V0.21.0",
    "v0.21",             # two numbers
    "v0.21.0.1",         # four numbers
    "v0.021.0",          # leading zero
    "v00.21.0",
    "v0.21.00",
    "v0.21.0-rc1",       # anything after it
    "v0.21.0 ",
    " v0.21.0",
    "v0.21.0\n",
    "v0.-1.0",
    "v0.2a.0",
    "v0.21.0x",
    "v65536.0.0",        # above 65535
    "v0.65536.0",
    "v0.0.65536",
    "v100000.0.0",       # 6 digits
    "v0.٢١.0",           # digits outside ASCII
])
def test_a_tag_out_of_form_is_refused(tag):
    with pytest.raises(ValueError):
        rk.parse(tag)
    result = run(tag)
    assert result.returncode == 1
    assert result.stdout == ""
    assert "release_kind:" in result.stderr


def test_the_command_prints_the_lines_for_github_output():
    result = run("v0.21.0")
    assert result.returncode == 0
    assert result.stdout == "kind=testing\nprerelease=true\nlatest=false\n"
    result = run("v0.22.0")
    assert result.returncode == 0
    assert result.stdout == "kind=usable\nprerelease=false\nlatest=true\n"


def test_a_wrong_number_of_arguments_is_exit_2():
    assert run().returncode == 2
    assert run("v0.21.0", "v0.22.0").returncode == 2
