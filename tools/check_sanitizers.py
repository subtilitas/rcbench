#!/usr/bin/env python3
"""Hold the sanitizer build to the code it is meant to instrument.

The suite runs a second time under AddressSanitizer and UBSan
(UndefinedBehaviorSanitizer) to catch what -Werror cannot see: a parser fed
a hostile frame reading off the end of a buffer, a signed shift overflowing,
an unaligned access.  Those faults live in `shared/`, not in the test files
that call it, so an instrumented test executable linked against
uninstrumented libraries checks almost nothing.

CMake gives a directory its copy of COMPILE_OPTIONS as the directory is
added, so flags set after the `add_subdirectory()` calls reach the
executables and none of the shared code.  The build still succeeds and the
tests still pass, which is why this is a check and not a comment.

Every translation unit has to carry the flags below, whole.  A build with
`-fsanitize=address` alone passes the suite and checks no undefined
behaviour; one without `-fno-sanitize-recover=all` prints what UBSan finds
and exits 0, so ctest stays green.  A flag of the same family that is not
in the list fails as well: `-fno-sanitize=undefined` after the first takes
it back.

    tools/check_sanitizers.py            configure and check
    tools/check_sanitizers.py --check    the same; accepted for symmetry

SPDX-License-Identifier: MIT
"""

import json
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / "test" / "host"

# Every translation unit the sanitizer build compiles is expected to carry
# the flags.  Both halves matter: shared/ is where the faults are, and
# test/host/ is where the harness that reaches them lives.
EXPECTED = ("shared", "test/host")

# What ENABLE_SANITIZERS=ON has to put on every compile command: both
# sanitizers, and every finding fatal.  The frame pointer keeps a report's
# stack trace whole.
SANITIZERS = frozenset({"address", "undefined"})
REQUIRED = ("-fno-sanitize-recover=all", "-fno-omit-frame-pointer")
# Flags that switch a sanitizer on or off, or decide whether a finding ends
# the run.
FAMILY = ("-fsanitize", "-fno-sanitize")


def flag_problems(args: list[str]) -> list[str]:
    """What is wrong with one compile command's sanitizer flags, in words;
    empty when it carries exactly the expected ones."""
    enabled: set[str] = set()
    problems = []
    for arg in args:
        if not arg.startswith(FAMILY):
            continue
        if arg.startswith("-fsanitize="):
            enabled |= set(arg.split("=", 1)[1].split(","))
        elif arg not in REQUIRED:
            problems.append(f"carries {arg}")
    for name in sorted(SANITIZERS - enabled):
        problems.append(f"no -fsanitize={name}")
    for name in sorted(enabled - SANITIZERS):
        problems.append(f"carries -fsanitize={name}")
    for flag in REQUIRED:
        if flag not in args:
            problems.append(f"no {flag}")
    return problems


def entry_args(entry: dict) -> list[str]:
    """A compile database entry's command as a list of arguments."""
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry.get("command", ""))


def compile_commands(build: Path) -> list[dict]:
    """Configure the suite with sanitizers on and return its compile db."""
    subprocess.run(
        [
            "cmake",
            "-S",
            str(SUITE),
            "-B",
            str(build),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DENABLE_SANITIZERS=ON",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    db = build / "compile_commands.json"
    if not db.exists():
        sys.exit(f"{db} was not written; cmake produced no compile database")
    return json.loads(db.read_text())


def bucket(path: Path, root: Path = ROOT) -> str | None:
    """Which half of the tree a translation unit belongs to, if either."""
    try:
        rel = path.resolve().relative_to(root).as_posix()
    except ValueError:
        return None
    for name in EXPECTED:
        if rel.startswith(name + "/"):
            return name
    return None


def tally(entries: list[dict], root: Path = ROOT) -> tuple[dict, dict]:
    """({half: [instrumented, compiled]}, {file: what is wrong with its
    flags}) over a compile database."""
    counts = {name: [0, 0] for name in EXPECTED}
    wrong: dict[str, list[str]] = {}
    for entry in entries:
        name = bucket(Path(entry["directory"]) / entry["file"], root)
        if name is None:
            continue
        counts[name][1] += 1
        problems = flag_problems(entry_args(entry))
        if problems:
            wrong[Path(entry["file"]).name] = problems
        else:
            counts[name][0] += 1
    return counts, wrong


def main() -> None:
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and sys.argv[1] != "--check"):
        sys.exit(__doc__)

    if not shutil.which("cmake"):
        sys.exit("cmake is not on PATH")

    with tempfile.TemporaryDirectory(prefix="rcbench-san-") as tmp:
        entries = compile_commands(Path(tmp))

    counts, wrong = tally(entries)

    for name in EXPECTED:
        got, total = counts[name]
        if total == 0:
            sys.exit(f"{name}/ compiled no sources; the check cannot pass")
        print(f"{name}/: {got}/{total} instrumented")

    if wrong:
        print()
        print(f"{len(wrong)} translation units do not carry "
              f"-fsanitize={','.join(sorted(SANITIZERS))} "
              f"{' '.join(REQUIRED)} and nothing else of that family:")
        for name, why in sorted(wrong.items()):
            print(f"  {name}: {'; '.join(why)}")
        sys.exit(
            "\nThe sanitizer job does not check what it is there for. Set "
            "the flags\nbefore the add_subdirectory() calls in "
            "test/host/CMakeLists.txt."
        )

    print("every translation unit in the sanitizer build is instrumented")


if __name__ == "__main__":
    main()
