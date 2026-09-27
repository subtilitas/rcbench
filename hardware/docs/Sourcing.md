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

`componentBrandEn` is the maker as JLCPCB labels it. Some rows carry the
label "JLCPCB Assembly" with a stock of 0; that is not a maker. A search for
MB85RC256V on 2026-09-27 returned four such rows beside the RAMXEED/FUJITSU
parts. It labelled C2061051 and C2061057, both MB85RC256V, "Fuji Electric". A
label can name the wrong maker.

`componentLibraryType` is `basic` or `expand`. Every part on this project's
list is `expand`, which on an assembly order means an extra fee and a part that
can be substituted if it runs out between quote and build.

### Observed divergence, 2026-09-01

The mirror `jlcsearch.tscircuit.com` reported 1046 pcs of INA228AIDGSR.
JLCPCB's own API, the same minute, reported 29 with `canPresaleNumber: -477`.
`yaqwsx/jlcparts` is the same class of source: a periodic snapshot. Both are
usable for finding a part and not for counting one.

### jlcparts truncated, 2026-09-14

On 2026-09-14 a failed download made the jlcparts workflow start an empty
database and publish it over the full one (upstream issue #159; the fix,
pull request #165, was open and not merged on 2026-09-27). On 2026-09-27 the
copy at `yaqwsx.github.io/jlcparts/data/` held 985,000 rows in
`jlc_components`, none below LCSC number C6374508, the `C` part number JLCPCB
and its distributor LCSC give each part. It held none of the round 1 fixed
inputs or held parts. The full copy held 7,161,863. The full copy of 2026-09-14
is kept on the research server ([Research](Research.md#sourcing-rules), rule
3). In it, 25.1 % of rows have an empty maker field, so a search that filters
on the maker drops parts such as INA238AIDGSR (C2868250). Before a later copy
replaces it, check its row count and that known LCSC numbers are present.

## Digi-Key

Ask Digi-Key's API, Product Information V4, with the owner's credentials in
`DIGIKEY_CLIENT_ID` and `DIGIKEY_CLIENT_SECRET`. A token lasts 599 s:

```bash
TOKEN=$(curl -s -X POST https://api.digikey.com/v1/oauth2/token \
  --data-urlencode "client_id=$DIGIKEY_CLIENT_ID" \
  --data-urlencode "client_secret=$DIGIKEY_CLIENT_SECRET" \
  --data-urlencode grant_type=client_credentials |
  python3 -c 'import json,sys; print(json.load(sys.stdin)["access_token"])')
curl -s "https://api.digikey.com/products/v4/search/INA238AIDGSR/productdetails" \
  -H "Authorization: Bearer $TOKEN" -H "X-DIGIKEY-Client-Id: $DIGIKEY_CLIENT_ID" \
  -H "X-DIGIKEY-Locale-Site: US" -H "X-DIGIKEY-Locale-Language: en" \
  -H "X-DIGIKEY-Locale-Currency: USD"
```

Three fields of the reply's `Product` object matter:

- `ProductVariations[].QuantityAvailableforPackageType`: the stock of each
  packaging (tape and reel, cut tape, Digi-Reel).
- `ManufacturerLeadWeeks`: the manufacturer's lead time.
- `ProductStatus.Status`: Digi-Key's lifecycle status.

The API carries no dated incoming quantity, and the research records none.
It reads Digi-Key through the API only and records stock, the manufacturer's
lead time and the product status (owner, 2026-09-27). On 2026-09-27 INA238AIDGSR read 0 in each
of its three packagings, 16 weeks, Active. The response header
`x-ratelimit-limit` read 1000; its time window is not stated.

For a check by hand outside the research: Digi-Key's product pages sit
behind Cloudflare, and `curl` gets a 403. The pages are readable by a fetcher
that executes the challenge. The search-result URL
takes a bare manufacturer part number:

```
https://www.digikey.com/en/products/result?keywords=INA238AIDGSR
```

Do not use a search engine's summary of that page. Asked about INA228AIDGSR, a
web search reported it "currently in stock and available for order with an
average time to ship of 1-3 days"; the page itself said 0 in stock, 666
expected 2026-11-03, 16-week manufacturer lead time.

From a page read by hand, record three values: stock, the dated incoming
quantity, and the manufacturer lead time.

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
