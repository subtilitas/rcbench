# Power-path part survey — distributor availability

**Fetched on: 2026-09-01.**

> **No figures were obtained.** Every vendor and distributor domain this survey
> needs is blocked by this environment's network egress proxy, so not one row
> below could be sourced to a page that was actually retrieved. The tables are
> kept in full, with every requested column, so that a run with network access
> can fill them in; each cell reads *blocked* rather than being left blank or
> filled from memory.

## Why this file has no numbers

The survey was to be sourced only from pages actually fetched. It could not be:
the proxy in this session refuses a TLS tunnel to all ten domains involved.

Evidence, taken at the time of writing:

```
$ curl -sS -o /dev/null --max-time 20 https://www.ti.com/lit/ds/symlink/tps55288.pdf
curl: (56) CONNECT tunnel failed, response 403

$ curl -sS -o /dev/null --max-time 20 https://example.com
curl: (56) CONNECT tunnel failed, response 403

$ curl -sS -o /dev/null -w '%{http_code}\n' --max-time 20 https://github.com
400
```

The last line is the control: github.com completes the tunnel and answers, so
the proxy is up and working and this is a domain allowlist, not a general
network fault. The fetch helper reports the same thing in its own words —
`EGRESS_BLOCKED: Access to <host> is blocked by the network egress proxy` — for
each of:

| Domain | Tried as | Result |
| --- | --- | --- |
| digikey.com | `www.digikey.com`, `digikey.com`, `www.digikey.ee` | blocked, all three |
| lcsc.com | `www.lcsc.com` | blocked |
| jlcpcb.com | `jlcpcb.com/parts/componentSearch` | blocked |
| ti.com | `www.ti.com`, `www.ti.com/lit/ds/...` | blocked |
| analog.com | `www.analog.com` | blocked |
| monolithicpower.com | `www.monolithicpower.com` | blocked |
| microchip.com | — | same proxy, not separately probed |
| st.com | — | same proxy, not separately probed |
| mouser.com | `www.mouser.com` | blocked |
| octopart.com | `octopart.com` | blocked |

Both fetch paths were tried — the URL-fetching tool and raw `curl` — and both
fail at the same place, the proxy's CONNECT, before any page is served. So this
is not a rendering problem, a bot wall, or a JavaScript-only search page; those
would have been worth reporting separately, and none of them was reached.

Web *search* is available in this session and does return result snippets
carrying stock and price figures. Those are deliberately not used here. A
snippet is undated, is frequently a stale cache of a page that has since
changed, and cannot be checked against the page it claims to quote — and stock,
price and lead time are exactly the quantities that go out of date fastest. A
purchasing decision made from one would be worse than a decision made from an
empty table, because the empty table is honest about what it does not know.

For the same reason, nothing below is filled in from prior knowledge of these
parts. Where a figure is absent it is because it could not be sourced today.

## What is needed to complete this

Add these to the environment's egress allowlist and re-run:

```
www.digikey.com  www.lcsc.com  jlcpcb.com  www.ti.com  www.analog.com
www.monolithicpower.com  www.microchip.com  www.st.com  www.mouser.com
octopart.com
```

Two of them will still need care once reachable. Digi-Key product URLs carry an
opaque numeric id (`.../TPS55288RPMR/13212451`), so each part must be reached
through the site's own search rather than by a guessed URL. The JLCPCB parts
library search is a client-side application, so a plain fetch of
`jlcpcb.com/parts/componentSearch` may return an empty shell even once the
domain is open; if it does, that is worth recording as its own distinct failure
rather than as "not carried".

Datasheet PDFs are the more robust target and should be taken first: TI's
`ti.com/lit/ds/symlink/<part>.pdf` and ADI's and Microchip's equivalents are
static files, and the Group A output-current question below is answerable from
them alone, without any distributor being reachable.

---

## Group A — buck-boost converters, I2C-programmable Vout and output current limit

| # | Part | DK orderable | DK stock | DK 1 / 10 / 100 | DK lead time | DK lifecycle | DK non-stock? | LCSC C-# | LCSC stock | LCSC 1 / 10 / 100 | JLC present | JLC type | JLC stock |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | TI TPS55288RPMR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 2 | TI TPS55289RPMR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 3 | TI TPS55285 (suffix TBD) | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 4 | MPS MP4245 (suffix TBD) | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 5 | Southchip SC8815QDER | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 6 | MPS MP8859GL-Z | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |

Rows 3 and 4 asked which orderable suffixes exist. That question is answerable
only from the vendor's own product page or parametric search, both of which are
on the blocked list, so no suffix is asserted here — including the `TPS55285VALR`
offered in the brief as an example, which is not confirmed by anything reachable
from this session.

Row 5 is worth a note for whoever picks this up: Southchip is not a
Digi-Key-catalogue manufacturer in the way TI and MPS are, so "not carried" is a
plausible real answer for that row rather than a fetch failure, and the two
should not be conflated when the table is filled in.

## Group B — 2S chargers with integrated cell balancing

| # | Part | DK orderable | DK stock | DK 1 / 10 / 100 | DK lead time | DK lifecycle | DK non-stock? | LCSC C-# | LCSC stock | LCSC 1 / 10 / 100 | JLC present | JLC type | JLC stock |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 7 | TI BQ25887RGER | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 8 | MPS MP2672A (suffix TBD) | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 9 | Injoinic IP2326 | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 10 | ADI MAX17320 (variant TBD) | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 11a | TI BQ25792RQMR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 11b | TI BQ25798RQMR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |

As with Group A, rows 8 and 10 asked for the orderable suffix or variant to be
discovered, and no source for that was reachable. Injoinic (row 9) is a
domestic-market Chinese vendor, so LCSC and JLCPCB are the rows that matter
there and Digi-Key "not carried" is the likely true answer — again, to be
confirmed rather than assumed.

## Group C — power monitors, to ~15 V and ~8 A

| # | Part | DK orderable | DK stock | DK 1 / 10 / 100 | DK lead time | DK lifecycle | DK non-stock? | LCSC C-# | LCSC stock | LCSC 1 / 10 / 100 | JLC present | JLC type | JLC stock |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 12a | TI INA260AIPWR (tape) | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 12b | TI INA260AIPW (tube) | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 13a | TI INA228AIDGSR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 13b | TI INA238AIDGSR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 13c | TI INA239AIDGSR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 14a | ADI LTC2947CUHF#PBF | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 14b | ADI LTC2947IUHF#PBF | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 15a | Microchip PAC1954T-E/JQ | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 15b | Microchip PAC1934T-I/J8X | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |
| 16 | ST TSC1641IQT | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |

## Group D — power monitors for 65 V / 300 A via external shunt

### D1 — the monitors

Rows 13a–13c above cover INA228, INA238 and INA239; they are recorded once, in
Group C, and serve both uses. The one part in this group not already listed:

| # | Part | DK orderable | DK stock | DK 1 / 10 / 100 | DK lead time | DK lifecycle | DK non-stock? | LCSC C-# | LCSC stock | LCSC 1 / 10 / 100 | JLC present | JLC type | JLC stock |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 17 | TI INA229AIDGSR | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked | blocked |

INA229 is the SPI sibling of the I2C INA228; INA239 is the SPI sibling of
INA238. That pairing decides which of the four suits a given bus and is worth
carrying into the filled-in table, but it is a structural fact about the family
rather than a figure, and it is not a substitute for the datasheet confirmation
requested below.

### D2 — 300 A-class shunts, 50–100 µΩ

The request was for what Digi-Key *actually stocks* in the 50–100 µΩ, ≥200 A
range — a parametric search against live stock. That search is on the blocked
domain, so no candidate list is reported. Reporting one from memory would be
the worst case of all here, since it would name specific part numbers,
tolerances and TCRs that nobody could tell had never been checked.

| Series | Manufacturer | DK part numbers in 50–100 µΩ, ≥200 A | Resistance | Tolerance | TCR | Power rating | Price |
| --- | --- | --- | --- | --- | --- | --- | --- |
| BAS / BVB-Z | Isabellenhütte | blocked | blocked | blocked | blocked | blocked | blocked |
| WSBS8518 | Vishay | blocked | blocked | blocked | blocked | blocked | blocked |
| CSS2H-5931 | Bourns | blocked | blocked | blocked | blocked | blocked | blocked |

When this is re-run, the Digi-Key parametric filter for current-sense resistors
takes resistance and power as ranges directly, which is a better route to the
answer than searching the three series by name — the question asked was what is
stocked in that window, and a series-by-series search will miss anything from a
fourth manufacturer that meets it.

---

## Datasheet facts

None of the following were confirmed. Every one of them was to come from a
vendor PDF, and `ti.com`, `analog.com` and `monolithicpower.com` are all blocked
at the proxy, as shown above. They are listed here in full so the questions
survive to the next attempt, with the specific section of each datasheet worth
going to.

**TPS55288 / TPS55289 / TPS55285** — for each: input voltage range; output
voltage range and programming step size; average inductor current limit, whether
it is I2C-programmable and over what range; and any datasheet statement about
deliverable output current in boost mode at ~5 V input.

*Not confirmed.* This is the load-bearing question of the survey — how much
current each part delivers at 8.4 V out from 5 V in, against the same from 12 V
in — and it deserves better than a recalled figure. Two things to look for
beyond the headline table, both of which are where this kind of question is
usually decided: the *average inductor* current limit is not the output current
limit in boost mode, since in boost the inductor carries the input current, so
the output figure follows from the limit and the conversion ratio rather than
being read off directly; and the typical-application and SOA curves near the
back of the datasheet, rather than the front-page bullet, are where a 5 V-input
boost case is actually characterised, if it is characterised at all. If the
datasheet does not state the 5 V case, that absence is itself the answer and
should be recorded as such.

**SC8815** — whether output voltage *and* current limit are both I2C-settable;
ADC resolution; and that it requires external MOSFETs. *Not confirmed.* Note
that Southchip's datasheet is often easier to obtain in Chinese than in English,
and the two revisions are not always the same document; whichever is used should
be named with its revision in the filled-in version of this file.

**BQ25887** — maximum charge current; cell-balancing current and how it is set;
maximum input voltage. *Not confirmed.*

**MP2672A** — maximum charge current; how cell balancing is enabled. *Not
confirmed.*

**INA228 vs INA238** — shunt full-scale ranges; ADC resolution; maximum
common-mode voltage; shunt-voltage LSB in each range. *Not confirmed.* The LSB
question needs care when it is answered: these parts have two selectable shunt
full-scale ranges, and the LSB differs between them, so the answer is two
numbers per part and not one. This comparison is also the one the project's own
running record leans on — see below.

**INA260** — maximum continuous current through the integrated shunt; the shunt
value; bus voltage maximum. *Not confirmed.* Worth separating, when answered,
the *shunt's* continuous rating from the package's thermal limit at the intended
ambient; for an 8 A target those are different constraints and the smaller one
governs.

**LTC2947** — bus voltage maximum; current range; internal shunt value; ADC
resolution, as an actual bit count from the datasheet. *Not confirmed.*

## Bearing on STATUS.md

`STATUS.md` currently carries an open question stating that the INA228 is
"back-ordered into January 2027, and the obvious substitute stops at 36 V, which
is under 8S", and asks that the INA238 be checked before a layout commits.

**That claim is neither confirmed nor contradicted here.** It is exactly what
row 13a and the INA228/INA238 datasheet comparison were meant to settle, and
both are blocked. It should be treated as still open, and still unverified, and
STATUS.md is left unedited for that reason — this run produced no evidence that
would justify changing what it says in either direction.

One thing that can be said without a fetch, because it is about the shape of the
claim rather than its content: a lead-time figure like "January 2027" is a
snapshot of one distributor on one day, and it is the single most perishable
number in this whole survey. Whenever it is refreshed, it is worth writing down
the date it was read and the distributor it was read from, so that the next
person can tell at a glance whether it is still worth anything.
