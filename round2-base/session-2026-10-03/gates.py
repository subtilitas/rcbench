import sys
sys.dont_write_bytecode=True
sys.path.insert(0,'/home/claude/github/rcbench/tools/research')
import session as S
S.use_round(2)
R='/home/claude/rcbench-research/round2/results'
cats=S.load("categories.json")
eff=S.effective_selection(R)
up=[c for t in ("T2","T4") for c in cats["tasks"][t]["categories"]]
print('up',up)
print('--- open_selections R3')
for x in S.open_selections(eff,['R3']): print('  ',x)
print('--- open_in_category R3')
for x in S.open_in_category(R,['R3']): print('  ',x)
print('--- refuted_for_itself'); print(S.refuted_for_itself(R,['R3']))
print('--- p1_unresolved R3'); print(S.p1_unresolved(R,['R3']))
import subprocess
commit=S.git("rev-parse","origin/research/round2")
text=S.git("show",f"{commit}:{S.PLAN_REL}")
rows=S.raised_rows(text)
print('--- stale R3'); print(S.stale_selections(R,['R3'],rows,commit,up,cats["tasks"]["T3"]["categories"]))
print('--- q9_open'); print(S.q9_open(R,S.decisions(text)))
print('--- t6_open'); o,l=S.t6_open(R); print(o); 
for x in l: 
    js=str(x)
    if 'R3' in js: print('  LEFT',js[:300])
