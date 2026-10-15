---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO steuert ein Servo, misst seinen Strom auf der Servoschiene, führt einen automatischen Test über mehrere Versorgungsspannungen aus und sucht die Weggrenzen des eingebauten Servos. Der Bildschirm öffnet über die Kachel SERVO im [Menü](overview.md). Das Zifferblatt belegt die linke Karte; die Messwerte, die Netzteilzeile, TEMPO, das Ziel und die Tasten belegen die rechte.

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

Die Optionen schließen beim Start des Laufs. Stufe und Phase stehen oben auf der linken Karte, das Messgerät neben TEST BEENDEN: `PD mini`, `INA3221 CH1` oder `MODELL`. Der Lauf endet vorzeitig, mit abgeschaltetem Ausgang und freigegebenem Servo, bei TEST BEENDEN, STOP, einem Schalten auf SAFE, einem verlorenen Link, dem Verlassen des Bildschirms, einem Finger auf dem Zifferblatt, ZENTRIEREN, SWEEP, FREIGEBEN, einem Tipp auf einen Sollwert oder einer Änderung an den Servo-Optionen. Ein Lauf am INA3221 endet auch, wenn der INA3221 nicht mehr das Messgerät ist oder 200 ms lang (4 Fenster zu 50 ms) kein Fenster mit Strom und Spannung kommt: `0.2 s kein INA3221-Fenster`.

![Ein Ergebnis](view:servo-result)
![Ein Ergebnis, vom INA3221 gemessen](view:servo-result-ina)

Das Ergebnis bleibt bis SCHLIESSEN: BESTANDEN, NICHT BESTANDEN oder ABGEBROCHEN und warum, die längste Stellzeit, der höchste Haltestrom und die Dateien, die die Karte aufgenommen hat, `BENCHnnn.CSV` und `+ .TXT` für den Bericht.

## Der Endlagenlauf

Der Endlagenlauf sucht, wo das eingebaute Servo an jedem Ende seines Wegs anläuft. Danach kann er LEFT ANGLE und RIGHT ANGLE des Servos senken, sodass das Servo vor der Stelle anhält, an der es anläuft. Er liegt auf der [Seite GRENZEN](servo-settings.md#grenzen) der Optionen.

![Der Endlagenlauf, bereit](view:servo-limrun-ready)

1. ENDLAGENLAUF antippen und NUR SUCHE oder SUCHE+PROG wählen.
2. ENDLAGENLAUF STARTEN 2 s halten, während der Prüfstand ARMED ist. Ein Tipp oder ein kürzeres Halten startet nichts.
3. Die Optionen schließen. Der Kasten auf der linken Karte zeigt die Phase, die Seite und den Puls sowie den Ausgang, den er treibt.

Die Zeile unter der Taste nennt die ganze Zeit Ausgang und Messgerät: `AUSGANG PROG PORT GP13, STROM INA3221 CH1`. Die Zeile ZIEL bleibt ebenfalls sichtbar.

Der Lauf liest nur INA3221 CH1. Mit dem PD mini oder dem Modell als Messgerät der Servoschiene bleibt ENDLAGENLAUF STARTEN grau, und die Zeile nennt den Grund:

| Zeile | Bedingung |
|---|---|
| `ZUERST ARM` | der Prüfstand ist SAFE |
| `INA3221 NICHT FÜR DIE SERVOSCHIENE EINGERICHTET` | der INA3221 oder sein CH1 ist unter SETUP, ANSCHLÜSSE aus |
| `INA3221 NICHT ONLINE` | kein Link, oder der INA3221 antwortet nicht |
| `NOCH KEINE INA3221-FENSTER` | der INA3221 ist noch nicht das Messgerät |
| `PORT PWM AUS: DER PORT GIBT KEINEN PULS AUS` | PWM-ZIEL ist PROG-PORT und PORT-PWM ist aus |
| `MITTE NICHT ZWISCHEN PULSE MIN UND MAX` | der Pulsbereich enthält PULS CENTRE nicht |
| `LETZTER BERICHT SCHREIBT NOCH` | ein Test oder ein Lauf ist im Gang, oder sein Bericht geht noch auf die Karte |

### Die Suche

Die Suche geht von PULS CENTRE nach außen bis PULS MIN, dann bis PULS MAX, 10 us je Schritt. Jede Stellung dauert 250 ms: 120 ms Einschwingen, dann das Mittel der nächsten Fenster. Ein Ende läuft an, wenn der Strom über dem 1,8-Fachen des freien Stroms liegt und ihn um mindestens 0,15 A übersteigt. Der Endpunkt wird 25 us vor der Stellung gemeldet, an der das Servo anläuft. PULS MIN und PULS MAX des Prüfstands bleiben unverändert.

### LEFT ANGLE und RIGHT ANGLE programmieren

SUCHE+PROG braucht ein KST- oder Chaservo-Servo am Programmierport: PWM-ZIEL auf PROG-PORT, den Port in PWM und das Netzteil auf 4,8 V oder mehr. Sonst zeigt ENDLAGENLAUF NUR SUCHE, und die dritte Zeile nennt den Grund.

![Der Endlagenlauf beim Programmieren](view:servo-limrun)

Der Lauf schreibt in Sitzungen des [Servo-Programmierers](servo-programmer.md):

1. Das Servo geht für 1000 ms zurück auf PULS CENTRE.
2. Der Lauf schaltet den Prüfstand auf SAFE.
3. VERBINDEN liest das Servo.
4. Ein Schreiben von LEFT ANGLE und RIGHT ANGLE, mit jeder Grenzprüfung des Programmierers.
5. VERLASSEN gibt den Port an PWM zurück.
6. Der Lauf schaltet den Prüfstand auf ARMED und sucht diese Seite erneut.

Die erste Sitzung liest nur. Das erste Schreiben sichert die Werte des Servos in eine Backup-Datei auf der Karte. Der Lauf findet, welches Register welche Seite begrenzt, und halbiert dann jede anlaufende Seite bis auf 2 Zählwerte. Er programmiert den letzten freien Wert minus 5 %, mindestens 2 Zählwerte, nie unter 1. Zuletzt sucht er beide Enden erneut. Ein Lauf schreibt höchstens 24 Werte. Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt. Ein Wert unter 50 ist eine weiche Grenze: Das Halten von 2 s beim Start ist die Zustimmung dazu.

### Was einen Endlagenlauf beendet

Jeder dieser Gründe beendet den Lauf als ABGEBROCHEN, mit abgeschaltetem Ausgang und freigegebenem Servo:

- LAUF BEENDEN, STOP, ein Schalten auf SAFE, ein verlorener Link, das Verlassen des Bildschirms, eine Änderung an den Servo-Optionen;
- der INA3221 nicht mehr das Messgerät, oder 200 ms kein INA3221-Fenster (`0.2 s kein INA3221-Fenster`);
- während einer Suche ein Fenster ohne Schienenspannung (`kein Spannungswert im Fenster`);
- ein Messwert ab 3,0 A, ein Messwert am Ende des Bereichs des INA3221 oder Fenstermittel über 1,0 A für 400 ms;
- eine Suche länger als 40 s, ein Prüfstand, der nicht in 3 s schaltet, oder ein Schritt des Programmierers länger als 150 s;
- während einer Suche eine Änderung an Sollwerten oder Ausgang des Netzteils oder die Schiene mehr als 1,0 V vom ersten Fenster des Laufs entfernt.

![Ein nach einem Schreiben beendeter Lauf](view:servo-limrun-stopped)

Nach einem Schreiben behält das Servo die zuletzt geschriebenen Werte. Der Kasten zeigt dann `ZURÜCKSPIELEN IN PROGRAMM.`. Im Programmierer bietet ZURÜCKSPIELEN nach VERBINDEN und ALLES LESEN `Werte vor dem Endlagenlauf` an.

![ZURÜCKSPIELEN nach einem Endlagenlauf](view:servo-limrun-restore)

### Das Ergebnis

Der Kasten bleibt bis SCHLIESSEN. Er zeigt das Urteil, den Grund oder die Werte und den Dateinamen des Berichts.

| Urteil | Bedeutung |
|---|---|
| GEFUNDEN | nur Suche: ein Ende läuft an; die Endpunkte werden gemeldet |
| FREI | kein Ende läuft an; nichts wird geschrieben |
| BESTANDEN | programmiert, und beide Enden in der letzten Suche frei: `LEFT 150>84  RIGHT 150>91` |
| NICHT BESTANDEN | ein Ende läuft weiterhin an, oder das Servo lässt sich nicht programmieren |
| ABGEBROCHEN | eine Schutzfunktion, der Bediener, der Link, das Messgerät, das Netzteil oder der Programmierer hat ihn beendet |

![Ein programmiertes Servo](view:servo-limrun-pass)
![Nur Suche](view:servo-limrun-found)

Der Bericht ist eine eigene `BENCHnnn.TXT`, geschrieben unabhängig von BERICHT. Er enthält die Endpunkte beider Suchen, LEFT ANGLE und RIGHT ANGLE vorher und nachher, die geschriebenen Werte, die gesendeten Schreibvorgänge und die Sitzungen, den Abstand, das Messgerät, mit eingebautem AS5600 den Winkel an jeder letzten freien Stellung, und das Urteil oder den Grund.

## Aktuelle Einschränkungen

- Am Netzteilmodell des Panels sind die Ströme des Tests die Last des Modells, nicht die des Servos.
- Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt.
- Bis der Bildschirm eine Position sendet, laufen die Pins mit der Rate, die der Koprozessor hält, nach einem Neustart 50 Hz.

## Verwandte Seiten

- [Servo-Optionen](servo-settings.md): Typ, Frame Rate, Pulse, der Test, die Grenzen und das Ziel.
- [Servo-Programmierer](servo-programmer.md): PROGRAMM., für KST- und Chaservo-Servos.
- [Netzteil](supply.md) und [Anschlüsse](setup-interfaces.md): der PD mini, der INA3221 und der AS5600.
