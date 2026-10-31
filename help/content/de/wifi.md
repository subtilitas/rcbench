---
screen: WIFI
title: WLAN
order: 51
---

# WLAN

WI-FI betreibt das WLAN des Panels für zwei Aufgaben im Browser: jede Datei der SD-Karte (Secure Digital) zum Herunterladen und zum Ändern, so weit [Schreiben per Browser](#schreiben-per-browser) es erlaubt, und ein Firmware-Image des Panels zum Hochladen. Der Bildschirm ist ein Tab des Streifens der Kachel [LOGS](logs.md). Sein Home-Tag führt zum Menü. Über der Netzwerkliste oder einer Tastatur führt es zuerst eine Ebene zurück, wie ABBRECHEN: von einer Tastatur zur Liste, ohne das Getippte zu behalten, und von der Liste zu den Tasten. Über der Frage zu einem hochgeladenen Image ist es ABBRECHEN: Das Image wird verworfen, und der Bildschirm bleibt; ein zweites Tippen, solange das Image noch da ist, verlässt ihn. Das Funkmodul läuft nur, solange dieser Bildschirm offen ist: Ein Tab des Streifens, der ihn verlässt, schaltet es aus, wie das Home-Tag. Bei jedem Start des Panels ist es aus.

## Der Startbildschirm

![Kein Heimnetz gespeichert](view:wifi)

| Taste | Was sie tut |
|---|---|
| WLAN-HOTSPOT | startet das eigene Netz des Panels |
| MIT HEIMNETZ VERBINDEN | verbindet mit dem gespeicherten Netz; grau, solange keins gespeichert ist |
| HEIMNETZ WÄHLEN | sucht und listet die Netze in Reichweite |
| HEIMNETZ LÖSCHEN | löscht das gespeicherte Netz und schaltet den Zugangscode wieder ein; grau, solange keins gespeichert ist |
| ZUGANGSCODE: EIN, AUS | ob die Browserseite nach dem Zugangscode fragt |

## Wenn das Funkmodul nicht starten darf

![Das Funkmodul darf nicht starten, nachdem es bei einem Schalten auf ARMED ausging](view:wifi-blocked)

Das Funkmodul startet nur mit dem Prüfstand SAFE, abgeschaltetem Netzteilausgang und ohne offenen Log-Lauf. Sonst sind die Tasten, die es starten, grau, und die Karte sagt, was fehlt. Auf dem rbmod-Carrier zählen die 5V der Programmierports, GPIO 21, ebenfalls als Netzteilausgang: solange ein Programmer einen Port hält und bis SUPPLY sie als `AUS` liest.

Das Funkmodul geht von selbst aus, und die Karte nennt den Grund in einer Zeile:

| Zeile | Ursache |
|---|---|
| `WLAN ausgeschaltet: Prüfstand ARMED.` | ein Schalten auf ARMED |
| `WLAN ausgeschaltet: STOP.` | STOP |
| `WLAN ausgeschaltet: Netzteilausgang ein.` | der Netzteilausgang wurde eingeschaltet |
| `WLAN ausgeschaltet: Aufzeichnungslauf gestartet.` | ein Lauf begann aufzuzeichnen |
| `WLAN nach 10min ohne Anfrage ausgeschaltet.` | 10min keine Anfrage |
| `WLAN nach 16 falschen Zugangscodes ausgeschaltet.` | 16 falsche Codes auf der Seite |
| `WLAN konnte nicht starten.` | das Funkmodul ließ sich nicht starten |

Das Verlassen des Bildschirms schaltet das Funkmodul ebenfalls ab. Der Link zum IOmcu und der Heartbeat laufen wie auf jedem anderen Bildschirm.

## WLAN-HOTSPOT

![Der Access Point läuft](view:wifi-ap)

WLAN-HOTSPOT startet das eigene Netz des Panels. Der Bildschirm zeigt, was ein Telefon oder ein Computer braucht.

| Eintrag | Wert |
|---|---|
| Band und Kanal | 2,4GHz, Kanal 6 |
| Sicherheit | WPA2 (Wi-Fi Protected Access 2) Personal |
| Netzname | `rcbench-` und 4 Hex-Ziffern der MAC-Adresse (Media Access Control) des Panels, zum Beispiel `rcbench-3F7A` |
| Passwort | 8 Zeichen, bei jedem Start neu, ohne 0, 1, i, l und o |
| Geräte | 1 zur selben Zeit |
| IP-Adresse | `http://192.168.4.1/` |
| Reichweite | nicht gemessen |

1. Am Telefon oder Computer das Netz verbinden, das der Bildschirm nennt, mit dem Passwort, das er zeigt.
2. `http://192.168.4.1/` öffnen.
3. Den Zugangscode eingeben, den der Bildschirm zeigt.

## Ein Heimnetz wählen

![Die Suche läuft](view:wifi-scanning)
![Die gefundenen Netze](view:wifi-scan)

Das Panel kann stattdessen einem WPA2- oder WPA3-Personal-Netz beitreten. HEIMNETZ WÄHLEN sucht höchstens 10s und listet dann bis zu 16 Netze, das stärkste zuerst, mit ihrem Signal in dBm. Netze ohne Namen, offene Netze und Enterprise-Netze fehlen.

- Ein Tipp auf ein Netz öffnet die Tastatur für sein Passwort.
- VERSTECKTES NETZWERK fragt nach dem Namen eines Netzes, das sich nicht ankündigt, dann nach seinem Passwort.
- NEU SUCHEN sucht erneut. ABBRECHEN führt zurück.

`Kein WPA2- oder WPA3-Netzwerk gefunden.` heißt: Die Suche ist fertig und hat kein Netz für die Liste gefunden. Eine gescheiterte Suche läuft erneut, höchstens 3 Suchen in den 10s. `WLAN konnte nicht starten.` an der Stelle der Liste heißt: Keine Suche wurde fertig; NEU SUCHEN versucht es erneut.

## Die Tastatur

![Der Name des Netzes](view:wifi-key-ssid)

![Kleinbuchstaben](view:wifi-key-lower)
![ABC: Großbuchstaben](view:wifi-key-upper)
![#+=: Zeichen](view:wifi-key-marks)

Die Tastatur hat drei Ebenen: Kleinbuchstaben, `ABC` für Großbuchstaben und `#+=` für Zeichen. Jedes der 95 druckbaren ASCII-Zeichen (American Standard Code for Information Interchange) liegt auf einer Ebene. LEERZEICHEN ist die breite Taste. Ein Name fasst 1 bis 32 Bytes. Ein Passwort fasst 8 bis 63 Zeichen.

![Das Passwort, verborgen](view:wifi-key-pass)
![ANZEIGEN](view:wifi-key-shown)
![Weniger als 8 Zeichen](view:wifi-key-short)

Das Passwort steht als Punkte, bis ANZEIGEN gedrückt wird; VERBERGEN verbirgt es wieder. OK unter 8 Zeichen sagt das, und ein 64. Zeichen wird abgelehnt. OK speichert das Netz: Name und Passwort gehen in den NVS (nichtflüchtiger Speicher).

## Mit dem Heimnetz verbinden

![Ein Heimnetz ist gespeichert](view:wifi-home)

![Verbinden](view:wifi-joining)
![Verbunden](view:wifi-joined)

MIT HEIMNETZ VERBINDEN verbindet mit dem gespeicherten Netz. Verbunden zeigt der Bildschirm das Netz, die Adresse des Panels und seinen Namen, `http://rcbench-3F7A.local/`. Der Name antwortet auf Computern, die `.local`-Namen auflösen; welche Telefone das tun, ist nicht gemessen. Die Adresse funktioniert auf jedem Gerät.

| Zeile | Bedingung |
|---|---|
| `Verbindung fehlgeschlagen. Passwort abgelehnt.` | das Netz hat das Passwort 3-mal in Folge abgelehnt, oder zuletzt abgelehnt, als die 20s abliefen |
| `Verbindung fehlgeschlagen. Netzwerk nicht gefunden.` | keine Verbindung innerhalb von 20s, und der letzte Abbruch von anderer Art oder keiner |
| `Keine Verbindung. Nach 15s noch keine IP-Adresse.` | verbunden, aber keine Adresse innerhalb von 15s |
| `Verbindung zum Heimnetz verloren.` | verbunden, dann hat das Netz das Panel verloren |

Ein gescheitertes Verbinden bietet WLAN-HOTSPOT an. WLAN AUS schaltet das Funkmodul ab.

## Der Zugangscode

Die Browserseite fragt nach einem Code aus 8 Zeichen, bevor sie eine Datei listet, die Karte ändert oder einen Upload annimmt. Der Bildschirm zeigt den Code; er ist bei jedem Start des Funkmoduls neu. 16 falsche Codes schalten das Funkmodul ab. ZUGANGSCODE schaltet den Code für beide Wege ab; der Bildschirm zeigt dann `Zugangscode aus. Jedes Gerät im Netzwerk kann die Seite öffnen.` Ein Upload installiert trotzdem erst nach dem Halten von 2s am Panel.

## Schreiben per Browser

![Schreiben per Browser HALTEN, vor dem Halten](view:wifi-writes-hold)

SETUP > SD CARD > Schreiben per Browser legt fest, was die Seite braucht, um die Karte zu ändern:

| Wert | Ein Schreibzugriff braucht |
|---|---|
| OFFEN (Vorgabe) | nur die Seite: den Zugangscode, solange ZUGANGSCODE an ist |
| ZUGANGSCODE | den Zugangscode, auch mit ZUGANGSCODE aus; Lesen braucht dann keinen, und die Seite fragt beim ersten Schreiben nach dem Code |
| HALTEN | ein Halten von 2s auf `2s HALTEN: BROWSER DARF SCHREIBEN` neben WLAN AUS, einmal je Sitzung des Funkmoduls |
| AUS | kein Schreibzugriff wird angenommen |

Eine Zeile unter den Zeilen sagt, was gilt, zum Beispiel `Der Browser darf auf die Karte schreiben: bisher 3 Schreibvorgänge.` Nach dem Halten heißt die Taste `BROWSER-SCHREIBEN AUS`; ein Tipp beendet das Schreiben. Es endet auch, wenn das Funkmodul abschaltet, und die nächste Sitzung beginnt ohne.

![Nach dem Halten, drei Schreibvorgänge](view:wifi-writes-allowed)

Jeder Schreibzugriff wird geprüft wie die von FILES: Prüfstand SAFE, kein Lauf wird aufgezeichnet; die Ordner des Prüfstands, die Vermerke der Servoprogrammierung und die Dateien, die der Logger schreibt, bleiben unberührt; ein Name folgt der Tastatur des Panels. Ein Wechsel zu ARMED hält einen Schreibzugriff an seinen nächsten 16KiB an und verwirft seine `.NEW`-Datei. Solange FILES am Panel an der Karte arbeitet, wartet ein Schreibzugriff bis zu 2s; dann versucht die Seite es nach 2s noch einmal, höchstens 3 Mal.

## Die Browserseite

Die Seite spricht die Sprache des Panels.

- **Panel-Firmware**: die installierte Version, eine Dateiauswahl und Hochladen. Die Datei ist das Release-Asset `rcbench-panel-p4-vX.Y.Z.bin`. Eine Datei größer als 8.126.464 Bytes wird abgelehnt, bevor ein Byte gesendet ist.
- **Dateien auf der Karte**: die Karte in zwei Spalten, wie [FILES](files.md) sie zeigt: links das Stammverzeichnis, rechts der Ordner User-Logs, 48 Einträge je Seite mit VORIGE und NÄCHSTE. Ein Tipp wählt eine Zeile; AKTUALISIEREN, INFO, HERUNTERLADEN und ÖFFNEN wirken auf sie, und eine gedimmte Taste sagt beim Tipp, warum. ÖFFNEN geht in einen Ordner oder zeigt eine Textdatei nur lesend, gefärbt wie am Panel, mit `ZEILE n VON m` gezählt wie im Betrachter des Panels; eine Datei über 1MiB, eine, die der Logger schreibt, und die Vermerke der Servoprogrammierung lassen sich nur herunterladen. INFO nennt die Zeit in der Zone von SETUP > PANEL, Zeitzone, `nicht bekannt` für ein Datum in 1980. Die Seite folgt dem hellen oder dunklen Schema des Browsers in den Theme-Farben des Panels. Eine zweite Reihe hält HOCHLADEN, NEUER ORDNER, NEUE TEXTDATEI, UMBENENNEN, VERSCHIEBEN und LÖSCHEN, gedimmt mit dem Grund, solange [Schreiben per Browser](#schreiben-per-browser) oder der Prüfstand sie ablehnt. HOCHLADEN sendet eine Textdatei bis 1MiB in den Ordner der aktiven Spalte, mit Fortschrittsbalken, und fragt, bevor es eine Datei ersetzt; eine Datei, die sich auf der Karte ändert, während die Frage offen ist, wird nicht ersetzt. VERSCHIEBEN legt den gewählten Eintrag in den Ordner der anderen Spalte. LÖSCHEN fragt zuerst und nimmt den Bericht eines Laufs mit; ein Ordner wird nur leer verschoben oder gelöscht. Ein Name enthält nur ASCII (American Standard Code for Information Interchange): Eine Datei mit einem deutschen Buchstaben im Pfad wird auf FILES umbenannt, verschoben und gelöscht. Eine Datei, die die Seite geöffnet hat und die sich seitdem auf der Karte geändert hat, wird mit `Seit dem Lesen auf der Karte geändert. Neu lesen.` abgelehnt. BEARBEITEN im Betrachter öffnet den Text im Editor des Browsers, gefärbt wie im Betrachter; SPEICHERN schreibt ihn mit seinen eigenen Zeilenenden und seinem BOM (Byte Order Mark) zurück, und nur über die Datei, wie die Seite sie gelesen hat: Eine Änderung am Panel oder anderswo seitdem fragt BLEIBEN, NEU LESEN oder ÜBERSCHREIBEN. ÜBERSCHREIBEN braucht einen Druck von 2s und speichert über die Datei, die das abgelehnte SPEICHERN fand; eine weitere Änderung in dieser Zeit wird ebenso abgelehnt, und eine fehlende Datei lässt sich nicht überschreiben. Eine KST-Datei verliert beim SPEICHERN ihre Prüfsummenzeile, wie am Panel, und ein ESC-Profil sagt, dass es beim nächsten Start gelesen wird. Eine Datei mit gemischten Zeilenenden wird mit LF gespeichert; eine Datei, die kein UTF-8 ist, wird nur am Panel bearbeitet. Mit Schreiben per Browser ZUGANGSCODE fragt SPEICHERN an Ort und Stelle nach dem Code und behält den Text. SCHLIESSEN mit ungespeicherten Änderungen fragt zuerst. Die Seite braucht JavaScript.
- **Alles als eine ZIP-Datei herunterladen**: die ganze Karte, unkomprimiert gespeichert. Eine Datei, die durch Anhängen wächst, wird gesendet, wie sie beim Start des Downloads war; eine Datei, die dabei hinzukommt, fehlt. Eine Datei, die während des Downloads gelöscht oder umbenannt wird, beendet ihn als fehlgeschlagen; das Rotieren von `BENCH/RCBENCH.LOG` bei 1MiB ist ein Umbenennen. In einem Heimnetz kam eine Datei von 16MiB mit 688-757kB/s herunter, und ein Image von 2,56MB ging mit 556-599kB/s hinauf. Die Seite selbst, 18,9kB mit ihrer alten Kartenliste, brauchte 4,54s; die Seite mit den zwei Spalten und dem Editor hat etwa 60kB, ihre Zeit ist nicht gemessen.

Die Seite beantwortet eine Anfrage nach der anderen: Eine Anfrage während eines Downloads wartet auf dessen Ende. Ein Download, von dem der Browser 30s lang nichts annimmt, endet, und der Browser meldet ihn als fehlgeschlagen.

## Ein Image installieren

![Ein neueres Image wartet](view:wifi-question)

Ein Image, das die Seite empfangen hat, wartet am Panel als Frage mit beiden Versionen. INSTALL ist ein Halten von 2s, `2s HALTEN ZUM INSTALLIEREN`; der Browser kann es nicht starten. ABBRECHEN verwirft das Image. Die Installation braucht den Prüfstand SAFE, den Netzteilausgang aus, keinen offenen Log-Lauf, keine Platinenzeichnung, die geladen oder gespeichert wird, und kein laufendes IOmcu-Update.

![Ein älteres Image, und eine Zeichnung wird gespeichert](view:wifi-older)

Eine ältere Version ist erlaubt, und die Frage sagt, dass sie älter ist.

![Dieselbe Version, ein anderer Build](view:wifi-rebuild)
![Der Build, der läuft](view:wifi-installed)

Dieselbe Version aus einem anderen Build wird als Neuinstallation angeboten. Der Build, der schon läuft, wird nicht angeboten. Das Halten öffnet [PANEL UPDATE](update.md).

## Aktuelle Einschränkungen

- Der NVS ist nicht verschlüsselt. Das Passwort des Heimnetzes liegt im Klartext im Flash des Panels.
- Die Seite ist reines HTTP (Hypertext Transfer Protocol). In einem Heimnetz gehen der Zugangscode und jede Datei unverschlüsselt durchs Netz. Der Bildschirm sagt das, solange er verbunden ist.
- Ein Gerät, das diesen Verkehr mitliest, kann einen eigenen Schreibzugriff senden, solange Schreiben erlaubt ist.
- Der Editor des Browsers speichert eine Datei mit gemischten Zeilenenden mit LF.
- Ein hochgeladenes Image wird gegen seinen eigenen SHA-256-Hash geprüft, nicht gegen eine Signatur.
- Das Panel bleibt nur verbunden, solange dieser Bildschirm offen ist.
