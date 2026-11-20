#!/usr/bin/env python3
"""Hold the weekly mutation run's figures in the docs to the tree.

`mutation.yml` runs the mutants of every line of `shared/safety` and
`shared/outputs` in shards.  Its comment, STATUS.md, docs/Building.md,
docs/Building-de.md and CONTRIBUTING.md state how many mutants that is,
in how many files, how many of them are in each folder, and what share of
them one week's budget reaches.  Each file added under the two folders
changes the counts.  This tool counts the mutants the way
`tools/mutate.py --list` does for the weekly run's file list and compares
every stated figure with the count.

The figures:

- the files the run names (every `.c` and `.h` under the two folders, as
  the workflow's `find` lists them) and the mutants in them;
- the mutants of `shared/safety` and of `shared/outputs`;
- the shards and the budget, read from the workflow's tool call;
- the mutants one shard reaches: the budget divided by 42s a mutant,
  rounded down; the run reaches that many times the shards;
- the mutants of `shared/safety` a shard holds: their count divided by
  the shards, rounded up;
- the mutants of `shared/outputs` a run reaches: the run's reach less the
  mutants of `shared/safety`, which lead every shard.

A figure keeps the thousands separator it is written with: `6,743`,
`6.743` or `6743`.

    python3 tools/check_mutants.py --check     # exit 1 on a stale figure
    python3 tools/check_mutants.py --update    # write the counted figures

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import math
import pathlib
import re
import sys

import mutate

REPO = pathlib.Path(__file__).resolve().parent.parent
WORKFLOW = ".github/workflows/mutation.yml"
# The folders the weekly run mutates, in the order of its `find`.
FOLDERS = ("shared/safety", "shared/outputs")
# Seconds one mutant takes on a hosted runner: 103,014s for 2,440 mutants
# in the run of 2026-11-15 is 42.2s.
MUTANT_S = 42

NUMBER = r"\d{1,3}(?:[,.]\d{3})+|\d+"

# Where each figure is stated.  `{name}` is a figure; a space stands for
# any run of white space and `#`, so a sentence may wrap across comment
# lines.  Each phrase occurs exactly once in its file.
EN_TABLE = [
    "({mutants} mutants in {files} files) in {shards} parallel shards",
    "no mutant is started after {budget}s in a shard, about {per_shard} "
    f"mutants at {MUTANT_S}s each, so the {{safety}} mutants of "
    "`shared/safety` run every week and about {outputs_run} of the "
    "{outputs} of `shared/outputs`",
]
PLACES: dict[str, list[str]] = {
    "STATUS.md": EN_TABLE,
    "docs/Building.md": EN_TABLE,
    "docs/Building-de.md": [
        "({mutants} Mutanten in {files} Dateien) in {shards} parallelen "
        "Shards",
        "nach {budget}s startet in einem Shard keine mehr, das sind etwa "
        f"{{per_shard}} Mutanten zu je {MUTANT_S}s, also laufen die "
        "{safety} Mutanten von `shared/safety` jede Woche und etwa "
        "{outputs_run} der {outputs} von `shared/outputs`",
    ],
    "CONTRIBUTING.md": [
        "`shared/outputs` ({mutants} mutants in {files} files)",
        "runs in {shards} shards.",
        "A shard starts no mutant after {budget}s,",
        f"At {MUTANT_S}s a mutant on a hosted runner a shard runs about "
        "{per_shard} mutants: the {safety} mutants of `shared/safety` run "
        "every week, and about {outputs_run} of the {outputs} of "
        "`shared/outputs`.",
    ],
    WORKFLOW: [
        "A shard starts no mutant after {budget}s:",
        f"at {MUTANT_S}s a mutant on the runner that is about {{per_shard}} "
        "mutants a shard, {run} of the {mutants} in all.",
        "Every mutant of shared/safety ({safety}, about {safety_per_shard} "
        "a shard) runs every week",
    ],
}


def weekly_call(text: str) -> tuple[int, int]:
    """Read the shards and the budget from the workflow's tool call.

    Args:
        text: mutation.yml.

    Returns:
        (shards, budget in seconds).

    Raises:
        ValueError: The workflow does not list the files of FOLDERS or
            names no shard count or budget.
    """
    if f"find {' '.join(FOLDERS)} -name '*.c' -o -name '*.h'" not in text:
        raise ValueError(f"{WORKFLOW} does not list the .c and .h files of "
                         f"{' and '.join(FOLDERS)}")
    shard = re.search(r'--shard "\$SHARD/(\d+)"', text)
    budget = re.search(r"--budget (\d+)\b", text)
    if not shard or not budget:
        raise ValueError(f"{WORKFLOW} names no --shard \"$SHARD/N\" or "
                         f"--budget S")
    return int(shard.group(1)), int(budget.group(1))


def weekly_files(root: pathlib.Path = REPO) -> list[str]:
    """The files the weekly run names, sorted, relative to @p root."""
    out = []
    for folder in FOLDERS:
        for path in (root / folder).rglob("*"):
            if path.suffix in (".c", ".h"):
                out.append(path.relative_to(root).as_posix())
    return sorted(out)


def figures(files: int, safety: int, outputs: int, shards: int,
            budget: int) -> dict[str, int]:
    """Every figure the docs state, from the counts.

    Args:
        files: The files the run names.
        safety: The mutants of shared/safety.
        outputs: The mutants of shared/outputs.
        shards: The shards of the run.
        budget: The seconds after which a shard starts no mutant.

    Returns:
        {figure name: value}.
    """
    per_shard = budget // MUTANT_S
    return {"files": files, "mutants": safety + outputs, "safety": safety,
            "outputs": outputs, "shards": shards, "budget": budget,
            "per_shard": per_shard, "run": per_shard * shards,
            "safety_per_shard": math.ceil(safety / shards),
            "outputs_run": per_shard * shards - safety}


def count(root: pathlib.Path = REPO) -> dict[str, int]:
    """Count the weekly run's mutants in the tree and derive the figures.

    Raises:
        ValueError: The workflow cannot be read.
    """
    shards, budget = weekly_call((root / WORKFLOW).read_text(
        encoding="utf-8"))
    names = weekly_files(root)
    every = mutate.candidates("unused", [str(root / n) for n in names])
    by = {f: sum(m.path.startswith(f + "/") for m in every)
          for f in FOLDERS}
    return figures(len(names), by[FOLDERS[0]], by[FOLDERS[1]], shards,
                   budget)


def phrase(template: str) -> re.Pattern[str]:
    """The pattern of one phrase: figures as groups, spaces as any gap."""
    out = []
    for part in re.split(r"(\{\w+\})", template):
        if part.startswith("{") and part.endswith("}"):
            out.append(f"(?P<{part[1:-1]}>{NUMBER})")
        else:
            out.append(r"[\s#]+".join(re.escape(w)
                                      for w in part.split(" ")))
    return re.compile("".join(out))


def written(value: int, like: str) -> str:
    """@p value with the thousands separator @p like is written with."""
    sep = next((c for c in like if c in ",."), "")
    return f"{value:,}".replace(",", sep) if sep else str(value)


def number(text: str) -> int:
    """The value of a figure as written, separators dropped."""
    return int(re.sub(r"[,.]", "", text))


def compare(name: str, text: str, want: dict[str, int]
            ) -> tuple[str, list[str]]:
    """Hold one file's figures to @p want.

    Args:
        name: The file, as PLACES names it.
        text: Its content.
        want: The counted figures.

    Returns:
        (the text with every figure as counted, the problems in words).
    """
    problems = []
    for template in PLACES[name]:
        pattern = phrase(template)
        found = list(pattern.finditer(text))
        if len(found) != 1:
            problems.append(f"{name}: {len(found)} matches of \"{template}\"; "
                            f"want 1")
            continue
        m = found[0]
        # Replace from the end so earlier spans stay where they are.
        for key in sorted(m.groupdict(), key=m.start, reverse=True):
            have = m.group(key)
            if number(have) != want[key]:
                problems.append(f"{name}: {key} reads {have}, the tree "
                                f"gives {want[key]}")
                text = (text[:m.start(key)] + written(want[key], have)
                        + text[m.end(key):])
    return text, problems


def main(argv: list[str] | None = None, root: pathlib.Path = REPO) -> int:
    """Check or write the figures; 0 when they agree or were written."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true",
                      help="exit 1 when a stated figure differs")
    mode.add_argument("--update", action="store_true",
                      help="write the counted figures into the files")
    args = ap.parse_args(argv)
    try:
        want = count(root)
    except ValueError as err:
        print(f"check_mutants: {err}", file=sys.stderr)
        return 1
    print(f"check_mutants: {want['mutants']} mutants in {want['files']} "
          f"files, {want['safety']} in {FOLDERS[0]}, {want['outputs']} in "
          f"{FOLDERS[1]}; {want['shards']} shards reach {want['run']}")
    failed = False
    for name in PLACES:
        path = root / name
        text = path.read_text(encoding="utf-8")
        new, problems = compare(name, text, want)
        for p in problems:
            print(p, file=sys.stderr)
        unplaced = any("matches of" in p for p in problems)
        if args.update and new != text:
            path.write_text(new, encoding="utf-8")
            print(f"check_mutants: wrote {name}")
        failed |= unplaced or (args.check and bool(problems))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
