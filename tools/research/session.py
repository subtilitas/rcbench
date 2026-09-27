#!/usr/bin/env python3
"""The research session's side of round 1 (hardware/docs/Research.md).

A workflow script has no file or git access, so the session runs this tool
around each task:

    session.py check
    session.py prepare TASK --base DIR --digikey-env FILE --model M --effort E
                       [--followup FILE] [--db FILE] [--no-fetch]
    session.py record TASK OUTPUT --base DIR [--name N]
    session.py raised --base DIR [--run T1|FU-N]

`check` holds the files in tools/research/ to the plan: the agent counts of
the layout, the categories, the schemas, the host table, and the dry run of
round1.js when node is installed. It exits 1 on the first disagreement.

`prepare` fetches origin, checks the task's turn and its answered questions,
makes the read-only checkout of `research/round1` at the commit the task
reads, the monostable pages at 23c82ca and the working tree of
`research/round1-results` under DIR, and writes DIR/args-TASK.json, the
Workflow tool's `args` for tools/research/round1.js. Before T6 it merges
`research/round1` into the results tree.

`record` takes the task output file the Workflow tool wrote, checks each
agent's return against its schema, writes one JSON file per return under
hardware/research/round1/RUN/ in the results tree, and commits only that
directory (after T6, the pages P7 wrote as well).

`raised` appends the questions a P1 run confirmed under "Raised by P1" in a
working tree of `research/round1` and commits them. Nothing is pushed.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PLAN_REL = os.path.join("hardware", "docs", "Research.md")
SPEC_REL = os.path.join("hardware", "docs", "IOBoard.md")
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
# The files T6 may write: the Outputs table of the plan.
T6_DIRS = ("hardware/docs/",)
T6_FILES = ("hardware/STATUS.md", "hardware/README.md", "tools/jlc_stock.py")


# The outputs T6 writes, from the Outputs table of the plan.
T6_REQUIRED = ["hardware/docs/IOBoard.md", "hardware/docs/Parts.md",
               "hardware/docs/Power.md", "hardware/docs/Research.md",
               "hardware/STATUS.md", "hardware/README.md",
               "tools/jlc_stock.py"]


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
    required, items, enum."""
    kind = schema.get("type")
    ok = {"object": dict, "array": list, "string": str, "boolean": bool}
    if kind == "integer":
        if not isinstance(value, int) or isinstance(value, bool):
            return [f"{where}: not an integer"]
    elif kind in ok and not isinstance(value, ok[kind]):
        return [f"{where}: not a {kind}"]
    if "enum" in schema and value not in schema["enum"]:
        return [f"{where}: {value!r} not in {schema['enum']}"]
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


def spec_text(commit):
    """The specification a run read: Research.md without its Raised by P1
    and Decided tables, which have their own checks, and IOBoard.md. None
    when the commit is not in this clone."""
    texts = []
    for rel in (PLAN_REL, SPEC_REL):
        shown = git("show", f"{commit}:{rel}", check=False)
        if shown.returncode:
            return None
        texts.append(shown.stdout)
    plan = re.sub(r"#### Raised by P1\n.*?(?=\n#{2,4} )", "", texts[0],
                  flags=re.S)
    plan = re.sub(r"#### Decided on the research's output\n.*?"
                  r"(?=\n#{2,4} |\Z)", "", plan, flags=re.S)
    return plan + "\0" + texts[1]


def decisions(text):
    """Q4, Q8, Q9 and the owner's entry in the Decision column."""
    sec = text.split("#### Decided on the research's output", 1)[-1]
    out = {}
    for line in sec.splitlines():
        c = cells(line)
        if c and len(c) == 4 and re.fullmatch(r"Q\d", c[0]):
            out[c[0]] = c[3]
    return out


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
    raised_rows(text)
    if set(decisions(text)) != {"Q4", "Q8", "Q9"}:
        fails.append("the decision table does not list Q4, Q8 and Q9")
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


def recorded(results, run):
    probe = git("-C", results, "cat-file", "-e",
                f"HEAD:{RUNS_DIR}/{run}/task.json", check=False)
    return probe.returncode == 0


def runs(results, stopped=False, pending=None):
    """The committed runs in the order they were recorded, stopped ones
    left out unless asked for: (name, task.json). They are read from HEAD,
    so an uncommitted edit counts for nothing; `pending` adds the run being
    recorded."""
    out = []
    listed = git("-C", results, "ls-tree", "--name-only", "HEAD",
                 RUNS_DIR + "/", check=False).stdout.split()
    for path in listed:
        name = os.path.basename(path)
        if "-stopped-" in name and not stopped:
            continue
        shown = git("-C", results, "show", f"HEAD:{path}/task.json",
                    check=False)
        if shown.returncode == 0:
            out.append((name, json.loads(shown.stdout)))
    if pending:
        out.append(pending)
    return sorted(out, key=lambda r: r[1].get("sequence", 0))


def effective_selection(results, pending=None):
    """The part each function keeps: the latest run that names a function
    decides it. A run that names it and verifies no part leaves it open,
    and records the earlier part it did not requalify."""
    eff = {}
    for name, task in runs(results, pending=pending):
        sel = (task.get("summary") or {}).get("selection") or {}
        for cat, entries in sel.items():
            for e in entries:
                slot = eff.setdefault(cat, {})
                held = slot.get(e["function"]) or {}
                entry = {
                    "part": e.get("part"), "rank": e.get("rank"),
                    "run": name,
                    "alternate_unverified":
                        e.get("alternate_unverified") or [],
                    "second_source_missing":
                        bool(e.get("second_source_missing")),
                    "q_alternatives": e.get("q_alternatives") or []}
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


def p1_unresolved(results, categories):
    """Each category whose latest P1 run left it unchecked: its P1, critic or
    re-check returned nothing, or P0 held it."""
    p1 = [(n, t) for n, t in runs(results) if t.get("task") == "T1" or
          (t.get("followup") or {}).get("phases") == "P1"]
    out = []
    for c in categories:
        # T1 covers every category; a P1 follow-up only its own.
        cover = [(n, t) for n, t in p1 if t.get("task") == "T1" or
                 c in ((t.get("followup") or {}).get("categories") or [])]
        if not cover:
            out.append(f"{c}: no P1 run covers it")
            continue
        name, last = cover[-1]
        left = [f for f in last.get("followUps", [])
                if f.get("category") == c and not f.get("notice") and
                str(f.get("role", "")).startswith(("P1", "category"))]
        if left:
            out.append(f"{c}: {name} left {len(left)} P1 items unresolved")
    return out


def t6_open(results):
    """What stands between the last P5/P6 check and T6: a part changed
    after it, or conflicts and gaps it listed with no round 2 run."""
    done = runs(results)
    checks = [(n, t) for n, t in done if t.get("task") == "T5" or
              (t.get("followup") or {}).get("phases") == "P5-P6"]
    if not checks:
        return ["no P5/P6 check is recorded"]
    name, last = checks[-1]
    out = [f"{n} changed a selection after {name}" for n, t in done
           if t.get("sequence", 0) > last.get("sequence", 0)
           and (t.get("summary") or {}).get("selection")]
    # An item may stand only when a round-2 research follow-up recorded
    # between the check before and this one covered its categories: the
    # plan writes what round 2 leaves as not known.
    # The first check's items were found by it, so no earlier run covered
    # them.
    before = checks[-2][1].get("sequence", 0) if len(checks) > 1 else None
    # Round 2 covers the categories a round-2 P2-P4 follow-up researched to
    # a selection, not the ones its file named.
    covered = set() if before is None else {c for _, t in done
               if (t.get("followup") or {}).get("round") == 2
               and (t.get("followup") or {}).get("phases") == "P2-P4"
               and before < t.get("sequence", 0) < last.get("sequence", 0)
               for c in (t.get("summary") or {}).get("selection") or {}}
    summary = last.get("summary") or {}
    # A conflict belongs to the categories it names and those of its parts.
    part_cat = {}
    for c, fns in effective_selection(results).items():
        for e in fns.values():
            for part in [e.get("part")] + [q.get("part") for q in
                                           e.get("q_alternatives") or []]:
                if part:
                    part_cat[part] = c
    items = [set(x.get("categories") or []) |
             {part_cat[p] for p in x.get("parts") or [] if p in part_cat}
             for x in summary.get("conflicts", [])] + \
        [{x["category"]} if x.get("category") else set()
         for x in summary.get("gaps", [])]
    uncovered = [i for i in items if not i or not i <= covered]
    if uncovered:
        out.append(f"{name} lists {len(uncovered)} conflicts and gaps that "
                   "no round 2 follow-up since the check before covered")
    if summary.get("missing_checks"):
        out.append(f"{name} ran without "
                   + ", ".join(summary["missing_checks"]))
    if summary.get("unchecked_items"):
        out.append(f"{name} left {summary['unchecked_items']} combinations "
                   "and budgets its critic did not rule on")
    if summary.get("rejected_items"):
        out.append(f"{name} has {summary['rejected_items']} combinations "
                   "and budgets its critic rejected")
    return out


def answers(rows, c):
    return {r[0]: r[3].strip() for r in rows if r[1] == c}


def deciding_runs(results, c, eff):
    """The runs whose selection decides at least one of c's functions, in
    record order: (name, task)."""
    names = {e["run"] for e in eff.get(c, {}).values()}
    return [(n, t) for n, t in runs(results) if n in names]


def stale_selections(results, categories, rows, now, upstream=()):
    """Each run whose parts for a category were not qualified against what
    is in force now: a P1 run raised questions for the category after it;
    the category's answers under "Raised by P1", or the specification,
    differ from those at the plan commit it read; or, for a category that
    takes the parts of T2 and T4 (`upstream`), one of those changed after
    it."""
    done = runs(results)
    seq = {n: t.get("sequence", 0) for n, t in done}
    eff = effective_selection(results)
    rows_at, spec_at = {}, {}

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

    changed_up = max([seq.get(e["run"], 0) for u in upstream
                      for e in eff.get(u, {}).values()] or [0])
    out = []
    for c in categories:
        p1 = [n for n, t in done if (t.get("task") == "T1" or (
            t.get("followup") or {}).get("phases") == "P1") and any(
            q.get("category") == c
            for q in (t.get("summary") or {}).get("questions", []))]
        for name, task in deciding_runs(results, c, eff):
            commit = task.get("commit", "")
            if p1 and seq[p1[-1]] > seq[name]:
                out.append(f"{c}: {p1[-1]} raised questions after {name} "
                           "selected its parts")
            if rows_of(commit) is None or spec_of(commit) is None:
                out.append(f"{c}: the plan {name} read, at {commit}, is not "
                           "in this clone")
                continue
            if answers(rows_of(commit), c) != answers(rows, c):
                out.append(f"{c}: the answers under Raised by P1 changed "
                           f"after {name} selected its parts")
            if spec_of(commit) != spec_of(now):
                out.append(f"{c}: the specification changed after {name} "
                           "selected its parts")
            if c not in upstream and changed_up > seq[name]:
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
        if c in ("R10", "R12") and eff.get(c) and not any(
                e.get("q_alternatives") for e in eff[c].values()):
            out.append(f"{c}: no function has a Q4 or Q8 alternative")
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


def check_followup(followup, cats):
    """The follow-up file: its phases, round, categories and items."""
    if followup.get("phases") not in ("P1", "P2-P4", "P5-P6"):
        raise SystemExit("the follow-up's phases is P1, P2-P4 or P5-P6")
    if followup.get("round") not in (1, 2):
        raise SystemExit("follow-up tasks run in rounds 1 and 2 only")
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
    # Only a question that names Q4, Q8 or Q9 feeds a decision alone.
    exempt = {q["id"] for _, q in committed
              if q.get("blocks") == "decision-only"
              and q.get("decision") in ("Q4", "Q8", "Q9")}
    open_q = [r[0] for r in rows if r[1] in categories and not r[3].strip()
              and r[0] not in exempt]
    if open_q:
        raise SystemExit("unanswered under Raised by P1: " + ", ".join(open_q))


def pending_p1(base, results, run):
    """Another P1 run prepared and not recorded, whose question IDs this
    run would repeat."""
    out = []
    for name in os.listdir(base):
        m = re.fullmatch(r"args-(.+)\.json", name)
        if not m or m.group(1) == run:
            continue
        a = json.loads(read(os.path.join(base, name)))
        is_p1 = a.get("task") == "T1" or \
            (a.get("followup") or {}).get("phases") == "P1"
        if is_p1 and not recorded(results, m.group(1)):
            out.append(m.group(1))
    return out


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
    for path in (db, manifest, env):
        if not os.path.isfile(path):
            raise SystemExit(f"{path} is not there")
    if not args.no_fetch:
        git("fetch", "-q", "origin")
    commit = git("rev-parse", f"origin/{BRANCH}")
    text = git("show", f"{commit}:{PLAN_REL}")
    run = f"FU-{args.name}" if args.task == "FU" else args.task

    # The results tree: on its branch, its recorded runs committed.
    os.makedirs(base, exist_ok=True)
    results = worktree(base, "results", RESULTS, detach=False)
    if changed_under(results, RUNS_DIR):
        raise SystemExit("recorded runs have uncommitted changes")
    if recorded(results, run):
        raise SystemExit(f"{run} is recorded already")

    # The task's turn.
    phases = (followup or {}).get("phases")
    owner = {c: t for t, v in cats["tasks"].items() for c in v["categories"]}
    after = list(AFTER.get(args.task, []))
    if phases == "P2-P4":
        after += sorted({owner[c] for c in followup["categories"]})
    elif phases == "P1":
        after += ["T1"]
    elif phases == "P5-P6":
        after += ["T5"]
    for task in after:
        if not recorded(results, task):
            raise SystemExit(f"{run} runs after {task}, which is not "
                             "recorded")
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
    reads_stock = args.task in cats["tasks"] or phases == "P2-P4"
    open_sel = []
    blocking_gate(text, args.task, task_cats, is_p1, reads_stock)
    if phases and followup.get("round") == 2 and not any(
            (t.get("followup") or {}).get("round") == 1
            for _, t in runs(results)):
        raise SystemExit("a round 2 follow-up runs after a round 1 "
                         "follow-up is recorded")
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
    open_sel += stale_selections(results, gate_cats, rows, commit, up)
    open_sel += [o for o in p1_unresolved(results, gate_cats)
                 if o not in open_sel]
    last_p56 = ""
    if args.task == "T6":
        questions_gate(results, rows, list(cats["categories"]))
        open_sel += t6_open(results)
        checks = [n for n, t in runs(results) if t.get("task") == "T5" or
                  (t.get("followup") or {}).get("phases") == "P5-P6"]
        last_p56 = checks[-1] if checks else ""
        dirty = [d for d in changed(results) if t6_output(d)]
        if dirty:
            raise SystemExit("the output paths have changes; commit or "
                             "remove them before T6: " + ", ".join(dirty))
        missing = [q for q, d in decisions(text).items() if not d]
        if missing:
            raise SystemExit("the owner has not decided " + ", ".join(missing))
        open_sel += q9_open(results, decisions(text))
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
            raise SystemExit("merging research/round1 into the results "
                             "tree failed: " + merged.stderr.strip())
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
    out = {
        "task": args.task, "mode": "run", "date": today, "commit": commit,
        "cap": cats["cap"], "categories": cats["categories"],
        "tasks": cats["tasks"], "schemas": resolved_schemas(),
        "hosts": load("hosts.json")["hosts"],
        "jlcparts": {"sha256": info["sha256"], "rows": info["rows"],
                     "manifest": manifest,
                     "manifest_created": info["manifest_created"],
                     "lcsc": plan_lcsc(text)},
        "paths": {"checkout": checkout, "monostable": mono, "db": db,
                  "results": results,
                  "scratch": os.path.join(base, "scratch"),
                  "digikey_env": env},
        "followup": followup, "first_v": first_v,
        "decisions": decisions(text),
        "run": run, "run_id": run_id,
        "results_head": git("-C", results, "rev-parse", "HEAD"),
        "for_research": for_research,
        "required_reports": cats.get("reports", {}),
        "last_p56": last_p56,
        "t6_outputs": T6_REQUIRED if args.task == "T6" else [],
        "run_info": run_info(args.model, args.effort),
        "accept_open": {"reason": args.accept_open, "functions": open_sel}
        if args.accept_open else None,
    }
    path = os.path.join(base, f"args-{run}.json")
    with open(path, "w") as f:
        json.dump(out, f, indent=1)
    print(f"{path}: task {args.task}, {BRANCH} at {commit}, {today}, "
          f"questions from V{first_v}")
    return 0


# ----------------------------------------------------------------- record

def cmd_record(args):
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
    on_branch(results, RESULTS)
    head = git("-C", results, "rev-parse", "HEAD")
    if want.get("results_head") and head != want["results_head"]:
        raise SystemExit(f"the results tree moved from "
                         f"{want['results_head']} to {head} since prepare")
    if result.get("run") != run or result.get("run_id") != want["run_id"]:
        raise SystemExit(f"the output is run {result.get('run')} "
                         f"{result.get('run_id')}, not the prepared {run} "
                         f"{want['run_id']}")
    with open(args.output, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    dirty_runs = git("-C", results, "status", "--porcelain",
                     "--untracked-files=all", "--", RUNS_DIR)
    if dirty_runs:
        raise SystemExit("recorded runs have uncommitted changes; the "
                         "selection is built from committed records only:\n"
                         + dirty_runs)
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
    for e in errors:
        print(f"FAIL {e}")
    if errors:
        return 1
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
    with open(os.path.join(tmp, "task.json"), "w") as f:
        json.dump(meta, f, indent=1, ensure_ascii=False)
    os.rename(tmp, target)
    message = [f"Record {run} of round 1, {result['date']}",
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
        rel = os.path.relpath(target, results)
        rets = result.get("returns", [])
        wrote = {f for r in rets if r["role"] == "P7"
                 for f in r["data"].get("files", [])}
        seen = {f for r in rets if r["role"] == "P7-critic"
                for f in r["data"].get("reviewed", [])}
        pages = {f for r in rets if r["role"] == "P7"
                 for f in (r["data"].get("group_pages") or {}).values()}
        outside = sorted(f for f in pages if not GROUP_PAGE.fullmatch(f)
                         or f in T6_REQUIRED)
        if outside:
            shutil.rmtree(target, ignore_errors=True)
            raise SystemExit("group pages outside hardware/docs/ or among "
                             "the fixed outputs: " + ", ".join(outside))
        allowed = set(T6_REQUIRED) | pages
        stray = [d for d in dirty if not d.startswith(rel)
                 and not (d in allowed and d in wrote and d in seen)]
        unchanged = sorted(wrote - set(dirty))
        if unchanged:
            shutil.rmtree(target, ignore_errors=True)
            raise SystemExit("P7 lists files it did not change: "
                             + ", ".join(unchanged))
        if stray:
            shutil.rmtree(target, ignore_errors=True)
            raise SystemExit("T6 changed files that are not outputs P7 "
                             "declared and its critic reviewed: "
                             + ", ".join(stray))
        paths += [os.path.join(results, d) for d in dirty
                  if not d.startswith(rel)]
    if (result.get("summary") or {}).get("selection") and not stopped:
        sel = os.path.join(results, RUNS_DIR, "selection.json")
        with open(sel, "w") as f:
            json.dump(effective_selection(results, pending=(run, meta)), f,
                      indent=1,
                      ensure_ascii=False)
        paths.append(sel)
    try:
        git("-C", results, "add", "--", *paths)
        git("-C", results, "commit", "-q", "-m", message[0], "-m",
            message[1], "--", *paths)
    except SystemExit:
        git("-C", results, "reset", "-q", "--", *paths, check=False)
        git("-C", results, "checkout", "-q", "--",
            os.path.join(RUNS_DIR, "selection.json"), check=False)
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
        return s.replace("|", "\\|").replace("\n", " ")

    def row(q):
        feeds = ""
        if q["blocks"] == "decision-only":
            feeds = f" (feeds {q['decision']} only)"
        return (f"| {q['id']} | {q['category']}, {cell(q['function'])} | "
                f"{cell(q['question'])}{feeds} | |")

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


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check").set_defaults(fn=cmd_check)
    p = sub.add_parser("prepare")
    p.add_argument("task", choices=TASKS)
    p.add_argument("--base", required=True)
    p.add_argument("--digikey-env", required=True)
    p.add_argument("--model", required=True)
    p.add_argument("--effort", required=True)
    p.add_argument("--followup")
    p.add_argument("--name", default="1", help="follow-up run name")
    p.add_argument("--db")
    p.add_argument("--no-fetch", action="store_true")
    p.add_argument("--accept-open", metavar="REASON",
                   help="start the run although earlier runs left items "
                   "open (tools/research/README.md lists them); the "
                   "reason and the items go into the arguments")
    p.set_defaults(fn=cmd_prepare)
    r = sub.add_parser("record")
    r.add_argument("task", choices=TASKS)
    r.add_argument("output")
    r.add_argument("--base", required=True)
    r.add_argument("--name", default="1", help="follow-up run name")
    r.set_defaults(fn=cmd_record)
    q = sub.add_parser("raised")
    q.add_argument("--base", required=True)
    q.add_argument("--run", default="T1")
    q.add_argument("--no-fetch", action="store_true")
    q.set_defaults(fn=cmd_raised)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
