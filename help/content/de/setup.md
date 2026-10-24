---
screen: SETUP
title: Setup
order: 60
---

# Setup

SETUP hält die Einstellungen des Panels für den ganzen Prüfstand in vier Kategorien: PANEL, ANSCHLÜSSE, SD-KARTE und SENSOREN. Die ESC-Einstellungen (Electronic Speed Controller) stehen unter [MOTOR & ESC, OPTIONEN](motor-settings.md). Der Bildschirm öffnet über die Kachel SETUP im [Menü](overview.md) auf einer Seite mit Kacheln, 3 mal 3. Die Kachel einer Kategorie öffnet ihre Liste über die ganze Breite des Bildschirms.

## Die Kacheln

![Die Kacheln, alles gespeichert](view:setup)

| Kachel | Zweite Zeile | Was sie öffnet |
|---|---|---|
| PANEL | `11 EINSTELLUNGEN` | die Liste von PANEL |
| ANSCHLÜSSE | `23 EINSTELLUNGEN` | die Liste von ANSCHLÜSSE |
| SD-KARTE | `4 EINSTELLUNGEN` | die Liste von SD-KARTE: welche Logs das Panel schreibt, und wohin |
| SENSOREN | `4 EINSTELLUNGEN`, und 9 mehr je bestücktem Baustein | [Sensoren](setup-sensors.md): die Strommonitore INA3221, ihre Shunts und was jeder Kanal misst |
| OUTPUTS | `PROTOKOLLE UND PINS` | [Ausgänge](outputs.md): die Rolle jedes IOmcu-Pins, als Liste |
| PICK A PIN | `AUF DER PLATINE` | die [Pin-Auswahl](picker.md): dasselbe, auf einer Zeichnung der Platine |
| WI-FI | `NETZWERK, DATEIEN, UPLOAD` | [WLAN](wifi.md): die Dateien der Karte und ein Upload der Panel-Firmware im Browser, und das Heimnetz |
| UPDATES | `VERSIONEN AUF GITHUB` | [Updates](updates.md): die Suche nach einem neueren Release der Panel-Firmware und den Schalter der Erinnerung |
| FILES | `ORDNER UND DATEIEN` | [Files](files.md): die Ordner und Dateien der Karte in zwei Spalten, zum Kopieren, Verschieben, Umbenennen und Löschen |

Kein Platz bleibt frei. Die Speichertaste steht unten rechts, wie auf jeder Liste. Eine ungespeicherte Änderung in einer beliebigen Kategorie zeigt sich auf ihr:

![Die Kacheln mit einem ungespeicherten Wert](view:setup-hub-dirty)

Das Home-Tag oben links führt eine Ebene nach der anderen zurück:

| Von | Das Home-Tag führt zu |
|---|---|
| der Liste einer Kategorie | den Kacheln; die Liste behält ihre Position |
| den Kacheln | dem Menü; mit einem ungespeicherten Wert zuerst der Warnung ([Mit ungespeicherten Werten verlassen](#mit-ungespeicherten-werten-verlassen)) |
| OUTPUTS, PICK A PIN, WI-FI, UPDATES, FILES | den Kacheln; WI-FI und FILES, aus LOGS geöffnet, führen zurück zu LOGS, OUTPUTS, aus ZIEL auf MOTOR & ESC oder SERVO geöffnet, dorthin zurück. Eine Liste oder eine Tastatur, die auf einem davon offen ist, schließt zuerst |

## Einen Wert ändern

![ANSCHLÜSSE, alles gespeichert](view:setup-interfaces)

Die Liste ist 984px breit und zeigt 6 Zeilen zu 64px. Darunter trägt eine Leiste, 48px hoch, den Namen der Kategorie, KATEGORIE ZURÜCKSETZEN und die Speichertaste. Die Leiste endet 4px über dem Band, das ein Alert unten zeigt; ein Alert verdeckt so weder eine Taste noch eine Zeile oder Schaltfläche.

Eine Zeile zeigt die Einstellung, eine Hilfezeile und ihren Wert mit Einheit. Eine Zahl hat die Tasten `-` und `+`; ein Schalter oder eine Auswahl hat `<` und `>`.

- **Tipp**: Eine Taste schaltet einmal, wenn der Finger innerhalb von 8px um den Druckpunkt auf der Taste abhebt.
- **Halten**: Eine gehaltene Taste schaltet zuerst nach 0,45s, dann 8-mal pro Sekunde und ab 2,25s 30-mal pro Sekunde.
- **Scrollen**: Ein senkrechtes Ziehen über mehr als 8px irgendwo auf der Liste scrollt sie und ändert keinen Wert.
- **Tastenfeld**: Ein Tippen auf den Wert einer Zahl, zwischen ihren Tasten `-` und `+`, öffnet das Tastenfeld über der Liste. Es nennt die Einstellung, ihren Bereich und ihre Einheit und nimmt so viele Stellen nach dem Punkt, wie der Schritt hat. OK setzt den Wert wie ein Schritt auf den näheren Schritt: 73% Helligkeit sind 75%. Ein Wert außerhalb des Bereichs lässt das Tastenfeld offen und nennt den Bereich in der Warnfarbe. Ein Wert im Bereich, den die Tasten nie erreichen, lässt es offen und nennt dort stattdessen den Grund in der Warnfarbe: `0 oder 5.0 bis 1000.0mΩ` für einen Shunt zwischen 0 und 5,0mΩ, `0 oder 10 bis 32767mA` für eine Stromgrenze zwischen 0 und 10mA, `Pins, die der Bus nimmt: 0 4 6 14 16 18 20 26` für einen Pin für Sensor-SDA, den der Bus nicht haben kann, `Diesen GPIO hält der andere PD-mini-Pin` für den GPIO des anderen PD-mini-Pins. Die nächste Taste bringt den Bereich zurück. ABBRECHEN und das Home-Tag ändern nichts. -1, ein nicht gesetzter Pin, wird mit `-` gesetzt: Das Tastenfeld eines Pins hat keine Taste `+/-`.
- Ein Finger zur selben Zeit: Ein zweiter Finger tut nichts, solange einer drückt oder scrollt.

Ein geänderter Wert gilt sofort. KATEGORIE ZURÜCKSETZEN setzt jeden Wert der offenen Kategorie auf seine Vorgabe zurück.

| Taste | Was sie tut |
|---|---|
| KATEGORIE ZURÜCKSETZEN | die Vorgaben der offenen Kategorie, sofort |
| GESPEICHERT, SPEICHERN, BEI STILLSTAND, NICHT GESPEICHERT | die Speichertaste; siehe unten |

## Die Werte behalten

![Ein geänderter Wert, SPEICHERN wird angeboten](view:setup-dirty)

Ein geänderter Wert wird erst in den Flash geschrieben, wenn SPEICHERN gedrückt wird. Ein Wert, der auf den gespeicherten zurückgestellt ist, ist keine Änderung: Die Taste lautet wieder `GESPEICHERT`, und ein angefordertes Schreiben entfällt.

| Beschriftung | Bedeutung |
|---|---|
| `GESPEICHERT` | nichts ist ungeschrieben; die Taste tut nichts |
| `SPEICHERN` | etwas ist ungeschrieben; ein Druck fordert das Schreiben an |
| `BEI STILLSTAND` | das Schreiben wartet auf einen ruhenden Prüfstand |
| `NICHT GESPEICHERT` | der Speicher hat das Schreiben abgelehnt; ein Druck versucht es erneut |

Geschrieben wird im ersten Frame mit dem Prüfstand SAFE, abgeschaltetem Netzteilausgang und ohne Platinenzeichnung, die geladen oder gespeichert wird. Bei ARMED lautet die Taste `BEI STILLSTAND`, bis der Prüfstand SAFE ist. Nicht gespeicherte Werte gelten, bis das Panel ausgeschaltet wird. Das Verlassen einer Liste oder der Kacheln schreibt nichts.

`NICHT GESPEICHERT` kann nicht sagen, wie viel den Flash erreicht hat: Eine Ablehnung mittendrin lässt die zuvor geschriebenen Schlüssel stehen; der nächste Start kann eine Mischung aus neuen und alten Werten laden. Nach einer Ablehnung bleibt die Taste bei `NICHT GESPEICHERT` oder `SPEICHERN`, bis ein Speichern gelingt, auch wenn jeder Wert zurückgestellt ist.

## Mit ungespeicherten Werten verlassen

![Ein ungespeicherter Wert, am Home-Tag](view:setup-leave)

Das Home-Tag auf den Kacheln führt zum Menü. Solange die Speichertaste `SPEICHERN` oder `NICHT GESPEICHERT` lautet, öffnet es zuerst eine Warnung. Die Warnung nennt jeden Wert, der vom gespeicherten abweicht, als `Name: gespeichert -> jetzt`, mit Wert und Einheit wie auf der Liste. Gespeichert sind die Werte, die beim Start geladen oder beim letzten gelungenen Speichern geschrieben wurden. Die Warnung nennt auch den Namen des Prüflings (DUT, device under test) und die Werte anderer Bildschirme, etwa SERVO, ESC STICK oder MOTOR & ESC: Ein Speichern behält sie alle.

| Taste | Was sie tut |
|---|---|
| BLEIBEN | schließt die Warnung; die Kacheln zeigen sich wieder, eine Liste behält Kategorie und Position |
| VERWERFEN | stellt jeden genannten Wert auf den gespeicherten zurück, wie ein Schritt von Hand, und öffnet dann das Menü |
| SPEICHERN | fordert das Schreiben an wie die Speichertaste und öffnet dann das Menü |

- VERWERFEN schickt einen Pin oder einen Port sofort zurück an den IOmcu. Eine zurückgestellte Verdrahtung des PD mini schaltet den Netzteilausgang ab, wie eine Änderung von Hand.
- Bei ARMED ist VERWERFEN abgeblendet und tut nichts; die Zeile `VERWERFEN wartet, bis der Prüfstand SAFE ist.` sagt das. BLEIBEN und SPEICHERN wirken.
- PWM-Ziel und Port-PWM werden nicht zurückgestellt, solange der Servo-Programmierer den Servo-Port hält; sie bleiben ungespeichert.
- Bei mehr als 10 Änderungen zeigen sich 9 Zeilen, und die letzte Zeile lautet bei 12 `und 3 weitere`.
- Nach einem abgelehnten Speichern steht unter der Liste die Zeile `Speichern fehlgeschlagen. Tippe erneut auf SPEICHERN.` VERWERFEN fordert dann zusätzlich das Schreiben der zurückgestellten Werte an, denn der Flash kann einen Teil des abgelehnten Schreibens halten.

![Zwölf ungespeicherte Werte](view:setup-leave-many)

Eine Änderung unter [MOTOR & ESC, OPTIONEN](motor-settings.md) fordert selbst das Schreiben an, und dieses Schreiben behält auch die ungespeicherten Werte von SETUP.

Keine Warnung erscheint, wenn nichts ungespeichert ist, wenn ein Speichern angefordert ist (`BEI STILLSTAND`) und auf dem Weg zu OUTPUTS, PICK A PIN, WI-FI, UPDATES und FILES. STOP stoppt den Prüfstand und lässt die Warnung offen. Ein Bildschirm, den das Panel selbst öffnet, etwa der Busfehler-Bildschirm nach einem Verbindungsverlust, schließt die Warnung; die Werte gelten weiter und bleiben ungespeichert.

## PANEL

![PANEL](view:setup-application)

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Design | DUNKEL, HELL | DUNKEL | das Farbschema, ab dem nächsten Frame |
| Helligkeit | 20 bis 100%, Schritte von 5 | 100 | skaliert die Palette |
| Kontrast | 60 bis 140%, Schritte von 5 | 100 | drückt Farben weg vom Mittelgrau |
| Bilddrehung | NORMAL, 180° | NORMAL | dreht Bild und Touch um 180°, ab dem nächsten Frame; gesperrt bei ARMED |
| Sprache | ENGLISH, DEUTSCH | ENGLISH | die Sprache der Oberfläche, ab dem nächsten Frame, ohne Neustart |
| Einheiten | METRISCH, IMPERIAL | METRISCH | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| Zeitzone | UTC, LONDON, BERLIN, HELSINKI, NEW YORK, CHICAGO, DENVER, LOS ANGELES, TOKYO, SYDNEY | BERLIN | die Zone, in der Dateidaten geschrieben und gezeigt werden, Sommerzeit eingeschlossen |
| Dimmen nach | 0 bis 60min, Schritte von 5 | 0 | gespeichert; kein Bildschirm nutzt es in diesem Release |
| Ein Finger schiebt | EIN, AUS | AUS | ein Finger verschiebt eine vergrößerte Grafik unter [Logs](logs.md#die-grafik) |
| Drehknopf | EIN, AUS | AUS | ein MT6701 unter 0x06 am I2C-Anschluss (Inter-Integrated Circuit) des Panels dreht den Throttle auf MOTOR & ESC oder das Horn auf SERVO |
| Knopf-Skala | 90 bis 720deg, Schritte von 10 | 270 | der Knopfwinkel, der den Regler über seinen ganzen Weg bewegt |

Die Hintergrundbeleuchtung ist an, solange das Panel läuft. Keine Einstellung schaltet sie.

Bilddrehung dreht das Bild, während jeder Frame hinausgeht, über die PPA (Pixel-Processing Accelerator) des ESP32-P4, und den Touch mit ihm, so dass jedes Bedienelement unter dem Finger bleibt. Ein Kontakt bei (x, y) unter NORMAL liegt unter 180° bei (1023 - x, 599 - y). Kontakte des Touch-Injectors drehen sich nicht. Solange der Prüfstand ARMED ist, hat die Zeile keine Tasten. Braucht die Kopie eines Frames länger als 50ms, gehen Bild und Touch gemeinsam auf NORMAL zurück, und das Band sagt es; die Einstellung bleibt 180°, und der nächste Start dreht wieder.

![Bilddrehung auf 180° bei ARMED](view:setup-rotation)

Das Panel hat keinen RTC-Chip (Real-Time Clock). Seine Uhr startet bei jedem Einschalten ungestellt und wird über NTP (Network Time Protocol) gestellt: Jede Verbindung mit dem Heimnetz fragt `pool.ntp.org` einmal, höchstens 30000ms lang. Eine Verbindung, die vor der Antwort endet, hält das Funkteil für sie an, höchstens 30000ms; ARM, STOP, die Versorgung, ein Log-Lauf und eine Installation nehmen das Funkteil sofort herunter. Ein Software-Neustart behält die Uhr. FAT speichert die Zeit einer Datei als Ortszeit im Moment des Schreibens: Ein späterer Wechsel der Zeitzone verschiebt die Zeiten der Dateien auf der Karte nicht. Bis zu einer Antwort tragen Dateien, die das Panel schreibt, das Datum 1980; FILES INFO liest ein solches Datum als `nicht bekannt`. Der Access Point fragt keinen Zeitserver.

![Zeitzone auf LOS ANGELES](view:setup-time-zone)

![SETUP im hellen Design](view:setup-light)

### Der Drehknopf

- Der Knopf bewegt einen Wert um den Drehweg, nie dorthin, wohin er zeigt. Bei 270deg bewegt eine Vierteldrehung den Throttle um 33,3 Punkte.
- Er schaltet nie auf ARMED. Solange der Prüfstand SAFE ist, bewegt eine Drehung nichts und wird nicht gemerkt.
- Der erste Messwert nach dem Einschalten, oder nachdem der Sensor nicht mehr antwortete, setzt einen Bezug und bewegt nichts.
- Ein Finger auf dem Throttle-Balken oder dem SERVO-Zifferblatt hat den Wert, solange er aufliegt.
- Der Sensor wird alle 10ms gelesen. Ein Sprung über 90deg zwischen zwei Messwerten wird verworfen.

Der Sensor ist ein MT6701 in seinem I2C-Modus, Pin MODE high, unter 0x06. Das Panel spricht für ihn nie 0x14, 0x20 bis 0x27, 0x30 bis 0x3F oder 0x5D an, wo der I/O-Expander CH422G und der Touch-Controller GT911 der Platine antworten, und auch nicht 0x00 bis 0x03 oder 0x78 bis 0x7F, die die I2C-Spezifikation reserviert. Ohne Sensor prüft `Drehknopf` EIN alle 100ms mit einer Adressprobe, ob 0x06 antwortet. Die Host-Tests zeigen, dass das sonst nichts ändert; am Panel ist es nicht gemessen. Verdrahtung: [Bauen](https://github.com/subtilitas/rcbench/blob/main/docs/Building-de.md#verdrahtung-des-drehknopfs).

## SD-KARTE

![SD-KARTE](view:setup-card)

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Auto-Logs | EIN, AUS | EIN | ein Log je ARMED- und Netzteil-Lauf in Ordner Auto-Logs: `SERVOnnn.CSV`, `MOTORnnn.CSV`, `SUPPLnnn.CSV` |
| User-Logs | EIN, AUS | EIN | `ATESTnnn.CSV` und Bericht des AUTOMATISCHEN TESTS und `LIMITnnn.TXT` des ENDLAGENLAUFS in Ordner User-Logs |
| Ordner Auto-Logs | ein Ordner | `BENCH/LOGS` | wohin Auto-Logs schreibt |
| Ordner User-Logs | ein Ordner | `USER/LOGS` | wohin User-Logs schreibt |

- AUS schreibt keine Datei und nimmt keine Nummer. Keine Kartenmeldung dieser Art von Lauf erscheint.
- Auto-Logs während eines Laufs ausgeschaltet beendet dessen Datei an dieser Stelle; während eines ARMED- oder Netzteil-Laufs eingeschaltet, beginnt dort eine Datei.
- User-Logs zählt bei TEST STARTEN und ENDLAGENLAUF STARTEN: Ein Lauf behält, womit er gestartet ist. Ist es aus, nennt der SERVO-Bildschirm `USER-LOGS AUS` an der Stelle der Datei.
- BERICHT unter SERVO > OPTIONEN > PRÜFLING entscheidet bei User-Logs EIN, ob `ATESTnnn.TXT` geschrieben wird.
- Das Fehlerprotokoll, `BENCH/RCBENCH.LOG`, wird unabhängig von Auto-Logs geschrieben.
- [Logs](logs.md#was-ein-lauf-schreibt) listet die Ordner.

Ein Tippen auf die Zeile eines Ordners öffnet die Tastatur auf dem Ordner. Ihre Taste `.` schreibt `/`. OK übernimmt den Ordner in Großbuchstaben; SPEICHERN behält ihn. ABBRECHEN und das Home-Tag lassen den Ordner, wie er war. Ein während eines Laufs geänderter Ordner gilt ab der nächsten Datei; der Lauf endet, wo er begonnen hat, und der nächste wird nach dem nummeriert, was der neue Ordner enthält. Beide Zeilen dürfen denselben Ordner nennen.

![Die Ordner-Tastatur mit einem abgelehnten Ordner](view:setup-folder)

Für einen Ordner, den die Karte nicht nehmen kann, lässt OK die Tastatur offen und den Ordner, wie er war. Die Zeile rechts vom Titel sagt, warum:

| Abgelehnt | Beispiel | Zeile |
|---|---|---|
| ein Ordnername leer, oder `/` am Anfang, am Ende oder doppelt | `/LOGS`, `LOGS/`, `A//B` | `Ein Ordnername ist leer` |
| über 8 Zeichen in einem Ordnernamen | `BENCHLOGS`; `BENCHLOG` wird genommen | `Höchstens 8 Zeichen je Ordner` |
| über 2 Ordner | `A/B/C`; `A/B` wird genommen | `Höchstens 2 Ordner tief` |
| ein Zeichen außer A-Z, 0-9, `_` und `-` | `MY LOGS` | `Nur A-Z, 0-9, _ und -` |
| ein Gerätename von Windows: CON, PRN, AUX, NUL, COM1 bis COM9, LPT1 bis LPT9 | `AUX/LOGS` | `Reservierter Name` |
| `BENCH/ESC`, `BENCH/KST` oder `BENCH/HITEC`, die eigenen Dateien des Prüfstands | `BENCH/KST`; `BENCH/KSTX` wird genommen | `Ordner der Prüfstand-Dateien` |

## ANSCHLÜSSE

ANSCHLÜSSE hält den Strommonitor INA228, die Pins des Sensorbusses, den Winkelsensor, den Phasenabgriff, den Vibrationssensor, den optischen Tacho, den PD mini und den Hitec-Empfang. Die Liste hat fünf Überschriften: I2C-BUS, PHASENABGRIFF, BALANCE-SENSOREN, NETZTEIL und PROG-PORT. [Anschlüsse](setup-interfaces.md) beschreibt sie.

## SENSOREN

SENSOREN hält bis zu vier Strommonitore INA3221: welche bestückt sind, den Shunt jedes Kanals und was jeder Kanal misst. [Sensoren](setup-sensors.md) beschreibt sie.

## Verwandte Seiten

- [Anschlüsse](setup-interfaces.md)
- [Sensoren](setup-sensors.md)
- [Ausgänge](outputs.md) und [Pin-Auswahl](picker.md)
- [Updates](updates.md)
- [Netzteil](supply.md), [Servo-Optionen](servo-settings.md) und [Motor-Optionen](motor-settings.md): die Einstellungen, die auf ihren eigenen Bildschirmen stehen.
