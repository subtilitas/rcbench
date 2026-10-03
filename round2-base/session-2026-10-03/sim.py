import sys, types, json, os
sys.path.insert(0, '/home/claude/github/rcbench/tools/research')
src = open('/home/claude/github/rcbench/tools/research/session.py').read()
# fallback
old = "                slot[e[\"function\"]] = entry\n    return eff\n"
assert old in src
new = '''                slot[e["function"]] = entry
    read_runs = [(n, t) for n, t in runs(results, pending=pending) if before is None or t.get("sequence", 0) < before]
    if fixed is None:
        fixed = dict(load("categories.json").get("fixed_inputs", {}))
        fixed.setdefault("R7", [{"function": "Pack cells", "part": "SLSXT30002130"}])
        for x in fixed.get("R8", []): x.setdefault("orderable", "INA228AIDGSR")
        for x in fixed.get("R6", []): x.setdefault("orderable", "TPS55285VALR")
    for c, xs in fixed.items():
        for x in xs:
            fn, T = x["function"], x["part"]; O = x.get("orderable") or T
            e = eff.get(c, {}).get(fn)
            if e and str(e.get("part") or "").upper().startswith(T.upper()):
                continue
            e = e or {}
            unv = [{"run": n, "part": row.get("part"), "status": row.get("status"), "reason": row.get("reason", "")}
                   for n, t in read_runs for res in ((t.get("summary") or {}).get("results") or []) if isinstance(res, dict) and res.get("category") == c
                   for row in res.get("ledger") or [] if row.get("function") == fn and T.upper() in str(row.get("part") or "").upper() and not str(row.get("status", "")).startswith("verified")]
            eff.setdefault(c, {})[fn] = {"part": O, "rank": None, "alternate": "", "run": "owner", "alternate_unverified": [], "second_source_missing": False, "q_alternatives": [], "decision": e.get("decision") or "none", "q_options_missing": [], "owner_fixed": {"input": T, "left_open_by": e.get("run") or "", "replaced": e.get("part") or e.get("not_requalified") or "", "unverified": unv}}
    return eff
'''
src = src.replace(old, new)
src = src.replace("def effective_selection(results, pending=None, before=None):", "def effective_selection(results, pending=None, before=None, fixed=None):")
SKIP = os.environ.get("SKIP", "1") == "1"
if SKIP:
    o = "        for fn, e in fns.items():\n            # The kept part, its rule-5 alternate, each Q alternative and\n            # that one's alternate.\n"
    assert o in src
    src = src.replace(o, "        for fn, e in fns.items():\n            if e.get('run') == 'owner':\n                continue\n", 1)
o = "    # The category IDs a conflict's or gap's category names"
assert o in src
src = src.replace(o, "    for c_, fns_ in eff.items():\n        for fn_, e_ in fns_.items():\n            if e_.get('run') == 'owner' and e_.get('part'):\n                selected.add(e_['part']); part_fn.setdefault(e_['part'], set()).add((c_, fn_))\n" + o, 1)
m = types.ModuleType('session'); m.__file__ = '/home/claude/github/rcbench/tools/research/session.py'
sys.modules['session'] = m
exec(compile(src, m.__file__, 'exec'), m.__dict__)
s = m
