# What this is for

<sub>**English** · [Deutsch](Manifest-de.md)</sub>

The bench answers questions about a drive system that you otherwise answer by
guessing, by buying three separate boxes, or by sending the motor away. One
board, one screen, one card. This page is what it can do today, what each
feature still needs, and what will not be built — so you know what to expect
and what not to wait for.

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
| **ESC programmer for whatever is reachable** — AM32 and BLHeli certain | one-wire half duplex at 19,200 with a handshake, on a PIO state machine | screen built and data-driven; no protocol speaks on a wire yet |
| **Balancing a whole system** with an accelerometer and a position sensor | the coprocessor holds both, **on one timebase** — which is the entire difficulty of a phase measurement | screen and placement guides built; the measurement waits on the sensors |
| **Servo tester, fully armed**: SBUS and the other protocols | hardware PWM out, one PIO program per sum-signal protocol | screen built and commanding over the link; the output stage itself is next |
| **Servo programming** where it can be reverse-engineered | Hitec's D-series is the one published in full; the rest wait on a borrowed programmer | held at the owner's request |
| **Log viewer for various formats** | `shared/logfile` — the locale-tolerant CSV reader is ported and tested | built: browse, import view, plot — and armed runs are recorded to the card |

## What not to wait for

Three doors are closed, and knowing that saves you watching them:

- **Configuration over JETI's EX Bus.** The specification says remote
  configuration is "available only for the products of JETI model" and keeps
  its description out of the document. EX Bus delivers channel values and
  telemetry — programming a device through it is not coming.
- **BLHeli_32 parameters.** The bench identifies and drives these ESCs, and
  direction, 3D mode, beacon and save-settings work; the parameter table does
  not. The reason is a key rather than effort, it was asked for, and the answer
  was no — [the full answer](BLHeli32.md).
- **Servo programming for KST** is held at the owner's request.

And one rule that shapes what arrives: every protocol here is written from its
specification, because nearly every open implementation is GPL or AGPL against
this repository's MIT. A protocol without a published specification arrives
late or not at all — that is the honest cost of the rule.
