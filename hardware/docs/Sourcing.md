# Sourcing

How to find out whether a part can be bought.

## Rule

A part is not chosen until it is available at a vendor, and the vendor is asked
directly. Parametric search engines, mirror databases and a search engine's
summary of a vendor's page have all returned stock figures the vendor
contradicted.

## JLCPCB

Ask JLCPCB's own API (application programming interface):

```bash
curl -s -A "Mozilla/5.0" -H "Content-Type: application/json" -X POST \
  "https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList" \
  -d '{"currentPage":1,"pageSize":12,"keyword":"INA228"}'
```

Two fields matter:

- `stockCount`: what is on the shelf.
- `canPresaleNumber`: what may still be ordered against incoming supply. A
  negative value means the part is oversold and unbuyable whatever `stockCount`
  says.

`componentLibraryType` is `basic` or `expand`. Every part on this project's
list is `expand`, which on an assembly order means an extra fee and a part that
can be substituted if it runs out between quote and build.

### Observed divergence, 2026-09-01

The mirror `jlcsearch.tscircuit.com` reported 1046 pcs of INA228AIDGSR.
JLCPCB's own API, the same minute, reported 29 with `canPresaleNumber: -477`.
`yaqwsx/jlcparts` is the same class of source: a periodic snapshot. Both are
usable for finding a part and not for counting one.

## Digi-Key

Digi-Key's product pages sit behind Cloudflare; `curl` gets a 403. The pages
are readable by a fetcher that executes the challenge. The search-result URL
takes a bare manufacturer part number:

```
https://www.digikey.com/en/products/result?keywords=INA238AIDGSR
```

Do not use a search engine's summary of that page. Asked about INA228AIDGSR, a
web search reported it "currently in stock and available for order with an
average time to ship of 1-3 days"; the page itself said 0 in stock, 666
expected 2026-11-03, 16-week manufacturer lead time.

Record three values: stock, the dated incoming quantity, and the manufacturer
lead time.

## The 2026-09-01 sweep

- TI quoted a 16-week manufacturer lead time on every part checked for [the
  power path](Power.md), and 26 weeks on the INA745x and the INA260.
- The two vendors are anti-correlated almost part for part. TPS55289: 2218 at
  JLCPCB, 0 at Digi-Key. INA238: 2246 at JLCPCB, 0 at Digi-Key. BQ25887: 874 at
  JLCPCB, 10,980 at Digi-Key. INA745A: 50 at JLCPCB, 6611 at Digi-Key.

Consequence: do not commit a footprint on one vendor's stock. Either the part
is available at both, or the board tolerates the alternative. For the monitors
it does: INA228 and INA238 share the VSSOP-10 and the pin order (A1, A0, ALERT,
SDA, SCL, VS, GND, IN−), so one footprint takes either, and the firmware reads
DEVICE_ID at 0x3F (0x2281 for the INA228, 0x2381 for the INA238). INA239 is not
interchangeable: it is the SPI (Serial Peripheral Interface) member with
different pins.
