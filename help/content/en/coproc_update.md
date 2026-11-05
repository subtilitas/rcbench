---
screen: COPROC_UPDATE
title: IOmcu update
order: 100
---

# IOmcu update

Each panel firmware image includes an IOmcu image. The panel offers it whenever the connected IOmcu runs a different build. It checks at every link-up over the CAN (Controller Area Network) bus, comparing SHA-256 (Secure Hash Algorithm, 256 bits) digests rather than version numbers. A mismatch raises the alert `IOmcu runs a different build. Update it on UPDATES.`.

## The question

![The question on the menu](view:coproc-update)

The update question opens on the [menu](overview.md), with UPDATE in the band, when the bench is SAFE, the supply output and Wi-Fi radio are off, and no panel update is running. It shows the IOmcu's running version and the version included with the panel.

| Control | What it does |
|---|---|
| HOLD 2s TO START | starts the update after a 2s hold, with the gesture and fade of ARM |
| CANCEL | closes the question; the panel does not ask again for that version until it restarts, and [Updates](updates.md#the-iomcu) still starts the update |

The home tag declines the update as CANCEL does. An interruption by another screen does not decline it: this applies to the [fault screen](busfault.md) after a lost link and to a panel-update download. The next link-up offers the update again, and the question opens on the menu when the same conditions are met.

![An older image](view:coproc-update-older)

An older image is offered too. The question then reads `The firmware on this panel is older.` in the warning colour.

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

When the update ends, the panel returns to the menu, or to [Updates](updates.md#the-iomcu) if it was started there. The alert band reports the result:

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
