---
screen: none
title: Servo-Optionen
order: 21
---

# Servo-Optionen

OPTIONEN in der Leiste von [SERVO](servo.md) enthält drei Seiten: AUSGANG, SWEEP und PRÜFLING. Jeder Lauf hat eigene Optionen. Öffne OPTIONEN auf seiner TEST-Karte für die Liste; SCHLIESSEN oben rechts oder das Home-Feld führt zurück. Die Ausgangskarte bleibt rechts sichtbar und bedienbar. Tippe auf eine Zahl für das Tastenfeld, auf eine Auswahl für deren Liste, auf einen Schalter zum Umschalten oder auf NAME für die Tastatur.

## AUSGANG

![Die Seite AUSGANG](view:servo-settings)

| Option | Bereich | Wirkung |
|---|---|---|
| TYP | STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760 | wählt das Servoprofil |
| PULSRATE | Liste des Typs oder EIGENE EINSTELLUNG über das Tastenfeld | Häufigkeit der ausgegebenen Pulse |
| PULS MIN, PULS MITTE, PULS MAX | 400 bis 2500us; jedes Ende mindestens 50us von PULS MITTE entfernt | ordnet -90° MIN, 0° MITTE und +90° MAX zu |
| TRIMMUNG | -200 bis +200us, Schritte von 5us | wird zur Mitte addiert; ein Tipp öffnet das Tastenfeld mit `+/-` |
| WEG | 10 bis 90° | begrenzt den Hornweg je Richtung |
| UMKEHREN | EIN, AUS | kehrt die Zuordnung von Winkel zu Puls um |

| Typ | Mitte | Weg | Pulsraten |
|---|---|---|---|
| STANDARD PWM | 1500us | 1000-2000us | 50, 60, 100, 150, 200, 250, 300, 333Hz |
| NARROW 760 | 760us | 660-860us | wie STANDARD PWM |
| WIDE | 1500us | 800-2200us | wie STANDARD PWM, bis 312Hz |
| HELI CYCLIC | 1520us | 820-2220us | 50, 120, 200, 333Hz |
| HELI TAIL 760 | 760us | 410-1110us | 200, 333, 560Hz |

Zwischen Pulsende und nächstem Frame ist eine Pause nötig: mindestens 1ms bei STANDARD PWM, NARROW 760 und WIDE, 0,5ms bei den Heli-Typen. Eine Pulsrate ohne ausreichende Pause wird abgelehnt.

Die Optionen von AUSGANG gelten für die Sitzung. Jeder Neustart kehrt zu STANDARD PWM mit 50Hz zurück.

![Das Tastenfeld der TRIMMUNG, -25us getippt](view:servo-trim-keypad)

Das Tastenfeld für TRIMMUNG hat zwischen Dezimalpunkt und OK die Taste `+/-`. Sie setzt oder entfernt das Minuszeichen vor der Eingabe. OK nimmt -200 bis +200us an und rundet auf den nächsten Schritt von 5us: 13 wird 15, -12 wird -10. Bei -201 bleibt das Tastenfeld offen und hebt den Bereich in der Warnfarbe hervor.

## Die Warnung bei schnellen Raten

![KANN DAS SERVO ZERSTÖREN](view:servo-warning)

Ein Heli-Typ oder eine Pulsrate über 60Hz kann ein ungeeignetes Servo zerstören. Die Auswahl öffnet eine Warnung in der Gefahrfarbe und nennt den gewählten Typ oder die Rate. Halte 2s HALTEN ZUM ÜBERNEHMEN, um die Wahl anzuwenden. ABBRECHEN, Abrutschen mit dem Finger oder Verlassen der Seite verwirft sie. Ein aktiver gefährlicher Typ oder eine solche Rate bleibt auf der rechten Karte rot.

AUSGANG zeigt den Status der Pulsrate:

| Hinweis | Bedeutung |
|---|---|
| `Aktiv: Ausgänge laufen mit 330Hz.` | Der IOmcu verwendet die Rate am aktiven Ziel. |
| `Die Pulsrate wird mit der nächsten Position an den IOmcu gesendet.` | Noch nicht geschrieben. |
| `ABGELEHNT: ...` | Ein Puls ist länger als der Frame, oder ein Pin teilt einen PWM-Slice mit anderer Rate. Die Pins behalten ihre Rate. |
| `Keine Verbindung. Die aktive Pulsrate ist unbekannt.` | Kein IOmcu antwortet. |

## SWEEP

![Die Seite SWEEP](view:servo-sweep-page)

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| KURVE | RECHTECK, SINUS, DREIECK | SINUS | die Kurve von SWEEP und der Bewegungen des Tests |
| TEMPO | 0,05 bis 5Hz | 0,5Hz | Kurvenzyklen pro Sekunde |
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
| BROWN-OUT | EIN 3.0/0.2V, AUS | EIN | der Brown-out-Lauf: ein Tippen öffnet seine Seite |
| STELLZEIT | 0 bis 5000ms | 0, nicht geprüft | nicht bestanden über diesem Wert von Ende zu Ende |

![HV-SERVO eingeschaltet](view:servo-hv)

Ist HV-SERVO eingeschaltet und eine Stufe über 6,0V gewählt, öffnet TEST STARTEN auf der Karte zuerst NUR HV-SERVOS und nennt die höchste Stufe.

## Der Brown-out-Lauf

![Die Seite des Brown-out-Laufs](view:servo-brownout)

BROWN-OUT in den Optionen des automatischen Tests öffnet die Seite des Laufs; SCHLIESSEN führt zurück. Die Zeile fasst NIEDRIGSTE SPANNUNG und SCHRITT zusammen, etwa `EIN 3.0/0.2V`. Nach den Netzteilstufen beginnt der Lauf bei 5,0V oder der niedrigeren Spannungsgrenze. Er senkt die Spannung jeweils um SCHRITT bis zur NIEDRIGSTEN SPANNUNG und endet bei der ersten Spannung ohne Bewegung.

| Option | Bereich | Vorgabe | Wirkung |
|---|---|---|---|
| BROWN-OUT | EIN, AUS | EIN | führt den Lauf nach den Netzteilstufen aus |
| NIEDRIGSTE SPANNUNG | 2,0 bis 4,8V | 3,0V | letzte Spannung des Laufs; ein höherer minimaler Netzteilsollwert beendet ihn bereits dort |
| SCHRITT | 0,1 bis 1,0V | 0,2V | Spannungsabnahme je Stufe; ein kürzerer letzter Schritt endet genau bei NIEDRIGSTE SPANNUNG |

Der Hinweis darunter beschreibt den Lauf mit dem aktiven Netzteil und die Anzahl angeforderter Spannungen, höchstens 31. Liegt NIEDRIGSTE SPANNUNG bei oder über der Startspannung, wird nur die Startspannung geprüft.

## Die Optionen des Endlagenlaufs

![Die Optionen der Karte ENDLAGENLAUF](view:servo-limits)

OPTIONEN auf der Karte ENDLAGENLAUF öffnet sie.

| Option | Bereich | Vorgabe | Wirkung |
|---|---|---|---|
| ENDLAGENLAUF | NUR SUCHE, SUCHEN + PROGRAMMIEREN | NUR SUCHE | ENDLAGENLAUF STARTEN sucht die Enden und programmiert optional LEFT ANGLE und RIGHT ANGLE. |
| SERVO | KST/CHASERVO, HITEC | zuletzt unter PROGRAMMIEREN geöffnete Servoprogrammierung, sonst KST/CHASERVO | Nur bei ZIEL PROG-PORT sichtbar. Wählt die Servoprogrammierung für die Sitzungen des Laufs. HITEC verwendet die in SERIE gewählte Serie ([Hitec-Servos](servo.md#hitec-servos)). |
| SERVOPULSE | BEHALTEN, 1000-2000 | BEHALTEN | Nur bei ZIEL PROG-PORT und SERVO KST/CHASERVO sichtbar. 1000-2000 setzt PULSE LOWER und PULSE UPPER vor der Suche auf 1000,0 und 2000,0us. |

## Die Grenzen von SUPPLY

Die aktiven Spannungs- und Stromgrenzen begrenzen Sollwerte und Laufstufen. Sie stehen unter den TEST-Karten und auf PRÜFLING in einer Taste wie `GRENZEN 21.00V 5.00A: SUPPLY ÖFFNEN`. Tippe darauf, um sie unter [SUPPLY](supply.md) > OPTIONEN > GRENZEN einzustellen. Das Home-Feld führt zurück. Bei gesperrtem ZIEL ist die Taste grau, weil das Verlassen von SERVO den Prüfstand auf SAFE schalten würde.

Der erste Hinweis auf AUSGANG nennt den für SERVO-TESTER gewählten Port: `SERVO-TESTER: PORT 1. Wähle ihn auf OUTPUTS.` Er ist nicht antippbar. Ändere die Auswahl unter [Ausgänge](outputs.md#der-eintrag-prog-port).

## PRÜFLING

![NAME auf der Tastatur](view:servo-name)
![Die Seite PRÜFLING mit eingeschaltetem Winkelsensor](view:servo-encoder)
![KST EXPERT MODE EIN](view:servo-dut-expert)

| Option | Wirkung |
|---|---|
| NAME | Prüflingsname mit bis zu 23 Zeichen; steht über jedem Bericht. |
| BERICHT | Speichert neben jeder Testaufzeichnung einen Textbericht `ATESTnnn.TXT`, solange Testaufzeichnungen unter SETUP > SD CARD EIN ist. |
| SENSOR-MITTE | Übernimmt per Tipp den aktuellen AS5600-Zählwert von 0 bis 4095 als Mitte, während das Servo neutral steht. Bei WINKELQUELLE IMU ohne Wirkung. |
| KST EXPERT MODE | Ob der [Programmierer für KST- und Chaservo-Servos](servo-programmer.md#kst-expert-mode) DEAD BAND, BOOST, STRETCH, PAIRING LÖSEN und den Reiter GESPERRT zeigt; standardmäßig AUS, EIN nach 2s Halten, AUS mit einem Tipp. |

WINKELQUELLE im IMU-Eintrag von [Sensoren](setup-sensors.md) wählt AS5600 oder IMU für die Laufwinkel. IMU-Winkel werden in AS5600-Zählschritten von 0,088° verwendet. Die Test-CSV nennt die Quelle in ihrer letzten Spalte `angle source`, beide Berichte in der Zeile `Winkelquelle:`.

Schalte den AS5600 an der Hornwelle unter [Sensoren](setup-sensors.md) ein oder aus. Die Taste `AS5600 EIN ODER AUS: SENSORS ÖFFNEN` auf PRÜFLING öffnet diesen Eintrag; das Home-Feld führt zurück. PORT-PWM, der Puls am Programmierport, wird ausschließlich im Eintrag PROG-PORT unter [Ausgänge](outputs.md#der-eintrag-prog-port) eingestellt.

## Die Zeile ZIEL

![Die Zeile ZIEL mit PROG-PORT](view:servo-target)

ZIEL auf der Ausgangskarte nennt die angesteuerten Ausgänge als `Protokoll Pins`. Tippe darauf, um [OUTPUTS](outputs.md#der-eintrag-prog-port) zu öffnen: PROG-PORT, wenn der IOmcu den Programmierport ansteuert, SERVO PWM bei den Ausgängen oder die Übersicht bei unbekanntem Ziel. Dort wählt SERVO-TESTER zwischen OUTPUTS (als SERVO PWM zugeordnete Pins) und PROG-PORT (Programmierport des IOmcu).

Das Verlassen von SERVO schaltet den Prüfstand auf SAFE und beendet Sweep oder Test. Das Home-Feld auf OUTPUTS führt zur selben SERVO-Aufgabe zurück. ZIEL bleibt unter den Seiten und der Servoprogrammierung bei jeder Aufgabe sichtbar. Bei ARMED, während Sweep, automatischem Test oder Endlagenlauf und bei belegtem Programmierport ist die Zeile gesperrt.

![Der Eintrag PROG-PORT von OUTPUTS](view:outputs-port)

| Zeile | Bedeutung |
|---|---|
| `SERVO PWM GPIO 4`, `SERVO PWM GPIO 2 GPIO 4` | Als SERVO PWM zugeordnete GPIOs (General-Purpose Inputs/Outputs). Weitere Pins werden als Anzahl wie `+2` angezeigt. |
| `PROG-PORT 1 GPIO 13` | Programmierport 1 und sein Pin, in der Warnfarbe. |
| `PORT 1 GPIO 13 PWM AUS` | Port ist das Ziel, gibt aber keinen Puls aus. |
| `KEIN SERVO-PWM-PIN` | Kein Pin ist als SERVO PWM zugeordnet. |
| `PINS NICHT AUSGELESEN` | Zuordnung noch nicht ausgelesen. |
| `UNBEKANNT` | Programmierport-Page des IOmcu noch nicht ausgelesen. |
| `KEINE VERBINDUNG` | Kein IOmcu antwortet; der Tester sendet nichts. |

Die Zeile folgt dem aktiven Ziel des IOmcu, nicht dem Einstellwert. Eine Änderung von ZIEL wartet auf einen ruhenden Prüfstand.

## Die Werte behalten

AUSGANG, HV-SERVO, ENDLAGENLAUF, SERVO und SERVOPULSE gelten nur für die Sitzung. SWEEP, Optionen des automatischen Tests, PRÜFLING samt KST EXPERT MODE und ZIEL werden im NVS (nichtflüchtiger Speicher) gespeichert. Wie bei den Netzteiloptionen erfolgt das Schreiben im nächsten Frame bei SAFE und ausgeschaltetem Netzteilausgang.
