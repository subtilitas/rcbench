import json,sys
d=json.load(open(sys.argv[1]))
r=d['data']
for fv in r.get('found_values',[]): print('FV',json.dumps(fv))
print()
for rp in r.get('report',[]): print('RP',json.dumps(rp))
for f in r.get('functions',[]):
  print('==',f['function'])
  for q in f['requirements']: print('  REQ',json.dumps(q)[:500])
  for s in f['shortlist']: print('  SL',s['rank'],s['part'],'|',s['maker'],'|',s['lcsc'],'|',s.get('second_source_route'),s.get('second_source_part'),'|',s['gate'][:250])
  for x in f['dropped']: print('  DROP',x['part'],'|',x['maker'],'|',x['reason'][:300])
