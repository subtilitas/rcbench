---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO steuert ein Servo, misst seinen Strom auf der Servoschiene, führt einen automatischen Test über mehrere Versorgungsspannungen aus und sucht die Weggrenzen des eingebauten Servos. Der Bildschirm öffnet über die Kachel SERVO im [Menü](overview.md), auf LIVE.

## Die Werkbank

Die Leiste unter dem Band trägt die Aufgaben von SERVO: LIVE, TEST, PROGRAMMIEREN und OPTIONEN. Ein Wechsel des Reiters ist keine Ebene: das Home-Tag führt von jeder Aufgabe nach Hause und steigt nur aus den Optionen eines Laufs eine Ebene zurück zu TEST und aus einem Programmierer zurück zur Liste von PROGRAMMIEREN. Jede Aufgabe behält ihre Seite und Auswahl bis zum Neustart des Panels. Das Verlassen von SERVO schaltet den Prüfstand auf SAFE.

| Aufgabe | Linke Karte |
|---|---|
| LIVE | der Winkel des Horns, das Zifferblatt, an seinem Fuß die Messwerte SOLL-PULSBREITE, GEMESSEN und STROM, und der Kasten eines Laufs links vom Zifferblatt |
| TEST | die Karten AUTOMATISCHER TEST und ENDLAGENLAUF, jede mit ihren OPTIONEN, einer Zusammenfassung, ihrem Start-Halten, dem letzten Ergebnis und seiner Datei; die Grenzen von SUPPLY, nur lesbar |
| PROGRAMMIEREN | die Liste der Programmierer; eine Zeile öffnet ihren Programmierer über der linken Spalte ([Servo-Programmierer](servo-programmer.md)) |
| OPTIONEN | die Seiten AUSGANG, SWEEP und PRÜFLING ([Servo-Optionen](servo-settings.md)) |

Die rechte Karte ist die Ausgangskarte, auf jeder Aufgabe dieselbe: die Typzeile, die Messzeile mit der Grafik, die Netzteilzeile, TEMPO, die Zeile ZIEL, ZENTRIEREN, SWEEP, FREIGEBEN und ARM. Jede Zeile, die man dort antippt, ist 48px (7,2mm) hoch oder höher, und ARM ist 60px hoch. Der Drehknopf bewegt das Horn nur auf LIVE.

## In Ruhe

![SERVO bei SAFE](view:servo)

Solange der Prüfstand SAFE ist, ist das Horn gedimmt gezeichnet, und die linke Karte von LIVE zeigt `ZUERST ARM`. Das Zifferblatt nimmt keine Position an. Der IOmcu schreibt auf jeden Servo-PWM-Pin (Pulsweitenmodulation) einen Puls der Länge null.

Die Messwerte von LIVE, am Fuß der linken Karte:

| Messwert | Zeigt |
|---|---|
| SOLL-PULSBREITE | die Pulsbreite, die der Bildschirm befiehlt, in us |
| GEMESSEN | den Winkel des Horns vom Winkelsensor AS5600, solange er eingeschaltet ist und seinen Magneten sieht, -180,0 bis +179,9deg; sonst `---` |
| STROM | den Strom der Servoschiene von ihrem Messgerät, in Hundertstel Ampere |

Die rechte Karte, von oben:

| Zeile | Zeigt |
|---|---|
| Typzeile | Servotyp, Frame Rate und Pulsbereich, die gelten: `STANDARD PWM  50Hz  1000-2000` |
| Messzeile und Grafik | Spannung, Strom und Leistung der Schiene, aufgetragen über die letzten 17,8s |
| SOLL | Spannung und Strombegrenzung des Netzteils und sein Ausgangsschalter |
| TEMPO | die Rate, mit der der Prüfstand den Ausgang bewegen darf; 100% gibt den Befehl unverändert weiter |
| ZIEL | was der Tester treibt, `Protokoll Pins`: `SERVO PWM GPIO 2`, `PROG-PORT 1 GPIO 13` oder eine Warnung; ein Tipp öffnet Ausgänge, wo es gewählt wird ([die Zeile ZIEL](servo-settings.md#die-zeile-ziel)) |

## ARMED

![SERVO bei ARMED SERVO](view:servo-armed)

1. ARM 2s halten. Das Band zeigt dann ARMED SERVO, und die Taste lautet DISARM.
2. Auf LIVE irgendwo auf dem Bogen ziehen, um eine Position zu befehlen. Der volle Arm ist die gemessene Position, der blasse Arm die befohlene.
3. Das Abheben des Fingers gibt nicht frei: Der Bildschirm wiederholt die letzte Position, das Servo bleibt, wo es hingestellt wurde.
4. DISARM tippen, STOP drücken oder den Bildschirm verlassen, um auf SAFE zu schalten.

ARMED SERVO treibt die Servo-Ausgänge und einen pulsenden PROG PORT. Throttle-Ausgänge bekommen keinen Puls.

| Bedienelement | SAFE | ARMED |
|---|---|---|
| Das Zifferblatt | abgelehnt | befiehlt die Position unter dem Finger |
| Drehknopf | abgelehnt; die Drehung wird nicht gemerkt | bewegt das Horn vom befohlenen Wert aus |
| ZENTRIEREN | abgelehnt; gedimmt gezeichnet | befiehlt PULS MITTE plus TRIMMUNG |
| SWEEP, PAUSE, PAUSIERT | abgelehnt; gedimmt gezeichnet | startet, hält und setzt den Sweep fort |
| FREIGEBEN | sendet die Freigabe | stellt die Ruder in ihre Ruhe: 1500us bei STANDARD PWM, 760us bei NARROW 760 |
| TEMPO | einstellbar | einstellbar; eine gehaltene Position wird mit der neuen Rate erneut gesendet |
| TEST STARTEN | abgelehnt: `ZUERST ARM` | startet nach seinem Halten den automatischen Test |

Das Schalten auf ARMED setzt den befohlenen Wert auf die Ruhe, die Mitte zwischen den Endpunkten des Kanals, ohne TRIMMUNG. Das erste Ziehen oder Drehen am Knopf beginnt dort. SWEEP ist grau ohne gebundenes Ruder, ohne Link oder an einem IOmcu älter als 0.10.0.

## Das Messgerät der Servoschiene

Die Zeile über der Grafik nennt das Messgerät der Servoschiene:

| Name | Messgerät |
|---|---|
| `INA 0x41 CH2` | der INA3221-Kanal, der in SETUP > SENSORS das Ziel des Servotesters überwacht, hier CH2 von 0x41: Mittelwert von Strom und Spannung seines letzten 50-ms-Fensters, 20 Punkte pro Sekunde |
| `PD MINI` | der letzte Messwert des Netzteils PD mini oder das Netzteilmodell des Panels |

![Der INA3221 als Messgerät der Schiene](view:servo-ina)
![Ein übersteuertes Fenster, in der Warnfarbe](view:servo-clipped)

Der INA3221 wird 1000ms, nachdem seine Bedingungen erfüllt sind, zum Messgerät: Ein Kanal eines bestückten Bausteins überwacht in SETUP > SENSORS das Ziel des Testers (an einem IOmcu älter als 0.22.0 CH1 seines Bausteins), der IOmcu ist 0.16.0 oder neuer, und Fenster mit Strommesswerten kommen an. Ein Fenster, in dem ein Messwert das Ende des Bereichs erreicht hat, 1,638A an einem Shunt von 0,1Ω, steht in der Warnfarbe: Sein Mittelwert ist dann eine Grenze.

![Das Netzteilmodell des Panels als Messgerät, mit der Marke SIMULIERT](view:servo-model)

Solange der PD mini unter SETUP, INTERFACES abgeschaltet ist, ist das Netzteil das Modell des Panels, und eine Marke SIMULIERT steht auf der Grafik. Seine Messwerte sind simuliert, nicht gemessen.

Ein Messwert, der nicht angekommen ist, steht als `---` in STROM auf LIVE, als `--` auf der Messzeile und als Lücke in der Grafik.

## Das Netzteil auf SERVO

Die Zeile SOLL trägt die beiden Sollwerte der Seite SUPPLY und ihren Ausgangsschalter. Eine Änderung auf einem der beiden Bildschirme gilt auf beiden.

- Ein Tipp auf einen Sollwert öffnet das Tastenfeld über der linken Karte.
- AUSGANG EIN ist ein Halten von 2s, AUSGANG AUS ein Tipp.
- Das Verlassen von SERVO schaltet einen Ausgang ab, den SERVO eingeschaltet hat.
- Nach einem VERLASSEN eines Programmierers, das den Ausgang eingeschaltet lässt, fragt SERVO, ob die Sollwerte von vor der Sitzung darauf zurückgehen ([Servo-Programmierer](servo-programmer.md)).

![NUR HV-SERVOS](view:servo-hv)

Standardservos sind für 4,8 bis 6,0V ausgelegt. Eine Spannung, die von 6,0V oder darunter auf über 6,0V steigt, öffnet NUR HV-SERVOS. Die Spannung gilt, nachdem 2s HALTEN ZUM ÜBERNEHMEN gehalten wurde; ABBRECHEN verwirft sie. AUSGANG EIN mit einem Sollwert über 6,0V öffnet dieselbe Warnung.

## SWEEP und PAUSE

SWEEP bewegt das Servo entlang der KURVE von OPTIONEN > SWEEP (Rechteck, Sinus oder Dreieck) mit deren TEMPO, 0,05 bis 5Hz, über den BEREICH, 10 bis 100% des Wegs. Der IOmcu taktet die Kurve; das Timing des Links formt sie nicht. Solange ein Sweep läuft, lautet die Taste PAUSE. Ein Tipp hält das Servo, wo es steht, und die Taste lautet PAUSIERT; ein Tipp auf PAUSIERT setzt den Sweep von dieser Stelle fort.

![Ein angehaltener Sweep](view:servo-paused)

![TEMPO BEGRENZT DEN SWEEP](view:servo-sweep)

`TEMPO BEGRENZT DEN SWEEP` ersetzt in der Warnfarbe die Beschriftung von TEMPO, solange TEMPO langsamer ist als die schnellste Änderung, die die Kurve verlangt. Die Kurve läuft dann als Rampen. TEMPO erhöhen oder TEMPO bzw. BEREICH von OPTIONEN > SWEEP senken. Mit den Vorgaben der Seite SWEEP (Sinus, 0,5Hz, BEREICH 80%) und WEG 90deg je Richtung verschwindet die Warnung bei TEMPO 63%.

Ein Finger auf dem Zifferblatt, ZENTRIEREN, FREIGEBEN, STOP, ein Schalten auf SAFE und das Verlassen des Bildschirms beenden einen Sweep.

## Der automatische Test

TEST STARTEN auf der Karte AUTOMATISCHER TEST von TEST führt das Servo durch die in den Optionen der Karte gewählten Versorgungsspannungen. Auf jeder Stufe misst er den Strom in Ruhe, in Bewegung und beim Halten, misst die Stellzeit und sucht die Spannung, bei der das Servo stehen bleibt. TEST STARTEN ist ein Halten von 2s. Es braucht den Prüfstand bei ARMED und ein Netzteil, das antwortet; sonst nennt die Zustandszeile der Karte den Grund: `ZUERST ARM`, `KEINE STUFE GEWÄHLT`, `NETZTEIL ANTWORTET NICHT`, `BEREICH ZU KLEIN`, `STUFE ÜBER DEN OBERGRENZEN` oder `LETZTER BERICHT SCHREIBT NOCH`.

![TEST mit seinen zwei Laufkarten](view:servo-test)

Die Zusammenfassung der Karte nennt die Stufen und die Länge jeder Stufe, `4.8 6.0V BROWN-OUT, JE 60s`. Unter TEST STARTEN folgen die Zustandszeile, dann das letzte Ergebnis und seine Dateien, `ZULETZT: BESTANDEN ATEST004.CSV + .TXT`.

![Ein Testlauf](view:servo-run)

TEST bleibt beim Start des Laufs zu sehen, und die Zustandszeile der Karte nennt die Stufe. LIVE zeigt Stufe und Phase in einem Kasten links vom Zifferblatt, unter `ZUERST ARM`, das Messgerät neben TEST BEENDEN: `PD mini`, der Kanal, `INA 0x41 CH2`, oder `SIMULATION`. Der Lauf endet vorzeitig, mit abgeschaltetem Ausgang und freigegebenem Servo, bei TEST BEENDEN, STOP, einem Schalten auf SAFE, einem verlorenen Link, dem Verlassen des Bildschirms, einem Finger auf dem Zifferblatt, ZENTRIEREN, SWEEP, FREIGEBEN, einem Tipp auf einen Sollwert oder einer Änderung an den Servo-Optionen. Ein Lauf am INA3221 endet auch, wenn der INA3221 aufhört, das Messgerät zu sein, oder 200ms lang (4 Fenster zu 50ms) kein Fenster mit Strom und Spannung kommt: `0.2s kein INA3221-Fenster`.

![Ein Ergebnis](view:servo-result)
![Ein Ergebnis, vom INA3221 gemessen](view:servo-result-ina)

Das Ergebnis bleibt bis SCHLIESSEN im Kasten auf LIVE und bis zum nächsten Lauf in der Zeile ZULETZT der Karte: BESTANDEN, NICHT BESTANDEN oder ABGEBROCHEN und warum, die längste Stellzeit, der höchste Haltestrom und die Dateien, die die Karte aufgenommen hat, `ATESTnnn.CSV` und `+ .TXT` für den Bericht, in `USER/LOGS`. Mit User-Logs AUS unter [SETUP](setup.md#sd-card) > SD CARD steht dort `USER-LOGS AUS`, und keine Datei wird geschrieben.

## Der Endlagenlauf

Der Endlagenlauf sucht, wo das eingebaute Servo an jedem Ende seines Wegs anläuft. Danach kann er LEFT ANGLE und RIGHT ANGLE des Servos senken, sodass das Servo vor der Stelle anhält, an der es anläuft. Er ist die Karte ENDLAGENLAUF auf TEST; OPTIONEN auf der Karte öffnet [seine Optionen](servo-settings.md#die-optionen-des-endlagenlaufs).

![Der Endlagenlauf, bereit](view:servo-limrun-ready)

1. OPTIONEN auf der Karte antippen, dann ENDLAGENLAUF und NUR SUCHE oder SUCHEN + PROGRAMMIEREN wählen.
2. Mit ZIEL auf PROG-PORT SERVOPULSE antippen und BEHALTEN oder 1000-2000 wählen. Es beginnt bei BEHALTEN.
3. Das Home-Tag antippen, zurück zu den Karten.
4. ENDLAGENLAUF STARTEN 2s halten, während der Prüfstand ARMED ist. Ein Tipp oder ein kürzeres Halten startet nichts und zeigt `Halte 2s, um den Endlagenlauf zu starten.` im Alert-Band.
5. TEST bleibt zu sehen. Der Kasten auf der linken Karte von LIVE zeigt die Phase, die Seite und den Puls sowie den Ausgang, den er treibt.

Die Zeile unter der Taste nennt die ganze Zeit Ausgang und Messgerät: `AUSGANG PROG-PORT 1 GPIO 13, STROM INA3221 0x41 CH2` oder `AUSGANG SERVO PWM GPIO 2, STROM INA3221 0x40 CH1`. Die Zeile ZIEL bleibt ebenfalls sichtbar.

Der Lauf liest nur den INA3221-Kanal, der das Ziel des Testers überwacht. Mit dem PD mini oder dem Modell als Messgerät der Servoschiene bleibt ENDLAGENLAUF STARTEN grau, und die Zeile nennt den Grund:

| Zeile | Bedingung |
|---|---|
| `ZUERST ARM` | der Prüfstand ist SAFE |
| `INA3221 NICHT ZUR SERVOSTROMMESSUNG EINGERICHTET` | kein Kanal eines bestückten INA3221 überwacht in SETUP > SENSORS das Ziel des Testers |
| `INA3221 NICHT ERREICHBAR` | kein Link, oder der INA3221 antwortet nicht |
| `NOCH KEINE INA3221-MESSFENSTER` | der INA3221 ist noch nicht das Messgerät |
| `SERVOVERSORGUNG UNTER 3.0V ODER NICHT AUSGELESEN` | das letzte INA3221-Fenster hat keine Schienenspannung oder weniger als 3,0V: ein ausgeschalteter Ausgang liest 0V |
| `PORT-PWM AUS. KEINE PULSE AN DIESEM PORT.` | ZIEL ist PROG-PORT und PORT-PWM ist aus |
| `KEIN SERVO-PWM-PIN`, `PINS NICHT AUSGELESEN` | ZIEL sind die Ausgänge des Prüfstands, und kein Ausgang ist als SERVO PWM gebunden, oder die Bindung ist noch nicht gelesen |
| `MITTE MUSS ZWISCHEN PULS MIN UND PULS MAX LIEGEN` | der Pulsbereich enthält PULS MITTE nicht |
| `LETZTER BERICHT SCHREIBT NOCH` | ein Test oder ein Lauf ist im Gang, oder sein Bericht geht noch auf die Karte |

### Die Suche

Die Suche geht von der Mitte nach außen, zuerst die untere Seite, 10us je Schritt. Jede Stellung dauert 250ms: 120ms Einschwingen, dann das Mittel der nächsten Fenster. Ein Ende läuft an, wenn der Strom über dem 1,8-Fachen des freien Stroms liegt und ihn um mindestens 0,15A übersteigt. Der Endpunkt wird 25us vor der Stellung gemeldet, an der das Servo anläuft. PULS MIN und PULS MAX des Prüfstands bleiben unverändert.

| ZIEL | Suche |
|---|---|
| ein SERVO-PWM-Pin | von PULS MITTE bis PULS MIN und PULS MAX; nichts wird gelesen oder geschrieben |
| PROG-PORT | zuerst eine Lesesitzung, dann von NEUTRAL des Servos bis zu seinem eigenen PULSE LOWER und PULSE UPPER, nie darüber hinaus |

Die Lesesitzung hält das Servo auf PULS MITTE, schaltet den Prüfstand auf SAFE, liest das Servo mit VERBINDEN, gibt den Port mit VERLASSEN zurück und schaltet den Prüfstand auf ARMED. Vorher wird nichts angesteuert. Der Lauf endet vor jeder Suche als NICHT BESTANDEN, wenn:

- kein KST- oder Chaservo-Servo gelesen wird (`kein KST- oder Chaservo-Servo`): ein Servo, das kein KST oder Chaservo ist, an einem SERVO-PWM-Pin suchen;
- NEUTRAL nicht zwischen PULSE LOWER und PULSE UPPER liegt (`NEUTRAL außerhalb LOWER/UPPER`);
- PULSE LOWER bis PULSE UPPER nicht innerhalb von PULS MIN bis PULS MAX liegt (`Servopulse außerhalb MIN/MAX`). Der Prüfstand bleibt SAFE, und der Kasten nennt beide Bereiche.

Ein erstes VERBINDEN, das aus einem anderen Grund scheitert, oder ein Lesen, das nicht abschließt, beendet den Lauf als ABGEBROCHEN mit `PROG-PORT-Sitzung gescheitert`.

![Die Pulse des Servos außerhalb von PULS MIN und PULS MAX](view:servo-limrun-range)

Ein KST-Servo im Lieferzustand hält PULSE LOWER 900,0us und PULSE UPPER 2100,0us. Der Bereich des PWM-Typs STANDARD ist 1000-2000us, also wird dieses Servo bei BEHALTEN abgelehnt. SERVOPULSE auf 1000-2000 wählen, oder einen PWM-Typ, dessen Bereich 900 bis 2100us enthält.

SERVOPULSE auf 1000-2000 schreibt in der Lesesitzung PULSE LOWER 1000,0us und PULSE UPPER 2000,0us, bevor sich etwas bewegt, und liest sie zurück. NEUTRAL wird nicht geschrieben. Ein Servo, das diese Werte schon hält, bekommt kein Schreiben. Mit NUR SUCHE sind diese beiden Werte das einzige Schreiben.

### LEFT ANGLE und RIGHT ANGLE programmieren

SUCHEN + PROGRAMMIEREN braucht ein KST- oder Chaservo-Servo am Programmierport: ZIEL auf PROG-PORT, den Port in PWM, einen PD mini mit eingeschaltetem Ausgang und einen Sollwert von 4,8V oder mehr. Das Servo übernimmt einen geschriebenen Wert erst, nachdem seine Versorgung aus- und wieder eingeschaltet wurde, was der Lauf über den PD mini tut. Ohne ihn werden SUCHEN + PROGRAMMIEREN und SERVOPULSE auf 1000-2000 nicht angeboten, die Karte zeigt `PROGRAMMIEREN: KEIN PD MINI ZUM AUS- UND EINSCHALTEN`, und der Lauf sucht nur, mit der Servoschiene im letzten Fenster des INA3221 bei 4,8V oder mehr. An den Ausgängen des Prüfstands zeigt ENDLAGENLAUF NUR SUCHE, und der Hinweis in den Optionen nennt den Grund. Am PROG-PORT beginnt jeder Lauf mit einer Lesesitzung, auch NUR SUCHE, und sie braucht ein KST- oder Chaservo-Servo: bei einem anderen Servo endet der Lauf mit `kein KST- oder Chaservo-Servo`. Ein Port nicht in PWM, ein ausgeschalteter Ausgang des PD mini oder eine Versorgung unter 4,8V hält die Schaltfläche auch für NUR SUCHE grau, und die Zustandszeile der Karte nennt die Bedingung.

![Kein PD mini: der Lauf schreibt nichts](view:servo-limrun-nopd)

![Der Endlagenlauf beim Programmieren](view:servo-limrun)

Der Lauf schreibt in Sitzungen des [Servo-Programmierers](servo-programmer.md):

1. Das Servo geht für 1000ms zurück in die Mitte.
2. Der Lauf schaltet den Prüfstand auf SAFE.
3. VERBINDEN liest das Servo.
4. Ein Schreiben, mit jeder Grenzprüfung des Programmierers.
5. VERLASSEN gibt den Port an PWM zurück.
6. Der Lauf schaltet den Prüfstand auf ARMED und sucht diese Seite erneut.

Das erste Schreiben sichert die Werte des Servos in eine Backup-Datei auf der Karte. Der Lauf nimmt das Servo als linear: PULSE LOWER und PULSE UPPER sind die Pulse, bei denen es den vollen Weg erreicht, den LEFT ANGLE und RIGHT ANGLE setzen. Der Anteil einer Seite beim Puls p ist `(p - NEUTRAL) / (PULSE LOWER oder UPPER - NEUTRAL)`. Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt, und der lineare Zusammenhang ist eine Annahme.

Jede anlaufende Seite bekommt zwei Schreibvorgänge, v0 der Wert, den ihr Register hielt:

1. Der Vorhalt: v0 x der Anteil am ersten blockierten Puls x 1,03, aufgerundet. Der Anschlag bleibt erreichbar, nahe an der Pulsgrenze. Der Lauf sucht diese Seite erneut. Ist sie dort frei, endet der Lauf als NICHT BESTANDEN, `kein Anschlag nach dem Vorhalt`.
2. Der Endwert: der Vorhalt x der Anteil am letzten freien Puls dieser Suche, abgerundet.

Beispiel: NEUTRAL 1500,2us, PULSE LOWER 900,0us, v0 150, die erste Suche frei bei 1000us und blockiert bei 990us. Der Vorhalt ist 150 x 0,850 x 1,03 = 132. Bei 132 ist die Suche frei bis 930us; der Endwert ist 132 x 0,950 = 125.

Nahe der Pulsgrenze, wo der Vorhalt den Anschlag um weniger als 20us verschöbe, schreibt der Lauf den Endwert sofort: v0 x der Anteil am letzten freien Puls der ersten Suche. Das erste Schreiben der ersten anlaufenden Seite findet auch, welches Register diese Seite begrenzt. Zuletzt sucht der Lauf beide Enden erneut: BESTANDEN, wenn beide frei sind, NICHT BESTANDEN, wenn eines anläuft. Ein Lauf schreibt höchstens 8 Werte: die zwei Pulse, zwei auf jeder Seite und zwei, um das Register zu finden. Ein Wert unter 50 ist eine weiche Grenze: Das Halten von 2s beim Start ist die Zustimmung dazu.

### Was einen Endlagenlauf beendet

Jeder dieser Gründe beendet den Lauf als ABGEBROCHEN, mit abgeschaltetem Ausgang und freigegebenem Servo:

- LAUF BEENDEN, STOP, ein Schalten auf SAFE, ein verlorener Link, das Verlassen des Bildschirms, eine Änderung an den Servo-Optionen;
- der INA3221, der aufhört, das Messgerät zu sein, oder 200ms kein INA3221-Fenster (`0.2s kein INA3221-Fenster`);
- während einer Suche ein Fenster ohne Schienenspannung (`kein Spannungswert im Fenster`) oder mit einem Mittel unter 3,0V, das erste Fenster eingeschlossen (`Netzteil oder Spannung geändert`);
- ein Messwert ab 3,0A, ein Messwert am Ende des Bereichs des INA3221 oder Fenstermittel über 1,0A für 400ms;
- eine Suche länger als 40s, ein Prüfstand, der nicht in 3s schaltet, oder ein Schritt des Programmierers länger als 150s;
- eine Änderung an Sollwerten oder Ausgang des Netzteils außerhalb der Sitzungen des Programmierers, darunter ein PD mini, der erst nach dem Start antwortet, mit ein- oder ausgeschaltetem Ausgang; während einer Suche die Schiene mehr als 1,0V vom ersten Fenster des Laufs entfernt;
- bei einer späteren Sitzung ein anderes Servo, oder REVERSION, die Pulse oder der Wertebereich geändert (`Servo hat andere Werte`).

Ein Lauf, der endet, während er auf ARMED wartet, schaltet den Prüfstand zudem auf SAFE.

![Ein nach einem Schreiben beendeter Lauf](view:servo-limrun-stopped)

Nach einem Schreiben behält das Servo die zuletzt geschriebenen Werte. Der Kasten zeigt dann `ZURÜCKSPIELEN IN PROGRAMMIEREN`. Im Programmierer bietet ZURÜCKSPIELEN nach VERBINDEN und ALLES AUSLESEN `Werte vor dem Endlagenlauf.` an. Eine Sitzung nach dem Lauf sichert bei ihrem ersten Schreiben eine eigene Backup-Datei.

![ZURÜCKSPIELEN nach einem Endlagenlauf](view:servo-limrun-restore)

### Das Ergebnis

Der Kasten auf LIVE bleibt bis SCHLIESSEN, die Zeile ZULETZT der Karte bis zum nächsten Lauf. Der Kasten zeigt das Urteil, den Grund oder die Werte und den Dateinamen des Berichts.

| Urteil | Bedeutung |
|---|---|
| GEFUNDEN | nur Suche: ein Ende läuft an; die Endpunkte werden gemeldet |
| FREI | kein Ende läuft an; nichts wird geschrieben |
| BESTANDEN | programmiert, und beide Enden in der letzten Suche frei: `LINKS 150>87  RECHTS 150>95` |
| NICHT BESTANDEN | ein Ende läuft weiterhin an, oder das Servo lässt sich nicht programmieren |
| ABGEBROCHEN | eine Schutzfunktion, der Bediener, der Link, das Messgerät, das Netzteil oder der Programmierer hat ihn beendet |

![Ein programmiertes Servo](view:servo-limrun-pass)
![Nur Suche](view:servo-limrun-found)

Der Bericht ist eine eigene `LIMITnnn.TXT` in `USER/LOGS`, geschrieben unabhängig von BERICHT und nicht geschrieben mit User-Logs AUS. Er enthält, wo die Suche lief, die Endpunkte beider Suchen, die Pulse des Servos und das Schreiben der Pulse, LEFT ANGLE und RIGHT ANGLE vorher und nachher mit den Anteilen, dem Vorhalt und dem Endwert, die geschriebenen Werte, die gesendeten Schreibvorgänge und die Sitzungen, das Messgerät, mit eingebautem AS5600 den Winkel an jeder letzten freien Stellung, und das Urteil oder den Grund. Bis die Karte ihn geschlossen hat, warten WLAN und ein Update.

## Aktuelle Einschränkungen

- Am Netzteilmodell des Panels sind die Ströme des Tests die Last des Modells, nicht die des Servos.
- Die Einheit von LEFT ANGLE und RIGHT ANGLE ist unbekannt. Der Endlagenlauf nimmt den Weg als linear im Puls; das ist nicht gemessen.
- Bis der Bildschirm eine Position sendet, laufen die Pins mit der Rate, die der IOmcu hält, nach einem Neustart 50Hz.

## Verwandte Seiten

- [Servo-Optionen](servo-settings.md): Typ, Frame Rate, Pulse, der Sweep, die Optionen jedes Laufs und das Ziel.
- [Servo-Programmierer](servo-programmer.md): die Zeile auf PROGRAMMIEREN für KST- und Chaservo-Servos.
- [Netzteil](supply.md) und [Anschlüsse](setup-interfaces.md): der PD mini, der INA3221 und der AS5600.
