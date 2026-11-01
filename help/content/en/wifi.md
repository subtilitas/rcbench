---
screen: WIFI
title: Wi-Fi
order: 51
---

# Wi-Fi

WI-FI runs the panel's Wi-Fi for two jobs in a browser: every file on the SD (Secure Digital) card to download, and to change as [Browser writes](#browser-writes) allows, and a panel firmware image to upload. It opens from its tile on [SETUP](setup.md), from the WI-FI button of [LOGS](logs.md) and from the button of [Updates](updates.md). Its home tag returns to the one that opened it. Over the network list or a keyboard it climbs one level first, as CANCEL does: from a keyboard to the list, keeping nothing typed, and from the list to the buttons. Over the question about an uploaded image it is CANCEL: the image is dropped and the screen stays; a second tap while the image is still there leaves. The radio runs only while this screen is open: the home tag that leaves it switches the radio off. It is off at every start of the panel.

## The start screen

![No home network kept](view:wifi)

| Button | What it does |
|---|---|
| ACCESS POINT | starts the panel's own network |
| JOIN HOME NETWORK | joins the network kept; grey while none is kept |
| CHOOSE HOME NETWORK | scans and lists the networks in reach |
| FORGET HOME NETWORK | erases the kept network and sets the access code back to on; grey while none is kept |
| ACCESS CODE: ON, OFF | whether the browser page asks for the access code |
| HOTSPOT PASSWORD: RANDOM, KEPT | opens the [hotspot password](#hotspot-password-and-web-code) |
| WEB CODE: RANDOM, KEPT | opens the [web code](#hotspot-password-and-web-code) |

## When the radio may not start

![The radio may not start, after it went off on a switch to ARMED](view:wifi-blocked)

The radio starts only with the bench SAFE, the supply output off and no log run open. Otherwise the buttons that start it are grey and the card says what is missing. On the rbmod carrier the programming ports' 5V, GPIO 21, counts as a supply output as well: while a programmer holds a port, and until SUPPLY reads it `OFF`.

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

## HOTSPOT PASSWORD and WEB CODE

![HOTSPOT PASSWORD kept, WEB CODE random](view:wifi-kept-buttons)
![The view of a random value](view:wifi-kept-pass)
![The view of a kept value](view:wifi-kept-pass-set)
![The access code's view](view:wifi-kept-code)

HOTSPOT PASSWORD is the password of the network ACCESS POINT starts. WEB CODE is the access code the browser page asks for. Each is RANDOM, new at every start of the radio, or a value you type, kept across restarts.

- TYPE A VALUE opens the keyboard: 8 to 63 characters, letters, digits, space and marks.
- BACK TO RANDOM takes one tap and no typing. It is grey while the value is RANDOM.
- A change takes effect at the next start of the radio.
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

`No WPA2 or WPA3 network found.` means the scan completed and found no network to list. A failed scan runs again, 3 scans at most within the 10s. `Wi-Fi could not start.` in the list's place means no scan completed: SCAN AGAIN tries again.

## The keyboard

![The network's name](view:wifi-key-ssid)

![Lower case](view:wifi-key-lower)
![ABC: upper case](view:wifi-key-upper)
![#+=: marks](view:wifi-key-marks)

The keyboard has three layers: lower case, `ABC` for upper case and `#+=` for marks. Each of the 95 printable ASCII (American Standard Code for Information Interchange) characters is on one layer. SPACE is the wide key. A name takes 1 to 32 bytes. A password takes 8 to 63 characters.

![The password, hidden](view:wifi-key-pass)
![SHOW](view:wifi-key-shown)
![Fewer than 8 characters](view:wifi-key-short)

The password shows as dots until SHOW; HIDE hides it again. OK under 8 characters says so, and a 64th character is refused. OK keeps the network: its name and password go to NVS (non-volatile storage).

## Joining the home network

![A home network is kept](view:wifi-home)

![Joining](view:wifi-joining)
![Joined](view:wifi-joined)

JOIN HOME NETWORK joins the kept network. Joined, the screen shows the network, the panel's address and its name, `http://rcbench-3F7A.local/`. The name answers on computers that resolve `.local` names; which phones do is not measured. The address works on every device.

| Line | Condition |
|---|---|
| `Could not connect. Password rejected.` | the network refused the password 3 times in a row, or refused it last when 20s ran out |
| `Could not connect. Network not found.` | no association within 20s, and the last drop of another kind, or none |
| `Connection failed. No IP address within 15s.` | associated, and no address within 15s |
| `Connection to the home network lost.` | joined, then the network dropped the panel |

A failed join offers ACCESS POINT. WI-FI OFF switches the radio off.

## The access code

The browser page asks for a code before it lists a file, changes the card or takes an upload. The screen shows the code. With WEB CODE at RANDOM it has 8 characters and is new at every start of the radio, so a code from an earlier session opens nothing. With WEB CODE kept that does not hold: the code stays valid until you change it, for a browser that kept it and for anyone who saw it. Over plain HTTP on a home network it crosses the network unencrypted. 16 wrong codes switch the radio off, for a kept code as well. ACCESS CODE switches the code off for both ways in; the screen then reads `Access code off. Any device on the network can open the page.` An upload still installs only after the 2s hold on the panel.

## Browser writes

![Browser writes HOLD, before the hold](view:wifi-writes-hold)

SETUP > SD CARD > Browser writes decides what the page needs to change the card:

| Value | A write needs |
|---|---|
| OPEN (default) | nothing more than the page: the access code while ACCESS CODE is on |
| ACCESS CODE | the access code, also with ACCESS CODE off; reads then need none, and the page asks for the code at the first write |
| HOLD | a 2s hold on `HOLD 2s: ALLOW BROWSER WRITES` beside WI-FI OFF, once a radio session |
| OFF | no write is taken |

A line under the rows says which holds, for example `The browser may write to the card: 3 writes so far.` After the hold the button reads `BROWSER WRITES OFF`; a tap ends the writes. Writes end too when the radio goes off, and the next radio session starts without them.

![After the hold, three writes made](view:wifi-writes-allowed)

Each write is checked as FILES checks its own: the bench SAFE, no run logged, the bench's folders, the servo programmers' records and the files the logger writes are not touched, and a name follows the panel's keyboard. A switch to ARMED stops a write at its next 16KiB and drops its `.NEW` file. While FILES on the panel works on the card, a write waits up to 2s; then the page tries again after 2s, 3 times at most.

## The browser page

The page uses the panel's language.

- **Panel firmware**: the version installed, a file picker and Upload. The file is the release asset `rcbench-panel-p4-vX.Y.Z.bin`. A file larger than 8,126,464 bytes is refused before a byte is sent.
- **Files on the card**: the card in two panes, as [FILES](files.md) shows it: the root on the left, the User log folder on the right, 48 entries a page with PREV and NEXT. A tap selects a row; RESCAN, INFO, DOWNLOAD and OPEN act on it, and a dimmed button says why when tapped. OPEN enters a folder or shows a text file read-only, coloured as on the panel, with `LINE n OF m` counted as the panel's viewer counts; a file over 1MiB, one the logger writes and the servo programmers' records only download. INFO gives the time in the zone of SETUP > PANEL's Time zone, `not known` for a 1980 date. The page follows the browser's light or dark scheme in the panel's theme colours. A second row holds UPLOAD, NEW FOLDER, NEW TEXT FILE, RENAME, MOVE and DELETE, dimmed with the reason while [Browser writes](#browser-writes) or the bench refuses them. UPLOAD sends one text file of up to 1MiB into the active pane's folder with a progress bar, and asks before it replaces a file; a file changed on the card while the question is open is not replaced. MOVE moves the selected entry into the other pane's folder. DELETE asks first and takes a run's report along; a folder moves or goes only when it is empty. A name holds ASCII (American Standard Code for Information Interchange) only: a file with a German letter in its path is renamed, moved and deleted on FILES. A file the page opened and changed on the card since is refused with `Changed on the card since it was read. Read it again.` EDIT in the viewer opens the text in the browser's editor, coloured as in the viewer; SAVE writes it back with its own line ends and BOM (byte order mark), and only over the file as the page read it: a change made on the panel or elsewhere since asks STAY, READ AGAIN or OVERWRITE. OVERWRITE takes a press of 2s and saves over the file that the refused SAVE found; a further change during that time is refused the same way, and a file that is gone cannot be overwritten. A KST file loses its checksum line on SAVE, as on the panel, and an ESC profile says it is read at the next start. A file with mixed line ends is saved with LF ends; a file that is not UTF-8 is edited on the panel only. With Browser writes ACCESS CODE, SAVE asks for the code in place and keeps the text. CLOSE with unsaved changes asks first. The page needs JavaScript.
- **Download all as one ZIP file**: the whole card, stored uncompressed. A file that grows by appending is sent as it was when the download started; a file added during it is left out. Removing or renaming a file during the download ends it as failed; the rotation of `BENCH/RCBENCH.LOG` at 1MiB is a rename. On a home network a 16MiB file came down at 688-757kB/s and a 2.56MB image went up at 556-599kB/s. The page itself, 18.9kB with its old card listing, took 4.54s; the page with the two panes and the editor is about 60kB, its time not measured.

The page answers one request at a time: a request made during a download waits for its end. A download the browser takes nothing of for 30s ends, and the browser reports it as failed.

## Installing an image

![A newer image waits](view:wifi-question)

An image the page received waits on the panel as a question with both versions. INSTALL is a 2s hold, `HOLD 2s TO INSTALL`; the browser cannot start it. CANCEL drops the image. The install needs the bench SAFE, the supply output off, no log run open, no board drawing being fetched or kept and no IOmcu update running.

![An older image, and a drawing being kept](view:wifi-older)

An older version is allowed, and the question says it is older.

![The same version, another build](view:wifi-rebuild)
![The build that runs](view:wifi-installed)

The same version from another build is offered as a reinstall. The build that runs already is not offered. The hold opens [PANEL UPDATE](update.md).

## Current limitations

- NVS is not encrypted. The home network's password, a kept HOTSPOT PASSWORD and a kept WEB CODE are kept in plain form in the panel's flash.
- The page is plain HTTP (hypertext transfer protocol). On a home network the access code and every file cross the network unencrypted. The screen says so while joined.
- A device that reads that traffic can send a write of its own while writes are allowed.
- The browser's editor saves a file with mixed line ends with LF ends.
- An uploaded image is checked against its own SHA-256 hash, not against a signature.
- The panel stays joined only while this screen is open.
