---
screen: UPDATES
title: Updates
order: 64
---

# Updates

UPDATES asks GitHub which panel firmware release is the newest and installs it. It opens from the UPDATES key on [Setup](setup.md). A [reminder at start](upd_remind.md) asks the same question once per start of the panel. 0.22 is the last release for the ESP32-S3 panel; the ESP32-P4 panel is offered every newer release, see [The release line](#the-release-line).

![UPDATES](view:updates)

| Control | What it does |
|---|---|
| CHECK FOR UPDATES | asks GitHub once; the same key reads CANCEL while the check runs |
| REMINDER AT START: ON / OFF | switches the check at start and its reminder; kept with SAVE on [Setup](setup.md#keeping-the-values); on by default |
| RELEASES: STABLE / TESTING | sets which releases a check reads, see [The check](#the-check); kept with SAVE on [Setup](setup.md#keeping-the-values); STABLE by default; a switch hides the last result, and a release found on the other setting is not offered |
| HOLD 2s TO DOWNLOAD & INSTALL | shown once a check found a newer release; downloads, checks and installs it |
| HOLD 2s TO UPDATE THE IOMCU | starts the [IOmcu update](coproc_update.md) from the image this panel carries, also after its question was declined |

## What the check needs

The card names the running version and the kept home network. The check and the download need:

- a home network kept on [Wi-Fi](wifi.md#choosing-a-home-network). Without one the card says `No home network saved.` and CHECK FOR UPDATES is grey;
- the bench SAFE, the supply output off and no log run open. Otherwise the key is grey and the card says `Wi-Fi requires SAFE, the supply output off,`;
- no IOmcu update running and no panel install under way.

The download also needs no board drawing being fetched or kept.

## The check

![A check under way](view:updates-checking)

An odd minor number is a testing release, published on GitHub as a pre-release (0.21.x). An even minor number is a stable release (0.22.x).

The panel joins the home network, sends one HTTPS (HTTP over Transport Layer Security) request and reads the newest release's tag from the answer. Then the radio goes off. The join, the address and the request take 30s at most, together. The card shows the step and the seconds left.

| RELEASES | Request | What is read |
|---|---|---|
| STABLE | `GET https://github.com/subtilitas/rcbench/releases/latest` | the tag from the `Location` header of the answer; GitHub's latest release is never a pre-release; when it is past the release line and newer than the running build, the TESTING request follows once |
| TESTING | `GET https://api.github.com/repos/subtilitas/rcbench/releases?per_page=10` | `tag_name` of each of the 10 newest releases by date, testing or stable, from the JSON (JavaScript Object Notation) answer; the highest version among those that are no draft is taken, whatever their order; read in pieces of 4096 bytes, 262,144 bytes at most, and not stored; one release out of form refuses the whole answer |

On both settings a release is offered only when it is of the panel's release line and newer than the running build. The ESP32-P4 panel's line holds every release, see [The release line](#the-release-line).

![A newer release](view:updates-newer)
![Up to date](view:updates-current)
![A testing release found with TESTING](view:updates-testing)

| Card | Meaning |
|---|---|
| `Version 0.22.1 is available.` | the newest release is newer than the running build; the hold appears |
| `Up to date. Panel version: 0.22.0.` | the newest release is the running version or older |
| `No newer release for the ESP32-S3 panel. Panel version: 0.22.0.` | a panel held to the 0.22 line only: the newest release is newer than the running build and past the release line; it is not offered, and the reminder at start does not open for it |
| `Could not connect. Password rejected.`, `Could not connect. Network not found.`, `Connection failed. No IP address within 15s.`, `Connection to the home network lost.` | the join failed, as on Files and update |
| `No response within 30s.` | the 30s ran out |
| `No connection to GitHub.` | the name lookup, the connection or the certificate check failed |
| `GitHub rejected the request: HTTP 404.` | an answer other than a redirect, or with TESTING other than 200 |
| `GitHub's request limit is reached: HTTP 403.` | TESTING: GitHub answered 403 or 429; it takes 60 API requests an hour per address without an account |
| `The panel's own version is not MAJOR.MINOR.PATCH: no update offered.` | the running build's version does not parse; nothing is offered |
| `Unexpected answer from GitHub.` | the `Location`, or with TESTING the answer, is missing or out of form; a second line says which |
| `Wi-Fi switched off: bench ARMED.` and the like | ARMED, the supply output, a log run or STOP ended the check |
| `Stopped: ...` | WI-FI, the IOmcu's update or CANCEL ended the check; `Stopped: the WI-FI screen is using Wi-Fi.` clears once WI-FI's session has ended and the radio is off |

![A check that found no answer](view:updates-failed)

The tag reads `v` and MAJOR.MINOR.PATCH, digits only, each number from 0 to 65535 in at most 5 digits and without a leading zero. A release is newer when its MAJOR is higher, or MAJOR is equal and MINOR is higher, or both are equal and PATCH is higher.

## The release line

0.22 is the last release for the ESP32-S3 panel. Later releases are for the 7-inch ESP32-P4 display board from Waveshare.

The ESP32-P4 panel's line holds every release: a check offers any release newer than the running build, on both settings.

| GitHub names | ESP32-P4 panel on 0.23.0 |
|---|---|
| `v0.24.0`, `v1.0.0` | offered |
| `v0.23.0` | up to date |
| `v0.22.1` | up to date; an older release is never offered |

The ESP32-S3 panel's line, 0.22, offers 0.22.x only, and its card's foot says so in its first lines:

| GitHub names | Panel on 0.22.0 |
|---|---|
| `v0.22.1` | offered |
| `v0.22.0` | up to date; an older release is never offered |
| `v0.23.0`, `v0.24.0`, `v1.0.0` | `No newer release for the ESP32-S3 panel.` |
| a tag out of form, such as `v0.22.1-rc1` | `Unexpected answer from GitHub.` |

With TESTING, when the highest of the 10 newest releases is past the line, the highest release of the line among them is offered in its place once it is newer than the running build. With STABLE, when GitHub's latest release is past the line and newer than the running build, the panel sends the same request once more and offers the newest 0.22.x among those 10 when it is newer. When that request fails, the card reads as without it.

## The download

The 2s hold is the one confirmation: no question follows. The panel joins the home network within 30s and fetches `SHA256SUMS` and `rcbench-panel-p4-vX.Y.Z.bin` of the release over HTTPS. It follows at most 3 redirects, only to `github.com`, `release-assets.githubusercontent.com` and `objects.githubusercontent.com`.

1. `SHA256SUMS` is 4096 bytes at most and holds exactly one line for the image.
2. The image announces its size first; an image without a size, of 0 bytes or over 8,126,464 bytes is refused. It is kept in PSRAM (pseudo-static random-access memory).
3. The image's SHA-256 (Secure Hash Algorithm, 256 bits) equals its line in `SHA256SUMS`.
4. Every check of an uploaded image runs, and the image names the release's version.

Only then the [panel update](update.md) writes it, as INSTALL does. Nothing touches the flash before.

![The download](view:panel-download)

CANCEL ends the download. STOP, ARMED, the supply output, a log run, a board drawing being fetched and 180s from the hold end it too. The running build stays; CLOSE goes back to UPDATES, or to the menu when the hold was the reminder's.

![A download that stopped](view:panel-download-failed)

## The IOmcu

![Another build on the IOmcu](view:updates-coproc)

Under the buttons, UPDATES names the version the IOmcu runs (`Running: 0.21.2`) and the version of the image this panel carries (`Carried: 0.22.0`). HOLD 2s TO UPDATE THE IOMCU starts the update the link-up's question starts, with the same progress screen. When the update ends, or the START was spent, the screen returns to UPDATES.

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
| `No IOmcu image for this board in this panel build.` | the IOmcu reports a board this panel build carries no image for; no update is offered |
| `IOmcu runs the build this panel carries.` | nothing to send |
| `The update requires SAFE and the supply output off.` | ARMED, an arm waiting, or the supply output on |
| `Waiting for the panel's Wi-Fi to switch off.` | a check, a download or [Wi-Fi](wifi.md) has the radio on |
| `Waiting: panel firmware installation is running.` | a panel install is under way |

With the hold live the line reads `Outputs stay off. ARMED is blocked until the update ends.`, or `The firmware on this panel is older.` in the warning colour for an older image.

## Current limitations

- How long the check and the download take on a real network is not measured.
- A first DNS (Domain Name System) server of the home network that does not answer delays each name lookup by about 7s after a join, out of the check's 30s: lwIP asks that server 4 times, up to 7s, before the next.
- TESTING: GitHub answers 60 API requests an hour for each public address without an account, shared by every device behind it. The panel sends no retry.
- TESTING reads the 10 newest releases by date. A release older by date than those is not seen, however high its version.
- The release list holds the 10 newest releases by date (`per_page=10`). A 0.22.x patch release with 10 or more releases published after it is not found, on either setting.
- No certificate's dates are checked: mbedTLS is built without its expiry check (`CONFIG_MBEDTLS_HAVE_TIME_DATE` off, ESP-IDF's default).
- A release whose assets are not named `rcbench-panel-p4-vX.Y.Z.bin` and `SHA256SUMS` cannot be downloaded.
