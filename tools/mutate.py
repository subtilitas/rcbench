#!/usr/bin/env python3
"""Mutate the lines a change touches and report what the host suite misses.

A test that runs a line does not have to notice when the line is wrong.
This tool changes one line of shared/ at a time, builds the host suite and
runs it.  A mutant the suite fails is killed.  A mutant the suite passes
survived: no test tells the changed line from the original.

    python3 tools/mutate.py                    # changed since origin/main
    python3 tools/mutate.py --base v0.14.0     # since another ref
    python3 tools/mutate.py --files shared/safety/heartbeat.c
                                               # every line of these files
    python3 tools/mutate.py --list             # the mutants, without a run

The lines are those the working tree changes against the merge base of
HEAD and --base, in the C files and headers under shared/.  Three kinds of
mutant, each a change to one line:

- flip: a comparison becomes its neighbour, `<` and `<=`, `>` and `>=`,
  `==` and `!=`;
- bound: an integer or an upper-case constant beside a comparison, or the
  integer a #define gives, plus 1 and minus 1;
- drop: an assignment through a pointer, to a member or to an element is
  taken out.

Comments, string literals and preprocessor lines other than #define are
left alone.  The same tree and arguments give the same mutants in the same
order: there is no random choice.  Above --max-mutants (60) the tool takes
every k-th mutant of the sorted list.

The working tree is never written.  The tool copies shared/, test/host,
firmware/iomcu and tools/gen_esc_profiles.py into a temporary directory,
builds there, and removes the directory on every way out: the end of the
run, an error, Ctrl-C and SIGTERM.

Run time is bounded.  The unmutated configure, build and suite run first,
at most --timeout (300 s) each.  No mutant starts after --budget (1200 s)
of mutants, and one mutant's build and suite run are stopped after
--timeout each, so a run ends within the budget plus five timeouts: 2700 s
with the defaults.  A mutant whose suite run is stopped counts as killed:
it hangs.  The mutants left when the budget ends are reported as not run.

The exit code is 0 whatever survives: the result is a report.  With
--max-survivors N it is 1 when more than N survive.  It is 2 when the tool
cannot run: git fails, a file named is missing, the run is interrupted, or
the unmutated suite does not build or pass.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass

REPO = pathlib.Path(__file__).resolve().parent.parent

# What the host build reads: the code under test, the suite, the
# coprocessor's pin header and PIO program, which two tests read, and the
# profile generator, which the esc_parity test holds the card reader to.
COPIED = ("shared", "test/host", "firmware/iomcu",
          "tools/gen_esc_profiles.py")
# Where a mutant is made.
MUTATED = "shared"
SUFFIXES = (".c", ".h")

DEFAULT_BASE = "origin/main"
DEFAULT_MAX = 60
DEFAULT_BUDGET_S = 1200
DEFAULT_TIMEOUT_S = 300


@dataclass(frozen=True, order=True)
class Mutant:
    path: str       # relative to the repository
    line: int       # 1-based
    column: int     # 0-based, where the changed text starts
    kind: str       # flip, bound or drop
    before: str     # the text replaced
    after: str      # what replaces it
    text: str       # the whole mutated line

    def label(self) -> str:
        return (f"{self.path}:{self.line}  {self.kind}  "
                f"`{self.before}` -> `{self.after}`")


# ------------------------------------------------------------ the lines --

HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def changed_lines(diff: str) -> dict[str, set[int]]:
    """{path: line numbers} of the lines a unified diff adds or changes,
    read from its hunk headers; the diff is made with -U0."""
    out: dict[str, set[int]] = {}
    path = None
    for line in diff.splitlines():
        if line.startswith("+++ "):
            name = line[4:].strip()
            path = None if name == "/dev/null" else name.removeprefix("b/")
            continue
        m = HUNK.match(line)
        if m and path is not None:
            start, count = int(m.group(1)), int(m.group(2) or "1")
            out.setdefault(path, set()).update(range(start, start + count))
    return {p: n for p, n in out.items() if n}


def mask(text: str) -> str:
    """@p text with every comment, string literal and character literal
    blanked to spaces, line breaks kept, so a column in the result is the
    same column in the source."""
    out = []
    i, n = 0, len(text)
    state = "code"
    while i < n:
        ch = text[i]
        two = text[i:i + 2]
        if state == "code":
            if two == "/*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if two == "//":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if ch in "\"'":
                state = ch
                out.append(" ")
                i += 1
                continue
            out.append(ch)
        elif state == "block":
            if two == "*/":
                state = "code"
                out.append("  ")
                i += 2
                continue
            out.append("\n" if ch == "\n" else " ")
        elif state == "line":
            if ch == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
        else:                                   # in a string or a character
            if ch == "\\" and i + 1 < n:
                out.append("  " if text[i + 1] != "\n" else " \n")
                i += 2
                continue
            if ch == state or ch == "\n":
                state = "code"
            out.append("\n" if ch == "\n" else " ")
        i += 1
    return "".join(out)


# ----------------------------------------------------------- the mutants --

OPERATOR = re.compile(r"<<=|>>=|->|<<|>>|<=|>=|==|!=|<|>")
FLIP = {"<": "<=", "<=": "<", ">": ">=", ">=": ">", "==": "!=", "!=": "=="}

INTEGER = r"(?<![\w.])(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)(?![\w.])"
CONSTANT = r"(?<![\w.>])([A-Z][A-Z0-9_]{2,})(?![\w(])"
# Upper-case names beside a comparison that are no bound.
NOT_BOUNDS = {"NULL"}
RIGHT = re.compile(rf"\s*(?:{INTEGER}|{CONSTANT})")
LEFT = re.compile(rf"(?:{INTEGER}|{CONSTANT})\s*$")
DEFINE = re.compile(rf"^(\s*#\s*define\s+\w+\s+\(?\s*){INTEGER}\s*\)?\s*$")
# A store that outlives the statement: through a pointer, to a member, or
# to an element.  A plain local is left alone: dropping its assignment is
# an unused variable, which does not build.
STORE = re.compile(
    r"^(\s*)(\*?\s*\(?\s*[A-Za-z_]\w*\s*\)?"
    r"(?:\s*(?:->|\.)\s*\w+|\s*\[[^\]]*\])*)\s*=(?!=)[^;]*;\s*$")


def step(literal: str, suffix: str, delta: int) -> str | None:
    """@p literal plus @p delta in the base it is written in; None below
    zero."""
    value = int(literal, 0) if literal[:2].lower() == "0x" else int(literal)
    value += delta
    if value < 0:
        return None
    if literal[:2].lower() == "0x":
        digits = f"{value:0{len(literal) - 2}x}"
        if not literal[2:].islower():
            digits = digits.upper()
        return literal[:2] + digits + suffix
    return f"{value}{suffix}"


def operand_mutants(match: re.Match[str]) -> list[tuple]:
    """(column, before, after) for the operand @p match found, plus 1 and
    minus 1."""
    out = []
    if match.group(1) is not None:
        column = match.start(1)
        before = match.group(1) + match.group(2)
        for delta in (1, -1):
            after = step(match.group(1), match.group(2), delta)
            if after is not None:
                out.append((column, before, after))
    elif match.group(3) not in NOT_BOUNDS:
        column = match.start(3)
        name = match.group(3)
        out.append((column, name, f"({name} + 1)"))
        out.append((column, name, f"({name} - 1)"))
    return out


def line_mutants(path: str, number: int, raw: str, code: str) -> list[Mutant]:
    """Every mutant of one line: @p raw is the line, @p code the same line
    with comments and literals blanked."""
    found: list[tuple[str, int, str, str]] = []
    stripped = code.lstrip()
    if stripped.startswith("#"):
        m = DEFINE.match(code)
        if m is None:
            return []
        column = m.start(2)
        before = m.group(2) + m.group(3)
        for delta in (1, -1):
            after = step(m.group(2), m.group(3), delta)
            if after is not None:
                found.append(("bound", column, before, after))
    else:
        for op in OPERATOR.finditer(code):
            if op.group() not in FLIP:
                continue
            found.append(("flip", op.start(), op.group(), FLIP[op.group()]))
            right = RIGHT.match(code, op.end())
            if right:
                found += [("bound", *t) for t in operand_mutants(right)]
            left = LEFT.search(code, 0, op.start())
            if left:
                found += [("bound", *t) for t in operand_mutants(left)]
        store = STORE.match(code)
        if store and re.search(r"->|\.|\[|^\s*\*", store.group(2)) \
                and code.count("(") == code.count(")"):
            column = len(store.group(1))
            found.append(("drop", column, raw[column:].rstrip(),
                          "(void)0;"))
    out = []
    for kind, column, before, after in sorted(set(found)):
        text = raw[:column] + after + raw[column + len(before):]
        if kind == "drop":
            text = raw[:column] + after
        out.append(Mutant(path, number, column, kind, before, after, text))
    return out


def file_mutants(path: str, text: str,
                 lines: set[int] | None = None) -> list[Mutant]:
    """The mutants of @p text on @p lines, or on every line."""
    raw = text.split("\n")
    code = mask(text).split("\n")
    out = []
    for number, (r, c) in enumerate(zip(raw, code, strict=True), 1):
        if lines is None or number in lines:
            out += line_mutants(path, number, r, c)
    return sorted(out)


def pick(mutants: list[Mutant], limit: int) -> list[Mutant]:
    """At most @p limit of @p mutants, evenly spaced over the list."""
    if limit <= 0 or len(mutants) <= limit:
        return list(mutants)
    return [mutants[i * len(mutants) // limit] for i in range(limit)]


def apply(text: str, mutant: Mutant) -> str:
    lines = text.split("\n")
    lines[mutant.line - 1] = mutant.text
    return "\n".join(lines)


# --------------------------------------------------------------- the run --

class Stop(Exception):
    """The tool cannot go on; the message says why."""


def git(*args: str) -> str:
    proc = subprocess.run(["git", *args], cwd=REPO, capture_output=True,
                          text=True)
    if proc.returncode != 0:
        raise Stop(f"git {' '.join(args)}: {proc.stderr.strip()}")
    return proc.stdout


def run(cmd: list[str], cwd: pathlib.Path, timeout: float) -> tuple:
    """(exit code or None when stopped at @p timeout, output).  The command
    runs in its own process group, so a stop takes the compiler or the test
    it started along."""
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            start_new_session=True)
    try:
        out, _ = proc.communicate(timeout=timeout)
        return proc.returncode, out
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        out, _ = proc.communicate()
        return None, out
    except BaseException:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        raise


FAILED_TEST = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (\S+) .*\*\*\*", re.M)


class Bench:
    """A copy of the host build's sources with a configured build beside
    it."""

    def __init__(self, root: pathlib.Path, jobs: int, timeout: float) -> None:
        self.root = root
        self.build = root / "build"
        self.jobs = jobs
        self.timeout = timeout
        # Set when a build was stopped at the time limit: what it was
        # writing may be half written and newer than its sources.
        self.dirty = False

    def copy(self) -> None:
        names = git("ls-files", "-z", "--cached", "--others",
                    "--exclude-standard", "--", *COPIED).split("\0")
        for name in names:
            src = REPO / name
            if name and src.is_file():
                dst = self.root / name
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)

    def configure(self) -> None:
        code, out = run(["cmake", "-S", str(self.root / "test" / "host"),
                         "-B", str(self.build), "-DCMAKE_BUILD_TYPE=Debug"],
                        self.root, self.timeout)
        if code != 0:
            raise Stop("the copy does not configure:\n" + out[-2000:])

    def compile(self) -> tuple:
        return run(["cmake", "--build", str(self.build), "-j",
                    str(self.jobs)], self.root, self.timeout)

    def test(self) -> tuple:
        return run(["ctest", "--test-dir", str(self.build), "-j",
                    str(self.jobs), "--stop-on-failure", "--timeout",
                    str(int(self.timeout))], self.root, self.timeout)

    def judge(self, mutant: Mutant) -> tuple[str, str]:
        """("killed" | "survived" | "nobuild", detail) for one mutant.  The
        copy holds the original again afterwards."""
        path = self.root / mutant.path
        original = path.read_text(encoding="utf-8")
        if self.dirty:
            code, out = run(["cmake", "--build", str(self.build),
                             "--clean-first", "-j", str(self.jobs)],
                            self.root, self.timeout)
            if code != 0:
                raise Stop("the copy does not rebuild after a build that "
                           "was stopped:\n" + (out or "")[-2000:])
            self.dirty = False
        try:
            path.write_text(apply(original, mutant), encoding="utf-8")
            code, out = self.compile()
            if code is None:
                self.dirty = True
                return "nobuild", "the build was stopped at the time limit"
            if code != 0:
                return "nobuild", "does not compile"
            code, out = self.test()
            if code is None:
                return "killed", "the suite was stopped at the time limit"
            if code == 0:
                return "survived", ""
            m = FAILED_TEST.search(out)
            return "killed", m.group(1) if m else "a test"
        finally:
            path.write_text(original, encoding="utf-8")


def candidates(base: str, files: list[str]) -> list[Mutant]:
    """The mutants of the changed lines, or of every line of @p files."""
    wanted: dict[str, set[int] | None]
    if files:
        wanted = {}
        for name in files:
            path = (pathlib.Path.cwd() / name).resolve()
            if not path.is_file():
                raise Stop(f"{name} is not a file")
            wanted[path.relative_to(REPO).as_posix()] = None
    else:
        fork = git("merge-base", base, "HEAD").strip()
        diff = git("diff", "-U0", "--no-color", "--no-ext-diff", fork,
                   "--", MUTATED)
        wanted = dict(changed_lines(diff))
    out: list[Mutant] = []
    for name in sorted(wanted):
        if not name.startswith(MUTATED + "/") or not name.endswith(SUFFIXES):
            continue
        path = REPO / name
        if path.is_file():
            out += file_mutants(name, path.read_text(encoding="utf-8"),
                                wanted[name])
    return sorted(out)


def summary(results: list[dict], not_run: int, seconds: float) -> str:
    """The result as Markdown, for a pull request's job summary."""
    count = {k: sum(1 for r in results if r["result"] == k)
             for k in ("killed", "survived", "nobuild")}
    lines = ["## Mutation check", "",
             f"{len(results)} mutants in {seconds:.0f} s: "
             f"{count['killed']} killed, {count['survived']} survived, "
             f"{count['nobuild']} did not build, {not_run} not run.", ""]
    survivors = [r for r in results if r["result"] == "survived"]
    if survivors:
        lines += ["Survivors: the host suite passes with each of these "
                  "changes.", "",
                  "| Line | Kind | Change |", "| --- | --- | --- |"]
        for r in survivors:
            lines.append(f"| `{r['path']}:{r['line']}` | {r['kind']} | "
                         f"`{r['before']}` to `{r['after']}` |")
        lines.append("")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default=DEFAULT_BASE,
                    help=f"the ref the change is measured against "
                         f"(default {DEFAULT_BASE})")
    ap.add_argument("--files", nargs="+", default=[], metavar="FILE",
                    help="mutate every line of these files, not a change")
    ap.add_argument("--list", action="store_true",
                    help="print the mutants and stop")
    ap.add_argument("--max-mutants", type=int, default=DEFAULT_MAX,
                    help=f"run at most this many (default {DEFAULT_MAX}; "
                         f"0 for all)")
    ap.add_argument("--budget", type=float, default=DEFAULT_BUDGET_S,
                    help=f"start no mutant after this many seconds "
                         f"(default {DEFAULT_BUDGET_S})")
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S,
                    help=f"stop one build or one suite run after this many "
                         f"seconds (default {DEFAULT_TIMEOUT_S})")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--max-survivors", type=int,
                    help="exit 1 when more than this many survive")
    ap.add_argument("--json", type=pathlib.Path,
                    help="write every result here")
    ap.add_argument("--summary", type=pathlib.Path,
                    help="append the result as Markdown here")
    args = ap.parse_args(argv)

    try:
        every = candidates(args.base, args.files)
    except Stop as stop:
        print(f"mutate: {stop}", file=sys.stderr)
        return 2
    chosen = pick(every, args.max_mutants)
    what = (f"{len(args.files)} file(s)" if args.files
            else f"the lines changed since {args.base}")
    print(f"mutate: {len(every)} mutants in {what}"
          + (f", running {len(chosen)}" if len(chosen) != len(every) else ""))
    if args.list:
        for m in chosen:
            print("  " + m.label())
        return 0

    results: list[dict] = []
    not_run = len(every) - len(chosen)
    started = time.monotonic()
    if chosen:
        def stop_now(signum, _frame):
            raise KeyboardInterrupt(signal.Signals(signum).name)

        old = signal.signal(signal.SIGTERM, stop_now)
        tmp = pathlib.Path(tempfile.mkdtemp(prefix="rcbench-mutate-"))
        try:
            bench = Bench(tmp, args.jobs, args.timeout)
            bench.copy()
            bench.configure()
            code, out = bench.compile()
            if code != 0:
                raise Stop("the unmutated suite does not build:\n"
                           + out[-2000:])
            code, out = bench.test()
            if code != 0:
                raise Stop("the unmutated suite does not pass:\n"
                           + out[-2000:])
            started = time.monotonic()
            for n, mutant in enumerate(chosen):
                if time.monotonic() - started > args.budget:
                    not_run += len(chosen) - n
                    print(f"mutate: the budget of {args.budget:.0f} s is "
                          f"spent")
                    break
                verdict, detail = bench.judge(mutant)
                results.append({**asdict(mutant), "result": verdict,
                                "detail": detail})
                print(f"{verdict.upper():<9}{mutant.label()}"
                      + (f"  ({detail})" if detail else ""), flush=True)
        except Stop as stop:
            print(f"mutate: {stop}", file=sys.stderr)
            return 2
        except KeyboardInterrupt as stop:
            print(f"mutate: stopped ({stop})", file=sys.stderr)
            return 2
        finally:
            signal.signal(signal.SIGTERM, old)
            shutil.rmtree(tmp, ignore_errors=True)
    seconds = time.monotonic() - started

    survived = sum(1 for r in results if r["result"] == "survived")
    killed = sum(1 for r in results if r["result"] == "killed")
    nobuild = sum(1 for r in results if r["result"] == "nobuild")
    print(f"mutate: {len(results)} run in {seconds:.0f} s: {killed} killed, "
          f"{survived} survived, {nobuild} did not build, {not_run} not run")
    if args.json:
        args.json.write_text(json.dumps(
            {"base": args.base, "files": args.files, "seconds":
             round(seconds, 1), "not_run": not_run, "results": results},
            indent=2) + "\n")
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as fh:
            fh.write(summary(results, not_run, seconds) + "\n")
    if args.max_survivors is not None and survived > args.max_survivors:
        print(f"mutate: {survived} survivors, over the ceiling of "
              f"{args.max_survivors}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
