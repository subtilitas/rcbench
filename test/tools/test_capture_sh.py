"""testbench/host/capture.sh: the warning for a channel that never changed.

sigrok-cli is replaced by a stub that prints a prepared bits listing for the
read-back call."""

import os
import re
import shutil
import stat
import subprocess

from conftest import REPO

SCRIPT = REPO / "testbench" / "host" / "capture.sh"

STUB = """#!/usr/bin/env bash
for a in "$@"; do
    [ "$a" = --input-file ] && exec cat "$FAKE_BITS"
done
exit 0
"""


STUB_FAILING = """#!/usr/bin/env bash
for a in "$@"; do
    [ "$a" = --input-file ] && { echo "device gone" >&2; exit 1; }
done
exit 0
"""


def rows(name, bits):
    """The bits formatter's layout: 64 samples a row, groups of 8."""
    out = []
    for i in range(0, len(bits), 64):
        chunk = bits[i:i + 64]
        groups = " ".join(chunk[j:j + 8] for j in range(0, len(chunk), 8))
        out.append(f"{name}:{groups}")
    return out


def listing(channels):
    lines = [r for name, bits in channels for r in rows(name, bits)]
    return "\n".join(lines) + "\n"


def run(tmp_path, text, timeout=60, stub=STUB):
    host = tmp_path / "testbench" / "host"
    host.mkdir(parents=True)
    (tmp_path / "testbench" / "captures").mkdir()
    shutil.copy(SCRIPT, host / "capture.sh")
    bindir = tmp_path / "bin"
    bindir.mkdir()
    cli = bindir / "sigrok-cli"
    cli.write_text(stub)
    cli.chmod(cli.stat().st_mode | stat.S_IXUSR)
    bits = tmp_path / "bits.txt"
    bits.write_text(text)
    path = f"{bindir}:{os.environ['PATH']}"
    env = dict(os.environ, FAKE_BITS=str(bits), PATH=path)
    cmd = ["bash", str(host / "capture.sh"), "t", "D0", "1m", "1", "1.65"]
    done = subprocess.run(cmd, env=env, capture_output=True, text=True,
                          check=True, timeout=timeout)
    assert re.fullmatch(r".*/captures/t-1\.65v-\d{8}T\d{6}Z\.sr\n",
                        done.stdout)
    return done.stderr


def never_changed(stderr):
    m = re.fullmatch(
        r"warning: never changed: (.*) -- probe, threshold or ground\n",
        stderr)
    return set(m.group(1).split()) if m else None


N = 1000


def test_a_flat_channel_is_named_high_or_low(tmp_path):
    err = run(tmp_path, listing([("D0", "1" * N), ("D1", "0" * N)]))
    assert never_changed(err) == {"D0", "D1"}


def test_a_toggling_channel_is_not_named(tmp_path):
    assert run(tmp_path, listing([("D0", "01" * (N // 2))])) == ""


def test_a_change_at_the_last_sample_counts(tmp_path):
    err = run(tmp_path, listing([("D2", "0" * (N - 1) + "1"),
                                 ("D3", "1" * (N - 1) + "0")]))
    assert err == ""


def test_a_change_at_the_first_sample_counts(tmp_path):
    assert run(tmp_path, listing([("D2", "1" + "0" * (N - 1))])) == ""


def test_an_empty_capture_is_reported(tmp_path):
    err = run(tmp_path, "")
    assert err.startswith("warning: no samples in the capture")


def test_a_failed_read_back_is_reported_and_the_path_is_printed(tmp_path):
    err = run(tmp_path, "", stub=STUB_FAILING)
    assert err.startswith("warning: read-back of ")
    assert "device gone" in err


def test_only_the_flat_channels_among_several_are_named(tmp_path):
    err = run(tmp_path, listing([("D0", "1" * N), ("D1", "01" * (N // 2)),
                                 ("D4", "0" * N),
                                 ("D10", "1" * 500 + "0" * 500)]))
    assert never_changed(err) == {"D0", "D4"}


def test_a_channel_name_with_digits_is_not_a_level(tmp_path):
    assert never_changed(run(tmp_path, listing([("D10", "1" * N)]))) == {"D10"}


def test_4_million_samples_are_checked_within_20_seconds(tmp_path):
    """The check is linear in the samples: 4 M samples of one channel take
    about 1 s."""
    n = 4_000_000
    err = run(tmp_path, listing([("D0", "1" * n)]), timeout=20)
    assert never_changed(err) == {"D0"}
