#!/usr/bin/env python3
"""Vendor and maker readings for the round 1 research agents.

    vendors.py fetch URL [--client chrome|safari|plain] [--marker REGEX]
                         [--out FILE]
    vendors.py jlcpcb C39843328
    vendors.py jlcpcb-search KEYWORD [--pages N]
    vendors.py digikey MPN
    vendors.py digikey-search KEYWORDS [--limit N]

Each command prints one JSON object that carries the URL or API call and the
UTC time it was read, so a figure can be dated and sourced (Sourcing rule 7 in
hardware/docs/Research.md). `jlcpcb` returns the result whose LCSC number
equals the argument, never the first result of the keyword search.

Digi-Key's credentials come from DIGIKEY_CLIENT_ID and DIGIKEY_CLIENT_SECRET,
or from the KEY=value file named by DIGIKEY_ENV_FILE. They are never printed.
The chrome and safari clients need curl_cffi (pip install curl_cffi).
"""

import argparse
import datetime
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

JLC_URL = ("https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/"
           "smtGood/selectSmtComponentList")
DK_TOKEN_URL = "https://api.digikey.com/v1/oauth2/token"
DK_DETAILS_URL = "https://api.digikey.com/products/v4/search/{}/productdetails"
DK_SEARCH_URL = "https://api.digikey.com/products/v4/search/keyword"
UA = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "Chrome/128.0 Safari/537.36")


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")


def emit(obj):
    json.dump(obj, sys.stdout, indent=1, ensure_ascii=False)
    sys.stdout.write("\n")


class Unreachable(Exception):
    pass


def http(url, data=None, headers=None, timeout=40):
    req = urllib.request.Request(url, data=data, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read(), resp.headers
    except urllib.error.HTTPError as err:
        return err.code, err.read(), err.headers
    except (urllib.error.URLError, OSError) as err:
        raise Unreachable(f"{type(err).__name__}: {err}") from err


def cmd_fetch(args):
    out = {"url": args.url, "client": args.client, "read_at": now()}
    try:
        if args.client == "plain":
            req = urllib.request.Request(args.url, headers={"User-Agent": UA})
            try:
                with urllib.request.urlopen(req, timeout=40) as resp:
                    code, body, final = resp.status, resp.read(), resp.url
            except urllib.error.HTTPError as err:
                code, body, final = err.code, err.read(), args.url
        else:
            from curl_cffi import requests as cffi
            resp = cffi.get(args.url, impersonate=args.client, timeout=40)
            code, body, final = resp.status_code, resp.content, resp.url
    except Exception as err:  # noqa: BLE001 - any failure is a reading
        reason = f"{type(err).__name__}: {err}"
        out.update(http_status=0, bytes=0, error=reason)
        emit(out)
        return 1
    out.update(http_status=code, bytes=len(body), final_url=str(final))
    text = body.decode("utf-8", errors="replace")
    walls = r"Just a moment|Access Denied|cf-mitigated"
    out["challenge"] = bool(re.search(walls, text))
    if args.marker:
        m = re.search(args.marker, text)
        # The status sits at the end of a long match; keep the last 200.
        out["marker"] = re.sub(r"\s+", " ", m.group(0))[-200:] if m else None
    if args.out:
        with open(args.out, "wb") as f:
            f.write(body)
        out["saved"] = args.out
    emit(out)
    return 0 if 200 <= code < 300 else 1


def jlc_query(keyword, page=1, size=50):
    payload = json.dumps({"currentPage": page, "pageSize": size,
                          "keyword": keyword}).encode()
    code, body, _ = http(JLC_URL, data=payload, headers={
        "User-Agent": "Mozilla/5.0", "Content-Type": "application/json"})
    if code != 200:
        raise SystemExit(f"JLCPCB API answered HTTP {code}")
    info = (json.loads(body).get("data") or {}).get("componentPageInfo") or {}
    return info.get("list") or [], info.get("pages") or 1


def jlc_row(p):
    return {
        "lcsc": p.get("componentCode"),
        "part": p.get("componentModelEn"),
        "maker_label": p.get("componentBrandEn"),
        "package": p.get("componentSpecificationEn"),
        "stockCount": p.get("stockCount"),
        "canPresaleNumber": p.get("canPresaleNumber"),
        "componentLibraryType": p.get("componentLibraryType"),
        "erpComponentName": p.get("erpComponentName"),
        "prices": [[q.get("startNumber"), q.get("productPrice")]
                   for q in p.get("componentPrices") or []],
        "description": p.get("describe"),
    }


def cmd_jlcpcb(args):
    code = args.lcsc.upper()
    rows, _ = jlc_query(code)
    match = [p for p in rows if (p.get("componentCode") or "").upper() == code]
    # jlc_query exits on any answer other than HTTP 200.
    emit({"api": JLC_URL, "keyword": code, "read_at": now(),
          "http_status": 200,
          "exact_match": jlc_row(match[0]) if match else None,
          "other_results": len(rows) - len(match)})
    return 0 if match else 1


def cmd_jlcpcb_search(args):
    found, page, pages = [], 1, 1
    while page <= min(pages, args.pages):
        rows, pages = jlc_query(args.keyword, page)
        found += [jlc_row(p) for p in rows]
        page += 1
    emit({"api": JLC_URL, "keyword": args.keyword, "read_at": now(),
          "pages_read": page - 1, "pages": pages, "results": found})
    return 0


def dk_credentials():
    cid = os.environ.get("DIGIKEY_CLIENT_ID")
    sec = os.environ.get("DIGIKEY_CLIENT_SECRET")
    path = os.environ.get("DIGIKEY_ENV_FILE")
    if (not cid or not sec) and path:
        with open(os.path.expanduser(path)) as f:
            for line in f:
                key, _, value = line.strip().partition("=")
                if key == "DIGIKEY_CLIENT_ID":
                    cid = value
                elif key == "DIGIKEY_CLIENT_SECRET":
                    sec = value
    if not cid or not sec:
        raise SystemExit("Digi-Key credentials not set: DIGIKEY_CLIENT_ID and "
                         "DIGIKEY_CLIENT_SECRET, or DIGIKEY_ENV_FILE")
    return cid, sec


def dk_headers():
    cid, sec = dk_credentials()
    form = {"client_id": cid, "client_secret": sec,
            "grant_type": "client_credentials"}
    data = urllib.parse.urlencode(form).encode()
    code, body, _ = http(DK_TOKEN_URL, data=data, headers={
        "Content-Type": "application/x-www-form-urlencoded"})
    if code != 200:
        raise SystemExit(f"Digi-Key token request answered HTTP {code}")
    token = json.loads(body)["access_token"]
    return {"Authorization": f"Bearer {token}", "X-DIGIKEY-Client-Id": cid,
            "X-DIGIKEY-Locale-Site": "US", "X-DIGIKEY-Locale-Language": "en",
            "X-DIGIKEY-Locale-Currency": "USD",
            "Content-Type": "application/json"}


def dk_product(p):
    return {
        "part": p.get("ManufacturerProductNumber"),
        "maker": (p.get("Manufacturer") or {}).get("Name"),
        "status": (p.get("ProductStatus") or {}).get("Status"),
        "lead_weeks": p.get("ManufacturerLeadWeeks"),
        "discontinued": p.get("Discontinued"),
        "end_of_life": p.get("EndOfLife"),
        "variations": [{
            "digikey_pn": v.get("DigiKeyProductNumber"),
            "packaging": (v.get("PackageType") or {}).get("Name"),
            "stock": v.get("QuantityAvailableforPackageType"),
            "min_order": v.get("MinimumOrderQuantity"),
        } for v in p.get("ProductVariations") or []],
        # Tape and reel, cut tape and Digi-Reel are views of one stock:
        # the product's QuantityAvailable, or else the largest of them.
        "stock_available": (
            p["QuantityAvailable"] if p.get("QuantityAvailable") is not None
            else max((v.get("QuantityAvailableforPackageType") or 0
                      for v in p.get("ProductVariations") or []),
                     default=0)),
        "datasheet": p.get("DatasheetUrl"),
        "url": p.get("ProductUrl"),
    }


def cmd_digikey(args):
    url = DK_DETAILS_URL.format(urllib.parse.quote(args.mpn, safe=""))
    code, body, headers = http(url, headers=dk_headers())
    out = {"api": url, "read_at": now(), "http_status": code,
           "ratelimit_remaining": headers.get("x-ratelimit-remaining")}
    if code == 200:
        out["product"] = dk_product(json.loads(body).get("Product") or {})
    else:
        out["error"] = body.decode("utf-8", errors="replace")[:500]
    emit(out)
    return 0 if code == 200 else 1


def cmd_digikey_search(args):
    payload = json.dumps({"Keywords": args.keywords,
                          "Limit": max(1, min(args.limit, 50)),
                          "Offset": 0}).encode()
    code, body, headers = http(DK_SEARCH_URL, data=payload,
                               headers=dk_headers())
    out = {"api": DK_SEARCH_URL, "keywords": args.keywords, "read_at": now(),
           "http_status": code,
           "ratelimit_remaining": headers.get("x-ratelimit-remaining")}
    if code == 200:
        data = json.loads(body)
        out["count"] = data.get("ProductsCount")
        out["products"] = [dk_product(p) for p in data.get("Products") or []]
    else:
        out["error"] = body.decode("utf-8", errors="replace")[:500]
    emit(out)
    return 0 if code == 200 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("fetch")
    f.add_argument("url")
    f.add_argument("--client", choices=["chrome", "safari", "plain"],
                   default="chrome")
    f.add_argument("--marker")
    f.add_argument("--out")
    f.set_defaults(fn=cmd_fetch)
    j = sub.add_parser("jlcpcb")
    j.add_argument("lcsc")
    j.set_defaults(fn=cmd_jlcpcb)
    js = sub.add_parser("jlcpcb-search")
    js.add_argument("keyword")
    js.add_argument("--pages", type=int, default=2)
    js.set_defaults(fn=cmd_jlcpcb_search)
    d = sub.add_parser("digikey")
    d.add_argument("mpn")
    d.set_defaults(fn=cmd_digikey)
    ds = sub.add_parser("digikey-search")
    ds.add_argument("keywords")
    ds.add_argument("--limit", type=int, default=25)
    ds.set_defaults(fn=cmd_digikey_search)
    args = ap.parse_args()
    try:
        return args.fn(args)
    except Unreachable as err:
        emit({"cmd": args.cmd, "read_at": now(), "error": str(err)})
        return 1


if __name__ == "__main__":
    sys.exit(main())
