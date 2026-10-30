---
screen: none
title: Servo-Programmierer
order: 22
---

# Servo-Programmierer

Der Servo-Programmierer liest die 32 Register eines KST- oder Chaservo-Servos über dessen Signalleitung, zeigt sie als Einstellungen und schreibt geänderte Einstellungen zurück. Die Servos beider Marken teilen ein Protokoll und einen Registersatz. Auf [SERVO](servo.md) listet PROGRAMMIEREN in der Leiste die Programmierer. Die Zeile `KST- und Chaservo-Servos` öffnet diesen über der linken Spalte samt den Reitern der Leiste, eine Ebene tiefer; SCHLIESSEN oder das Home-Tag führt zurück zur Liste. Das Öffnen beendet einen laufenden automatischen Test.

![Die Liste von PROGRAMMIEREN](view:servo-program)

Die Zeile nennt, was sie ansteuert: den Port, den SERVO-TESTER nennt, und seinen Pin, `PROG-PORT 1 GPIO 13`. Sie ist gedimmt und öffnet nichts, solange der IOmcu, die RP2350-Platine, kein Servo-PWM meldet, auch wenn kein IOmcu antwortet: die Zeile zeigt dann `IOMCU OHNE SERVO PWM`.

![Die Zeile gedimmt](view:servo-program-nocap)

Sie ist mit `PORT BELEGT` gedimmt, solange eine Hitec-Sitzung, die die Bench-Konsole geöffnet hat, diesen Port hält.

![Der Port belegt](view:servo-program-busy)

KST und Chaservo sind Namen oder Marken ihrer jeweiligen Inhaber. rcbench ist mit ihnen nicht verbunden, wird von ihnen nicht empfohlen und nicht gefördert. Die Namen sagen, mit welchen Servos der Programmierer arbeitet.

Registerbelegung und Timing stammen von 1 KST Tool Card #2 und 1 Chaservo-Servo. Die Wirkung eines Registers auf die Bewegung des Servos ist nicht gemessen: Namen, Einheiten und Grenzen sind die, die das Werkzeug des Herstellers anzeigt und annimmt.

## Was er braucht

![Der Programmierer, während der Tester den Port hält](view:servo-kst)

- Einen IOmcu mit 0.17.0 oder neuer (Protokoll 4.15, VERLASSEN eingeschlossen).
- Das Servo an einem Programmierport des IOmcu, PROG-PORT 1 bis 4, mit FUNKTION SERVO PWM und einem Pin: Port 1 ist GPIO 13, Pad 17, solange kein anderer Pin dafür gespeichert ist; die Ports 2 bis 4 haben den Pin, der auf ihrem Eintrag PROG-PORT von Ausgänge gesetzt ist.
- SERVO-TESTER auf diesem Eintrag auf diesen Port, PORT 1, PORT 2, PORT 3 oder PORT 4; ein Tipp auf die Zeile ZIEL öffnet ihn ([die Zeile ZIEL](servo-settings.md#die-zeile-ziel)).
- Den Prüfstand SAFE. VERBINDEN wird bei ARMED abgelehnt mit `Zuerst DISARM tippen. Bei ARMED kann der Port nicht übernommen werden.` Solange der Port gehalten wird, schaltet kein Bildschirm den Prüfstand auf ARMED.

![Ein IOmcu ohne Programmierport](view:servo-kst-nopage)
![Die Standardausgänge sind das Ziel](view:servo-kst-target)

Fehlt eines davon, ist jedes Bedienelement außer SCHLIESSEN aus, und der Reiter sagt, was fehlt.

## Der Port und die Versorgung

Der Port ist im PWM-Betrieb (Pulsweitenmodulation) oder im Programmiermodus, nie in beidem. VERBINDEN nimmt den Port aus dem PWM-Betrieb, stellt die Versorgung, wo ein PD mini antwortet, führt die Einstiegsfolge aus und liest alle 32 Register. Die Taste lautet dann VERLASSEN, und ABBRECHEN, solange ein Vorgang läuft.

![Was VERBINDEN stellt](view:servo-kst-options)

WEITERES stellt, solange der Tester den Port hat, was VERBINDEN anwendet:

| Eintrag | Bereich | Vorgabe |
|---|---|---|
| PROGRAMMIERSPANNUNG | 4,8 bis 6,0V in Schritten von 0,1V; bis 8,4V mit HV-SERVO eingeschaltet | 5,0V |
| STROMBEGRENZUNG | 50 bis 3000mA | 1000mA |
| WARTEZEIT VOR PWM | 0 bis 10000ms | 100ms |

![Ein PD mini antwortet](view:servo-kst-supply)
![Die Versorgung im Programmiermodus](view:servo-kst-supplied)

Solange der Port aus dem PWM-Betrieb genommen ist, gehören die Sollwerte des Netzteils dem Port: Die Zeile SOLL und der Bildschirm SUPPLY nehmen keine Änderung und kein EIN an. AUSGANG AUS und STOP wirken. Ohne PD mini werden Spannung und Stromgrenze gespeichert und nicht angewendet. Wieder in PWM kehren die Sollwerte von vor der Sitzung zurück, sobald der Ausgang aus ist, beim nächsten AUS, wenn die Sitzung ihn eingeschaltet gelassen hat; ein zwischendurch geänderter Sollwert bleibt.

![Die Frage nach dem Verlassen](view:servo-hitec-restore-ask)

Ein Verlassen, das den Ausgang bei den Sollwerten der Sitzung eingeschaltet lässt, während die Sollwerte von vor der Sitzung andere sind, lässt SERVO fragen, sobald der Port in PWM ist: `Deine Versorgungs-Sollwerte 5.50V 2.00A am eingeschalteten Ausgang wiederherstellen?` JA gibt dem eingeschalteten Ausgang beide; eine Spannung, die von 6,0V oder darunter über 6,0V steigt, öffnet zuerst NUR HV-SERVOS. NEIN behält die der Sitzung bis zum nächsten AUS. Die Frage kommt einmal je Verlassen, wartet auf Tastenfelder, Listen, Warnungen und Fragen von SERVO und das Halten von AUSGANG EIN, und kommt nicht, solange ein Port das Netzteil hält, nach einem AUS, einem Fehler oder einem STOP, innerhalb eines Endlagenlaufs oder wenn sich seitdem ein Sollwert änderte. Nur ein Verlassen fragt: PORT FREIGEBEN, ein STOP, ein Fehler, ein verlorener Link, ein Neustart des Panels oder des IOmcu, ein Port, den der IOmcu ohne Verlassen zurückgibt, und Ports, die unter einer Sitzung auf ihre eigene Versorgung wechseln, beenden das Halten ohne die Frage, und die Sollwerte der Sitzung bleiben bis zum nächsten AUS. Der Hitec-Programmierer fragt ebenso.

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) laufen die Programmierports an eigenen 5V, geschaltet über GPIO 21, nicht am PD mini; WEITERES zeigt `PROGRAMMIERSPANNUNG: 5V FEST, GPIO 21`. Die Sitzung schaltet die 5V dort, wo die Tabelle unten den Ausgang des PD mini schaltet. Das Feld PROG 5V unter PD MINI zeigt dann `BELEGT VON PROG-PORT n` und nimmt keinen Druck an. Ohne Sitzung schaltet das Feld die 5V von Hand: PROG 5V EIN ist ein Halten von 2s, PROG 5V AUS ein Tipp.

![Der KST-Programmer hält die 5V](view:servo-kst-rbmod)

## Der Versorgungszyklus

Ein KST- oder Chaservo-Servo tritt nur aus einem Einschalten in den Programmiermodus ein und handelt nach einem geschriebenen Wert erst ab seinem nächsten Einschalten: ein Chaservo am Prüfstand überging VERBINDEN bei eingeschalteter Versorgung und folgte nach einem Schreiben keinem PWM, bis seine Versorgung aus und wieder an war.

| Schritt | Ein PD mini mit eingeschaltetem Ausgang | Kein PD mini |
|---|---|---|
| VERBINDEN | der Ausgang 1s aus, an, die Leitung 1s auf low, dann der Einstieg | der Einstieg 250ms nach dem Tippen: Servoversorgung aus- und einschalten, 1s später VERBINDEN tippen |
| VERLASSEN nach einem Schreiben | der Ausgang 1s aus, an, die Leitung 1s auf low, dann PWM | PWM; die Werte wirken nach Aus- und Einschalten der Servoversorgung |
| VERLASSEN ohne Geschriebenes | PWM nach WARTEZEIT VOR PWM, die Versorgung, wie sie ist | dasselbe |

Mit ausgeschaltetem Ausgang schaltet VERBINDEN ihn ein und steigt ein. ABBRECHEN oder STOP in der 1s aus lässt den Ausgang aus: `Der Versorgungszyklus wurde abgebrochen. Der Ausgang des PD mini bleibt aus.`

![VERBINDEN mit eingeschaltetem Ausgang](view:servo-kst-supply-on)
![Die 1s aus](view:servo-kst-cycling)

![Ein laufender Vorgang](view:servo-kst-working)

## Der Reiter SERVO

![Ein gelesenes Servo](view:servo-kst-read)

| Zeile | Taste | Wirkung |
|---|---|---|
| PORT | VERBINDEN, ABBRECHEN, VERLASSEN | nimmt den Port, beendet den laufenden Vorgang, gibt den Port zurück |
| SERVO | ALLES AUSLESEN | liest die 32 Register, jedes, bis 3 von höchstens 5 Lesungen übereinstimmen |
| TYPE | V6?, V8?, V6, V8 | der Servotyp, V6 oder V8, wie der Bediener ihn setzt |
| DATEIEN | WÄHLEN | speichert die Werte des Servos auf die Karte, lädt eine Datei |
| WEITERES | WÄHLEN | im Programmiermodus: PRÜFEN, ÄNDERUNGEN VERWERFEN, PAIRING LÖSEN |
| SCHREIBEN INS SERVO FREIGEBEN | AUS, EIN | der Schreibschalter |
| ZURÜCKSPIELEN | WÄHLEN | schreibt ein Backup zurück |
| (keine Beschriftung) | n WERTE SCHREIBEN | schreibt die geänderten Werte |

Kein Register hält die Serie: V8-Flächenservos lesen mit gesetztem 20k_sel, wie V6. Nach einem Lesen zeigt TYPE den Typ, den 20k_sel nahelegt, mit Fragezeichen, `V6?`, und die Zeile unter dem Zustand bittet, das Etikett des Servos zu prüfen. Ein Tippen auf TYPE setzt V6 oder V8. Der Typ gehört der Sitzung, geht in eine gespeicherte Datei und hält ein Laden: eine Datei eines anderen Typs wird nicht geladen, und eine Datei, die ohne gesetzten Typ angetippt wird, fragt zuerst nach dem Typ. Der Typ wird nie ins Servo geschrieben.

![Die Liste TYPE](view:servo-kst-type)
![Ein gesetzter Typ](view:servo-kst-type-set)

![Ein Servo unbekannter Belegung: nur lesen](view:servo-kst-readonly)

Ein Servo, dessen Register nicht zur bekannten Belegung passen, wird gezeigt und nie beschrieben: `Nur Auslesen möglich. Dieser Servotyp ist unbekannt.`

## Bearbeiten

![Geänderte Werte auf ABSTIMMEN](view:servo-kst-tune)

Ein Wert wird in einem Entwurf auf dem Panel bearbeitet. Vor dem Schreiben erreicht nichts das Servo. Ein geänderter Wert ist mit `*` und der Akzentfüllung markiert. Eine Zahl öffnet das Tastenfeld, begrenzt auf das, was die harten Grenzen annehmen; STRETCH öffnet eine Liste; ein Schalter schaltet beim Tipp um.

![Das Tastenfeld mit dem Bereich, den die Grenzen annehmen](view:servo-kst-keypad)
![Die Liste von STRETCH](view:servo-kst-stretch)

| Reiter | Zeilen |
|---|---|
| ABSTIMMEN | DEAD BAND, MAX. DUTY (10 bis 98%), BOOST (bis 3000us), STRETCH (x1 bis x32), SOFT START, TEMPOGRENZE, TEMPO |
| PULS | NEUTRAL (1360 bis 1640us), PULSE LOWER (ab 500us), PULSE UPPER (bis 2500us), LEFT ANGLE, RIGHT ANGLE, REVERSION |
| FAILSAFE | BEWEGEN BEI SIGNALVERLUST, POSITION (500 bis 2500us), NACH (0,4 bis 13,0s) |
| GESPERRT | 20K SEL, LOCK, MODE und der Schutzblock; nur angezeigt, nie bearbeitet |

![Der Reiter PULS](view:servo-kst-pulse)
![Der Reiter FAILSAFE](view:servo-kst-failsafe)

LEFT ANGLE und RIGHT ANGLE sind Zahlen des Servos; ihre Einheit ist unbekannt.

![Ein Wert, den eine harte Grenze ablehnt](view:servo-kst-hard)

Ein Tipp, der eine harte Grenze verletzen würde, ändert nichts und nennt die Grenze.

![ÄNDERUNG PRÜFEN](view:servo-kst-soft)

Ein Wert innerhalb der harten Grenzen, der eine weiche Grenze verletzt, öffnet ÄNDERUNG PRÜFEN, mit dem Wert des Servos, dem neuen Wert und einer Zeile je Grenze. 2s HALTEN ZUM ÜBERNEHMEN übernimmt ihn nach 2s; ABBRECHEN lässt den Entwurf, wie er war. Ein [Endlagenlauf](servo.md#der-endlagenlauf) fragt hier nicht: Sein Halten von 2s beim Start ist die Zustimmung zu einem LEFT ANGLE oder RIGHT ANGLE unter 50.

![Der Reiter GESPERRT](view:servo-kst-locked)

## Schreiben

![Das Schreiben freigeben](view:servo-kst-enable)

SCHREIBEN INS SERVO FREIGEBEN ist ab Werk aus. Ein Tipp auf AUS öffnet eine Seite, die die Risiken nennt; 2s HALTEN ZUM FREIGEBEN schaltet es nach 2s ein. Es geht aus bei jedem Aufruf von SERVO, bei jedem STOP, VERBINDEN und VERLASSEN, bei verlorenem Link und wenn ein Servo unklar ist.

![Drei geänderte Werte, der Schalter an](view:servo-kst-changes)
![Die Seite zum Schreiben](view:servo-kst-write)

n WERTE SCHREIBEN öffnet die Seite zum Schreiben mit den Änderungen, `DEAD BAND: 2.0 -> 1.6us`. 2s HALTEN ZUM SCHREIBEN startet das Schreiben nach 2s.

1. Das erste Schreiben einer Sitzung speichert die Werte des Servos in eine neue Datei auf der SD-Karte (Secure Digital).
2. Das Panel vermerkt das Schreiben in `BENCH/KST/CONF/KST.TXT`, oder ohne Karte im Speicher.
3. Der IOmcu prüft die Werte gegen dieselben Grenzen, plant die Registerschreibvorgänge und führt sie aus.
4. Jedes geschriebene Register wird zurückgelesen. Ein Schreiben, das nicht übernommen wird, wird rückgängig gemacht.

![Ein laufendes Schreiben: nicht abziehen](view:servo-kst-writing)
![Ein fertiges Schreiben](view:servo-kst-written)

Ein Wert, der geschrieben und zurückgelesen ist, steht im Servo und wirkt ab seinem nächsten Einschalten. Mit PD mini sagt die Meldung `Die Werte wirken nach VERLASSEN: es schaltet die Versorgung 1s aus und wieder ein.`; ohne ihn `Die geschriebenen Werte wirken erst nach Aus- und Einschalten der Servoversorgung.`

![Ein Schreiben mit PD mini](view:servo-kst-written-supply)

## Dateien

![Die Dateien auf der Karte](view:servo-kst-files)

DATEIEN listet die Servodateien im Ordner `BENCH/KST/CONF` der Karte, 5 je Seite, jede mit ihrem Servotyp. Der erste Eintrag speichert die Werte des Servos unter einem neuen Namen, `KST001` und aufwärts, bis 16 Zeichen.

![Der Name einer neuen Datei](view:servo-kst-name)
![Eine geladene Datei](view:servo-kst-file-loaded)

Ein Tipp auf eine Datei lädt sie in den Entwurf. Vor dem Schreiben erreicht nichts das Servo. Eine Datei eines anderen Typs als des unter TYPE gesetzten, eine beschädigte Datei oder ein Wert außerhalb seiner harten Grenze wird nicht geladen, und die Zeile sagt warum. Ohne gesetzten Typ öffnet der Tipp zuerst `SERVO-TYPE VOR DEM LADEN`.

![Der Typ vor einem Laden](view:servo-kst-type-first)

## ZURÜCKSPIELEN und WEITERES

![Die Liste von ZURÜCKSPIELEN](view:servo-kst-restore-list)
![Das Zurückspielen des Backups der Sitzung](view:servo-kst-restore)

ZURÜCKSPIELEN schreibt das Backup der Sitzung zurück, das Servo, wie es nach VERBINDEN zuerst gelesen wurde, die Werte vor einem unterbrochenen Schreiben oder `Werte vor dem Endlagenlauf.`. Jedes Zurückspielen braucht den Schreibschalter und ein Halten von 2s.

![WEITERES im Programmiermodus](view:servo-kst-more)

- PRÜFEN liest alle Register und vergleicht sie mit dem gehaltenen Abbild.
- ÄNDERUNGEN VERWERFEN setzt den Entwurf auf die Werte des Servos zurück.
- PAIRING LÖSEN setzt Register 1D nach einem Halten von 2s auf 00. Was das Servo mit dem Wert macht, ist nicht bekannt.

## Ein unterbrochenes Schreiben

![Ein unterbrochenes Schreiben](view:servo-kst-doubt)

Ein Schreiben gilt als unterbrochen, wenn der Link abreißt, eine Platine neu startet, ABBRECHEN oder ein Stopp es beendet oder kein Ergebnis rechtzeitig ankommt. Der Reiter zeigt dann `Schreiben wurde unterbrochen. Die Einstellungen sind vielleicht unvollständig.`, bis das Servo erneut gelesen ist. ALLES AUSLESEN vergleicht das Servo mit dem Vermerk und bietet ZURÜCKSPIELEN an.

![Das Servo hält eine Mischung alter und neuer Werte](view:servo-kst-journal)

## Verlassen

![PROGRAMMIERMODUS VERLASSEN](view:servo-kst-leave)
![VERLASSEN nach einem Schreiben mit PD mini](view:servo-kst-leave-cycle)

VERLASSEN, oder SCHLIESSEN oder das Home-Tag im Programmiermodus, öffnet PROGRAMMIERMODUS VERLASSEN: Der Port geht zurück auf Servo-PWM, und das Servo kann fahren. Nach einem Schreiben mit PD mini sagt die Seite, dass der Ausgang zuerst 1s aus und wieder an geht; ohne PD mini sagt sie, dass die Werte nach Aus- und Einschalten der Versorgung wirken. Ohne Geschriebenes bleibt die Versorgung, wie sie ist. 2s HALTEN ZUM VERLASSEN übernimmt es nach 2s. Der Port hält seine Leitung für WARTEZEIT VOR PWM auf low und gibt den Pin dann dem Tester. Das Verlassen des Bildschirms SERVO ist kein VERLASSEN: Sitzung und Port bleiben, und beim nächsten Aufruf ist der Programmierer auf PROGRAMMIEREN wieder offen. Über einem Tastenfeld, einer Liste, einer Warnung oder der Buchstabentastatur im Programmierer schließt das Home-Tag zuerst dieses Feld.

## Verwandte Seiten

- [Servo](servo.md) und [Servo-Optionen](servo-settings.md)
- [Referenz des Servo-Programmierers](https://github.com/subtilitas/rcbench/blob/main/docs/ServoProgrammer-de.md): jede Meldung, jede Grenze, jede Einschränkung.
