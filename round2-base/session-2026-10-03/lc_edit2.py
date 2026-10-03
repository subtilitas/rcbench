import sys
md=sys.argv[1]; t=open(md,encoding="utf-8").read()
o="Digi-Key's `ProductStatus`: Active passes; Not For New Designs, Last Time Buy and Obsolete fail; any other value is recorded and reported to the owner"
n="Digi-Key's `ProductStatus`: Active passes; Not For New Designs, Last Time Buy and Obsolete fail, and Preliminary, a preview, fails under S5; any other value is recorded and reported to the owner"
assert t.count(o)==1; open(md,"w",encoding="utf-8").write(t.replace(o,n)); print(md,"ok")
