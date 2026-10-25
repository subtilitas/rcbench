---
screen: none
title: Servo-Optionen
order: 21
---

# Servo-Optionen

OPTIONEN, der dritte Reiter der Leiste von [SERVO](servo.md), zeigt die Optionen des Servos im Seitenbereich auf drei Seiten: AUSGANG, SWEEP und PRÜFLING. Die Optionen jedes Laufs öffnen sich von seiner Karte auf TEST: OPTIONEN auf der Karte zeigt sie als Liste eine Ebene tiefer, und SCHLIESSEN oben rechts in der Liste oder das Home-Tag führt zurück zu den Karten. Die Ausgangskarte rechts bleibt an ihrem Platz und wirkt. Ein Wert öffnet das Tastenfeld, eine Liste öffnet eine Liste, ein Schalter schaltet beim Tipp um, und NAME öffnet eine Tastatur.

## AUSGANG

![Die Seite AUSGANG](view:servo-settings)

| Option | Bereich | Was sie tut |
|---|---|---|
| TYP | STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760 | das Servoprofil |
| PULSRATE | die Liste des Typs, oder EIGENE EINSTELLUNG über das Tastenfeld | wie oft ein Puls gesendet wird |
| PULS MIN, PULS MITTE, PULS MAX | 400 bis 2500us; jedes Ende mindestens 50us von PULS MITTE entfernt | -90deg ist MIN, 0 ist MITTE, +90deg ist MAX |
| TRIMMUNG | -200 bis +200us, 5us je Schritt | wird zur Mitte addiert; ein Tippen auf den Wert öffnet das Tastenfeld mit der Taste `+/-` |
| WEG | 10 bis 90deg | wie weit das Horn je Richtung gehen darf |
| UMKEHREN | EIN, AUS | die Richtung, in der der Winkel auf den Puls abgebildet wird |

| Typ | Mitte | Weg | Frame Rates |
|---|---|---|---|
| STANDARD PWM | 1500us | 1000-2000us | 50, 60, 100, 150, 200, 250, 300, 333Hz |
| NARROW 760 | 760us | 660-860us | wie STANDARD PWM |
| WIDE | 1500us | 800-2200us | wie STANDARD PWM, bis 312Hz |
| HELI CYCLIC | 1520us | 820-2220us | 50, 120, 200, 333Hz |
| HELI TAIL 760 | 760us | 410-1110us | 200, 333, 560Hz |

Ein Puls braucht eine Pause vor dem nächsten Frame: mindestens 1ms bei STANDARD PWM, NARROW 760 und WIDE, 0,5ms bei den Heli-Typen. Eine Frame Rate, die den Pulsen keine Pause lässt, wird abgelehnt.

Die Optionen von AUSGANG gelten für die Sitzung. Jeder Neustart kehrt zu STANDARD PWM mit 50Hz zurück.

![Das Tastenfeld der TRIMMUNG, -25us getippt](view:servo-trim-keypad)

Das Tastenfeld der TRIMMUNG hat zwischen dem Punkt und OK eine Taste `+/-`: Sie setzt ein Minuszeichen vor die Eingabe oder nimmt es weg. OK nimmt -200 bis +200us und legt den Wert auf den näheren 5-us-Schritt: 13 wird 15, -12 wird -10. -201 lässt das Tastenfeld offen und nennt den Bereich in der Warnfarbe.

## Die Warnung bei schnellen Raten

![KANN DAS SERVO ZERSTÖREN](view:servo-warning)

Ein Heli-Typ oder jede Frame Rate über 60Hz kann ein Servo zerstören, das nicht dafür gebaut ist. Die Wahl öffnet eine Warnung in der Gefahrfarbe, die die Wahl nennt. Sie gilt, nachdem 2s HALTEN ZUM ÜBERNEHMEN gehalten wurde. ABBRECHEN, ein Finger, der abrutscht, und das Verlassen des Bildschirms übernehmen nichts. Typ und Rate, die gelten, bleiben auf der rechten Karte rot.

Die Seite AUSGANG sagt, was aus der Rate wurde:

| Hinweis | Bedeutung |
|---|---|
| `Aktiv: AUSGÄNGE laufen mit 330Hz.` | der IOmcu fährt die Rate auf dem Ziel, das er hält |
| `Die Pulsrate wird mit der nächsten Position an den IOmcu gesendet.` | noch nicht geschrieben |
| `ABGELEHNT: ...` | ein Puls ist länger als der Frame, oder ein Pin teilt den PWM-Slice mit anderer Rate; die Pins behalten ihre Rate |
| `Keine Verbindung. Die aktive Pulsrate ist unbekannt.` | kein IOmcu antwortet |

## SWEEP

![Die Seite SWEEP](view:servo-sweep-page)

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| KURVE | RECHTECK, SINUS, DREIECK | SINUS | die Kurve von SWEEP und der Bewegungen des Tests |
| TEMPO | 0,05 bis 5Hz | 0,5Hz | Kurven pro Sekunde |
| BEREICH | 10 bis 100% | 80% | der Anteil des Wegs, der durchfahren wird |

## Die Optionen des automatischen Tests

![Die Optionen der Karte AUTOMATISCHER TEST](view:servo-test-settings)

OPTIONEN auf der Karte AUTOMATISCHER TEST öffnet sie.

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| TESTDAUER NACH | ZEIT, BEWEGUNGEN | ZEIT | wie jede Netzteilstufe bemessen wird |
| TESTZEIT | 5 bis 3600s | 60s | die Dauer einer Stufe |
| BEWEGUNGEN | 1 bis 1000 | 20 | die Bewegungen einer Stufe |
| HALTEZEIT | 0 bis 5000ms | 200ms | an jedem Ende gehalten, im Test und bei SWEEP |
| EINSCHWINGZEIT | 0 bis 10000ms | 500ms | Wartezeit nach einer Netzteilstufe vor dem Messen |
| BLOCKIERT AB | 0,1 bis 5,0A | 2,00A | über diesem Strom gilt das Servo als blockiert |
| RUHESTROM | 0 bis 5,00A | 0, nicht geprüft | nicht bestanden über diesem Wert in Ruhe |
| HALTESTROM | 0 bis 5,00A | 0, nicht geprüft | nicht bestanden über diesem Wert beim Halten eines Endes |
| STUFE 4.8V, STUFE 6.0V | EIN, AUS | EIN | die Netzteilstufen |
| HV-SERVO | EIN, AUS | AUS | fügt STUFE 7.4V und STUFE 8.4V hinzu; bei jedem Neustart aus |
| BROWN-OUT | EIN, AUS | EIN | ab 5,0V abwärts, bis das Servo stehen bleibt |
| STELLZEIT | 0 bis 5000ms | 0, nicht geprüft | nicht bestanden über diesem Wert von Ende zu Ende |

![HV-SERVO eingeschaltet](view:servo-hv)

Ist HV-SERVO eingeschaltet und eine Stufe über 6,0V gewählt, öffnet TEST STARTEN auf der Karte zuerst NUR HV-SERVOS und nennt die höchste Stufe.

## Die Optionen des Endlagenlaufs

![Die Optionen der Karte ENDLAGENLAUF](view:servo-limits)

OPTIONEN auf der Karte ENDLAGENLAUF öffnet sie.

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| ENDLAGENLAUF | NUR SUCHE, SUCHEN + PROGRAMMIEREN | NUR SUCHE | was ENDLAGENLAUF STARTEN tut: die Enden suchen oder zusätzlich LEFT ANGLE und RIGHT ANGLE programmieren |
| SERVOPULSE | BEHALTEN, 1000-2000 | BEHALTEN | nur mit ZIEL auf PROG-PORT angezeigt: 1000-2000 schreibt PULSE LOWER und PULSE UPPER des Servos vor der Suche auf 1000,0 und 2000,0us |

## Die Grenzen von SUPPLY

Die Spannungs- und Stromgrenzen, in denen die Sollwerte und die Stufen eines Laufs bleiben, stehen nur lesbar unter den Karten auf TEST und auf PRÜFLING: `Grenzen 21.00V 5.00A: SUPPLY > OPTIONEN`, die geltenden Werte. Eingestellt werden sie auf [SUPPLY](supply.md) > OPTIONEN.

## PRÜFLING

![NAME auf der Tastatur](view:servo-name)
![Die Seite PRÜFLING mit eingeschaltetem Winkelsensor](view:servo-encoder)

| Option | Was sie tut |
|---|---|
| NAME | der Prüfling, bis 23 Zeichen; steht über jedem Bericht |
| BERICHT | ein Textbericht, `ATESTnnn.TXT`, neben dem Log jedes Tests, solange User-Logs unter SETUP > SD CARD EIN ist |
| SENSOR-MITTE | ein Tipp übernimmt den aktuellen Zählwert, 0 bis 4095, als Mitte, mit dem Servo in Neutral |

AS5600, der Winkelsensor an der Hornwelle, wird nur unter [Anschlüsse](setup-interfaces.md) geschaltet. PORT-PWM, der Puls auf dem Programmierport, wird nur auf dem Eintrag PROG-PORT von [Ausgänge](outputs.md#der-eintrag-prog-port) gestellt.

## Die Zeile ZIEL

![Die Zeile ZIEL mit PROG-PORT](view:servo-target)

Die Zeile ZIEL der Ausgangskarte nennt, was der Tester jetzt treibt, als `Protokoll Pins`, und wählt es, wie auf MOTOR & ESC: ein Tipp öffnet [OUTPUTS](outputs.md#der-eintrag-prog-port) auf dem, was die Zeile zeigt: dem Eintrag PROG-PORT, solange der IOmcu den Port treibt, SERVO PWM, solange er die Ausgänge treibt, der Übersicht, solange das Ziel nicht bekannt ist. Dort wählt SERVO-TESTER OUTPUTS, die als SERVO PWM gebundenen Pins, oder PROG-PORT, den Programmierport des IOmcu. Das Verlassen von SERVO schaltet den Prüfstand auf SAFE und beendet einen Sweep oder einen Test; das Home-Tag auf OUTPUTS führt zurück zu SERVO, auf dieselbe Aufgabe. Die Zeile bleibt auf jeder Aufgabe sichtbar, unter den Seiten und unter dem Servo-Programmierer. Gesperrt ist sie bei ARMED, während eines Sweeps, eines automatischen Tests oder eines Endlagenlaufs und solange der Servo-Programmierer den Port hält.

![Der Eintrag PROG-PORT von OUTPUTS](view:outputs-port)

| Zeile | Bedeutung |
|---|---|
| `SERVO PWM GP4`, `SERVO PWM GP2 GP4` | die als SERVO PWM gebundenen GPIOs (General-Purpose Inputs/Outputs); was nicht passt, steht als `+2` |
| `PROG-PORT 1 GP13` | Programmierport 1 und sein Pin, in der Warnfarbe |
| `PORT 1 GP13 PWM AUS` | der Port ist das Ziel und gibt keinen Puls aus |
| `KEIN SERVO-PWM-PIN` | kein Pin ist als SERVO PWM gebunden |
| `PINS NICHT AUSGELESEN` | keine Bindung wurde gelesen |
| `UNBEKANNT` | die Programmierport-Seite des IOmcu ist nicht gelesen |
| `KEINE VERBINDUNG` | kein IOmcu antwortet; der Tester sendet nichts |

Die Zeile folgt dem Ziel, das der IOmcu hält, nicht der Einstellung. Eine Änderung von ZIEL wartet auf einen ruhenden Prüfstand.

## Die Werte behalten

AUSGANG, HV-SERVO, ENDLAGENLAUF und SERVOPULSE gelten für die Sitzung. SWEEP, die Optionen des automatischen Tests, PRÜFLING und ZIEL werden im NVS (nichtflüchtiger Speicher) gehalten und wie die Netzteil-Optionen geschrieben: im nächsten Frame mit dem Prüfstand SAFE und abgeschaltetem Netzteilausgang.
