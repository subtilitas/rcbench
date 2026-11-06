---
screen: UPDATES
title: Updates
order: 64
---

# Updates

UPDATES sucht auf GitHub nach neuerer Panel-Firmware und bietet deren Installation an. Öffne die Seite über [Setup](setup.md). Die [Erinnerung beim Start](upd_remind.md) führt dieselbe Suche einmal je Panelstart aus. Das ESP32-S3-Panel ist auf die Linie 0.22 begrenzt; ESP32-P4 unterstützt neuere Linien. Siehe [Die Release-Linie](#die-release-linie).

![UPDATES](view:updates)

| Bedienelement | Wirkung |
|---|---|
| NACH UPDATES SUCHEN | Fragt GitHub einmal ab. Während der Suche heißt die Taste ABBRECHEN. |
| ERINNERUNG BEIM START: EIN / AUS | Aktiviert Startsuche und Erinnerung. Mit SPEICHERN unter [Setup](setup.md#die-werte-behalten) speichern; Vorgabe EIN. |
| RELEASES: STABLE / TESTING | Wählt die geprüften Releases; siehe [Die Suche](#die-suche). Mit SPEICHERN unter [Setup](setup.md#die-werte-behalten) speichern; Vorgabe STABLE. Ein Wechsel verwirft die Ergebnisanzeige. Ein unter der anderen Einstellung gefundenes Release wird nicht angeboten. |
| 2s HALTEN ZUM LADEN & INSTALLIEREN | Erscheint nach Fund eines neueren Releases und lädt, prüft und installiert es. |
| 2s HALTEN ZUM IOMCU-UPDATE | Startet das [IOmcu-Update](coproc_update.md) aus dem mitgelieferten Image, auch nach Ablehnung der Verbindungsrückfrage. |
| 2s HALTEN ZUM C6-FUNK-UPDATE | Sendet das mitgelieferte Image des C6-Funks an den ESP32-C6 des Boards, siehe [Der C6-Funk](#der-c6-funk). |

## Was die Suche braucht

Die Karte zeigt laufende Version und gespeichertes Heimnetz. Suche und Download benötigen:

- Ein unter [WLAN](wifi.md#ein-heimnetz-wählen) gespeichertes Heimnetz. Ohne dieses erscheinen `Kein Heimnetz gespeichert.` und `NETZWERK SPEICHERN: WI-FI ÖFFNEN`. Öffne WI-FI zum Speichern eines Netzwerks und kehre über das Home-Feld zurück. NACH UPDATES SUCHEN bleibt bis dahin grau.
- Einen Prüfstand in SAFE, ausgeschalteten Netzteilausgang und keine offene Aufzeichnung. Andernfalls ist die Taste grau; die Karte zeigt `WLAN benötigt SAFE und einen ausgeschalteten Netzteilausgang.`.
- Kein laufendes IOmcu-Update und keine laufende Panel-Installation.

Während des Downloads darf außerdem keine Platinenzeichnung geladen oder gespeichert werden.

## Die Suche

![Eine laufende Suche](view:updates-checking)

Eine ungerade Minor-Nummer ist ein Testing-Release, auf GitHub als Pre-Release veröffentlicht (0.21.x). Eine gerade Minor-Nummer ist ein Stable-Release (0.22.x).

Das Panel verbindet sich mit dem Heimnetz, sendet eine HTTPS-Anfrage (HTTP über Transport Layer Security) und liest den Release-Tag aus der Antwort. Danach schaltet es das Funkmodul aus. Verbindungsaufbau, Adressvergabe und Anfrage teilen sich eine Frist von 30s. Die Karte zeigt den laufenden Schritt und die Restzeit.

| RELEASES | Anfrage | Auswertung |
|---|---|---|
| STABLE | `GET https://github.com/subtilitas/rcbench/releases/latest` | Tag aus dem `Location`-Header. GitHubs Latest-Release ist kein Pre-Release. Ist es neuer als der laufende Build und liegt außerhalb der Release-Linie, folgt einmal die TESTING-Anfrage. |
| TESTING | `GET https://api.github.com/repos/subtilitas/rcbench/releases?per_page=10` | `tag_name` der 10 nach Datum neuesten Releases, Testing oder Stable, aus der JSON-Antwort (JavaScript Object Notation). Gewählt wird die höchste Version, die kein Entwurf ist, unabhängig von der Reihenfolge. Die Antwort wird in Blöcken von 4096 Bytes bis höchstens 262.144 Bytes verarbeitet und nicht gespeichert. Ein ungültiger Release-Eintrag verwirft die gesamte Antwort. |

In beiden Einstellungen wird ein Release nur angeboten, wenn es zur Release-Linie des Panels gehört und neuer als der laufende Build ist. Die Linie des ESP32-P4-Panels enthält jedes Release, siehe [Die Release-Linie](#die-release-linie).

![Ein neueres Release](view:updates-newer)
![Aktuell](view:updates-current)
![Ein Testing-Release, gefunden mit TESTING](view:updates-testing)

| Karte | Bedeutung |
|---|---|
| `Version 0.22.1 ist verfügbar.` | das neueste Release ist neuer als der laufende Build; das Halten erscheint |
| `Aktuell. Panel-Version: 0.22.0.` | das neueste Release ist die laufende Version oder älter |
| `Kein neueres Release für das ESP32-S3-Panel. Panel-Version: 0.22.0.` | nur auf einem Panel mit der Linie 0.22: das neueste Release ist neuer als der laufende Build und liegt hinter der Release-Linie; es wird nicht angeboten, und die Erinnerung beim Start öffnet sich dafür nicht |
| `Verbindung fehlgeschlagen. Passwort abgelehnt.`, `Verbindung fehlgeschlagen. Netzwerk nicht gefunden.`, `Keine Verbindung. Nach 15s noch keine IP-Adresse.`, `Verbindung zum Heimnetz verloren.` | das Verbinden scheiterte, wie unter Dateien und Update |
| `Keine Antwort innerhalb von 30s.` | die 30s sind abgelaufen |
| `Keine Verbindung zu GitHub.` | Namensauflösung, Verbindung oder Zertifikatsprüfung scheiterten |
| `GitHub hat die Anfrage abgelehnt: HTTP 404.` | eine andere Antwort als ein Redirect, oder mit TESTING eine andere als 200 |
| `GitHub-Anfragelimit erreicht: HTTP 403.` | TESTING: GitHub antwortete mit 403 oder 429; ohne Konto nimmt es 60 API-Anfragen pro Stunde und Adresse an |
| `Die eigene Version des Panels ist nicht MAJOR.MINOR.PATCH: kein Update angeboten.` | die Version des laufenden Builds ist nicht lesbar; nichts wird angeboten |
| `Unerwartete Antwort von GitHub.` | der `Location`-Header, oder mit TESTING die Antwort, fehlt oder ist ungültig; eine zweite Zeile sagt, was |
| `WLAN ausgeschaltet: Prüfstand ARMED.` und ähnliche | ARMED, der Netzteilausgang, ein Log-Lauf oder STOP beendeten die Suche |
| `Abgebrochen: ...` | WI-FI, das Update des IOmcu oder ABBRECHEN beendeten die Suche; `Abgebrochen: Die Seite WI-FI nutzt das WLAN.` verschwindet, sobald die Sitzung von WI-FI geendet hat und das Funkteil aus ist |

![Eine Suche ohne Antwort](view:updates-failed)

Ein gültiger Tag besteht aus `v` und MAJOR.MINOR.PATCH. Jeder Bestandteil enthält nur Ziffern, liegt zwischen 0 und 65535 und hat höchstens 5 Stellen ohne führende Null. Verglichen wird zuerst MAJOR, bei Gleichheit MINOR, danach PATCH.

## Die Release-Linie

0.22 ist das letzte Release für das ESP32-S3-Panel. Spätere Releases sind für die 7-Zoll-Display-Platine mit ESP32-P4 von Waveshare.

Die Release-Linie des ESP32-P4-Panels umfasst alle Versionen. In beiden Einstellungen wird ein gefundenes Release angeboten, wenn es neuer als der laufende Build ist.

| GitHub nennt | ESP32-P4-Panel mit 0.23.0 |
|---|---|
| `v0.24.0`, `v1.0.0` | angeboten |
| `v0.23.0` | aktuell |
| `v0.22.1` | aktuell; ein älteres Release wird nie angeboten |

Das ESP32-S3-Panel akzeptiert nur 0.22.x. Die ersten Zeilen im Kartenfuß nennen diese Einschränkung:

| GitHub nennt | Panel mit 0.22.0 |
|---|---|
| `v0.22.1` | angeboten |
| `v0.22.0` | aktuell; ein älteres Release wird nie angeboten |
| `v0.23.0`, `v0.24.0`, `v1.0.0` | `Kein neueres Release für das ESP32-S3-Panel.` |
| ein ungültiger Tag, etwa `v0.22.1-rc1` | `Unerwartete Antwort von GitHub.` |

Liegt unter TESTING die höchste Version der 10 neuesten Releases außerhalb der Panel-Linie, wird stattdessen die höchste passende Version aus dieser Liste angeboten, sofern sie neuer als der laufende Build ist. Liegt unter STABLE GitHubs Latest-Release außerhalb der Linie und über der laufenden Version, fragt das Panel einmal dieselbe Liste mit 10 Releases ab und sucht darin nach einem neueren 0.22.x. Scheitert die Anfrage, bleibt das Ergebnis der ersten Prüfung.

## Der Download

Das Halten über 2s ist die einzige Bestätigung: Es folgt keine Frage. Das Panel verbindet sich innerhalb von 30s mit dem Heimnetz und lädt `SHA256SUMS` und `rcbench-panel-p4-vX.Y.Z.bin` des Release über HTTPS. Es folgt höchstens 3 Redirects, nur zu `github.com`, `release-assets.githubusercontent.com` und `objects.githubusercontent.com`.

1. `SHA256SUMS` darf höchstens 4096 Bytes enthalten und muss genau eine Zeile für das Image haben.
2. Das Image muss seine Größe ankündigen. Fehlende Größe, 0 Bytes oder mehr als 8.126.464 Bytes werden abgelehnt. Das Image liegt im PSRAM (Pseudo-Static Random-Access Memory).
3. Sein SHA-256 (Secure Hash Algorithm, 256 Bit) muss dem Eintrag in `SHA256SUMS` entsprechen.
4. Es muss alle Prüfungen für hochgeladene Images bestehen und die Version des Releases nennen.

Erst nach diesen Prüfungen schreibt das [Panel-Update](update.md) das Image wie bei INSTALLIEREN. Vorher bleibt der Flash unverändert.

![Der Download](view:panel-download)

ABBRECHEN beendet den Download. Auch STOP, ARMED, ein eingeschalteter Netzteilausgang, eine offene Aufzeichnung, das Laden einer Platinenzeichnung oder die Frist von 180s ab dem Halten brechen ihn ab. Der laufende Build bleibt erhalten. SCHLIESSEN führt zu UPDATES zurück oder zum Menü, wenn der Download über die Erinnerung begann.

![Ein abgebrochener Download](view:panel-download-failed)

## Der IOmcu

![Ein anderer Build auf dem IOmcu](view:updates-coproc)

Unter den Tasten nennt UPDATES die laufende IOmcu-Version (`Läuft: 0.21.2`) und das mitgelieferte Image (`Auf diesem Panel: 0.22.0`). 2s HALTEN ZUM IOMCU-UPDATE öffnet denselben Updateablauf mit Fortschrittsanzeige wie die Rückfrage beim Verbindungsaufbau. Nach Updateende oder Ablauf der START-Anfrage kehrt die Seite zu UPDATES zurück.

Für das Halten gelten dieselben Voraussetzungen wie für die Rückfrage: ein IOmcu mit abweichendem Build, SAFE, Netzteilausgang aus, WLAN vollständig aus und keine laufende Panel-Installation. Andernfalls ist die Taste grau. Darüber steht der erste zutreffende Grund:

| Zeile | Bedingung |
|---|---|
| `Dieses Panel enthält keine IOmcu-Firmware.` | das Panel ist ohne das Image des IOmcu gebaut |
| `Wartet auf das Ende des IOmcu-Updates.` | ein Update läuft, oder sein START ist noch nicht angenommen |
| `Kein IOmcu antwortet.` | keine Verbindung; `Läuft: unbekannt` |
| `Wartet auf die Build-Prüfung des IOmcu.` | ein IOmcu antwortet; sein Build ist noch nicht verglichen |
| `Build des IOmcu wird geprüft.` | der SHA-256-Vergleich läuft |
| `Build-Prüfung fehlgeschlagen. Sie läuft bei der nächsten Verbindung erneut.` | der Vergleich endete ohne Antwort |
| `Kein Update über CAN. Installiere Firmware einmal über USB.` | der IOmcu hat keine Update-Pages oder keine Partitionstabelle |
| `Dieser Panel-Build enthält keine IOmcu-Firmware für diese Platine.` | der IOmcu meldet ein Board, für das dieser Panel-Build kein Image trägt; kein Update wird angeboten |
| `Der IOmcu nutzt den im Panel enthaltenen Build.` | es gibt nichts zu senden |
| `Für das Update muss SAFE aktiv und der Netzteilausgang aus sein.` | ARMED, ein wartendes Arm, oder der Netzteilausgang ist an |
| `Wartet, bis das WLAN des Panels aus ist.` | eine Suche, ein Download oder [WLAN](wifi.md) hat den Funk an |
| `Wartet: Die Installation der Panel-Firmware läuft.` | eine Panel-Installation ist im Gang |

Bei verfügbarer Haltetaste steht `Ausgänge bleiben aus. ARMED ist bis zum Ende des Updates gesperrt.`. Bei einem älteren Image erscheint stattdessen `Die Firmware auf diesem Panel ist älter.` in der Warnfarbe.

## Der C6-Funk

![Der C6 im Auslieferungszustand: keine Version gemeldet](view:updates-c6)

Unter der Karte zeigt UPDATES den ESP32-C6 des Boards, der Wi-Fi und Bluetooth trägt: `C6-Funk: nicht gemeldet. Auf diesem Panel: 2.13.0.` Die erste Version ist die, die der C6 beim ersten Wi-Fi-Start seit dem Panel-Start genannt hat; der ausgelieferte C6 nennt keine. 2s HALTEN ZUM C6-FUNK-UPDATE sendet das mitgelieferte Image über die SDIO-Verbindung (Secure Digital Input/Output) des Boards; die Taste zeigt dann `C6-UPDATE 37%`.

Das Halten ist aktiv, wenn der Prüfstand SAFE ist, kein ARM wartet, der Netzteilausgang aus ist, kein Log-Lauf offen ist, keine Platinenzeichnung geholt wird, kein anderes Update läuft, das WLAN aus ist und der C6 nicht die mitgelieferte Version hat. Ein Tippen auf das graue Halten zeigt den Grund im Alarmband, zum Beispiel `Das C6-Update wartet, bis das WLAN des Panels aus ist.`

Während das Update läuft, wird ARM abgelehnt, und das WLAN bleibt aus. Es endet mit einem Alarm:

| Alarm | Bedeutung |
|---|---|
| `C6-Funk auf 2.13.0 aktualisiert. Das Panel startet neu.` | fertig; das Panel startet 3s später neu |
| `Der C6-Funk nutzt bereits 2.13.0. Nichts gesendet.` | nichts zu senden |
| `Der C6-Funk nimmt kein Update über SDIO an. Flashe ihn über Header H4.` | die Firmware des C6 hat das Update abgelehnt; die alte Firmware läuft |
| `Der C6-Funk hat nicht geantwortet. WLAN ist bis zum Neustart des Panels aus.` | die Verbindung zum C6 ist ausgefallen |
| `C6-Update abgebrochen: keine Antwort. Vorherige Firmware läuft.` | ein Schritt blieb ohne Antwort; `Schreiben fehlgeschlagen`, `Prüfung im C6 fehlgeschlagen` und `Aktivierung fehlgeschlagen` lauten ebenso |

Nach jedem Ende außer dem ersten behält der C6 die Firmware, die er hatte, und das WLAN arbeitet wie zuvor (nach einer ausgefallenen Verbindung erst nach einem Neustart des Panels). Das Flashen über Header H4 mit einem USB-UART-Adapter steht in [Bauen](https://github.com/subtilitas/rcbench/wiki/Building-de#wiederherstellung-über-header-h4).

## Aktuelle Einschränkungen

- Die Dauer von Suche und Download in einem echten Netz wurde nicht gemessen.
- Die Dauer des C6-Funk-Updates ist nicht gemessen, und ob der ausgelieferte C6 es über SDIO annimmt, ist nicht bekannt.
- Ein nicht antwortender erster DNS-Server (Domain Name System) verzögert nach dem Verbinden jede Namensauflösung um etwa 7s innerhalb der Suchfrist von 30s. lwIP fragt diesen Server 4-mal für insgesamt bis zu 7s ab, bevor es den nächsten versucht.
- Unter TESTING erlaubt GitHub ohne Konto 60 API-Anfragen pro Stunde und öffentlicher Adresse, gemeinsam für alle Geräte dahinter. Das Panel wiederholt keine Anfrage.
- TESTING liest die 10 nach Datum neuesten Releases. Ältere Veröffentlichungen werden unabhängig von ihrer Versionsnummer nicht erfasst.
- Die Liste ist auf diese 10 Releases begrenzt (`per_page=10`). Ein Patch-Release 0.22.x wird in keiner Einstellung gefunden, wenn danach mindestens 10 Releases veröffentlicht wurden.
- Die zeitliche Gültigkeit von Zertifikaten wird nicht geprüft. mbedTLS ist ohne Ablaufprüfung gebaut (`CONFIG_MBEDTLS_HAVE_TIME_DATE` aus, die Vorgabe von ESP-IDF).
- Zum Download müssen die Release-Dateien `rcbench-panel-p4-vX.Y.Z.bin` und `SHA256SUMS` heißen.
