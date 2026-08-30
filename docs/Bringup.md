# Bringing the link up on silicon

<sub>**English** · [Deutsch](Bringup-de.md)</sub>

The link has more ways to be half-broken than the display does, and most of
them present as "it does not work". The firmware says which one — this page is
how to make it tell you, and what each answer means.

## Start here: does anything cross the bus?

Before the page protocol means anything, one question has to be answered on
its own: **do frames cross this bus intact?** Answering it together with "does
the link work" is how a bring-up turns into an afternoon — a bit timing that is
slightly wrong, a missing terminator, a transceiver in the wrong mode and a
dispatcher bug all present as "the panel shows no numbers".

So there is an echo test that answers only that. The panel sends a frame, the
coprocessor sends it straight back, the panel checks it came back byte for
byte. No page map, no registers, no state machine. **If this passes and the
link still does not work, the fault is above the wire** — which is worth
knowing before anybody unplugs anything.

### Running it

The coprocessor needs nothing: it tries CAN at boot, says whether the
controller answered, and echoes probes from then on. That costs one register
read per loop and answers only a page the map does not use, so it is left in
permanently — the bring-up tool that is already flashed is the one that gets
used.

The panel side is opt-in, because starting CAN takes native USB away:

```bash
cd firmware/panel
idf.py -DRCBENCH_CAN_SELFTEST=1 build flash
```

**Watch the UART socket, not the USB one.** GPIO19 and GPIO20 carry both the
native USB and the CAN transceiver, and the multiplexer has to choose — see
[the link](Link.md). The console is on UART0 with USB-Serial-JTAG as a
secondary precisely so this session has somewhere to talk.

It runs for five seconds at boot and prints:

    can: 1000000 bit/s: brp 4, tseg1 14, tseg2 5, sjw 4, sample point 75.0%
    panel: CAN self-test: every probe came back intact
      sent 2024 echoed 2024 corrupt 0 lost 0 stale 0  (transmit queue full 0 times)
      round trip min 334 max 1356 us
      panel  tx_err 0 rx_err 0 bus_err 0
      copro  CAN up, 2024 echoes, 0 overflow(s), tx_err 0 rx_err 0 flags 0x00

**Both ends are in that report**, and the coprocessor's half arrived over the
bus rather than over a second USB cable. That matters more than the
convenience: several faults are only visible in the comparison. Frames the
coprocessor answered but the panel never heard are a return-path fault; frames
it never answered are an outbound one; and a coprocessor that dropped frames
for want of a free buffer is neither, which no bus counter anywhere records.

The status exchange is polled, shares the echo test's page and runs at the same
lowest priority. It happens **either side** of the echo phase, not only after:
the far end's counter runs from its own boot, so only the difference across the
measurement means anything — a reading taken once at the end is a lifetime
total and says nothing.

| What it says | What it means | Where to look |
| --- | --- | --- |
| `no probe came back` | Nothing crosses at all | CANH/CANL swapped? Far end powered? Both at the same bit rate? Terminators at **both** ends? |
| `probes come back altered` | Frames cross and arrive wrong | Sample point or bit timing; a missing terminator reflects |
| `probes cross, and not all of them` | Marginal | Timing, one terminator, or a bus longer than the rate |
| `probes go missing without a bus error` | They arrived intact and nobody read them | A receive buffer overran. **Not** a wiring fault — check the coprocessor's overflow count in the same report |
| `every probe came back intact` | The wire is fine | Anything still wrong is above it |

Corruption outranks loss in that table, and the ordering is deliberate: a
marginal bus does both, loss is the louder number, and it points at cable
length while the corruption says the bit timing is wrong.

Loss itself splits in two, on the controller's own error count. Frames lost
*with* bus errors were corrupted on the wire — termination, timing, length.
Frames lost with **zero** bus errors arrived perfectly and were dropped by
something that stopped reading in time, and sending somebody to check
terminators for that wastes an afternoon on hardware that is working.

The payloads are not arbitrary either. CAN stuffs a complementary bit after
five of the same polarity, so the patterns that stress a marginal bus are long
runs of one level — the probes cycle through all-dominant, all-recessive and
both alternating patterns, and a test sending only counting integers would pass
on a bus that drops real traffic.

If the coprocessor prints `CAN DID NOT ANSWER` at boot, the fault is on **SPI
and not on CAN**: the datasheet guarantees the controller wakes in
configuration mode, so a CANSTAT that says otherwise means nothing is listening
on the SPI bus the driver thinks it has. That is worth telling apart before a
scope comes out.

---

## What to write back

The round trip, both ends' error counters, and whether the coprocessor
reported any receive overflows. Those three are what the record wants: the
first sets what a poll period has to clear, and the other two say whether the
bus or the software was the limit.

Also worth noting is what the *heartbeat* did while all this was happening. Its
monostable is not fitted, so the edges reach a header pin and a scope and
nothing else — but this is a session where both boards are powered at once, and
it is the cheapest opportunity to confirm the line is edging at the rate
[safety](Safety.md) says it should.
