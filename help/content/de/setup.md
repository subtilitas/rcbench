---
screen: SETUP
title: Setup
order: 60
---

# Setup

SETUP hält die Einstellungen des Panels in drei Kategorien: ESC / PRÜFSTAND, ANWENDUNG und ANSCHLÜSSE. Der Bildschirm öffnet über die Kachel SETUP im [Menü](overview.md); das Band nennt ihn SETTINGS. Die linke Spalte wählt die Kategorie und trägt OUTPUTS, PICK A PIN, KATEGORIE ZURÜCKSETZEN und die Speichertaste. Die Liste der Kategorie belegt die rechte.

## Einen Wert ändern

![ESC / PRÜFSTAND, alles gespeichert](view:setup)

Eine Zeile zeigt die Einstellung, eine Hilfezeile und ihren Wert mit Einheit. Eine Zahl hat die Tasten `-` und `+`; ein Schalter oder eine Auswahl hat `<` und `>`.

- **Tipp**: Eine Taste schaltet einmal, wenn der Finger innerhalb von 8 px um den Druckpunkt auf der Taste abhebt.
- **Halten**: Eine gehaltene Taste schaltet zuerst nach 0,45 s, dann 8-mal pro Sekunde und ab 2,25 s 30-mal pro Sekunde.
- **Scrollen**: Ein senkrechtes Ziehen über mehr als 8 px irgendwo auf der Liste scrollt sie und ändert keinen Wert.
- Ein Finger zur selben Zeit: Ein zweiter Finger tut nichts, solange einer drückt oder scrollt.

Ein geänderter Wert gilt sofort. KATEGORIE ZURÜCKSETZEN setzt jeden Wert der offenen Kategorie auf seine Vorgabe zurück.

| Taste | Was sie tut |
|---|---|
| OUTPUTS | öffnet [Ausgänge](outputs.md): die Rolle jedes Koprozessor-Pins, als Liste |
| PICK A PIN | öffnet die [Pin-Auswahl](picker.md): dasselbe, auf einer Zeichnung der Platine |
| KATEGORIE ZURÜCKSETZEN | die Vorgaben der offenen Kategorie, sofort |
| GESPEICHERT, SPEICHERN, BEI STILLSTAND, NICHT GESPEICHERT | die Speichertaste; siehe unten |

## Die Werte behalten

![Ein geänderter Wert, SPEICHERN wird angeboten](view:setup-dirty)

Ein geänderter Wert wird erst in den Flash geschrieben, wenn SPEICHERN gedrückt wird.

| Beschriftung | Bedeutung |
|---|---|
| `GESPEICHERT` | nichts ist ungeschrieben; die Taste tut nichts |
| `SPEICHERN` | etwas ist ungeschrieben; ein Druck fordert das Schreiben an |
| `BEI STILLSTAND` | das Schreiben wartet auf einen ruhenden Prüfstand |
| `NICHT GESPEICHERT` | der Speicher hat das Schreiben abgelehnt; ein Druck versucht es erneut |

Geschrieben wird im ersten Frame mit dem Prüfstand SAFE, abgeschaltetem Netzteilausgang und ohne Platinenzeichnung, die geladen oder gespeichert wird. Bei ARMED lautet die Taste `BEI STILLSTAND`, bis der Prüfstand SAFE ist. Nicht gespeicherte Werte gelten, bis das Panel ausgeschaltet wird. Das Verlassen des Bildschirms schreibt nichts.

`NICHT GESPEICHERT` kann nicht sagen, wie viel den Flash erreicht hat: Eine Ablehnung mittendrin lässt die zuvor geschriebenen Schlüssel stehen; der nächste Start kann eine Mischung aus neuen und alten Werten laden.

## ESC / PRÜFSTAND

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Zellen | 1 bis 14 S | 6 | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| Kapazität | 0 bis 30000 mAh, Schritte von 100 mAh | 5000 | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| Innenwiderstand | 0 bis 200 mOhm | 18,0 | gespeichert; kein Bildschirm nutzt ihn in diesem Release |
| Motorpole | 2 bis 42, Schritte von 2 | 14 | die Drehzahl ist eRPM (elektrische Drehzahl) geteilt durch Pole/2 |
| Nenn-kV | 0 bis 10000 rpm/V, Schritte von 10 | 0 | der kV-Wert auf [MOTOR & ESC](motor.md#kv-rpm-v-und-eff); 0 lässt ihn leer |
| Leerlaufpuls | 800 bis 1600 us, Schritte von 10 | 1000 | die PWM-Breite (Pulsweitenmodulation) bei 0 % Throttle |
| Vollpuls | 1400 bis 2400 us, Schritte von 10 | 2000 | die PWM-Breite bei 100 % Throttle |
| Rampengrenze | 5 bis 300 %/s, Schritte von 5 | 55 | die Throttle-Rampe des Panelmodells, genutzt, solange kein Koprozessor antwortet |

Leerlaufpuls und Vollpuls gehen beim Link-Aufbau und bei jeder Änderung an den Koprozessor. Der Koprozessor fährt keine Throttle-Rampe: Ein gebundener Pin springt auf den Befehl.

## ANWENDUNG

![ANWENDUNG](view:setup-application)

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| Design | DUNKEL, HELL | DUNKEL | das Farbschema, ab dem nächsten Frame |
| Helligkeit | 20 bis 100 %, Schritte von 5 | 100 | skaliert die Palette |
| Kontrast | 60 bis 140 %, Schritte von 5 | 100 | drückt Farben weg vom Mittelgrau |
| Beleuchtung | EIN, AUS | EIN | gespeichert; kein Bildschirm nutzt sie in diesem Release; die Platine verdrahtet die Hintergrundbeleuchtung nur als ein oder aus |
| Sprache | ENGLISH, DEUTSCH | ENGLISH | die Sprache der Oberfläche, ab dem nächsten Frame, ohne Neustart |
| Einheiten | METRISCH, IMPERIAL | METRISCH | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| Dimmen nach | 0 bis 60 min, Schritte von 5 | 0 | gespeichert; kein Bildschirm nutzt es in diesem Release |
| Ein Finger schiebt | EIN, AUS | AUS | ein Finger verschiebt eine vergrößerte Grafik unter [Logs](logs.md#die-grafik) |
| Drehknopf | EIN, AUS | AUS | ein AS5600 am I2C-Anschluss (Inter-Integrated Circuit) des Panels dreht den Throttle auf MOTOR & ESC oder das Horn auf SERVO |
| Knopf-Skala | 90 bis 720 deg, Schritte von 10 | 270 | der Knopfwinkel, der den Regler über seinen ganzen Weg bewegt |

Die Hilfezeilen von Helligkeit und Dimmen nach passen auf Deutsch in ihre Zeile; auf Englisch sind sie am rechten Rand abgeschnitten.

![SETUP im hellen Design](view:setup-light)

### Der Drehknopf

- Der Knopf bewegt einen Wert um den Drehweg, nie dorthin, wohin er zeigt. Bei 270 deg bewegt eine Vierteldrehung den Throttle um 33,3 Punkte.
- Er schaltet nie auf ARMED. Solange der Prüfstand SAFE ist, bewegt eine Drehung nichts und wird nicht gemerkt.
- Der erste Messwert nach dem Einschalten, oder nachdem der Sensor nicht mehr antwortete, setzt einen Bezug und bewegt nichts.
- Ein Finger auf dem Throttle-Balken oder dem SERVO-Zifferblatt hat den Wert, solange er aufliegt.
- Der Sensor wird alle 10 ms gelesen. Ein Sprung über 90 deg zwischen zwei Messwerten wird verworfen.

Bekannte Einschränkung: Auf dem Panel Waveshare ESP32-S3 Touch LCD 7 antwortet der I/O-Expander CH422G der Platine auf den I2C-Adressen 0x20 bis 0x27 und 0x30 bis 0x3F. Das schließt die feste Adresse 0x36 des AS5600 ein; ein AS5600 als Knopf lässt sich an diesem Bus nicht lesen.

## ANSCHLÜSSE

ANSCHLÜSSE hält die Strommonitore, den Winkelsensor, den Phasenabgriff, den Vibrationssensor, den optischen Tacho und den PD mini. [Anschlüsse](setup-interfaces.md) beschreibt sie.

## Verwandte Seiten

- [Anschlüsse](setup-interfaces.md)
- [Ausgänge](outputs.md) und [Pin-Auswahl](picker.md)
- [Netzteil](supply.md) und [Servo-Optionen](servo-settings.md): die Einstellungen, die auf ihren eigenen Bildschirmen stehen.
