---
screen: UPDATE
title: Panel update
order: 52
---

# Panel update

PANEL UPDATE writes a new panel firmware image into the panel's flash. It opens after the 2 s hold on INSTALL on [Wi-Fi](wifi.md), or when a download from [Updates](updates.md) has passed its checks, and it takes the whole screen: no band and no STOP. Nothing can be switched to ARMED behind it.

## Writing

![Writing the image](view:panel-update)

The hold switches the radio off first. The panel then stops its polls and withholds the heartbeat, so the IOmcu holds its outputs in failsafe, as at any restart of the panel. The screen writes the slot that is not running, in pieces of 64 KiB, and shows the bytes written and the percentage. The display may flicker while flash memory is erased. Do not switch off the panel.

How long the erase and the write take is not measured.

## Written and checked

![Written and checked: the panel restarts](view:panel-update-done)

When the write completes, the slot is read back and checked, it becomes the slot that starts, and the panel restarts. The splash shows the new version, and the band says `Panel firmware 0.19.0 installed.`.

The first start of a new build is a trial. The build is kept once the display and touch are up. A start that does not get there goes back to the build before it, and the band says `Panel firmware 0.19.0 failed to start. Version 0.18.0 is running again.`. When keeping a good start fails, the band says `Panel firmware 0.19.0 was not kept. The next restart restores the previous version.`.

## The write failed

![The write failed](view:panel-update-failed)

A write that fails changes nothing that starts: the running firmware stays. CLOSE goes back after an upload to the screen that opened [Wi-Fi](wifi.md), [SETUP](setup.md) or [LOGS](logs.md), and to [Updates](updates.md) or the menu after a download.

## Current limitations

- A power cut during the trial start loses the band's line; the panel still goes back to the build before.
- Before the first update over Wi-Fi the panel needs one flash over USB (Universal Serial Bus) to reach the layout with two application slots: [First run](https://github.com/subtilitas/rcbench/blob/main/docs/FirstRun.md).
