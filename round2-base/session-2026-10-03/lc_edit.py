"""Apply the lifecycle wording of 2026-10-02 to a Research.md (and round1.js / dryrun.js when given)."""
import sys
md = sys.argv[1]
t = open(md, encoding="utf-8").read()
R = [
 ("Where another product page cannot be read or carries no status in its body, Digi-Key's API (`ProductStatus`) is read in its place and recorded as a distributor figure (owner, 2026-10-02); a part Digi-Key does not list either is recorded as not read and reported to the owner",
  "Where another product page cannot be read or carries no status in its body, Digi-Key's API (`ProductStatus`) is read in its place, from the Digi-Key product whose maker and manufacturer part number are the part's, and recorded as a distributor figure (owner, 2026-10-02); a part Digi-Key does not list under its maker either is recorded as not read and reported to the owner"),
 ("| Lifecycle of Analog Devices, Melexis and Allegro parts | Digi-Key's product status, recorded as a distributor figure.",
  "| Lifecycle of Analog Devices, Melexis and Allegro parts | Digi-Key's product status, recorded as a distributor figure; for another maker's part too where its product page cannot be read or carries no status (owner, 2026-10-02)."),
 ("For Analog Devices, Melexis and Allegro parts, Digi-Key's `ProductStatus` is the status, and a JLCPCB or LCSC status that disagrees is written down |",
  "For Analog Devices, Melexis and Allegro parts, and for a part whose status the Manufacturer status row reads at Digi-Key, Digi-Key's `ProductStatus` is the status, and a JLCPCB or LCSC status that disagrees is written down |"),
 ("is recorded with the time and the client; the figure\nis recorded as not read, and the item goes to a follow-up task.",
  "is recorded with the time and the client; the figure\nis recorded as not read, and the item goes to a follow-up task. A lifecycle\nstatus is read at Digi-Key in that case ([Lifecycle check](#lifecycle-check))."),
]
for o, n in R:
    assert t.count(o) == 1, (t.count(o), o[:60])
    t = t.replace(o, n)
open(md, "w", encoding="utf-8").write(t)
print(md, "ok")
