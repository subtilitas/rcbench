---
screen: UPDATES
title: Updates
order: 64
---

# Updates

UPDATES asks GitHub which panel firmware release is the newest and installs it. It opens from the UPDATES key on [Setup](setup.md). A [reminder at start](upd_remind.md) asks the same question once per start of the panel.

![UPDATES](view:updates)

| Control | What it does |
|---|---|
| CHECK FOR UPDATES | asks GitHub once; the same key reads CANCEL while the check runs |
| REMINDER AT START: ON / OFF | switches the check at start and its reminder; kept with SAVE on [Setup](setup.md#keeping-the-values); on by default |
| HOLD 2 s: DOWNLOAD & INSTALL | shown once a check found a newer release; downloads, checks and installs it |
| HOLD 2 s: UPDATE IOMCU | starts the [IOmcu update](coproc_update.md) from the image this panel carries, also after its question was declined |

## What the check needs

The card names the running version and the kept home network. The check and the download need:

- a home network kept on [Wi-Fi](wifi.md#choosing-a-home-network). Without one the card says `No home network saved.` and CHECK FOR UPDATES is grey;
- the bench SAFE, the supply output off and no log run open. Otherwise the key is grey and the card says `Wi-Fi requires SAFE, the supply output off,`;
- no IOmcu update running and no panel install under way.

The download also needs no board drawing being fetched or kept.

## The check

![A check under way](view:updates-checking)

The panel joins the home network, sends one HTTPS (HTTP over Transport Layer Security) request to `https://github.com/subtilitas/rcbench/releases/latest` and reads the newest release's tag from the `Location` header of the answer. Then the radio goes off. The join, the address and the request take 30 s at most, together. The card shows the step and the seconds left.

![A newer release](view:updates-newer)
![Up to date](view:updates-current)

| Card | Meaning |
|---|---|
| `Version 0.20.0 is available.` | the newest release is newer than the running build; the hold appears |
| `Up to date. Panel version: 0.19.0.` | the newest release is the running version or older |
| `Could not connect. Password rejected.`, `Could not connect. Network not found.`, `Connection failed. No IP address within 15 s.`, `Connection to the home network lost.` | the join failed, as on Files and update |
| `No response within 30 s.` | the 30 s ran out |
| `No connection to GitHub.` | the name lookup, the connection or the certificate check failed |
| `GitHub rejected the request: HTTP 404.` | an answer other than a redirect |
| `Unexpected answer from GitHub.` | the `Location` is missing or out of form; a second line says which |
| `Wi-Fi switched off: bench ARMED.` and the like | ARMED, the supply output, a log run or STOP ended the check |
| `Stopped: ...` | WI-FI, the IOmcu's update or CANCEL ended the check |

![A check that found no answer](view:updates-failed)

The tag reads `v` and MAJOR.MINOR.PATCH, digits only, each number from 0 to 65535 in at most 5 digits and without a leading zero. A release is newer when its MAJOR is higher, or MAJOR is equal and MINOR is higher, or both are equal and PATCH is higher.

## The download

The 2 s hold is the one confirmation: no question follows. The panel joins the home network within 30 s and fetches `SHA256SUMS` and `rcbench-panel-vX.Y.Z.bin` of the release over HTTPS. It follows at most 3 redirects, only to `github.com`, `release-assets.githubusercontent.com` and `objects.githubusercontent.com`.

1. `SHA256SUMS` is 4096 bytes at most and holds exactly one line for the image.
2. The image announces its size first; an image without a size, of 0 bytes or over 4,063,232 bytes is refused. It is kept in PSRAM (pseudo-static random-access memory).
3. The image's SHA-256 (Secure Hash Algorithm, 256 bits) equals its line in `SHA256SUMS`.
4. Every check of an uploaded image runs, and the image names the release's version.

Only then the [panel update](update.md) writes it, as INSTALL does. Nothing touches the flash before.

![The download](view:panel-download)

CANCEL ends the download. STOP, ARMED, the supply output, a log run, a board drawing being fetched and 180 s from the hold end it too. The running build stays; CLOSE goes back to UPDATES, or to the menu when the hold was the reminder's.

![A download that stopped](view:panel-download-failed)

## The IOmcu

![Another build on the IOmcu](view:updates-coproc)

Under the buttons, UPDATES names the version the IOmcu runs (`Running: 0.17.0`) and the version of the image this panel carries (`Carried: 0.18.0`). HOLD 2 s: UPDATE IOMCU starts the update the link-up's question starts, with the same progress screen. When the update ends, or the START was spent, the screen returns to UPDATES.

The hold takes what the question takes: an IOmcu that runs another build, the bench SAFE, the supply output off, the Wi-Fi radio wholly off and no panel install under way. Otherwise it is grey and the line above it says why, the first of these that holds:

| Line | Condition |
|---|---|
| `This panel carries no IOmcu firmware.` | the panel was built without the IOmcu's image |
| `Waiting for the IOmcu update to finish.` | an update runs, or its START is not taken yet |
| `No IOmcu is responding.` | no link; `Running: not known` |
| `Waiting for the IOmcu's build check.` | an IOmcu answers; its build was not compared yet |
| `Checking the IOmcu's build.` | the SHA-256 comparison runs |
| `Build check failed. It runs again at the next link-up.` | the comparison ended without an answer |
| `No update over CAN. Install firmware once via USB.` | the IOmcu has no update pages or no partition table |
| `IOmcu runs the build this panel carries.` | nothing to send |
| `The update requires SAFE and the supply output off.` | ARMED, an arm waiting, or the supply output on |
| `Waiting for the panel's Wi-Fi to switch off.` | a check, a download or [Wi-Fi](wifi.md) has the radio on |
| `Waiting: panel firmware installation is running.` | a panel install is under way |

With the hold live the line reads `Outputs stay off. ARMED is blocked until the update ends.`, or `The firmware on this panel is older.` in the warning colour for an older image.

## Current limitations

- How long the check and the download take on a real network is not measured.
- No certificate's dates are checked: the panel has no clock.
- A release whose assets are not named `rcbench-panel-vX.Y.Z.bin` and `SHA256SUMS` cannot be downloaded.
