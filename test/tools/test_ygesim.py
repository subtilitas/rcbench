"""testbench/ygesim: the YGE ESC simulator against an in-memory wire.

Frames and the CRC-16/XMODEM check values of docs/OpenYGE.md, every mode
of station step H2, writes, the address and the echo filter.  The wire is a
loopback: what the simulator writes comes back to its own receiver, as on
the station's joined TX and RX, and also reaches the master.

SPDX-License-Identifier: MIT
"""

import argparse
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "testbench" / "ygesim"))
import ygesim as ys  # noqa: E402

MS = 0.001
ESC_TYPES = (ys.FT_TELE_AUTO, ys.FT_TELE_RESP, ys.FT_WRITE_PARAM_RESP)


class Loopback:
    """A half-duplex wire: every byte reaches both ends."""

    def __init__(self, echo=True):
        """Start idle.

        Args:
            echo: False for a wire whose TX does not reach RX.
        """
        self.echo = echo
        self.to_esc = bytearray()
        self.to_master = bytearray()

    def read(self):
        """The simulator's side: every byte waiting."""
        out = bytes(self.to_esc)
        self.to_esc.clear()
        return out

    def write(self, data):
        """The simulator sends."""
        if self.echo:
            self.to_esc += data
        self.to_master += data

    def master_send(self, data):
        """The master sends; it hears its own bytes as well."""
        self.to_esc += data
        self.to_master += data

    def master_take(self):
        """Every byte that reached the master since the last take."""
        out = bytes(self.to_master)
        self.to_master.clear()
        return out


def request(seq, ftype=ys.FT_TELE_REQ, idx=0, val=0, device=0x81):
    """A master frame: 12 bytes with the control payload."""
    return ys.build_frame(ftype, ys.control_payload(idx, val), seq, device)


def esc_frames(wire, decoder=None):
    """The frames the ESC sent, the master's own requests left out."""
    dec = decoder or ys.Decoder()
    return [f for f in dec.feed(wire.master_take())
            if f.ftype in ESC_TYPES]


def rig(mode="normal", echo=True, **kw):
    """A simulator on a loopback wire."""
    wire = Loopback(echo)
    return ys.Simulator(ys.Esc(mode, **kw), wire), wire


def poll(sim, wire, seq, t, wait=10 * MS, **kw):
    """Send one request at t, step through wait, return the ESC frames."""
    wire.master_send(request(seq, **kw))
    sim.step(t)
    sim.step(t + wait)
    return esc_frames(wire)


# CRC and frames


def test_crc_check_values():
    """docs/OpenYGE.md 2.3: 0x31C3 with seed 0, 0x29B1 with 0xFFFF."""
    assert ys.crc16(b"123456789") == 0x31C3
    assert ys.crc16(b"123456789", 0xFFFF) == 0x29B1
    assert ys.crc16(b"") == 0


def test_frame_lengths():
    """A request is 12 bytes, a v3 telemetry frame 34, a legacy one 32."""
    req = request(7)
    assert len(req) == 12 and req[3] == 12
    assert req[:6] == bytes((0xA5, 3, 0x03, 12, 7, 0x81))
    tele = ys.build_frame(ys.FT_TELE_RESP, ys.telemetry_payload(3, 4))
    assert len(tele) == 34 and tele[3] == 34
    leg = ys.build_frame(ys.FT_TELE_AUTO, ys.telemetry_payload(3, 4),
                         legacy=True)
    assert len(leg) == 32 and leg[1] == 2
    assert ys.crc16(leg[:-2]) == leg[-2] | leg[-1] << 8


def test_frame_round_trip():
    """Build, decode, and read back type, seq, device and the pair."""
    raw = ys.build_frame(ys.FT_TELE_RESP,
                         ys.telemetry_payload(0x0102, 0xBEEF, 7400), 255, 1)
    (f,) = ys.Decoder().feed(raw)
    assert (f.ftype, f.seq, f.device) == (ys.FT_TELE_RESP, 255, 1)
    assert f.pair() == (0x0102, 0xBEEF)
    assert f.control() is None
    assert f.payload[1] == 65 and f.payload[12:14] == (7400).to_bytes(
        2, "little")
    (r,) = ys.Decoder().feed(request(1, ys.FT_WRITE_PARAM_REQ, 21, 13))
    assert r.control() == (21, 13) and r.pair() is None


def test_decoder_byte_by_byte_and_noise():
    """Noise with a false sync in front, then a frame fed byte by byte."""
    dec = ys.Decoder()
    got = []
    for b in bytes((0x00, 0xA5, 0x03, 0xFF, 0xA5)) + request(9):
        got += dec.feed(bytes((b,)))
    assert len(got) == 1 and got[0].seq == 9


def test_decoder_counts_crc_errors():
    """A broken frame is counted and the next good one still decodes."""
    bad = bytearray(request(1))
    bad[-1] ^= 0x01
    dec = ys.Decoder()
    got = dec.feed(bytes(bad) + request(2))
    assert [f.seq for f in got] == [2]
    assert dec.crc_errors == 1


def test_decoder_skips_a_stalled_false_header():
    """A false header claiming 64 bytes does not hold up the requests.

    A5 03 03 40 then 3 requests: 40 bytes, short of the 64 the header
    claims.  Each request decodes as it completes.
    """
    dec = ys.Decoder()
    assert dec.feed(bytes((0xA5, 0x03, 0x03, 0x40))) == []
    for s in (1, 2, 3):
        assert [f.seq for f in dec.feed(request(s))] == [s]
    assert dec.resyncs == 1 and dec.crc_errors == 0


def test_decoder_waits_for_a_frame_still_arriving():
    """A real frame in pieces is not dropped while its bytes come in."""
    raw = ys.build_frame(ys.FT_TELE_RESP, ys.telemetry_payload(1, 2), 5, 1)
    dec = ys.Decoder()
    got = []
    for k in range(0, len(raw), 5):
        got += dec.feed(raw[k:k + 5])
    assert [f.seq for f in got] == [5] and dec.resyncs == 0


def test_false_header_then_session_polls():
    """The simulator answers the requests behind a stalled false header."""
    sim, wire = rig()
    wire.master_send(bytes((0xA5, 0x03, 0x03, 0x40)))
    sim.step(0.0)
    for s in range(3):
        assert len(poll(sim, wire, s, 0.2 * (s + 1))) == 1
    assert sim.esc.stats["requests"] == 3


def test_decoder_length_bounds():
    """A length below header plus CRC or above 64 is not a frame."""
    dec = ys.Decoder()
    assert dec.feed(bytes((0xA5, 3, 0x03, 7, 0, 0, 0))) == []
    assert dec.feed(bytes((0xA5, 3, 0x03, 65)) + bytes(70)) == []
    assert dec.feed(request(3))[0].seq == 3


# Normal mode


def test_normal_answers_after_turnaround():
    """No reply at 2ms, the reply at 3ms with the request's seq."""
    sim, wire = rig()
    wire.master_send(request(42))
    sim.step(0.0)
    sim.step(2 * MS)
    assert esc_frames(wire) == []
    sim.step(3 * MS)
    (f,) = esc_frames(wire)
    assert (f.ftype, f.seq, f.device, f.version) == (
        ys.FT_TELE_RESP, 42, 1, 3)


def test_normal_cycles_the_table():
    """33 requests: indices 0 to 31, then 0 again; the values match."""
    sim, wire = rig()
    pairs = [poll(sim, wire, s, s * 50 * MS)[0].pair() for s in range(33)]
    assert [p[0] for p in pairs] == list(range(32)) + [0]
    assert [p[1] for p in pairs[:32]] == list(ys.DEFAULT_TABLE)
    assert sim.esc.stats["requests"] == 33


def test_seq_wrap_echoed():
    """Sequence 255 and 0 come back unchanged."""
    sim, wire = rig()
    assert poll(sim, wire, 255, 0.0)[0].seq == 255
    assert poll(sim, wire, 0, 0.05)[0].seq == 0


def test_address_and_broadcast():
    """Address 1 and broadcast 0 answered, address 2 and no master bit not.

    A frame without the master bit is another ESC's or the own echo.
    """
    sim, wire = rig()
    assert len(poll(sim, wire, 1, 0.0, device=0x81)) == 1
    assert len(poll(sim, wire, 2, 0.05, device=0x80)) == 1
    assert poll(sim, wire, 3, 0.10, device=0x82) == []
    assert poll(sim, wire, 4, 0.15, device=0x01) == []
    assert sim.esc.stats["ignored"] == 2


def test_other_frame_types_ignored():
    """An unknown type or a telemetry type from the master: no answer."""
    sim, wire = rig()
    poll(sim, wire, 1, 0.0, ftype=0x06)
    poll(sim, wire, 2, 0.05, ftype=ys.FT_TELE_RESP)
    assert sim.esc.stats["sent"] == 0 and sim.esc.stats["ignored"] == 2


# Writes


def test_write_applied_and_read_back():
    """Index 21 from 12 to 13: WRITE_PARAM_RESP, then the cycle shows 13."""
    sim, wire = rig()
    (f,) = poll(sim, wire, 5, 0.0, ftype=ys.FT_WRITE_PARAM_REQ, idx=21,
                val=13)
    assert f.ftype == ys.FT_WRITE_PARAM_RESP and f.seq == 5
    assert sim.esc.table[21] == 13
    seen = dict(poll(sim, wire, s, 0.05 * s)[0].pair()
                for s in range(1, 33))
    assert seen[21] == 13 and sim.esc.stats["writes"] == 1


def test_write_at_or_above_count_not_stored():
    """Index 32 of a 32-entry table changes nothing and is answered."""
    sim, wire = rig()
    before = list(sim.esc.table)
    assert len(poll(sim, wire, 1, 0.0, ftype=ys.FT_WRITE_PARAM_REQ,
                    idx=32, val=1)) == 1
    assert sim.esc.table == before


@pytest.mark.parametrize("idx", sorted(ys.GUARDED))
def test_guarded_write_counted(idx):
    """A write the guard never sends is stored and counted."""
    sim, wire = rig()
    poll(sim, wire, 1, 0.0, ftype=ys.FT_WRITE_PARAM_REQ, idx=idx, val=7)
    assert sim.esc.stats["guarded"] == 1 and sim.esc.table[idx] == 7


def test_write_to_editable_not_guarded():
    """Index 21 is not a guarded index."""
    sim, wire = rig()
    poll(sim, wire, 1, 0.0, ftype=ys.FT_WRITE_PARAM_REQ, idx=21, val=7)
    assert sim.esc.stats["guarded"] == 0


# Modes


def test_silent_sends_nothing():
    """Requests, writes and --auto: not one byte goes out."""
    sim, wire = rig("silent", auto=True)
    poll(sim, wire, 1, 0.0)
    poll(sim, wire, 2, 0.5, ftype=ys.FT_WRITE_PARAM_REQ, idx=21, val=9)
    assert wire.master_take() == b""
    assert sim.esc.stats["sent"] == 0
    assert sim.esc.stats["requests"] == 1 and sim.esc.table[21] == 9


def test_late_reply_at_250ms():
    """Nothing at 249ms, the reply at 250ms."""
    sim, wire = rig("late")
    wire.master_send(request(8))
    sim.step(0.0)
    sim.step(249 * MS)
    assert esc_frames(wire) == []
    sim.step(250 * MS)
    (f,) = esc_frames(wire)
    assert f.seq == 8


def test_crc_mode_breaks_ten_percent():
    """1000 replies: 70 to 130 fail the master's CRC, the rest decode."""
    sim, wire = rig("crc", seed=7)
    dec = ys.Decoder()
    good = 0
    for s in range(1000):
        wire.master_send(request(s & 0xFF))
        sim.step(s * 0.05)
        sim.step(s * 0.05 + 10 * MS)
        good += len(esc_frames(wire, dec))
    assert 70 <= dec.crc_errors <= 130
    assert dec.crc_errors == sim.esc.stats["crc_broken"]
    assert good + dec.crc_errors == 1000


def test_crc_mode_same_seed_same_draw():
    """The draw repeats with the seed."""
    counts = []
    for _ in range(2):
        sim, wire = rig("crc", seed=3)
        for s in range(200):
            poll(sim, wire, s & 0xFF, s * 0.05)
        counts.append(sim.esc.stats["crc_broken"])
    assert counts[0] == counts[1] > 0


def test_seq_mode_answers_plus_one():
    """Every reply carries the request's seq plus 1, 255 to 0."""
    sim, wire = rig("seq")
    assert poll(sim, wire, 10, 0.0)[0].seq == 11
    assert poll(sim, wire, 255, 0.05)[0].seq == 0


def test_legacy_broadcasts_and_never_answers():
    """Version 2 TELE_AUTO frames of 32 bytes every 50ms; no reply."""
    sim, wire = rig("legacy")
    for k in range(5):
        wire.master_send(request(k))
        sim.step(k * 50 * MS)
    frames = esc_frames(wire)
    assert len(frames) == 5
    for f in frames:
        assert (f.ftype, f.version, f.seq, len(f.raw)) == (
            ys.FT_TELE_AUTO, 2, None, 32)
    assert [f.pair()[0] for f in frames] == [0, 1, 2, 3, 4]
    assert sim.esc.stats["requests"] == 5
    assert sim.esc.stats["sent"] == sim.esc.stats["auto"] == 5


def test_count33():
    """Index 0 reads 33 and the cycle runs through index 32."""
    sim, wire = rig("count33")
    pairs = [poll(sim, wire, s, s * 0.05)[0].pair() for s in range(34)]
    assert pairs[0] == (0, 33)
    assert [p[0] for p in pairs] == list(range(33)) + [0]


def test_unknown_mode_refused():
    """A mode outside MODES raises."""
    with pytest.raises(ValueError):
        ys.Esc("fast")


def test_auto_until_takeover():
    """TELE_AUTO every 50ms; after the first request none, only replies."""
    sim, wire = rig("normal", auto=True)
    for k in range(4):
        sim.step(k * 50 * MS)
    autos = esc_frames(wire)
    assert [f.ftype for f in autos] == [ys.FT_TELE_AUTO] * 4
    assert all(f.version == 3 and f.device == 1 for f in autos)
    poll(sim, wire, 1, 0.2)
    for k in range(5, 10):
        sim.step(k * 50 * MS)
    assert esc_frames(wire) == []


def test_no_auto_by_default():
    """Without --auto a v3 ESC is quiet until asked."""
    sim, wire = rig()
    for k in range(10):
        sim.step(k * 50 * MS)
    assert wire.master_take() == b""


# Echo


def test_own_echo_dropped():
    """100 polls on a joined wire: the echo never reaches the decoder."""
    sim, wire = rig()
    for s in range(100):
        assert len(poll(sim, wire, s, s * 0.05)) == 1
    assert sim.esc.stats["requests"] == 100
    assert sim.esc.stats["ignored"] == 0
    assert sim.decoder.crc_errors == 0
    assert (sim.echo.missing, sim.echo.mismatch) == (0, 0)


def test_echo_filter_strips_only_its_bytes():
    """The echo comes off the front; the bytes after it pass."""
    ef = ys.EchoFilter()
    ef.sent(b"\x01\x02\x03", 0.0)
    assert ef.take(b"\x01\x02", 0.001) == b""
    assert ef.take(b"\x03\xA5\x03", 0.002) == b"\xA5\x03"
    assert (ef.missing, ef.mismatch) == (0, 0)


def test_echo_collision_counted():
    """A byte that differs from the echo ends it and passes on."""
    ef = ys.EchoFilter()
    ef.sent(b"\x01\x02", 0.0)
    assert ef.take(b"\x01\x09\x02", 0.001) == b"\x01\x09\x02"
    assert ef.mismatch == 1


def test_echo_missing_on_split_wire():
    """TX not joined to RX: the missing echo is counted once.

    The next request still decodes.
    """
    sim, wire = rig(echo=False)
    assert len(poll(sim, wire, 1, 0.0)) == 1
    sim.step(0.2)
    assert sim.echo.missing == 1
    assert len(poll(sim, wire, 2, 0.3)) == 1
    assert sim.esc.stats["requests"] == 2


def test_echo_mismatch_does_not_eat_a_request():
    """A request arriving inside the echo window is still decoded."""
    sim, wire = rig()
    wire.master_send(request(1))
    sim.step(0.0)
    sim.step(0.003)                    # reply sent, echo pending
    wire.to_esc.clear()                # the echo lost on the wire
    wire.master_send(request(2))
    sim.step(0.004)
    assert sim.esc.stats["requests"] == 2
    assert sim.echo.mismatch == 1


# Arguments and summary


def test_parse_set():
    """INDEX=VALUE in decimal or hex; out of range refused."""
    assert ys.parse_set("21=13") == (21, 13)
    assert ys.parse_set("0x0b=0x1650") == (11, 0x1650)
    for bad in ("21", "x=1", "64=0", "1=65536", "-1=0"):
        with pytest.raises(argparse.ArgumentTypeError):
            ys.parse_set(bad)


def test_summary_lists_counters_and_table():
    """One counter line, then one line per index."""
    sim, wire = rig()
    poll(sim, wire, 1, 0.0)
    lines = sim.summary()
    assert "requests=1" in lines[0] and "echo_missing=0" in lines[0]
    assert "resyncs=0" in lines[0]
    assert lines[1] == "00 = 32" and len(lines) == 33


def test_main_on_a_pyserial_loopback(capsys):
    """main() on pyserial's loop://: auto frames, every echo dropped."""
    from conftest import import_or_fail
    import_or_fail("serial", "pyserial")
    assert ys.main(["--port", "loop://", "--auto", "--seconds", "0.3",
                    "--quiet", "--set", "21=13"]) == 0
    lines = capsys.readouterr().out.splitlines()
    stats = dict(kv.split("=") for kv in lines[0].split())
    assert int(stats["auto"]) >= 4
    assert stats["echo_missing"] == stats["echo_mismatch"] == "0"
    assert stats["requests"] == stats["ignored"] == "0"
    assert stats["crc_errors_in"] == "0"
    assert "21 = 13" in lines
