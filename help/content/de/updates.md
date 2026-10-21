---
screen: UPDATES
title: Updates
order: 64
---

# Updates

UPDATES fragt GitHub, welches Release der Panel-Firmware das neueste ist, und installiert es. Der Bildschirm öffnet über die Taste UPDATES unter [Setup](setup.md). Eine [Erinnerung beim Start](upd_remind.md) stellt dieselbe Frage einmal pro Start des Panels. 0.22 ist das letzte Release für das ESP32-S3-Panel: dem Panel werden nur Patch-Releases 0.22.x angeboten, siehe [Die Release-Linie](#die-release-linie).

![UPDATES](view:updates)

| Bedienelement | Was es tut |
|---|---|
| NACH UPDATES SUCHEN | fragt GitHub einmal; dieselbe Taste heißt ABBRECHEN, solange die Suche läuft |
| ERINNERUNG BEIM START: EIN / AUS | schaltet die Suche beim Start und ihre Erinnerung; gespeichert mit SPEICHERN unter [Setup](setup.md#die-werte-behalten); ab Werk an |
| RELEASES: STABLE / TESTING | legt fest, welche Releases eine Suche liest, siehe [Die Suche](#die-suche); gespeichert mit SPEICHERN unter [Setup](setup.md#die-werte-behalten); ab Werk STABLE; ein Wechsel blendet das letzte Ergebnis aus, und ein mit der anderen Einstellung gefundenes Release wird nicht angeboten |
| 2 s HALTEN: LADEN & INSTALLIEREN | erscheint, sobald eine Suche ein neueres Release gefunden hat; lädt, prüft und installiert es |
| 2 s HALTEN: IOMCU AKTUALISIEREN | startet das [IOmcu-Update](coproc_update.md) aus dem Image, das dieses Panel mitbringt, auch nachdem seine Frage abgelehnt wurde |

## Was die Suche braucht

Die Karte nennt die laufende Version und das gespeicherte Heimnetz. Suche und Download brauchen:

- ein unter [WLAN](wifi.md#ein-heimnetz-wählen) gespeichertes Heimnetz. Ohne eines sagt die Karte `Kein Heimnetz gespeichert.`, und NACH UPDATES SUCHEN ist grau;
- den Prüfstand SAFE, den Netzteilausgang aus und keinen offenen Log-Lauf. Sonst ist die Taste grau, und die Karte sagt `Für WLAN muss SAFE aktiv und das Netzteil aus sein.`;
- kein laufendes Update des IOmcu und keine Panel-Installation im Gang.

Der Download braucht außerdem, dass keine Platinenzeichnung geladen oder gespeichert wird.

## Die Suche

![Eine laufende Suche](view:updates-checking)

Eine ungerade Minor-Nummer ist ein Testing-Release, auf GitHub als Pre-Release veröffentlicht (0.21.x). Eine gerade Minor-Nummer ist ein Stable-Release (0.22.x).

Das Panel verbindet sich mit dem Heimnetz, sendet eine HTTPS-Anfrage (HTTP über Transport Layer Security) und liest den Tag des neuesten Release aus der Antwort. Danach geht der Funk aus. Verbinden, Adresse und Anfrage dauern zusammen höchstens 30 s. Die Karte zeigt den Schritt und die verbleibenden Sekunden.

| RELEASES | Anfrage | Was gelesen wird |
|---|---|---|
| STABLE | `GET https://github.com/subtilitas/rcbench/releases/latest` | der Tag aus dem `Location`-Header der Antwort; das Latest-Release von GitHub ist nie ein Pre-Release |
| TESTING | `GET https://api.github.com/repos/subtilitas/rcbench/releases?per_page=10` | `tag_name` jedes der 10 nach Datum neuesten Releases, Testing oder Stable, aus der JSON-Antwort (JavaScript Object Notation); genommen wird die höchste Version unter denen, die kein Entwurf sind, gleich in welcher Reihenfolge; gelesen in Stücken von 4096 Bytes, höchstens 262 144 Bytes, und nicht gespeichert; ein ungültiges Release lehnt die ganze Antwort ab |

In beiden Einstellungen wird ein Release nur angeboten, wenn es zur Release-Linie 0.22 gehört und neuer als der laufende Build ist, siehe [Die Release-Linie](#die-release-linie).

![Ein neueres Release](view:updates-newer)
![Aktuell](view:updates-current)
![Ein Testing-Release, gefunden mit TESTING](view:updates-testing)

| Karte | Bedeutung |
|---|---|
| `Version 0.20.0 ist verfügbar.` | das neueste Release ist neuer als der laufende Build; das Halten erscheint |
| `Aktuell. Panel-Version: 0.19.0.` | das neueste Release ist die laufende Version oder älter |
| `Kein neueres Release für das ESP32-S3-Panel. Panel-Version: 0.22.0.` | das neueste Release ist neuer als der laufende Build und liegt hinter der Release-Linie; es wird nicht angeboten, und die Erinnerung beim Start öffnet sich dafür nicht |
| `Verbindung fehlgeschlagen. Passwort abgelehnt.`, `Verbindung fehlgeschlagen. Netzwerk nicht gefunden.`, `Keine Verbindung. Nach 15 s noch keine IP-Adresse.`, `Verbindung zum Heimnetz verloren.` | das Verbinden scheiterte, wie unter Dateien und Update |
| `Keine Antwort innerhalb von 30 s.` | die 30 s sind abgelaufen |
| `Keine Verbindung zu GitHub.` | Namensauflösung, Verbindung oder Zertifikatsprüfung scheiterten |
| `GitHub hat die Anfrage abgelehnt: HTTP 404.` | eine andere Antwort als ein Redirect, oder mit TESTING eine andere als 200 |
| `GitHub-Anfragelimit erreicht: HTTP 403.` | TESTING: GitHub antwortete mit 403 oder 429; ohne Konto nimmt es 60 API-Anfragen pro Stunde und Adresse an |
| `Die eigene Version des Panels ist nicht MAJOR.MINOR.PATCH: kein Update angeboten.` | die Version des laufenden Builds ist nicht lesbar; nichts wird angeboten |
| `Unerwartete Antwort von GitHub.` | der `Location`-Header, oder mit TESTING die Antwort, fehlt oder ist ungültig; eine zweite Zeile sagt, was |
| `WLAN ausgeschaltet: Prüfstand ARMED.` und ähnliche | ARMED, der Netzteilausgang, ein Log-Lauf oder STOP beendeten die Suche |
| `Abgebrochen: ...` | WI-FI, das Update des IOmcu oder ABBRECHEN beendeten die Suche |

![Eine Suche ohne Antwort](view:updates-failed)

Der Tag lautet `v` und MAJOR.MINOR.PATCH, nur Ziffern, jede Zahl von 0 bis 65535 in höchstens 5 Ziffern und ohne führende Null. Ein Release ist neuer, wenn sein MAJOR größer ist, oder MAJOR gleich und MINOR größer, oder beide gleich und PATCH größer.

## Die Release-Linie

0.22 ist das letzte Release für das ESP32-S3-Panel. Spätere Releases sind für das 7-Zoll-Display-Board mit ESP32-P4 von Waveshare. Der Fuß der Karte sagt das in seinen ersten Zeilen.

| GitHub nennt | Panel mit 0.22.0 |
|---|---|
| `v0.22.1` | angeboten |
| `v0.22.0` | aktuell; ein älteres Release wird nie angeboten |
| `v0.23.0`, `v0.24.0`, `v1.0.0` | `Kein neueres Release für das ESP32-S3-Panel.` |
| ein ungültiger Tag, etwa `v0.22.1-rc1` | `Unerwartete Antwort von GitHub.` |

Liegt mit TESTING das höchste der 10 neuesten Releases hinter der Linie, wird an seiner Stelle das höchste Release der Linie unter ihnen angeboten, sobald es neuer als der laufende Build ist.

## Der Download

Das Halten über 2 s ist die einzige Bestätigung: Es folgt keine Frage. Das Panel verbindet sich innerhalb von 30 s mit dem Heimnetz und lädt `SHA256SUMS` und `rcbench-panel-vX.Y.Z.bin` des Release über HTTPS. Es folgt höchstens 3 Redirects, nur zu `github.com`, `release-assets.githubusercontent.com` und `objects.githubusercontent.com`.

1. `SHA256SUMS` hat höchstens 4096 Bytes und genau eine Zeile für das Image.
2. Das Image kündigt zuerst seine Größe an; ein Image ohne Größe, mit 0 Bytes oder über 4.063.232 Bytes wird abgelehnt. Es liegt im PSRAM (Pseudo-Static Random-Access Memory).
3. Der SHA-256 (Secure Hash Algorithm, 256 Bit) des Image gleicht seiner Zeile in `SHA256SUMS`.
4. Jede Prüfung eines hochgeladenen Image läuft, und das Image nennt die Version des Release.

Erst dann schreibt das [Panel-Update](update.md) es, so wie INSTALL. Vorher berührt nichts den Flash.

![Der Download](view:panel-download)

ABBRECHEN beendet den Download. STOP, ARMED, der Netzteilausgang, ein Log-Lauf, eine geladene Platinenzeichnung und 180 s nach dem Halten beenden ihn auch. Der laufende Build bleibt; SCHLIESSEN geht zurück zu UPDATES, oder zum Menü, wenn das Halten das der Erinnerung war.

![Ein abgebrochener Download](view:panel-download-failed)

## Der IOmcu

![Ein anderer Build auf dem IOmcu](view:updates-coproc)

Unter den Tasten nennt UPDATES die Version, die der IOmcu laufen hat (`Läuft: 0.17.0`), und die Version des Images, das dieses Panel mitbringt (`Auf diesem Panel: 0.18.0`). 2 s HALTEN: IOMCU AKTUALISIEREN startet das Update, das die Frage beim Link-up startet, mit demselben Fortschritt. Endet das Update, oder verfiel der START, kehrt der Bildschirm zu UPDATES zurück.

Das Halten braucht, was die Frage braucht: einen IOmcu mit einem anderen Build, den Prüfstand SAFE, den Netzteilausgang aus, den WLAN-Funk ganz aus und keine Panel-Installation im Gang. Sonst ist es grau, und die Zeile darüber sagt, warum, das Erste davon, das gilt:

| Zeile | Bedingung |
|---|---|
| `Dieses Panel enthält keine IOmcu-Firmware.` | das Panel ist ohne das Image des IOmcu gebaut |
| `Wartet auf das Ende des IOmcu-Updates.` | ein Update läuft, oder sein START ist noch nicht angenommen |
| `Kein IOmcu antwortet.` | keine Verbindung; `Läuft: unbekannt` |
| `Wartet auf die Build-Prüfung des IOmcu.` | ein IOmcu antwortet; sein Build ist noch nicht verglichen |
| `Build des IOmcu wird geprüft.` | der SHA-256-Vergleich läuft |
| `Build-Prüfung fehlgeschlagen. Sie läuft bei der nächsten Verbindung erneut.` | der Vergleich endete ohne Antwort |
| `Kein Update über CAN. Firmware einmal über USB installieren.` | der IOmcu hat keine Update-Pages oder keine Partitionstabelle |
| `Der IOmcu nutzt den Build dieses Panels.` | es gibt nichts zu senden |
| `Für das Update muss SAFE aktiv und der Netzteilausgang aus sein.` | ARMED, ein wartendes Arm, oder der Netzteilausgang ist an |
| `Wartet, bis das WLAN des Panels aus ist.` | eine Suche, ein Download oder [WLAN](wifi.md) hat den Funk an |
| `Wartet: Die Installation der Panel-Firmware läuft.` | eine Panel-Installation ist im Gang |

Ist das Halten aktiv, lautet die Zeile `Ausgänge bleiben aus. ARMED ist bis zum Ende des Updates gesperrt.`, oder `Die Firmware auf diesem Panel ist älter.` in der Warnfarbe für ein älteres Image.

## Aktuelle Einschränkungen

- Wie lange Suche und Download in einem echten Netz dauern, ist nicht gemessen.
- TESTING: GitHub beantwortet ohne Konto 60 API-Anfragen pro Stunde und öffentlicher Adresse, geteilt von allen Geräten dahinter. Das Panel wiederholt keine Anfrage.
- TESTING liest die 10 nach Datum neuesten Releases. Ein Release, das nach Datum älter ist, sieht das Panel nicht, wie hoch seine Version auch ist.
- STABLE liest nur das Latest-Release von GitHub. Ist ein späteres Stable-Release veröffentlicht, ist ein Patch-Release 0.22.x nicht mehr Latest, und nur TESTING findet es.
- Die Daten der Zertifikate werden nicht geprüft: Das Panel hat keine Uhr.
- Ein Release, dessen Dateien nicht `rcbench-panel-vX.Y.Z.bin` und `SHA256SUMS` heißen, lässt sich nicht laden.
