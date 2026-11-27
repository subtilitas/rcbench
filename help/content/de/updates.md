---
screen: UPDATES
title: Updates
order: 64
---

# Updates

UPDATES führt in sechs Schritten durch die Firmware-Updates: PANEL, IOMCU, C6-FUNK, DIAL, LEISTUNGSKNOTEN und AUSWUCHTKNOTEN. Nur der aktuelle Schritt hat ein Bedienelement. NACH UPDATES SUCHEN fragt GitHub nach neuerer Panel-Firmware. Öffne UPDATES über [Setup](setup.md). Die [Erinnerung beim Start](upd_remind.md) führt dieselbe Suche einmal je Panelstart aus. Das ESP32-S3-Panel ist auf die Linie 0.22 begrenzt; ESP32-P4 unterstützt neuere Linien. Siehe [Die Release-Linie](#die-release-linie).

![UPDATES: die letzte Karte](view:updates)

Die Schrittliste links zeigt je Schritt Nummer, Namen, die Version seines Ziels und sein Etikett. Die Karte rechts zeigt den aktuellen Schritt: seine Versionen, was sein Update tut, worauf er wartet und wie sein letzter Versuch endete.

| Bedienelement | Wirkung |
|---|---|
| NACH UPDATES SUCHEN | Fragt GitHub einmal ab. Während der Suche heißt die Taste ABBRECHEN. Grau, solange ein Update des IOmcu, des C6-Funks oder eines Funkgeräts läuft. |
| das Bedienelement des aktuellen Schritts | Auf der Karte; 2s Halten startet das Update des Schritts, siehe [Die Schritte](#die-schritte). Grau, solange etwas es sperrt; ein Druck zeigt den Grund im Meldeband. |
| SCHRITT ÜBERSPRINGEN | Auf der Karte; ein Tippen übergeht den Schritt bis zum Neustart des Panels, der nächste Schritt wird aktuell. |
| ÜBERSPRUNGENE SCHRITTE ZEIGEN | Auf der letzten Karte, solange ein Schritt übersprungen ist; holt die übersprungenen Schritte zurück. |
| UPDATE ABBRECHEN | In der Mitte der Fußleiste, während die Übertragung eines Geräts läuft; beendet sie vor dem letzten Byte, grau, sobald das Gerät installiert. |
| OPTIONEN | Rechts in der Fußleiste; öffnet die Liste mit Erinnerung beim Start, Releases und Einzelne Updates, siehe [Optionen](#optionen). Grau, solange eine Suche, ein Download oder ein Update läuft. |

Updates und Neuinstallationen außerhalb der Reihe stehen auf der Seite EINZELNE UPDATES, siehe [Einzelne Updates](#einzelne-updates).

## Die Schritte

![Schritt 2: der IOmcu](view:updates-step-iomcu)

| Schritt | Aktuell, solange | Bedienelement |
|---|---|---|
| 1 PANEL | eine Suche ein neueres Release fand | 2s HALTEN: LADEN & INSTALLIEREN, siehe [Der Download](#der-download) |
| 2 IOMCU | der IOmcu einen anderen Build als den auf diesem Panel ausführt | 2s HALTEN ZUM IOMCU-UPDATE, siehe [Der IOmcu](#der-iomcu) |
| 3 C6-FUNK | der C6 eine andere Version als die enthaltene meldet; nach diesem Schritt startet das Panel neu | 2s HALTEN ZUM C6-FUNK-UPDATE, siehe [Der C6-Funk](#der-c6-funk) |
| 4 DIAL | das verbundene Dial eine andere Version ausführt | 2s HALTEN ZUM DIAL-UPDATE, siehe [Das Dial](#das-dial) |
| 5 LEISTUNGSKNOTEN | der verbundene Leistungsknoten eine andere Version ausführt | 2s HALTEN ZUM LEISTUNGSKNOTEN-UPDATE, siehe [Die Sensorknoten](#die-sensorknoten) |
| 6 AUSWUCHTKNOTEN | der verbundene Auswuchtknoten eine andere Version ausführt | 2s HALTEN ZUM AUSWUCHTKNOTEN-UPDATE |

Das Panel-Image bringt die IOmcu-Images mit; die Images des C6-Funks und der Geräte sind Dateien des Release, die das Halten ihres Schritts holt (siehe [Geräte-Images](#geräte-images)). Ein neueres Panel-Release bringt neuere, darum kommt PANEL zuerst. Ein neueres Release, das gefunden wird, während ein späterer Schritt aktuell ist, macht PANEL wieder aktuell. Eine Suche, die nichts Neueres fand oder fehlschlug, sperrt nie, ebenso wenig ein fehlgeschlagener oder gestoppter Download oder eine fehlgeschlagene Installation: NACH UPDATES SUCHEN ist der Weg zurück. Nach einer fehlgeschlagenen Installation zeigt die Karte `Installation fehlgeschlagen. Die Panel-Firmware ist unverändert.` und `NACH UPDATES SUCHEN bietet das Release erneut an.` Eine Suche nimmt einem laufenden Update nie die Karte: Die Aufforderung eines Geräts zum Annehmen und UPDATE ABBRECHEN bleiben.

| Etikett | Bedeutung |
|---|---|
| `ALS NÄCHSTES` | der aktuelle Schritt |
| `AKTUELL` | führt die enthaltene oder neueste Version aus |
| `SPÄTER` | wartet auf den aktuellen Schritt |
| `UPDATE LÄUFT`, `WIRD GEPRÜFT` | sein Update läuft, oder eine kurze Wartezeit; die Karte sagt welche |
| `FEHLGESCHLAGEN` | sein letzter Versuch seit dem Start des Panels schlug fehl; die Karte zeigt den Grund, das Halten ist der neue Versuch |
| `ÜBERSPRUNGEN` | SCHRITT ÜBERSPRINGEN wurde seit dem Start des Panels getippt |
| `NICHT GESUCHT` | PANEL: keine Suche seit dem Start |
| `OHNE VERBINDUNG`, `NICHT VERBUNDEN`, `NICHT GEBUNDEN` | kein IOmcu antwortet; ein gebundenes Gerät ist nicht verbunden; kein Gerät der Art ist gebunden |
| `AUS` | ein gebundenes Gerät ist auf WIRELESS ausgeschaltet; sein Schritt wartet nie, und die letzte Karte nennt es |
| `KEIN BLUETOOTH` | ein gebundenes Gerät ist EIN, und Bluetooth LE kann es nicht verbinden (keine Verbindung zum C6 oder kein Sync); sein Schritt wartet nie, und die letzte Karte nennt es |
| `KEINE FIRMWARE`, `FIRMWARE DEFEKT`, `PRÜFFEHLER`, `NUR USB`, `KEINE ANTWORT`, `NICHT GEMELDET` | hier nichts zu aktualisieren; die letzte Karte nennt diese Schritte |

![Ein Schritt, gesperrt bei ARMED](view:updates-held)

ARMED, der Netzteilausgang an, ein offener Aufzeichnungslauf, WLAN an oder eine Platinenzeichnung, die geladen wird, sperren den Schritt: Das Bedienelement ist grau, und die Karte nennt den Grund. Ein laufendes Halten endet ohne Start, wenn sich sein Schritt unter dem Finger ändert.

![Das Dial fragt nach seinem Druck](view:updates-step-dial-ask)

Während die Übertragung eines Geräts läuft, zeigt das Bedienelement ihre Phase, zum Beispiel `AM DIAL DRÜCKEN ZUM ANNEHMEN: 27s`, und UPDATE ABBRECHEN steht in der Fußleiste.

Nach der Installation des Panels und nach dem Update des C6-Funks startet das Panel neu, und der Update-Lauf geht weiter: UPDATES öffnet sich einmal je Start von selbst im Menü, bei SAFE, Netzteilausgang aus, WLAN aus und ohne laufende Installation. Bis dahin nennt das Meldeband, worauf es wartet, zum Beispiel `Update-Lauf: UPDATES öffnet bei SAFE und Netzteilausgang aus.` Die Suche beim Start läuft während eines Laufs nicht. Das Home-Tag auf UPDATES beendet den Lauf, ebenso der letzte erledigte Schritt. Ein Start, der keinen offenen Schritt findet, beendet den Lauf, ohne UPDATES zu öffnen. Ein Lauf öffnet UPDATES bei höchstens 2 Starts.

Nach einem Neustart in einem Update-Lauf wartet ein Schritt, dessen IOmcu oder Gerät noch nicht verbunden ist, mit SCHRITT ÜBERSPRINGEN und geht weiter, sobald es sich verbindet.

![Der Schritt des Dial wartet nach einem Neustart](view:updates-step-waiting)

Ist kein Schritt mehr übrig, nennt die Karte die Schritte, die nicht verbunden, hier nicht aktualisierbar oder übersprungen sind, unter einer von drei Überschriften: `Alle Firmware ist aktuell.`, wenn eine Suche nichts Neueres fand und jeder andere Schritt `AKTUELL` oder `NICHT GEBUNDEN` zeigt; `Nicht nach einem Panel-Release gesucht.`, wenn PANEL `NICHT GESUCHT` oder `FEHLGESCHLAGEN` zeigt und der Rest aktuell ist; `Kein Schritt offen.`, wenn ein Schritt übersprungen wurde oder hier kein Update hat.

![Die letzte Karte mit übersprungenem Dial](view:updates-done)

## Optionen

![Die OPTIONEN von UPDATES](view:updates-settings)

| Zeile | Werte | Wirkung |
|---|---|---|
| Erinnerung beim Start | AUS, EIN; Vorgabe EIN | schaltet die Suche beim Start und ihre Erinnerung |
| Releases | STABLE, TESTING; Vorgabe STABLE | wählt die geprüften Releases, siehe [Die Suche](#die-suche); ein Wechsel verwirft die Ergebnisanzeige, und ein unter der anderen Einstellung gefundenes Release wird nicht angeboten |
| Einzelne Updates | - | schließt die Liste und öffnet EINZELNE UPDATES, siehe [Einzelne Updates](#einzelne-updates); bei ungespeicherter Änderung mit Rückfrage |

Eine Änderung gilt sofort. SPEICHERN in der Leiste der Liste behält sie, wie unter [Setup](setup.md#die-werte-behalten). Das Home-Feld schließt die Liste, bei ungespeicherter Änderung mit Rückfrage.

## Was die Suche braucht

Die Karte zeigt laufende Version und gespeichertes Heimnetz. Suche und Download benötigen:

- Ein unter [WLAN](wifi.md#ein-heimnetz-wählen) gespeichertes Heimnetz. Ohne dieses erscheint `Kein Heimnetz gespeichert.`, und die letzte Karte trägt `NETZWERK SPEICHERN: WI-FI ÖFFNEN`. Öffne WI-FI zum Speichern eines Netzwerks und kehre über das Home-Feld zurück. NACH UPDATES SUCHEN bleibt bis dahin grau.
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
| TESTING | `GET https://api.github.com/repos/subtilitas/rcbench/releases?per_page=5` | `tag_name` der 5 nach Datum neuesten Releases, Testing oder Stable, aus der JSON-Antwort (JavaScript Object Notation). Gewählt wird die höchste Version, die kein Entwurf ist, unabhängig von der Reihenfolge. Die Antwort wird in Blöcken von 4096 Bytes bis höchstens 1.310.720 Bytes verarbeitet und nicht gespeichert. Ein ungültiger Release-Eintrag verwirft die gesamte Antwort. |

In beiden Einstellungen wird ein Release nur angeboten, wenn es zur Release-Linie des Panels gehört und neuer als der laufende Build ist. Die Linie des ESP32-P4-Panels enthält jedes Release, siehe [Die Release-Linie](#die-release-linie).

![Ein neueres Release](view:updates-newer)
![Aktuell](view:updates-current)
![Ein Testing-Release, gefunden mit TESTING](view:updates-testing)

| Karte | Bedeutung |
|---|---|
| `Version 0.22.1 ist verfügbar.` | das neueste Release ist neuer als der laufende Build; der Schritt PANEL wird mit dem Halten aktuell |
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

Liegt unter TESTING die höchste Version der 5 neuesten Releases außerhalb der Panel-Linie, wird stattdessen die höchste passende Version aus dieser Liste angeboten, sofern sie neuer als der laufende Build ist. Liegt unter STABLE GitHubs Latest-Release außerhalb der Linie und über der laufenden Version, fragt das Panel einmal dieselbe Liste mit 10 Releases ab und sucht darin nach einem neueren 0.22.x. Scheitert die Anfrage, bleibt das Ergebnis der ersten Prüfung.

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

## Einzelne Updates

![EINZELNE UPDATES](view:updates-single)

EINZELNE UPDATES startet ein Update oder eine Neuinstallation zur Zeit: des IOmcu, des C6-Funks oder eines Funkgeräts. Öffne die Seite über die Zeile Einzelne Updates in OPTIONEN. Sie bedeckt UPDATES; jeder Start ist ein Halten von 2s.

| Bedienelement | Wirkung |
|---|---|
| 2s HALTEN ZUM IOMCU-UPDATE | Startet das [IOmcu-Update](coproc_update.md) aus dem mitgelieferten Image, auch nach Ablehnung des Angebots, siehe [Der IOmcu](#der-iomcu). |
| 2s HALTEN ZUM C6-FUNK-UPDATE | Holt das Image des C6-Funks aus dem Release und sendet es an den ESP32-C6 des Boards, siehe [Der C6-Funk](#der-c6-funk). |
| 2s HALTEN: C6-FUNK NEU INSTALLIEREN | Dasselbe, solange der C6 die Version des Release meldet. |
| FIRMWARE DER FUNKGERÄTE | Öffnet die Seite mit den Firmware-Zeilen des Dial und der Sensorknoten, siehe [Funkgeräte](#funkgeräte); während ein Update läuft, zeigt sie die Beschriftung dieser Zeile. |
| ZURÜCK | Rechts in der Fußleiste; schließt die Seite. |

Ein Finger zur Zeit: Ein zweiter Finger auf einem anderen Bedienelement startet nichts. Das Home-Tag schließt zuerst die Seite der Funkgeräte, dann EINZELNE UPDATES, dann verlässt es UPDATES. Das Verlassen von UPDATES schließt die Seite, und ein laufendes Halten startet nichts. Eine Suche, die startet, während die Seite offen ist, schließt nichts.

## Der IOmcu

![Ein anderer Build auf dem IOmcu](view:updates-coproc)

In der linken Spalte von EINZELNE UPDATES nennt die Seite die laufende IOmcu-Version (`Läuft: 0.21.2`) und das mitgelieferte Image (`Auf diesem Panel: 0.22.0`). 2s HALTEN ZUM IOMCU-UPDATE öffnet denselben Updateablauf mit Fortschrittsanzeige wie Schritt 2 auf UPDATES. Nach Updateende oder Ablauf der START-Anfrage kehrt die Seite zu UPDATES zurück.

Für das Halten gelten dieselben Voraussetzungen wie für Schritt 2: ein IOmcu mit abweichendem Build, SAFE, Netzteilausgang aus, WLAN vollständig aus und keine laufende Panel-Installation. Andernfalls ist die Taste grau. Darüber steht der erste zutreffende Grund; ein Tippen oder Halten auf die graue Taste zeigt dieselbe Zeile auf dem Alarmband:

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

Unter der Überschrift zeigt EINZELNE UPDATES den ESP32-C6 des Boards, der Wi-Fi und Bluetooth trägt: `C6-Funk: nicht gemeldet. Release: 2.13.0.` Die erste Version ist die, die der C6 beim ersten Wi-Fi-Start seit dem Panel-Start genannt hat; der ausgelieferte C6 nennt keine. 2s HALTEN ZUM C6-FUNK-UPDATE holt zuerst das Image des Release ([Geräte-Images](#geräte-images)) und sendet es über die SDIO-Verbindung (Secure Digital Input/Output) des Boards; die Taste zeigt dann `C6-UPDATE 37%`.

Das Halten ist aktiv, wenn der Prüfstand SAFE ist, kein ARM wartet, der Netzteilausgang aus ist, kein Log-Lauf offen ist, keine Platinenzeichnung geholt wird, kein anderes Update läuft, keine Servo- oder ESC-Programmierung einen Port belegt und das WLAN aus ist. Bei einem C6, der die Version des Release meldet, heißt es 2s HALTEN: C6-FUNK NEU INSTALLIEREN: Ein Image derselben Version kann andere Einstellungen tragen, etwa 3 Bluetooth-Verbindungen statt 5. Ein Tippen auf das graue Halten zeigt den Grund im Alarmband, zum Beispiel `Das C6-Update wartet, bis das WLAN des Panels aus ist.`

Während das Update läuft, werden ARM, AUSGANG EIN des Netzteils, PROG 5V EIN und der Start einer Programmierung abgelehnt, und das WLAN bleibt aus. Es endet mit einem Alarm:

| Alarm | Bedeutung |
|---|---|
| `C6-Funk auf 2.13.0 aktualisiert. Das Panel startet neu.` | fertig; das Panel startet 3s später neu |
| `C6-Funk auf 2.13.0 aktualisiert. Neustart, sobald SAFE und alle Ausgänge aus sind.` | fertig; der Neustart wartet, bis der Prüfstand SAFE ist, Netzteilausgang und PROG 5V aus sind und keine Programmierung einen Port belegt |
| `Der C6-Funk nimmt kein Update über SDIO an. Flashe ihn über Header H4.` | die Firmware des C6 hat das Update abgelehnt; die alte Firmware läuft |
| `Der C6-Funk hat nicht geantwortet. WLAN ist bis zum Neustart des Panels aus.` | die Verbindung zum C6 ist ausgefallen |
| `C6-Update abgebrochen: keine Antwort. Vorherige Firmware läuft.` | ein Schritt blieb ohne Antwort; `Schreiben fehlgeschlagen`, `Prüfung im C6 fehlgeschlagen` und `Aktivierung fehlgeschlagen` lauten ebenso |

Nach jedem Ende außer dem ersten behält der C6 die Firmware, die er hatte, und das WLAN arbeitet wie zuvor (nach einer ausgefallenen Verbindung erst nach einem Neustart des Panels). Das Flashen über Header H4 mit einem USB-UART-Adapter steht in [Der Funk über den ESP32-C6](https://github.com/subtilitas/rcbench/wiki/C6-de#wiederherstellung-über-header-h4).

## Funkgeräte

![Der Eintrag FUNKGERÄTE, während das Dial nach seinem Druck fragt](view:updates-dial)

Der Eintrag FUNKGERÄTE auf EINZELNE UPDATES zählt die Geräte, die eine Version gemeldet haben, und die, denen eine andere Version angeboten wird: `Verbunden: 1 von 3. Andere Version: 0.` FIRMWARE DER FUNKGERÄTE öffnet die Seite über EINZELNE UPDATES. ZURÜCK und das Home-Tag schließen sie und zeigen wieder EINZELNE UPDATES.

![Die Seite der Funkgeräte](view:updates-devices)

| Bedienelement | Wirkung |
|---|---|
| 2s HALTEN ZUM DIAL-UPDATE | Holt das Image des M5Stack Dial aus dem Release und sendet es über Bluetooth LE an das verbundene Dial, siehe [Das Dial](#das-dial). |
| 2s HALTEN: DIAL-FIRMWARE NEU INSTALLIEREN | Dasselbe, solange das Dial die Version des Release meldet. |
| 2s HALTEN ZUM LEISTUNGSKNOTEN-UPDATE | Sendet das Image des Leistungsknotens an den gebundenen Leistungsknoten, siehe [Die Sensorknoten](#die-sensorknoten). |
| 2s HALTEN: LEISTUNGSKNOTEN NEU INSTALLIEREN | Dasselbe, solange der Knoten die Version des Release meldet. |
| 2s HALTEN ZUM AUSWUCHTKNOTEN-UPDATE | Sendet das Image des Auswuchtknotens an den gebundenen Auswuchtknoten. |
| 2s HALTEN: AUSWUCHTKNOTEN NEU INSTALLIEREN | Dasselbe, solange der Knoten die Version des Release meldet. |
| 2s HALTEN: UPDATE VON DER SD-KARTE | Auf jeder Zeile oben, nach einem fehlgeschlagenen Download oder ohne Heimnetz: liest das Image von der SD-Karte, siehe [Geräte-Images](#geräte-images). |
| UPDATE ABBRECHEN | In der Mitte der Fußleiste, solange ein Update läuft, ein einfaches Tippen: Beendet das Update vor seinem letzten Byte, und das Gerät behält seine Firmware; danach installiert das Gerät, und die Taste ist grau. |
| ZURÜCK | Schließt die Seite: EINZELNE UPDATES erscheint wieder. |

Es läuft ein Update zur Zeit; solange eines läuft, ist das Halten der anderen Zeilen grau.

## Das Dial

Die Zeile DIAL-FIRMWARE zeigt die Version des verbundenen Dial und die Version des Release, das das Panel ausführt: `Dial: 0.21.0. Release: 0.22.0.` Das Halten braucht den Prüfstand in SAFE, ein verbundenes Dial und kein anderes laufendes Update. Ein Tippen auf das graue Halten zeigt den Grund im Alarmband, zum Beispiel `Verbinde das Gerät zuerst auf WIRELESS.`

1. Das Halten beendet den Knopf-Link des Dial; die Taste zeigt `DIAL-UPDATE: VERBINDET`.
2. Das Dial zeigt die Versionen des Updates und fragt 30s lang nach einem Druck seiner Bildschirmtaste; die Taste zeigt `AM DIAL DRÜCKEN ZUM ANNEHMEN: 27s`. Ein Druck nimmt an, ein Halten von 1s am Dial lehnt ab, kein Druck lehnt ab. Vor der Antwort wird nichts gelöscht.
3. Das Dial schreibt das Image; die Taste zeigt `DIAL-UPDATE 37%`.
4. Das Dial prüft den SHA-256 (Secure Hash Algorithm, 256 Bit) des Images, installiert es und startet neu. Der Alarm lautet `Dial auf 0.22.0 aktualisiert. Verbindet sich von selbst neu.`

Jedes andere Ende lautet `Dial-Update abgebrochen: <Grund>. Vorherige Firmware läuft.`, zum Beispiel `nicht innerhalb von 30s angenommen` oder `Image beschädigt angekommen`; das Dial behält die Firmware, mit der es lief. Bricht der Link nach dem letzten Byte ab, kann das Dial gerade installieren: Das Panel wartet bis zu 60s auf seine Version und meldet, was es findet, oder `Dial hat sich nach dem Update nicht gemeldet. Prüfe seine Version auf UPDATES.` Während das Update läuft, wird ARM mit `ARMED gesperrt: Ein Update eines Funkgeräts läuft.` abgelehnt, und das Update des C6-Funks wartet. AUSGANG EIN des Netzteils, PROG 5V EIN und der Start einer Programmierung werden mit einem Alarm abgelehnt, der das Update nennt, zum Beispiel `Netzteilausgang nicht eingeschaltet: Das Dial-Update läuft.` Der Knopf steuert das Panel nicht, bis sich sein Link wieder verbindet; diese Lücke ist kein Verlustfehler.

Das Image trägt keine Signatur: Das Dial nimmt jedes Dial-Image an, das ein Gerät in der Nähe sendet, sobald seine Bildschirmtaste gedrückt wird. Das Image eines anderen Geräts lehnt es ab, bevor es etwas schreibt. Ein neues Image, das vor seiner ersten Verbindung neu startet, geht zum Image davor zurück; ein Dial, das zuletzt vor dieser Firmware über USB geflasht wurde, hat diesen Rollback erst nach einem USB-Flash.

## Die Sensorknoten

![Der Leistungsknoten fragt nach seiner BOOT-Taste](view:updates-node)

Die Zeilen LEISTUNGSKNOTEN-FIRMWARE und AUSWUCHTKNOTEN-FIRMWARE zeigen die Version des Knotens, aus seinem STATUS-Paket auf der laufenden Verbindung, und die Version des Release, das das Panel ausführt: `Knoten: 0.23.17. Release: 0.23.18.` Das Halten braucht den Prüfstand in SAFE, den Knoten gebunden und verbunden mit gelesener Version und kein anderes laufendes Update.

1. Das Halten beendet den eigenen Link des Knotens; WIRELESS zeigt den Knoten AUS, bis das Update endet. Die Taste zeigt `LEISTUNGSKNOTEN-UPDATE: VERBINDET`.
2. Der Knoten wartet 30s auf seine BOOT-Taste, 1s gehalten; die Taste zeigt `BOOT AM KNOTEN 1s HALTEN: 27s` und das Band `Halte innerhalb von 30s die BOOT-Taste am Knoten 1s, um das Update anzunehmen.` Kein Halten lehnt ab. Vor dem Halten wird nichts gelöscht.
3. Der Knoten schreibt das Image; die Taste zeigt `LEISTUNGSKNOTEN-UPDATE 37%`.
4. Der Knoten prüft das Image, installiert es und startet neu. Der Alarm lautet `Leistungsknoten auf 0.23.18 aktualisiert. Verbindet sich von selbst neu.` Der Link des Knotens verbindet sich wieder aus den Werten von SETUP, und die Zeile liest die neue Version aus dem ersten STATUS-Paket des Knotens.

Die Zeile des Leistungsknotens sendet das Image des Boards, das der Knoten in seinem INFO nennt: `rcbench-node-pwr-<Version>.bin` an einen ESP32-C6-Zero, `rcbench-node-display-<Version>.bin` an den Anzeigeknoten ES3C28P. Für den Anzeigeknoten zeigt die Zeile ANZEIGEKNOTEN-FIRMWARE und `Anzeigeknoten: 0.23.17. Release: 0.23.18.`, der Schritt ANZEIGEKNOTEN; seine BOOT-Taste ist IO0. Ein Image, das nicht zum Board des Knotens passt, endet vor dem ersten Byte mit `Anzeigeknoten-Update abgebrochen: Image für eine andere Platine. Vorherige Firmware läuft.` Ein Board, für das das Release kein Image hat, zeigt KEINE FIRMWARE.

Die Messwerte eines Leistungsknotens und die Messungen eines Auswuchtknotens ruhen vom Halten an, bis der Knoten zurück ist. Diese Lücke ist kein Verlust: Das Band zeigt kein `LEISTUNGSKNOTEN VERLOREN`. Jedes andere Ende lautet `Auswuchtknoten-Update abgebrochen: <Grund>. Vorherige Firmware läuft.` mit den Gründen des Dial, und der Knoten behält die Firmware, mit der er lief. Ein Knoten lehnt das Image der anderen Rolle ab, bevor er etwas schreibt. Wie beim Update des Dial werden AUSGANG EIN des Netzteils, PROG 5V EIN und der Start einer Programmierung abgelehnt, zum Beispiel mit `Programmierung nicht gestartet: Das Leistungsknoten-Update läuft.`

## Geräte-Images

Das Panel-Image bringt nur die IOmcu-Images mit. Die Images des C6-Funks, des Dial und der Knoten sind Dateien des Release, das das Panel ausführt: `rcbench-c6-2.13.0.bin` (die esp_hosted-Version des C6), `rcbench-dial-<Version>.bin`, `rcbench-node-pwr-<Version>.bin`, `rcbench-node-bal-<Version>.bin` und `rcbench-node-display-<Version>.bin`. Das Halten eines Schritts nimmt sein Image von einer von drei Stellen, und die Karte nennt die Datei:

| Zeile der Karte | Woher das Halten es nimmt |
|---|---|
| `Das Halten lädt zuerst rcbench-dial-0.23.18.bin von GitHub.` | GitHub, mit gespeichertem Heimnetz: `SHA256SUMS` und die Datei über HTTPS, die Länge der Datei an den Slot des Geräts gebunden und ihr SHA-256 an ihre Zeile in `SHA256SUMS` |
| `Das Halten liest BENCH/UPDATES/rcbench-dial-0.23.18.bin von der SD-Karte.` | `BENCH/UPDATES` der SD-Karte, nach einem fehlgeschlagenen Download oder ohne Heimnetz; das Bedienelement zeigt 2s HALTEN: UPDATE VON DER SD-KARTE. Eine `SHA256SUMS` neben der Datei wird geprüft, wenn sie sie nennt |
| `rcbench-dial-0.23.18.bin ist geladen und geprüft.` | der Speicher, bis das Panel neu startet |

Das Bedienelement zeigt `DOWNLOAD 43%`, `DATEI WIRD GEPRÜFT` oder `SD-KARTE WIRD GELESEN`. Das Image besteht die Prüfungen der Sender: Chip und Projekt des Geräts, sein eigener SHA-256, die Rolle bei Dial und Knoten, die Version des Release. Danach startet das Update von selbst, sobald der Funk aus ist, innerhalb von 20s. ARMED, das Netzteil, ein Log-Lauf, STOP, WI-FI und UPDATE ABBRECHEN beenden einen Download; solange ARMED gilt, startet keiner. UPDATE ABBRECHEN verwirft auch das Update, das das Halten angefordert hat; ein Image, das noch besteht, bleibt für das nächste Halten. Kein Ausgang ändert sich dabei.

![Der Schritt des Dial nach einem fehlgeschlagenen Download](view:updates-step-dial-card)

Ein fehlgeschlagener Versuch steht mit einem Code auf der Karte: `Download fehlgeschlagen (D10): Keine Verbindung zu GitHub.`, `SD-Karte fehlgeschlagen (S02): BENCH/UPDATES/rcbench-dial-0.23.18.bin fehlt auf der SD-Karte.`, `Prüfung fehlgeschlagen (V05): Die Datei hat Version 0.23.17, dieses Release 0.23.18.` Das nächste Halten nimmt die nächste Stelle: die Karte nach einem Download, GitHub nach der Karte. Die Codes stehen in der [Bildschirm-Referenz](https://github.com/subtilitas/rcbench/wiki/Screens-de#geräte-images).

## Aktuelle Einschränkungen

- Die Dauer von Suche und Download in einem echten Netz wurde nicht gemessen.
- Die Dauer des C6-Funk-Updates ist nicht gemessen, und ob der ausgelieferte C6 es über SDIO annimmt, ist nicht bekannt.
- Die Dauer des Dial-Updates ist nicht gemessen.
- Die Dauer eines Knoten-Updates ist nicht gemessen.
- Ein nicht antwortender erster DNS-Server (Domain Name System) verzögert nach dem Verbinden jede Namensauflösung um etwa 7s innerhalb der Suchfrist von 30s. lwIP fragt diesen Server 4-mal für insgesamt bis zu 7s ab, bevor es den nächsten versucht.
- Unter TESTING erlaubt GitHub ohne Konto 60 API-Anfragen pro Stunde und öffentlicher Adresse, gemeinsam für alle Geräte dahinter. Das Panel wiederholt keine Anfrage.
- TESTING liest die 5 nach Datum neuesten Releases. Ältere Veröffentlichungen werden unabhängig von ihrer Versionsnummer nicht erfasst.
- Die Liste ist auf diese 5 Releases begrenzt (`per_page=5`). Ein Patch-Release 0.22.x wird in keiner Einstellung gefunden, wenn danach mindestens 5 Releases veröffentlicht wurden.
- Panels auf 0.21.0 bis 0.23.18 fragen nach 10 Releases und lehnen eine Antwort über 262.144 Bytes ab. GitHubs Antwort ist seit v0.23.18 größer, daher endet TESTING dort mit `Der Download ist zu groß für das Panel.`. Ein solches Panel bekommt das nächste Release einmal von Hand: `rcbench-panel-p4-vX.Y.Z.bin` und seine `.sig`, auf der WLAN-Seite hochgeladen, oder der Web-Flasher über USB.
- Die zeitliche Gültigkeit von Zertifikaten wird nicht geprüft. mbedTLS ist ohne Ablaufprüfung gebaut (`CONFIG_MBEDTLS_HAVE_TIME_DATE` aus, die Vorgabe von ESP-IDF).
- Zum Download müssen die Release-Dateien `rcbench-panel-p4-vX.Y.Z.bin` und `SHA256SUMS` heißen.
