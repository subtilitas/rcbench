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

    tools/check_sanitizers.py            configure and check
    tools/check_sanitizers.py --check    the same; accepted for symmetry
"""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / "test" / "host"

# Every translation unit the sanitizer build compiles is expected to carry
# the flag.  Both halves matter: shared/ is where the faults are, and
# test/host/ is where the harness that reaches them lives.
EXPECTED = ("shared", "test/host")

FLAG = "-fsanitize"


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


def bucket(path: Path) -> str | None:
    """Which half of the tree a translation unit belongs to, if either."""
    try:
        rel = path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return None
    for name in EXPECTED:
        if rel.startswith(name + "/"):
            return name
    return None


def main() -> None:
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and sys.argv[1] != "--check"):
        sys.exit(__doc__)

    if not shutil.which("cmake"):
        sys.exit("cmake is not on PATH")

    with tempfile.TemporaryDirectory(prefix="rcbench-san-") as tmp:
        entries = compile_commands(Path(tmp))

    counts = {name: [0, 0] for name in EXPECTED}
    missing: list[str] = []
    for entry in entries:
        name = bucket(Path(entry["directory"]) / entry["file"])
        if name is None:
            continue
        counts[name][1] += 1
        if FLAG in entry.get("command", entry.get("arguments", "")):
            counts[name][0] += 1
        else:
            missing.append(Path(entry["file"]).name)

    for name in EXPECTED:
        got, total = counts[name]
        if total == 0:
            sys.exit(f"{name}/ compiled no sources; the check cannot pass")
        print(f"{name}/: {got}/{total} instrumented")

    if missing:
        print()
        print(f"{len(missing)} translation units carry no {FLAG}:")
        for name in sorted(set(missing)):
            print(f"  {name}")
        sys.exit(
            "\nThe sanitizer job runs against uninstrumented code. Set the "
            "flags\nbefore the add_subdirectory() calls in "
            "test/host/CMakeLists.txt."
        )

    print("every translation unit in the sanitizer build is instrumented")


if __name__ == "__main__":
    main()
