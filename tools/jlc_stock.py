#!/usr/bin/env python3
"""Re-read the stock of every part in hardware/docs/Parts.md.

Reads JLCPCB's own API for every LCSC number in Parts.md and for each row's
alternate, and takes the result whose LCSC number equals the row's, never
the first result of the keyword search.  A part's gate is rule 4 of the
sourcing rules in hardware/docs/Research.md: stockCount at or above 5 times
the need, never under 50, and canPresaleNumber at zero or above.  The need is
the build quantity times the row's placements per board.

A row whose second source is Digi-Key, and a part off the board with its
alternate, is read at Digi-Key's API (Product Information V4) with the
owner's credentials: DIGIKEY_CLIENT_ID and DIGIKEY_CLIENT_SECRET in the
environment, or the KEY=value file named by DIGIKEY_ENV_FILE, as
tools/research/vendors.py reads them.  They are never printed.  A row held
in the owner's personal library is checked by rule 6 and printed apart: the
quantity the owner stated, with its date, at or above the need.  Its
second source at Digi-Key is read as for any other row.

    python3 tools/jlc_stock.py 5            # report only, exit 0
    python3 tools/jlc_stock.py --check 5    # exit 1 on any [FAIL] line

Each problem is one line that starts "[FAIL] PART", PART the part number
alone.  A stock shortfall reads "[FAIL] PART: stock N, gate G", or
"[FAIL] PART (second vendor): stock N, gate G" at Digi-Key; a presale
shortfall reads "[FAIL] PART: presale N".  A failed lookup is a [FAIL] line
in other words.  A part that fails rule 4 while its alternate passes is a
[warn] line.  The report ends with "N problem(s)", N the count of [FAIL]
lines.

Run by hand before an order, not in CI (continuous integration): a stock
count moving is not a defect in the tree.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

REPO = pathlib.Path(__file__).resolve().parent.parent
PARTS_MD = REPO / "hardware" / "docs" / "Parts.md"

JLC_URL = ("https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/"
           "smtGood/selectSmtComponentList")
DK_TOKEN_URL = "https://api.digikey.com/v1/oauth2/token"
DK_DETAILS_URL = "https://api.digikey.com/products/v4/search/{}/productdetails"
DK_SEARCH_URL = "https://api.digikey.com/products/v4/search/keyword"

# The tables of Parts.md this tool reads, by the heading above each.
SELECTED = "Selected parts"
OTHERS = "Alternates and Q options"

LCSC_RE = re.compile(r"\bC\d{3,}\b")
TICKED_RE = re.compile(r"`([^`]+)`")


class Row:
    """One table row of Parts.md, reduced to what a stock check reads."""

    def __init__(self, table: str, cells: dict[str, str]) -> None:
        self.table = table
        self.function = cells.get("Function", "")
        self.part = cells.get("Part number", "").strip("` ")
        self.maker = cells.get("Manufacturer", "")
        lcsc = LCSC_RE.search(cells.get("LCSC", ""))
        self.lcsc = lcsc.group(0) if lcsc else ""
        self.placements = leading_int(cells.get("Placements per board", ""))
        held = cells.get("Held", "")
        self.held = leading_int(held) if held[:1].isdigit() else 0
        date = re.search(r"\d{4}-\d{2}-\d{2}", held)
        self.held_date = date.group(0) if date else "no date"
        source = cells.get("Second source", "")
        head = source.strip().lower()
        self.route = ("digikey" if head.startswith("digi-key")
                      else "alternate" if head.startswith("alternate")
                      else "none")
        ticked = TICKED_RE.search(source) if self.route == "digikey" else None
        self.dk_part = ticked.group(1) if ticked else self.part
        alt = cells.get("Alternate", "").strip()
        self.alt = "" if alt.lower().startswith("none") else (
            alt.split("(")[0].strip().strip("`"))
        alt_lcsc = LCSC_RE.search(alt)
        self.alt_lcsc = alt_lcsc.group(0) if alt_lcsc else ""


def leading_int(text: str) -> int:
    m = re.match(r"\s*(\d+)", text)
    return int(m.group(1)) if m else 0


def read_rows(path: pathlib.Path = PARTS_MD) -> list[Row]:
    """The rows of the two tables, each tagged with the table it is in."""
    rows, table, header = [], "", None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            table, header = line[3:].strip(), None
            continue
        if table not in (SELECTED, OTHERS) or not line.startswith("|"):
            header = None if not line.startswith("|") else header
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if header is None:
            header = cells
        elif set("".join(cells)) <= set("-: "):
            continue
        else:
            rows.append(Row(table, dict(zip(header, cells, strict=False))))
    return rows


def gate(boards: int, placements: int) -> int:
    """Rule 4 (question S4): 5 times the need, never under 50."""
    return max(5 * boards * placements, 50)


def http(url, data=None, headers=None, timeout=40):
    req = urllib.request.Request(url, data=data, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as err:
        return err.code, err.read()
    except (urllib.error.URLError, OSError) as err:
        return 0, str(err).encode()


def jlc_exact(lcsc: str):
    """(error, row) for the JLCPCB result whose LCSC number is lcsc."""
    payload = json.dumps({"currentPage": 1, "pageSize": 50,
                          "keyword": lcsc}).encode()
    code, body = http(JLC_URL, data=payload, headers={
        "User-Agent": "Mozilla/5.0", "Content-Type": "application/json"})
    if code != 200:
        return f"JLCPCB lookup failed, HTTP {code}", None
    try:
        info = (json.loads(body).get("data") or {}).get(
            "componentPageInfo") or {}
    except ValueError:
        return "JLCPCB answer is not JSON", None
    for p in info.get("list") or []:
        if (p.get("componentCode") or "").upper() == lcsc.upper():
            return "", p
    return f"no exact match for {lcsc} at JLCPCB", None


def dk_credentials():
    cid = os.environ.get("DIGIKEY_CLIENT_ID")
    sec = os.environ.get("DIGIKEY_CLIENT_SECRET")
    path = os.environ.get("DIGIKEY_ENV_FILE")
    if (not cid or not sec) and path:
        try:
            with open(os.path.expanduser(path)) as f:
                for line in f:
                    key, _, value = line.strip().partition("=")
                    if key == "DIGIKEY_CLIENT_ID":
                        cid = value
                    elif key == "DIGIKEY_CLIENT_SECRET":
                        sec = value
        except OSError:
            return None, None
    return (cid, sec) if cid and sec else (None, None)


class DigiKey:
    """Digi-Key's API with one token for the run."""

    def __init__(self) -> None:
        self.headers = None
        self.error = "no Digi-Key credentials"
        cid, sec = dk_credentials()
        if not cid:
            return
        form = urllib.parse.urlencode({
            "client_id": cid, "client_secret": sec,
            "grant_type": "client_credentials"}).encode()
        code, body = http(DK_TOKEN_URL, data=form, headers={
            "Content-Type": "application/x-www-form-urlencoded"})
        if code != 200:
            self.error = f"Digi-Key token request failed, HTTP {code}"
            return
        token = json.loads(body)["access_token"]
        self.headers = {
            "Authorization": f"Bearer {token}", "X-DIGIKEY-Client-Id": cid,
            "X-DIGIKEY-Locale-Site": "US", "X-DIGIKEY-Locale-Language": "en",
            "X-DIGIKEY-Locale-Currency": "USD",
            "Content-Type": "application/json"}
        self.error = ""

    def stock(self, mpn: str, maker: str):
        """(error, stock) for the part number, the maker's product when
        several products carry it."""
        if self.headers is None:
            return self.error, None
        url = DK_DETAILS_URL.format(urllib.parse.quote(mpn, safe=""))
        code, body = http(url, headers=self.headers)
        if code == 200:
            return "", available(json.loads(body).get("Product") or {})
        if code != 404:
            return f"Digi-Key lookup failed, HTTP {code}", None
        # 404: not listed under this string, or several products carry it
        # ("Duplicate Products found"). One keyword search lists them.
        payload = json.dumps({"Keywords": mpn, "Limit": 50,
                              "Offset": 0}).encode()
        code, body = http(DK_SEARCH_URL, data=payload, headers=self.headers)
        if code != 200:
            return f"Digi-Key search failed, HTTP {code}", None
        want = norm(mpn)
        found = [p for p in json.loads(body).get("Products") or []
                 if norm(p.get("ManufacturerProductNumber")) == want]
        if not found:
            return f"no Digi-Key product carries {mpn}", None
        first = maker.split()[0].lower() if maker.split() else ""
        own = [p for p in found
               if first and first in ((p.get("Manufacturer") or {})
                                      .get("Name") or "").lower()]
        return "", max(available(p) for p in own or found)


def available(product: dict) -> int:
    """Tape and reel, cut tape and Digi-Reel are views of one stock: the
    product's QuantityAvailable, or else the largest of them."""
    if product.get("QuantityAvailable") is not None:
        return int(product["QuantityAvailable"])
    return max((v.get("QuantityAvailableforPackageType") or 0
                for v in product.get("ProductVariations") or []), default=0)


def norm(part) -> str:
    return re.sub(r"[^0-9A-Z]", "", str(part or "").upper())


class Report:
    def __init__(self) -> None:
        self.fails = 0

    def line(self, text: str) -> None:
        print(text)

    def fail(self, text: str) -> None:
        self.fails += 1
        print(f"[FAIL] {text}")


def rule4(p: dict, row_gate: int) -> tuple[bool, int, int]:
    stock = int(p.get("stockCount") or 0)
    presale = p.get("canPresaleNumber")
    presale = 0 if presale is None else int(presale)
    return stock >= row_gate and presale >= 0, stock, presale


def shortfalls(rep: Report, part: str, stock: int, presale: int,
               row_gate: int) -> None:
    if stock < row_gate:
        rep.fail(f"{part}: stock {stock}, gate {row_gate}")
    if presale < 0:
        rep.fail(f"{part}: presale {presale}")


def check_jlc(rep: Report, row: Row, boards: int) -> set[str]:
    """Rule 4 at JLCPCB for an on-board row and its alternate. Returns the
    LCSC numbers read."""
    row_gate = gate(boards, row.placements)
    read = {row.lcsc}
    err, p = jlc_exact(row.lcsc)
    if err:
        rep.fail(f"{row.part}: {err}")
        return read
    ok, stock, presale = rule4(p, row_gate)
    rep.line(f"  [{'ok' if ok else '..'}] {row.part} ({row.lcsc}): stock "
             f"{stock}, presale {presale}, gate {row_gate}")
    alt_ok, alt_read = None, False
    if row.alt and row.alt_lcsc:
        read.add(row.alt_lcsc)
        aerr, ap = jlc_exact(row.alt_lcsc)
        if aerr:
            rep.fail(f"{row.alt}: {aerr}")
        else:
            alt_read = True
            alt_ok, astock, apresale = rule4(ap, row_gate)
            rep.line(f"  [{'ok' if alt_ok else '..'}] {row.alt} "
                     f"({row.alt_lcsc}), alternate of {row.part}: stock "
                     f"{astock}, presale {apresale}, gate {row_gate}")
            if not alt_ok and not ok:
                shortfalls(rep, row.alt, astock, apresale, row_gate)
    if not ok:
        if alt_ok:
            rep.line(f"  [warn] {row.part} fails rule 4 and its alternate "
                     f"{row.alt} passes")
        else:
            if row.alt and not alt_read:
                rep.line(f"  {row.part} fails rule 4; its alternate "
                         f"{row.alt} was not read")
            shortfalls(rep, row.part, stock, presale, row_gate)
    return read


def check_digikey(rep: Report, dk: DigiKey, row: Row, boards: int) -> None:
    row_gate = gate(boards, row.placements)
    if row.lcsc:
        # On the board: Digi-Key is the second vendor (rule 5).
        err, stock = dk.stock(row.dk_part, row.maker)
        if err:
            rep.fail(f"{row.part}: second vendor not checked, {err}")
        elif stock < row_gate:
            rep.fail(f"{row.part} (second vendor): stock {stock}, "
                     f"gate {row_gate}")
        else:
            rep.line(f"  [ok] {row.part} at Digi-Key as {row.dk_part}: "
                     f"stock {stock}, gate {row_gate}")
        return
    # Off the board: Digi-Key is where the part is bought (rule 1), and
    # the alternate is its only second source (rule 5).
    for mpn, what in ((row.part, "part off the board"),
                      (row.alt, f"alternate of {row.part}")):
        if not mpn:
            rep.line(f"  [note] {row.part}: no alternate, so no rule-5 "
                     "second source")
            continue
        err, stock = dk.stock(mpn, row.maker)
        if err:
            rep.fail(f"{mpn}: not checked, {err}")
        elif stock < row_gate:
            rep.line(f"  {mpn} is the {what}")
            rep.fail(f"{mpn}: stock {stock}, gate {row_gate}")
        else:
            rep.line(f"  [ok] {mpn} at Digi-Key, {what}: stock {stock}, "
                     f"gate {row_gate}")


def check(boards: int) -> int:
    rows = read_rows()
    selected = [r for r in rows if r.table == SELECTED]
    others = [r for r in rows if r.table == OTHERS]
    dk = DigiKey()
    rep = Report()
    print(f"{len(selected)} parts in {PARTS_MD.relative_to(REPO)}, "
          f"{boards} boards; gate = max(5 x {boards} x placements, 50)")
    if dk.error:
        print(f"Digi-Key: {dk.error}; its rows are not checked")
    read: set[str] = set()
    held = []
    for row in selected:
        if row.held:
            held.append(row)
            continue
        print(f"{row.part}:")
        if row.lcsc:
            read |= check_jlc(rep, row, boards)
        if row.route == "digikey" or not row.lcsc:
            check_digikey(rep, dk, row, boards)
    for row in others:
        if row.lcsc in read or not row.lcsc:
            continue
        row_gate = gate(boards, row.placements)
        err, p = jlc_exact(row.lcsc)
        if err:
            rep.fail(f"{row.part}: {err}")
            continue
        ok, stock, presale = rule4(p, row_gate)
        mark = "ok" if ok else "warn"
        print(f"  [{mark}] {row.part} ({row.lcsc}), not selected: stock "
              f"{stock}, presale {presale}, gate {row_gate}")
        if row.route == "digikey":
            # A verified option the owner did not choose: read at its
            # second vendor too, reported but not a problem of the build.
            derr, dstock = dk.stock(row.dk_part, row.maker)
            if derr:
                print(f"  [warn] {row.part} at Digi-Key, not selected: "
                      f"not checked, {derr}")
            else:
                mark = "ok" if dstock >= row_gate else "warn"
                print(f"  [{mark}] {row.part} at Digi-Key as {row.dk_part}, "
                      f"not selected: stock {dstock}, gate {row_gate}")
    if held:
        print("Personal library (rule 6):")
    for row in held:
        need = boards * row.placements
        err, p = jlc_exact(row.lcsc) if row.lcsc else ("no LCSC", None)
        if err:
            rep.fail(f"{row.part}: {err}")
        shop = (f"; JLCPCB stock {p.get('stockCount')}, presale "
                f"{p.get('canPresaleNumber')}" if p else f"; {err}")
        if row.held >= need:
            print(f"  [ok] {row.part}: held {row.held} (stated "
                  f"{row.held_date}), need {need}{shop}")
        else:
            rep.fail(f"{row.part}: held {row.held} (stated "
                     f"{row.held_date}) is under the need of {need}")
        if row.route == "digikey":
            # A held part still needs its rule-5 second source.
            check_digikey(rep, dk, row, boards)
    print(f"{rep.fails} problem(s)")
    return rep.fails


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("boards", type=int,
                    help="boards in the build being ordered")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 on any [FAIL] line")
    args = ap.parse_args()
    if args.boards < 1:
        sys.exit("the build quantity is 1 or more")
    fails = check(args.boards)
    return 1 if args.check and fails else 0


if __name__ == "__main__":
    sys.exit(main())
