---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO steuert ein Servo, misst seinen Strom auf der Servoschiene und führt einen automatischen Test über mehrere Versorgungsspannungen aus. Der Bildschirm öffnet über die Kachel SERVO im [Menü](overview.md). Das Zifferblatt belegt die linke Karte; die Messwerte, die Netzteilzeile, TEMPO, das Ziel und die Tasten belegen die rechte.

## In Ruhe

![SERVO bei SAFE](view:servo)

Solange der Prüfstand SAFE ist, ist das Horn gedimmt gezeichnet, und die linke Karte zeigt `ZUERST ARM`. Das Zifferblatt nimmt keine Position an. Der Koprozessor schreibt auf jeden Servo-PWM-Pin (Pulsweitenmodulation) einen Puls der Länge null.

Die rechte Karte, von oben:

| Zeile | Zeigt |
|---|---|
| PULSBREITE | die Pulsbreite, die der Bildschirm befiehlt, in us |
| GEMESSEN | den Winkel des Horns vom Winkelsensor AS5600, solange er eingeschaltet ist und seinen Magneten sieht, -180,0 bis +179,9 deg; sonst `---` |
| STROM | den Strom der Servoschiene von ihrem Messgerät, in Hundertstel Ampere |
| Typzeile | Servotyp, Frame Rate und Pulsbereich, die gelten: `STANDARD PWM  50 Hz  1000-2000` |
| Messzeile und Grafik | Spannung, Strom und Leistung der Schiene, aufgetragen über die letzten 13 s |
| SOLL | Spannung und Strombegrenzung des Netzteils und sein Ausgangsschalter |
| TEMPO | die Rate, mit der der Prüfstand den Ausgang bewegen darf; 100 % gibt den Befehl unverändert weiter |
| ZIEL | die Pins, die der Tester treibt: `GP2`, `PROG-PORT GP13` oder eine Warnung |

## ARMED

![SERVO bei ARMED SERVO](view:servo-armed)

1. ARM 2 s halten. Das Band zeigt dann ARMED SERVO, und die Taste lautet DISARM.
2. Irgendwo auf dem Bogen ziehen, um eine Position zu befehlen. Der volle Arm ist die gemessene Position, der blasse Arm die befohlene.
3. Das Abheben des Fingers gibt nicht frei: Der Bildschirm wiederholt die letzte Position, das Servo bleibt, wo es hingestellt wurde.
4. DISARM tippen, STOP drücken oder den Bildschirm verlassen, um auf SAFE zu schalten.

ARMED SERVO treibt die Servo-Ausgänge und einen pulsenden PROG PORT. Throttle-Ausgänge bekommen keinen Puls.

| Bedienelement | SAFE | ARMED |
|---|---|---|
| Das Zifferblatt | abgelehnt | befiehlt die Position unter dem Finger |
| Drehknopf | abgelehnt; die Drehung wird nicht gemerkt | bewegt das Horn vom befohlenen Wert aus |
| ZENTRIEREN | abgelehnt; gedimmt gezeichnet | befiehlt PULS CENTRE plus TRIM |
| SWEEP, PAUSE, PAUSIERT | abgelehnt; gedimmt gezeichnet | startet, hält und setzt den Sweep fort |
| FREIGEBEN | sendet die Freigabe | stellt die Ruder in ihre Ruhe: 1500 us bei STANDARD PWM, 760 us bei NARROW 760 |
| TEMPO | einstellbar | einstellbar; eine gehaltene Position wird mit der neuen Rate erneut gesendet |
| TEST STARTEN | abgelehnt: `ZUERST ARM` | startet nach seinem Halten den automatischen Test |

Das Schalten auf ARMED setzt den befohlenen Wert auf die Ruhe, die Mitte zwischen den Endpunkten des Kanals, ohne TRIM. Das erste Ziehen oder Drehen am Knopf beginnt dort. SWEEP ist grau ohne gebundenes Ruder, ohne Link oder an einem Koprozessor älter als Protokoll 4.2.

## Das Messgerät der Servoschiene

Die Zeile über der Grafik nennt das Messgerät der Servoschiene:

| Name | Messgerät |
|---|---|
| `INA3221 CH1` | CH1 des INA3221: Mittelwert von Strom und Spannung seines letzten 50-ms-Fensters, 20 Punkte pro Sekunde |
| `PD MINI` | der letzte Messwert des Netzteils PD mini oder das Netzteilmodell des Panels |

![Der INA3221 als Messgerät der Schiene](view:servo-ina)
![Ein übersteuertes Fenster, in der Warnfarbe](view:servo-clipped)

Der INA3221 wird 1000 ms, nachdem seine Bedingungen erfüllt sind, zum Messgerät: Er ist unter SETUP, ANSCHLÜSSE mit CH1 eingeschaltet, der Koprozessor spricht Protokoll 4.11, und Fenster mit Strommesswerten kommen an. Ein Fenster, in dem ein Messwert das Ende des Bereichs erreicht hat, 1,638 A an einem Shunt von 0,1 Ohm, steht in der Warnfarbe: Sein Mittelwert ist dann eine Grenze.

![Das Netzteilmodell des Panels als Messgerät, mit der Marke SIMULIERT](view:servo-model)

Solange der PD mini unter SETUP, ANSCHLÜSSE abgeschaltet ist, ist das Netzteil das Modell des Panels, und eine Marke SIMULIERT steht auf der Grafik. Seine Messwerte sind simuliert, nicht gemessen.

Ein Messwert, der nicht angekommen ist, steht als `---` in der Zeile STROM, als `--` auf der Messzeile und als Lücke in der Grafik.

## Das Netzteil auf SERVO

Die Zeile SOLL trägt die beiden Sollwerte der Seite SUPPLY und ihren Ausgangsschalter. Eine Änderung auf einem der beiden Bildschirme gilt auf beiden.

- Ein Tipp auf einen Sollwert öffnet das Tastenfeld über der linken Karte.
- AUSGANG EIN ist ein Halten von 2 s, AUSGANG AUS ein Tipp.
- Das Verlassen von SERVO schaltet einen Ausgang ab, den SERVO eingeschaltet hat.

![NUR HV-SERVOS](view:servo-hv)

Standardservos sind für 4,8 bis 6,0 V ausgelegt. Eine Spannung, die von 6,0 V oder darunter auf über 6,0 V steigt, öffnet NUR HV-SERVOS. Die Spannung gilt, nachdem HALTEN ZUM ÜBERNEHMEN 2 s gehalten wurde; ABBRECHEN verwirft sie. AUSGANG EIN mit einem Sollwert über 6,0 V öffnet dieselbe Warnung.

## SWEEP und PAUSE

SWEEP bewegt das Servo entlang der KURVE der Seite TEST (Rechteck, Sinus oder Dreieck) mit deren TEMPO, 0,05 bis 5 Hz, über den BEREICH, 10 bis 100 % des Wegs. Der Koprozessor taktet die Kurve; das Timing des Links formt sie nicht. Solange ein Sweep läuft, lautet die Taste PAUSE. Ein Tipp hält das Servo, wo es steht, und die Taste lautet PAUSIERT; ein Tipp auf PAUSIERT setzt den Sweep von dieser Stelle fort.

![Ein angehaltener Sweep](view:servo-paused)

![TEMPO BEGRENZT DEN SWEEP](view:servo-sweep)

`TEMPO BEGRENZT DEN SWEEP` ersetzt in der Warnfarbe die Beschriftung von TEMPO, solange TEMPO langsamer ist als die schnellste Änderung, die die Kurve verlangt. Die Kurve läuft dann als Rampen. TEMPO erhöhen oder TEMPO bzw. BEREICH der Seite TEST senken. Mit den Vorgaben der Seite TEST (Sinus, 0,5 Hz, BEREICH 80 %) und WEG 90 deg je Richtung verschwindet die Warnung bei TEMPO 63 %.

Ein Finger auf dem Zifferblatt, ZENTRIEREN, FREIGEBEN, STOP, ein Schalten auf SAFE und das Verlassen des Bildschirms beenden einen Sweep.

## Der automatische Test

TEST STARTEN auf der Seite TEST führt das Servo durch die dort gewählten Versorgungsspannungen. Auf jeder Stufe misst er den Strom in Ruhe, in Bewegung und beim Halten, misst die Stellzeit und sucht die Spannung, bei der das Servo stehen bleibt. TEST STARTEN ist ein Halten von 2 s. Es braucht den Prüfstand bei ARMED und ein Netzteil, das antwortet; sonst nennt die Zeile darunter den Grund: `ZUERST ARM`, `KEINE STUFE GEWÄHLT`, `NETZTEIL ANTWORTET NICHT`, `BEREICH ZU KLEIN`, `STUFE ÜBER DEN OBERGRENZEN` oder `LETZTER BERICHT SCHREIBT NOCH`.

![Ein Testlauf](view:servo-run)

Die Optionen schließen beim Start des Laufs. Stufe und Phase stehen oben auf der linken Karte, das Messgerät neben TEST BEENDEN: `PD mini`, `INA3221 CH1` oder `MODELL`. Der Lauf endet vorzeitig, mit abgeschaltetem Ausgang und freigegebenem Servo, bei TEST BEENDEN, STOP, einem Schalten auf SAFE, einem verlorenen Link, dem Verlassen des Bildschirms, einem Finger auf dem Zifferblatt, ZENTRIEREN, SWEEP, FREIGEBEN, einem Tipp auf einen Sollwert oder einer Änderung an den Servo-Optionen.

![Ein Ergebnis](view:servo-result)
![Ein Ergebnis, vom INA3221 gemessen](view:servo-result-ina)

Das Ergebnis bleibt bis SCHLIESSEN: BESTANDEN, NICHT BESTANDEN oder ABGEBROCHEN und warum, die längste Stellzeit, der höchste Haltestrom und die Dateien, die die Karte aufgenommen hat, `BENCHnnn.CSV` und `+ .TXT` für den Bericht.

## Aktuelle Einschränkungen

- Am Netzteilmodell des Panels sind die Ströme des Tests die Last des Modells, nicht die des Servos.
- Bis der Bildschirm eine Position sendet, laufen die Pins mit der Rate, die der Koprozessor hält, nach einem Neustart 50 Hz.

## Verwandte Seiten

- [Servo-Optionen](servo-settings.md): Typ, Frame Rate, Pulse, der Test, die Grenzen und das Ziel.
- [Servo-Programmierer](servo-programmer.md): PROGRAMM., für KST- und Chaservo-Servos.
- [Netzteil](supply.md) und [Anschlüsse](setup-interfaces.md): der PD mini, der INA3221 und der AS5600.
