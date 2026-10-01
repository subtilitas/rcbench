#!/usr/bin/env python3
"""The research session's side of the component research
(hardware/docs/Research.md), in round 1 and round 2.

A workflow script has no file or git access, so the session runs this tool
around each run:

    session.py check
    session.py prepare TASK --base DIR --digikey-env FILE --model M --effort E
                       [--round N] [--followup FILE] [--db FILE] [--no-fetch]
    session.py record TASK OUTPUT --base DIR [--round N] [--name N]
    session.py raised --base DIR [--round N] [--run T1|FU-N]
    session.py script RUN --base DIR [--round N] [--out DIR]

`--round` selects the research round, 1 unless given. Round N reads its
plan on `research/roundN`, keeps its results on `research/roundN-results`
and records its runs under hardware/research/roundN/. The runs of every
round up to N count as earlier runs. Round 1 runs T1 to T6 and follow-up
tasks; a later round runs follow-up tasks and a T6 of its own.

`check` holds the files in tools/research/ to the plan: the agent counts of
the layout, the categories and the rows that carry "P1 asks", the schemas,
the host table, the round rules on a throwaway repository, and the dry run
of round1.js when node is installed. It exits 1 on any disagreement.

`prepare` fetches origin, checks the run's turn and its answered questions,
makes the read-only checkout of the round's plan branch at the commit the
run reads, the monostable pages at 23c82ca and the working tree of the
round's results branch under DIR, and writes DIR/args-RUN.json, the
Workflow tool's `args` for tools/research/round1.js. Before T6 it merges the
plan branch into the results tree.

`record` takes the output file the Workflow tool wrote, checks each agent's
return against its schema, writes one JSON file per return under
hardware/research/roundN/RUN/ in the results tree, and commits that
directory and the round's selection.json (after T6, the pages P7 wrote as
well).

`raised` appends the questions a P1 run confirmed under "Raised by P1" in a
working tree of the round's plan branch and commits them. Nothing is pushed.

`script` writes roundN-RUN.js, round1.js with DIR/args-RUN.json in place of
the Workflow tool's `args`, for the Workflow tool's scriptPath, into DIR or
the directory --out names.
"""

import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile

import vendors

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PLAN_REL = os.path.join("hardware", "docs", "Research.md")
# The Sourcing questions each task waits on, as the plan's Sourcing section
# states them: S1, S3 and S8 block T1; S2 and S4 to S7 block T2, T3 and T4;
# S9 blocks R7.
SOURCING_T1 = ("S1", "S3", "S8")
SOURCING_STOCK = ("S2", "S4", "S5", "S6", "S7")
SOURCING_CATEGORY = {"R7": ("S9",)}
MONOSTABLE = "23c82ca6ca976d956098cfafd25dbefa11584f5d"
BRANCH = "research/round1"
RESULTS = "research/round1-results"
TASKS = ["T1", "T2", "T3", "T4", "T5", "T6", "FU"]
AFTER = {"T2": ["T1"], "T4": ["T1"], "T3": ["T2", "T4"], "T5": ["T3"],
         "T6": ["T5"]}
RUNS_DIR = os.path.join("hardware", "research", "round1")
# The research rounds `--round` selects. The branches and the records
# directory above are round 1's; use_round points them at another round.
# RUNS_DIRS holds the records directory of every round up to the selected
# one, oldest first: their runs count as earlier runs.
ROUNDS = (1, 2)
ROUND = 1
RUNS_DIRS = (RUNS_DIR,)
# The files T6 may write: the Outputs table of the plan.
T6_DIRS = ("hardware/docs/",)
T6_FILES = ("hardware/STATUS.md", "hardware/README.md", "tools/jlc_stock.py")


# The outputs T6 writes, from the Outputs table of the plan.
T6_REQUIRED = ["hardware/docs/IOBoard.md", "hardware/docs/Parts.md",
               "hardware/docs/Power.md", "hardware/docs/Research.md",
               "hardware/STATUS.md", "hardware/README.md",
               "tools/jlc_stock.py"]
# The outputs T6 of each round must write. Round 2 updates the group pages
# round 1 wrote and writes another output only where its returns change it.
T6_WRITTEN = {1: T6_REQUIRED,
              2: ["hardware/docs/IOBoard.md", "hardware/docs/Research.md",
                  "hardware/STATUS.md"]}


def runs_dir(n):
    """Round n's records directory, as git names it."""
    return f"hardware/research/round{n}"


def use_round(n):
    """Point the branches and the records directory at round n."""
    global ROUND, BRANCH, RESULTS, RUNS_DIR, RUNS_DIRS
    ROUND = n
    BRANCH = f"research/round{n}"
    RESULTS = f"research/round{n}-results"
    RUNS_DIR = runs_dir(n)
    RUNS_DIRS = tuple(runs_dir(k) for k in range(1, n + 1))


def posix(path):
    return path.replace(os.sep, "/")


def t6_output(path):
    return path in T6_FILES or path.startswith(T6_DIRS)


def changed(tree):
    """Paths git reports as changed or new, from NUL-separated output so
    no status column or file name is trimmed. A rename or copy gives both
    its destination and its source."""
    raw = git("-C", tree, "status", "--porcelain", "-z",
              "--untracked-files=all", check=False).stdout
    out, parts = [], raw.split("\0")
    i = 0
    while i < len(parts):
        entry = parts[i]
        if len(entry) > 3:
            out.append(entry[3:])
            if entry[0] in "RC" and i + 1 < len(parts):
                i += 1  # a rename or copy carries its source next
                out.append(parts[i])
        i += 1
    return out


def load(name):
    with open(os.path.join(HERE, name)) as f:
        return json.load(f)


def resolved_schemas():
    raw = load("schemas.json")

    def res(node):
        if isinstance(node, list):
            return [res(x) for x in node]
        if isinstance(node, dict):
            if "$ref" in node:
                return res(raw["defs"][node["$ref"]])
            return {k: res(v) for k, v in node.items()}
        return node

    return {role: res(s) for role, s in raw["roles"].items()}


def validate(schema, value, where="$"):
    """The subset of JSON Schema the schemas use: type, properties,
    required, items, enum, pattern."""
    kind = schema.get("type")
    ok = {"object": dict, "array": list, "string": str, "boolean": bool}
    if kind == "integer":
        if not isinstance(value, int) or isinstance(value, bool):
            return [f"{where}: not an integer"]
    elif kind in ok and not isinstance(value, ok[kind]):
        return [f"{where}: not a {kind}"]
    if "enum" in schema and value not in schema["enum"]:
        return [f"{where}: {value!r} not in {schema['enum']}"]
    if "pattern" in schema and isinstance(value, str) and not re.search(
            schema["pattern"], value):
        return [f"{where}: {value!r} does not match {schema['pattern']}"]
    errs = []
    if kind == "object":
        for key in schema.get("required", []):
            if key not in value:
                errs.append(f"{where}.{key}: missing")
        for key, sub in schema.get("properties", {}).items():
            if key in value:
                errs += validate(sub, value[key], f"{where}.{key}")
    if kind == "array":
        for i, item in enumerate(value):
            errs += validate(schema["items"], item, f"{where}[{i}]")
    return errs


def git(*args, cwd=ROOT, check=True):
    out = subprocess.run(["git", *args], cwd=cwd, capture_output=True,
                         text=True)
    if not check:
        return out
    if out.returncode:
        raise SystemExit(f"git {' '.join(args)}: {out.stderr.strip()}")
    return out.stdout.strip()


def read(path):
    with open(path) as f:
        return f.read()


def plan_lcsc(text):
    return sorted(set(re.findall(r"\bC\d{4,11}\b", text)))


def cells(line):
    """The cells of a table row, split on pipes that are not escaped."""
    line = line.strip()
    if not line.startswith("|") or not line.endswith("|"):
        return None
    return [c.strip() for c in re.split(r"(?<!\\)\|", line)[1:-1]]


def raised_rows(text):
    """The rows under "Raised by P1": (id, category, question, answer)."""
    sec = text.split("#### Raised by P1", 1)
    if len(sec) < 2:
        raise SystemExit("the Raised by P1 section is not found")
    rows = []
    for line in sec[1].split("\n####", 1)[0].splitlines():
        c = cells(line)
        if c and len(c) == 4 and re.fullmatch(r"V\d+", c[0]):
            rows.append((c[0], c[1].split(",")[0].strip(), c[2], c[3]))
    return rows


def blocking_rows(text):
    """The Blocking table: (id, categories it blocks, answer)."""
    sec = text.split("#### Blocking: answered before the research tasks", 1)
    if len(sec) < 2:
        raise SystemExit("the Blocking section is not found")
    rows = []
    for line in sec[1].split("\n####", 1)[0].splitlines():
        c = cells(line)
        if c and len(c) == 5 and re.fullmatch(r"[A-Z]\d+", c[0]):
            rows.append((c[0], set(re.findall(r"R\d+", c[2])), c[4]))
    return rows


def sourcing_answers(text):
    """The Sourcing table: {id: answer}."""
    sec = text.split("### Sourcing", 1)
    if len(sec) < 2:
        raise SystemExit("the Sourcing section is not found")
    out = {}
    for line in sec[1].split("\n### ", 1)[0].splitlines():
        c = cells(line)
        if c and len(c) == 4 and re.fullmatch(r"S\d+", c[0]):
            out[c[0]] = c[3]
    return out


def fixed_inputs(text):
    """The Scope table of fixed inputs: (input, part), the part being the
    part number that leads the value cell."""
    sec = text.split("| Input | Value | Source |", 1)
    if len(sec) < 2:
        raise SystemExit("the fixed inputs table is not found")
    rows = []
    for line in sec[1].split("\n")[1:]:
        c = cells(line)
        if not c or len(c) != 3:
            break
        if not c[0].strip("-: "):
            continue  # the separator row
        m = re.match(r"[A-Za-z0-9]+", c[1])
        rows.append((c[0], m.group(0) if m else ""))
    return rows


def spec_text(commit):
    """The specification a run read: the files under hardware/docs/, by
    content, and Research.md without the rows under Raised by P1 and with
    the Decision cells of Q4, Q8 and Q9 blank, which have their own checks.
    None when the commit is not in this clone."""
    plan_rel = PLAN_REL.replace(os.sep, "/")
    listed = git("ls-tree", "-r", commit, "--", "hardware/docs/", check=False)
    shown = git("show", f"{commit}:{plan_rel}", check=False)
    if listed.returncode or shown.returncode:
        return None
    lines, sec = [], ""
    for line in shown.stdout.split("\n"):
        sec = line if line.startswith("#") else sec
        c = cells(line)
        if c and len(c) == 4 and sec == "#### Raised by P1" and \
                re.fullmatch(r"V\d+", c[0]):
            continue
        if c and len(c) == 4 and re.fullmatch(r"Q\d", c[0]) and \
                sec == "#### Decided on the research's output":
            line = "| " + " | ".join(c[:3]) + " | |"
        lines.append(line)
    others = [x for x in listed.stdout.splitlines()
              if not x.endswith("\t" + plan_rel)]
    return "\n".join(lines) + "\0" + "\n".join(others)


def jlc_stock_row(text):
    """The sentences of the Outputs table's row for tools/jlc_stock.py,
    each of which the P7 critic confirms the tool does."""
    sec = text.split("\n## Outputs\n", 1)
    if len(sec) < 2:
        raise SystemExit("the Outputs section is not found")
    for line in sec[1].split("\n## ", 1)[0].splitlines():
        c = cells(line)
        if c and len(c) == 2 and c[0] == "`tools/jlc_stock.py`":
            return re.split(r"(?<=\.)\s+(?=[A-Z`])", c[1])
    raise SystemExit("the Outputs table has no row for tools/jlc_stock.py")


def decisions(text, column=3):
    """Q4, Q8, Q9 and the owner's entry in the Decision column; column 2
    gives the Reported by cell instead."""
    sec = text.split("#### Decided on the research's output", 1)[-1]
    out = {}
    for line in sec.splitlines():
        c = cells(line)
        if c and len(c) == 4 and re.fullmatch(r"Q\d", c[0]):
            # Two rows for one decision leave no single decision.
            if c[0] in out:
                raise SystemExit(f"the decision table has two rows for {c[0]}")
            out[c[0]] = c[column]
    return out


def decision_categories(text):
    """The categories the Reported by column names for Q4, Q8 and Q9: the
    only categories a question may feed that decision alone from."""
    return {q: sorted(set(re.findall(r"\bR\d+\b", cell)),
                      key=lambda c: int(c[1:]))
            for q, cell in decisions(text, 2).items()}


def plan_body(text):
    """The plan page from its first section on: all but the status line."""
    at = text.find("\n## ")
    return text[at:] if at >= 0 else None


# ------------------------------------------------------------------ check

def cmd_check(_args):
    text = read(os.path.join(ROOT, PLAN_REL))
    cats = load("categories.json")
    fails = []

    rows = re.findall(r"^\| (R\d+) \|", text, re.M)
    if rows != list(cats["categories"]):
        fails.append(f"categories {list(cats['categories'])} against the "
                     f"page's rows {rows}")
    layout = re.search(r"^T1 .*?^```", text, re.M | re.S)
    counts = dict(re.findall(r"^(T\d)\s.*?(\d+)\s*$",
                             layout.group(0) if layout else "", re.M))
    for task, n in cats["planned"].items():
        if int(counts.get(task, -1)) != n:
            fails.append(f"{task}: {n} planned here, "
                         f"{counts.get(task)} in the layout")
    n = len(cats["categories"])
    want = {"T1": 1 + 2 * n + 1, "T5": 4, "T6": 2}
    for task, t in cats["tasks"].items():
        want[task] = 1 + 5 * len(t["categories"])
        for c in t["categories"]:
            if c not in cats["categories"]:
                fails.append(f"{task}: unknown category {c}")
    for task, v in want.items():
        if cats["planned"][task] != v:
            fails.append(f"{task}: planned {cats['planned'][task]}, "
                         f"the phases give {v}")
        if v > cats["cap"]:
            fails.append(f"{task}: {v} agents over the cap {cats['cap']}")
    grouped = [c for t in cats["tasks"].values() for c in t["categories"]]
    if sorted(grouped) != sorted(cats["categories"]):
        fails.append("the groups do not cover every category once")
    m = re.search(r"Size\. T1 (\d+), T2 (\d+), T4 (\d+), T3 (\d+), "
                  r"T5 (\d+), T6 (\d+): (\d+) agents", text)
    if not m:
        fails.append("the Size paragraph is not found")
    else:
        size = dict(zip(["T1", "T2", "T4", "T3", "T5", "T6"],
                        map(int, m.groups()[:6]), strict=True))
        if size != cats["planned"]:
            fails.append(f"Size paragraph {size} against {cats['planned']}")
        if int(m.group(7)) != sum(cats["planned"].values()):
            fails.append(f"Size total {m.group(7)} against "
                         f"{sum(cats['planned'].values())}")
    # Each budget P5 owes once for each chip is a budget of p5_budgets, and
    # the plan names each chip's budgets as the gate does.
    chips = cats.get("p5_chips") or {}
    stray = [n for n in chips.get("budgets", [])
             if n not in cats.get("p5_budgets", [])]
    if stray:
        fails.append(f"p5_chips budgets not in p5_budgets: {stray}")
    for c in chips.get("chips", []):
        if f"`NAME: {c}`" not in text:
            fails.append(f"p5_chips names the chip {c}; the plan does not "
                         f"name its budgets `NAME: {c}`")
    # The "P1 asks" inventory covers the rows that carry one, by distinct
    # names.
    asks = cats.get("p1_asks", {})
    carry = [r for r, row in re.findall(r"^\| (R\d+) \|(.*)$", text, re.M)
             if "P1 asks" in row]
    if sorted(asks) != sorted(carry):
        fails.append(f"p1_asks names {', '.join(asks)}; the rows that carry "
                     f"\"P1 asks\" are {', '.join(carry)}")
    for c, names in asks.items():
        if not names or len(set(names)) != len(names):
            fails.append(f"p1_asks {c}: no name, or a name twice")
    # The fixed inputs are the Scope table's, by input name and part, each
    # in a known category.
    fixed = cats.get("fixed_inputs", {})
    listed = sorted((x.get("function"), x.get("part"))
                    for xs in fixed.values() for x in xs)
    if listed != sorted(fixed_inputs(text)):
        fails.append(f"fixed_inputs {listed} against the Scope table "
                     f"{sorted(fixed_inputs(text))}")
    for c in fixed:
        if c not in cats["categories"]:
            fails.append(f"fixed_inputs: unknown category {c}")
    raised_rows(text)
    jlc_stock_row(text)
    if set(decisions(text)) != {"Q4", "Q8", "Q9"}:
        fails.append("the decision table does not list Q4, Q8 and Q9")
    if not all(decision_categories(text).values()):
        fails.append("a decision's Reported by names no category")
    ids = [i for i, _, _ in blocking_rows(text)]
    if ids != [f"F{n}" for n in range(1, 17)] + ["Q7"]:
        fails.append(f"the Blocking table lists {ids}")
    if any(not blocks for _, blocks, _ in blocking_rows(text)):
        fails.append("a Blocking row names no category")
    if sorted(sourcing_answers(text)) != sorted(f"S{n}" for n in range(1, 10)):
        fails.append("the Sourcing table does not list S1 to S9")
    rule = "S1, S3 and S8 block T1. S2 and S4 to S7 block T2, T3 and T4. S9 " \
        "blocks R7 (T3)."
    if rule not in " ".join(text.split()):
        fails.append("the Sourcing section no longer states which task each "
                     "question blocks as session.py reads it")
    # The prompts of a later round name its section of the page.
    for n in ROUNDS[1:]:
        if f"\n## Round {n}\n" not in text:
            fails.append(f"the page has no section \"Round {n}\", which the "
                         f"prompts of round {n} name")
    fails += round_selftest()

    schemas = resolved_schemas()
    js = read(os.path.join(HERE, "round1.js"))
    for role in sorted(set(re.findall(r"run\('([^']+)'", js))):
        if role not in schemas:
            fails.append(f"round1.js starts {role}, which has no schema")
    for role in schemas:
        if f"'{role}'" not in js:
            fails.append(f"schema {role} is not used by round1.js")

    hosts = load("hosts.json")
    for h in hosts["hosts"]:
        for key in ("host", "client", "probe", "hold"):
            if key not in h:
                fails.append(f"host {h.get('host')}: no {key}")
        if h.get("client") not in hosts["clients"]:
            fails.append(f"host {h['host']}: client {h.get('client')}")
        for c in h.get("hold", []) + h.get("hold_in_t1", []):
            if c not in cats["categories"]:
                fails.append(f"host {h['host']}: unknown category {c}")
        if h.get("marker"):
            try:
                re.compile(h["marker"])
            except re.error as err:
                fails.append(f"host {h['host']}: marker {err}")

    if shutil.which("node"):
        run = subprocess.run(["node", os.path.join(HERE, "dryrun.js")],
                             capture_output=True, text=True)
        if run.returncode:
            fails.append("dry run: " + run.stdout.strip())
        dry = run.stdout.strip().splitlines()[-1:] or ["no output"]
    else:
        dry = ["not run: node is not installed"]

    for f in fails:
        print(f"FAIL {f}")
    print(f"tools/research: {len(fails)} failures; dry run: {dry[0]}")
    return 1 if fails else 0


def round_selftest():
    """The round rules on a throwaway repository: the order and the rounds
    of the records, the directories a lookup reads, and the turn gate of
    both rounds. Returns the failures; reads no network."""
    cats = load("categories.json")
    fails = []
    tmp = tempfile.mkdtemp(prefix="rcbench-rounds-")
    r1, r2 = (posix(runs_dir(k)) for k in (1, 2))
    p1 = {"phases": "P1", "round": 1, "categories": ["R1"], "items": []}
    p24 = {"phases": "P2-P4", "round": 1, "categories": ["R1"], "items": []}
    p56 = {"phases": "P5-P6", "round": 1, "categories": [], "items": []}

    def put(run, task):
        path = os.path.join(tmp, run, "task.json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(task, f)
        git("add", "-A", cwd=tmp)
        git("-c", "user.name=check", "-c", "user.email=check@localhost",
            "-c", "commit.gpgsign=false", "commit", "-q", "--no-verify",
            "-m", f"Record {run}", cwd=tmp)

    def refusal(n, task, run, followup=None):
        use_round(n)
        try:
            turn(tmp, task, run, followup, cats)
        except SystemExit as err:
            return str(err)
        return ""

    def expect(ok, what):
        if not ok:
            fails.append(f"round self-test: {what}")
    try:
        git("init", "-q", tmp)
        put(f"{r1}/T1", {"task": "T1", "sequence": 1})
        put(f"{r1}/T2", {"task": "T2", "sequence": 2})
        put(f"{r1}/T4", {"task": "T4", "sequence": 3})
        put(f"{r1}/T3", {"task": "T3", "sequence": 4})
        put(f"{r1}/FU-A", {"task": "FU", "sequence": 5, "followup": p24})
        put(f"{r1}/T5", {"task": "T5", "sequence": 6})
        expect(refusal(2, "FU", "FU-2X", p1)
               == "round 2 starts after round 1's T6",
               "round 2 starts before round 1's T6")
        put(f"{r1}/T6", {"task": "T6", "sequence": 7})
        expect(refusal(1, "FU", "FU-B", p1)
               == "T6 is recorded; round 1 is closed",
               "round 1 runs a follow-up after its T6")
        expect(refusal(2, "T3", "T3") == "T1 to T5 run in round 1 only",
               "round 2 runs T3")
        expect(refusal(2, "T6", "T6").startswith(
            "T6 of round 2 runs after a P5-P6 follow-up of round 2"),
            "round 2 runs T6 before a P5-P6 follow-up of its own")
        expect(refusal(2, "FU", "FU-2X", {**p1, "round": 2}).startswith(
            "a pass 2 follow-up runs after a pass 1 follow-up of round 2"),
            "round 2 runs pass 2 on round 1's pass 1")
        expect(refusal(2, "FU", "FU-A", p1) == "FU-A is recorded already",
               "round 2 repeats a run name of round 1")
        expect(refusal(2, "FU", "FU-2Y", p24)
               == "FU-2Y runs after a P1 follow-up of round 2 covering R1, "
               "which is not recorded",
               "round 2 runs a P2-P4 follow-up before a P1 follow-up of its "
               "own")
        expect(refusal(2, "FU", "FU-2X", p1) == "",
               "round 2 refuses its first follow-up")
        put(f"{r2}/FU-2X", {"task": "FU", "sequence": 8, "followup": p1})
        expect(refusal(2, "FU", "FU-2Y", p24) == "",
               "round 2 refuses a P2-P4 follow-up after its P1 follow-up")
        expect(refusal(2, "FU", "FU-2Y", {**p24, "categories": ["R1", "R2"]})
               == "FU-2Y runs after a P1 follow-up of round 2 covering R2, "
               "which is not recorded",
               "round 2 runs a P2-P4 follow-up in a category no P1 follow-up "
               "of its own covers")
        put(f"{r2}/FU-2Q", {"task": "FU", "sequence": 9,
                            "followup": {**p1, "categories": ["R5", "R9"]}})
        expect(refusal(2, "FU", "FU-2W", {**p24, "categories": ["R5"]})
               == "FU-2W runs after P2-P4 follow-ups of round 2 covering "
               "R1, R9, which are not recorded",
               "round 2 runs R5 before its P2-P4 follow-ups in R1 and R9")
        expect(refusal(2, "FU", "FU-2P", p56)
               == "FU-2P runs after P2-P4 follow-ups of round 2 covering "
               "R1, R5, R9, which are not recorded",
               "round 2 checks the board before its P2-P4 follow-ups")
        use_round(2)
        got = [(n, t.get("research_round")) for n, t in runs(tmp)]
        expect(got == [("T1", 1), ("T2", 1), ("T4", 1), ("T3", 1),
                       ("FU-A", 1), ("T5", 1), ("T6", 1), ("FU-2X", 2),
                       ("FU-2Q", 2)],
               f"round 2 reads the runs {got}")
        expect(recorded(tmp, "FU-2X") and not recorded(tmp, "T1")
               and recorded_any(tmp, "T1"),
               "round 2 looks a run up in the wrong directories")
        use_round(1)
        expect([n for n, _ in runs(tmp)]
               == ["T1", "T2", "T4", "T3", "FU-A", "T5", "T6"]
               and not recorded_any(tmp, "FU-2X"),
               "round 1 reads round 2's records")
        expect(refusal(2, "FU", "FU-2X", p1) == "FU-2X is recorded already",
               "round 2 repeats a run name of its own")
        expect(refusal(2, "FU", "FU-2Y", {**p1, "round": 2}) == "",
               "round 2 refuses pass 2 after its own pass 1")
        put(f"{r2}/FU-2Y", {"task": "FU", "sequence": 10, "followup": p24})
        put(f"{r2}/FU-2V", {"task": "FU", "sequence": 11,
                            "followup": {**p24, "categories": ["R9"]}})
        expect(refusal(2, "FU", "FU-2W", {**p24, "categories": ["R5"]})
               == "", "round 2 refuses R5 after its P2-P4 follow-ups in R1 "
               "and R9")
        put(f"{r2}/FU-2W", {"task": "FU", "sequence": 12,
                            "followup": {**p24, "categories": ["R5"]}})
        expect(refusal(2, "FU", "FU-2P", p56) == "",
               "round 2 refuses its P5-P6 follow-up after its P2-P4 "
               "follow-ups")
        put(f"{r2}/FU-2P", {"task": "FU", "sequence": 13, "followup": p56})
        expect(refusal(2, "T6", "T6") == "",
               "round 2 refuses its T6 after its P5-P6 follow-up")
        put(f"{r2}/T6", {"task": "T6", "sequence": 14})
        expect(refusal(2, "FU", "FU-2Z", p1)
               == "T6 of round 2 is recorded; round 2 is closed",
               "round 2 runs a follow-up after its T6")
        expect(refusal(2, "T6", "T6") == "T6 is recorded already",
               "round 2 runs T6 twice")
    except SystemExit as err:
        fails.append(f"round self-test: {err}")
    finally:
        use_round(1)
        shutil.rmtree(tmp, ignore_errors=True)
    return fails


# ---------------------------------------------------------------- prepare

def worktree(base, name, ref, detach=True):
    path = os.path.join(base, name)
    listed = git("worktree", "list", "--porcelain")
    registered = f"worktree {path}" in listed.splitlines()
    if os.path.isdir(path) and not registered:
        raise SystemExit(f"{path} exists and is not a git worktree; "
                         "remove it (chmod -R u+w first)")
    if not registered:
        opts = ["--detach"] if detach else []
        git("worktree", "add", *opts, path, ref)
    # A reused tree must still be where this run needs it.
    if detach:
        head, want = git("-C", path, "rev-parse", "HEAD"), git(
            "rev-parse", ref + "^{commit}")
        if head != want:
            raise SystemExit(f"{path} is at {head}, not {want}")
    elif git("-C", path, "branch", "--show-current") != ref:
        raise SystemExit(f"{path} is not on {ref}")
    return path


def read_only(path):
    for top, dirs, files in os.walk(path):
        for n in dirs + files:
            p = os.path.join(top, n)
            if not os.path.islink(p):
                os.chmod(p, os.stat(p).st_mode & ~0o222)
    os.chmod(path, os.stat(path).st_mode & ~0o222)


def recorded(results, run, dirs=None):
    """Whether HEAD holds the run's record in this round's directory, or
    in one of `dirs`."""
    return any(git("-C", results, "cat-file", "-e",
                   f"HEAD:{d}/{run}/task.json", check=False).returncode == 0
               for d in dirs or (RUNS_DIR,))


def recorded_any(results, run):
    """Whether any round up to this one recorded the run."""
    return recorded(results, run, RUNS_DIRS)


def runs(results, stopped=False, pending=None):
    """The committed runs of every round up to this one in the order they
    were recorded, stopped ones left out unless asked for: (name,
    task.json), each task.json carrying its round in `research_round`.
    They are read from HEAD, so an uncommitted edit counts for nothing;
    `pending` adds the run being recorded."""
    out = []
    for k, d in enumerate(RUNS_DIRS, 1):
        listed = git("-C", results, "ls-tree", "--name-only", "HEAD",
                     d + "/", check=False).stdout.split()
        for path in listed:
            name = os.path.basename(path)
            if "-stopped-" in name and not stopped:
                continue
            shown = git("-C", results, "show", f"HEAD:{path}/task.json",
                        check=False)
            if shown.returncode == 0:
                task = json.loads(shown.stdout)
                task.setdefault("research_round", k)
                out.append((name, task))
    if pending:
        out.append(pending)
    return sorted(out, key=lambda r: r[1].get("sequence", 0))


def effective_selection(results, pending=None, before=None):
    """The part each function keeps: the latest run that names a function
    decides it. A run that names it and verifies no part leaves it open,
    and records the earlier part it did not requalify. A function an
    earlier P1 inventory of its category lists and the latest one does
    not is retired by the next run that selects parts in the category
    without naming it. `before` reads only the runs recorded before that
    sequence number."""
    eff, listed, dropped = {}, {}, {}
    for name, task in runs(results, pending=pending):
        if before is not None and task.get("sequence", 0) >= before:
            continue
        for cat, names in ((task.get("summary") or {}).get("inventory")
                           or {}).items():
            if names:
                dropped[cat] = (dropped.get(cat, set())
                                | listed.get(cat, set())) - set(names)
                listed[cat] = set(names)
        sel = (task.get("summary") or {}).get("selection") or {}
        for cat, entries in sel.items():
            named = {e["function"] for e in entries}
            for fn in [f for f in eff.get(cat, {}) if entries
                       and f in dropped.get(cat, ()) and f not in named]:
                del eff[cat][fn]
            for e in entries:
                slot = eff.setdefault(cat, {})
                held = slot.get(e["function"]) or {}
                entry = {
                    "part": e.get("part"), "rank": e.get("rank"),
                    "alternate": e.get("alternate") or "",
                    "run": name,
                    "alternate_unverified":
                        e.get("alternate_unverified") or [],
                    "second_source_missing":
                        bool(e.get("second_source_missing")),
                    "q_alternatives": e.get("q_alternatives") or [],
                    "decision": e.get("decision", "none"),
                    "q_options_missing": e.get("q_options_missing") or []}
                if not e.get("part") and held.get("part"):
                    entry["not_requalified"] = held["part"]
                    if held["part"] in (e.get("refuted") or []):
                        entry["refuted"] = held["part"]
                slot[e["function"]] = entry
    return eff


def changed_under(tree, rel):
    return [p for p in changed(tree) if p.startswith(rel)]


def committed_questions(results):
    """The questions every committed P1 run confirmed: (run, question)."""
    return [(name, q) for name, task in runs(results)
            for q in (task.get("summary") or {}).get("questions", [])]


def raised_questions(results):
    """The committed questions as round1.js matches them against a P1
    run's values, items and "P1 asks": a P1 run does not raise one again.
    Before a P1 follow-up `questions_gate` has checked that those of its
    categories are under "Raised by P1"."""
    return [{k: q.get(k, "") for k in ("id", "category", "question",
                                        "for_where", "for_quantity", "asks")}
            for _, q in committed_questions(results)]


def p1_unresolved(results, categories):
    """Each category with P1 items no later run cleared: a P1 run's item
    for the category (its P1, critic or re-check returned nothing, a
    question was not ruled on, or P0 held it) stays open until a later P1
    follow-up covering the category lists it among its items and leaves
    no item of its own there."""
    p1 = [(n, t) for n, t in runs(results) if t.get("task") == "T1" or
          (t.get("followup") or {}).get("phases") == "P1"]

    def key(item):
        return json.dumps(item, sort_keys=True)

    def left_by(task, c):
        return [f for f in task.get("followUps", [])
                if f.get("category") == c and not f.get("notice") and
                str(f.get("role", "")).startswith(("P1", "category"))]
    out = []
    for c in categories:
        # T1 covers every category; a P1 follow-up only its own.
        cover = [(n, t) for n, t in p1 if t.get("task") == "T1" or
                 c in ((t.get("followup") or {}).get("categories") or [])]
        if not cover:
            out.append(f"{c}: no P1 run covers it")
            continue
        open_items = {}
        for name, task in cover:
            listed = {key(i) for i in
                      (task.get("followup") or {}).get("items") or []}
            # The items it listed are cleared one by one; what it left
            # open itself takes their place.
            for k in listed:
                open_items.pop(k, None)
            for f in left_by(task, c):
                open_items[key(f)] = name
        for name in sorted(set(open_items.values())):
            n = sum(1 for v in open_items.values() if v == name)
            out.append(f"{c}: {name} left {n} P1 items unresolved")
    return out


def t6_open(results):
    """What stands between the last P5/P6 check and T6: a part changed
    after it, or conflicts and gaps it listed with no pass 2 follow-up.
    Also the conflicts and gaps the checks leave, which the pages state as
    not known: each the last check lists, and each of an earlier check that
    no P2-P4 follow-up after it covered. Returns (what is open, what is
    left)."""
    done = runs(results)
    checks = [(n, t) for n, t in done if t.get("task") == "T5" or
              (t.get("followup") or {}).get("phases") == "P5-P6"]
    if not checks:
        return ["no P5/P6 check is recorded"], []
    name, last = checks[-1]
    # An effective entry the check did not see: a function bound to other
    # parts, or the same parts requalified by a later run.
    eff = effective_selection(results)
    then = effective_selection(results, before=last.get("sequence", 0))

    def bound(sel, c):
        return {fn: (e.get("part"), e.get("alternate"), e.get("run"),
                     json.dumps(e.get("q_alternatives") or [],
                                sort_keys=True))
                for fn, e in sel.get(c, {}).items()}
    out = [f"{c}: selections changed after {name}"
           for c in sorted(set(eff) | set(then))
           if bound(eff, c) != bound(then, c)]
    # A P2-P4 follow-up of these passes (the follow-up file's round)
    # covers each function it verified a part for, and each category it
    # verified any part in; not what its file named.
    def covered_between(start, end, rounds):
        fns = {(c, e["function"]) for _, t in done
               if (t.get("followup") or {}).get("round") in rounds
               and (t.get("followup") or {}).get("phases") == "P2-P4"
               and start < t.get("sequence", 0) < end
               for c, entries in ((t.get("summary") or {}).get("selection")
                                  or {}).items()
               for e in entries if e.get("part")}
        return fns, {c for c, _ in fns}
    summary = last.get("summary") or {}
    # A conflict's part points at every function any run selected it for,
    # in every category, retired functions aside: each of those functions
    # must be covered, and each category the conflict names or its parts
    # belong to. A part only retired functions kept points at none.
    part_fn = {}
    selected = set()
    for _, t in done:
        for c, entries in ((t.get("summary") or {}).get("selection")
                           or {}).items():
            for e in entries:
                # The kept part, its rule-5 alternate, and each Q
                # alternative with its own alternate.
                for part in [e.get("part"), e.get("alternate")] + [
                        p for q in e.get("q_alternatives") or []
                        for p in (q.get("part"), q.get("alternate"))]:
                    if part:
                        selected.add(part)
                    if part and e["function"] in eff.get(c, {}):
                        part_fn.setdefault(part, set()).add(
                            (c, e["function"]))

    # The category IDs a conflict's or gap's category names, a range such
    # as R5 to R8 naming each ID in it; a category holding any other word
    # names none. A part that points at no function, or a category that
    # names no ID, is never covered.
    def ids(cat):
        text = re.sub(r"\bR(\d+)\s*(?:-|–|—|to|through)\s*R(\d+)\b",
                      lambda m: " ".join(f"R{i}" for i in range(
                          int(m[1]), int(m[2]) + 1)), str(cat or ""))
        if re.sub(r"\bR\d+\b|\band\b|[\s,;/&]", "", text):
            return set()
        return set(re.findall(r"\bR\d+\b", text))
    unknown = set()

    def uncovered_items(check, fn_done, covered):
        out_items = []
        for x in (check.get("summary") or {}).get("conflicts", []):
            parts = x.get("parts") or []
            unknown.update(p for p in parts if p not in selected)
            fns = set().union(*(part_fn.get(p, set()) for p in parts))
            named = [ids(c) for c in x.get("categories") or []]
            cats = set().union(*named) | {c for c, _ in fns}
            if (not fns and not named) or not all(named) or \
                    any(p not in part_fn for p in parts) or \
                    not fns <= fn_done or not cats <= covered:
                out_items.append({"conflict": x})
        for x in (check.get("summary") or {}).get("gaps", []):
            gap_cats = ids(x.get("category"))
            if not gap_cats or not gap_cats <= covered:
                out_items.append({"gap": x})
        return out_items

    # The last check's items stand only when a pass 2 follow-up covered
    # them after the check before it (never for the first check, whose
    # items it found). An earlier check's items stay open, even when a
    # later check omits them, until a P2-P4 follow-up of either pass
    # covered them after that check.
    uncovered = []
    left = [{"check": name, "conflict": x}
            for x in summary.get("conflicts", [])] + [
        {"check": name, "gap": x} for x in summary.get("gaps", [])]
    if len(checks) > 1:
        uncovered += uncovered_items(last, *covered_between(
            checks[-2][1].get("sequence", 0), last.get("sequence", 0), (2,)))
        for earlier_name, earlier in checks[:-1]:
            items = uncovered_items(earlier, *covered_between(
                earlier.get("sequence", 0), last.get("sequence", 0),
                (1, 2)))
            uncovered += items
            left += [{"check": earlier_name, **x} for x in items]
    else:
        uncovered += uncovered_items(last, set(), set())
    if uncovered:
        out.append(f"{len(uncovered)} conflicts and gaps up to {name} that "
                   "no follow-up after their check covered")
    if unknown:
        out.append(f"conflicts up to {name} name parts no run selected: "
                   + ", ".join(sorted(unknown)))
    if summary.get("missing_checks"):
        out.append(f"{name} ran without "
                   + ", ".join(summary["missing_checks"]))
    if summary.get("unchecked_items"):
        out.append(f"{name} left {summary['unchecked_items']} combinations "
                   "and budgets its critic did not rule on")
    if summary.get("budgets_missing"):
        out.append(f"{name} has no budget for "
                   + ", ".join(summary["budgets_missing"]))
    if summary.get("rejected_items"):
        out.append(f"{name} has {summary['rejected_items']} combinations "
                   "and budgets its critic rejected")
    if summary.get("unsourced_items"):
        out.append(f"{name} has {summary['unsourced_items']} combinations "
                   "and budgets without source and time")
    if summary.get("budgets_over"):
        out.append(f"{name} has {summary['budgets_over']} budgets over "
                   "their limit")
    if summary.get("assumptions_missing"):
        out.append(f"{name} states no assumption for "
                   + ", ".join(summary["assumptions_missing"]))
    return out, left


def t6_inputs(results, text):
    """What T6 is given besides the plan: the P5/P6 checks in record order,
    the last of them, the conflicts and gaps they leave, the assumptions of
    the last check's P5 that its critic upheld, which the pages state
    beside the budgets that rest on them, the parts selection.json keeps,
    and the sentences of the stock tool's Outputs row. Returns (what is
    open before T6, the arguments)."""
    open_t6, left = t6_open(results)
    checks = [(n, t) for n, t in runs(results) if t.get("task") == "T5" or
              (t.get("followup") or {}).get("phases") == "P5-P6"]
    last = (checks[-1][1].get("summary") or {}) if checks else {}
    return open_t6, {
        "last_p56": checks[-1][0] if checks else "",
        "p56_runs": [n for n, _ in checks],
        "left_open": left,
        "p5_assumed": [{k: x.get(k, "") for k in ("item", "value", "why")}
                       for x in last.get("assumptions") or []
                       if x.get("upheld") is True],
        "selection": effective_selection(results),
        "jlc_stock_row": jlc_stock_row(text)}


def answers(rows, c, skip=()):
    return {r[0]: r[3].strip() for r in rows if r[1] == c
            and r[0] not in skip}


def feeds_suffix(q):
    """The text `raised` appends to a question that only feeds a decision:
    " (feeds Q4 only)" for one with `decision` Q4, empty for any other."""
    if q.get("blocks") == "decision-only":
        return f" (feeds {q.get('decision')} only)"
    return ""


def decision_only(results):
    """The questions committed P1 runs raised that only feed Q4, Q8 or Q9:
    they hold neither P2 nor its task."""
    return {q["id"] for _, q in committed_questions(results)
            if q.get("blocks") == "decision-only"
            and q.get("decision") in ("Q4", "Q8", "Q9")}


def deciding_runs(results, c, eff):
    """The runs whose selection decides at least one of c's functions, in
    record order: (name, task)."""
    names = {e["run"] for e in eff.get(c, {}).values()}
    return [(n, t) for n, t in runs(results) if n in names]


def stale_selections(results, categories, rows, now, upstream=(),
                     downstream=()):
    """Each run whose parts for a category were not qualified against what
    is in force now: a P1 run raised questions for the category after it;
    the category's answers under "Raised by P1", or the specification,
    differ from those at the plan commit it read (answers to questions
    that only feed a decision aside); or, for a category that takes the
    parts of T2 and T4 (`downstream`, those of T3), the `upstream` parts
    differ from those selection.json held when it ran."""
    done = runs(results)
    seq = {n: t.get("sequence", 0) for n, t in done}
    eff = effective_selection(results)
    skip = decision_only(results)
    rows_at, spec_at, up_at = {}, {}, {}

    def rows_of(commit):
        if commit not in rows_at:
            shown = git("show", f"{commit}:{PLAN_REL}", check=False)
            rows_at[commit] = raised_rows(shown.stdout) \
                if shown.returncode == 0 else None
        return rows_at[commit]

    def spec_of(commit):
        if commit not in spec_at:
            spec_at[commit] = spec_text(commit)
        return spec_at[commit]

    def up_of(before):
        # Each upstream function's part, alternate, Q alternatives and the
        # run that qualified them: a swap or a requalification is a change.
        if before not in up_at:
            sel = effective_selection(results, before=before)
            up_at[before] = {(u, fn): (e.get("part"), e.get("alternate"),
                                       e.get("run"), json.dumps(
                                           e.get("q_alternatives") or [],
                                           sort_keys=True))
                             for u in upstream
                             for fn, e in sel.get(u, {}).items()}
        return up_at[before]

    out = []
    for c in categories:
        # Every P1 run covering the category: its questions, or a function
        # it added to the inventory, can change what the parts must meet.
        p1 = [n for n, t in done if t.get("task") == "T1" or (
            (t.get("followup") or {}).get("phases") == "P1" and c in (
                (t.get("followup") or {}).get("categories") or []))]
        for name, task in deciding_runs(results, c, eff):
            commit = task.get("commit", "")
            if p1 and seq[p1[-1]] > seq[name]:
                out.append(f"{c}: {p1[-1]} read the category after {name} "
                           "selected its parts")
            if rows_of(commit) is None or spec_of(commit) is None:
                out.append(f"{c}: the plan {name} read, at {commit}, is not "
                           "in this clone")
                continue
            if answers(rows_of(commit), c, skip) != answers(rows, c, skip):
                out.append(f"{c}: the answers under Raised by P1 changed "
                           f"after {name} selected its parts")
            if spec_of(commit) != spec_of(now):
                out.append(f"{c}: the specification changed after {name} "
                           "selected its parts")
            if c in downstream and up_of(seq[name]) != up_of(None):
                out.append(f"{c}: parts of T2 or T4 changed after {name} "
                           "selected its parts")
    return out


def q9_open(results, now):
    """An R3 function decided by a run that was not given the Q9 decision
    now in force: a choice that adds or changes a part needs an R3
    follow-up told of it, and a P5/P6 check after that (the plan's
    follow-up tasks)."""
    eff = effective_selection(results)
    out = []
    for name, task in deciding_runs(results, "R3", eff):
        then = (task.get("decisions") or {}).get("Q9", "")
        if then.strip() != now.get("Q9", "").strip():
            out.append(f"R3: {name} selected its parts without the Q9 "
                       "decision now in force; a choice that adds or "
                       "changes a part needs an R3 follow-up and a P5/P6 "
                       "check after it")
    return out


def inventory(results, categories):
    """Each category's function inventory, from the latest P1 run that
    returned one for it."""
    out = {}
    for _, t in runs(results):
        if t.get("task") == "T1" or (t.get("followup") or {}).get(
                "phases") == "P1":
            for c, names in ((t.get("summary") or {}).get("inventory")
                             or {}).items():
                if c in categories and names:
                    out[c] = names
    return out


def refuted_for_itself(results, categories):
    """Each kept part a run refuted for itself (the adjudicator's scope
    part: lifecycle, end-of-life, maker, identity), in any category, at or
    after the run that decided its function."""
    done = runs(results)
    seq = {n: t.get("sequence", 0) for n, t in done}
    refuted = [(n, p) for n, t in done
               for parts in ((t.get("summary") or {}).get("part_refuted")
                             or {}).values() for p in parts]
    out = []
    for c, fns in effective_selection(results).items():
        if c not in categories:
            continue
        for fn, e in fns.items():
            # The kept part, its rule-5 alternate, each Q alternative and
            # that one's alternate.
            used = [e.get("part"), e.get("alternate")] + [
                p for q in e.get("q_alternatives") or []
                for p in (q.get("part"), q.get("alternate"))]
            for name, part in refuted:
                if part in used and seq[name] >= seq.get(e.get("run"), 0):
                    out.append(f"{c}: {fn} uses {part}, which {name} "
                               "refuted for itself")
    return out


def open_in_category(results, categories):
    """What the runs that decide each category's functions left open for
    the whole category: figures not returned or not confirmed; and in R10
    and R12 no Q4 or Q8 alternative."""
    eff = effective_selection(results)
    out = []
    for c in categories:
        for name, task in deciding_runs(results, c, eff):
            figs = ((task.get("summary") or {}).get("figures_open")
                    or {}).get(c, [])
            out += [f"{c}: figure {f} not confirmed in {name}" for f in figs]
        # The function that implements Q4 (R10) or Q8 (R12) carries the
        # owner's alternatives.
        q = {"R10": "Q4", "R12": "Q8"}.get(c)
        deciders = [e for e in eff.get(c, {}).values()
                    if e.get("decision") == q] if q else []
        if q and eff.get(c) and len(deciders) > 1:
            out.append(f"{c}: {len(deciders)} functions implement {q}")
        elif q and eff.get(c) and not (
                deciders and deciders[0].get("q_alternatives")
                and not deciders[0].get("q_options_missing")):
            out.append(f"{c}: no function implementing {q} has an "
                       "alternative for each of its options")
    return out


def open_selections(eff, categories):
    """Each function with no verified part, and each category with no
    selection at all."""
    out = []
    for c in categories:
        if not eff.get(c):
            out.append(f"{c}: no part selected for any function")
        out += [f"{c}: {fn}" + (f" ({e['not_requalified']} not requalified "
                                f"by {e['run']})"
                                if e.get("not_requalified") else "")
                for fn, e in eff.get(c, {}).items() if not e["part"]]
        out += [f"{c}: {fn} (alternate {', '.join(e['alternate_unverified'])}"
                " not verified)" for fn, e in eff.get(c, {}).items()
                if e["part"] and e.get("alternate_unverified")]
        out += [f"{c}: {fn} (no second source)"
                for fn, e in eff.get(c, {}).items()
                if e["part"] and e.get("second_source_missing")]
        # R10 and R12 keep an owner-selectable alternative for Q4 and Q8;
        # each must be verified, with its second source, as the part kept.
        out += [f"{c}: {fn} (Q alternative {q['part']}: {why})"
                for fn, e in eff.get(c, {}).items()
                for q in e.get("q_alternatives") or []
                for why in q_open(q)]
    return out


def q_open(q):
    why = []
    if not str(q.get("status", "")).startswith("verified"):
        why.append(q.get("status") or "not verified")
    if q.get("alternate") and not str(
            q.get("alternate_status", "")).startswith("verified"):
        why.append(f"alternate {q['alternate']} not verified")
    if q.get("second_source_missing"):
        why.append("no second source")
    return why


EFFORTS = ["low", "medium", "high", "xhigh", "max"]
MODELS = ["sonnet", "opus", "haiku", "fable"]


def run_info(model, effort):
    try:
        claude = subprocess.run(["claude", "--version"], capture_output=True,
                                text=True).stdout.strip()
    except OSError:
        claude = "not found"
    cpus = os.cpu_count() or 0
    env = os.environ.get("CLAUDE_CODE_WORKFLOW_MAX_CONCURRENT_AGENTS")
    return {"claude_code": claude, "cpus": cpus, "model": model,
            "effort": effort,
            "concurrency": int(env) if env else min(16, max(1, cpus - 2)),
            "concurrency_setting": env or "unset"}


def not_on_research_branch():
    here = git("branch", "--show-current")
    if here in (BRANCH, RESULTS):
        raise SystemExit(f"this clone is on {here}; git cannot check the "
                         "branch out twice. Run from a clone on main.")


NAME = re.compile(r"[A-Za-z0-9]+")
GROUP_PAGE = re.compile(r"hardware/docs/[A-Za-z0-9_-]+\.md")


def p5_chips(cats):
    """The chips P5 budgets one by one in this round, and the budgets owed
    once for each: from round 2 the main and the measurement coprocessor
    (Research.md, Round 2, Rules of stage 1). Empty before that round."""
    chips = cats.get("p5_chips") or {}
    if ROUND < chips.get("from_round", ROUND + 1):
        return {}
    return {"chips": chips.get("chips", []),
            "budgets": chips.get("budgets", [])}


def check_followup(followup, cats):
    """The follow-up file: its phases, round, categories and items. Its
    `round` is the pass of follow-up tasks within the research round."""
    if followup.get("phases") not in ("P1", "P2-P4", "P5-P6"):
        raise SystemExit("the follow-up's phases is P1, P2-P4 or P5-P6")
    if followup.get("round") not in (1, 2):
        raise SystemExit("follow-up tasks run in rounds 1 and 2 only"
                         if ROUND == 1 else "a follow-up's pass, its round "
                         "field, is 1 or 2")
    fc = followup.get("categories")
    items = followup.get("items", [])
    if not isinstance(items, list):
        raise SystemExit("the follow-up's items are a list")
    if followup["phases"] == "P5-P6":
        if fc or items:
            raise SystemExit("a P5-P6 follow-up takes no categories or items; "
                             "it checks the whole board")
        return
    if (not isinstance(fc, list) or not fc or len(set(fc)) != len(fc)
            or any(c not in cats["categories"] for c in fc)):
        raise SystemExit("the follow-up's categories are a list of distinct "
                         "IDs from R1 to R13")
    stray = [i for i in items
             if not isinstance(i, dict) or i.get("category") not in fc]
    if stray:
        raise SystemExit("each follow-up item names one of the follow-up's "
                         f"categories: {stray}")


def pre_t6_merge(commit):
    """The merge prepare makes before T6: two parents, the second a pushed
    commit of the round's plan branch, and nothing beyond it but the run
    records."""
    parents = git("rev-list", "--parents", "-n", "1", commit).split()[1:]
    if len(parents) != 2 or not git("log", "-1", "--format=%s", commit) \
            .startswith(f"Merge {BRANCH} at "):
        return False
    if git("merge-base", "--is-ancestor", parents[1], f"origin/{BRANCH}",
           check=False).returncode:
        return False
    # The records are the results branch's own: the merge changes none.
    if git("diff", "--quiet", parents[0], commit, "--", *RUNS_DIRS,
           check=False).returncode:
        return False
    dirs = tuple(posix(d) + "/" for d in RUNS_DIRS)
    return all(p.startswith(dirs) for p in git(
        "diff", "--name-only", parents[1], commit).splitlines())


def sync_results(results):
    """The results tree at origin's head: fast-forwarded when behind, and
    refused when it holds records origin lacks or has diverged."""
    remote = f"origin/{RESULTS}"
    if git("rev-parse", "--verify", "-q", remote, check=False).returncode:
        raise SystemExit(f"{remote} is not there; push {RESULTS} first")
    here = git("-C", results, "rev-parse", "HEAD")
    there = git("rev-parse", remote)
    if here == there:
        return
    if git("merge-base", "--is-ancestor", here, there,
           check=False).returncode == 0:
        if git("-C", results, "merge", "-q", "--ff-only", there,
               check=False).returncode:
            raise SystemExit(f"fast-forwarding {results} to {remote} failed")
        return
    if git("merge-base", "--is-ancestor", there, here,
           check=False).returncode == 0:
        # Ahead by the merge before T6 alone is fine: a merge of a pushed
        # plan branch commit that adds nothing but the records. Ahead
        # by records is not, as other clones cannot see them; ahead by
        # anything else is a commit nobody reviewed.
        if all(pre_t6_merge(c) for c in
               git("rev-list", "--first-parent", f"{there}..{here}").split()):
            return
        if git("diff", "--quiet", there, here, "--", *RUNS_DIRS,
               check=False).returncode:
            raise SystemExit(f"{RESULTS} holds records {remote} does not; "
                             "push it first")
        raise SystemExit(f"{RESULTS} holds commits that are neither records "
                         f"nor the merge before T6; remove them (git -C "
                         f"{results} reset --hard {there}) or review and "
                         "push them")
    raise SystemExit(f"{RESULTS} and {remote} have diverged")


def on_branch(tree, branch):
    here = git("-C", tree, "branch", "--show-current")
    if here != branch:
        raise SystemExit(f"{tree} is on {here or 'a detached HEAD'}, "
                         f"not {branch}")


def blocking_gate(text, task, categories, is_p1, reads_stock):
    """The Blocking and Sourcing questions a run waits on, answered."""
    src = sourcing_answers(text)
    need = list(SOURCING_T1) if is_p1 or task == "T1" else []
    if reads_stock:
        need += list(SOURCING_STOCK)
        need += [q for c in categories for q in SOURCING_CATEGORY.get(c, ())]
    open_q = [q for q in need if not src.get(q, "").strip()]
    if reads_stock:
        open_q += [i for i, blocks, answer in blocking_rows(text)
                   if blocks & set(categories) and not answer.strip()]
    if open_q:
        raise SystemExit("unanswered questions this run waits on: "
                         + ", ".join(open_q))


def questions_gate(results, rows, categories):
    """Every question a committed P1 run raised for these categories is
    published and answered, unless it only feeds Q4, Q8 or Q9."""
    committed = committed_questions(results)
    ids = {r[0] for r in rows}
    unpublished = [f"{q['id']} ({run})" for run, q in committed
                   if q["category"] in categories and q["id"] not in ids]
    if unpublished:
        raise SystemExit("raised by P1 but not under Raised by P1 on "
                         f"{BRANCH}: " + ", ".join(unpublished)
                         + ". Run raised and push it.")
    # A published row keeps the category and question P1 raised.
    by_id = {r[0]: r for r in rows}

    def words(t):
        return " ".join(str(t).split())
    # The suffix is the one `raised` wrote for the question, or none.
    moved = [q["id"] for _, q in committed if q["id"] in by_id
             and (q["category"] in categories
                  or by_id[q["id"]][1] in categories)
             and (by_id[q["id"]][1] != q["category"]
                  or words(by_id[q["id"]][2]) != words(
                      str(q.get("question", "")).replace("|", "\\|"))
                  + feeds_suffix(q))]
    if moved:
        raise SystemExit("rows under Raised by P1 that no longer carry the "
                         "category and question P1 raised: "
                         + ", ".join(moved))
    # Only a question that names Q4, Q8 or Q9 feeds a decision alone.
    exempt = decision_only(results)
    open_q = [r[0] for r in rows if r[1] in categories and not r[3].strip()
              and r[0] not in exempt]
    if open_q:
        raise SystemExit("unanswered under Raised by P1: " + ", ".join(open_q))


def pending_p1(base, results, run):
    """Another P1 run prepared and not recorded, whose question IDs this
    run would repeat. A run recorded as stopped raised none."""
    settled = {t["run_id"] for _, t in runs(results, stopped=True)
               if t.get("run_id")}
    out = []
    for name in os.listdir(base):
        m = re.fullmatch(r"args-(.+)\.json", name)
        if not m or m.group(1) == run:
            continue
        a = json.loads(read(os.path.join(base, name)))
        is_p1 = a.get("task") == "T1" or \
            (a.get("followup") or {}).get("phases") == "P1"
        if is_p1 and not recorded_any(results, m.group(1)) \
                and a.get("run_id") not in settled:
            out.append(m.group(1))
    return out


def turn(results, task, run, followup, cats):
    """The run's turn, refused out of order. Round 1 runs T1 to T6 in
    their order and follow-ups after the tasks they build on, until its T6
    closes it. A later round runs follow-ups and a T6 of its own after the
    previous round's T6, a P2-P4 follow-up after P1 follow-ups of its own
    that cover its categories and after P2-P4 follow-ups of its own in the
    categories its task builds on, a P5-P6 follow-up after P2-P4 follow-ups
    of its own in every category its P1 follow-ups cover, and its T6 after
    a P5-P6 follow-up of its own; a run name is unique over the rounds, T6
    aside."""
    if ROUND == 1:
        if recorded(results, run):
            raise SystemExit(f"{run} is recorded already")
        # The pages T6 committed close the round: no run changes what they
        # rest on.
        if recorded(results, "T6"):
            raise SystemExit("T6 is recorded; round 1 is closed")
    else:
        prev = ROUND - 1
        if task not in ("FU", "T6"):
            raise SystemExit("T1 to T5 run in round 1 only")
        if not recorded(results, "T6", (runs_dir(prev),)):
            raise SystemExit(f"round {ROUND} starts after round {prev}'s T6")
        if recorded(results, run) if run == "T6" else \
                recorded_any(results, run):
            raise SystemExit(f"{run} is recorded already")
        if recorded(results, "T6"):
            raise SystemExit(f"T6 of round {ROUND} is recorded; round "
                             f"{ROUND} is closed")
    phases = (followup or {}).get("phases")
    owner = {c: t for t, v in cats["tasks"].items() for c in v["categories"]}
    after = list(AFTER.get(task, []))
    if phases == "P2-P4":
        after += sorted({owner[c] for c in followup["categories"]})
    elif phases == "P1":
        after += ["T1"]
    elif phases == "P5-P6":
        after += ["T5"]
    for t in after:
        if not recorded_any(results, t):
            raise SystemExit(f"{run} runs after {t}, which is not "
                             "recorded")
    # The passes of follow-up tasks count within the round.
    done = [t for _, t in runs(results) if t.get("research_round") == ROUND]
    if ROUND > 1 and task == "T6" and not any(
            (t.get("followup") or {}).get("phases") == "P5-P6" for t in done):
        raise SystemExit(f"T6 of round {ROUND} runs after a P5-P6 follow-up "
                         f"of round {ROUND}, which is not recorded")
    if phases and followup.get("round") == 2 and not any(
            (t.get("followup") or {}).get("round") == 1 for t in done):
        raise SystemExit("a round 2 follow-up runs after a round 1 "
                         "follow-up is recorded" if ROUND == 1 else
                         f"a pass 2 follow-up runs after a pass 1 follow-up "
                         f"of round {ROUND} is recorded")
    # A later round's P2-P4 follow-up selects against the requirements its
    # own P1 follow-ups confirmed, and after its own P2-P4 follow-ups in the
    # categories its task builds on (T3 on T2 and T4), as the plan's Runs
    # table orders them. Its P5-P6 follow-up checks the board once its own
    # P2-P4 follow-ups cover every category its P1 follow-ups cover.
    if ROUND > 1 and phases in ("P2-P4", "P5-P6"):
        def covered(kind):
            return {c for t in done
                    if (t.get("followup") or {}).get("phases") == kind
                    for c in (t.get("followup") or {}).get("categories")
                    or []}
        asked, chosen = covered("P1"), covered("P2-P4")
        order = sorted(cats["categories"], key=lambda c: int(c[1:]))
        if phases == "P2-P4":
            miss = [c for c in followup["categories"] if c not in asked]
            if miss:
                raise SystemExit(f"{run} runs after a P1 follow-up of round "
                                 f"{ROUND} covering {', '.join(miss)}, which "
                                 "is not recorded")
            upstream = {u for c in followup["categories"]
                        for t in cats["tasks"][owner[c]].get("after", [])
                        for u in cats["tasks"][t]["categories"]}
            need = upstream & asked
        else:
            need = asked
        miss = [c for c in order if c in need and c not in chosen]
        if miss:
            raise SystemExit(f"{run} runs after P2-P4 follow-ups of round "
                             f"{ROUND} covering {', '.join(miss)}, which are "
                             "not recorded")


def selection_file(results):
    """The selection.json a run reads, repository-relative: that of the
    latest round whose runs selected parts."""
    for d in reversed(RUNS_DIRS):
        rel = posix(d) + "/selection.json"
        if git("-C", results, "cat-file", "-e", f"HEAD:{rel}",
               check=False).returncode == 0:
            return rel
    return posix(RUNS_DIR) + "/selection.json"


def group_pages(results):
    """The group pages the previous round's T6 wrote, from its P7 return:
    {group: page}."""
    prev = posix(runs_dir(ROUND - 1)) + "/T6"
    listed = git("-C", results, "ls-tree", "--name-only", "HEAD", prev + "/",
                 check=False).stdout.split()
    p7 = [p for p in listed
          if re.fullmatch(r"\d+-P7(-restart)?\.json", os.path.basename(p))]
    pages = json.loads(git("-C", results, "show", f"HEAD:{p7[-1]}"))[
        "data"].get("group_pages") if p7 else None
    if not pages:
        raise SystemExit(f"{prev} holds no P7 return that names the group "
                         "pages")
    return pages


def database_gate(db, manifest, info):
    """The parts database is the saved copy of jlcparts.json: its SHA-256,
    its jlc_components rows and its manifest's created time, read here
    rather than taken from P0's return."""
    digest = hashlib.sha256()
    with open(db, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    wrong = []
    if digest.hexdigest() != info["sha256"]:
        wrong.append(f"SHA-256 {digest.hexdigest()}, not {info['sha256']}")
    con = sqlite3.connect(pathlib.Path(db).as_uri() + "?mode=ro", uri=True)
    try:
        rows = con.execute("SELECT COUNT(*) FROM jlc_components").fetchone()[0]
        if rows != info["rows"]:
            wrong.append(f"{rows} jlc_components rows, not {info['rows']}")
    except sqlite3.Error as err:
        wrong.append(f"jlc_components not readable: {err}")
    finally:
        con.close()

    def when(t):
        try:
            return datetime.datetime.fromisoformat(
                str(t).strip().replace("Z", "+00:00"))
        except ValueError:
            return None
    try:
        created = json.loads(read(manifest)).get("created")
    except (ValueError, AttributeError):
        created = None
    if when(created) is None or when(created) != when(
            info["manifest_created"]):
        wrong.append(f"manifest created {created}, not "
                     f"{info['manifest_created']}")
    if wrong:
        raise SystemExit(f"{db} is not the saved parts database: "
                         + "; ".join(wrong))


def cmd_prepare(args):
    not_on_research_branch()
    base = os.path.abspath(os.path.expanduser(args.base))
    cats = load("categories.json")
    info = load("jlcparts.json")
    db = os.path.abspath(os.path.expanduser(args.db or info["path"]))
    manifest = os.path.join(os.path.dirname(db), "manifest.json")
    env = os.path.abspath(os.path.expanduser(args.digikey_env))
    if not NAME.fullmatch(args.name) or "stopped" in args.name:
        raise SystemExit("--name is letters and digits, without 'stopped'")
    followup = None
    if args.task == "FU":
        if not args.followup:
            raise SystemExit("FU needs --followup FILE")
        followup = json.loads(read(args.followup))
        check_followup(followup, cats)
    # The owner's stock exceptions, T6 only: PART=REASON each. Read before
    # any merge, so a malformed one leaves the results tree untouched.
    stock_exceptions = []
    for x in args.stock_exception:
        part, _, why = x.partition("=")
        if not part.strip() or not why.strip():
            raise SystemExit(f"--stock-exception {x!r}: give PART=REASON")
        stock_exceptions.append({"part": part.strip(), "reason": why.strip()})
    if stock_exceptions and args.task != "T6":
        raise SystemExit("--stock-exception applies to T6 only")
    for path in (db, manifest, env):
        if not os.path.isfile(path):
            raise SystemExit(f"{path} is not there")
    database_gate(db, manifest, info)
    if not args.no_fetch:
        git("fetch", "-q", "origin")
    commit = git("rev-parse", f"origin/{BRANCH}")
    text = git("show", f"{commit}:{PLAN_REL}")
    run = f"FU-{args.name}" if args.task == "FU" else args.task
    # A later round's prompts name its section of the plan.
    if ROUND > 1 and f"\n## Round {ROUND}\n" not in text:
        raise SystemExit(f"{PLAN_REL} at {commit} has no section "
                         f"\"Round {ROUND}\"")

    # The results tree: on its branch, its recorded runs committed.
    os.makedirs(base, exist_ok=True)
    results = worktree(base, "results", RESULTS, detach=False)
    if any(changed_under(results, d) for d in RUNS_DIRS):
        raise SystemExit("recorded runs have uncommitted changes")
    # The gates read the runs every clone has recorded and pushed.
    sync_results(results)

    # The task's turn.
    turn(results, args.task, run, followup, cats)
    phases = (followup or {}).get("phases")
    is_p1 = args.task == "T1" or phases == "P1"
    if is_p1:
        waiting = pending_p1(base, results, run)
        if waiting:
            raise SystemExit("P1 runs prepared and not recorded: "
                             + ", ".join(waiting)
                             + "; record them first, so question IDs do "
                             "not repeat")

    # The gates.
    rows = raised_rows(text)
    task_cats = (followup or {}).get("categories") or \
        cats["tasks"].get(args.task, {}).get("categories", [])
    # P5 re-reads the stock of shared parts: T5 and P5-P6 follow-ups read
    # stock too.
    reads_stock = args.task in cats["tasks"] or args.task == "T5" or \
        phases in ("P2-P4", "P5-P6")
    open_sel = []
    blocking_gate(text, args.task, task_cats, is_p1, reads_stock)
    if reads_stock or is_p1 and args.task == "FU":
        questions_gate(results, rows, task_cats)
    if args.task == "T5" or phases == "P5-P6":
        questions_gate(results, rows, list(cats["categories"]))
    if reads_stock:
        open_sel += p1_unresolved(results, task_cats)
    # T3 and a P2-P4 follow-up on its categories take the parts T2 and T4
    # selected; T5, a P5-P6 follow-up and T6 take every category's.
    up = [c for t in ("T2", "T4") for c in cats["tasks"][t]["categories"]]
    if args.task == "T3" or phases == "P2-P4" and set(
            followup["categories"]) & set(cats["tasks"]["T3"]["categories"]):
        gate_cats = up
    elif args.task in ("T5", "T6") or phases == "P5-P6":
        gate_cats = list(cats["categories"])
    else:
        gate_cats = []
    open_sel += open_selections(effective_selection(results), gate_cats)
    open_sel += open_in_category(results, gate_cats)
    open_sel += refuted_for_itself(results, gate_cats)
    open_sel += stale_selections(results, gate_cats, rows, commit, up,
                                 cats["tasks"]["T3"]["categories"])
    open_sel += [o for o in p1_unresolved(results, gate_cats)
                 if o not in open_sel]
    t6 = {"last_p56": "", "p56_runs": [], "left_open": [], "p5_assumed": [],
          "selection": {}, "jlc_stock_row": []}
    pages = None
    if args.task == "T5" or phases == "P5-P6":
        t6["selection"] = effective_selection(results)
    if args.task == "T6":
        questions_gate(results, rows, list(cats["categories"]))
        open_t6, t6 = t6_inputs(results, text)
        open_sel += open_t6
        dirty = [d for d in changed(results) if t6_output(d)]
        if dirty:
            raise SystemExit("the output paths have changes; commit or "
                             "remove them before T6: " + ", ".join(dirty))
        # Each of Q4, Q8 and Q9 has its row and the owner's decision.
        found = decisions(text)
        missing = [q for q in ("Q4", "Q8", "Q9") if not found.get(q)]
        if missing:
            raise SystemExit("the owner has not decided " + ", ".join(missing))
        open_sel += q9_open(results, decisions(text))
        # A later round updates the group pages the round before wrote.
        if ROUND > 1:
            pages = group_pages(results)
        # An exception names a part the stock check reads: a kept part, an
        # alternate, a Q alternative or its alternate.
        kept = set()
        for fns in t6["selection"].values():
            for e in fns.values():
                kept |= {e.get("part"), e.get("alternate")}
                for q in e.get("q_alternatives") or []:
                    kept |= {q.get("part"), q.get("alternate")}
        stray = [x["part"] for x in stock_exceptions if x["part"] not in kept]
        if stray:
            raise SystemExit("--stock-exception names parts no function "
                             "keeps: " + ", ".join(stray))
    if open_sel and not args.accept_open:
        raise SystemExit("open before this run: " + "; ".join(open_sel)
                         + ". Resolve them in a follow-up task, or pass "
                         "--accept-open REASON")

    # Every refusal is behind us: make the trees, and merge before T6.
    checkout = worktree(base, f"checkout-{commit[:12]}", commit)
    read_only(checkout)
    mono = worktree(base, "monostable-23c82ca", MONOSTABLE)
    read_only(mono)
    if args.task == "T6":
        merged = git("-C", results, "merge", "--no-edit", "-m",
                     f"Merge {BRANCH} at {commit[:12]} before T6", commit,
                     check=False)
        if merged.returncode:
            git("-C", results, "merge", "--abort", check=False)
            raise SystemExit(f"merging {BRANCH} into the results tree "
                             "failed: " + merged.stderr.strip())
    # A run that reads stock starts only with enough of the day's Digi-Key
    # calls left to finish: 1,000 a day, and a task takes 400 to 500.
    quota = None
    if reads_stock and args.digikey_min > 0:
        os.environ["DIGIKEY_ENV_FILE"] = env
        try:
            code, quota, reset = vendors.dk_quota()
        except vendors.Unreachable as err:
            raise SystemExit(f"Digi-Key's API is unreachable: {err}")
        if quota is None:
            raise SystemExit(f"Digi-Key's API answered HTTP {code} with no "
                             "count of calls left")
        if quota < args.digikey_min:
            raise SystemExit(f"{quota} Digi-Key calls left today, fewer than "
                             f"--digikey-min {args.digikey_min}; the count "
                             f"resets at {reset}")
    now = datetime.datetime.now(datetime.timezone.utc)
    today = now.date().isoformat()
    ids = [int(r[0][1:]) for r in rows] + [
        int(q["id"][1:]) for _, q in committed_questions(results)]
    first_v = 1 + max(ids or [0])
    ident = json.dumps([run, commit, now.isoformat(), followup, args.model,
                        args.effort], sort_keys=True)
    run_id = hashlib.sha256(ident.encode()).hexdigest()[:16]
    for_research = [{"id": r[0], "category": r[1]} for r in rows
                    if "for research" in r[3].lower()]
    # The agents run this tool's vendors.py, copied for the run so a branch
    # switch in this clone does not change it; the checkout of the round's
    # plan branch carries the copy of the day the branch was cut.
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "vendors.py")
    vendors_copy = os.path.join(base, "tools", run_id, "vendors.py")
    os.makedirs(os.path.dirname(vendors_copy), exist_ok=True)
    shutil.copyfile(src, vendors_copy)
    vendors_sha = hashlib.sha256(open(vendors_copy, "rb").read()).hexdigest()
    out = {
        "task": args.task, "mode": "run", "date": today, "commit": commit,
        "cap": cats["cap"], "categories": cats["categories"],
        "tasks": cats["tasks"], "schemas": resolved_schemas(),
        "hosts": load("hosts.json")["hosts"],
        "clients": load("hosts.json")["clients"],
        "jlcparts": {"sha256": info["sha256"], "rows": info["rows"],
                     "manifest": manifest,
                     "manifest_created": info["manifest_created"],
                     "lcsc": plan_lcsc(text)},
        "paths": {"checkout": checkout, "monostable": mono, "db": db,
                  "results": results,
                  "scratch": os.path.join(base, "scratch"),
                  "digikey_env": env,
                  "digikey_cache": os.path.join(base, "cache", "digikey"),
                  "vendors": vendors_copy},
        "followup": followup, "first_v": first_v,
        "decisions": decisions(text),
        "decision_categories": decision_categories(text),
        "run": run, "run_id": run_id,
        "results_head": git("-C", results, "rev-parse", "HEAD"),
        "for_research": for_research,
        "raised": raised_questions(results),
        "p1_asks": cats.get("p1_asks", {}),
        "fixed_inputs": cats.get("fixed_inputs", {}),
        "required_reports": cats.get("reports", {}),
        "per_part_reports": cats.get("reports_per_part", {}),
        "inventory": inventory(results, list(cats["categories"])),
        "p5_budgets": cats.get("p5_budgets", []),
        "p5_conditional": cats.get("p5_conditional", []),
        "p5_assumptions": cats.get("p5_assumptions", []),
        "p5_chips": p5_chips(cats),
        "q_options": cats.get("q_options", {}),
        "research_round": ROUND, "branch": BRANCH,
        "runs_dir": posix(RUNS_DIR),
        "runs_dirs": [posix(d) for d in RUNS_DIRS],
        "selection_file": selection_file(results),
        "group_pages": pages,
        "t6_outputs": T6_WRITTEN[ROUND] if args.task == "T6" else [],
        "t6_may_write": [f for f in T6_REQUIRED if f not in T6_WRITTEN[ROUND]]
        if args.task == "T6" else [],
        **t6,
        "run_info": {**run_info(args.model, args.effort),
                     "agent_model": args.agent_model,
                     "oversight_model": args.oversight_model,
                     "digikey_calls_left": quota,
                     "vendors_sha256": vendors_sha},
        "accept_open": {"reason": args.accept_open, "functions": open_sel}
        if args.accept_open else None,
        "stock_exceptions": stock_exceptions,
    }
    path = os.path.join(base, f"args-{run}.json")
    with open(path, "w") as f:
        json.dump(out, f, indent=1)
    print(f"{path}: task {args.task}, {BRANCH} at {commit}, {today}, "
          f"questions from V{first_v}")
    return 0


# ----------------------------------------------------------------- record

def result_shape(result, want):
    """The whole workflow result: its fields, the summary its task writes,
    and the returns a run that did not stop cannot lack."""
    fields = {"summary": dict, "returns": list, "followUps": list,
              "missing": list, "started": int, "planned": int}
    bad = [k for k, kind in fields.items()
           if not isinstance(result.get(k), kind)
           or isinstance(result.get(k), bool)]
    if bad:
        raise SystemExit("the output lacks or misstates: " + ", ".join(bad))
    summary = result["summary"]
    roles = {r.get("role") for r in result["returns"] if isinstance(r, dict)}
    phases = (want.get("followup") or {}).get("phases")
    task = want.get("task")
    if summary.get("stopped"):
        if not isinstance(summary.get("reasons"), list):
            raise SystemExit("a stopped run gives its reasons")
        return
    checks = task == "T5" or phases == "P5-P6"
    if task == "T6":
        need, keys = ["P7", "P7-critic"], {}
    elif checks:
        # Each of the four agents returned, or the summary names it among
        # the missing checks.
        gone = summary.get("missing_checks") or []
        need = [r for r in ("P5", "P5-critic", "P6", "P6-critic")
                if r not in gone and r.split("-")[0] not in gone]
        keys = {"conflicts": list, "gaps": list, "missing_checks": list,
                "unchecked_items": int, "rejected_items": int,
                "budgets_missing": list, "unsourced_items": int,
                "budgets_over": int, "assumptions_missing": list}
    elif task == "T1" or phases == "P1":
        need, keys = ["P0"], {"questions": list, "inventory": dict}
    else:
        need, keys = ["P0"], {"selection": dict, "figures_open": dict,
                              "q_missing": list, "chain_failed": list}
    lacking = [r for r in need if r not in roles] + [
        f"summary.{k}" for k, kind in keys.items()
        if not isinstance(summary.get(k), kind)
        or isinstance(summary.get(k), bool)]
    if lacking:
        raise SystemExit("the output of a run that did not stop lacks: "
                         + ", ".join(lacking))


def set_aside(results, paths, result):
    """A refused T6 leaves the output paths clean for the next attempt;
    what P7 changed is kept in a stash, not lost."""
    if not paths:
        return ""
    name = f"refused T6 {result.get('run_id', '')}"
    git("-C", results, "stash", "push", "-q", "--include-untracked", "-m",
        name, "--", *paths, check=False)
    return f"; the changes are in the stash '{name}' of {results}"


def cited_returns(returns):
    """The files under RUNS_DIRS the P7 critic's figure checks name,
    repository-relative. The workflow counts a check that names no such
    file for no page. The pattern is round1.js's RETURN_FILE: plain path
    segments ending in .json, so ':34' or '#L34' after it is no part of
    the path."""
    runs_rel = "|".join(re.escape(posix(d)) for d in RUNS_DIRS)
    seg = "[A-Za-z0-9_-][A-Za-z0-9_.-]*"
    path = re.compile(rf"(?:^|[^A-Za-z0-9_.-])((?:{runs_rel})/"
                      rf"(?:{seg}/)*{seg}\.json)(?![A-Za-z0-9_./-])")
    out = set()
    for r in returns:
        if r.get("role") != "P7-critic":
            continue
        for x in r["data"].get("figure_checks", []):
            m = path.search(str(x.get("return_file", "")))
            if m:
                out.add(m.group(1))
    return out


def states(line, figure):
    """Whether the line states the figure as a value of its own: not part
    of a longer number or word, so "5 V" is not read in "15 V", "0.5 V",
    "-5 V" or "5 VA". Runs of whitespace compare as one space."""
    return re.search(r"(?<![0-9A-Za-z.,+\-\u2212\u00b1])"
                     + re.escape(" ".join(figure.split()))
                     + r"(?![0-9A-Za-z])(?![.,][0-9])",
                     " ".join(line.split())) is not None


def misplaced_figures(results, returns):
    """The P7 critic's figure checks whose page line does not state the
    figure, as the page stands after T6: a line below 1 or past the end, a
    file outside the tree or not a file, or a line that does not state the
    figure (`states`)."""
    root = os.path.realpath(results)
    out = []
    for r in returns:
        if r.get("role") != "P7-critic":
            continue
        for x in r["data"].get("figure_checks", []):
            f, n = str(x.get("file", "")), x.get("line")
            figure = " ".join(str(x.get("figure", "")).split())
            path = os.path.realpath(os.path.join(root, f))
            lines = (read(path).split("\n")
                     if path.startswith(root + os.sep)
                     and os.path.isfile(path) else [])
            if not (type(n) is int and 0 < n <= len(lines) and figure
                    and states(lines[n - 1], figure)):
                out.append(f"{f}:{n} {figure}")
    return out


def cmd_record(args):
    """record, with every refusal after T6 setting P7's changes aside."""
    try:
        return record(args)
    except (SystemExit, OSError, ValueError, KeyError, TypeError,
            AttributeError) as failed:
        # A malformed output fails like a refusal.
        refused = failed if isinstance(failed, SystemExit) else SystemExit(
            f"the output cannot be recorded: {failed!r}")
        results = os.path.join(os.path.abspath(os.path.expanduser(
            args.base)), "results")
        if args.task != "T6" or not os.path.isdir(results) or git(
                "-C", results, "branch", "--show-current",
                check=False).stdout.strip() != RESULTS:
            raise
        # The stash is named for the prepared run when the output is
        # unreadable.
        try:
            run_id = json.loads(read(os.path.join(os.path.dirname(results),
                                                  "args-T6.json")))["run_id"]
        except (OSError, ValueError, KeyError, TypeError):
            run_id = ""
        left = set_aside(results, changed(results), {"run_id": run_id})
        raise SystemExit(f"{refused}{left}") from None


def record(args):
    base = os.path.abspath(os.path.expanduser(args.base))
    results = os.path.join(base, "results")
    with open(args.output) as f:
        out = json.load(f)
    result = out.get("result", out)
    if result.get("mode") == "plan":
        raise SystemExit("the output is a plan or a refusal, not a run")
    if result.get("task") != args.task:
        raise SystemExit(f"the output is task {result.get('task')}")
    run = args.task if args.task != "FU" else f"FU-{args.name}"
    prepared = os.path.join(base, f"args-{run}.json")
    if not os.path.isfile(prepared):
        raise SystemExit(f"{prepared} is not there; prepare {run} first")
    want = json.loads(read(prepared))
    if want.get("research_round", 1) != ROUND:
        raise SystemExit(f"{prepared} is prepared for round "
                         f"{want.get('research_round', 1)}, not round {ROUND}")
    on_branch(results, RESULTS)
    head = git("-C", results, "rev-parse", "HEAD")
    if want.get("results_head") and head != want["results_head"]:
        raise SystemExit(f"the results tree moved from "
                         f"{want['results_head']} to {head} since prepare")
    if result.get("run") != run or result.get("run_id") != want["run_id"]:
        raise SystemExit(f"the output is run {result.get('run')} "
                         f"{result.get('run_id')}, not the prepared {run} "
                         f"{want['run_id']}")
    copied = [k for k in ("commit", "date", "followup", "decisions",
                          "accept_open")
              if result.get(k) != want.get(k)]
    if (result.get("stock_exceptions") or []) != \
            (want.get("stock_exceptions") or []):
        copied.append("stock_exceptions")
    if copied:
        raise SystemExit("the output's " + ", ".join(copied)
                         + " differ from the prepared arguments")
    result_shape(result, want)
    with open(args.output, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    dirty_runs = git("-C", results, "status", "--porcelain",
                     "--untracked-files=all", "--", *RUNS_DIRS)
    if dirty_runs:
        why = ("recorded runs have uncommitted changes; the selection is "
               "built from committed records only:\n" + dirty_runs)
        # After T6 those changes are P7's: set aside with its pages.
        if args.task == "T6":
            why += set_aside(results, changed(results), result)
        raise SystemExit(why)
    for name, task in runs(results, stopped=True):
        if task.get("run_id") == want["run_id"] or \
                task.get("output_sha256") == digest:
            raise SystemExit(f"this output is recorded already, as {name}")
    # The schemas the run was prepared with are its contract.
    schemas = want["schemas"]
    errors = []
    for r in result.get("returns", []):
        if r.get("role") not in schemas:
            errors.append(f"{r.get('label')}: no schema for role "
                          f"{r.get('role')!r}")
            continue
        errors += [f"{r['label']}: {e}"
                   for e in validate(schemas[r["role"]], r["data"])]
    if errors:
        raise SystemExit("returns that do not match their schemas:\n"
                         + "\n".join(f"FAIL {e}" for e in errors))
    if (result.get("summary") or {}).get("stopped"):
        k = 1
        while os.path.exists(os.path.join(results, RUNS_DIR,
                                          f"{run}-stopped-{k}")):
            k += 1
        run = f"{run}-stopped-{k}"
    target = os.path.join(results, RUNS_DIR, run)
    if os.path.exists(target):
        raise SystemExit(f"{target} exists; a run is recorded once")
    tmp = target + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    for i, r in enumerate(result.get("returns", []), 1):
        name = f"{i:03d}-{r['label']}{'-restart' if r['attempt'] else ''}"
        with open(os.path.join(tmp, f"{name}.json"), "w") as f:
            json.dump(r, f, indent=1, ensure_ascii=False)
    meta = {k: v for k, v in result.items() if k != "returns"}
    meta["sequence"] = 1 + len(runs(results, stopped=True))
    meta["output_sha256"] = digest
    meta["research_round"] = ROUND
    with open(os.path.join(tmp, "task.json"), "w") as f:
        json.dump(meta, f, indent=1, ensure_ascii=False)
    os.rename(tmp, target)
    message = [f"Record {run} of round {ROUND}, {result['date']}",
               f"Read {BRANCH} at {result['commit']}; {result['started']} "
               f"agents started, {len(result['followUps'])} items for "
               "follow-up."]
    paths = [target]
    stopped = bool((result.get("summary") or {}).get("stopped"))
    if args.task == "T6" and stopped:
        # Pages a stopped T6 wrote, deleted or changed are not kept for the
        # next attempt: tracked paths return to HEAD, new ones are removed.
        for o in T6_FILES + T6_DIRS:
            if git("-C", results, "cat-file", "-e", f"HEAD:{o.rstrip('/')}",
                   check=False).returncode == 0:
                git("-C", results, "checkout", "-q", "HEAD", "--", o)
            if os.path.exists(os.path.join(results, o)):
                git("-C", results, "clean", "-fdq", "--", o, check=False)
    if args.task == "T6" and not stopped:
        dirty = changed(results)
        rel = os.path.relpath(target, results).replace(os.sep, "/")

        def in_run(d):
            return d == rel or d.startswith(rel + "/")
        rets = result.get("returns", [])
        wrote = {f for r in rets if r["role"] == "P7"
                 for f in r["data"].get("files", [])}
        seen = {f for r in rets if r["role"] == "P7-critic"
                for f in r["data"].get("reviewed", [])}
        by_group = [(g, f) for r in rets if r["role"] == "P7"
                    for g, f in (r["data"].get("group_pages") or {}).items()]
        pages = {f for _, f in by_group}

        def refuse(why):
            shutil.rmtree(target, ignore_errors=True)
            raise SystemExit(why + set_aside(
                results, [d for d in changed(results) if not in_run(d)],
                result))

        outside = sorted(f for f in pages if not GROUP_PAGE.fullmatch(f)
                         or f in T6_REQUIRED)
        if outside:
            refuse("group pages outside hardware/docs/ or among the fixed "
                   "outputs: " + ", ".join(outside))
        # A group page is a new file, not a page the tree holds, unless it
        # is the page of its group the previous round's T6 wrote.
        given = want.get("group_pages") or {}
        held = sorted({f for g, f in by_group if f != given.get(g)
                       and git("-C", results, "cat-file", "-e", f"HEAD:{f}",
                               check=False).returncode == 0})
        if held:
            refuse("group pages that are not new files: " + ", ".join(held))
        # Every output of the plan and each group page is a file after T6;
        # a deletion is not an output.
        gone = sorted(f for f in set(T6_REQUIRED) | pages
                      if not os.path.isfile(os.path.join(results, f)))
        if gone:
            refuse("T6 left these outputs missing: " + ", ".join(gone))
        # Of Research.md P7 writes the status line, the lead above its first
        # section: every section below, the owner's tables among them, stays
        # as it is.
        plan = PLAN_REL.replace(os.sep, "/")
        at_head = git("-C", results, "show", f"HEAD:{plan}", check=False)
        if at_head.returncode == 0 and plan_body(at_head.stdout) != \
                plan_body(read(os.path.join(results, plan))):
            refuse(f"P7 changed {plan} below its status line")
        allowed = set(T6_REQUIRED) | pages
        stray = [d for d in dirty if not in_run(d)
                 and not (d in allowed and d in wrote and d in seen)]
        unchanged = sorted(wrote - set(dirty))
        if unchanged:
            refuse("P7 lists files it did not change: " + ", ".join(unchanged))
        if stray:
            refuse("T6 changed files that are not outputs P7 declared and its "
                   "critic reviewed: " + ", ".join(stray))
        # A figure the critic checked comes from a committed file.
        absent = sorted(f for f in cited_returns(rets)
                        if git("-C", results, "cat-file", "-e", f"HEAD:{f}",
                               check=False).returncode)
        if absent:
            refuse("the critic checked figures against files not committed: "
                   + ", ".join(absent))
        # Each check names the line of its page that states its figure.
        misplaced = misplaced_figures(results, rets)
        if misplaced:
            refuse("figure checks whose line does not state the figure: "
                   + "; ".join(misplaced))
        paths += [os.path.join(results, d) for d in dirty
                  if not in_run(d)]
    if (result.get("summary") or {}).get("selection") and not stopped:
        sel = os.path.join(results, RUNS_DIR, "selection.json")
        with open(sel, "w") as f:
            json.dump(effective_selection(results, pending=(run, meta)), f,
                      indent=1,
                      ensure_ascii=False)
        paths.append(sel)
    sel_rel = os.path.join(RUNS_DIR, "selection.json")
    sel_tracked = git("-C", results, "cat-file", "-e", f"HEAD:{sel_rel}",
                      check=False).returncode == 0
    try:
        git("-C", results, "add", "--", *paths)
        git("-C", results, "commit", "-q", "-m", message[0], "-m",
            message[1], "--", *paths)
    except SystemExit:
        git("-C", results, "reset", "-q", "--", *paths, check=False)
        # A selection.json this record created goes; one in HEAD returns.
        if sel_tracked:
            git("-C", results, "checkout", "-q", "--", sel_rel, check=False)
        elif os.path.exists(os.path.join(results, sel_rel)):
            os.remove(os.path.join(results, sel_rel))
        shutil.rmtree(target, ignore_errors=True)
        raise
    print(f"{target}: {len(result.get('returns', []))} returns committed "
          f"on {RESULTS}; push it")
    return 0


# ----------------------------------------------------------------- raised

def cmd_raised(args):
    not_on_research_branch()
    base = os.path.abspath(os.path.expanduser(args.base))
    results = os.path.join(base, "results")
    shown = git("-C", results, "show",
                f"HEAD:{RUNS_DIR}/{args.run}/task.json", check=False)
    if shown.returncode:
        raise SystemExit(f"{args.run} is not a committed run")
    if changed_under(results, os.path.join(RUNS_DIR, args.run)):
        raise SystemExit(f"{args.run} has uncommitted changes")
    task = json.loads(shown.stdout)
    summary = task.get("summary") or {}
    questions = summary.get("questions", [])
    if not questions:
        stopped = " (the task stopped)" if summary.get("stopped") else ""
        raise SystemExit(f"{args.run} raised no questions{stopped}")
    on_branch(results, RESULTS)
    plan = worktree(base, "plan", BRANCH, detach=False)
    if changed(plan):
        raise SystemExit(f"{plan} has uncommitted changes")
    if not args.no_fetch:
        git("-C", plan, "pull", "-q", "--ff-only", "origin", BRANCH)
    page = os.path.join(plan, PLAN_REL)
    text = read(page)
    have = {r[0] for r in raised_rows(text)}
    clash = [q["id"] for q in questions if q["id"] in have]
    if clash:
        raise SystemExit("already under Raised by P1: " + ", ".join(clash))

    def cell(s):
        return " ".join(s.replace("|", "\\|").split())

    def row(q):
        return (f"| {q['id']} | {q['category']}, {cell(q['function'])} | "
                f"{cell(q['question'])}{feeds_suffix(q)} | |")

    lines = text.split("\n")
    start = lines.index("#### Raised by P1")
    head = next(i for i in range(start, len(lines))
                if lines[i].startswith("| ID | Category and function"))
    end = head + 2
    while end < len(lines) and lines[end].startswith("| V"):
        end += 1
    lines[end:end] = [row(q) for q in questions]
    with open(page, "w") as f:
        f.write("\n".join(lines))
    git("-C", plan, "add", "--", page)
    git("-C", plan, "commit", "-q", "-m",
        f"Raise the questions of {args.run}",
        "-m", f"{len(questions)} questions P1 raised and its critics or the "
        "re-check confirmed.", "--", page)
    print(f"{page}: {len(questions)} questions committed on {BRANCH}; "
          "push it")
    return 0


ARGS_LINE = "const A = args || {}"


def cmd_script(args):
    """DIR/roundN-RUN.js, N the round: round1.js with the prepared arguments
    of RUN in place of the Workflow tool's `args`, for the Workflow tool's
    scriptPath.
    The arguments of a run grow past 100 KB once P1 has raised questions;
    embedding them leaves nothing to copy by hand. Only the line
    `const A = args || {}` changes, and the result is read back."""
    m = re.fullmatch(r"FU-(.+)", args.run)
    if not (args.run in TASKS and args.run != "FU" or m and NAME.fullmatch(
            m.group(1)) and "stopped" not in m.group(1)):
        raise SystemExit(f"{args.run!r} is not a run: T1 to T6, or FU-NAME "
                         "with NAME of letters and digits")
    base = os.path.abspath(os.path.expanduser(args.base))
    prepared = os.path.join(base, f"args-{args.run}.json")
    if not os.path.isfile(prepared):
        raise SystemExit(f"{prepared} is not there; run prepare first")
    a = json.loads(read(prepared))
    if a.get("research_round", 1) != ROUND:
        raise SystemExit(f"{prepared} is prepared for round "
                         f"{a.get('research_round', 1)}, not round {ROUND}")
    src = read(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "round1.js"))
    lines = src.split("\n")
    if lines.count(ARGS_LINE) != 1:
        raise SystemExit(f"round1.js holds `{ARGS_LINE}` "
                         f"{lines.count(ARGS_LINE)} times, not once")
    i = lines.index(ARGS_LINE)
    embedded = "const A = " + json.dumps(a, ensure_ascii=True,
                                         separators=(",", ":"))
    out = lines[:i] + [f"// The arguments of {args.run}, run "
                       f"{a.get('run_id')}, from {prepared}.",
                       embedded] + lines[i + 1:]
    # Written beside the target, read back from the file, then renamed.
    out_dir = os.path.abspath(os.path.expanduser(args.out)) if args.out \
        else base
    if not os.path.isdir(out_dir):
        raise SystemExit(f"{out_dir} is not a directory")
    target = os.path.join(out_dir, f"round{ROUND}-{args.run}.js")
    tmp = target + ".tmp"
    with open(tmp, "w") as f:
        f.write("\n".join(out))
    back = read(tmp).split("\n")
    try:
        same = back[:i] == lines[:i] and back[i + 2:] == lines[i + 1:] \
            and json.loads(back[i + 1][len("const A = "):]) == a
    except (IndexError, ValueError):
        same = False
    if not same:
        os.remove(tmp)
        raise SystemExit("the script does not read back as round1.js with "
                         "the prepared arguments")
    os.replace(tmp, target)
    print(f"{target}: {args.run}, run {a.get('run_id')}; Workflow tool "
          "scriptPath, no args")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check").set_defaults(fn=cmd_check)
    # The research round of a run: its branches and its records directory.
    rnd = argparse.ArgumentParser(add_help=False)
    rnd.add_argument("--round", type=int, choices=ROUNDS, default=1,
                     help="the research round (default: 1)")
    p = sub.add_parser("prepare", parents=[rnd])
    p.add_argument("task", choices=TASKS)
    p.add_argument("--base", required=True)
    p.add_argument("--digikey-env", required=True)
    p.add_argument("--model", required=True)
    p.add_argument("--effort", required=True, choices=EFFORTS,
                   help="the effort every agent of the run gets")
    p.add_argument("--agent-model", choices=MODELS,
                   help="the model of every role but P2, the adjudicator, "
                   "P7 and the P7 critic (default: the session's model)")
    p.add_argument("--oversight-model", choices=MODELS,
                   help="the model of P2, the adjudicator, P7 and the P7 "
                   "critic (default: the session's model)")
    p.add_argument("--followup")
    p.add_argument("--name", default="1", help="follow-up run name")
    p.add_argument("--digikey-min", type=int, default=600, metavar="N",
                   help="refuse a run that reads stock while fewer than N of "
                   "the day's Digi-Key calls are left (0: no check)")
    p.add_argument("--db")
    p.add_argument("--no-fetch", action="store_true")
    p.add_argument("--stock-exception", action="append", default=[],
                   metavar="PART=REASON",
                   help="T6: a part whose stock gate failure the owner "
                   "accepts; the stock check may fail on these alone, and "
                   "the pages state each (repeatable)")
    p.add_argument("--accept-open", metavar="REASON",
                   help="start the run although earlier runs left items "
                   "open (tools/research/README.md lists them); the "
                   "reason and the items go into the arguments")
    p.set_defaults(fn=cmd_prepare)
    r = sub.add_parser("record", parents=[rnd])
    r.add_argument("task", choices=TASKS)
    r.add_argument("output")
    r.add_argument("--base", required=True)
    r.add_argument("--name", default="1", help="follow-up run name")
    r.set_defaults(fn=cmd_record)
    q = sub.add_parser("raised", parents=[rnd])
    q.add_argument("--base", required=True)
    q.add_argument("--run", default="T1")
    q.add_argument("--no-fetch", action="store_true")
    q.set_defaults(fn=cmd_raised)
    w = sub.add_parser("script", parents=[rnd])
    w.add_argument("run", help="T1 to T6, or FU-N for a follow-up")
    w.add_argument("--base", required=True)
    w.add_argument("--out", help="directory to write roundN-RUN.js to, "
                   "one that the Workflow tool reads (default: the base)")
    w.set_defaults(fn=cmd_script)
    args = ap.parse_args()
    use_round(getattr(args, "round", 1))
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
