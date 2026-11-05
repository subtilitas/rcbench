# SPDX-License-Identifier: MIT
"""test/host/wa_mock_server.py: the mock answers as wa_api.c and wa_guard.c.

The server runs on a free port over a folder that stands in for what
wa_page_dump writes.  The reason table and the texts are the sources' own,
read from wa_api.c and ui_text.def.
"""

import http.client
import json
import pathlib
import sys
import threading
import zlib

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "test" / "host"))

import wa_mock_server as m  # noqa: E402

PAGE = b"<!doctype html><title>page</title>\n"


@pytest.fixture(scope="module")
def pages(tmp_path_factory):
    """A folder with the files wa_page_dump writes, in the shape it writes."""
    d = tmp_path_factory.mktemp("pages")
    for lang in ("en", "de"):
        (d / f"page_{lang}.html").write_bytes(PAGE + lang.encode())
        for how in ("pass", "fail"):
            (d / f"report_{lang}_{how}.txt").write_bytes(
                f"TITLE\nResult: {how}\n".encode())
    (d / "spans.json").write_text(json.dumps(
        {f"report_{lang}_{how}.txt": [[["hd", "TITLE"]]]
         for lang in ("en", "de") for how in ("pass", "fail")}))
    return d


@pytest.fixture(scope="module")
def server(pages):
    """The mock on a free port, served from a thread."""
    srv = m.make_server(pages)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    yield srv
    srv.shutdown()
    srv.server_close()


class Client:
    """One request at a time against the server, with the panel's headers."""

    def __init__(self, srv):
        """A client of @p srv."""
        self.port = srv.server_address[1]
        self.srv = srv

    def call(self, method, url, body=None, headers=None, own=True):
        """One request; own adds the headers the page's own script sends."""
        h = {"Host": f"127.0.0.1:{self.port}"}
        if own and method != "GET":
            h["Origin"] = f"http://127.0.0.1:{self.port}"
            h[m.UPLOAD_HDR] = "1"
        h.update(headers or {})
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request(method, url, body=body, headers=h)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, dict(r.getheaders()), data

    def json(self, *a, **k):
        """call() with the body read as JSON."""
        status, head, data = self.call(*a, **k)
        return status, head, (json.loads(data) if data else None)

    def reset(self, **opts):
        """Reset the mock's state."""
        self.call("POST", "/__mock/reset", json.dumps(opts).encode())

    def form(self, what, **fields):
        """POST /api/<what> with a form."""
        return self.json("POST", f"/api/{what}", "&".join(
            f"{k}={v}" for k, v in fields.items()).encode())


@pytest.fixture
def api(server):
    """A client on a freshly reset card."""
    c = Client(server)
    c.reset()
    return c


def etag(data):
    """The ETag a file of these bytes has."""
    return '"%08x"' % zlib.crc32(data)


def test_the_reason_table_is_wa_api_cs_and_every_sentence_exists():
    """34 reasons are read, each with the status and sentence of the source."""
    why = m.read_reasons()
    texts = m.read_texts()
    assert len(why) == 34
    assert why["CHANGED"] == (412, "P_API_CHANGED")
    assert why["FORM_TOO_LARGE"][0] == 413
    assert why["HOST"][0] == 421
    for word, (status, tid) in why.items():
        assert 200 <= status < 600
        assert tid == "" or tid in texts, word
    assert texts["P_API_CHANGED"][0] != texts["P_API_CHANGED"][1]


def test_adjacent_c_literals_are_one_text():
    r"""Two literals in a row join and \" unescapes."""
    assert m.c_string('"a b "\n "c\\"d"') == 'a b c"d'


def test_the_page_is_the_dump_byte_for_byte_in_the_panels_language(api):
    """GET / sends the dump's file of the language, as no-store HTML."""
    status, head, data = api.call("GET", "/")
    assert (status, data) == (200, PAGE + b"en")
    assert head["Content-Type"] == "text/html; charset=utf-8"
    assert head["Cache-Control"] == "no-store"
    api.reset(lang="de")
    assert api.call("GET", "/")[2] == PAGE + b"de"


@pytest.mark.parametrize("host,ok", [
    ("rcbench-1A2B", True), ("RCBENCH-1a2b.local", True),
    ("rcbench-1A2B.local.", True), ("127.0.0.1", True),
    ("evil.example", False),
    ("rcbench-1A2B:81", False), ("", False)])
def test_the_host_header_must_name_the_panel(api, host, ok):
    """Name, name.local, name.local. and the address pass; others get 421."""
    h = {"Host": host if ":" in host or host == "" else f"{host}:{api.port}"}
    status, _, body = api.json("GET", "/api/status", headers=h)
    assert status == (200 if ok else 421)
    if not ok:
        assert body["error"] == "HOST"
        assert body["text"]


def test_a_read_with_a_body_is_refused_before_anything_else(api):
    """A GET that carries a body is 400 BODY."""
    status, _, body = api.json("GET", "/api/status", body=b"x")
    assert (status, body["error"]) == (400, "BODY")


@pytest.mark.parametrize("headers,status", [
    ({m.UPLOAD_HDR: "0"}, 403), ({"Origin": "http://evil.example"}, 403),
    ({"Origin": "null"}, 403)])
def test_a_write_from_another_page_is_refused_403_origin(api, headers, status):
    """A wrong Origin, or an upload header that is not 1, is 403 ORIGIN."""
    s, _, body = api.json("PUT", "/api/file?path=A.TXT", b"x", headers)
    assert (s, body["error"]) == (status, "ORIGIN")
    assert "A.TXT" not in api.json("GET", "/__mock/tree")[2]["files"]


def test_a_write_without_the_header_or_the_origin_is_refused(api):
    """No X-Rcbench-Upload, or neither Origin nor Referer, is 403 ORIGIN."""
    s, _, body = api.json("PUT", "/api/file?path=A.TXT", b"x", own=False,
                          headers={"Origin": f"http://127.0.0.1:{api.port}"})
    assert (s, body["error"]) == (403, "ORIGIN")
    s, _, body = api.json("PUT", "/api/file?path=A.TXT", b"x", own=False,
                          headers={m.UPLOAD_HDR: "1"})
    assert (s, body["error"]) == (403, "ORIGIN")
    s, _, body = api.json("PUT", "/api/file?path=A.TXT", b"x", own=False,
                          headers={m.UPLOAD_HDR: "1",
                                   "Referer": f"http://127.0.0.1:{api.port}/x"})
    assert s == 201


def test_a_put_body_of_1mib_is_taken_and_one_byte_more_is_refused(api):
    """1,048,576 bytes pass the guard; 1,048,577 is 413 TOO_LARGE."""
    status, _, _ = api.json("PUT", "/api/file?path=BIG.TXT", b"a" * m.FILE_MAX)
    assert status == 201
    status, _, body = api.json("PUT", "/api/file?path=BIG2.TXT",
                               b"a" * (m.FILE_MAX + 1))
    assert (status, body["error"]) == (413, "TOO_LARGE")


def test_a_form_over_1024_bytes_is_413_form_too_large(api):
    """A POST's form of 1,025 bytes is refused; 1,024 is read."""
    s, _, body = api.form("mkdir", path="N" * 1019)   # 1,024 bytes in all
    assert (s, body["error"]) == (400, "NAME")        # read, then refused
    s, _, body = api.form("mkdir", path="N" * 1020)
    assert (s, body["error"]) == (413, "FORM_TOO_LARGE")


def test_a_file_is_read_with_its_etag_eol_and_line_count(api):
    """The ETag is the CRC-32; the eol and line headers describe the bytes."""
    api.call("POST", "/__mock/put", json.dumps(
        {"path": "A.TXT", "text": "a\r\nb\r\n"}).encode())
    s, h, data = api.call("GET", "/api/file?path=A.TXT")
    assert (s, data) == (200, b"a\r\nb\r\n")
    assert h["ETag"] == etag(data)
    assert (h["X-Rcbench-Eol"], h["X-Rcbench-Bom"], h["X-Rcbench-Lines"]) == (
        "crlf", "0", "2")
    api.call("POST", "/__mock/put", json.dumps(
        {"path": "A.TXT", "text": "a\r\nb\n"}).encode())
    head = api.call("GET", "/api/file?path=A.TXT")[1]
    assert head["X-Rcbench-Eol"] == "mixed"


def test_a_file_the_viewer_cannot_hold_is_refused_with_its_status(api):
    """A NUL in the first 4kB is 415, a locked file 403, a folder 404."""
    for body, want in ((b"a" * 4095 + b"\x00", 415),
                       (b"a" * 4096 + b"\x00", 200), (b"\n" * 65537, 422)):
        api.srv.state.card.files["V.TXT"] = body
        assert api.call("GET", "/api/file?path=V.TXT")[0] == want, want
    s, _, b = api.json("GET", "/api/file?path=BENCH/KST/CONF/KST.TXT")
    assert (s, b["error"]) == (403, "LOCKED")
    s, _, b = api.json("GET", "/api/file?path=USER&dir=1")
    assert s == 404
    s, _, b = api.json("GET", "/api/file?path=NOPE.TXT")
    assert (s, b["error"]) == (404, "NOT_FOUND")
    s, _, b = api.json("GET", "/api/file?path=HELLO.TXT&mount=9")
    assert (s, b["error"]) == (503, "CARD_GONE")


def test_a_put_checks_if_match_and_if_none_match(api):
    """If-Match on another sum is 412 with the ETag found; * on a name, 409."""
    data = b"Hello from the card.\n"
    q = "/api/file?path=HELLO.TXT"
    s, h, body = api.json("PUT", q, b"x", {"If-Match": '"00000000"'})
    assert (s, body["error"], body["etag"]) == (
        412, "CHANGED", etag(data).strip('"'))
    s, _, body = api.json("PUT", "/api/file?path=GONE.TXT", b"x",
                          {"If-Match": '"00000000"'})
    assert (s, body["error"], "etag" in body) == (412, "CHANGED", False)
    s, _, body = api.json("PUT", q, b"x", {"If-None-Match": "*"})
    assert (s, body["error"]) == (409, "EXISTS")
    s, _, body = api.json("PUT", q, b"x", {"If-Match": etag(data),
                                           "If-None-Match": "*"})
    assert (s, body["error"]) == (400, "REQUEST")
    s, _, body = api.json("PUT", q, b"x", {"If-Match": "nope"})
    assert (s, body["error"]) == (400, "REQUEST")
    s, h, body = api.json("PUT", q, b"new\n", {"If-Match": etag(data)})
    assert (s, body["created"], h["ETag"]) == (200, False, etag(b"new\n"))
    assert body["etag"] == etag(b"new\n").strip('"')
    s, h, body = api.json("PUT", "/api/file?path=NEW.TXT", b"n\n")
    assert (s, body["created"]) == (201, True)


def test_an_esc_profile_is_saved_with_the_restart_note(api):
    """A .json in BENCH/ESC/CONF answers note ESC_NEXT_START."""
    s, _, body = api.json("PUT", "/api/file?path=BENCH/ESC/CONF/P.json", b"{}")
    assert (s, body["note"]) == (201, "ESC_NEXT_START")


def test_the_gate_and_the_setting_refuse_before_the_card_is_asked(api):
    """OFF, HOLD without a window, ARMED and LOGGING refuse a write."""
    for opts, word in (({"writes": "OFF"}, "WRITES_OFF"),
                       ({"writes": "HOLD"}, "WRITES_HOLD"),
                       ({"armed": True}, "ARMED"),
                       ({"logging": True}, "LOGGING")):
        api.reset(**opts)
        s, _, body = api.json("PUT", "/api/file?path=A.TXT", b"x")
        assert (s, body["error"]) == (m.read_reasons()[word][0], word)
        status = api.json("GET", "/api/status")[2]
        assert (status["writes"], status["why"]) == ("closed", word)
        assert status["why_text"] == body["text"]
    api.reset(writes="HOLD", window=True)
    assert api.json("GET", "/api/status")[2]["writes"] == "open"
    assert api.json("PUT", "/api/file?path=A.TXT", b"x")[0] == 201


def test_a_user_log_folder_of_49_entries_pages_forward_and_back(api):
    """48 and 1, folders first, with ahead, total, prev and next."""
    s, _, p1 = api.json("GET", "/api/list?path=USER/LOGS")
    assert (p1["ahead"], p1["total"], p1["prev"], p1["next"]) == (
        0, 49, False, True)
    assert len(p1["entries"]) == 48 and p1["entries"][0]["n"] == "OLD"
    last = p1["entries"][-1]["n"]
    s, _, p2 = api.json("GET", f"/api/list?path=USER/LOGS&go=next&key={last}")
    assert [e["n"] for e in p2["entries"]] == ["NOTES.TXT"]
    assert (p2["ahead"], p2["prev"], p2["next"]) == (48, True, False)
    s, _, p3 = api.json(
        "GET", "/api/list?path=USER/LOGS&go=prev&key=NOTES.TXT")
    assert p3["entries"] == p1["entries"]
    s, _, p4 = api.json("GET", "/api/list?path=USER/LOGS&go=last")
    assert (p4["ahead"], len(p4["entries"])) == (1, 48)
    s, _, p5 = api.json("GET", "/api/list?path=USER/LOGS&go=next&key=ZZZ")
    assert p5["ahead"] == 1                 # past the end: the last page
    s, _, body = api.json("GET", "/api/list?path=NOPE")
    assert (s, body["error"]) == (404, "NOT_FOUND")
    s, _, body = api.json("GET", "/api/list?path=A%2FB")
    assert (s, body["error"]) == (400, "NAME")
    s, _, body = api.json("GET", "/api/list?go=next")
    assert (s, body["error"]) == (400, "REQUEST")


def test_entries_carry_kind_place_and_the_hidden_are_counted(api):
    """A run log, a report, the bench's folders and a dot name."""
    api.call("POST", "/__mock/put", json.dumps(
        {"path": ".hidden", "text": "x"}).encode())
    root = api.json("GET", "/api/list")[2]
    assert root["unlisted"] == 1
    by = {e["n"]: e for e in root["entries"]}
    assert (by["BENCH"]["p"], by["BENCH"]["k"]) == ("FIXED", "FOLDER")
    assert by["HELLO.TXT"]["k"] == "TEXT"
    logs = {e["n"]: e for e in api.json("GET", "/api/list?path=USER/LOGS")[2][
        "entries"]}
    assert logs["ATEST001.TXT"]["k"] == "REPORT"
    assert logs["OLD"]["p"] == "FREE"


def test_a_folder_is_made_renamed_moved_and_deleted_by_the_panels_rules(api):
    """MKDIR is 201, RENAME and MOVE 200; FIXED, NOT_EMPTY, EXISTS refuse."""
    s, _, b = api.form("mkdir", path="TRIPS")
    assert (s, b) == (201, {"path": "TRIPS", "created": True, "note": ""})
    assert api.form("mkdir", path="TRIPS")[2]["error"] == "EXISTS"
    assert api.form("mkdir", path="a%3Ab")[2]["error"] == "NAME"
    s, _, b = api.form("rename", **{"from": "TRIPS", "to": "TRIPS2",
                                    "dir": 1})
    assert (s, b["path"]) == (200, "TRIPS2")
    assert api.form("rename", **{"from": "USER", "to": "USER2",
                                 "dir": 1})[2]["error"] == "FIXED"
    assert api.form("rename", **{"from": "HELLO.TXT", "to": "A/B.TXT",
                                 "dir": 0})[2]["error"] == "REQUEST"
    s, _, b = api.form("rename", **{"from": "HELLO.TXT",
                                    "to": "TRIPS2/HELLO.TXT", "dir": 0})
    assert (s, b["path"]) == (200, "TRIPS2/HELLO.TXT")
    assert api.form("delete", path="TRIPS2", dir=1)[2]["error"] == "NOT_EMPTY"
    assert api.form("delete", path="TRIPS2/HELLO.TXT", dir=0)[0] == 200
    assert api.form("delete", path="TRIPS2", dir=1)[0] == 200
    assert api.form("delete", path="TRIPS2", dir=1)[2]["error"] == "NOT_FOUND"
    assert api.form("delete", path="BENCH/KST/CONF/KST.TXT", dir=0)[2][
        "error"] == "LOCKED"


def test_deleting_a_run_log_takes_its_report_along(api):
    """ATEST003.CSV and ATEST003.TXT go together."""
    for ext in ("CSV", "TXT"):
        api.call("POST", "/__mock/put", json.dumps(
            {"path": f"ATEST003.{ext}", "text": "x"}).encode())
    assert api.form("delete", path="ATEST003.CSV", dir=0)[0] == 200
    assert "ATEST003.TXT" not in api.json("GET", "/__mock/tree")[2]["files"]


def test_the_key_of_a_name_with_a_slash_is_refused(api):
    """A key of the list is a name, never a path."""
    s, _, b = api.json("GET", "/api/list?go=next&key=a%2Fb")
    assert (s, b["error"]) == (400, "REQUEST")


def test_the_name_rule_is_the_panel_keyboards(api):
    """Device names, edge dots and spaces and long names are refused."""
    for name in ("con", "NUL.txt", "a.", ".a", "a b ", "x" * 60, "q?"):
        assert not m.name_ok(name), name
    for name in ("a b.txt", "Größe.TXT", "x" * 59):
        assert m.name_ok(name), name
    assert m.fold("aäz") == "AÄZ" and m.fold("ß") == "ß"


def test_renaming_a_folder_keeps_what_it_holds(api):
    """The files, folders and stamps under the folder follow its new name."""
    s, _, _ = api.form("rename", **{"from": "USER/LOGS/OLD",
                                    "to": "USER/LOGS/ARCHIVE", "dir": 1})
    assert s == 200
    tree = api.json("GET", "/__mock/tree")[2]
    assert "USER/LOGS/ARCHIVE/OLD001.CSV" in tree["files"]
    assert not [p for p in tree["files"] if p.startswith("USER/LOGS/OLD")]
    assert "USER/LOGS/ARCHIVE" in tree["dirs"]
    rows = api.json("GET", "/api/list?path=USER/LOGS/ARCHIVE")[2]["entries"]
    assert [e["n"] for e in rows] == ["OLD001.CSV"]
    info = api.json("GET", "/api/info?path=USER/LOGS/ARCHIVE/OLD001.CSV")[2]
    assert info["t"] > 0


def test_a_path_outside_ascii_is_refused_as_fm_path_plain_does(api):
    """Umlauts pass the name rule but not a write path: 400 NAME."""
    s, _, body = api.json("PUT", "/api/file?path=Gr%C3%B6%C3%9Fe.TXT", b"x")
    assert (s, body["error"]) == (400, "NAME")
    assert api.form("mkdir", path="Gr%C3%B6%C3%9Fe")[2]["error"] == "NAME"
    assert api.json("GET", "/api/list")[2]["total"] == 3


def test_a_scripted_busy_file_is_listed_as_busy_and_refused(api):
    """A busy path in the reset sets b in the list and answers 423."""
    api.reset(busy=["HELLO.TXT"])
    rows = {e["n"]: e for e in api.json("GET", "/api/list")[2]["entries"]}
    assert rows["HELLO.TXT"]["b"] is True and rows["BENCH"]["b"] is False
    assert api.json("GET", "/api/file?path=HELLO.TXT")[0] == 423
    assert api.json("PUT", "/api/file?path=HELLO.TXT", b"x")[0] == 423


def test_a_destination_the_panel_protects_is_refused_for_put_rename_move(api):
    """KST.NEW and RCBENCH.OLD are LOCKED and FIXED as a destination."""
    assert api.json("PUT", "/api/file?path=KST.NEW", b"x")[0] == 201
    s, _, body = api.form("rename", **{
        "from": "KST.NEW", "to": "BENCH/KST/CONF/KST.NEW", "dir": 0})
    assert (s, body["error"]) == (403, "LOCKED")
    assert api.json("PUT", "/api/file?path=BENCH/FREE.TXT", b"x")[0] == 201
    s, _, body = api.form("rename", **{
        "from": "BENCH/FREE.TXT", "to": "BENCH/RCBENCH.OLD", "dir": 0})
    assert (s, body["error"]) == (403, "FIXED")
    s, _, body = api.json("PUT", "/api/file?path=BENCH/KST/CONF/KST.TXT", b"x")
    assert (s, body["error"]) == (403, "LOCKED")


def test_a_folder_named_as_a_protected_record_is_refused(api):
    """MKDIR of KST.NEW or RCBENCH.OLD is LOCKED or FIXED (fm_job_mkdir())."""
    s, _, body = api.form("mkdir", path="BENCH/KST/CONF/KST.NEW")
    assert (s, body["error"]) == (403, "LOCKED")
    s, _, body = api.form("mkdir", path="BENCH/RCBENCH.OLD")
    assert (s, body["error"]) == (403, "FIXED")
    assert api.form("mkdir", path="BENCH/FREE")[0] == 201


def test_a_parent_named_in_other_case_is_stored_as_the_card_spells_it(api):
    """A file or folder made in user/logs lands in USER/LOGS and is listed."""
    s, _, body = api.json("PUT", "/api/file?path=user/logs/NEW.TXT", b"x")
    assert (s, body["path"]) == (201, "USER/LOGS/NEW.TXT")
    s, _, body = api.form("mkdir", path="user/logs/SUB")
    assert (s, body["path"]) == (201, "USER/LOGS/SUB")
    first = api.json("GET", "/api/list?path=user/logs")[2]
    last = api.json("GET", "/api/list?path=USER/LOGS&go=last")[2]
    assert first["total"] == 51
    assert "SUB" in [e["n"] for e in first["entries"]]
    assert "NEW.TXT" in [e["n"] for e in last["entries"]]


def test_info_of_a_folder_sums_its_immediate_files(api):
    """USER/LOGS/OLD holds one file of 8 bytes."""
    info = api.json("GET", "/api/info?path=USER/LOGS/OLD&dir=1")[2]
    assert (info["s"], info["entries"]) == (8, 1)


def test_a_write_path_needs_room_for_the_new_suffix(api):
    """A path of 117 bytes is refused, 116 is taken: ".NEW" fits 120."""
    folder = "USER/LOGS/" + "d" * 48               # 58 bytes
    assert api.form("mkdir", path=folder)[0] == 201
    ok = f"{folder}/" + "f" * 57                    # 116 bytes
    assert api.json("PUT", f"/api/file?path={ok}", b"x")[0] == 201
    long = f"{folder}/" + "f" * 58                  # 117 bytes
    s, _, body = api.json("PUT", f"/api/file?path={long}", b"x")
    assert (s, body["error"]) == (400, "NAME")


def test_a_save_is_refused_while_its_new_file_exists(api):
    """HELLO.TXT.NEW may be a copy to recover: the save is 409 EXISTS."""
    api.call("POST", "/__mock/put", json.dumps(
        {"path": "HELLO.TXT.NEW", "text": "x"}).encode())
    s, _, body = api.json("PUT", "/api/file?path=HELLO.TXT", b"x")
    assert (s, body["error"]) == (409, "EXISTS")


def test_a_move_takes_a_name_of_63_bytes_that_a_save_refuses(api):
    """A move makes no .NEW: 63 bytes pass, 64 are not listed at all."""
    name = "n" * 60 + ".TX"                          # 63 bytes
    api.call("POST", "/__mock/put", json.dumps(
        {"path": name, "text": "x"}).encode())
    s, _, body = api.form("rename", **{"from": name,
                                       "to": f"USER/LOGS/{name}", "dir": 0})
    assert (s, body["path"]) == (200, f"USER/LOGS/{name}")
    assert api.json("PUT", f"/api/file?path={name}", b"x")[0] == 400
