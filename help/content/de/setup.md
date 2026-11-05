---
screen: SETUP
title: Setup
order: 60
---

# Setup

SETUP enthält die prüfstandsweiten Paneleinstellungen in den Kategorien PANEL, SENSORS und SD CARD. ESC-Einstellungen (Electronic Speed Controller) stehen unter [MOTOR & ESC, OPTIONEN](motor-settings.md). Öffne SETUP im [Menü](overview.md), um das Raster aus 3 mal 3 Kacheln zu sehen. Jede Kategorie öffnet ihre Einstellungsliste über die volle Seitenbreite.

## Die Kacheln

![Die Kacheln, alles gespeichert](view:setup)

| Kachel | Zweite Zeile | Ziel |
|---|---|---|
| PANEL | `DESIGN, SPRACHE, DREHKNOPF` | Einstellungen des Panels. |
| SENSORS | `STROM, WINKEL, PHASENABGRIFF` | [Sensoren](setup-sensors.md): Sensorbus, Strommonitore INA228 und INA3221, AS5600, Phasenabgriff und Bluetooth-IMU. Links stehen 9 Einträge, rechts die Einstellungen der Auswahl. |
| OUTPUTS | `PROTOKOLLE, PINS, PLATINE` | [Ausgänge](outputs.md): Funktion der IOmcu-Pins auf einer Platinenzeichnung. |
| SD CARD | `LOGS, BROWSER-SCHREIBEN` | Aufzeichnungsarten, Speicherordner und Voraussetzungen für Browser-Schreibzugriffe. |
| WI-FI | `NETZWERK, DATEIEN, UPLOAD` | [WLAN](wifi.md): Funkverbindung für Kartendateien und Firmware-Images im Browser. |
| UPDATES | `VERSIONEN AUF GITHUB` | [Updates](updates.md): Suche nach neuerer Panel-Firmware und Update-Erinnerung. |
| IMU | `BLUETOOTH-WINKELSENSOR` | [IMU](imu.md): HC-05 und gebundener WitMotion BWT901CL, Sensorsuche, Kalibrierung und aktuelle Winkel. |
| LAYOUT | `STECKER UND PINS` | [Layout](layout.md): nur auf dem rbmod-Carrier mit fest verlöteten Pins. |

2 Plätze bleiben frei; auf dem Carrier belegt LAYOUT einen davon. Wie bei den Listen steht unten rechts die Speichertaste. Sie zeigt ungespeicherte Änderungen aus allen Kategorien an:

![Die Kacheln mit einem ungespeicherten Wert](view:setup-hub-dirty)

Das Home-Feld oben links führt jeweils eine Ebene zurück:

| Ausgangspunkt | Ziel des Home-Felds |
|---|---|
| Kategorieliste | Kacheln; die Listenposition bleibt erhalten. |
| Kacheln | Menü; bei ungespeicherten Werten erscheint zuerst eine [Warnung](#mit-ungespeicherten-werten-verlassen). |
| OUTPUTS, WI-FI, UPDATES, IMU | Kacheln. Wurde OUTPUTS über ZIEL auf MOTOR & ESC oder SERVO geöffnet, führt es dorthin zurück; WI-FI kehrt entsprechend zu LOGS oder UPDATES zurück. Offene Listen oder Tastaturen schließen zuerst. |

## Einen Wert ändern

![Die Kacheln, alles gespeichert](view:setup)

Die Liste ist 984px breit, auf SENSORS 754px, und zeigt 6 Zeilen zu 64px. Eine 48px hohe Leiste darunter enthält Kategoriename, KATEGORIE ZURÜCKSETZEN und Speichertaste. Sie endet 4px über dem Hinweisband, sodass Meldungen keine Tasten oder Zeilen verdecken.

Eine Zeile zeigt die Einstellung, eine Hilfezeile und ihren Wert mit Einheit. Eine Zahl hat die Tasten `-` und `+`; ein Schalter oder eine Auswahl hat `<` und `>`.

- **Tippen**: Eine Taste ändert den Wert einmal, wenn du innerhalb der Taste und höchstens 8px vom Druckpunkt entfernt loslässt.
- **Halten**: Die Wiederholung beginnt nach 0,45s mit 8 Schritten pro Sekunde. Ab 2,25s sind es 30 Schritte pro Sekunde.
- **Scrollen**: Ziehe die Liste senkrecht um mehr als 8px, um ohne Wertänderung zu scrollen.
- **Tastenfeld**: Tippe auf einen Zahlenwert zwischen `-` und `+`. Das Tastenfeld zeigt Einstellung, Bereich und Einheit; die Nachkommastellen entsprechen der Schrittweite. OK rundet auf den nächsten Schritt: Aus 73% Helligkeit werden 75%. Werte außerhalb des Bereichs lassen das Tastenfeld offen und heben den Bereich in der Warnfarbe hervor. Auch innerhalb des angezeigten Bereichs können Werte unzulässig sein. Dann erscheint stattdessen ein Grund: `0 oder 5.0 bis 1000.0mΩ` bei einem Shunt zwischen 0 und 5,0mΩ, `0 oder 10 bis 32767mA` bei einer Stromgrenze zwischen 0 und 10mA, `Zulässige Bus-Pins: 0 4 6 14 16 18 20 26` bei einem unzulässigen Sensor-SDA-Pin oder `GPIO vom anderen PD-mini-Pin belegt`. Die nächste Eingabe zeigt wieder den Bereich. ABBRECHEN oder das Home-Feld verwirft die Eingabe. Setze einen nicht zugeordneten Pin (-1) mit `-`; Pin-Tastenfelder haben keine Taste `+/-`.
- Es wird nur ein Finger verarbeitet. Ein zweiter hat keine Wirkung, solange der erste drückt oder scrollt.

Ein geänderter Wert gilt sofort. KATEGORIE ZURÜCKSETZEN setzt jeden Wert der offenen Kategorie auf seine Vorgabe zurück; auf SENSORS tut `INA228 ZURÜCKSETZEN` das allein für den gewählten Eintrag.

| Taste | Wirkung |
|---|---|
| KATEGORIE ZURÜCKSETZEN | Stellt die Vorgaben der offenen Kategorie sofort wieder her. |
| GESPEICHERT, SPEICHERN, BEI STILLSTAND, NICHT GESPEICHERT | Zustände der Speichertaste; siehe unten. |

## Die Werte behalten

![Ein geänderter Wert, SPEICHERN wird angeboten](view:setup-dirty)

SPEICHERN schreibt geänderte Werte in den Flash. Werden alle Werte auf ihren gespeicherten Stand zurückgestellt, gilt nichts mehr als geändert: Die Taste zeigt wieder `GESPEICHERT`, und eine ausstehende Speicheranforderung entfällt.

| Beschriftung | Bedeutung |
|---|---|
| `GESPEICHERT` | Alle Werte sind gespeichert; die Taste hat keine Wirkung. |
| `SPEICHERN` | Ungespeicherte Werte vorhanden; ein Druck fordert das Schreiben an. |
| `BEI STILLSTAND` | Schreibvorgang wartet auf einen ruhenden Prüfstand. |
| `NICHT GESPEICHERT` | Schreiben fehlgeschlagen; ein Druck versucht es erneut. |

Geschrieben wird im ersten Frame bei SAFE, ausgeschaltetem Netzteilausgang und ohne laufenden Lade- oder Speichervorgang einer Platinenzeichnung. Bei ARMED zeigt die Taste bis zum Wechsel auf SAFE `BEI STILLSTAND`. Ungespeicherte Werte gelten bis zum Ausschalten des Panels. Das Verlassen einer Liste oder der Kachelseite speichert sie nicht.

`NICHT GESPEICHERT` sagt nicht aus, wie viel bereits im Flash steht. Nach einem teilweise fehlgeschlagenen Schreibvorgang können einzelne Schlüssel aktualisiert sein. Der nächste Start kann daher alte und neue Werte mischen. Bis zum erfolgreichen Speichern bleibt die Taste bei `NICHT GESPEICHERT` oder `SPEICHERN`, auch wenn du alle Werte zurückstellst.

## Mit ungespeicherten Werten verlassen

![Ein ungespeicherter Wert, am Home-Tag](view:setup-leave)

Das Home-Feld auf der Kachelseite führt zum Menü. Zeigt die Speichertaste `SPEICHERN` oder `NICHT GESPEICHERT`, erscheint zuerst eine Warnung. Sie listet jeden geänderten Wert als `Name: gespeichert -> jetzt`, mit demselben Zahlenformat und derselben Einheit wie in der Liste. Als gespeicherter Stand gelten die beim Start geladenen oder zuletzt erfolgreich geschriebenen Werte.

Die Warnung umfasst auch den Prüflingsnamen (DUT, device under test) und Einstellungen anderer Seiten, etwa SERVO, ESC STICK und MOTOR & ESC. Ein Speichervorgang schreibt sie alle.

| Taste | Wirkung |
|---|---|
| BLEIBEN | Schließt die Warnung und zeigt wieder die Kacheln; Listen behalten Kategorie und Position. |
| VERWERFEN | Stellt alle aufgelisteten Werte wie bei einer manuellen Änderung auf den gespeicherten Stand zurück und öffnet das Menü. |
| SPEICHERN | Fordert wie die Speichertaste das Schreiben an und öffnet das Menü. |

- VERWERFEN sendet zurückgestellte Pin- oder Portwerte sofort an den IOmcu. Eine wiederhergestellte PD-mini-Verdrahtung schaltet wie eine manuelle Änderung den Netzteilausgang aus.
- Bei ARMED ist VERWERFEN gedimmt und ohne Wirkung. Die Zeile `VERWERFEN wartet, bis der Prüfstand SAFE ist.` erklärt das. BLEIBEN und SPEICHERN bleiben verfügbar.
- PWM-Ziel und Port-PWM werden nicht zurückgestellt, solange die Servoprogrammierung den Port belegt. Sie bleiben ungespeichert.
- Bei mehr als 10 Änderungen erscheinen 9 einzelne Werte und eine Zusammenfassung: bei 12 Änderungen `und 3 weitere`.
- Nach einem fehlgeschlagenen Speicherversuch erscheint `Speichern fehlgeschlagen. Tippe erneut auf SPEICHERN.` VERWERFEN fordert dann auch das Schreiben der zurückgestellten Werte an, da der Flash bereits teilweise geändert sein kann.

![Zwölf ungespeicherte Werte](view:setup-leave-many)

Eine Änderung unter [MOTOR & ESC, OPTIONEN](motor-settings.md) fordert automatisch das Speichern an. Dabei werden auch ungespeicherte SETUP-Werte geschrieben.

Die Warnung entfällt, wenn keine ungespeicherten Werte vorliegen, bereits eine Speicherung angefordert ist (`BEI STILLSTAND`) oder OUTPUTS, WI-FI beziehungsweise UPDATES geöffnet wird. STOP stoppt den Prüfstand und lässt die Warnung offen. Eine automatisch geöffnete Seite, etwa CAN BUS FAULT nach Verbindungsverlust, schließt die Warnung. Die Werte gelten weiter und bleiben ungespeichert.

## PANEL

![PANEL](view:setup-application)

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Design | DUNKEL, HELL | DUNKEL | das Farbschema, ab dem nächsten Frame |
| Helligkeit | 20 bis 100%, Schritte von 5 | 100 | skaliert die Palette |
| Kontrast | 60 bis 140%, Schritte von 5 | 100 | verstärkt den Abstand der Farben zum Mittelgrau |
| Bilddrehung | NORMAL, 180° | NORMAL | dreht Bild und Touch um 180°, ab dem nächsten Frame; gesperrt bei ARMED |
| Sprache | ENGLISH, DEUTSCH | ENGLISH | die Sprache der Oberfläche, ab dem nächsten Frame, ohne Neustart |
| Einheiten | METRISCH, IMPERIAL | METRISCH | gespeichert; keine Seite nutzt sie in diesem Release |
| Zeitzone | UTC, LONDON, BERLIN, HELSINKI, NEW YORK, CHICAGO, DENVER, LOS ANGELES, TOKYO, SYDNEY | BERLIN | die Zone, in der Dateidaten geschrieben und gezeigt werden, Sommerzeit eingeschlossen |
| Dimmen nach | 0 bis 60min, Schritte von 5 | 0 | gespeichert; keine Seite nutzt es in diesem Release |
| Ein Finger schiebt | EIN, AUS | AUS | ein Finger verschiebt eine vergrößerte Grafik unter [Logs](logs.md#die-grafik) |
| Drehknopf | EIN, AUS | AUS | ein MT6701 unter 0x06 an einem eigenen Bus, IO30 und IO31 des Headers P3 (I2C: Inter-Integrated Circuit), ändert Gas auf MOTOR & ESC oder das Horn auf SERVO |
| Drehknopfbereich | 90 bis 720°, Schritte von 10 | 270 | der Knopfwinkel, der den Regler über seinen ganzen Weg bewegt |
| Tastaturtransparenz | AUS, 25%, 50% | AUS | wie viel des Texts durch die Tastatur des Texteditors auf [Files](files.md#der-texteditor) scheint; andere Tastaturen bleiben deckend |
| Demo-Modus | EIN, AUS | AUS | blendet das Wasserzeichen SIMULATION ab dem nächsten Bild aus; das Band zeigt weiter SIM, und die Kacheln zeigen weiter SIMULIERT. Das Panel zeichnet das Wasserzeichen nur, solange kein IOmcu antwortet, die Einstellung ändert also nichts an einem Prüfstand mit IOmcu |

Die Hintergrundbeleuchtung ist an, solange das Panel läuft. Keine Einstellung schaltet sie.

Bilddrehung dreht jeden ausgegebenen Frame über den PPA (Pixel-Processing Accelerator) des ESP32-P4 und passt die Touch-Koordinaten an. Ein Kontakt bei (x, y) unter NORMAL liegt bei 180° an (1023 - x, 599 - y). Kontakte des Touch-Injectors werden nicht gedreht. Bei ARMED hat die Einstellungszeile keine Tasten.

Dauert das Kopieren eines Frames länger als 50ms, kehren Bild und Touch gemeinsam zu NORMAL zurück. Das Band meldet den Vorgang. Die Einstellung bleibt 180°, sodass der nächste Start die Drehung erneut versucht.

![Bilddrehung auf 180° bei ARMED](view:setup-rotation)

Das Panel hat keinen RTC-Chip (Real-Time Clock). Nach dem Einschalten ist die Uhr ungestellt. Sie erhält die Zeit über NTP (Network Time Protocol): Jede Verbindung zum Heimnetz fragt `pool.ntp.org` einmal für höchstens 30000ms ab. Endet die Verbindung vorher, bleibt das Funkmodul für die Antwort höchstens 30000ms eingeschaltet. ARM, STOP, Netzteilbetrieb, eine Aufzeichnung oder eine Installation schaltet es sofort aus. Ein nicht antwortender erster DNS-Server (Domain Name System) verzögert die Anfrage nach jeder Verbindung um etwa 7s.

Ein Software-Neustart erhält die Uhrzeit. FAT speichert Zeitstempel als Ortszeit beim Schreiben. Ein späterer Wechsel der Zeitzone ändert vorhandene Dateizeitstempel nicht. Bis zur NTP-Antwort erhalten Dateien das Jahr 1980; FILES INFO zeigt dafür `nicht bekannt`. Im Access-Point-Modus wird kein Zeitserver abgefragt.

![Zeitzone auf LOS ANGELES](view:setup-time-zone)

![SETUP im hellen Design](view:setup-light)

### Der Drehknopf

- Der Drehknopf ändert Werte um den Drehweg, nicht entsprechend seiner absoluten Stellung. Bei einem Bereich von 270° ändert eine Vierteldrehung Gas um 33,3 Prozentpunkte.
- Er schaltet nie auf ARMED. Drehungen bei SAFE haben keine Wirkung und werden verworfen.
- Der erste Messwert nach Einschalten oder Sensorausfall setzt den Bezug, ohne den Ausgang zu bewegen.
- Ein aufliegender Finger auf dem Gasregler oder dem SERVO-Zifferblatt hat Vorrang vor dem Drehknopf.
- Der Sensor wird alle 10ms ausgelesen. Änderungen über 90° zwischen zwei Messwerten werden verworfen.

Der MT6701 arbeitet im I2C-Modus mit MODE auf High unter Adresse 0x06. Sein Bus wird von keinem anderen Bauteil der Platine genutzt. Das Panel fragt dafür weder die GT911-Adressen 0x14 und 0x5D des Touch-Busses noch die reservierten I2C-Adressen 0x00 bis 0x03 und 0x78 bis 0x7F ab. Ohne Sensor prüft `Drehknopf` EIN alle 100ms, ob 0x06 antwortet. Die Host-Tests zeigen keine weiteren Auswirkungen; am Panel wurde dies nicht gemessen. Zur Verdrahtung siehe [Bauen](https://github.com/subtilitas/rcbench/blob/main/docs/Building-de.md#verdrahtung-des-drehknopfs).

## SD CARD

![SD CARD](view:setup-card)

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Auto-Aufzeichnungen | EIN, AUS | EIN | eine Aufzeichnung je ARMED- und Netzteillauf in Ordner Auto-Aufzeichnungen: `SERVOnnn.CSV`, `MOTORnnn.CSV`, `SUPPLnnn.CSV` |
| Testaufzeichnungen | EIN, AUS | EIN | `ATESTnnn.CSV` und Bericht des AUTOMATISCHEN TESTS und `LIMITnnn.TXT` des ENDLAGENLAUFS in Ordner Testaufzeichnungen |
| Schreiben per Browser | OFFEN, ZUGANGSCODE, HALTEN, AUS | OFFEN | was die Browserseite von WI-FI braucht, um die Karte zu ändern: [WLAN](wifi.md#schreiben-per-browser) |
| Ordner Auto-Aufzeichnungen | ein Ordner | `BENCH/LOGS` | wohin Auto-Aufzeichnungen schreibt |
| Ordner Testaufzeichnungen | ein Ordner | `USER/LOGS` | wohin Testaufzeichnungen schreibt |

- AUS erzeugt keine Datei, vergibt keine Nummer und unterdrückt Kartenmeldungen für diese Aufzeichnungsart.
- Wird Auto-Aufzeichnungen während eines Laufs ausgeschaltet, endet dessen Datei an dieser Stelle. Einschalten während ARMED oder eines Netzteillaufs beginnt dort eine Datei.
- Testaufzeichnungen wird bei TEST STARTEN und ENDLAGENLAUF STARTEN ausgewertet. Diese Wahl gilt bis zum Laufende. Bei AUS zeigt SERVO statt des Dateinamens `TESTAUFZEICHNUNGEN AUS`.
- Bei Testaufzeichnungen EIN entscheidet BERICHT unter SERVO > OPTIONEN > PRÜFLING, ob `ATESTnnn.TXT` geschrieben wird.
- Das Fehlerprotokoll `BENCH/RCBENCH.LOG` wird unabhängig von Auto-Aufzeichnungen geschrieben.
- [Logs](logs.md#was-ein-lauf-schreibt) beschreibt die Ordner.

Tippe auf eine Ordnerzeile, um ihre Tastatur zu öffnen. Die Taste `.` gibt `/` ein. OK übernimmt den Ordner in Großbuchstaben, SPEICHERN speichert ihn. ABBRECHEN oder das Home-Feld behält den bisherigen Ordner. Eine Änderung während eines Laufs gilt erst für die nächste Datei; der laufende Vorgang bleibt im ursprünglichen Ordner. Die nächste Nummer richtet sich nach dem Inhalt des neuen Ordners. Beide Einstellungen dürfen denselben Ordner verwenden.

![Die Ordner-Tastatur mit einem abgelehnten Ordner](view:setup-folder)

Bei einem unzulässigen Ordner bleibt die Tastatur offen und die Einstellung unverändert. Neben dem Titel steht der Grund:

| Abgelehnt | Beispiel | Zeile |
|---|---|---|
| ein Ordnername leer, oder `/` am Anfang, am Ende oder doppelt | `/LOGS`, `LOGS/`, `A//B` | `Ein Ordnername ist leer` |
| über 8 Zeichen in einem Ordnernamen | `BENCHLOGS`; `BENCHLOG` wird genommen | `Höchstens 8 Zeichen je Ordner` |
| über 2 Ordner | `A/B/C`; `A/B` wird genommen | `Höchstens 2 Ordnerebenen` |
| ein Zeichen außer A-Z, 0-9, `_` und `-` | `MY LOGS` | `Nur A-Z, 0-9, _ und -` |
| ein Gerätename von Windows: CON, PRN, AUX, NUL, COM1 bis COM9, LPT1 bis LPT9 | `AUX/LOGS` | `Reservierter Name` |
| `BENCH/ESC`, `BENCH/KST`, `BENCH/HITEC` oder `BENCH/ESCW`, die eigenen Dateien des Prüfstands | `BENCH/KST`; `BENCH/KSTX` wird genommen | `Für Prüfstand-Dateien reserviert` |

## SENSORS

SENSORS listet Sensorbus, INA228, vier INA3221, AS5600 und Phasenabgriff des IOmcu sowie die Bluetooth-IMU des Panels. Jedes hat einen Eintrag in der linken Liste. Siehe [Sensoren](setup-sensors.md).

## Verwandte Seiten

- [Netzteil](supply.md): die Verdrahtung des PD mini, im Tab PD MINI.
- [Sensoren](setup-sensors.md)
- [Ausgänge](outputs.md)
- [Updates](updates.md)
- [Netzteil](supply.md), [Servo-Optionen](servo-settings.md) und [Motor-Optionen](motor-settings.md): die Einstellungen, die auf ihren eigenen Bildschirmen stehen.
