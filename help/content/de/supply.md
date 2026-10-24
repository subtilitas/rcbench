---
screen: SUPPLY
title: Netzteil
order: 30
---

# Netzteil

SUPPLY stellt, schaltet und zeichnet ein programmierbares Netzteil auf: den PD mini, einen USB-PD-Trigger (USB Power Delivery), den der IOmcu über einen UART (Universal Asynchronous Receiver-Transmitter) an zwei seiner Pins ansteuert. Der Bildschirm öffnet über die Kachel SUPPLY im [Menü](overview.md), immer auf LIVE.

## Die Leiste

Die Leiste oben trägt zwei Aufgaben, LIVE und OPTIONEN. Ein Tipp wechselt die Aufgabe. LIVE behält seine Ansicht, GRAFIK oder TABELLE, über einen Wechsel.

| Aufgabe | Die linke Spalte |
|---|---|
| LIVE | die Grafik oder die Tabelle und die Sollwerte |
| OPTIONEN | die Obergrenzen, die Startwerte, die Abschaltungen und die Rückfragen ([OPTIONEN](#optionen)) |

Die rechte Spalte ist auf beiden Aufgaben dieselbe: die Messwerte und die Karte STEUERUNG mit SPITZENWERTE ZURÜCKSETZEN und AUSGANG EIN an ihrem Fuß, wo SERVO und MOTOR & ESC ARM haben. Die Kopfzeile ist die Zeile rechts neben der Leiste: das Netzteil, sein Zustand und der Bereich, den die Sollwerte annehmen können. Sie bleibt auf beiden Aufgaben und unter dem Tastenfeld und der Rückfrage sichtbar. Das Home-Tag führt eine Ebene nach der anderen zurück: Über der Rückfrage verwirft es die Änderung, wie ABBRECHEN; über einem Tastenfeld schließt es das Tastenfeld und behält den Wert. Ein Wechsel der Aufgabe ist keine Ebene: Von LIVE oder OPTIONEN führt es, wenn nichts offen ist, zum Menü.

## Das Netzteilmodell

![SUPPLY mit abgeschaltetem PD mini: SIMULIERTES NETZTEIL](view:supply)

Ist der PD mini unter SETUP, ANSCHLÜSSE abgeschaltet, rechnet das Panel ein Modell eines Netzteils. Der Kopf lautet `SIMULIERTES NETZTEIL`, und die Kachel im Menü trägt die Marke SIMULIERT. Das Modell ist eine Last von 6Ω mit einem Stromstoß von 1,4A für 0,6s alle 3s, hinter einem Quellwiderstand von 0,05Ω. Seine Messwerte sind nicht gemessen, und nichts am Prüfstand wird versorgt.

## Der PD mini

![Der PD mini eingeschaltet, kein IOmcu liest ihn: ANTWORTET NICHT](view:supply-offline)

Ist der PD mini eingeschaltet, nennt der Kopf das Modul und die Pins des IOmcu, die es tragen, zum Beispiel `PD MINI TX GP12 RX GP13`, aus den Einstellungen PD mini TX und PD mini RX. Ist einer der Pins nicht gesetzt oder liegen beide auf einem GPIO, lautet er `PD MINI OHNE PINS`. Das Panel liest die SUPPLY-Seite des IOmcu alle 100ms.

| Kopf | Bedeutung |
|---|---|
| `ONLINE 38400` | das Modul antwortet; die Zahl ist die verwendete Baudrate |
| `ANTWORTET NICHT` | kein IOmcu ab 0.10.0 antwortet, oder die Messwerte sind älter als 1500ms; ein eingeschalteter Ausgang wird abgeschaltet |

Vor dem ersten Einschalten zeigt die Grafik `Ausgang wurde noch nicht eingeschaltet.`, und die Karten zeigen Striche, bis ein Messwert ankommt.

## Die Messwerte

| Karte | Zeigt |
|---|---|
| SPANN. | die Ausgangsspannung; den Sollwert in Klammern; die niedrigste Spannung des Laufs darunter |
| STROM | den Ausgangsstrom; die Strombegrenzung in Klammern; die Spitze des Laufs darunter |
| LEIST. | die Ausgangsleistung und die Spitze des Laufs |
| MODUS | CV (Constant Voltage) an der eingestellten Spannung, CC (Constant Current) an der Strombegrenzung, ABGESCHALTET nach einer Abschaltung; CC steht in der Warnfarbe |

Die Grafik zeigt Spannung, Strom und Leistung über die letzten 30s, jeden Sollwert als gestrichelte Linie. TABELLE listet dieselben Werte. Der Wert in Klammern ist der Sollwert, den das Netzteil meldet; solange es nicht antwortet, der, den der Bildschirm hält.

## Sollwerte

| Sollwert | Modell | PD mini | Schritttasten |
|---|---|---|---|
| SPANNUNG | 3,3 bis 21V, Schritte von 20mV | 1 bis 20V, Schritte von 10mV | 0,1V |
| STROMBEGRENZUNG | 0,5 bis 5A, Schritte von 50mA | 0,05 bis 3A, Schritte von 10mA | 0,1A |

Die Bereiche des PD mini stammen von der Seite des Herstellers und sind nicht gemessen. Der PD mini gibt nicht mehr ab, als er bekommt: Seine Spannung ist auf 0,7V unter der Eingangsspannung begrenzt, die er meldet; ein Eingang von 5V erlaubt höchstens 4,3V. Die Obergrenzen in OPTIONEN engen beide Bereiche ein, und der gültige Bereich steht am rechten Ende der Kopfzeile.

- Einen Schieberegler ziehen oder auf seine Spur tippen, um den Wert unter dem Finger zu setzen.
- `-0.1` und `+0.1` ändern den Wert schrittweise.
- Ein Tipp auf die Karte SPANN. oder STROM oder auf den Wert am rechten Ende einer Sollwertzeile öffnet das Tastenfeld.

![Das Tastenfeld über der linken Spalte](view:supply-keypad)

Das Tastenfeld zeigt den Bereich in seiner Titelzeile. OK übernimmt einen Wert innerhalb des Bereichs, gerundet auf den Schritt des Netzteils. Ein Wert außerhalb wird abgelehnt, und der Bereich wechselt in die Warnfarbe. ABBRECHEN, oder OK ohne Eingabe, behält den Sollwert.

## Eine Änderung an einem eingeschalteten Ausgang

![Die Rückfrage vor einer Änderung an einem eingeschalteten Ausgang](view:supply-confirm)

Solange der Ausgang ein ist, öffnet ein neuer Sollwert eine Rückfrage, die die Änderung nennt. ÜBERNEHMEN gibt ihn dem Netzteil; ABBRECHEN verwirft ihn, und der Schieberegler geht zurück. Ein Ziehen fragt einmal, beim Loslassen; ein Ziehen, das endet, ohne dass sein Finger abhebt, etwa am Home-Tag mit einem zweiten Finger, fragt nichts, und der Schieberegler geht zurück. Bei abgeschaltetem Ausgang wird nichts gefragt. OPTIONEN schaltet die Rückfrage für den Schieberegler und das Tastenfeld getrennt ab.

## AUSGANG EIN und AUS

AUSGANG EIN ist ein Halten von 2s, mit dem Übergang, den ARM verwendet. AUSGANG AUS ist ein Tipp. Der Ausgang schaltet ab und bleibt bis zum nächsten Halten aus bei:

- STOP, auf jedem Bildschirm;
- Verlassen von SUPPLY, für einen Ausgang, den SUPPLY eingeschaltet hat;
- einem Netzteil, das nicht mehr antwortet oder 3000ms keinen Messwert sendet;
- einer Abschaltung;
- einer Eingangsspannung des PD mini unter dem Sollwert plus 0,5V bei 2 Lesungen in Folge;
- einem Modul, das seinen Ausgang selbst abschaltet;
- einer Änderung an Pins, Baudrate oder Einschalt-Einstellung des PD mini.

Das Band nennt den Grund. Ein Ausgang, den ein anderer Bildschirm eingeschaltet hat, bleibt beim Verlassen von SUPPLY, wie er ist. Nach jedem Neustart ist der Ausgang aus, unabhängig von den Startwerten.

## OPTIONEN

![Die Optionen des Netzteils](view:supply-settings)

OPTIONEN, der zweite Reiter der Leiste, zeigt die Optionen in der linken Spalte. Ein Wert öffnet das Tastenfeld; ein Schalter schaltet beim Tipp um. Ein Tipp auf LIVE bei offenem Tastenfeld schließt es und lässt den Wert, wie ABBRECHEN. Solange die Rückfrage zu einem eingeschalteten Ausgang offen ist, wartet die Leiste auf ÜBERNEHMEN oder ABBRECHEN.

| Option | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| SPANNUNG MAX | 3,3 bis 21V | 21,00V | die höchste Spannung, die ein Sollwert annimmt |
| STROM MAX | 0,5 bis 5A | 5,00A | die höchste Strombegrenzung, die ein Sollwert annimmt |
| STARTSPANNUNG | bis SPANNUNG MAX | 6,00V | der Spannungssollwert nach einem Neustart |
| STARTSTROM | bis STROM MAX | 2,00A | die Strombegrenzung nach einem Neustart |
| ÜBERSTROM | 0 bis 5A | AUS | Ausgang aus, sobald der Strom ABSCHALTZEIT lang darüber liegt |
| ÜBERSPANNUNG | 0 bis 21V | AUS | Ausgang aus, sobald die Spannung ABSCHALTZEIT lang darüber liegt |
| ABSCHALTZEIT | 0 bis 5000ms | 100ms | wie lange ein Messwert über einer Schwelle liegen muss, bevor sie auslöst |
| REGLER UND SCHRITTE | EIN, AUS | EIN | Rückfrage, bevor der Schieberegler einen eingeschalteten Ausgang ändert |
| TASTENFELD | EIN, AUS | EIN | Rückfrage, bevor das Tastenfeld einen eingeschalteten Ausgang ändert |

- Eine Schwelle von 0 ist aus.
- Eine Überstromschwelle auf oder über der STROMBEGRENZUNG löst nicht aus: Das Netzteil hält die Grenze in CC.
- Eine Obergrenze, die unter einen Sollwert gesenkt wird, zieht den Sollwert mit.

Die Optionen werden im NVS (nichtflüchtiger Speicher) gehalten. Sie werden im nächsten Frame geschrieben, wenn der Prüfstand SAFE und der Ausgang aus ist. Die untere Zeile lautet `GESPEICHERT`, `SPEICHERN AUSSTEHEND`, `NICHT GESPEICHERT` oder `ÄNDERUNGEN IN SETUP NICHT GESPEICHERT`.

Mit dem PD mini als Netzteil bieten die OPTIONEN zusätzlich PD MINI ZURÜCKSETZEN. Es schaltet den Ausgang ab und startet das Modul neu, für ein Modul, das ERR zeigt. Ob ein Neustart jedes ERR löscht, ist nicht gemessen.

## Das Log eines Laufs

Ein Lauf ist ein Einschalten des Ausgangs. Die Grafik leert sich, wenn der Ausgang einschaltet, und hält den Lauf, wenn er ausgeht. Die mAh und Wh in der Titelzeile von STEUERUNG zählen den Lauf. SPITZENWERTE ZURÜCKSETZEN startet die niedrigsten und höchsten Werte neu ab dem aktuellen Messwert.

Jeder Lauf wird in eine eigene `SUPPLnnn.CSV` in `BENCH/LOGS` auf der SD-Karte (Secure Digital) geschrieben, solange Auto-Logs unter [SETUP](setup.md#sd-karte) > SD-KARTE EIN ist, eine Zeile alle 50ms, mit Sollwert, Spannung, Grenze, Strom, Leistung, Modus, Ladung, Energie und Quelle, `measured` oder `model`. [Logs](logs.md) öffnet sie.

## Verwandte Seiten

- [Anschlüsse](setup-interfaces.md): PD mini, PD mini TX, PD mini RX und PD mini Baud.
- [Servo](servo.md): dieselben Sollwerte und derselbe Ausgangsschalter auf dem Bildschirm SERVO.
