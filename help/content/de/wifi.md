---
screen: WIFI
title: WLAN
order: 51
---

# WLAN

WI-FI ermöglicht den Browserzugriff auf das Panel. Du kannst Dateien der SD-Karte (Secure Digital) herunterladen, sie gemäß [Schreiben per Browser](#schreiben-per-browser) bearbeiten und Panel-Firmware hochladen. Öffne WI-FI über [SETUP](setup.md), [LOGS](logs.md) oder [Updates](updates.md). Das Home-Feld führt zur aufrufenden Seite zurück.

In Netzwerkliste und Tastatur wirkt Home wie ABBRECHEN: Die Tastatureingabe wird verworfen, die Liste erscheint; von dort geht es zu den Haupttasten zurück. Bei einer Rückfrage zum hochgeladenen Image verwirft Home das Image und bleibt auf WI-FI. Ein zweiter Tipp, während das Image noch vorhanden ist, verlässt die Seite. Dabei schaltet das Funkmodul aus. Auch bei jedem Panelstart ist es ausgeschaltet.

## Der Startbildschirm

![Kein Heimnetz gespeichert](view:wifi)

| Taste | Was sie tut |
|---|---|
| WLAN-HOTSPOT | startet das eigene Netz des Panels |
| MIT HEIMNETZ VERBINDEN | verbindet mit dem gespeicherten Netz; grau, solange keins gespeichert ist |
| HEIMNETZ WÄHLEN | sucht und listet die Netze in Reichweite |
| HEIMNETZ LÖSCHEN | löscht das gespeicherte Netz und schaltet den Zugangscode wieder ein; grau, solange keins gespeichert ist |
| ZUGANGSCODE: EIN, AUS | ob die Browserseite nach dem Zugangscode fragt |
| HOTSPOT-PASSWORT: ZUFALL, FEST | öffnet das [Hotspot-Passwort](#hotspot-passwort-und-zugangscode) |
| ZUGANGSCODE: ZUFALL, FEST | öffnet den [Zugangscode](#hotspot-passwort-und-zugangscode) |

## Wenn das Funkmodul nicht starten darf

![Das Funkmodul darf nicht starten, nachdem es bei einem Schalten auf ARMED ausging](view:wifi-blocked)

Das Funkmodul benötigt SAFE, einen ausgeschalteten Netzteilausgang und keine offene Aufzeichnung. Andernfalls sind die Starttasten grau, und die Karte nennt den Grund. Auf dem rbmod-Carrier zählt die 5V-Schiene der Programmierports an GPIO 21 ebenfalls als Netzteilausgang. Sie sperrt WLAN, solange eine Servoprogrammierung einen Port belegt und bis SUPPLY die Schiene als `AUS` meldet.

Das Funkmodul schaltet in folgenden Fällen selbstständig aus; die Karte nennt den Grund:

| Zeile | Ursache |
|---|---|
| `WLAN ausgeschaltet: Prüfstand ARMED.` | ein Schalten auf ARMED |
| `WLAN ausgeschaltet: STOP.` | STOP |
| `WLAN ausgeschaltet: Netzteilausgang ein.` | der Netzteilausgang wurde eingeschaltet |
| `WLAN ausgeschaltet: Aufzeichnungslauf gestartet.` | ein Lauf begann aufzuzeichnen |
| `WLAN nach 10min ohne Anfrage ausgeschaltet.` | 10min keine Anfrage |
| `WLAN nach 16 falschen Zugangscodes ausgeschaltet.` | 16 falsche Codes auf der Seite |
| `WLAN konnte nicht starten.` | das Funkmodul ließ sich nicht starten |

Auch das Verlassen der Seite schaltet das Funkmodul aus. Verbindung zum IOmcu und Heartbeat laufen wie auf allen anderen Seiten weiter.

## WLAN-HOTSPOT

![Der Access Point läuft](view:wifi-ap)

WLAN-HOTSPOT startet das eigene Netzwerk des Panels. Die Seite zeigt die Zugangsdaten für Telefon oder Computer.

| Eintrag | Wert |
|---|---|
| Band und Kanal | 2,4GHz, Kanal 6 |
| Sicherheit | WPA2 (Wi-Fi Protected Access 2) Personal |
| Netzname | `rcbench-` und 4 Hex-Ziffern der MAC-Adresse (Media Access Control) des Panels, zum Beispiel `rcbench-3F7A` |
| Passwort | ZUFALL: 8 Zeichen, bei jedem Start neu, ohne 0, 1, i, l und o. Fest: die 8 bis 63 Zeichen von HOTSPOT-PASSWORT |
| Geräte | 1 zur selben Zeit |
| IP-Adresse | `http://192.168.4.1/` |
| Reichweite | nicht gemessen |

1. Verbinde Telefon oder Computer mit dem angezeigten Netzwerk und Passwort.
2. Öffne `http://192.168.4.1/`.
3. Gib den auf dem Panel angezeigten Zugangscode ein.

## HOTSPOT-PASSWORT und ZUGANGSCODE

![HOTSPOT-PASSWORT fest, ZUGANGSCODE Zufall](view:wifi-kept-buttons)
![Die Ansicht eines Zufallswerts](view:wifi-kept-pass)
![Die Ansicht eines festen Werts](view:wifi-kept-pass-set)
![Die Ansicht des Zugangscodes](view:wifi-kept-code)

HOTSPOT-PASSWORT schützt das Netzwerk von WLAN-HOTSPOT. ZUGANGSCODE regelt den Zugang zur Browserseite. Beide können bei jedem Funkstart neu erzeugt werden (ZUFALL) oder als eingegebener Wert Neustarts überdauern.

- WERT EINGEBEN öffnet die Tastatur für 8 bis 63 Zeichen: Buchstaben, Ziffern, Leer- und Satzzeichen.
- Tippe auf ZURÜCK AUF ZUFALL, um ohne Eingabe zur Zufallserzeugung zu wechseln. Bei ZUFALL ist die Taste grau.
- Änderungen gelten ab dem nächsten WLAN-Start.
- Beide Werte lassen sich auf dem Panel anzeigen und zurücksetzen, ohne Browser. HEIMNETZ LÖSCHEN behält sie.
- Solange ein fester Wert sichtbar ist, nimmt das Panel keinen Screenshot auf.

![Die Tastatur eines festen Werts](view:wifi-key-kept)
![Weniger als 8 Zeichen](view:wifi-key-kept-short)

## Ein Heimnetz wählen

![Die Suche läuft](view:wifi-scanning)
![Die gefundenen Netze](view:wifi-scan)

Das Panel kann sich auch mit einem WPA2- oder WPA3-Personal-Netzwerk verbinden. HEIMNETZ WÄHLEN sucht höchstens 10s und listet bis zu 16 Netzwerke nach Signalstärke, mit Angaben in dBm. Namenlose, offene und Enterprise-Netzwerke werden nicht angezeigt.

- Tippe auf ein Netzwerk, um die Passworttastatur zu öffnen.
- VERSTECKTES NETZWERK fragt zuerst nach dem Namen, dann nach dem Passwort eines nicht ausgesendeten Netzwerks.
- AKTUALISIEREN sucht erneut, ABBRECHEN führt zurück.

`Kein WPA2- oder WPA3-Netzwerk gefunden.` bedeutet, dass eine Suche ohne passendes Ergebnis abgeschlossen wurde. Fehlgeschlagene Suchen werden wiederholt, höchstens 3-mal innerhalb von 10s. Bei `WLAN konnte nicht starten.` statt der Liste wurde keine Suche abgeschlossen. Versuche es mit AKTUALISIEREN erneut.

## Die Tastatur

![Der Name des Netzes](view:wifi-key-ssid)

![Kleinbuchstaben](view:wifi-key-lower)
![ABC: Großbuchstaben](view:wifi-key-upper)
![#+=: Zeichen](view:wifi-key-marks)

Die Tastatur hat drei Ebenen: Kleinbuchstaben, `ABC` für Großbuchstaben und `#+=` für Zeichen. Jedes der 95 druckbaren ASCII-Zeichen (American Standard Code for Information Interchange) liegt auf einer Ebene. LEERZEICHEN ist die breite Taste. Ein Name fasst 1 bis 32 Bytes. Ein Passwort fasst 8 bis 63 Zeichen.

![Das Passwort, verborgen](view:wifi-key-pass)
![ANZEIGEN](view:wifi-key-shown)
![Weniger als 8 Zeichen](view:wifi-key-short)

Das Passwort erscheint als Punkte. ANZEIGEN macht es sichtbar, VERBERGEN verdeckt es wieder. OK meldet Eingaben unter 8 Zeichen; ein 64. Zeichen wird abgelehnt. Bei gültiger Eingabe speichert OK Netzwerkname und Passwort im NVS (nichtflüchtiger Speicher).

## Mit dem Heimnetz verbinden

![Ein Heimnetz ist gespeichert](view:wifi-home)

![Verbinden](view:wifi-joining)
![Verbunden](view:wifi-joined)

MIT HEIMNETZ VERBINDEN öffnet die Verbindung zum gespeicherten Netzwerk. Danach zeigt die Seite Netzwerk, IP-Adresse und Panelnamen, etwa `http://rcbench-3F7A.local/`. Der Name funktioniert auf Computern mit `.local`-Namensauflösung; die Unterstützung auf Telefonen wurde nicht gemessen. Verwende die IP-Adresse, um die Namensauflösung zu umgehen.

| Zeile | Bedingung |
|---|---|
| `Verbindung fehlgeschlagen. Passwort abgelehnt.` | Passwort 3-mal in Folge abgelehnt oder zuletzt abgelehnt, als die Frist von 20s ablief. |
| `Verbindung fehlgeschlagen. Netzwerk nicht gefunden.` | Innerhalb von 20s keine WLAN-Assoziierung; letzter Abbruch anderer Art oder kein Abbruchgrund vorhanden. |
| `Keine Verbindung. Nach 15s noch keine IP-Adresse.` | WLAN-Verbindung aufgebaut, aber nach 15s keine Adresse erhalten. |
| `Verbindung zum Heimnetz verloren.` | Bestehende Verbindung vom Netzwerk getrennt. |

Nach einem fehlgeschlagenen Verbindungsaufbau wird WLAN-HOTSPOT angeboten. WLAN AUS schaltet das Funkmodul aus.

## Der Zugangscode

Vor Dateiliste, Kartenänderung oder Upload verlangt die Browserseite den auf dem Panel angezeigten Zugangscode. ZUFALL erzeugt bei jedem Funkstart 8 neue Zeichen; Codes früherer Sitzungen werden ungültig. Ein fester Code gilt bis zur Änderung weiter, auch für Browser, die ihn gespeichert haben, und Personen, die ihn kennen. Im Heimnetz wird er über unverschlüsseltes HTTP übertragen.

Nach 16 falschen Codes schaltet das Funkmodul aus, auch bei festem Code. Jede falsche Eingabe zählt. Ein falscher Code im Browser-Cookie zählt dagegen nur einmal. Ein offen gebliebener Reiter mit altem Code verbraucht daher nicht alle 16 Versuche.

ZUGANGSCODE kann die Prüfung für Hotspot und Heimnetz ausschalten. Dann steht `Zugangscode aus. Jedes Gerät im Netzwerk kann die Seite öffnen.` auf dem Panel. Eine hochgeladene Firmware erfordert weiterhin 2s Halten am Panel zur Installation.

## Schreiben per Browser

![Schreiben per Browser HALTEN, vor dem Halten](view:wifi-writes-hold)

SETUP > SD CARD > Schreiben per Browser legt fest, was die Seite braucht, um die Karte zu ändern:

| Wert | Ein Schreibzugriff braucht |
|---|---|
| OFFEN (Vorgabe) | nur die Seite: den Zugangscode, solange ZUGANGSCODE an ist |
| ZUGANGSCODE | den Zugangscode, auch mit ZUGANGSCODE aus; Lesen braucht dann keinen, und die Seite fragt beim ersten Schreiben nach dem Code |
| HALTEN | ein Halten von 2s auf `2s HALTEN: BROWSER DARF SCHREIBEN` neben WLAN AUS, einmal je Sitzung des Funkmoduls |
| AUS | kein Schreibzugriff wird angenommen |

Die Zeile darunter nennt die Schreibfreigabe, etwa `Der Browser darf auf die Karte schreiben: bisher 3 Schreibvorgänge.` Nach erfolgreichem Halten heißt die Taste `BROWSER-SCHREIBEN AUS`. Tippe darauf, um die Freigabe zu beenden. Auch das Ausschalten des Funkmoduls beendet sie. Jede neue Sitzung beginnt ohne die Haltefreigabe.

![Nach dem Halten, drei Schreibvorgänge](view:wifi-writes-allowed)

Browser-Schreibzugriffe unterliegen denselben Prüfungen wie FILES: SAFE, keine laufende Aufzeichnung, geschützte Prüfstandsordner und Programmiervermerke sowie Schutz offener Aufzeichnungsdateien. Namen folgen den Regeln der Paneltastatur. Ein Wechsel auf ARMED stoppt das Schreiben vor dem nächsten Block von 16KiB und löscht die `.NEW`-Datei. Belegt FILES die Karte, wartet die Anfrage bis zu 2s. Danach versucht die Seite es mit 2s Abstand höchstens 3-mal erneut.

## Die Browserseite

Die Browserseite verwendet die Panelsprache.

**Panel-Firmware** zeigt die installierte Version, eine Dateiauswahl und Hochladen. Wähle das Release-Asset `rcbench-panel-p4-vX.Y.Z.bin`. Dateien über 8.126.464 Bytes werden vor der Übertragung abgelehnt.

**Dateien auf der Karte** zeigt wie [FILES](files.md) zwei Spalten: links das Stammverzeichnis, rechts den Ordner Testaufzeichnungen, mit 48 Einträgen je Seite sowie VORIGE und NÄCHSTE. Wähle eine Zeile für AKTUALISIEREN, INFO, HERUNTERLADEN oder ÖFFNEN. Ein Tipp auf eine gedimmte Taste erklärt ihre Sperre. Die Seite folgt dem hellen oder dunklen Browserschema mit den Designfarben des Panels.

ÖFFNEN zeigt einen Ordner oder eine Textdatei schreibgeschützt, mit denselben Farben und derselben Zählung `ZEILE n VON m` wie am Panel. Dateien über 1MiB, laufende Aufzeichnungen und Programmiervermerke lassen sich nur herunterladen. INFO verwendet die Zeitzone aus SETUP > PANEL; bei Daten aus 1980 steht `nicht bekannt`.

Die zweite Tastenreihe enthält HOCHLADEN, NEUER ORDNER, NEUE TEXTDATEI, UMBENENNEN, VERSCHIEBEN und LÖSCHEN. Sperren durch [Schreiben per Browser](#schreiben-per-browser) oder den Prüfstand werden mit Grund angezeigt. HOCHLADEN sendet eine Textdatei bis 1MiB in den aktiven Ordner und zeigt den Fortschritt. Vor dem Ersetzen fragt es nach. Ändert sich die Zieldatei während der Rückfrage, wird sie nicht ersetzt. VERSCHIEBEN überträgt den gewählten Eintrag in den anderen Ordner. LÖSCHEN fragt zuerst und entfernt den zugehörigen Laufbericht mit. Ordner müssen zum Verschieben oder Löschen leer sein.

Namen erlauben nur ASCII (American Standard Code for Information Interchange). Verwende FILES am Panel zum Umbenennen, Verschieben oder Löschen von Pfaden mit deutschen Buchstaben. Eine seit dem Einlesen geänderte Datei wird mit `Die Daten auf der Karte haben sich seit dem Lesen geändert. Lies sie erneut ein.` abgelehnt.

BEARBEITEN öffnet den Browser-Texteditor mit denselben Farben wie der Betrachter. SPEICHERN erhält Zeilenenden und BOM (Byte Order Mark); gemischte Zeilenenden werden jedoch zu LF. Dateien außerhalb von UTF-8 lassen sich nur am Panel bearbeiten.

SPEICHERN prüft, ob auf der Karte noch die eingelesene Version liegt. Bei Änderungen am Panel oder anderswo erscheint BLEIBEN, NEU LESEN oder ÜBERSCHREIBEN. Halte ÜBERSCHREIBEN 2s, um die beim abgelehnten Speicherversuch gefundene Version zu ersetzen. Eine weitere Änderung während des Haltens führt erneut zur Ablehnung. Fehlende Dateien lassen sich nicht überschreiben.

Beim Speichern einer KST-Datei entfällt wie am Panel die Prüfsummenzeile. Bei ESC-Profilen weist die Seite auf das Einlesen beim nächsten Start hin. Mit Schreiben per Browser ZUGANGSCODE fragt SPEICHERN nach dem Code und behält den bearbeiteten Text. SCHLIESSEN fragt vor dem Verwerfen ungespeicherter Änderungen nach. Die Seite benötigt JavaScript.

**Alles als eine ZIP-Datei herunterladen** überträgt die gesamte Karte unkomprimiert. Dateien, die durch Anhängen wachsen, werden mit ihrer Größe vom Downloadbeginn gesendet; später hinzugekommene Dateien fehlen. Löschen oder Umbenennen während des Downloads lässt ihn fehlschlagen. Dazu zählt das Rotieren von `BENCH/RCBENCH.LOG` bei 1MiB.

In einem Heimnetz wurde eine Datei von 16MiB mit 688-757kB/s heruntergeladen und ein Image von 2,56MB mit 556-599kB/s hochgeladen. Die 18,9kB große Seite mit der früheren Kartenliste lud in 4,54s. Die Seite mit zwei Spalten und Editor ist etwa 60kB groß; ihre Ladezeit wurde nicht gemessen.

Der Server bearbeitet jeweils eine Anfrage. Während eines Downloads wartet die nächste Anfrage auf dessen Ende. Nimmt der Browser 30s lang keine Daten ab, endet die Übertragung mit einer Fehlermeldung im Browser.

## Ein Image installieren

![Ein neueres Image wartet](view:wifi-question)

Nach dem Upload zeigt das Panel eine Rückfrage mit laufender und hochgeladener Version. Halte `2s HALTEN ZUM INSTALLIEREN` für 2s zur Installation; der Browser kann sie nicht starten. ABBRECHEN verwirft das Image. Erforderlich sind SAFE, Netzteilausgang aus, keine offene Aufzeichnung, keine Platinenzeichnung im Lade- oder Speichervorgang und kein laufendes IOmcu-Update.

![Ein älteres Image, und eine Zeichnung wird gespeichert](view:wifi-older)

Ältere Versionen sind erlaubt und werden in der Rückfrage entsprechend gekennzeichnet.

![Dieselbe Version, ein anderer Build](view:wifi-rebuild)
![Der Build, der läuft](view:wifi-installed)

Ein anderer Build derselben Version wird zur Neuinstallation angeboten. Der bereits laufende Build wird nicht angeboten. Das Halten öffnet [PANEL UPDATE](update.md).

## Aktuelle Einschränkungen

- NVS ist nicht verschlüsselt. Heimnetzpasswort, festes HOTSPOT-PASSWORT und fester ZUGANGSCODE liegen im Klartext im Panel-Flash.
- Die Seite verwendet unverschlüsseltes HTTP (Hypertext Transfer Protocol). Im Heimnetz werden Zugangscode und Dateien unverschlüsselt übertragen. Die verbundene Seite weist darauf hin.
- Wer diesen Datenverkehr mitliest, kann während einer Schreibfreigabe eigene Schreibanfragen senden.
- Der Browsereditor vereinheitlicht gemischte Zeilenenden zu LF.
- Hochgeladene Images werden gegen ihren eigenen SHA-256-Hash geprüft, nicht gegen eine Signatur.
- Die Verbindung besteht nur, solange diese Seite geöffnet ist.
