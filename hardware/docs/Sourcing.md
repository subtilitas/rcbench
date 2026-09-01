# Sourcing

How to find out whether a part exists to be bought. This is a short page about
a boring subject, and it is here because getting it wrong once already put a
false number into a design conversation.

## The rule

**A part is not chosen until it is chosen at a vendor**, and the vendor has to
be asked directly. Every intermediary in this chain — parametric search
engines, mirror databases, even a search engine's summary of a vendor's own
page — has been observed returning a stock figure that the vendor contradicts.

## JLCPCB

Ask JLCPCB, not a mirror of JLCPCB:

```bash
curl -s -A "Mozilla/5.0" -H "Content-Type: application/json" -X POST \
  "https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList" \
  -d '{"currentPage":1,"pageSize":12,"keyword":"INA228"}'
```

Two fields matter and only one of them is obvious:

- **`stockCount`** — what is on the shelf.
- **`canPresaleNumber`** — what may still be ordered against incoming supply. A
  **negative** value means the part is oversold: more is promised than is
  coming, and the part is unbuyable whatever `stockCount` says.

`componentLibraryType` is `basic` or `expand`; everything on this project's
list is `expand`, which on an assembly order means an extra fee and a part that
can be substituted out from under you if it runs dry between quote and build.

### The failure that produced this page

On 2026-09-01 the mirror `jlcsearch.tscircuit.com` reported **1046** pcs of
INA228AIDGSR. JLCPCB's own API, the same minute, reported **29, with
`canPresaleNumber: -477`**. The mirror was not slightly stale; it was
describing a supply situation that had inverted.

`yaqwsx/jlcparts` is the same class of artefact — a periodic snapshot of a
catalogue that moves daily. Both are excellent for *finding* a part and
worthless for *counting* one.

## Digi-Key

Digi-Key's product pages sit behind Cloudflare, so `curl` gets a 403 and a
"Just a moment..." interstitial. The pages are readable by a fetcher that
executes the challenge; the search-result URL is stable and takes a bare
manufacturer part number:

```
https://www.digikey.com/en/products/result?keywords=INA238AIDGSR
```

**Do not trust a search engine's summary of that page.** Asked about
INA228AIDGSR, a web search reported it "currently in stock and available for
order with an average time to ship of 1-3 days". The page itself said **0 in
stock, 666 expected 2026-11-03, 16-week manufacturer lead time.** The summary
was not quoting the page; it was reciting what such a page usually says.

Read three things off the page and record all three: stock, the *dated*
incoming quantity, and the manufacturer lead time. A part with 0 in stock and
2500 arriving in eight weeks is a different problem from one with 0 and nothing
scheduled.

## What the 2026-09-01 sweep showed

Two patterns worth carrying forward, both about the market rather than about
any part:

**TI is on a 16-week manufacturer lead time across the board** — every TI part
checked for [the power path](Power.md), with 26 weeks on the INA745x and the
INA260. That is long enough that a part in stock today and a part on a 16-week
lead are the same part only if nobody else wants it.

**The two vendors are anti-correlated, almost part for part.** TPS55289: 2218
at JLCPCB, zero at Digi-Key. INA238: 2246 at JLCPCB, zero at Digi-Key.
BQ25887: 874 at JLCPCB, 10,980 at Digi-Key. INA745A: 50 at JLCPCB, 6611 at
Digi-Key.

The consequence is a design rule rather than a purchasing one: **do not commit
a footprint on one vendor's stock.** Either the part is available at both, or
the board tolerates the alternative. For the monitors it does: INA228 and
INA238 are the same VSSOP-10 with the same pin order — A1, A0, ALERT, SDA,
SCL, VS, GND, IN− — so one footprint takes whichever is buyable on the day,
and the firmware asks which one it got: DEVICE_ID at 3Fh reads 2281h on the
INA228 and 2381h on the INA238. That
interchangeability is a property to preserve deliberately rather than a
coincidence to notice later. INA239 is **not** part of it: it is the SPI
member, and SPI needs different pins.
