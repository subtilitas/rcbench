---
screen: UPDATE
title: Panel update
order: 52
---

# Panel update

PANEL UPDATE writes a new panel firmware image into the panel's flash. It opens after the 2s hold on INSTALL on [Wi-Fi](wifi.md), or when a download from [Updates](updates.md) has passed its checks, and it takes the whole screen: no band and no STOP. Nothing can be switched to ARMED behind it.

## Writing

![Writing the image](view:panel-update)

The hold switches the radio off first. The panel then stops its polls and withholds the heartbeat, so the IOmcu holds its outputs in failsafe, as at any restart of the panel. The screen writes the slot that is not running, in pieces of 64KiB, and shows the bytes written and the percentage. What the display shows during each flash erase is not measured. Do not switch off the panel.

How long the erase and the write take is not measured.

## Written and checked

![Written and checked: the panel restarts](view:panel-update-done)

When the write completes, the slot is read back and checked, it becomes the slot that starts, and the panel restarts. The splash shows the new version, and the band says `Panel firmware 0.22.1 installed.`.

The first start of a new build is a trial. Without touch the panel goes back to the build before it at once: no touch, no STOP. With the display and touch up, the build is kept once the panel works as a whole: the card tried (no card counts), the settings loaded, the control and logger tasks running, and a whole frame on the display. The IOmcu is not waited for. 30s after the start the build is kept once the screen runs, whatever else is missing. Keeping waits while the bench is ARMED or the supply output is on. A start that resets before the build is kept, by a fault, a watchdog, a restart or a power cut, goes back to the build before it, and the band says `Panel firmware 0.22.1 failed to start. Version 0.22.0 is running again.`. When keeping a good start fails, the band says `Panel firmware 0.22.1 was not kept. The next restart restores the previous version.`. The install keeps the version it wrote in NVS (non-volatile storage) before the restart, so the `failed to start` line survives a restart, a fault and a power cut during the trial, and shows once.

## The write failed

![The write failed](view:panel-update-failed)

A write that fails changes nothing that starts: the running firmware stays. CLOSE goes back after an upload to the screen that opened [Wi-Fi](wifi.md), [SETUP](setup.md), [LOGS](logs.md) or [Updates](updates.md), and to [Updates](updates.md) or the menu after a download.

## Current limitations

- An install that cannot keep its note in NVS (non-volatile storage) says `restart note not kept` on the console, and a start of its build that goes back shows no line on the band.
- The first start after an install from 0.23.2 or older finds no note in NVS; those builds keep it in LP (low-power) RAM.
- A trial start that stops on the splash, in a card mount that does not return for example, is not kept and does not restart by itself. A restart or a power cut goes back to the build before.
- Before the first update over Wi-Fi the panel needs one flash over USB (Universal Serial Bus) to reach the layout with two application slots: [First run](https://github.com/subtilitas/rcbench/blob/main/docs/FirstRun.md).
