#!/usr/bin/env python3
"""Vendor and maker readings for the round 1 research agents.

    vendors.py fetch URL [--client chrome|safari|plain] [--marker REGEX]
                         [--out FILE]
    vendors.py jlcpcb C39843328
    vendors.py jlcpcb-search KEYWORD [--pages N]
    vendors.py digikey MPN
    vendors.py digikey-search KEYWORDS [--limit N]
    vendors.py digikey-quota

Each command prints one JSON object that carries the URL or API call and the
UTC time it was read, so a figure can be dated and sourced (Sourcing rule 7 in
hardware/docs/Research.md). `jlcpcb` returns the result whose LCSC number
equals the argument, never the first result of the keyword search.

Digi-Key's credentials come from DIGIKEY_CLIENT_ID and DIGIKEY_CLIENT_SECRET,
or from the KEY=value file named by DIGIKEY_ENV_FILE. They are never printed.
Digi-Key allows 1,000 API calls a day, counted per UTC day. With
DIGIKEY_CACHE_DIR set, `digikey` and `digikey-search` keep each answer of
HTTP 200 or 404 in a folder of that directory for the UTC day, and give it
again for the same part or keywords that day, marked "cached": true, with the
time and API call of the reading. A search also keeps each part number it
lists as that part's details reading, unless one is kept already. A part
number several Digi-Key products carry (404 "Duplicate Products found") is
read with one keyword search: the answer lists the matches with their makers,
or gives the one product. `digikey-quota` prints the calls left today and
when the count resets; it takes one call.
The chrome and safari clients need curl_cffi (pip install curl_cffi).
"""

import argparse
import contextlib
import datetime
import fcntl
import hashlib
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


def dk_cache(kind, key):
    """The file that keeps today's answer to one Digi-Key call, or None
    without DIGIKEY_CACHE_DIR. One folder per UTC day, the day Digi-Key's
    count of calls runs over."""
    folder = os.environ.get("DIGIKEY_CACHE_DIR")
    if not folder:
        return None
    day = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    digest = hashlib.sha256(f"{kind}\0{key}".encode()).hexdigest()[:24]
    return os.path.join(folder, day, f"{kind}-{digest}.json")


def dk_norm(part):
    """A part number as the cache keys it: upper case, runs of whitespace as
    one space, none at the ends. The details request sends the same form."""
    return " ".join(str(part or "").split()).upper()


@contextlib.contextmanager
def dk_locked(path):
    """Hold the key's lock while the cache is read, the API called and the
    answer kept, so agents that ask for one part at once make one call."""
    if not path:
        yield
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + ".lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def dk_kept(path):
    if not path:
        return None
    try:
        with open(path) as f:
            out = json.load(f)
    except (OSError, ValueError):
        return None
    out["cached"] = True
    return out


def dk_keep(path, out, only_new=False):
    """Keep an answer of HTTP 200 or 404: a reading, or a part number
    Digi-Key does not list. A refusal such as 429 is read again."""
    if not path or out.get("http_status") not in (200, 404):
        return
    if only_new and os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w") as f:
        json.dump(out, f, ensure_ascii=False)
    os.replace(tmp, path)


def cmd_digikey(args):
    path = dk_cache("details", dk_norm(args.mpn))
    with dk_locked(path):
        return dk_details(args, path)


def dk_details(args, path):
    kept = dk_kept(path)
    if kept:
        emit(kept)
        return 0 if kept.get("http_status") == 200 else 1
    mpn = " ".join(args.mpn.split())
    url = DK_DETAILS_URL.format(urllib.parse.quote(mpn, safe=""))
    code, body, headers = http(url, headers=dk_headers())
    out = {"api": url, "read_at": now(), "http_status": code,
           "ratelimit_remaining": headers.get("x-ratelimit-remaining")}
    if code == 200:
        out["product"] = dk_product(json.loads(body).get("Product") or {})
    else:
        out["error"] = body.decode("utf-8", errors="replace")[:500]
    # A part number several Digi-Key products carry answers 404 "Duplicate
    # Products found": one keyword search lists them with their makers. The
    # 404 is kept only once the search resolved it; a failed search is
    # asked again.
    if code == 404 and "Duplicate Products" in out.get("error", ""):
        resolved = dk_duplicates(mpn, url)
        if resolved:
            out = resolved
            dk_keep(path, out)
    else:
        dk_keep(path, out)
    emit(out)
    return 0 if out.get("http_status") == 200 else 1


def dk_search_call(keywords, limit):
    payload = json.dumps({"Keywords": keywords, "Limit": limit,
                          "Offset": 0}).encode()
    code, body, headers = http(DK_SEARCH_URL, data=payload,
                               headers=dk_headers())
    out = {"api": DK_SEARCH_URL, "keywords": keywords, "read_at": now(),
           "http_status": code,
           "ratelimit_remaining": headers.get("x-ratelimit-remaining")}
    if code == 200:
        data = json.loads(body)
        out["count"] = data.get("ProductsCount")
        out["products"] = [dk_product(p) for p in data.get("Products") or []]
    else:
        out["error"] = body.decode("utf-8", errors="replace")[:500]
    return out


def dk_complete(search):
    """Whether a search returned every product it matched."""
    count = search.get("count")
    return isinstance(count, int) and count <= len(search.get("products")
                                                   or [])


def dk_by_part(products):
    """The products of a search grouped by part number."""
    groups = {}
    for p in products:
        if p.get("part"):
            groups.setdefault(dk_norm(p["part"]), []).append(p)
    return groups


def dk_reading(search, part_products, **extra):
    """A details reading made from a search's products for one part number:
    the product when one carries it, else every match with its maker."""
    out = {"api": search["api"], "keywords": search["keywords"],
           "read_at": search["read_at"], "http_status": 200,
           "ratelimit_remaining": search.get("ratelimit_remaining"), **extra}
    if len(part_products) == 1:
        out["product"] = part_products[0]
    else:
        out["matches"] = part_products
    return out


def dk_duplicates(mpn, details_api):
    search = dk_search_call(mpn.strip(), 50)
    if search["http_status"] != 200:
        return None
    matches = dk_by_part(search["products"]).get(dk_norm(mpn), [])
    if not matches:
        return None
    # complete: false when the search matched more products than it
    # returned, so a maker may be missing from the matches.
    return dk_reading(search, matches, details_api=details_api,
                      details_status=404, duplicate=True,
                      complete=dk_complete(search))


def dk_quota():
    """(HTTP status, calls left today, reset time) from one details call."""
    url = DK_DETAILS_URL.format(urllib.parse.quote("INA238AIDGSR", safe=""))
    code, _, headers = http(url, headers=dk_headers())
    left = headers.get("x-ratelimit-remaining")
    return (code, int(left) if str(left or "").isdigit() else None,
            headers.get("x-ratelimit-resettime"))


def cmd_digikey_quota(_args):
    code, left, reset = dk_quota()
    emit({"read_at": now(), "http_status": code, "remaining": left,
          "reset": reset})
    return 0 if left is not None else 1


def cmd_digikey_search(args):
    limit = max(1, min(args.limit, 50))
    path = dk_cache("search", f"{args.keywords.strip()}\0{limit}")
    with dk_locked(path):
        return dk_search(args, path, limit)


def dk_search(args, path, limit):
    kept = dk_kept(path)
    if kept:
        emit(kept)
        return 0 if kept.get("http_status") == 200 else 1
    out = dk_search_call(args.keywords, limit)
    dk_keep(path, out)
    # Each part number the search lists becomes today's details reading of
    # it, unless one is kept already: a search record carries the same
    # fields as a details record. Only a search that returned every product
    # it matched: a slice may hold one of a part number's several products.
    if out["http_status"] == 200 and dk_complete(out):
        for part, products in dk_by_part(out["products"]).items():
            derived = dk_cache("details", part)
            with dk_locked(derived):
                dk_keep(derived, dk_reading(out, products, from_search=True),
                        only_new=True)
    emit(out)
    return 0 if out["http_status"] == 200 else 1


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
    sub.add_parser("digikey-quota").set_defaults(fn=cmd_digikey_quota)
    args = ap.parse_args()
    try:
        return args.fn(args)
    except Unreachable as err:
        emit({"cmd": args.cmd, "read_at": now(), "error": str(err)})
        return 1


if __name__ == "__main__":
    sys.exit(main())
