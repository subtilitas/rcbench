---
screen: COPROC_UPDATE
title: IOmcu update
order: 100
---

# IOmcu update

Each panel firmware image includes an IOmcu image. The panel offers it whenever the connected IOmcu runs a different build. It checks at every link-up over the CAN (Controller Area Network) bus, comparing SHA-256 (Secure Hash Algorithm, 256 bits) digests rather than version numbers.

## The offer

![UPDATES opened by an offer](view:updates-offer)

An offer opens [Updates](updates.md) with step 2, IOMCU, as the current step, once per offer. It opens on the [menu](overview.md) when the bench is SAFE, the supply output and the Wi-Fi radio are off, and no panel update is running. The band says `IOmcu runs a different build. Update it on UPDATES.`. While one of these conditions holds it back, the band names it instead, such as `IOmcu runs a different build. UPDATES opens once the panel's Wi-Fi is off.`, and again when the opening waits for something else. While an update run after a restart waits to open UPDATES, the band shows the run's line only, and UPDATES opens once for both.

The step's card shows the IOmcu's running version and the version included with the panel. An older image is offered too: the card then reads `The firmware on this panel is older.` in the warning colour.

| Control | What it does |
|---|---|
| HOLD 2s TO UPDATE THE IOMCU | starts the update after a 2s hold, with the gesture and fade of ARM |
| SKIP THIS STEP | declines the offer; the panel does not offer that version again until it restarts, and [Single updates](updates.md#the-iomcu) still starts the update |

The home tag that leaves UPDATES declines the offer as SKIP THIS STEP does. Over OPTIONS, SINGLE UPDATES or the devices' page it closes that page first and declines nothing. A screen the panel opens over UPDATES does not decline: the [fault screen](busfault.md) after a lost link and a panel-update download. The next link-up offers the update again, and UPDATES opens on the menu when the same conditions are met. A new offer brings back a skipped step 2 and ends a hold under way on it.

## The progress

![Sending firmware.](view:coproc-update-progress)

After the hold completes, the update takes exclusive control of the panel. The link goes down and the outputs are off. The IOmcu refuses ARM until the update ends. The progress display occupies the whole screen, with no status band or STOP control.

| Step | Shown while |
|---|---|
| `Preparing firmware.` | the panel hashes the image, 8KiB a step |
| `Sending firmware.` | the blocks go out, 64 bytes each, counted as `Block 1497 of 2994, 50%` |
| `IOmcu is checking the firmware.` | the IOmcu hashes the slot it wrote |
| `IOmcu is starting the new firmware.` | it restarts into the new slot, on trial |
| `Keeping the new firmware.` | the panel reads the new image's identity and keeps it |

The transfer time is not measured.

## How it ended

When the update ends, the panel returns to [Updates](updates.md). The alert band reports the result, and step 2's card keeps a failure's line until the next attempt or the next start:

| Alert | Meaning |
|---|---|
| `IOmcu updated to 0.22.0.` | the new image runs and is kept |
| `IOmcu update stopped: no response. Previous firmware is running.` | nothing answered for 4s |
| `IOmcu update stopped: refused, error 6. Previous firmware is running.` | the IOmcu refused a step |
| `IOmcu update stopped: firmware check failed. Previous firmware is running.` | the slot's SHA-256 is not the image's |
| `IOmcu update stopped: flash memory stayed busy. Previous firmware is running.` | the flash stayed busy for 10s |
| `IOmcu update stopped: IOmcu restarted 3 times. Previous firmware is running.` | three starts from block 0 |
| `IOmcu update stopped: new firmware did not start. Previous firmware is running.` | no identity from the new image within 12s |
| `IOmcu cannot update over CAN. Install firmware once via USB.` | the IOmcu has no update pages or no partition table; flash it once over USB (Universal Serial Bus) |
| `New IOmcu firmware kept.` | an image found on trial at link-up was kept |

A power cut during the update leaves the IOmcu on its old image, and the next link-up offers the update again.

## Related pages

- [Panel update](update.md): installing a panel image over Wi-Fi.
- [Bringing up the link](https://github.com/subtilitas/rcbench/blob/main/docs/Link.md): the transfer, the flash layout and the error numbers.
