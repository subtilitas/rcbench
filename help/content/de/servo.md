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
| SOLL-PULSBREITE | die Pulsbreite, die der Bildschirm befiehlt, in us |
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
| ZENTRIEREN | abgelehnt; gedimmt gezeichnet | befiehlt PULS MITTE plus TRIMMUNG |
| SWEEP, PAUSE, PAUSIERT | abgelehnt; gedimmt gezeichnet | startet, hält und setzt den Sweep fort |
| FREIGEBEN | sendet die Freigabe | stellt die Ruder in ihre Ruhe: 1500 us bei STANDARD PWM, 760 us bei NARROW 760 |
| TEMPO | einstellbar | einstellbar; eine gehaltene Position wird mit der neuen Rate erneut gesendet |
| TEST STARTEN | abgelehnt: `ZUERST ARM` | startet nach seinem Halten den automatischen Test |

Das Schalten auf ARMED setzt den befohlenen Wert auf die Ruhe, die Mitte zwischen den Endpunkten des Kanals, ohne TRIMMUNG. Das erste Ziehen oder Drehen am Knopf beginnt dort. SWEEP ist grau ohne gebundenes Ruder, ohne Link oder an einem Koprozessor älter als Protokoll 4.2.

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

Die Optionen schließen beim Start des Laufs. Stufe und Phase stehen oben auf der linken Karte, das Messgerät neben TEST BEENDEN: `PD mini`, `INA3221 CH1` oder `SIMULATION`. Der Lauf endet vorzeitig, mit abgeschaltetem Ausgang und freigegebenem Servo, bei TEST BEENDEN, STOP, einem Schalten auf SAFE, einem verlorenen Link, dem Verlassen des Bildschirms, einem Finger auf dem Zifferblatt, ZENTRIEREN, SWEEP, FREIGEBEN, einem Tipp auf einen Sollwert oder einer Änderung an den Servo-Optionen. Ein Lauf am INA3221 endet auch, wenn der INA3221 nicht mehr das Messgerät ist oder 200 ms lang (4 Fenster zu 50 ms) kein Fenster mit Strom und Spannung kommt: `0.2 s kein INA3221-Fenster`.

![Ein Ergebnis](view:servo-result)
![Ein Ergebnis, vom INA3221 gemessen](view:servo-result-ina)

Das Ergebnis bleibt bis SCHLIESSEN: BESTANDEN, NICHT BESTANDEN oder ABGEBROCHEN und warum, die längste Stellzeit, der höchste Haltestrom und die Dateien, die die Karte aufgenommen hat, `SERVOnnn.CSV` und `+ .TXT` für den Bericht.

## Der Endlagenlauf

Der Endlagenlauf sucht, wo das eingebaute Servo an jedem Ende seines Wegs anläuft. Danach kann er LEFT ANGLE und RIGHT ANGLE des Servos senken, sodass das Servo vor der Stelle anhält, an der es anläuft. Er liegt auf der [Seite GRENZEN](servo-settings.md#grenzen) der Optionen.

![Der Endlagenlauf, bereit](view:servo-limrun-ready)

1. ENDLAGENLAUF antippen und NUR SUCHE oder SUCHEN + PROGRAMMIEREN wählen.
2. Mit PWM-ZIEL auf PROG-PORT SERVOPULSE antippen und BEHALTEN oder 1000-2000 wählen. Es beginnt bei BEHALTEN.
3. ENDLAGENLAUF STARTEN 2 s halten, während der Prüfstand ARMED ist. Ein Tipp oder ein kürzeres Halten startet nichts.
4. Die Optionen schließen. Der Kasten auf der linken Karte zeigt die Phase, die Seite und den Puls sowie den Ausgang, den er treibt.

Die Zeile unter der Taste nennt die ganze Zeit Ausgang und Messgerät: `AUSGANG PROG PORT GP13, STROM INA3221 CH1`. Die Zeile ZIEL bleibt ebenfalls sichtbar.

Der Lauf liest nur INA3221 CH1. Mit dem PD mini oder dem Modell als Messgerät der Servoschiene bleibt ENDLAGENLAUF STARTEN grau, und die Zeile nennt den Grund:

| Zeile | Bedingung |
|---|---|
| `ZUERST ARM` | der Prüfstand ist SAFE |
| `INA3221 NICHT ZUR SERVOSTROMMESSUNG EINGERICHTET` | der INA3221 oder sein CH1 ist unter SETUP, ANSCHLÜSSE aus |
| `INA3221 NICHT ERREICHBAR` | kein Link, oder der INA3221 antwortet nicht |
| `NOCH KEINE INA3221-MESSFENSTER` | der INA3221 ist noch nicht das Messgerät |
| `SERVOVERSORGUNG UNTER 3.0 V ODER NICHT AUSGELESEN` | das letzte INA3221-Fenster hat keine Schienenspannung oder weniger als 3,0 V: ein ausgeschalteter Ausgang liest 0 V |
| `PORT-PWM AUS. KEINE PULSE AN DIESEM PORT.` | PWM-ZIEL ist PROG-PORT und PORT-PWM ist aus |
| `KEIN SERVO-PWM-PIN`, `PINS NICHT AUSGELESEN` | PWM-ZIEL sind die Ausgänge des Prüfstands, und kein Ausgang ist als SERVO PWM gebunden, oder die Bindung ist noch nicht gelesen |
| `MITTE MUSS ZWISCHEN PULS MIN UND PULS MAX LIEGEN` | der Pulsbereich enthält PULS MITTE nicht |
| `LETZTER BERICHT SCHREIBT NOCH` | ein Test oder ein Lauf ist im Gang, oder sein Bericht geht noch auf die Karte |

### Die Suche

Die Suche geht von der Mitte nach außen, zuerst die untere Seite, 10 us je Schritt. Jede Stellung dauert 250 ms: 120 ms Einschwingen, dann das Mittel der nächsten Fenster. Ein Ende läuft an, wenn der Strom über dem 1,8-Fachen des freien Stroms liegt und ihn um mindestens 0,15 A übersteigt. Der Endpunkt wird 25 us vor der Stellung gemeldet, an der das Servo anläuft. PULS MIN und PULS MAX des Prüfstands bleiben unverändert.

| PWM-ZIEL | Suche |
|---|---|
| ein SERVO-PWM-Pin | von PULS MITTE bis PULS MIN und PULS MAX; nichts wird gelesen oder geschrieben |
| PROG-PORT | zuerst eine Lesesitzung, dann von NEUTRAL des Servos bis zu seinem eigenen PULSE LOWER und PULSE UPPER, nie darüber hinaus |

Die Lesesitzung hält das Servo auf PULS MITTE, schaltet den Prüfstand auf SAFE, liest das Servo mit VERBINDEN, gibt den Port mit VERLASSEN zurück und schaltet den Prüfstand auf ARMED. Vorher wird nichts angesteuert. Der Lauf endet vor jeder Suche als NICHT BESTANDEN, wenn:

- das Lesen scheitert (`kein KST- oder Chaservo-Servo` oder `Programmierport ohne Abschluss`): ein Servo, das kein KST oder Chaservo ist, an einem SERVO-PWM-Pin suchen;
- NEUTRAL nicht zwischen PULSE LOWER und PULSE UPPER liegt (`NEUTRAL außerhalb LOWER/UPPER`);
- PULSE LOWER bis PULSE UPPER nicht innerhalb von PULS MIN bis PULS MAX liegt (`Servopulse außerhalb MIN/MAX`). Der Prüfstand bleibt SAFE, und der Kasten nennt beide Bereiche.

![Die Pulse des Servos außerhalb von PULS MIN und PULS MAX](view:servo-limrun-range)

Ein KST-Servo im Lieferzustand hält PULSE LOWER 900,0 us und PULSE UPPER 2100,0 us. Der Bereich des PWM-Typs STANDARD ist 1000-2000 us, also wird dieses Servo bei BEHALTEN abgelehnt. SERVOPULSE auf 1000-2000 wählen, oder einen PWM-Typ, dessen Bereich 900 bis 2100 us enthält.

SERVOPULSE auf 1000-2000 schreibt in der Lesesitzung PULSE LOWER 1000,0 us und PULSE UPPER 2000,0 us, bevor sich etwas bewegt, und liest sie zurück. NEUTRAL wird nicht geschrieben. Ein Servo, das diese Werte schon hält, bekommt kein Schreiben. Mit NUR SUCHE sind diese beiden Werte das einzige Schreiben.

### LEFT ANGLE und RIGHT ANGLE programmieren

SUCHEN + PROGRAMMIEREN braucht ein KST- oder Chaservo-Servo am Programmierport: PWM-ZIEL auf PROG-PORT, den Port in PWM und 4,8 V oder mehr. Mit einem PD mini muss dessen Ausgang eingeschaltet sein, und sein Sollwert zählt; ohne ihn zählt die Servoschiene im letzten Fenster des INA3221. An den Ausgängen des Prüfstands zeigt ENDLAGENLAUF NUR SUCHE, und die dritte Zeile nennt den Grund. Am PROG-PORT beginnt jeder Lauf mit einer Lesesitzung: ein Port nicht in PWM, ein ausgeschalteter Ausgang des PD mini oder eine Versorgung unter 4,8 V hält die Schaltfläche auch für NUR SUCHE grau, und die dritte Zeile nennt die Bedingung.

![Der Endlagenlauf beim Programmieren](view:servo-limrun)

Der Lauf schreibt in Sitzungen des [Servo-Programmierers](servo-programmer.md):

1. Das Servo geht für 1000 ms zurück in die Mitte.
2. Der Lauf schaltet den Prüfstand auf SAFE.
3. VERBINDEN liest das Servo.
4. Ein Schreiben, mit jeder Grenzprüfung des Programmierers.
5. VERLASSEN gibt den Port an PWM zurück.
6. Der Lauf schaltet den Prüfstand auf ARMED und sucht diese Seite erneut.

Das erste Schreiben sichert die Werte des Servos in eine Backup-Datei auf der Karte. Der Lauf nimmt das Servo als linear: PULSE LOWER und PULSE UPPER sind die Pulse, bei denen es den vollen Weg erreicht, den LEFT ANGLE und RIGHT ANGLE setzen. Der Anteil einer Seite beim Puls p ist `(p - NEUTRAL) / (PULSE LOWER oder UPPER - NEUTRAL)`. Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt, und der lineare Zusammenhang ist eine Annahme.

Jede anlaufende Seite bekommt zwei Schreibvorgänge, v0 der Wert, den ihr Register hielt:

1. Der Vorhalt: v0 x der Anteil am ersten angelaufenen Puls x 1,03, aufgerundet. Der Anschlag bleibt erreichbar, nahe an der Pulsgrenze. Der Lauf sucht diese Seite erneut. Ist sie dort frei, endet der Lauf als NICHT BESTANDEN, `kein Anschlag nach dem Vorhalt`.
2. Der Endwert: der Vorhalt x der Anteil am letzten freien Puls dieser Suche, abgerundet.

Beispiel: NEUTRAL 1500,2 us, PULSE LOWER 900,0 us, v0 150, die erste Suche frei bei 1000 us und angelaufen bei 990 us. Der Vorhalt ist 150 x 0,850 x 1,03 = 132. Bei 132 ist die Suche frei bis 930 us; der Endwert ist 132 x 0,950 = 125.

Nahe der Pulsgrenze, wo der Vorhalt den Anschlag um weniger als 20 us verschöbe, schreibt der Lauf den Endwert sofort: v0 x der Anteil am letzten freien Puls der ersten Suche. Das erste Schreiben der ersten anlaufenden Seite findet auch, welches Register diese Seite begrenzt. Zuletzt sucht der Lauf beide Enden erneut: BESTANDEN, wenn beide frei sind, NICHT BESTANDEN, wenn eines anläuft. Ein Lauf schreibt höchstens 8 Werte: die zwei Pulse, zwei auf jeder Seite und zwei, um das Register zu finden. Ein Wert unter 50 ist eine weiche Grenze: Das Halten von 2 s beim Start ist die Zustimmung dazu.

### Was einen Endlagenlauf beendet

Jeder dieser Gründe beendet den Lauf als ABGEBROCHEN, mit abgeschaltetem Ausgang und freigegebenem Servo:

- LAUF BEENDEN, STOP, ein Schalten auf SAFE, ein verlorener Link, das Verlassen des Bildschirms, eine Änderung an den Servo-Optionen;
- der INA3221 nicht mehr das Messgerät, oder 200 ms kein INA3221-Fenster (`0.2 s kein INA3221-Fenster`);
- während einer Suche ein Fenster ohne Schienenspannung (`kein Spannungswert im Fenster`) oder mit einem Mittel unter 3,0 V, das erste Fenster eingeschlossen (`Netzteil oder Spannung geändert`);
- ein Messwert ab 3,0 A, ein Messwert am Ende des Bereichs des INA3221 oder Fenstermittel über 1,0 A für 400 ms;
- eine Suche länger als 40 s, ein Prüfstand, der nicht in 3 s schaltet, oder ein Schritt des Programmierers länger als 150 s;
- eine Änderung an Sollwerten oder Ausgang des Netzteils außerhalb der Sitzungen des Programmierers, darunter ein PD mini, der erst nach dem Start antwortet, mit ein- oder ausgeschaltetem Ausgang; während einer Suche die Schiene mehr als 1,0 V vom ersten Fenster des Laufs entfernt;
- bei einer späteren Sitzung ein anderes Servo, oder REVERSION, die Pulse oder der Wertebereich geändert (`Servo hat andere Werte`).

Ein Lauf, der endet, während er auf ARMED wartet, schaltet den Prüfstand zudem auf SAFE.

![Ein nach einem Schreiben beendeter Lauf](view:servo-limrun-stopped)

Nach einem Schreiben behält das Servo die zuletzt geschriebenen Werte. Der Kasten zeigt dann `ZURÜCKSPIELEN IN PROGRAMMIEREN`. Im Programmierer bietet ZURÜCKSPIELEN nach VERBINDEN und ALLES AUSLESEN `Werte vor dem Endlagenlauf.` an. Eine Sitzung nach dem Lauf sichert bei ihrem ersten Schreiben eine eigene Backup-Datei.

![ZURÜCKSPIELEN nach einem Endlagenlauf](view:servo-limrun-restore)

### Das Ergebnis

Der Kasten bleibt bis SCHLIESSEN. Er zeigt das Urteil, den Grund oder die Werte und den Dateinamen des Berichts.

| Urteil | Bedeutung |
|---|---|
| GEFUNDEN | nur Suche: ein Ende läuft an; die Endpunkte werden gemeldet |
| FREI | kein Ende läuft an; nichts wird geschrieben |
| BESTANDEN | programmiert, und beide Enden in der letzten Suche frei: `LINKS 150>87  RECHTS 150>95` |
| NICHT BESTANDEN | ein Ende läuft weiterhin an, oder das Servo lässt sich nicht programmieren |
| ABGEBROCHEN | eine Schutzfunktion, der Bediener, der Link, das Messgerät, das Netzteil oder der Programmierer hat ihn beendet |

![Ein programmiertes Servo](view:servo-limrun-pass)
![Nur Suche](view:servo-limrun-found)

Der Bericht ist eine eigene `SERVOnnn.TXT`, geschrieben unabhängig von BERICHT. Er enthält, wo die Suche lief, die Endpunkte beider Suchen, die Pulse des Servos und das Schreiben der Pulse, LEFT ANGLE und RIGHT ANGLE vorher und nachher mit den Anteilen, dem Vorhalt und dem Endwert, die geschriebenen Werte, die gesendeten Schreibvorgänge und die Sitzungen, das Messgerät, mit eingebautem AS5600 den Winkel an jeder letzten freien Stellung, und das Urteil oder den Grund. Bis die Karte ihn geschlossen hat, warten WLAN und ein Update.

## Aktuelle Einschränkungen

- Am Netzteilmodell des Panels sind die Ströme des Tests die Last des Modells, nicht die des Servos.
- Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt. Der Endlagenlauf nimmt den Weg als linear im Puls; das ist nicht gemessen.
- Bis der Bildschirm eine Position sendet, laufen die Pins mit der Rate, die der Koprozessor hält, nach einem Neustart 50 Hz.

## Verwandte Seiten

- [Servo-Optionen](servo-settings.md): Typ, Frame Rate, Pulse, der Test, die Grenzen und das Ziel.
- [Servo-Programmierer](servo-programmer.md): PROGRAMMIEREN, für KST- und Chaservo-Servos.
- [Netzteil](supply.md) und [Anschlüsse](setup-interfaces.md): der PD mini, der INA3221 und der AS5600.
