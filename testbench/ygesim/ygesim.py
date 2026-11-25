#!/usr/bin/env python3
"""A YGE ESC (electronic speed controller) on the station Pi's UART.

The simulator speaks OpenYGE as docs/OpenYGE.md describes it: 115200 baud
8N1, one half-duplex wire, CRC-16/XMODEM.  It answers TELE_REQ with
TELE_RESP and WRITE_PARAM_REQ with WRITE_PARAM_RESP.  Every reply carries
one (index, value) pair of its parameter table; the index cycles from 0 to
count - 1.  A write below the count is stored at once.

The Pi's TX and RX are joined on one wire, so the UART hears every byte
the simulator sends.  The echo filter drops those bytes before the
decoder sees them, and the decoder takes only frames with the master bit
set.

Modes, one per run (station step H2, testbench/ygesim/README.md):

    normal   every request answered after the turnaround (3ms)
    silent   nothing sent
    late     every reply 250ms after its request
    crc      10% of the replies with a broken CRC
    seq      every reply with the request's sequence number plus 1
    legacy   version 2 TELE_AUTO frames every 50ms, no request answered
    count33  index 0 reads 33, the table has 33 entries

``--auto`` adds TELE_AUTO frames every 50ms until the first request.

Usage on the Pi::

    python3 ygesim.py --port /dev/serial0 --mode normal

The frame and CRC code is this file's own.  pyserial is imported only when
a serial port is opened, so the tests run without it.

SPDX-License-Identifier: MIT
"""

import argparse
import random
import sys
import time

SYNC = 0xA5
VERSION = 3
VERSION_LEGACY = 2
HDR_V3 = 6
HDR_LEGACY = 4
TELE_BYTES = 26
CONTROL_BYTES = 4
MASTER = 0x80
MAX_FRAME = 64

FT_TELE_AUTO = 0x00
FT_TELE_RESP = 0x02
FT_TELE_REQ = 0x03
FT_WRITE_PARAM_RESP = 0x04
FT_WRITE_PARAM_REQ = 0x05
FT_NAMES = {
    FT_TELE_AUTO: "TELE_AUTO",
    FT_TELE_RESP: "TELE_RESP",
    FT_TELE_REQ: "TELE_REQ",
    FT_WRITE_PARAM_RESP: "WRITE_PARAM_RESP",
    FT_WRITE_PARAM_REQ: "WRITE_PARAM_REQ",
}

MODES = ("normal", "silent", "late", "crc", "seq", "legacy", "count33")
TURNAROUND_S = 0.003
LATE_S = 0.250
AUTO_PERIOD_S = 0.050
CRC_RATE = 0.10
ECHO_WAIT_S = 0.050
# Times are float seconds; a time within 1ns of its due time is due.
EPS_S = 1e-9

# Indices the write guard never sends: identity (0, 11 to 15) and the
# telemetry type (25).  The simulator stores such a write and counts it.
GUARDED = frozenset((0, 11, 12, 13, 14, 15, 25))

FIRMWARE = 103555
# A writable ESC: count 32, mode 2, BEC 7.4V, type 5712 (165 HVT),
# firmware V1.03555, serial 4660; the table of test/host/oy_sim.h.
DEFAULT_TABLE = (
    32, 2, 74, 0, 0, 5, 5, 1, 1, 2, 1, 5712,
    FIRMWARE & 0xFFFF, FIRMWARE >> 16, 0x1234, 0,
    0, 1100, 800, 20, 5, 12, 120, 5, 20, 0, 0, 12000, 0, 0, 0, 0,
)


def crc16(data, crc=0x0000):
    """CRC-16/XMODEM: polynomial 0x1021, seed 0, no reflection, no xor.

    Args:
        data: The bytes to check.
        crc: The seed, 0x0000 for OpenYGE.

    Returns:
        The CRC as an int from 0 to 65535.
    """
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def build_frame(ftype, payload, seq=0, device=0, legacy=False):
    """One frame: header, payload and the CRC, little-endian.

    Args:
        ftype: The frame type, one of the FT_ constants.
        payload: The payload bytes.
        seq: The sequence number, 0 to 255; left out of a legacy header.
        device: The device byte; left out of a legacy header.
        legacy: True for the 4-byte header of version 2.

    Returns:
        The frame as bytes.
    """
    hdr = HDR_LEGACY if legacy else HDR_V3
    n = hdr + len(payload) + 2
    head = [SYNC, VERSION_LEGACY if legacy else VERSION, ftype, n]
    if not legacy:
        head += [seq & 0xFF, device & 0xFF]
    body = bytes(head) + bytes(payload)
    crc = crc16(body)
    return body + bytes((crc & 0xFF, crc >> 8))


def control_payload(index, value):
    """The 4-byte payload of TELE_REQ and WRITE_PARAM_REQ.

    Args:
        index: The parameter index, 0 to 65535.
        value: The value to write, 0 to 65535.

    Returns:
        The payload as bytes.
    """
    return bytes((index & 0xFF, index >> 8, value & 0xFF, value >> 8))


def telemetry_payload(pidx, pdata, bec_mv=0):
    """A 26-byte telemetry payload: 25°C, 12.00V, motor stopped.

    Args:
        pidx: The parameter index the frame carries.
        pdata: That parameter's value.
        bec_mv: The BEC (battery eliminator circuit) voltage in mV.

    Returns:
        The payload as bytes.
    """
    p = bytearray(TELE_BYTES)
    p[1] = 25 + 40
    p[2:4] = (1200).to_bytes(2, "little")
    p[12:14] = (bec_mv & 0xFFFF).to_bytes(2, "little")
    p[22:24] = (pidx & 0xFFFF).to_bytes(2, "little")
    p[24:26] = (pdata & 0xFFFF).to_bytes(2, "little")
    return bytes(p)


class Frame:
    """A decoded frame.

    Attributes:
        version: The header's version byte.
        ftype: The frame type.
        seq: The sequence number, None in a legacy frame.
        device: The device byte, None in a legacy frame.
        payload: The bytes between header and CRC.
        raw: The whole frame.
    """

    def __init__(self, raw):
        """Split a frame whose CRC holds.

        Args:
            raw: The frame's bytes.
        """
        self.raw = bytes(raw)
        self.version = raw[1]
        self.ftype = raw[2]
        legacy = self.version < VERSION
        self.seq = None if legacy else raw[4]
        self.device = None if legacy else raw[5]
        self.payload = self.raw[HDR_LEGACY if legacy else HDR_V3:-2]

    def control(self):
        """The (index, value) of a request, None for another payload size.

        Returns:
            A tuple of two ints, or None.
        """
        if len(self.payload) != CONTROL_BYTES:
            return None
        p = self.payload
        return p[0] | p[1] << 8, p[2] | p[3] << 8

    def pair(self):
        """The (pidx, pdata) of a telemetry frame, None for another size.

        Returns:
            A tuple of two ints, or None.
        """
        if len(self.payload) != TELE_BYTES:
            return None
        p = self.payload
        return p[22] | p[23] << 8, p[24] | p[25] << 8


class Decoder:
    """Frames from a byte stream: every sync candidate is tried.

    Attributes:
        crc_errors: Candidates whose length fit and whose CRC failed.
    """

    def __init__(self):
        """Start with an empty buffer."""
        self.buf = bytearray()
        self.crc_errors = 0

    def feed(self, data):
        """Take bytes and return the frames they complete.

        Args:
            data: The received bytes.

        Returns:
            A list of Frame, in the order they ended.
        """
        self.buf += data
        out = []
        while True:
            start = self.buf.find(SYNC)
            if start < 0:
                self.buf.clear()
                return out
            del self.buf[:start]
            if len(self.buf) < 4:
                return out
            hdr = HDR_V3 if self.buf[1] >= VERSION else HDR_LEGACY
            n = self.buf[3]
            if n < hdr + 2 or n > MAX_FRAME:
                del self.buf[0]
                continue
            if len(self.buf) < n:
                return out
            raw = self.buf[:n]
            if crc16(raw[:-2]) != (raw[-2] | raw[-1] << 8):
                self.crc_errors += 1
                del self.buf[0]
                continue
            out.append(Frame(raw))
            del self.buf[:n]


class EchoFilter:
    """Drops the bytes the simulator hears of its own transmission.

    Attributes:
        missing: Echoes that did not arrive within 50ms: TX and RX are not
            joined.
        mismatch: Echoes that differed from what was sent: a collision.
            The bytes of that echo taken so far pass on with the rest;
            the decoder resynchronises on them.
    """

    def __init__(self):
        """Expect no echo."""
        self.expect = bytearray()
        self.run = bytearray()
        self.since = 0.0
        self.missing = 0
        self.mismatch = 0

    def sent(self, data, now):
        """Note bytes that went out.

        Args:
            data: The bytes written to the UART.
            now: The time of the write, s.
        """
        if not self.expect:
            self.since = now
        self.expect += data

    def take(self, data, now):
        """Strip the echo from received bytes.

        Args:
            data: The bytes the UART received.
            now: The time they were read, s.

        Returns:
            The bytes that came from another sender.
        """
        if self.expect and now - self.since > ECHO_WAIT_S + len(
                self.expect) * 87e-6:
            self.missing += 1
            self.expect.clear()
            self.run.clear()
        out = bytearray()
        for b in data:
            if self.expect and self.expect[0] == b:
                del self.expect[0]
                self.run.append(b)
                self.since = now
                if not self.expect:
                    self.run.clear()
                continue
            if self.expect:
                # A collision: the bytes taken as echo since the last
                # complete one may be another sender's, so they pass on.
                self.mismatch += 1
                out += self.run
                self.expect.clear()
                self.run.clear()
            out.append(b)
        return bytes(out)


class Esc:
    """The simulated ESC: table, modes and the replies it schedules.

    Attributes:
        table: The parameter values; its length is the count.
        log: A callable taking one text line.
        stats: Counters by name.
    """

    def __init__(self, mode="normal", auto=False, address=1, seed=1,
                 turnaround=TURNAROUND_S, log=None):
        """Power on.

        Args:
            mode: One of MODES.
            auto: True for TELE_AUTO frames until the first request.
            address: The ESC's address, 1 to 127.
            seed: The seed of the CRC error draw.
            turnaround: Request end to reply start in normal mode, s.
            log: A callable taking one text line, or None.

        Raises:
            ValueError: For a mode not in MODES.
        """
        if mode not in MODES:
            raise ValueError("mode %r not in %s" % (mode, ", ".join(MODES)))
        self.mode = mode
        self.auto = auto or mode == "legacy"
        self.address = address
        self.turnaround = turnaround
        self.rng = random.Random(seed)
        self.log = log or (lambda line: None)
        self.table = list(DEFAULT_TABLE)
        if mode == "count33":
            self.table.append(0)
            self.table[0] = 33
        self.cycle = 0
        self.taken = False
        self.next_auto = None
        self.stats = dict.fromkeys(
            ("requests", "writes", "sent", "auto", "crc_broken",
             "guarded", "ignored"), 0)

    def next_pair(self):
        """The next (index, value) of the cycle.

        Returns:
            A tuple of two ints.
        """
        idx = self.cycle
        self.cycle = (self.cycle + 1) % len(self.table)
        return idx, self.table[idx]

    def frame(self, ftype, seq):
        """A frame of this ESC carrying the next pair.

        Args:
            ftype: The frame type.
            seq: The sequence number to put in.

        Returns:
            The frame as bytes.
        """
        idx, val = self.next_pair()
        bec = self.table[2] * 100 if len(self.table) > 2 else 0
        return build_frame(ftype, telemetry_payload(idx, val, bec), seq,
                           self.address, self.mode == "legacy")

    def autos(self, now):
        """TELE_AUTO frames due at now.

        Args:
            now: The time, s.

        Returns:
            A list of frames to send now.
        """
        if not self.auto or self.taken or self.mode == "silent":
            return []
        if self.next_auto is None:
            self.next_auto = now
        out = []
        if now + EPS_S >= self.next_auto:
            out.append(self.frame(FT_TELE_AUTO, 0))
            self.stats["auto"] += 1
            self.next_auto = max(self.next_auto + AUTO_PERIOD_S, now)
        return out

    def on_frame(self, f, now):
        """Handle one received frame.

        Args:
            f: The Frame.
            now: The time the frame ended, s.

        Returns:
            A list of (due time in s, frame bytes) to send.
        """
        ctl = f.control()
        if (f.version < VERSION or f.device is None
                or not f.device & MASTER or ctl is None
                or f.ftype not in (FT_TELE_REQ, FT_WRITE_PARAM_REQ)):
            self.stats["ignored"] += 1
            return []
        addr = f.device & 0x7F
        if addr not in (0, self.address):
            self.stats["ignored"] += 1
            return []
        idx, val = ctl
        write = f.ftype == FT_WRITE_PARAM_REQ
        self.log("%.3f rx %s seq=%d dev=0x%02X idx=%d val=%d" % (
            now, FT_NAMES[f.ftype], f.seq, f.device, idx, val))
        self.stats["writes" if write else "requests"] += 1
        if write and idx < len(self.table):
            if idx in GUARDED:
                self.stats["guarded"] += 1
                self.log("%.3f GUARDED index %d written" % (now, idx))
            self.log("%.3f write idx=%d %d->%d" % (
                now, idx, self.table[idx], val))
            self.table[idx] = val
        if self.mode in ("silent", "legacy"):
            return []
        self.taken = True
        seq = f.seq
        if self.mode == "seq":
            seq = (seq + 1) & 0xFF
        out = self.frame(FT_WRITE_PARAM_RESP if write else FT_TELE_RESP,
                         seq)
        if self.mode == "crc" and self.rng.random() < CRC_RATE:
            out = out[:-1] + bytes((out[-1] ^ 0x5A,))
            self.stats["crc_broken"] += 1
        delay = LATE_S if self.mode == "late" else self.turnaround
        return [(now + delay, out)]


class Simulator:
    """The ESC on a wire: echo filter, decoder and the reply queue.

    Attributes:
        esc: The Esc.
        wire: An object with read() -> bytes and write(bytes).
        echo: The EchoFilter.
        decoder: The Decoder.
    """

    def __init__(self, esc, wire):
        """Join an ESC to a wire.

        Args:
            esc: The Esc.
            wire: An object with read() -> bytes and write(bytes).
        """
        self.esc = esc
        self.wire = wire
        self.echo = EchoFilter()
        self.decoder = Decoder()
        self.queue = []

    def send(self, data, now):
        """Write a frame and expect its echo.

        Args:
            data: The frame.
            now: The time, s.
        """
        self.echo.sent(data, now)
        self.wire.write(data)
        f = Frame(data) if crc16(data[:-2]) == (
            data[-2] | data[-1] << 8) else None
        pair = f.pair() if f else None
        self.esc.stats["sent"] += 1
        self.esc.log("%.3f tx %s seq=%s pidx=%s pdata=%s%s" % (
            now, FT_NAMES.get(data[2], data[2]), f.seq if f else "-",
            pair[0] if pair else "-", pair[1] if pair else "-",
            "" if f else " crc=broken"))

    def step(self, now):
        """Read, decode, answer and send what is due.

        Args:
            now: The time, s.
        """
        heard = self.echo.take(self.wire.read(), now)
        for f in self.decoder.feed(heard):
            self.queue += self.esc.on_frame(f, now)
        for data in self.esc.autos(now):
            self.send(data, now)
        self.queue.sort(key=lambda item: item[0])
        while self.queue and self.queue[0][0] <= now + EPS_S:
            self.send(self.queue.pop(0)[1], now)

    def summary(self):
        """The counters and the table as text lines.

        Returns:
            A list of str.
        """
        s = dict(self.esc.stats, crc_errors_in=self.decoder.crc_errors,
                 echo_missing=self.echo.missing,
                 echo_mismatch=self.echo.mismatch)
        lines = [" ".join("%s=%d" % kv for kv in s.items())]
        for i, v in enumerate(self.esc.table):
            lines.append("%02d = %d" % (i, v))
        return lines


class SerialWire:
    """The Pi's UART through pyserial, non-blocking."""

    def __init__(self, port):
        """Open the port at 115200 baud 8N1.

        Args:
            port: The device, such as /dev/serial0, or a pyserial URL
                such as loop://.
        """
        import serial  # pyserial, only on the station

        self.port = serial.serial_for_url(port, 115200, timeout=0)

    def read(self):
        """Every byte waiting.

        Returns:
            bytes, possibly empty.
        """
        return self.port.read(self.port.in_waiting or 1)

    def write(self, data):
        """Send bytes.

        Args:
            data: The bytes.
        """
        self.port.write(data)


def parse_set(text):
    """One --set argument, INDEX=VALUE.

    Args:
        text: The argument.

    Returns:
        A tuple (index, value).

    Raises:
        argparse.ArgumentTypeError: For a malformed or out-of-range pair.
    """
    try:
        idx, val = (int(x, 0) for x in text.split("=", 1))
    except ValueError:
        raise argparse.ArgumentTypeError("expected INDEX=VALUE: " + text)
    if not 0 <= idx < 64 or not 0 <= val <= 0xFFFF:
        raise argparse.ArgumentTypeError("out of range: " + text)
    return idx, val


def main(argv=None):
    """Run the simulator, then print the counters and the table.

    Args:
        argv: The arguments, sys.argv[1:] when None.

    Returns:
        The exit status.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default="/dev/serial0")
    ap.add_argument("--mode", choices=MODES, default="normal")
    ap.add_argument("--auto", action="store_true",
                    help="TELE_AUTO frames every 50ms until a request")
    ap.add_argument("--address", type=int, default=1)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--turnaround-ms", type=float, default=3.0)
    ap.add_argument("--set", type=parse_set, action="append", default=[],
                    metavar="INDEX=VALUE", help="start value of a parameter")
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="run time; 0 runs until Ctrl-C")
    ap.add_argument("--quiet", action="store_true", help="no frame lines")
    args = ap.parse_args(argv)
    esc = Esc(args.mode, args.auto, args.address, args.seed,
              args.turnaround_ms / 1000.0,
              None if args.quiet else print)
    for idx, val in args.set:
        if idx >= len(esc.table):
            ap.error("index %d at or above the count %d"
                     % (idx, len(esc.table)))
        esc.table[idx] = val
    sim = Simulator(esc, SerialWire(args.port))
    t0 = time.monotonic()
    try:
        while not args.seconds or time.monotonic() - t0 < args.seconds:
            sim.step(time.monotonic() - t0)
            time.sleep(0.0005)
    except KeyboardInterrupt:
        pass
    print("\n".join(sim.summary()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
