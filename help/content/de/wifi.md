---
screen: WIFI
title: Dateien und Update
order: 51
---

# Dateien und Update

FILES & UPDATE betreibt das WLAN des Panels für zwei Aufgaben im Browser: jede Datei der SD-Karte (Secure Digital) zum Herunterladen und ein Firmware-Image des Panels zum Hochladen. Der Bildschirm öffnet aus [LOGS](logs.md), mit WLAN in der unteren Reihe. Das Funkmodul läuft nur, solange dieser Bildschirm offen ist. Bei jedem Start des Panels ist es aus.

## Der Startbildschirm

![Kein Heimnetz gespeichert](view:wifi)

| Taste | Was sie tut |
|---|---|
| ACCESS POINT | startet das eigene Netz des Panels |
| HEIMNETZ VERBINDEN | verbindet mit dem gespeicherten Netz; grau, solange keins gespeichert ist |
| HEIMNETZ WÄHLEN | sucht und listet die Netze in Reichweite |
| HEIMNETZ VERGESSEN | löscht das gespeicherte Netz und schaltet den Zugangscode wieder ein; grau, solange keins gespeichert ist |
| ZUGANGSCODE: EIN, AUS | ob die Browserseite nach dem Zugangscode fragt |

## Wenn das Funkmodul nicht starten darf

![Das Funkmodul darf nicht starten, nachdem es bei einem Schalten auf ARMED ausging](view:wifi-blocked)

Das Funkmodul startet nur mit dem Prüfstand SAFE, abgeschaltetem Netzteilausgang und ohne offenen Log-Lauf. Sonst sind die Tasten, die es starten, grau, und die Karte sagt, was fehlt.

Das Funkmodul geht von selbst aus, und die Karte nennt den Grund in einer Zeile:

| Zeile | Ursache |
|---|---|
| `WLAN ging aus: ARMED.` | ein Schalten auf ARMED |
| `WLAN ging aus: STOP.` | STOP |
| `WLAN ging aus: Netzteilausgang an.` | der Netzteilausgang wurde eingeschaltet |
| `WLAN ging aus: ein Log-Lauf begann.` | ein Lauf begann aufzuzeichnen |
| `WLAN ging aus: 10 min ohne Anfrage.` | 10 min keine Anfrage |
| `WLAN ging aus: 16 falsche Zugangscodes.` | 16 falsche Codes auf der Seite |
| `WLAN startete nicht.` | das Funkmodul ließ sich nicht starten |

Das Verlassen des Bildschirms schaltet das Funkmodul ebenfalls ab. Der Link zum Koprozessor und der Heartbeat laufen wie auf jedem anderen Bildschirm.

## ACCESS POINT

![Der Access Point läuft](view:wifi-ap)

ACCESS POINT startet das eigene Netz des Panels. Der Bildschirm zeigt, was ein Telefon oder ein Computer braucht.

| Eintrag | Wert |
|---|---|
| Band und Kanal | 2,4 GHz, Kanal 6 |
| Sicherheit | WPA2 (Wi-Fi Protected Access 2) Personal |
| Netzname | `rcbench-` und 4 Hex-Ziffern der MAC-Adresse (Media Access Control) des Panels, zum Beispiel `rcbench-3F7A` |
| Passwort | 8 Zeichen, bei jedem Start neu, ohne 0, 1, i, l und o |
| Geräte | 1 zur selben Zeit |
| Adresse | `http://192.168.4.1/` |
| Reichweite | nicht gemessen |

1. Am Telefon oder Computer das Netz verbinden, das der Bildschirm nennt, mit dem Passwort, das er zeigt.
2. `http://192.168.4.1/` öffnen.
3. Den Zugangscode eingeben, den der Bildschirm zeigt.

## Ein Heimnetz wählen

![Die Suche läuft](view:wifi-scanning)
![Die gefundenen Netze](view:wifi-scan)

Das Panel kann stattdessen einem WPA2- oder WPA3-Personal-Netz beitreten. HEIMNETZ WÄHLEN sucht höchstens 10 s und listet dann bis zu 16 Netze, das stärkste zuerst, mit ihrem Signal in dBm. Netze ohne Namen, offene Netze und Enterprise-Netze fehlen.

- Ein Tipp auf ein Netz öffnet die Tastatur für sein Passwort.
- VERSTECKTES NETZ fragt nach dem Namen eines Netzes, das sich nicht ankündigt, dann nach seinem Passwort.
- NEU SUCHEN sucht erneut. ABBRECHEN führt zurück.

## Die Tastatur

![Der Name des Netzes](view:wifi-key-ssid)

![Kleinbuchstaben](view:wifi-key-lower)
![ABC: Großbuchstaben](view:wifi-key-upper)
![#+=: Zeichen](view:wifi-key-marks)

Die Tastatur hat drei Ebenen: Kleinbuchstaben, `ABC` für Großbuchstaben und `#+=` für Zeichen. Jedes der 95 druckbaren ASCII-Zeichen (American Standard Code for Information Interchange) liegt auf einer Ebene. LEERZEICHEN ist die breite Taste. Ein Name fasst 1 bis 32 Bytes. Ein Passwort fasst 8 bis 63 Zeichen.

![Das Passwort, verborgen](view:wifi-key-pass)
![ZEIGEN](view:wifi-key-shown)
![Weniger als 8 Zeichen](view:wifi-key-short)

Das Passwort steht als Punkte, bis ZEIGEN gedrückt wird; VERBERGEN verbirgt es wieder. OK unter 8 Zeichen sagt das, und ein 64. Zeichen wird abgelehnt. OK speichert das Netz: Name und Passwort gehen in den NVS (nichtflüchtiger Speicher).

## Mit dem Heimnetz verbinden

![Ein Heimnetz ist gespeichert](view:wifi-home)

![Verbinden](view:wifi-joining)
![Verbunden](view:wifi-joined)

HEIMNETZ VERBINDEN verbindet mit dem gespeicherten Netz. Verbunden zeigt der Bildschirm das Netz, die Adresse des Panels und seinen Namen, `http://rcbench-3F7A.local/`. Der Name antwortet auf Computern, die `.local`-Namen auflösen; welche Telefone das tun, ist nicht gemessen. Die Adresse funktioniert auf jedem Gerät.

| Zeile | Bedingung |
|---|---|
| `Verbinden scheiterte: Passwort abgelehnt.` | das Netz hat das Passwort 3-mal in Folge abgelehnt, oder zuletzt abgelehnt, als die 20 s abliefen |
| `Verbinden scheiterte: Netz nicht gefunden.` | keine Verbindung innerhalb von 20 s, und der letzte Abbruch von anderer Art oder keiner |
| `Verbinden scheiterte: keine Adresse in 15 s.` | verbunden, aber keine Adresse innerhalb von 15 s |
| `Das Heimnetz ist weggefallen.` | verbunden, dann hat das Netz das Panel verloren |

Ein gescheitertes Verbinden bietet ACCESS POINT an. WLAN AUS schaltet das Funkmodul ab.

## Der Zugangscode

Die Browserseite fragt nach einem Code aus 8 Zeichen, bevor sie eine Datei listet oder einen Upload annimmt. Der Bildschirm zeigt den Code; er ist bei jedem Start des Funkmoduls neu. 16 falsche Codes schalten das Funkmodul ab. ZUGANGSCODE schaltet den Code für beide Wege ab; der Bildschirm zeigt dann `Zugangscode aus: die Seite ist für jedes Gerät im Netz offen.` Ein Upload installiert trotzdem erst nach dem Halten von 2 s am Panel.

## Die Browserseite

Die Seite spricht die Sprache des Panels.

- **Panel-Firmware**: die installierte Version, eine Dateiauswahl und Hochladen. Die Datei ist das Release-Asset `rcbench-panel-vX.Y.Z.bin`. Eine Datei größer als 4.063.232 Bytes wird abgelehnt, bevor ein Byte gesendet ist.
- **Dateien auf der Karte**: jede Datei mit Name, Größe in Bytes und Zeit; jeder Name lädt diese Datei herunter. Die Seite schreibt nichts auf die Karte.
- **Alles als eine ZIP-Datei herunterladen**: die ganze Karte, unkomprimiert gespeichert. Die Übertragungsrate ist nicht gemessen.

## Ein Image installieren

![Ein neueres Image wartet](view:wifi-question)

Ein Image, das die Seite empfangen hat, wartet am Panel als Frage mit beiden Versionen. INSTALL ist ein Halten von 2 s, `2 s HALTEN: INSTALL`; der Browser kann es nicht starten. ABBRECHEN verwirft das Image. Die Installation braucht den Prüfstand SAFE, den Netzteilausgang aus, keinen offenen Log-Lauf, keine Platinenzeichnung, die geladen oder gespeichert wird, und kein laufendes Koprozessor-Update.

![Ein älteres Image, und eine Zeichnung wird gespeichert](view:wifi-older)

Eine ältere Version ist erlaubt, und die Frage sagt, dass sie älter ist.

![Dieselbe Version, ein anderer Build](view:wifi-rebuild)
![Der Build, der läuft](view:wifi-installed)

Dieselbe Version aus einem anderen Build wird als Neuinstallation angeboten. Der Build, der schon läuft, wird nicht angeboten. Das Halten öffnet [PANEL UPDATE](update.md).

## Aktuelle Einschränkungen

- Der NVS ist nicht verschlüsselt. Das Passwort des Heimnetzes liegt im Klartext im Flash des Panels.
- Die Seite ist reines HTTP (Hypertext Transfer Protocol). In einem Heimnetz gehen der Zugangscode und jede Datei unverschlüsselt durchs Netz. Der Bildschirm sagt das, solange er verbunden ist.
- Ein hochgeladenes Image wird gegen seinen eigenen SHA-256-Hash geprüft, nicht gegen eine Signatur.
- Das Panel bleibt nur verbunden, solange dieser Bildschirm offen ist.
