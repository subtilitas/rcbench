# What this is for

<sub>**English** · [Deutsch](Manifest-de.md)</sub>

The bench exists to answer questions about a drive system that you otherwise
have to answer by guessing, by buying three separate boxes, or by sending the
motor somewhere. One board, one screen, one card.

## The pitch

Julian's, in his words:

> - Eingang für externe Stromsensoren (I²C)
> - ESC-Programmierer für das, was geht — im Moment AM32 und BLHeli garantiert,
>   YGE mal anfragen oder über EX-Bus einfach machen
> - Wuchten von Systemen über Beschleunigungssensor und Positionssensor
> - Unterstützung Reglerprogrammierung, soweit es geht
> - Servo-Tester mit allem und scharf: SBUS und andere Protokolle
> - Servo-Programmierung, wenn mir jemand Programmiergeräte ausleiht und das
>   Reverse Engineering klappt, respektive über EX-Bus
> - Logviewer für diverse Formate

## What each line means, and where it stands

| The pitch | Where it lands | State |
| --- | --- | --- |
| **External current sensors over I²C** | the coprocessor's own bus, not the panel's — the panel's is the touch controller's and must never be delayed | interface named, driver not written |
| **ESC programmer for whatever is reachable** — AM32 and BLHeli certain | one-wire half duplex at 19,200 with a handshake, on a PIO state machine | not started; needs no permission from anybody |
| **Balancing a whole system** with an accelerometer and a position sensor | the coprocessor holds both, **on one timebase** — which is the entire difficulty of a phase measurement | not started |
| **Servo tester, fully armed**: SBUS and the other protocols | hardware PWM out, one PIO program per sum-signal protocol | not started |
| **Servo programming** where it can be reverse-engineered | Hitec's D-series is the one published in full; the rest wait on a borrowed programmer | held at the owner's request |
| **Log viewer for various formats** | `shared/logfile` — the locale-tolerant CSV reader is ported and tested | reader built, screen not re-cut yet |

One feature is in the pitch only by implication and everything else needs it:
the ESC tester that produces the numbers. Its counterpart, the logger that
writes them to the card so the viewer has something to open, is not built — and
nothing is blocking it, which is a different and less comfortable position than
being blocked.

## What this says about priorities

**Nothing here is a consumer appliance.** Every line is a workshop task —
measure it, program it, balance it, read the log back. That is why the UI is
dense and high contrast rather than friendly, and why a screen that cannot do
something says which decision is missing instead of hiding the feature.

**Protocols are the long pole, not the UI.** SBUS, BLHeli and AM32 serial,
DShot telemetry: most of the remaining work is talking to other people's
firmware correctly. Which is why the parts that *can* be pinned down in pure C
— the rasteriser, the parsing, the settings model, [the link](Link.md) — are,
and are tested to the last few percent on a laptop. When the protocol work
starts, the ground under it should not move.

**"Soweit es geht" is a real constraint, not modesty.** A servo programmer has
to be borrowed. Reverse engineering may not work. And one door the pitch was
leaning on is closed: remote configuration over JETI's EX Bus is, by its own
specification, "available only for the products of JETI model" and its
description is not part of that document. What EX Bus delivers is channel
values and telemetry. For programming, the routes are the ones that were always
there — ask the manufacturer, borrow a programmer and listen to it, or reverse
engineer it.

## One licence constraint that shapes every protocol commit

Nearly every open implementation of these protocols is GPL or AGPL against this
repository's MIT. The permissive exceptions are PX4's receiver decoders (BSD)
and MIT reference code for SRXL2, JETI EX Bus, DShot and DroneCAN. Everything
else gets written from the specification, not from somebody's line.
