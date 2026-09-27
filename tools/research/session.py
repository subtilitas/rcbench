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
MONOSTABLE = "23c82ca6ca976d956098cfafd25dbefa11584f5d"
BRANCH = "research/round1"
RESULTS = "research/round1-results"
TASKS = ["T1", "T2", "T3", "T4", "T5", "T6", "FU"]
AFTER = {"T2": ["T1"], "T4": ["T1"], "T3": ["T2", "T4"], "T5": ["T3"],
         "T6": ["T5"]}
RUNS_DIR = os.path.join("hardware", "research", "round1")
# The files T6 may write: the Outputs table of the plan.
T6_OUTPUTS = ("hardware/docs/", "hardware/STATUS.md", "hardware/README.md",
              "tools/jlc_stock.py")


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


def runs(results, stopped=False):
    """The recorded runs in the order they were recorded, stopped ones
    left out unless asked for: (name, task.json)."""
    top = os.path.join(results, RUNS_DIR)
    out = []
    for name in sorted(os.listdir(top)) if os.path.isdir(top) else []:
        path = os.path.join(top, name, "task.json")
        if ("-stopped-" in name and not stopped) or not os.path.isfile(path):
            continue
        out.append((name, json.loads(read(path))))
    return sorted(out, key=lambda r: r[1].get("sequence", 0))


def effective_selection(results):
    """The part each function keeps: a later run's part overrides an
    earlier one, and a run that selected none leaves an earlier part."""
    eff = {}
    for name, task in runs(results):
        sel = (task.get("summary") or {}).get("selection") or {}
        for cat, entries in sel.items():
            for e in entries:
                slot = eff.setdefault(cat, {})
                held = slot.get(e["function"])
                if e.get("part") or held is None:
                    slot[e["function"]] = {"part": e.get("part"),
                                           "rank": e.get("rank"), "run": name}
                elif held["part"] in (e.get("refuted") or []):
                    # This run refuted the part in force and kept none.
                    slot[e["function"]] = {"part": None, "rank": None,
                                           "run": name,
                                           "refuted": held["part"]}
    return eff


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


def cmd_prepare(args):
    not_on_research_branch()
    base = os.path.abspath(os.path.expanduser(args.base))
    cats = load("categories.json")
    info = load("jlcparts.json")
    db = os.path.abspath(os.path.expanduser(args.db or info["path"]))
    manifest = os.path.join(os.path.dirname(db), "manifest.json")
    env = os.path.abspath(os.path.expanduser(args.digikey_env))
    followup = None
    if args.task == "FU":
        if not args.followup:
            raise SystemExit("FU needs --followup FILE")
        followup = json.loads(read(args.followup))
        if followup.get("phases") not in ("P1", "P2-P4", "P5-P6"):
            raise SystemExit("the follow-up's phases is P1, P2-P4 or P5-P6")
        if int(followup.get("round", 0)) not in (1, 2):
            raise SystemExit("follow-up tasks run in rounds 1 and 2 only")
        fc = followup.get("categories")
        if followup["phases"] != "P5-P6" and (
                not isinstance(fc, list) or not fc or len(set(fc)) != len(fc)
                or any(c not in cats["categories"] for c in fc)):
            raise SystemExit("the follow-up's categories are a list of "
                             "distinct IDs from R1 to R13")
        if not isinstance(followup.get("items", []), list):
            raise SystemExit("the follow-up's items are a list")
    for path in (db, manifest, env):
        if not os.path.isfile(path):
            raise SystemExit(f"{path} is not there")
    if not args.no_fetch:
        git("fetch", "-q", "origin")
    commit = git("rev-parse", f"origin/{BRANCH}")
    text = git("show", f"{commit}:{PLAN_REL}")

    os.makedirs(base, exist_ok=True)
    results = worktree(base, "results", RESULTS, detach=False)
    for task in AFTER.get(args.task, []):
        if not recorded(results, task):
            raise SystemExit(f"{args.task} runs after {task}, "
                             "which is not recorded")
    rows = raised_rows(text)
    if followup:
        task_cats = followup.get("categories", [])
    else:
        task_cats = cats["tasks"].get(args.task, {}).get("categories", [])
    reads_stock = args.task in cats["tasks"] or \
        (followup or {}).get("phases") == "P2-P4"
    if reads_stock:
        open_q = [r[0] for r in rows if r[1] in task_cats
                  and not r[3].strip() and "(feeds Q" not in r[2]]
        if open_q:
            raise SystemExit("unanswered under Raised by P1: "
                             + ", ".join(open_q))
    gate = {"T3": ["T2", "T4"], "T5": ["T2", "T3", "T4"]}.get(args.task, [])
    eff = effective_selection(results)
    open_sel = [f"{c}: {fn}" for t in gate
                for c in cats["tasks"][t]["categories"]
                for fn, e in eff.get(c, {}).items() if not e["part"]]
    if open_sel and not args.accept_open:
        raise SystemExit("functions with no verified part: "
                         + "; ".join(open_sel)
                         + ". Resolve them in a follow-up task, or pass "
                         "--accept-open REASON")
    if args.task == "T6":
        missing = [q for q, d in decisions(text).items() if not d]
        if missing:
            raise SystemExit("the owner has not decided " + ", ".join(missing))
        merged = git("-C", results, "merge", "--no-edit", "-m",
                     f"Merge {BRANCH} at {commit[:12]} before T6", commit,
                     check=False)
        if merged.returncode:
            raise SystemExit("merging research/round1 into the results "
                             "tree failed: " + merged.stderr.strip())

    checkout = worktree(base, f"checkout-{commit[:12]}", commit)
    read_only(checkout)
    mono = worktree(base, "monostable-23c82ca", MONOSTABLE)
    read_only(mono)
    now = datetime.datetime.now(datetime.timezone.utc)
    today = now.date().isoformat()
    first_v = 1 + max([int(r[0][1:]) for r in rows] or [0])
    run = f"FU-{args.name}" if args.task == "FU" else args.task
    if recorded(results, run):
        raise SystemExit(f"{run} is recorded already")
    ident = json.dumps([run, commit, now.isoformat(), followup, args.model,
                        args.effort], sort_keys=True)
    run_id = hashlib.sha256(ident.encode()).hexdigest()[:16]
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
        "run": run, "run_id": run_id,
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
    if result.get("run") != run or result.get("run_id") != want["run_id"]:
        raise SystemExit(f"the output is run {result.get('run')} "
                         f"{result.get('run_id')}, not the prepared {run} "
                         f"{want['run_id']}")
    with open(args.output, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    for name, task in runs(results, stopped=True):
        if task.get("run_id") == want["run_id"] or \
                task.get("output_sha256") == digest:
            raise SystemExit(f"this output is recorded already, as {name}")
    schemas = resolved_schemas()
    errors = []
    for r in result.get("returns", []):
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
    top = os.path.join(results, RUNS_DIR)
    meta["sequence"] = 1 + len([n for n in os.listdir(top)
                                if not n.endswith(".tmp")])
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
    if args.task == "T6" and not stopped:
        dirty = [line[3:] for line in git(
            "-C", results, "status", "--porcelain", "--untracked-files=all"
        ).splitlines()]
        rel = os.path.relpath(target, results)
        stray = [d for d in dirty if not d.startswith(rel)
                 and not d.startswith(T6_OUTPUTS)]
        if stray:
            shutil.rmtree(target, ignore_errors=True)
            raise SystemExit("T6 changed files outside its outputs: "
                             + ", ".join(stray))
        paths += [os.path.join(results, d) for d in dirty
                  if not d.startswith(rel)]
    if (result.get("summary") or {}).get("selection") and not stopped:
        sel = os.path.join(results, RUNS_DIR, "selection.json")
        with open(sel, "w") as f:
            json.dump(effective_selection(results), f, indent=1,
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
    task = json.loads(read(os.path.join(results, RUNS_DIR, args.run,
                                        "task.json")))
    summary = task.get("summary") or {}
    questions = summary.get("questions", [])
    if not questions:
        stopped = " (the task stopped)" if summary.get("stopped") else ""
        raise SystemExit(f"{args.run} raised no questions{stopped}")
    plan = worktree(base, "plan", BRANCH, detach=False)
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
    git("-C", plan, "add", page)
    git("-C", plan, "commit", "-q", "-m", f"Raise the questions of {args.run}",
        "-m", f"{len(questions)} questions P1 raised and its critics or the "
        "re-check confirmed.")
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
                   help="start T3 or T5 with functions that have no "
                   "verified part")
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
