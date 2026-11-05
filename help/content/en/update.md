---
screen: UPDATE
title: Panel update
order: 52
---

# Panel update

PANEL UPDATE writes a new panel firmware image to flash memory. It opens after you hold INSTALL for 2s on [Wi-Fi](wifi.md), or after a download from [Updates](updates.md) passes its checks. It occupies the whole display, with no status band or STOP control. The bench cannot switch to ARMED while this screen is open.

## Writing

![Writing the image](view:panel-update)

The hold first switches off the radio. The panel then stops polling and withholds its heartbeat, keeping the IOmcu outputs in failsafe as at any panel restart. It writes the inactive slot in 64KiB pieces and displays the bytes written and percentage complete. Display behaviour during each flash erase has not been measured. Do not switch off the panel.

How long the erase and the write take is not measured.

## Written and checked

![Written and checked: the panel restarts](view:panel-update-done)

After writing, the panel reads back and checks the slot, selects it for startup and restarts. The splash shows the new version, and the band reports `Panel firmware 0.22.1 installed.`.

The first startup of a new build is a trial. If touch is unavailable, the panel immediately restores the previous build because STOP cannot be operated. With display and touch working, the build is kept once the card has been tried, settings are loaded, the control and logger tasks are running, and a complete frame is displayed. No card counts as a completed card check. The panel does not wait for the IOmcu.

After 30s, the build is kept once the screen is running, even if other components are missing. Keeping the build waits while the bench is ARMED or the supply output is on.

A fault, watchdog reset, restart or power cut before the build is kept restores the previous build. The band then reports `Panel firmware 0.22.1 failed to start. Version 0.22.0 is running again.`. If keeping a working build fails, it reports `Panel firmware 0.22.1 was not kept. The next restart restores the previous version.`. Before restarting, the installer records the installed version in NVS (non-volatile storage). This lets the `failed to start` message survive a restart, fault or power cut during the trial and appear once.

## The write failed

![The write failed](view:panel-update-failed)

A failed write leaves the startup selection unchanged, so the existing firmware continues to run. After an upload, CLOSE returns to the page that opened [Wi-Fi](wifi.md): [SETUP](setup.md), [LOGS](logs.md) or [Updates](updates.md). After a download, it returns to [Updates](updates.md) or the menu.

## Current limitations

- If the installer cannot save its restart note in NVS (non-volatile storage), the console reports `restart note not kept`. A rollback of that build then produces no message in the band.
- The first startup after an installation from 0.23.2 or older finds no note in NVS; those builds store it in LP (low-power) RAM (random-access memory).
- A trial startup that stalls on the splash, for example in a card mount that does not return, is neither kept nor restarted automatically. A restart or power cut restores the previous build.
- Before the first update over Wi-Fi, flash the panel once over USB (Universal Serial Bus) to install the layout with two application slots. See [First run](https://github.com/subtilitas/rcbench/blob/main/docs/FirstRun.md).
