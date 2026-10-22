#!/usr/bin/env python3
"""Hold the Doxygen comments of the C sources to a baseline.

Runs Doxygen with the repository's Doxyfile and reads its warnings.  Each
warning becomes a file and a message, without the line number, so that
code moving inside a file changes nothing.  Two warnings are added that
Doxygen does not give: a C file or header in the Doxyfile's input with no
`@file` block, and a `@file` block with no `@brief`.  Without a `@file`
block Doxygen documents nothing of a file and warns about none of it.

Two warnings Doxygen gives are dropped: "parameters of member X are not
documented" and "return type of member X is not documented" for a
`static` function in a `.c` file.  A private helper needs a `@brief`
only.  Its missing `@brief` is still a warning.

    tools/doc_check.py --check                 fail on a warning that
                                               tools/doc_check_baseline.txt
                                               does not hold, and on a
                                               baseline entry that no longer
                                               occurs
    tools/doc_check.py --check --base REF      also fail on the warnings of
                                               new and changed code since
                                               the merge base with REF (below)
    tools/doc_check.py --update                rewrite the baseline with the
                                               warnings of the tree
    --log FILE                                 read a Doxygen warning log
                                               instead of running Doxygen
    --doxygen PATH                             the Doxygen binary (default:
                                               doxygen on PATH)
    --out DIR                                  Doxygen's output directory
                                               (default: build/doxygen)

With --base, a warning fails, listed in the baseline or not, when

- its file is new since the merge base (renamed files count as new);
- its file changed and the warning's span overlaps a line range the change
  adds or rewrites (`git diff -U0`, new side; a pure deletion covers the
  line before the cut).  The span is the line Doxygen reports,
  widened by the comment block next to it: Doxygen reports a documented
  item at the start of its block, so a block starting on that line adds
  its lines and the declaration after it; it reports an undocumented item
  at its declaration, so a block ending on the line above adds its lines.
  A declaration over several lines counts by the reported line only;
- its file changed and it is a missing `@file` block or brief;
- it is a `/**` or `/*!` block without `@brief` in a new file or on a
  changed line (NO_BLOCK_BRIEF); a member's trailing `/**<` block, a group
  block (`@{`, `@}`, `@name`) and the `@file` block are exempt.  Doxygen
  takes such a block as the item's detailed text and warns about nothing.

Renamed and moved files count as new: code moved into a new file is held
to the rule whole.

Old gaps on lines a change does not touch stay in the baseline.

Doxygen's warnings differ between releases, so the baseline is written by
DOXYGEN_VERSION only, the release CI installs with
tools/install_doxygen.sh; another release stops the tool before it runs.

SPDX-License-Identifier: MIT
"""

import argparse
import collections
import fnmatch
import pathlib
import re
import shlex
import subprocess
import sys
from typing import NamedTuple

REPO = pathlib.Path(__file__).resolve().parent.parent
DOXYFILE = REPO / "Doxyfile"
BASELINE = REPO / "tools" / "doc_check_baseline.txt"
DEFAULT_OUT = pathlib.Path("build") / "doxygen"

# The Doxygen release the baseline is written by; ci.yml and pages.yml
# install this release (test/tools/test_doc_check.py holds them equal).
DOXYGEN_VERSION = "1.18.0"

BASELINE_HEADER = """\
# Doxygen warnings of the C sources that tools/doc_check.py accepts.
# One warning a line: the file, then the message, without line numbers.
# Written by tools/doc_check.py --update with Doxygen {version}; a fixed gap
# leaves a stale line, which fails --check until the file is rewritten.
"""

NO_FILE_BLOCK = "file has no @file block"
NO_FILE_BRIEF = "@file block has no @brief"
NO_BLOCK_BRIEF = "comment block has no @brief"

_WARNING_RE = re.compile(r"^(?P<file>.*?):(?P<line>\d+): (?P<text>.*)$")
_HELPER_NOISE_RE = re.compile(
    r"^warning: (?:parameters of member (?P<p>\w+) are not documented"
    r"|return type of member (?P<r>\w+) is not documented)$")
_DOC_BLOCK_RE = re.compile(r"/\*[*!](?!/).*?\*/", re.DOTALL)
_FILE_CMD_RE = re.compile(r"[@\\]file\b")
_BRIEF_CMD_RE = re.compile(r"[@\\]brief\b")
_GROUP_CMD_RE = re.compile(r"[@\\](?:\{|\}|name\b)")

# Options for every git diff the tool runs: the a/ and b/ prefixes and
# plain output whatever diff.noprefix, diff.mnemonicPrefix, color.diff or
# an external diff driver in the user's configuration say.
_DIFF_OPTIONS = ("--src-prefix=a/", "--dst-prefix=b/", "--no-color",
                 "--no-ext-diff", "--no-renames")

# How far below a warning's line the function it names may start: the
# warning carries the line of the comment block above the function.
_HELPER_SEARCH_LINES = 60


class DocWarning(NamedTuple):
    """One warning of a run.

    Attributes:
        file: The path relative to the repository root, with forward
            slashes; empty for a warning that names no file.
        line: The line Doxygen gives, 0 for none.  Shown, never compared.
        message: The text after the line number, white space collapsed.
    """

    file: str
    line: int
    message: str

    @property
    def key(self) -> str:
        """The baseline form: the file and the message, no line."""
        return f"{self.file}: {self.message}"


def read_doxyfile(path: pathlib.Path) -> dict[str, list[str]]:
    """Read the settings of a Doxyfile.

    Args:
        path: The Doxyfile.

    Returns:
        Each setting's name mapped to its values, split as a shell splits
        words; `+=` appends to an earlier value.
    """
    settings: dict[str, list[str]] = {}
    logical = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        if not logical and line.lstrip().startswith("#"):
            continue
        if line.rstrip().endswith("\\"):
            logical += line.rstrip()[:-1] + " "
            continue
        logical += line
        match = re.match(r"^\s*([A-Z_]+)\s*(\+?=)\s*(.*)$", logical)
        logical = ""
        if not match:
            continue
        name, op, value = match.groups()
        values = shlex.split(value)
        if op == "+=":
            settings.setdefault(name, []).extend(values)
        else:
            settings[name] = values
    return settings


def in_scope(rel: str, settings: dict[str, list[str]]) -> bool:
    """Tell whether Doxygen reads a file.

    Args:
        rel: The path relative to the repository root.
        settings: The Doxyfile's settings.

    Returns:
        True when the file is under an INPUT directory, its name matches a
        FILE_PATTERNS entry and its path matches no EXCLUDE_PATTERNS entry.
    """
    inputs = settings.get("INPUT", [])
    if not any(rel == top or rel.startswith(top.rstrip("/") + "/")
               for top in inputs):
        return False
    name = rel.rsplit("/", 1)[-1]
    if not any(fnmatch.fnmatchcase(name, pattern)
               for pattern in settings.get("FILE_PATTERNS", ["*"])):
        return False
    # Doxygen matches the patterns against the absolute path, so "*/x/*"
    # matches a top-level x/ as well.
    rooted = "/" + rel
    return not any(fnmatch.fnmatchcase(rooted, pattern)
                   for pattern in settings.get("EXCLUDE_PATTERNS", []))


def git_lines(root: pathlib.Path, *args: str) -> list[str]:
    """Run git in a directory and return its output lines.

    Args:
        root: The working directory.
        *args: The git arguments.

    Returns:
        The non-empty lines of standard output.

    Raises:
        subprocess.CalledProcessError: git exits non-zero.
    """
    out = subprocess.run(("git",) + args, cwd=root, check=True,
                         capture_output=True, text=True).stdout
    return [line for line in out.splitlines() if line]


def source_files(root: pathlib.Path,
                 settings: dict[str, list[str]]) -> list[str]:
    """List the files Doxygen reads that git tracks or would track.

    Args:
        root: The repository root.
        settings: The Doxyfile's settings.

    Returns:
        Paths relative to root, sorted: tracked files and untracked files
        that .gitignore does not exclude.
    """
    files = git_lines(root, "ls-files", "--cached", "--others",
                      "--exclude-standard", "--",
                      *settings.get("INPUT", []))
    return sorted({f for f in files
                   if in_scope(f, settings) and (root / f).is_file()})


def file_block_warnings(root: pathlib.Path,
                        files: list[str]) -> list[DocWarning]:
    """Find the files without a `@file` block or without its `@brief`.

    Args:
        root: The repository root.
        files: Paths relative to root.

    Returns:
        One warning for each file whose doc comments (`/**` or `/*!`) hold
        no `@file`, and one for each whose first `@file` block holds no
        `@brief`.
    """
    found = []
    for rel in files:
        text = (root / rel).read_text(encoding="utf-8", errors="replace")
        block = next((m for m in _DOC_BLOCK_RE.finditer(text)
                      if _FILE_CMD_RE.search(m.group(0))), None)
        if block is None:
            found.append(DocWarning(rel, 1, NO_FILE_BLOCK))
        elif not _BRIEF_CMD_RE.search(block.group(0)):
            line = text.count("\n", 0, block.start()) + 1
            found.append(DocWarning(rel, line, NO_FILE_BRIEF))
    return found


def parse_log(text: str, root: pathlib.Path) -> list[DocWarning]:
    """Read a Doxygen warning log.

    A line that does not start with `file:line:` continues the warning
    above it; a warning that names no file has an empty file.

    Args:
        text: The log as WARN_FORMAT "$file:$line: $text" writes it.
        root: The repository root, stripped from the paths.

    Returns:
        The warnings in log order.
    """
    prefix = root.as_posix().rstrip("/") + "/"
    warnings: list[DocWarning] = []
    for raw in text.splitlines():
        if not raw.strip():
            continue
        match = _WARNING_RE.match(raw)
        if match and match.group("text").startswith(("warning:", "error:")):
            path = match.group("file").replace("\\", "/")
            if path.startswith(prefix):
                path = path[len(prefix):]
            warnings.append(DocWarning(path, int(match.group("line")),
                                    " ".join(match.group("text").split())))
        elif warnings and raw[:1].isspace():
            last = warnings[-1]
            warnings[-1] = last._replace(
                message=last.message + " " + " ".join(raw.split()))
        else:
            warnings.append(DocWarning("", 0, " ".join(raw.split())))
    return warnings


def is_helper_noise(warning: DocWarning, root: pathlib.Path) -> bool:
    """Tell whether a warning asks a static helper for more than a brief.

    Args:
        warning: A warning of the run.
        root: The repository root.

    Returns:
        True for "parameters of member X are not documented" and "return
        type of member X is not documented" when X is a `static` function
        of a `.c` file: the first line at or below the warning's line that
        calls X with an opening parenthesis, or the line above that one,
        starts with `static`.
    """
    match = _HELPER_NOISE_RE.match(warning.message)
    if not match or not warning.file.endswith(".c"):
        return False
    name = match.group("p") or match.group("r")
    path = root / warning.file
    if not path.is_file():
        return False
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    call = re.compile(r"\b" + re.escape(name) + r"\s*\(")
    start = max(warning.line - 1, 0)
    for index in range(start, min(start + _HELPER_SEARCH_LINES, len(lines))):
        if call.search(lines[index]):
            head = lines[index] if index == 0 else (
                lines[index - 1] + "\n" + lines[index])
            return re.search(r"(?m)^\s*static\b", head) is not None
    return False


def doxygen_version(doxygen: str) -> str:
    """Return the release of a Doxygen binary.

    Args:
        doxygen: The binary.

    Returns:
        The release, for example "1.18.0".

    Raises:
        FileNotFoundError: The binary does not exist.
    """
    out = subprocess.run([doxygen, "--version"], check=True,
                         capture_output=True, text=True).stdout
    return out.split()[0] if out.split() else ""


def run_doxygen(doxygen: str, root: pathlib.Path,
                out: pathlib.Path) -> str:
    """Run Doxygen over the tree and return its warning log.

    Args:
        doxygen: The binary.
        root: The repository root, where Doxygen runs.
        out: The output directory; the HTML lands in out/html.

    Returns:
        The text of the warning log.

    Raises:
        subprocess.CalledProcessError: Doxygen exits non-zero.
    """
    out = out if out.is_absolute() else root / out
    out.mkdir(parents=True, exist_ok=True)
    log = out / "warnings.log"
    config = (root / "Doxyfile").read_text(encoding="utf-8")
    config += f'\nOUTPUT_DIRECTORY = "{out}"\nWARN_LOGFILE = "{log}"\n'
    subprocess.run([doxygen, "-"], cwd=root, input=config, text=True,
                   check=True)
    return log.read_text(encoding="utf-8", errors="replace")


def collect(log_text: str, root: pathlib.Path) -> list[DocWarning]:
    """Turn a warning log into the warnings the check compares.

    Args:
        log_text: Doxygen's warning log.
        root: The repository root.

    Returns:
        Doxygen's warnings without the static helper noise, followed by
        the missing `@file` blocks and briefs.
    """
    settings = read_doxyfile(root / "Doxyfile")
    warnings = [w for w in parse_log(log_text, root)
                if not is_helper_noise(w, root)]
    warnings += file_block_warnings(root, source_files(root, settings))
    return warnings


def read_baseline(path: pathlib.Path) -> collections.Counter:
    """Read a baseline file.

    Args:
        path: The file; a missing file is an empty baseline.

    Returns:
        Each "file: message" line with the number of times it occurs.
    """
    if not path.is_file():
        return collections.Counter()
    return collections.Counter(
        line for line in path.read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("#"))


def write_baseline(path: pathlib.Path, warnings: list[DocWarning]) -> None:
    """Write the warnings as the baseline, sorted.

    Args:
        path: The file to write.
        warnings: The warnings of the run.
    """
    lines = sorted(w.key for w in warnings)
    path.write_text(BASELINE_HEADER.format(version=DOXYGEN_VERSION)
                    + "".join(line + "\n" for line in lines),
                    encoding="utf-8")


class Changes(NamedTuple):
    """What a pull request changes, against the merge base.

    Attributes:
        new: Files the merge base does not have, untracked ones included.
        hunks: For each changed file, the line ranges of the new side that
            the change adds or rewrites, first and last line inclusive.
    """

    new: set[str]
    hunks: dict[str, list[tuple[int, int]]]


_HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def parse_hunks(diff: str) -> dict[str, list[tuple[int, int]]]:
    """Read the new side's line ranges out of a `git diff -U0`.

    A hunk that only deletes covers the line before the cut.

    Args:
        diff: The output of `git diff -U0 --no-renames`.

    Returns:
        Each file of a `+++ b/` line mapped to its ranges.
    """
    hunks: dict[str, list[tuple[int, int]]] = {}
    current = None
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[4:]
            current = path[2:] if path.startswith("b/") else None
            if current is not None:
                hunks.setdefault(current, [])
            continue
        match = _HUNK_RE.match(line)
        if match and current is not None:
            first = int(match.group(1))
            count = 1 if match.group(2) is None else int(match.group(2))
            if count == 0:
                hunks[current].append((max(first, 1), max(first, 1)))
            else:
                hunks[current].append((first, first + count - 1))
    return hunks


def changes(root: pathlib.Path, base: str) -> Changes:
    """Find the new files and the changed lines since the merge base.

    The working tree is compared with the merge base of base and HEAD, so
    a commit not yet made counts too.  Renames count as new files.

    Args:
        root: The repository root.
        base: A git revision, for example origin/main.

    Returns:
        The new files and the changed line ranges of the other files.
    """
    merge_base = git_lines(root, "merge-base", base, "HEAD")[0]
    new = set(git_lines(root, "diff", *_DIFF_OPTIONS, "--name-only",
                        "--diff-filter=A", merge_base))
    new |= set(git_lines(root, "ls-files", "--others",
                         "--exclude-standard"))
    diff = subprocess.run(
        ["git", "diff", *_DIFF_OPTIONS, "-U0", "--diff-filter=M",
         merge_base],
        cwd=root, check=True, capture_output=True, text=True).stdout
    return Changes(new, parse_hunks(diff))


def warning_span(warning: DocWarning, lines: list[str]) -> tuple[int, int]:
    """Find the lines a warning stands for.

    Doxygen reports an undocumented item at its declaration and a
    documented one at the start of its comment block.  The span is the
    reported line, widened by the comment block next to it:

    - a block that starts on the reported line, through its end and on to
      the first code line after it (the declaration it documents);
    - a block that ends on the line above the reported line, from its
      start (the comment of the declaration reported).

    A declaration that runs over several lines counts by its reported
    line only.

    Args:
        warning: A warning with a line.
        lines: The file's lines.

    Returns:
        The first and last line of the span, inclusive.
    """
    first = last = warning.line
    index = warning.line - 1
    if 0 <= index < len(lines) and re.match(r"\s*/\*[*!]", lines[index]):
        end = index
        while end < len(lines) and "*/" not in lines[end]:
            end += 1
        end += 1
        while end < len(lines) and not lines[end].strip():
            end += 1
        last = min(end, len(lines) - 1) + 1
    elif 0 < index < len(lines) and lines[index - 1].rstrip().endswith("*/"):
        start = index - 1
        while start > 0 and not re.match(r"\s*/\*", lines[start]):
            start -= 1
        if re.match(r"\s*/\*[*!]", lines[start]):
            first = start + 1
    return first, last


def select_touched(warnings: list[DocWarning], diff: Changes,
                   root: pathlib.Path) -> list[DocWarning]:
    """Pick the warnings that new or changed code carries.

    Args:
        warnings: The warnings of the run.
        diff: The new files and changed lines.
        root: The repository root.

    Returns:
        Every warning in a new file; in a changed file, every warning whose
        span (warning_span) overlaps a changed range, and a missing @file
        block or brief wherever the file changed.
    """
    touched = []
    cache: dict[str, list[str]] = {}
    for warning in warnings:
        if warning.file in diff.new:
            touched.append(warning)
            continue
        ranges = diff.hunks.get(warning.file)
        if not ranges:
            continue
        if warning.message in (NO_FILE_BLOCK, NO_FILE_BRIEF):
            touched.append(warning)
            continue
        if warning.file not in cache:
            path = root / warning.file
            cache[warning.file] = path.read_text(
                encoding="utf-8", errors="replace").splitlines() \
                if path.is_file() else []
        first, last = warning_span(warning, cache[warning.file])
        if any(a <= last and first <= b for a, b in ranges):
            touched.append(warning)
    return touched


def block_brief_warnings(root: pathlib.Path, diff: Changes,
                         settings: dict[str, list[str]]) -> list[DocWarning]:
    """Find the comment blocks without a brief that a change adds or edits.

    Args:
        root: The repository root.
        diff: The new files and changed lines.
        settings: The Doxyfile's settings; files Doxygen does not read are
            skipped.

    Returns:
        One NO_BLOCK_BRIEF warning, at the block's first line, for each
        `/**` or `/*!` block in a new file, or overlapping a changed range,
        that holds no `@brief` and is not a `/**<` block, a group block or
        the `@file` block.
    """
    found = []
    for rel in sorted(diff.new | set(diff.hunks)):
        path = root / rel
        if not in_scope(rel, settings) or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        ranges = diff.hunks.get(rel, [])
        for block in _DOC_BLOCK_RE.finditer(text):
            body = block.group(0)
            if (body[3:4] == "<" or _BRIEF_CMD_RE.search(body)
                    or _GROUP_CMD_RE.search(body)
                    or _FILE_CMD_RE.search(body)):
                continue
            first = text.count("\n", 0, block.start()) + 1
            last = first + body.count("\n")
            if rel in diff.new or any(a <= last and first <= b
                                      for a, b in ranges):
                found.append(DocWarning(rel, first, NO_BLOCK_BRIEF))
    return found


class Result(NamedTuple):
    """What a comparison found.

    Attributes:
        new: Warnings the baseline does not hold, in excess of its count.
        stale: Baseline lines that no longer occur, with their excess.
        touched: Warnings that new or changed code carries.
    """

    new: list[DocWarning]
    stale: list[str]
    touched: list[DocWarning]

    @property
    def failed(self) -> bool:
        """True when any of the three lists has an entry."""
        return bool(self.new or self.stale or self.touched)


def compare(warnings: list[DocWarning], baseline: collections.Counter,
            touched: list[DocWarning]) -> Result:
    """Compare the warnings of a run with the baseline.

    Args:
        warnings: The warnings of the run.
        baseline: The baseline's lines with their counts.
        touched: Warnings that fail baseline or not (select_touched).

    Returns:
        The new warnings, the stale baseline lines and the touched ones.
    """
    seen: collections.Counter = collections.Counter()
    new = []
    for warning in warnings:
        seen[warning.key] += 1
        if seen[warning.key] > baseline[warning.key]:
            new.append(warning)
    stale = sorted((baseline - seen).elements())
    return Result(new, stale, list(touched))


def report(result: Result) -> str:
    """Write a comparison's failures for a reader.

    Args:
        result: The comparison.

    Returns:
        The text, empty when nothing failed.
    """
    parts = []
    if result.touched:
        parts.append(f"{len(result.touched)} warning(s) in new files or on "
                     "changed lines; document what you add or change:")
        parts += [f"  {w.file}:{w.line}: {w.message}"
                  for w in result.touched]
    touched = set(result.touched)
    new = [w for w in result.new if w not in touched]
    if new:
        parts.append(f"{len(new)} warning(s) not in the baseline:")
        parts += [f"  {w.file}:{w.line}: {w.message}" for w in new]
    if result.stale:
        parts.append(f"{len(result.stale)} baseline line(s) no longer "
                     "occur; run tools/doc_check.py --update:")
        parts += [f"  {line}" for line in result.stale]
    return "\n".join(parts)


def main(argv: list[str] | None = None) -> int:
    """Run the check or the update.

    Args:
        argv: The arguments; sys.argv[1:] when None.

    Returns:
        0 when the check passes or the baseline is written, 1 when the
        check fails, 2 when Doxygen is missing or another release.
    """
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--update", action="store_true")
    parser.add_argument("--base", help="also fail on any warning in a "
                        "file new since the merge base with this revision "
                        "and on changed lines")
    parser.add_argument("--log", type=pathlib.Path,
                        help="read this warning log instead of running "
                        "Doxygen")
    parser.add_argument("--doxygen", default="doxygen")
    parser.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    parser.add_argument("--root", type=pathlib.Path, default=REPO,
                        help=argparse.SUPPRESS)
    parser.add_argument("--baseline", type=pathlib.Path,
                        help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    baseline_path = args.baseline or root / "tools" / BASELINE.name

    if args.log is not None:
        log_text = args.log.read_text(encoding="utf-8", errors="replace")
    else:
        try:
            version = doxygen_version(args.doxygen)
        except (FileNotFoundError, subprocess.CalledProcessError):
            print(f"doc_check: {args.doxygen} not found; install Doxygen "
                  f"{DOXYGEN_VERSION} with tools/install_doxygen.sh",
                  file=sys.stderr)
            return 2
        if version != DOXYGEN_VERSION:
            print(f"doc_check: Doxygen {version or '?'} found, the baseline "
                  f"is Doxygen {DOXYGEN_VERSION}'s; install it with "
                  "tools/install_doxygen.sh", file=sys.stderr)
            return 2
        log_text = run_doxygen(args.doxygen, root, args.out)

    warnings = collect(log_text, root)
    if args.update:
        write_baseline(baseline_path, warnings)
        print(f"doc_check: {len(warnings)} warning(s) written to "
              f"{baseline_path.relative_to(root)}")
        return 0

    diff = changes(root, args.base) if args.base else Changes(set(), {})
    touched = select_touched(warnings, diff, root)
    touched += block_brief_warnings(root, diff,
                                    read_doxyfile(root / "Doxyfile"))
    result = compare(warnings, read_baseline(baseline_path), touched)
    if result.failed:
        print(report(result))
        return 1
    print(f"doc_check: {len(warnings)} warning(s), all in the baseline; "
          f"{len(diff.new)} new and {len(diff.hunks)} changed file(s) "
          "clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
