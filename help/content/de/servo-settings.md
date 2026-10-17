---
screen: none
title: Servo-Optionen
order: 21
---

# Servo-Optionen

OPTIONEN, oben auf der rechten Karte von [SERVO](servo.md), öffnet die Optionen des Servos über der linken Karte. Vier Seiten: AUSGANG, TEST, GRENZEN und PRÜFLING. ARM, ZENTRIEREN, FREIGEBEN und STOP bleiben an ihrem Platz und wirken. Ein Wert öffnet das Tastenfeld, eine Liste öffnet eine Liste, ein Schalter schaltet beim Tipp um, und NAME öffnet eine Tastatur.

## AUSGANG

![Die Seite AUSGANG](view:servo-settings)

| Option | Bereich | Was sie tut |
|---|---|---|
| TYP | STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760 | das Servoprofil |
| PULSRATE | die Liste des Typs, oder EIGENE EINSTELLUNG über das Tastenfeld | wie oft ein Puls gesendet wird |
| PULS MIN, PULS MITTE, PULS MAX | 400 bis 2500 us; jedes Ende mindestens 50 us von PULS MITTE entfernt | -90 deg ist MIN, 0 ist MITTE, +90 deg ist MAX |
| TRIMMUNG | -200 bis +200 us, 5 us je Schritt | wird zur Mitte addiert |
| WEG | 10 bis 90 deg | wie weit das Horn je Richtung gehen darf |
| UMKEHREN | EIN, AUS | die Richtung, in der der Winkel auf den Puls abgebildet wird |

| Typ | Mitte | Weg | Frame Rates |
|---|---|---|---|
| STANDARD PWM | 1500 us | 1000-2000 us | 50, 60, 100, 150, 200, 250, 300, 333 Hz |
| NARROW 760 | 760 us | 660-860 us | wie STANDARD PWM |
| WIDE | 1500 us | 800-2200 us | wie STANDARD PWM, bis 312 Hz |
| HELI CYCLIC | 1520 us | 820-2220 us | 50, 120, 200, 333 Hz |
| HELI TAIL 760 | 760 us | 410-1110 us | 200, 333, 560 Hz |

Ein Puls braucht eine Pause vor dem nächsten Frame: mindestens 1 ms bei STANDARD PWM, NARROW 760 und WIDE, 0,5 ms bei den Heli-Typen. Eine Frame Rate, die den Pulsen keine Pause lässt, wird abgelehnt.

Die Optionen von AUSGANG gelten für die Sitzung. Jeder Neustart kehrt zu STANDARD PWM mit 50 Hz zurück.

## Die Warnung bei schnellen Raten

![KANN DAS SERVO ZERSTÖREN](view:servo-warning)

Ein Heli-Typ oder jede Frame Rate über 60 Hz kann ein Servo zerstören, das nicht dafür gebaut ist. Die Wahl öffnet eine Warnung in der Gefahrfarbe, die die Wahl nennt. Sie gilt, nachdem HALTEN ZUM ÜBERNEHMEN 2 s gehalten wurde. ABBRECHEN, ein Finger, der abrutscht, und das Verlassen des Bildschirms übernehmen nichts. Typ und Rate, die gelten, bleiben auf der rechten Karte rot.

Die Seite AUSGANG sagt, was aus der Rate wurde:

| Hinweis | Bedeutung |
|---|---|
| `Aktiv: AUSGÄNGE laufen mit 330 Hz.` | der Koprozessor fährt die Rate auf dem Ziel, das er hält |
| `Die Pulsrate wird mit der nächsten Position an den Koprozessor gesendet.` | noch nicht geschrieben |
| `ABGELEHNT: ...` | ein Puls ist länger als der Frame, oder ein Pin teilt den PWM-Slice mit anderer Rate; die Pins behalten ihre Rate |
| `Keine Verbindung. Die aktive Pulsrate ist unbekannt.` | kein Koprozessor antwortet |

## TEST

![Die Seite TEST](view:servo-test)

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| KURVE | RECHTECK, SINUS, DREIECK | SINUS | die Kurve von SWEEP und der Bewegungen des Tests |
| TEMPO | 0,05 bis 5 Hz | 0,5 Hz | Kurven pro Sekunde |
| BEREICH | 10 bis 100 % | 80 % | der Anteil des Wegs, der durchfahren wird |
| TESTDAUER NACH | ZEIT, BEWEGUNGEN | ZEIT | wie jede Netzteilstufe bemessen wird |
| TESTZEIT | 5 bis 3600 s | 60 s | die Dauer einer Stufe |
| BEWEGUNGEN | 1 bis 1000 | 20 | die Bewegungen einer Stufe |
| HALTEZEIT | 0 bis 5000 ms | 200 ms | an jedem Ende gehalten |
| EINSCHWINGZEIT | 0 bis 10000 ms | 500 ms | Wartezeit nach einer Netzteilstufe vor dem Messen |
| STUFE 4.8 V, STUFE 6.0 V | EIN, AUS | EIN | die Netzteilstufen |
| HV-SERVO | EIN, AUS | AUS | fügt STUFE 7.4 V und STUFE 8.4 V hinzu; bei jedem Neustart aus |
| BROWN-OUT | EIN, AUS | EIN | ab 5,0 V abwärts, bis das Servo stehen bleibt |
| TEST STARTEN | | | der automatische Test: ein Halten von 2 s bei ARMED |

![HV-SERVO eingeschaltet](view:servo-hv)

Ist HV-SERVO eingeschaltet und eine Stufe über 6,0 V gewählt, öffnet TEST STARTEN zuerst NUR HV-SERVOS und nennt die höchste Stufe.

## GRENZEN

![Die Seite GRENZEN](view:servo-limits)

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| SPANNUNG MAX, STROM MAX | wie auf SUPPLY | 21,00 V, 5,00 A | die Obergrenzen der Seite SUPPLY, dieselben Einstellungen |
| BLOCKIERT AB | 0,1 bis 5,0 A | 2,00 A | über diesem Strom gilt das Servo als blockiert |
| RUHESTROM | 0 bis 5,00 A | 0, nicht geprüft | nicht bestanden über diesem Wert in Ruhe |
| HALTESTROM | 0 bis 5,00 A | 0, nicht geprüft | nicht bestanden über diesem Wert beim Halten eines Endes |
| STELLZEIT | 0 bis 5000 ms | 0, nicht geprüft | nicht bestanden über diesem Wert von Ende zu Ende |
| ENDLAGENLAUF | NUR SUCHE, SUCHEN + PROGRAMMIEREN | NUR SUCHE | was ENDLAGENLAUF STARTEN tut: die Enden suchen oder zusätzlich LEFT ANGLE und RIGHT ANGLE programmieren |
| SERVOPULSE | BEHALTEN, 1000-2000 | BEHALTEN | nur mit PWM-ZIEL auf PROG-PORT angezeigt: 1000-2000 schreibt PULSE LOWER und PULSE UPPER des Servos vor der Suche auf 1000,0 und 2000,0 us |

ENDLAGENLAUF STARTEN unter der Tabelle startet den [Endlagenlauf](servo.md#der-endlagenlauf) mit einem Halten von 2 s bei ARMED. Die Zeile darunter nennt Ausgang und Messgerät oder den Grund, warum der Lauf nicht starten kann.

## PRÜFLING

![NAME auf der Tastatur](view:servo-name)
![Die Seite PRÜFLING mit eingeschaltetem Winkelsensor](view:servo-encoder)

| Option | Was sie tut |
|---|---|
| NAME | der Prüfling, bis 23 Zeichen; steht über jedem Bericht |
| BERICHT | ein Textbericht, `SERVOnnn.TXT`, neben dem Log jedes Tests |
| AS5600 | der Winkelsensor an der Hornwelle, dieselbe Einstellung wie unter [Anschlüsse](setup-interfaces.md) |
| SENSOR-MITTE | ein Tipp übernimmt den aktuellen Zählwert, 0 bis 4095, als Mitte, mit dem Servo in Neutral |
| PWM-ZIEL | AUSGÄNGE, die als SERVO PWM gebundenen Pins, oder PROG-PORT, der Programmierport des Koprozessors |
| PORT-PWM | EIN oder AUS: der Puls auf dem Programmierport, solange er das Ziel ist |

PWM-ZIEL ist gesperrt bei ARMED, während eines Sweeps oder Tests und solange der Servo-Programmierer den Port hält.

## Die Zeile ZIEL

![Die Zeile ZIEL mit PROG-PORT](view:servo-target)

Die Zeile ZIEL der rechten Karte nennt die Pins, die der Tester jetzt treibt. Sie bleibt unter den Optionen und dem Servo-Programmierer sichtbar.

| Zeile | Bedeutung |
|---|---|
| `GP4`, `GP2 GP4` | die als SERVO PWM gebundenen GPIOs (General-Purpose Inputs/Outputs); was nicht passt, steht als `+2` |
| `PROG-PORT GP13` | der Pin des Programmierports, in der Warnfarbe |
| `PROG-PORT GP13 PWM AUS` | der Port ist das Ziel und gibt keinen Puls aus |
| `KEIN SERVO-PWM-PIN` | kein Pin ist als SERVO PWM gebunden |
| `PINS NICHT AUSGELESEN` | keine Bindung wurde gelesen |
| `UNBEKANNT` | die Programmierport-Seite des Koprozessors ist nicht gelesen |
| `KEINE VERBINDUNG` | kein Koprozessor antwortet; der Tester sendet nichts |

Die Zeile folgt dem Ziel, das der Koprozessor hält, nicht der Einstellung. Eine Änderung von PWM-ZIEL wartet auf einen ruhenden Prüfstand.

## Die Werte behalten

AUSGANG, HV-SERVO, ENDLAGENLAUF und SERVOPULSE gelten für die Sitzung. TEST, der Rest von GRENZEN und PRÜFLING werden im NVS (nichtflüchtiger Speicher) gehalten und wie die Netzteil-Optionen geschrieben: im nächsten Frame mit dem Prüfstand SAFE und abgeschaltetem Netzteilausgang.
