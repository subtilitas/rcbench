---
screen: none
title: Servo-Programmierer
order: 22
---

# Servo-Programmierer

Der Servo-Programmierer liest die 32 Register eines KST- oder Chaservo-Servos über dessen Signalleitung, zeigt sie als Einstellungen und schreibt geänderte Einstellungen zurück. Die Servos beider Marken teilen ein Protokoll und einen Registersatz. Auf [SERVO](servo.md) listet PROGRAMMIEREN in der Leiste die Programmierer. Die Zeile `KST- und Chaservo-Servos` öffnet diesen über der linken Spalte samt den Reitern der Leiste, eine Ebene tiefer; SCHLIESSEN oder das Home-Tag führt zurück zur Liste. Das Öffnen beendet einen laufenden automatischen Test.

![Die Liste von PROGRAMMIEREN](view:servo-program)

Die Zeile nennt, was sie ansteuert: den Port, den SERVO-TESTER nennt, und seinen Pin, `PROG-PORT 1 GP13`. Sie ist gedimmt und öffnet nichts, solange der IOmcu, die RP2350-Platine, kein Servo-PWM meldet, auch wenn kein IOmcu antwortet: die Zeile zeigt dann `IOMCU OHNE SERVO PWM`.

![Die Zeile gedimmt](view:servo-program-nocap)

Sie ist mit `PORT BELEGT` gedimmt, solange eine Hitec-Sitzung, die die Bench-Konsole geöffnet hat, diesen Port hält.

![Der Port belegt](view:servo-program-busy)

KST und Chaservo sind Namen oder Marken ihrer jeweiligen Inhaber. rcbench ist mit ihnen nicht verbunden, wird von ihnen nicht empfohlen und nicht gefördert. Die Namen sagen, mit welchen Servos der Programmierer arbeitet.

Registerbelegung und Timing stammen von 1 KST Tool Card #2 und 1 Chaservo-Servo. Die Wirkung eines Registers auf die Bewegung des Servos ist nicht gemessen: Namen, Einheiten und Grenzen sind die, die das Werkzeug des Herstellers anzeigt und annimmt.

## Was er braucht

![Der Programmierer, während der Tester den Port hält](view:servo-kst)

- Einen IOmcu mit Protokoll 4.13 oder neuer; VERLASSEN braucht 4.15.
- Das Servo am Programmierport des IOmcu, Port 1: GP13, Pad 17, solange kein anderer Pin dafür gespeichert ist.
- SERVO-TESTER auf dem Eintrag PROG-PORT von Ausgänge auf PROG-PORT; ein Tipp auf die Zeile ZIEL öffnet ihn ([die Zeile ZIEL](servo-settings.md#die-zeile-ziel)).
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
| PROGRAMMIERSPANNUNG | 4,8 bis 6,0 V in Schritten von 0,1 V; bis 8,4 V mit HV-SERVO eingeschaltet | 5,0 V |
| STROMBEGRENZUNG | 50 bis 3000 mA | 1000 mA |
| WARTEZEIT VOR PWM | 0 bis 10000 ms | 100 ms |

![Ein PD mini antwortet](view:servo-kst-supply)
![Die Versorgung im Programmiermodus](view:servo-kst-supplied)

Solange der Port aus dem PWM-Betrieb genommen ist, gehören die Sollwerte des Netzteils dem Port: Die Zeile SOLL und der Bildschirm SUPPLY nehmen keine Änderung und kein EIN an. AUSGANG AUS und STOP wirken. Ohne PD mini werden Spannung und Stromgrenze gespeichert und nicht angewendet.

![Ein laufender Vorgang](view:servo-kst-working)

## Der Reiter SERVO

![Ein gelesenes Servo](view:servo-kst-read)

| Zeile | Taste | Wirkung |
|---|---|---|
| PORT | VERBINDEN, ABBRECHEN, VERLASSEN | nimmt den Port, beendet den laufenden Vorgang, gibt den Port zurück |
| SERVO V6, SERVO V8 | ALLES AUSLESEN | liest die 32 Register, jedes, bis 3 von höchstens 5 Lesungen übereinstimmen |
| DATEIEN | WÄHLEN | speichert die Werte des Servos auf die Karte, lädt eine Datei |
| ZURÜCKSPIELEN | WÄHLEN | schreibt ein Backup zurück |
| WEITERES | WÄHLEN | im Programmiermodus: PRÜFEN, ÄNDERUNGEN VERWERFEN, PAIRING LÖSEN |
| SCHREIBEN INS SERVO FREIGEBEN | AUS, EIN | der Schreibschalter |
| ÄNDERUNGEN | n WERTE SCHREIBEN | schreibt die geänderten Werte |

V6 ist eine Ansteuerung mit 20 kHz, V8 eine mit 333 Hz. Der Typ wird nie bearbeitet und nie geschrieben.

![Ein Servo unbekannter Belegung: nur lesen](view:servo-kst-readonly)

Ein Servo, dessen Register nicht zur bekannten Belegung passen, wird gezeigt und nie beschrieben: `Nur Auslesen möglich. Dieser Servotyp ist unbekannt.`

## Bearbeiten

![Geänderte Werte auf ABSTIMMEN](view:servo-kst-tune)

Ein Wert wird in einem Entwurf auf dem Panel bearbeitet. Vor dem Schreiben erreicht nichts das Servo. Ein geänderter Wert ist mit `*` und der Akzentfüllung markiert. Eine Zahl öffnet das Tastenfeld, begrenzt auf das, was die harten Grenzen annehmen; STRETCH öffnet eine Liste; ein Schalter schaltet beim Tipp um.

![Das Tastenfeld mit dem Bereich, den die Grenzen annehmen](view:servo-kst-keypad)
![Die Liste von STRETCH](view:servo-kst-stretch)

| Reiter | Zeilen |
|---|---|
| ABSTIMMEN | DEAD BAND, MAX. DUTY (10 bis 98 %), BOOST (bis 3000 us), STRETCH (x1 bis x32), SOFT START, TEMPOGRENZE, TEMPO |
| PULS | NEUTRAL (1360 bis 1640 us), PULSE LOWER (ab 500 us), PULSE UPPER (bis 2500 us), LEFT ANGLE, RIGHT ANGLE, REVERSION |
| FAILSAFE | BEWEGEN BEI SIGNALVERLUST, POSITION (500 bis 2500 us), NACH (0,4 bis 13,0 s) |
| GESPERRT | der Servotyp, LOCK, MODE und der Schutzblock; nur angezeigt, nie bearbeitet |

![Der Reiter PULS](view:servo-kst-pulse)
![Der Reiter FAILSAFE](view:servo-kst-failsafe)

LEFT ANGLE und RIGHT ANGLE sind Zahlen des Servos; ihre Einheit ist unbekannt.

![Ein Wert, den eine harte Grenze ablehnt](view:servo-kst-hard)

Ein Tipp, der eine harte Grenze verletzen würde, ändert nichts und nennt die Grenze.

![ÄNDERUNG PRÜFEN](view:servo-kst-soft)

Ein Wert innerhalb der harten Grenzen, der eine weiche Grenze verletzt, öffnet ÄNDERUNG PRÜFEN, mit dem Wert des Servos, dem neuen Wert und einer Zeile je Grenze. HALTEN ZUM ÜBERNEHMEN übernimmt ihn nach 2 s; ABBRECHEN lässt den Entwurf, wie er war. Ein [Endlagenlauf](servo.md#der-endlagenlauf) fragt hier nicht: Sein Halten von 2 s beim Start ist die Zustimmung zu einem LEFT ANGLE oder RIGHT ANGLE unter 50.

![Der Reiter GESPERRT](view:servo-kst-locked)

## Schreiben

![Das Schreiben freigeben](view:servo-kst-enable)

SCHREIBEN INS SERVO FREIGEBEN ist ab Werk aus. Ein Tipp auf AUS öffnet eine Seite, die die Risiken nennt; HALTEN ZUM FREIGEBEN schaltet es nach 2 s ein. Es geht aus bei jedem Aufruf von SERVO, bei jedem STOP, VERBINDEN und VERLASSEN, bei verlorenem Link und wenn ein Servo unklar ist.

![Drei geänderte Werte, der Schalter an](view:servo-kst-changes)
![Die Seite zum Schreiben](view:servo-kst-write)

n WERTE SCHREIBEN öffnet die Seite zum Schreiben mit den Änderungen, `DEAD BAND: 2.0 -> 1.6 us`. HALTEN ZUM SCHREIBEN startet das Schreiben nach 2 s.

1. Das erste Schreiben einer Sitzung speichert die Werte des Servos in eine neue Datei auf der SD-Karte (Secure Digital).
2. Das Panel vermerkt das Schreiben in `BENCH/KST/CONF/KST.TXT`, oder ohne Karte im Speicher.
3. Der IOmcu prüft die Werte gegen dieselben Grenzen, plant die Registerschreibvorgänge und führt sie aus.
4. Jedes geschriebene Register wird zurückgelesen. Ein Schreiben, das nicht übernommen wird, wird rückgängig gemacht.

![Ein laufendes Schreiben: nicht abziehen](view:servo-kst-writing)
![Ein fertiges Schreiben](view:servo-kst-written)

Ein Wert, der geschrieben und zurückgelesen ist, gilt als wirksam. Ob ein Servo ihn vor seinem nächsten Einschalten anwendet, ist nicht gemessen.

## Dateien

![Die Dateien auf der Karte](view:servo-kst-files)

DATEIEN listet die Servodateien im Ordner `BENCH/KST/CONF` der Karte, 5 je Seite, jede mit ihrem Servotyp. Der erste Eintrag speichert die Werte des Servos unter einem neuen Namen, `KST001` und aufwärts, bis 16 Zeichen.

![Der Name einer neuen Datei](view:servo-kst-name)
![Eine geladene Datei](view:servo-kst-file-loaded)

Ein Tipp auf eine Datei lädt sie in den Entwurf. Vor dem Schreiben erreicht nichts das Servo. Eine Datei des anderen Servotyps, eine beschädigte Datei oder ein Wert außerhalb seiner harten Grenze wird nicht geladen, und die Zeile sagt warum.

## ZURÜCKSPIELEN und WEITERES

![Die Liste von ZURÜCKSPIELEN](view:servo-kst-restore-list)
![Das Zurückspielen des Backups der Sitzung](view:servo-kst-restore)

ZURÜCKSPIELEN schreibt das Backup der Sitzung zurück, das Servo, wie es nach VERBINDEN zuerst gelesen wurde, die Werte vor einem unterbrochenen Schreiben oder `Werte vor dem Endlagenlauf.`. Jedes Zurückspielen braucht den Schreibschalter und ein Halten von 2 s.

![WEITERES im Programmiermodus](view:servo-kst-more)

- PRÜFEN liest alle Register und vergleicht sie mit dem gehaltenen Abbild.
- ÄNDERUNGEN VERWERFEN setzt den Entwurf auf die Werte des Servos zurück.
- PAIRING LÖSEN setzt Register 1D nach einem Halten von 2 s auf 00. Was das Servo mit dem Wert macht, ist nicht bekannt.

## Ein unterbrochenes Schreiben

![Ein unterbrochenes Schreiben](view:servo-kst-doubt)

Ein Schreiben gilt als unterbrochen, wenn der Link abreißt, eine Platine neu startet, ABBRECHEN oder ein Stopp es beendet oder kein Ergebnis rechtzeitig ankommt. Der Reiter zeigt dann `Schreiben wurde unterbrochen. Die Einstellungen sind vielleicht unvollständig.`, bis das Servo erneut gelesen ist. ALLES AUSLESEN vergleicht das Servo mit dem Vermerk und bietet ZURÜCKSPIELEN an.

![Das Servo hält eine Mischung alter und neuer Werte](view:servo-kst-journal)

## Verlassen

![PROGRAMMIERMODUS VERLASSEN](view:servo-kst-leave)

VERLASSEN, oder SCHLIESSEN oder das Home-Tag im Programmiermodus, öffnet PROGRAMMIERMODUS VERLASSEN: Der Port geht zurück auf Servo-PWM, die Versorgung bleibt, wie sie ist, und das Servo kann fahren. HALTEN ZUM VERLASSEN übernimmt es nach 2 s. Der Port hält seine Leitung für WARTEZEIT VOR PWM auf low und gibt den Pin dann dem Tester. Das Verlassen des Bildschirms SERVO ist kein VERLASSEN: Sitzung und Port bleiben, und beim nächsten Aufruf ist der Programmierer auf PROGRAMMIEREN wieder offen. Über einem Tastenfeld, einer Liste, einer Warnung oder der Buchstabentastatur im Programmierer schließt das Home-Tag zuerst dieses Feld.

## Verwandte Seiten

- [Servo](servo.md) und [Servo-Optionen](servo-settings.md)
- [Referenz des Servo-Programmierers](https://github.com/subtilitas/rcbench/blob/main/docs/ServoProgrammer-de.md): jede Meldung, jede Grenze, jede Einschränkung.
