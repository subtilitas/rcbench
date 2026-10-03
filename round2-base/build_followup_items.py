"""Build FU-2P1.json from the Follow-ups table of Research.md's Round 2 section."""
import json, re, sys
text = open(sys.argv[1], encoding="utf-8").read()
sec = text.split("## Round 2", 1)[1].split("### Follow-ups", 1)[1].split("### Runs", 1)[0]
items = []
for line in sec.splitlines():
    if not line.startswith("| ") or line.startswith("| Function") or line.startswith("| ---"):
        continue
    cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
    fn, cat, req, src = cells
    for c in [x.strip() for x in cat.split(",")]:
        items.append({"category": c, "function": fn, "requirement": req,
                      "source": src, "reason": "requirement changed after round 1 or round 1 left the function without a part (Research.md, Round 2, Follow-ups)"})
cats = sorted({i["category"] for i in items}, key=lambda c: int(c[1:]))
json.dump({"phases": "P1", "round": 1, "categories": cats, "items": items},
          open(sys.argv[2], "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print(len(items), "items;", ", ".join(cats))
