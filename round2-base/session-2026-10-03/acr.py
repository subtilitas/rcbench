import re,sys
for f in sys.argv[1:]:
    seen={}
    for n,l in enumerate(open(f,encoding='utf-8'),1):
        # skip code spans
        l2=re.sub(r'`[^`]*`','',l)
        for m in re.finditer(r'(?<![\w/.-])([A-Z][A-Za-z]*[A-Z][A-Za-z0-9]*|[A-Z]{2,})(?![\w/-])',l2):
            t=m.group(1)
            if re.search(r'\d',t): continue
            if t not in seen: seen[t]=(n,l2[max(0,m.start()-30):m.end()+60].strip())
    print('=====',f)
    for t,(n,c) in sorted(seen.items(), key=lambda x:x[1][0]):
        print(f'{n:4d} {t:12s} {c}')
