"""Build a P2-P4 follow-up file of round 2 from Research.md's Follow-ups table
and FU-2P1's P1 follow-ups.

Usage: build_followup.py [--answers-from N] [--answers V1,V2,...]
                         RESEARCH_MD FU2P1_TASK_JSON OUT CAT [CAT ...]

Items: one per category a table row names, in table order, then FU-2P1's
followUps of role P1 or P1-critic in those categories, in record order.
Without options this reproduces followup-2A.json from research/round2 at
08042b0.

--answers-from N appends to a table item's requirement each answer under
"Raised by P1" from VN on whose row names the item's category and shares
a function with the item (equal names, or one the start of the other),
unless the answer is "for research".
--answers V1,V2 also appends those rows' answers, whatever their number.
The table rows stay as FU-2A read them; the stale-selection gate compares
Research.md outside the Raised by P1 rows, so the answers travel in the
items.
"""
import json
import re
import sys

REASON = ("requirement changed after round 1 or round 1 left the function "
          "without a part (Research.md, Round 2, Follow-ups)")


def table_items(text, cats):
    sec = text.split("## Round 2", 1)[1].split("### Follow-ups", 1)[1] \
        .split("### Runs", 1)[0]
    items = []
    for line in sec.splitlines():
        if not line.startswith("| ") or line.startswith("| Function") \
                or line.startswith("| ---"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
        if len(cells) != 4:
            raise SystemExit(f"row of {len(cells)} cells: {line[:80]}")
        fn, cat, req, src = cells
        for c in [x.strip() for x in cat.split(",")]:
            if c in cats:
                items.append({"category": c, "function": fn,
                              "requirement": req, "source": src,
                              "reason": REASON})
    return items


def raised_answers(text, first, also=()):
    """(V number, category, function, answer) of each row under Raised by
    P1 from V`first` on, and of the rows named in `also`."""
    sec = text.split("#### Raised by P1", 1)[1].split("\n#", 1)[0]
    out = []
    for line in sec.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
        if len(cells) != 4 or not re.fullmatch(r"V\d+", cells[0]):
            continue
        n = int(cells[0][1:])
        cat, _, fn = cells[1].partition(", ")
        if n >= first or cells[0] in also:
            out.append((cells[0], cat.strip(), fn.strip(), cells[3]))
    return out


def names(fn):
    return {re.sub(r"\s*\(new\)$", "", x).strip().lower()
            for x in fn.split(";") if x.strip()}


def related(a, b):
    """Whether two function cells share a function: equal names, or one
    name the start of the other ("servo supply temperature" and "servo
    supply temperature sensor")."""
    return any(x == y or x.startswith(y) or y.startswith(x)
               for x in names(a) for y in names(b))


def add_answers(items, answers):
    for it in items:
        got = [f"{v}: {a}" for v, cat, fn, a in answers
               if cat == it["category"] and related(fn, it["function"])
               and not a.lower().startswith("for research")]
        if got:
            stop = "" if it["requirement"].endswith(".") else "."
            it["requirement"] += (f"{stop} Answered under Raised by P1: "
                                  + " ".join(got))


def main():
    argv = sys.argv[1:]
    first, also = None, ()
    while argv[:1] in (["--answers-from"], ["--answers"]):
        if argv[0] == "--answers-from":
            first = int(argv[1])
        else:
            also = tuple(argv[1].split(","))
        argv = argv[2:]
    md, task, out, *cats = argv
    if not cats:
        raise SystemExit(__doc__)
    text = open(md, encoding="utf-8").read()
    items = table_items(text, set(cats))
    if first is not None or also:
        add_answers(items, raised_answers(text, first if first is not None
                                          else 10 ** 6, also))
    fus = json.load(open(task, encoding="utf-8"))["followUps"]
    items += [f for f in fus if f.get("role") in ("P1", "P1-critic")
              and f.get("category") in cats]
    order = sorted(cats, key=lambda c: int(c[1:]))
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"phases": "P2-P4", "round": 1, "categories": order,
                   "items": items}, f, indent=1, ensure_ascii=False)
    print(f"{out}: {len(items)} items; {', '.join(order)}")


if __name__ == "__main__":
    main()
