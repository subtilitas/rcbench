---
screen: none
title: Servo-Programmierer
order: 22
---

# Servo-Programmierer

Die Servoprogrammierung liest die 32 Register eines KST- oder Chaservo-Servos über dessen Signalleitung und zeigt sie als Einstellungen. Beide Marken verwenden dasselbe Protokoll und denselben Registersatz. Änderungen werden erst auf Anforderung zurückgeschrieben.

Öffne PROGRAMMIEREN auf [SERVO](servo.md) und wähle `KST- und Chaservo-Servos`. Die Sitzung liegt eine Ebene unter der Liste und verdeckt die linke Spalte und die Aufgabenreiter. SCHLIESSEN oder das Home-Feld führt zur Liste zurück. Das Öffnen beendet einen laufenden automatischen Test.

![Die Liste von PROGRAMMIEREN](view:servo-program)

Die Zeile nennt den SERVO-TESTER-Port und seinen Pin, etwa `PROG-PORT 1 GPIO 13`. Meldet der IOmcu, die RP2350-Platine, keine Servo-PWM-Unterstützung, ist sie gedimmt und gesperrt. Das gilt auch ohne IOmcu-Antwort. Dann steht dort `IOMCU OHNE SERVO PWM`.

![Die Zeile gedimmt](view:servo-program-nocap)

Belegt eine über die Prüfstandskonsole geöffnete Hitec-Sitzung denselben Port, ist die Zeile mit `PORT BELEGT` gesperrt.

![Der Port belegt](view:servo-program-busy)

KST und Chaservo sind Namen oder Marken ihrer jeweiligen Inhaber. rcbench steht mit ihnen in keiner Verbindung und wird von ihnen weder empfohlen noch finanziell unterstützt. Die Namen kennzeichnen die unterstützten Servos.

Registerbelegung und Timing wurden an 1 KST Tool Card #2 und 1 Chaservo-Servo ermittelt. Die Auswirkungen der Register auf die Bewegung wurden nicht gemessen. Namen, Einheiten und Grenzen entsprechen den Angaben und zulässigen Werten des Herstellerwerkzeugs.

## Was er braucht

![Der Programmierer, während der Tester den Port hält](view:servo-kst)

- IOmcu 0.17.0 oder neuer, mit Protokoll 4.15 einschließlich LEAVE.
- Ein Servo an einem IOmcu-Programmierport, PROG-PORT 1 bis 4, mit Pin und FUNKTION SERVO PWM. Bei manueller Verdrahtung verwendet Port 1 standardmäßig GPIO 13, Pad 17. Andere Pins werden im jeweiligen PROG-PORT-Eintrag auf Ausgänge gewählt. Carrier-Pins sind fest; siehe [Der Port und die Versorgung](#der-port-und-die-versorgung).
- SERVO-TESTER auf diesem Port: PORT 1, PORT 2, PORT 3 oder PORT 4. Die [Zeile ZIEL](servo-settings.md#die-zeile-ziel) öffnet den zugehörigen Eintrag auf Ausgänge. Ab Carrier-Protokoll 4.36 kannst du den Port auch über die PORT-Zeile der Servoprogrammierung wählen.
- Prüfstand in SAFE. Bei ARMED wird VERBINDEN mit `Tippe zuerst auf DISARM. Bei ARMED kann der Port nicht übernommen werden.` abgelehnt. Solange der Port belegt ist, kann keine Seite auf ARMED schalten.

![Ein IOmcu ohne Programmierport](view:servo-kst-nopage)
![Die Standardausgänge sind das Ziel](view:servo-kst-target)

Fehlt eine Voraussetzung, sind alle Bedienelemente außer SCHLIESSEN gesperrt. Der Reiter nennt den Grund.

## Der Port und die Versorgung

Der Port arbeitet entweder mit PWM (Pulsweitenmodulation) oder im Programmiermodus. VERBINDEN beendet PWM, stellt bei antwortendem PD mini die Versorgung ein, führt die Einstiegsfolge aus und liest alle 32 Register aus. Danach heißt die Taste VERLASSEN, während eines Vorgangs ABBRECHEN.

![Was VERBINDEN stellt](view:servo-kst-options)

Solange der Tester den Port verwendet, stellt WEITERES die bei VERBINDEN angewendeten Werte ein:

| Eintrag | Bereich | Vorgabe |
|---|---|---|
| PROGRAMMIERSPANNUNG | 4,8 bis 6,0V in Schritten von 0,1V; bis 8,4V mit HV-SERVO eingeschaltet | 5,0V |
| STROMBEGRENZUNG | 50 bis 3000mA | 1000mA |
| WARTEZEIT VOR PWM | 0 bis 10000ms | 100ms |

![Ein PD mini antwortet](view:servo-kst-supply)
![Die Versorgung im Programmiermodus](view:servo-kst-supplied)

Im Programmiermodus kontrolliert der Port die Netzteilsollwerte. Die Zeile SOLL und SUPPLY lassen weder Änderungen noch EIN zu; AUSGANG AUS und STOP bleiben verfügbar. Ohne PD mini werden Spannung und Strombegrenzung gespeichert, aber nicht angewendet. Nach Rückkehr zu PWM werden die Sollwerte vor der Sitzung wiederhergestellt, sobald der Ausgang aus ist. Lässt VERLASSEN ihn eingeschaltet, wartet die Wiederherstellung auf das nächste AUS. Ein zwischenzeitlich geänderter Sollwert bleibt erhalten.

![Die Frage nach dem Verlassen](view:servo-hitec-restore-ask)

Lässt VERLASSEN den Ausgang mit Sitzungssollwerten eingeschaltet, die von den vorherigen Werten abweichen, fragt SERVO nach Rückkehr zu PWM: `Netzteil-Sollwerte 5.50V 2.00A am eingeschalteten Ausgang wiederherstellen?` JA übernimmt beide Werte. Steigt die Spannung dabei von höchstens 6,0V auf über 6,0V, erscheint zuerst NUR HV-SERVOS. NEIN behält die Sitzungswerte bis zum nächsten AUS.

Die Frage erscheint einmal je VERLASSEN. Sie wartet auf Tastenfelder, Listen, Warnungen, Rückfragen und das Halten von AUSGANG EIN. Sie entfällt bei belegtem Netzteil, nach AUS, Fehler oder STOP, während eines Endlagenlaufs oder nach zwischenzeitlicher Sollwertänderung.

Nur VERLASSEN löst die Frage aus. PORT FREIGEBEN, STOP, Fehler, Verbindungsverlust, Panel- oder IOmcu-Neustart, eine Portfreigabe ohne VERLASSEN oder der Wechsel zur eigenen Portversorgung beendet die Belegung ohne Rückfrage. Dann bleiben die Sitzungswerte bis zum nächsten AUS bestehen. Die Hitec-Servoprogrammierung verhält sich ebenso.

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) verwenden Programmierports eine eigene feste 5V-Versorgung über GPIO 21. WEITERES zeigt `PROGRAMMIERSPANNUNG: 5V FEST, GPIO 21`. Die Sitzung schaltet diese Schiene dort, wo die folgende Tabelle den PD-mini-Ausgang nennt. Währenddessen zeigt das Feld PROG 5V unter PD MINI `BELEGT VON PROG-PORT n` und nimmt keinen Druck an. Außerhalb einer Sitzung kannst du PROG 5V EIN 2s halten oder PROG 5V AUS antippen.

Ab Carrier-Protokoll 4.36 öffnet ein Tipp auf die PORT-Beschriftung vor VERBINDEN die Auswahl: PROGP01 (GPIO 13), PROGP02 (GPIO 14), PROGP03 (GPIO 15) oder PROGP04 (GPIO 0). Die Wahl setzt SERVO-TESTER und fordert SERVO PWM am gewählten Port sowie KEINE an den anderen drei an. Bis zur Bestätigung sendet der Tester keine Befehle. Bei ARMED oder ausstehender ARM-Anfrage, während Test, Sweep oder Endlagenlauf sowie bei belegtem Programmierport ist die Auswahl gesperrt. Auch ein PWM-Ausgang auf demselben Slice sperrt den betreffenden Port.

![Der KST-Programmer hält die 5V](view:servo-kst-rbmod)

## Der Versorgungszyklus

KST- und Chaservo-Servos öffnen den Programmiermodus nach dem Einschalten. Geschriebene Werte gelten erst nach dem nächsten Einschalten. Das getestete Chaservo ignorierte VERBINDEN bei durchgehend eingeschalteter Versorgung. Nach einem Schreibvorgang reagierte es erst wieder auf PWM, nachdem die Versorgung aus- und eingeschaltet wurde.

| Schritt | PD mini mit eingeschaltetem Ausgang | Ohne PD mini |
|---|---|---|
| VERBINDEN | Ausgang 1s aus, dann ein; Leitung 1s Low, danach Einstieg. | Einstieg 250ms nach dem Tipp. Schalte die Servoversorgung aus und ein und tippe 1s später auf VERBINDEN. |
| VERLASSEN nach einem Schreibvorgang | Ausgang 1s aus, dann ein; Leitung 1s Low, danach PWM. | PWM; die geschriebenen Werte gelten nach Aus- und Einschalten der Servoversorgung. |
| VERLASSEN ohne Schreibvorgang | PWM nach WARTEZEIT VOR PWM; Versorgung unverändert. | ebenso |

Bei ausgeschaltetem Ausgang schaltet VERBINDEN ihn ein und öffnet den Programmiermodus. ABBRECHEN oder STOP während der 1s langen Aus-Phase lässt ihn ausgeschaltet: `Der Versorgungszyklus wurde abgebrochen. Der Ausgang des PD mini bleibt aus.`

![VERBINDEN mit eingeschaltetem Ausgang](view:servo-kst-supply-on)
![Die 1s aus](view:servo-kst-cycling)

![Ein laufender Vorgang](view:servo-kst-working)

## Der Reiter SERVO

![Ein ausgelesenes Servo](view:servo-kst-read)

| Zeile | Taste | Wirkung |
|---|---|---|
| PORT | VERBINDEN, ABBRECHEN, VERLASSEN | nimmt den Port, beendet den laufenden Vorgang, gibt den Port zurück |
| SERVO | ALLES AUSLESEN | liest die 32 Register, jedes, bis 3 von höchstens 5 Lesungen übereinstimmen |
| TYP | V6?, V8?, V6, V8 | der Servotyp, V6 oder V8, wie der Bediener ihn setzt |
| DATEIEN | WÄHLEN | speichert die Werte des Servos auf die Karte, lädt eine Datei |
| WEITERES | WÄHLEN | im Programmiermodus: PRÜFEN, ÄNDERUNGEN VERWERFEN und, bei KST EXPERT MODE EIN, PAIRING LÖSEN |
| SCHREIBEN INS SERVO FREIGEBEN | AUS, EIN | der Schreibschalter |
| ZURÜCKSPIELEN | WÄHLEN | schreibt ein Backup zurück |
| (keine Beschriftung) | n WERTE SCHREIBEN | schreibt die geänderten Werte |

Kein Register identifiziert die Serie. Bei V8-Flächenservos kann 20k_sel wie bei V6 gesetzt sein. Nach dem Auslesen zeigt TYP daher einen Vorschlag mit Fragezeichen, etwa `V6?`, und fordert zum Prüfen des Servoetiketts auf. Tippe auf TYP, um V6 oder V8 zu bestätigen.

Der Typ gilt für die Sitzung und wird in Dateien mitgespeichert. Laden erfordert einen passenden Typ. Ist noch keiner gewählt, fragt die Dateiauswahl zuerst danach. Der Typ wird nie ins Servo geschrieben.

![Die Liste TYP](view:servo-kst-type)
![Ein gesetzter Typ](view:servo-kst-type-set)

![Ein Servo unbekannter Belegung: nur lesen](view:servo-kst-readonly)

Passt die Registerbelegung nicht zum bekannten Layout, bleibt das Servo schreibgeschützt: `Nur Auslesen möglich. Dieser Servotyp ist unbekannt.`

## Bearbeiten

![Geänderte Werte auf ABSTIMMEN](view:servo-kst-tune)

Änderungen bleiben bis zum Schreiben im Entwurf auf dem Panel. Ein `*` und die Akzentfarbe markieren geänderte Werte. Tippe auf eine Zahl für das Tastenfeld innerhalb der harten Grenzen, auf STRETCH für die Auswahlliste oder auf einen Schalter zum Umschalten.

![Das Tastenfeld mit dem Bereich, den die Grenzen annehmen](view:servo-kst-keypad)
![Die Liste von STRETCH](view:servo-kst-stretch)

| Reiter | Zeilen |
|---|---|
| ABSTIMMEN | DEAD BAND, MAX. DUTY (10 bis 98%), BOOST (bis 3000us), STRETCH (x1 bis x32), SOFT START, TEMPOGRENZE, TEMPO; DEAD BAND, BOOST und STRETCH nur bei KST EXPERT MODE EIN |
| PULS | NEUTRAL (1360 bis 1640us), PULSE LOWER (ab 500us), PULSE UPPER (bis 2500us), LEFT ANGLE, RIGHT ANGLE, REVERSION |
| FAILSAFE | BEWEGEN BEI SIGNALVERLUST, POSITION (500 bis 2500us), NACH (0,4 bis 13,0s) |
| GESPERRT | 20K SEL, LOCK, MODE und der Schutzblock; nur angezeigt, nie bearbeitet; der Reiter erscheint nur bei KST EXPERT MODE EIN |

![Der Reiter PULS](view:servo-kst-pulse)
![Der Reiter FAILSAFE](view:servo-kst-failsafe)

LEFT ANGLE und RIGHT ANGLE sind Zahlen des Servos; ihre Einheit ist unbekannt.

![Ein Wert, den eine harte Grenze ablehnt](view:servo-kst-hard)

Ein Tipp, der eine harte Grenze verletzen würde, ändert nichts und nennt die Grenze.

![ÄNDERUNG PRÜFEN](view:servo-kst-soft)

Ein Wert innerhalb der harten Grenzen, aber außerhalb einer weichen Grenze, öffnet ÄNDERUNG PRÜFEN. Die Seite zeigt Servowert, vorgeschlagenen Wert und eine Zeile je betroffener Grenze. Halte 2s HALTEN ZUM ÜBERNEHMEN für 2s zur Bestätigung. ABBRECHEN behält den Entwurf. Beim [Endlagenlauf](servo.md#der-endlagenlauf) gilt bereits das Halten beim Start als Zustimmung zu LEFT ANGLE oder RIGHT ANGLE unter 50; eine weitere Frage entfällt.

![Der Reiter GESPERRT](view:servo-kst-locked)

## KST EXPERT MODE

KST EXPERT MODE auf [Servo-Optionen](servo-settings.md#prüfling) > PRÜFLING legt fest, was der Programmierer zeigt. Er ist standardmäßig AUS und bleibt über einen Neustart erhalten. Halte die Zeile 2s, um ihn einzuschalten; ein kürzerer Druck zeigt `Halte 2s, um KST EXPERT MODE einzuschalten.` auf dem Band. Tippe, um ihn auszuschalten.

Bei AUS verschwinden DEAD BAND, BOOST und STRETCH von ABSTIMMEN, PAIRING LÖSEN aus WEITERES und der Reiter GESPERRT aus der Leiste. ABSTIMMEN rückt zusammen, und die Leiste hält 4 Reiter. DEAD BAND und BOOST können Pendeln, Brummen und Erwärmung verursachen, STRETCH Schwingungen; was PAIRING LÖSEN im Servo bewirkt und was die gesperrten Werte bedeuten, ist nicht bekannt.

![ABSTIMMEN bei KST EXPERT MODE AUS](view:servo-kst-tune-expert-off)
![WEITERES bei KST EXPERT MODE AUS](view:servo-kst-more-expert-off)

Das Laden einer Datei und ZURÜCKSPIELEN schreiben weiter das ganze Abbild. Die Schreibseite, DIESE DATEI PRÜFEN und die Seite des Zurückspielens nennen jeden verborgenen Wert, der sich ändert, zuerst und in der Warnfarbe: `DEAD BAND (KST EXPERT MODE): 2.0 -> 1.6us`. Ein Entwurf behält beim Ausschalten eine Änderung eines verborgenen Werts, und die Schreibseite nennt sie.

![Die Schreibseite nennt die verborgenen Werte](view:servo-kst-write-expert-off)

## Schreiben

![Das Schreiben freigeben](view:servo-kst-enable)

SCHREIBEN INS SERVO FREIGEBEN ist zunächst aus. Tippe auf AUS, um die Risiken zu lesen, und halte 2s HALTEN ZUM FREIGEBEN für 2s. Die Freigabe endet bei jedem Aufruf von SERVO, bei STOP, VERBINDEN oder VERLASSEN, bei Verbindungsverlust und bei unklarem Servozustand.

![Drei geänderte Werte, der Schalter an](view:servo-kst-changes)
![Die Seite zum Schreiben](view:servo-kst-write)

n WERTE SCHREIBEN öffnet die Änderungsübersicht, etwa `DEAD BAND: 2.0 -> 1.6us`. Halte 2s HALTEN ZUM SCHREIBEN für 2s, um zu schreiben.

1. Das erste Schreiben einer Sitzung speichert die Werte des Servos in eine neue Datei auf der SD-Karte (Secure Digital).
2. Das Panel vermerkt das Schreiben in `BENCH/KST/CONF/KST.TXT`, oder ohne Karte im Speicher.
3. Der IOmcu prüft die Werte gegen dieselben Grenzen, plant die Registerschreibvorgänge und führt sie aus.
4. Jedes geschriebene Register wird zurückgelesen. Ein Schreiben, das nicht übernommen wird, wird rückgängig gemacht.

![Ein laufendes Schreiben: nicht abziehen](view:servo-kst-writing)
![Ein fertiges Schreiben](view:servo-kst-written)

Ein geschriebener und zurückgelesener Wert liegt im Servo, wirkt aber erst nach erneutem Einschalten. Mit PD mini erscheint `Die Werte werden nach VERLASSEN wirksam: Die Versorgung wird 1s aus- und wieder eingeschaltet.` Ohne PD mini lautet die Meldung `Die geschriebenen Werte werden erst nach Aus- und Einschalten der Servoversorgung wirksam.`

![Ein Schreiben mit PD mini](view:servo-kst-written-supply)

## Dateien

![Die Dateien auf der Karte](view:servo-kst-files)

DATEIEN zeigt die Servodateien aus `BENCH/KST/CONF`, 5 pro Seite, jeweils mit Servotyp. Der erste Eintrag speichert die Servowerte unter einem neuen Namen ab `KST001`. Namen dürfen bis zu 16 Zeichen enthalten.

![Der Name einer neuen Datei](view:servo-kst-name)
![Eine geladene Datei](view:servo-kst-file-loaded)

Tippe auf eine Datei, um sie in den Entwurf zu laden. Erst das Schreiben überträgt sie ins Servo. Ein falscher Servotyp, eine beschädigte Datei oder Werte außerhalb harter Grenzen verhindern das Laden; die Statuszeile nennt den Grund. Ohne bestätigten Typ erscheint zuerst `SERVOTYP VOR DEM LADEN WÄHLEN`.

![Der Typ vor einem Laden](view:servo-kst-type-first)

## ZURÜCKSPIELEN und WEITERES

![Die Liste von ZURÜCKSPIELEN](view:servo-kst-restore-list)
![Das Zurückspielen des Backups der Sitzung](view:servo-kst-restore)

ZURÜCKSPIELEN bietet das Sitzungsbackup (die zuerst nach VERBINDEN ausgelesenen Werte), den Stand vor einem unterbrochenen Schreibvorgang oder `Werte vor dem Endlagenlauf.` an. Jede Wiederherstellung erfordert Schreibfreigabe und 2s Halten.

![WEITERES im Programmiermodus](view:servo-kst-more)

- PRÜFEN liest alle Register aus und vergleicht sie mit dem gespeicherten Abbild.
- ÄNDERUNGEN VERWERFEN setzt den Entwurf auf die Servowerte zurück.
- PAIRING LÖSEN, bei KST EXPERT MODE EIN, setzt Register 1D nach 2s Halten auf 00. Die Wirkung im Servo ist unbekannt.

## Ein unterbrochenes Schreiben

![Ein unterbrochenes Schreiben](view:servo-kst-doubt)

Bei Verbindungsverlust, Platinenneustart, ABBRECHEN, Stopp oder ausbleibendem Ergebnis bis zur Frist gilt ein Schreibvorgang als unterbrochen. Der Reiter zeigt `Schreiben wurde unterbrochen. Die Einstellungen können unvollständig sein.`, bis das Servo erneut ausgelesen wurde. ALLES AUSLESEN vergleicht den aktuellen Stand mit dem Schreibvermerk und bietet ZURÜCKSPIELEN an.

![Das Servo hält eine Mischung alter und neuer Werte](view:servo-kst-journal)

## Verlassen

![PROGRAMMIERMODUS VERLASSEN](view:servo-kst-leave)
![VERLASSEN nach einem Schreiben mit PD mini](view:servo-kst-leave-cycle)

VERLASSEN, SCHLIESSEN oder das Home-Feld im Programmiermodus öffnet PROGRAMMIERMODUS VERLASSEN. Bei Rückkehr zu Servo-PWM kann sich das Servo bewegen. Nach einem Schreibvorgang mit PD mini kündigt die Seite zuerst einen Versorgungszyklus mit 1s Aus-Zeit an. Ohne PD mini nennt sie den nötigen manuellen Versorgungszyklus. Ohne Schreibvorgang bleibt die Versorgung unverändert.

Halte 2s HALTEN ZUM VERLASSEN für 2s. Der Port hält die Leitung für WARTEZEIT VOR PWM auf Low und gibt den Pin dann an den Tester zurück. Das Verlassen der SERVO-Seite führt diesen Vorgang nicht aus: Die Sitzung behält den Port und öffnet beim nächsten Besuch wieder auf PROGRAMMIEREN. Innerhalb der Servoprogrammierung schließt Home zuerst offene Tastenfelder, Listen, Warnungen oder Tastaturen.

## Hitec-D-Empfang

WEITERES bietet Hitec-D-Empfang an, solange der Tester den Port verwendet. PULL-UP als Vorgabe benötigt einen Widerstand von 2,2kΩ zwischen Signalleitung und Servoversorgung. PRECHARGE kommt ohne ihn aus: Der IOmcu legt das Pad alle 1,09us für 100ns auf High und tastet 113ns später ab.

Die Einstellung gilt ab dem nächsten OPEN einer D-Serie-Leitung und lässt sich während einer Sitzung nicht ändern. Wähle VERLASSEN, ändere die Einstellung und wähle danach VERBINDEN. Classic-Servos der Serien HS-5xxx und HS-7xxx benötigen keines der Verfahren und ignorieren die Einstellung.

IOmcu 0.21.1 und 0.21.2 (Protokoll 4.23) empfangen immer mit PULL-UP. Bei gewähltem PRECHARGE steht `IOmcu älter als 0.22.0: empfängt mit PULL-UP.` in der Liste. Vor Version 0.21.1 fehlt die Hitec-Leitung; dann erscheint bei PRECHARGE `IOmcu älter als 0.21.1: keine Hitec-Leitung.`

![Die Liste von Hitec-D-Empfang](view:servo-hitec-rx)

## Hitec: SERVO BEWEGEN

SERVO BEWEGEN auf dem Reiter WEG der Hitec-Programmierung fährt das Ruderhorn eines Servos der D-Serie auf die bearbeitete Position, in der gehaltenen Sitzung, während der Prüfstand SAFE bleibt. Bis n WERTE SCHREIBEN wird nichts im Servo gespeichert.

![Die Seite SERVO BEWEGEN](view:servo-hitec-move)
![SERVO BEWEGEN an, ENDE LINKS verfolgt](view:servo-hitec-move-on)

- Tippe auf AUS, lies die Seite und halte 2s HALTEN ZUM BEWEGEN 2s lang. Ein kürzerer Druck zeigt `Halte 2s, um das Servo zu bewegen.` und startet nichts.
- Es braucht ein antwortendes PD mini mit eingeschaltetem Ausgang: STOP schaltet die Versorgung des Servos aus. Ohne, auch an den festen 5V des rbmod-Carriers, meldet es `Bewegen braucht ein PD mini mit eingeschaltetem Ausgang.`
- DREHRICHTUNG ist im Servo und im Entwurf CW; die Abbildung bei CCW ist nicht gemessen.
- NEUTRAL, ENDE LINKS und ENDE RECHTS bekommen Tasten `-` und `+` mit 8 Zählwerten, 0.11°, die beim Halten wiederholen. Das Ruderhorn folgt der zuletzt geänderten Zeile, ihr Name steht in der Akzentfarbe; es beginnt mit NEUTRAL. Ein Tippen auf den Wert öffnet das Tastenfeld.
- Das Ruderhorn fährt höchstens 2.0° je Durchlauf und bleibt in der Reichweite der gespeicherten Endlagen, 2606 bis 13743 bei einem D485HW im Werkszustand.
- Ein Ruderhorn, das 2 Durchläufe lang mehr als 2.0° nachhängt, ein Strom von 80% der Stromgrenze für 0.4s oder ein AS5600-Winkel, der nicht folgt, fährt das Ruderhorn 1.0° zurück und beendet die Schleife: `Servo bei 6000 blockiert. Zurück auf 6167 gefahren.`
- Ein Tippen auf EIN, ein Reiterwechsel, SCHLIESSEN oder das Home-Tag beenden es: Ziel 3000, ein Neustart des Servos und ein Lesen, das mit dem Start verglichen wird, etwa 2.6s im Host-Modell. `SERVO BEWEGEN steht auf AUS. Die gespeicherten Werte sind unverändert zurückgelesen.`
- STOP, ein Schalten auf SAFE am Prüfstand, eine verlorene Verbindung, eine ausgefallene Versorgung und ein gescheitertes Lesen beenden es sofort, mit allen Ausgängen aus.

### ANSCHLAG SUCHEN

Solange SERVO BEWEGEN an ist, steht ANSCHLAG SUCHEN in der Zeile von DREHRICHTUNG. Es sucht den mechanischen Anschlag einer Seite und setzt diese Endlage im Entwurf; es schreibt nichts.

![ANSCHLAG SUCHEN links bei der Suche](view:servo-hitec-move-find)
![Ein Anschlag links gefunden](view:servo-hitec-move-found)

- Tippe auf ENDE LINKS oder ENDE RECHTS. Das Ruderhorn fährt auf NEUTRAL, dann alle 2 Durchläufe 0.5° nach außen, bis zum Rand der Reichweite.
- Eine Blockierung setzt diese Endlage 1.0° vor den Anschlag, mit `*`: `Anschlag bei 4840. ENDE LINKS in den Änderungen auf 4915 gesetzt.` Das Ruderhorn folgt dann dieser Endlage.
- Keine Blockierung bis zum Rand, 40s vorbei oder ein zweites Tippen auf die Taste: die Änderungen bleiben, wie sie waren.
- n WERTE SCHREIBEN nach AUS schreibt die gefundene Endlage.

Die Schwellen sind gewählt, nicht mit einem Ruderhorn an einem Anschlag gemessen. [SERVO BEWEGEN in der Hitec-Programmierung](https://github.com/subtilitas/rcbench/wiki/HitecMove-de) nennt jede Meldung und Grenze.

## Verwandte Seiten

- [Servo](servo.md) und [Servo-Optionen](servo-settings.md)
- [Referenz des Servo-Programmierers](https://github.com/subtilitas/rcbench/blob/main/docs/ServoProgrammer-de.md): jede Meldung, jede Grenze, jede Einschränkung.
