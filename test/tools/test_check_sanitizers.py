"""check_sanitizers.py: the flags are held whole, not by a substring.

SPDX-License-Identifier: MIT
"""

import check_sanitizers as cs

GOOD = ["cc", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer", "-g", "-c", "x.c"]


def without(flag):
    """The expected flags without @p flag."""
    return [a for a in GOOD if a != flag]


def test_the_expected_flags_pass():
    """The expected flags pass."""
    assert cs.flag_problems(GOOD) == []


def test_the_two_sanitizers_may_be_given_apart():
    """The two sanitizers may be given apart."""
    args = without("-fsanitize=address,undefined") + [
        "-fsanitize=undefined", "-fsanitize=address"]
    assert cs.flag_problems(args) == []


def test_address_alone_fails():
    """Address alone fails."""
    args = without("-fsanitize=address,undefined") + ["-fsanitize=address"]
    assert cs.flag_problems(args) == ["no -fsanitize=undefined"]


def test_a_build_that_recovers_fails():
    """A build that recovers fails."""
    assert cs.flag_problems(without("-fno-sanitize-recover=all")) == [
        "no -fno-sanitize-recover=all"]


def test_a_flag_that_takes_a_sanitizer_back_fails():
    """A flag that takes a sanitizer back fails."""
    assert cs.flag_problems(GOOD + ["-fno-sanitize=undefined"]) == [
        "carries -fno-sanitize=undefined"]


def test_a_third_sanitizer_fails():
    """A third sanitizer fails."""
    assert cs.flag_problems(GOOD + ["-fsanitize=leak"]) == [
        "carries -fsanitize=leak"]


def test_no_sanitizer_at_all_names_every_missing_flag():
    """No sanitizer at all names every missing flag."""
    assert cs.flag_problems(["cc", "-c", "x.c"]) == [
        "no -fsanitize=address", "no -fsanitize=undefined",
        "no -fno-sanitize-recover=all", "no -fno-omit-frame-pointer"]


def test_an_entry_is_read_as_a_command_or_as_arguments():
    """An entry is read as a command or as arguments."""
    assert cs.entry_args({"command": "cc -DX='a b' -c x.c"}) == [
        "cc", "-DX=a b", "-c", "x.c"]
    assert cs.entry_args({"arguments": ["cc", "-c"]}) == ["cc", "-c"]


def test_tally_counts_each_half_and_names_the_wrong_files(tmp_path):
    """Tally counts each half and names the wrong files."""
    (tmp_path / "shared" / "a").mkdir(parents=True)
    (tmp_path / "test" / "host").mkdir(parents=True)
    good = " ".join(GOOD)
    entries = [
        {"directory": str(tmp_path), "file": "shared/a/a.c", "command": good},
        {"directory": str(tmp_path), "file": "protocols/p/p.c",
         "command": good},
        {"directory": str(tmp_path), "file": "protocols/p/q.c",
         "command": "cc -c q.c"},
        {"directory": str(tmp_path), "file": "shared/a/b.c",
         "command": "cc -fsanitize=address -c b.c"},
        {"directory": str(tmp_path), "file": "test/host/t.c",
         "command": good},
        {"directory": "/elsewhere", "file": "x.c", "command": "cc -c x.c"},
    ]
    counts, wrong = cs.tally(entries, tmp_path)
    assert counts == {"shared": [1, 2], "protocols": [1, 2],
                      "test/host": [1, 1]}
    assert list(wrong) == ["q.c", "b.c"]
    assert "no -fsanitize=undefined" in wrong["b.c"]
