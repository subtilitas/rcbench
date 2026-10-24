#!/usr/bin/env python3
r"""Run clang-tidy over C files and skip the ones whose inputs are unchanged.

A file is checked again unless a pass is on record for the same key.  The
key is a SHA-256 over:

- the clang-tidy version text and the clang-tidy binary;
- the compiler's version text (it lists the dependencies);
- every .clang-tidy file in the tree, and this script;
- the file's compile command, with the tree's path replaced by a token;
- the path and the content of every file the compiler reads for the
  translation unit (`cc -M`): the source, each project header and each
  system header.

Raw content is hashed, not preprocessed text, so a changed comment such as
a NOLINT in a header counts as a change too.  A header change reaches every
file that includes it.  Only a pass is recorded: a failing file runs again
on every call.  A file the compiler cannot list the dependencies of always
runs, and so does a file one of whose inputs cannot be read or whose
compile command names a response file (@FILE) or a specs file, which the
compiler reads without listing them.  clang-tidy
gets no arguments beyond the file and the compile database, so the
compile command alone decides what it reads.

    tools/tidy_cache.py --build test/host/build --cache DIR \
        [--clang-tidy clang-tidy] [-j N] [--prune] [--root DIR] \
        FILE... | -

`-` reads the file names from standard input, one per line.  `--prune`
deletes the records no file of this call matched, so a restored cache
directory does not grow from run to run.  The exit status is 1 when
clang-tidy reports on any file, 2 on a usage error, and 0 otherwise.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# The record of the last run time of each file, in seconds, so the slowest
# files start first and the run does not end on one long file.
TIMES = "times.json"
# A key record's suffix.
PASS = ".pass"
# The line clang prints after a file with filtered-out warnings.
COUNT_LINE = re.compile(r"^\d+ warnings? (and \d+ errors? )?generated\.\s*$")


def sha_file(path: Path, memo: dict[str, str]) -> str:
    """Hashes a file's content once per call.

    Args:
        path: The file.
        memo: Hashes already taken, by path.

    Returns:
        The hex SHA-256 of the content, or "missing" when it cannot be read.
    """
    name = str(path)
    if name not in memo:
        try:
            memo[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError:
            memo[name] = "missing"
    return memo[name]


def tool_text(argv: list[str]) -> str:
    """Runs a tool and returns what it prints.

    Args:
        argv: The command.

    Returns:
        Standard output and standard error, or the error text when the
        tool does not start.
    """
    try:
        done = subprocess.run(argv, capture_output=True, text=True,
                              check=False)
    except OSError as err:
        return f"error: {err}"
    return done.stdout + done.stderr


def config_files(root: Path) -> list[Path]:
    """Lists every .clang-tidy file under the tree.

    Args:
        root: The top of the tree.

    Returns:
        The files, sorted, build directories excluded.
    """
    found = []
    for top, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs
                         if d not in (".git", "build", "node_modules"))
        if ".clang-tidy" in files:
            found.append(Path(top) / ".clang-tidy")
    return sorted(found)


def static_key(clang_tidy: str, compiler: str, root: Path) -> str:
    """Hashes the inputs every file shares.

    Args:
        clang_tidy: The clang-tidy command.
        compiler: The compiler of the compile commands.
        root: The top of the tree.

    Returns:
        The hex SHA-256.
    """
    h = hashlib.sha256()
    h.update(tool_text([clang_tidy, "--version"]).encode())
    binary = shutil.which(clang_tidy)
    if binary:
        h.update(sha_file(Path(os.path.realpath(binary)), {}).encode())
    h.update(tool_text([compiler, "--version"]).encode())
    for cfg in config_files(root):
        h.update(str(cfg.relative_to(root)).encode() + b"\0")
        h.update(cfg.read_bytes())
    h.update(Path(__file__).read_bytes())
    return h.hexdigest()


def command_args(entry: dict) -> list[str]:
    """Reads one compile command as an argument list.

    Args:
        entry: An entry of compile_commands.json.

    Returns:
        The arguments.
    """
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def deps_args(args: list[str]) -> list[str]:
    """Turns a compile command into one that lists its inputs.

    Args:
        args: The compile command.

    Returns:
        The command with -o and -c removed and -M added, writing the list
        to standard output.
    """
    out = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg == "-o":
            skip = True
            continue
        if arg == "-c" or arg.startswith("-o"):
            continue
        out.append(arg)
    return out + ["-M", "-MF", "/dev/stdout"]


def parse_deps(text: str) -> list[str]:
    """Reads the file list of a make rule as `cc -M` writes it.

    The compiler escapes a space and a # in a path with a backslash and a $
    as $$; a backslash before a newline continues the line.

    Args:
        text: The rule.

    Returns:
        The prerequisites, in order, unescaped.
    """
    body = text.replace("\\\n", " ")
    _, _, prereq = body.partition(": ")
    out: list[str] = []
    cur: list[str] = []
    i = 0
    while i < len(prereq):
        ch = prereq[i]
        nxt = prereq[i + 1] if i + 1 < len(prereq) else ""
        if ch == "\\" and nxt in (" ", "#"):
            cur.append(nxt)
            i += 2
            continue
        if ch == "$" and nxt == "$":
            cur.append("$")
            i += 2
            continue
        if ch.isspace():
            if cur:
                out.append("".join(cur))
                cur = []
        else:
            cur.append(ch)
        i += 1
    if cur:
        out.append("".join(cur))
    return out


def unlisted_input(arg: str) -> bool:
    """Tells whether a compiler argument names a file `cc -M` does not list.

    A response file (@FILE) holds more arguments and a specs file
    (-specs=FILE) changes the compiler's own; neither is a prerequisite of
    the make rule, so a change to one would not change the key.

    Args:
        arg: One argument of a compile command.

    Returns:
        True for such an argument.
    """
    return arg.startswith("@") or arg.startswith(("-specs", "--specs"))


def entry_hash(entry: dict, root: Path, memo: dict[str, str]) -> str | None:
    """Hashes one compile command and the files it reads.

    Args:
        entry: The compile command.
        root: The top of the tree.
        memo: Content hashes already taken.

    Returns:
        The hex SHA-256, or None when the inputs cannot be listed.
    """
    args = command_args(entry)
    if any(unlisted_input(a) for a in args):
        return None
    cwd = entry["directory"]
    try:
        done = subprocess.run(deps_args(args), cwd=cwd, capture_output=True,
                              text=True, check=False)
    except OSError:
        return None
    if done.returncode != 0:
        return None
    deps = parse_deps(done.stdout)
    if not deps:
        return None
    top = str(root)
    h = hashlib.sha256()
    _, same = same_command(entry)
    h.update("\0".join(a.replace(top, "@ROOT@") for a in same).encode())
    h.update(b"\0" + cwd.replace(top, "@ROOT@").encode())
    for dep in deps:
        path = Path(os.path.realpath(os.path.join(cwd, dep)))
        h.update(b"\0" + str(path).replace(top, "@ROOT@").encode() + b"\0")
        digest = sha_file(path, memo)
        if digest == "missing":
            return None
        h.update(digest.encode())
    return h.hexdigest()


def file_key(entries: list[dict], base: str, root: Path,
             memo: dict[str, str]) -> str | None:
    """Hashes one file's inputs over every distinct compile command.

    Args:
        entries: Its distinct compile commands.
        base: The shared key.
        root: The top of the tree.
        memo: Content hashes already taken.

    Returns:
        The hex SHA-256, or None when an input list cannot be taken.
    """
    h = hashlib.sha256(base.encode())
    for part in sorted(entry_hash(e, root, memo) or "" for e in entries):
        if not part:
            return None
        h.update(part.encode())
    return h.hexdigest()


def same_command(entry: dict) -> tuple[str, tuple[str, ...]]:
    """Names what makes two compile commands of one file the same check.

    Args:
        entry: An entry of compile_commands.json.

    Returns:
        The directory and the arguments without the output file.
    """
    return entry["directory"], tuple(deps_args(command_args(entry)))


def load_commands(build: Path, root: Path) -> dict[str, list[dict]]:
    """Reads compile_commands.json, one entry per distinct command.

    A file built into several test programs with the same flags has one
    entry per program, and clang-tidy checks it once per entry; those
    entries differ only in the output file and give the same answer.

    Args:
        build: The build directory.
        root: The top of the tree.

    Returns:
        The distinct entries by path relative to the tree.
    """
    entries = json.loads((build / "compile_commands.json").read_text())
    out: dict[str, list[dict]] = {}
    seen: set[tuple[str, tuple[str, tuple[str, ...]]]] = set()
    for entry in entries:
        path = Path(entry["directory"]) / entry["file"]
        rel = os.path.relpath(os.path.realpath(path), root)
        mark = (rel, same_command(entry))
        if mark in seen:
            continue
        seen.add(mark)
        out.setdefault(rel, []).append(entry)
    return out


def read_times(cache: Path) -> dict[str, float]:
    """Reads the recorded run times.

    Args:
        cache: The cache directory.

    Returns:
        Seconds by file; empty when there is no readable record.
    """
    try:
        data = json.loads((cache / TIMES).read_text())
    except (OSError, ValueError):
        return {}
    if not isinstance(data, dict):
        return {}
    return {k: float(v) for k, v in data.items()
            if isinstance(v, (int, float))}


def quiet(text: str) -> str:
    """Drops clang's count lines from clang-tidy's output.

    clang-tidy writes "N warnings generated." for the warnings it then
    filters out by HeaderFilterRegex and the check list; the line carries
    no finding.

    Args:
        text: The output.

    Returns:
        The output without those lines.
    """
    return "".join(line for line in text.splitlines(keepends=True)
                   if not COUNT_LINE.match(line))


def run_one(clang_tidy: str, build: Path, name: str,
            root: Path) -> tuple[str, int, str, float]:
    """Runs clang-tidy over one file.

    Args:
        clang_tidy: The clang-tidy command.
        build: The directory of the compile database to use.
        name: The file, relative to the tree.
        root: The top of the tree.

    Returns:
        The file, the exit status, the output and the seconds it took.
    """
    start = time.monotonic()
    try:
        done = subprocess.run(
            [clang_tidy, "-p", str(build), "--quiet", name],
            cwd=root, capture_output=True, text=True, check=False)
        status, text = done.returncode, done.stdout + done.stderr
    except OSError as err:
        status, text = 1, f"{name}: clang-tidy does not start: {err}\n"
    return name, status, text, time.monotonic() - start


def main(argv: list[str] | None = None) -> int:
    """Checks the files and records the passes.

    Args:
        argv: The arguments; sys.argv when None.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True, type=Path)
    ap.add_argument("--cache", required=True, type=Path)
    ap.add_argument("--clang-tidy", default="clang-tidy")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--prune", action="store_true")
    ap.add_argument("--root", type=Path, default=ROOT,
                    help="the top of the tree; the repository by default")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args(argv)

    root = args.root.resolve()
    names = args.files
    if names == ["-"]:
        names = [line.strip() for line in sys.stdin if line.strip()]
    if not names:
        print("tidy_cache: no files", file=sys.stderr)
        return 2
    build = args.build.resolve()
    try:
        commands = load_commands(build, root)
    except (OSError, ValueError) as err:
        print(f"tidy_cache: {err}", file=sys.stderr)
        return 2
    rels = list(dict.fromkeys(
        os.path.relpath(os.path.realpath(n), root) for n in names))
    missing = [n for n in rels if n not in commands]
    if missing:
        for name in missing:
            print(f"tidy_cache: {name}: no compile command", file=sys.stderr)
        return 2
    jobs = max(1, args.jobs)
    compiler = command_args(commands[rels[0]][0])[0]
    base = static_key(args.clang_tidy, compiler, root)
    args.cache.mkdir(parents=True, exist_ok=True)

    memo: dict[str, str] = {}
    with ThreadPoolExecutor(jobs) as pool:
        keys = dict(zip(rels, pool.map(
            lambda n: file_key(commands[n], base, root, memo), rels),
            strict=True))

    times = read_times(args.cache)
    todo = [n for n in rels
            if keys[n] is None or not (args.cache / (keys[n] + PASS)).exists()]
    # Unknown files first, then the slowest.
    todo.sort(key=lambda n: -times.get(n, float("inf")))
    print(f"tidy_cache: {len(rels)} files, {len(rels) - len(todo)} "
          f"unchanged, {len(todo)} to check", flush=True)

    failed = []
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as tmp, \
            ThreadPoolExecutor(jobs) as pool:
        db = Path(tmp)
        (db / "compile_commands.json").write_text(json.dumps(
            [e for n in todo for e in commands[n]], indent=1))
        runs = pool.map(lambda n: run_one(args.clang_tidy, db, n, root),
                        todo)
        for name, status, text, took in runs:
            times[name] = round(took, 2)
            text = quiet(text)
            if text:
                sys.stdout.write(text)
                sys.stdout.flush()
            if status != 0:
                failed.append(name)
            elif keys[name] is not None:
                (args.cache / (keys[name] + PASS)).write_text(f"{name}\n")

    if args.prune:
        live = {k + PASS for k in keys.values() if k is not None}
        for rec in args.cache.glob("*" + PASS):
            if rec.name not in live:
                rec.unlink()
        times = {k: v for k, v in times.items() if k in keys}
    (args.cache / TIMES).write_text(json.dumps(times, indent=0,
                                               sort_keys=True) + "\n")
    print(f"tidy_cache: checked {len(todo)} in "
          f"{time.monotonic() - start:.1f}s, {len(failed)} failed",
          flush=True)
    for name in failed:
        print(f"tidy_cache: FAILED {name}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
