---
screen: WIFI
title: Wi-Fi
order: 51
---

# Wi-Fi

WI-FI provides browser access to the panel. You can download SD (Secure Digital) card files, edit them as [Browser writes](#browser-writes) permits, and upload panel firmware. Open it from [SETUP](setup.md), [LOGS](logs.md) or [Updates](updates.md). The home tag returns to the page that opened it.

Within a network list or keyboard, home acts like CANCEL: it discards keyboard input and returns to the list, then returns from the list to the main buttons. In an uploaded-image question, it discards the image but stays on WI-FI. A second tap while the image is still present leaves the screen. Leaving switches the radio off. The radio is also off at every panel start.

## The start screen

![No home network kept](view:wifi)

| Button | What it does |
|---|---|
| ACCESS POINT | starts the panel's own network |
| JOIN HOME NETWORK | joins the network kept; grey while none is kept |
| CHOOSE HOME NETWORK | scans and lists the networks in reach |
| FORGET HOME NETWORK | erases the kept network and sets the access code back to on; grey while none is kept |
| ACCESS CODE: ON, OFF | whether the browser page asks for the access code |
| HOTSPOT PASSWORD: RANDOM, KEPT | opens the [hotspot password](#hotspot-password-and-access-code) |
| ACCESS CODE: RANDOM, KEPT | opens the [access code](#hotspot-password-and-access-code) |

## When the radio may not start

![The radio may not start, after it went off on a switch to ARMED](view:wifi-blocked)

The radio requires a SAFE bench, supply output off and no open log run. Otherwise its start buttons are grey and the card explains why. On the rbmod carrier, the programming ports' 5V rail at GPIO 21 also counts as a supply output: it blocks Wi-Fi while a programmer holds a port and until SUPPLY reports it `OFF`.

The radio goes off on its own, and the card names the reason in one line:

| Line | Cause |
|---|---|
| `Wi-Fi switched off: bench ARMED.` | a switch to ARMED |
| `Wi-Fi switched off: STOP.` | STOP |
| `Wi-Fi switched off: supply output on.` | the supply output came on |
| `Wi-Fi switched off: a recording session started.` | a run started recording |
| `Wi-Fi switched off after 10min without a request.` | no request for 10min |
| `Wi-Fi switched off after 16 wrong access codes.` | 16 wrong codes on the page |
| `Wi-Fi could not start.` | the radio failed to start |

Leaving the screen switches the radio off as well. The link to the IOmcu and the heartbeat run as on every other screen.

## ACCESS POINT

![The access point is up](view:wifi-ap)

ACCESS POINT starts the panel's own network. The screen shows what a phone or a computer needs.

| Item | Value |
|---|---|
| Band and channel | 2.4GHz, channel 6 |
| Security | WPA2 (Wi-Fi Protected Access 2) personal |
| Network name | `rcbench-` and 4 hex digits of the panel's MAC (media access control) address, for example `rcbench-3F7A` |
| Password | RANDOM: 8 characters, new at every start, without 0, 1, i, l and o. Kept: the 8 to 63 characters of HOTSPOT PASSWORD |
| Clients | 1 at a time |
| IP address | `http://192.168.4.1/` |
| Range | not measured |

1. On the phone or computer, join the network the screen names, with the password it shows.
2. Open `http://192.168.4.1/`.
3. Enter the access code the screen shows.

## HOTSPOT PASSWORD and ACCESS CODE

![HOTSPOT PASSWORD kept, ACCESS CODE random](view:wifi-kept-buttons)
![The view of a random value](view:wifi-kept-pass)
![The view of a kept value](view:wifi-kept-pass-set)
![The access code's view](view:wifi-kept-code)

HOTSPOT PASSWORD secures the panel's ACCESS POINT network. ACCESS CODE controls entry to the browser page. Each can be RANDOM, regenerated at every radio start, or a value you enter and retain across restarts.

- TYPE A VALUE opens the keyboard: 8 to 63 characters, letters, digits, space and marks.
- BACK TO RANDOM takes one tap and no typing. It is grey while the value is RANDOM.
- A change takes effect the next time Wi-Fi starts.
- Each view shows its value, so a kept value can be read and reset on the panel without a browser. FORGET HOME NETWORK keeps both.
- The panel takes no screenshot while it shows a kept value.

![The keyboard of a kept value](view:wifi-key-kept)
![Fewer than 8 characters](view:wifi-key-kept-short)

## Choosing a home network

![Scanning](view:wifi-scanning)
![The networks found](view:wifi-scan)

The panel can join a WPA2 or WPA3 personal network instead. CHOOSE HOME NETWORK scans for 10s at most, then lists up to 16 networks, strongest first, with their signal in dBm. Networks without a name, open networks and enterprise networks are left off.

- A tap on a network opens the keyboard for its password.
- HIDDEN NETWORK asks for the name of a network that does not announce itself, then for its password.
- SCAN AGAIN scans again. CANCEL goes back.

`No WPA2 or WPA3 network found.` means a scan completed without a network eligible for the list. Failed scans are retried, with at most 3 attempts within 10s. `Wi-Fi could not start.` in place of the list means no scan completed. Use SCAN AGAIN to retry.

## The keyboard

![The network's name](view:wifi-key-ssid)

![Lower case](view:wifi-key-lower)
![ABC: upper case](view:wifi-key-upper)
![#+=: marks](view:wifi-key-marks)

The keyboard has three layers: lower case, `ABC` for upper case and `#+=` for marks. Each of the 95 printable ASCII (American Standard Code for Information Interchange) characters is on one layer. SPACE is the wide key. A name takes 1 to 32 bytes. A password takes 8 to 63 characters.

![The password, hidden](view:wifi-key-pass)
![SHOW](view:wifi-key-shown)
![Fewer than 8 characters](view:wifi-key-short)

Passwords are hidden as dots. SHOW reveals them; HIDE conceals them again. OK reports passwords shorter than 8 characters, and a 64th character cannot be entered. A valid OK saves the network name and password in NVS (non-volatile storage).

## Joining the home network

![A home network is kept](view:wifi-home)

![Joining](view:wifi-joining)
![Joined](view:wifi-joined)

JOIN HOME NETWORK connects to the saved network. Once connected, the screen shows the network, panel IP address and name, such as `http://rcbench-3F7A.local/`. The name works on computers that resolve `.local`; phone support has not been measured. Use the IP address to avoid name resolution.

| Line | Condition |
|---|---|
| `Could not connect. Password rejected.` | the network refused the password 3 times in a row, or refused it last when 20s ran out |
| `Could not connect. Network not found.` | no association within 20s, and the last drop of another kind, or none |
| `Connection failed. No IP address within 15s.` | associated, and no address within 15s |
| `Connection to the home network lost.` | joined, then the network dropped the panel |

A failed join offers ACCESS POINT. WI-FI OFF switches the radio off.

## The access code

The browser requires the access code before listing files, changing the card or accepting an upload. Read the code on the panel. RANDOM generates 8 new characters at every radio start, invalidating previous session codes. A kept code remains valid until you change it, including for browsers that saved it and anyone who has seen it. On a home network, plain HTTP sends it unencrypted.

After 16 wrong codes, the radio switches off, including when a kept code is used. Each wrong entry counts. A wrong code stored in a browser cookie counts only once, so a tab left open with an old code does not exhaust the allowance.

ACCESS CODE can disable this check in both access-point and home-network modes. The screen then shows `Access code off. Any device on the network can open the page.` Uploaded firmware still requires a 2s hold on the panel before installation.

## Browser writes

![Browser writes HOLD, before the hold](view:wifi-writes-hold)

SETUP > SD CARD > Browser writes decides what the page needs to change the card:

| Value | A write needs |
|---|---|
| OPEN (default) | nothing more than the page: the access code while ACCESS CODE is on |
| ACCESS CODE | the access code, also with ACCESS CODE off; reads then need none, and the page asks for the code at the first write |
| HOLD | a 2s hold on `HOLD 2s: ALLOW BROWSER WRITES` beside WI-FI OFF, once a radio session |
| OFF | no write is taken |

The line below the settings reports write access, for example `The browser may write to the card: 3 writes so far.` After a successful hold, the button reads `BROWSER WRITES OFF`; tap it to revoke write access. Switching the radio off also revokes it. Each new radio session begins without the hold grant.

![After the hold, three writes made](view:wifi-writes-allowed)

Browser writes use the same checks as FILES: the bench must be SAFE with no recording in progress, protected bench folders and programmer records cannot be changed, and open log files are protected. Names follow the panel keyboard's rules. Switching to ARMED stops a write before its next 16KiB block and deletes its `.NEW` file. If FILES is using the card, the request waits up to 2s. The page then retries after 2s, at most 3 times.

## The browser page

The page uses the panel's language.

**Panel firmware** shows the installed version, a file picker and Upload. Select the release asset `rcbench-panel-p4-vX.Y.Z.bin`. Files larger than 8,126,464 bytes are refused before transmission.

**Files on the card** uses two panes like [FILES](files.md): root on the left, User log folder on the right, with 48 entries per page and PREV and NEXT. Select a row for RESCAN, INFO, DOWNLOAD or OPEN. Tap a dimmed button to see why it is unavailable. The page follows the browser's light or dark scheme using the panel theme colours.

OPEN enters a folder or displays text read-only, with the panel's highlighting and `LINE n OF m` counting. Files over 1MiB, files being logged and programmer records can only be downloaded. INFO uses the Time zone from SETUP > PANEL and shows `not known` for a 1980 date.

The second button row contains UPLOAD, NEW FOLDER, NEW TEXT FILE, RENAME, MOVE and DELETE. [Browser writes](#browser-writes) or bench restrictions dim them and show the reason. UPLOAD sends one text file of up to 1MiB to the active folder, with a progress bar. It asks before replacing a file and refuses replacement if that file changes while the question is open. MOVE sends the selected entry to the other pane's folder. DELETE asks first and removes a run's report with it. Only empty folders can be moved or deleted.

Names accept ASCII (American Standard Code for Information Interchange) only. Use FILES on the panel to rename, move or delete a path containing German letters. A file changed on the card after the page read it is refused with `Changed on the card since it was read. Read it again.`

EDIT opens the browser text editor with the same highlighting as the viewer. SAVE preserves the file's line endings and BOM (byte order mark), except that mixed line endings become LF. Files outside UTF-8 can be edited only on the panel.

SAVE checks that the card still contains the version the page read. Changes made on the panel or elsewhere prompt STAY, READ AGAIN or OVERWRITE. Hold OVERWRITE for 2s to replace the version found by the refused SAVE. A further change during the hold is refused again. A missing file cannot be overwritten.

Saving a KST file removes its checksum line, as on the panel. ESC profiles show that they will be read at the next start. With Browser writes set to ACCESS CODE, SAVE asks for the code without losing the edited text. CLOSE asks before discarding unsaved changes. The page requires JavaScript.

**Download all as one ZIP file** sends the whole card without compression. Append-only files are sent up to their size at download start; files added later are omitted. Removing or renaming a file during the download makes it fail. Rotation of `BENCH/RCBENCH.LOG` at 1MiB is such a rename.

Measured on one home network, a 16MiB file downloaded at 688-757kB/s and a 2.56MB image uploaded at 556-599kB/s. The 18.9kB page with the earlier card listing loaded in 4.54s. The page with two panes and an editor is about 60kB; its load time has not been measured.

The server handles one request at a time. A request made during a download waits until it finishes. If the browser accepts no download data for 30s, the transfer ends and the browser reports failure.

## Installing an image

![A newer image waits](view:wifi-question)

After upload, the panel shows a confirmation with the running and uploaded versions. Hold `HOLD 2s TO INSTALL` for 2s to install; the browser cannot start installation. CANCEL discards the image. Installation requires SAFE, supply output off, no open log run, no board drawing being fetched or saved, and no IOmcu update in progress.

![An older image, and a drawing being kept](view:wifi-older)

An older version is allowed, and the question says it is older.

![The same version, another build](view:wifi-rebuild)
![The build that runs](view:wifi-installed)

A different build with the same version can be reinstalled. The currently running build is not offered. The hold opens [PANEL UPDATE](update.md).

## Current limitations

- NVS is not encrypted. The home network's password, a kept HOTSPOT PASSWORD and a kept ACCESS CODE are kept in plain form in the panel's flash.
- The page is plain HTTP (hypertext transfer protocol). On a home network the access code and every file cross the network unencrypted. The screen says so while joined.
- A device that reads that traffic can send a write of its own while writes are allowed.
- The browser's editor saves a file with mixed line ends with LF ends.
- An uploaded image is checked against its own SHA-256 hash, not against a signature.
- The panel stays joined only while this screen is open.
