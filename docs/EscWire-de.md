# ESC-Signalleitung

<sub>[English](EscWire.md) · **Deutsch**</sub>

Diese Seite beschreibt die Zeile `AM32, Bluejay, BLHeli_S, ESCape32`, rechts `SIGNALLEITUNG`, unter MOTOR & ESC > PROGRAMMIEREN. ESC ist Electronic Speed Controller. Die Sitzung liest und schreibt die Einstellungen eines ESC über seine Signalleitung, von einem Gas-Slot oder einem DShot-Port aus. Die Zeile bleibt abgeblendet, bis der IOmcu `LINK_CAP_PROGRAM` meldet. Die Pages dahinter stehen in [Die ESC-Einstellungsseiten](LinkEsc-de.md).

Dieser Code entstand aus einer Verhaltensspezifikation, ohne Zugriff auf die Firmware-Quellen.

## Aktuelle Einschränkungen

- Kein Frame hat einen echten ESC erreicht. Jedes Ergebnis hier stammt von einem simulierten ESC in den Host-Tests und von Bench-Läufen ohne ESC an der Leitung.
- Bluejay und ESCape32 liefen nicht auf Hardware.
- Ein bidirektionaler DShot-Pin ruht bei SAFE auf high, die Eintrittsbedingung eines Bootloaders. Was das nächste ARM danach tut, ist nicht gemessen.
- Nach `TORN` oder `LOST` gehen alle Ausgänge aus. Auf BLHeli_S und Bluejay verliert die Seite dann ihre 48 Tag-Bytes und liest sich nur noch. Ein AM32 bleibt gelöscht bis AUS BACKUP HERSTELLEN.
- 10 fehlgeschlagene Lesungen der Page, 50ms beim Poll-Takt, stoppen eine Sitzung. Ein Schreiben bekommt keine Ausnahme: es endet als `NO_ANSWER`, mit Fehler und STOP.
- Der Eintrag `ESCW.TXT` entsteht vor dem Schreiben. Er sagt nicht, wie das Schreiben endete.
- SimonK wird nicht unterstützt: dieser Bench hat keinen Code dafür, und kein Screen bietet es an. Ein ESC mit SimonK gibt `Keine Antwort`.

## Was es nimmt

| Firmware | Geschrieben | Nur lesen, mit Feldnamen | Editierbare Felder |
|---|---|---|---:|
| BLHeli_S | Layout 33 | Layout 32 | 12 |
| Bluejay | Layout 208 | Layouts 33, 200 bis 207 | 15 |
| AM32 | Revisionen 3 und 4 | | 39 auf Revision 3, 40 auf Revision 4 |
| ESCape32 | Revision 17 | | 46 |

Eine Seite ohne Feldnamen zeigt ihre Bytes, 16 je Zeile. Eine Firmware oder ein Layout, das nicht geschrieben wird, hat keine Taste zum Schreiben.

## Verbinden

Der Streifen nennt, was die Sitzung treibt: `ZIEL SLOT 1 GPIO 0`, dann das Netzteil und SAFE oder ARMED. Kein Pin steht in der Warnfarbe.

| Zeile | Bereich | Vorgabe |
|---|---|---|
| ZIEL | Gas-Slots und DShot-Ports | der erste |
| STROM | SWITCHED, BY HAND | SWITCHED |
| SPANNUNG | 3.00V bis zur Grenze von SUPPLY | Sollwert von SUPPLY |
| LIMIT | 0.05A bis zur Grenze von SUPPLY | Sollwert von SUPPLY |
| ENTRY HOLD | 0.5s bis 120s | 3.0s |
| AUS-ZEIT | 0s bis 10s | 1.0s |

Die Sitzung hat keine Vorgabespannung. SPANNUNG und LIMIT folgen SUPPLY, bis ein Tipp auf die Zahl eine andere eingibt. SWITCHED nutzt den PD mini. BY HAND legt die Leitung auf high und wartet höchstens 60s auf `STROM IST AN`.

Nimm den Propeller ab. Schließe nur Signal und Masse an: ein BEC (Battery Eliminator Circuit) auf der Mittelader des ESC speist sonst die Schiene des Bench. Stecke Servos ab. VERBINDEN ist ein roter Hold von 2s.

![Verbinden](img/de/motor-wire-connect.png)

![Verbinden, BY HAND](img/de/motor-wire-hand.png)

![Tastenfeld Spannung](img/de/motor-wire-keypad.png)

![Zielliste](img/de/motor-wire-target.png)

![Kein Ziel](img/de/motor-wire-notarget.png)

![Eintritt](img/de/motor-wire-entering.png)

![Wartet auf Strom](img/de/motor-wire-handwait.png)

## Die Einstellungen

IDENTITÄT nennt Firmware, Version, Layout oder Revision, Signatur oder Name, und `SCHREIBBAR` oder `NUR LESEN` mit Grund. Die anderen Tabs halten die Felder. Ein Tipp auf eine Zahl öffnet das Tastenfeld, ein Tipp auf eine Auswahl ihre Liste, und jeder Editor zeigt die Hilfezeile des Felds. Eine Änderung ändert einen Entwurf auf dem Panel und markiert den Feldnamen; zum ESC geht nichts vor dem Schreiben.

![AM32 Identität](img/de/motor-wire-am.png)

![AM32 Motor](img/de/motor-wire-am-motor.png)

![AM32 Bremse und Sinus](img/de/motor-wire-am-brake.png)

![AM32 Eingang](img/de/motor-wire-am-input.png)

![AM32 Schutz](img/de/motor-wire-am-protect.png)

![Eine Auswahl](img/de/motor-wire-am-choice.png)

![Eine Zahl](img/de/motor-wire-am-number.png)

![BLHeli_S](img/de/motor-wire-s.png)

![Bluejay](img/de/motor-wire-bj.png)

![ESCape32 Identität](img/de/motor-wire-e32.png)

![ESCape32 Motor](img/de/motor-wire-e32-1.png)

![ESCape32 Duty](img/de/motor-wire-e32-2.png)

![ESCape32 Gas](img/de/motor-wire-e32-3.png)

![ESCape32 Eingang](img/de/motor-wire-e32-4.png)

![ESCape32 Schutz](img/de/motor-wire-e32-5.png)

![ESCape32 Ton](img/de/motor-wire-e32-6.png)

![Nur lesen, Identität](img/de/motor-wire-readonly.png)

![Nur lesen, Zeilen](img/de/motor-wire-readonly-rows.png)

![Bytes eines Layouts ohne Feldnamen](img/de/motor-wire-hex.png)

## Schreiben

n WERTE SCHREIBEN öffnet eine Seite mit den Änderungen. Jedes heikle Feld bleibt editierbar: die Seite nennt es zuerst mit `PRÜFE` und seinem neuen Wert. Schreiben ist ein roter Hold von 2s.

1. Das erste Schreiben einer Sitzung speichert das Backup der Sitzung nach `BENCH/ESCW/CONF/ESCW001.TXT` bis `ESCW999.TXT`. Ohne Karte, oder bei einer Karte, die es ablehnt, fragt die Seite; ein zweiter Hold von 2s geht ohne.
2. Der Eintrag `BENCH/ESCW/CONF/ESCW.TXT` nimmt die geänderten Felder und den ESC wie gelesen auf. Eine Karte, die ihn ablehnt, stellt dieselbe Frage.
3. Die Page liest neu, schreibt, liest zurück und nennt das Ergebnis in Worten. Ein Schreiben auf BLHeli_S oder Bluejay dauert etwa 3848ms.

![Schreiben](img/de/motor-wire-am-write.png)

![Schreibt](img/de/motor-wire-am-running.png)

DATEIEN listet die Dateien auf der Karte, die neueste zuerst, und speichert das Backup der Sitzung von Hand. Eine Datei geht in den Entwurf, auf dieselbe Firmware und dasselbe Layout, nur editierbare Felder. Eine im Editor des Panels geänderte Datei hat `edited` als letzte Zeile statt der Prüfsumme, und der Programmierer liest ihre Feldzeilen.

## Gelöschter AM32

Ein AM32, dessen Einstellungen sich als 0xFF lesen, bietet AUS BACKUP HERSTELLEN und sonst nichts. Eine Datei gilt nur, wenn ihre CRC-32 stimmt und Pin-Code, Flash-Code, Bootloader-Protokoll und Name denen des ESC gleichen. Das Herstellen ist ein roter Hold von 2s.

![Gelöschter AM32](img/de/motor-wire-erased.png)

## Verlassen

ZURÜCK öffnet VERLASSEN, einen Hold von 2s. Der ESC verlässt seinen Bootloader, die Leitung geht auf low, die Versorgung aus und der Pin an seinen Ausgang zurück. Nicht geschriebene Änderungen gehen verloren.

ESCape32 hat kein LEAVE: ein Power-Cycle beendet die Sitzung. SWITCHED parkt die Leitung und schaltet die Versorgung aus. BY HAND parkt die Leitung und sagt, den Strom des ESC zu trennen.

![Verlassen](img/de/motor-wire-am-leave.png)

![Verlassen auf ESCape32](img/de/motor-wire-e32-leave.png)

## Stops und andere Schreiber

STOP im Band stoppt die Sitzung. Ein Schreiben läuft erst bis zum Zurücklesen. ABBRUCH auf der Seite schneidet sofort, auch im Schreiben. Nach einem Stop oder Fehler bietet die Seite STOP LÖSCHEN und PIN ZURÜCK. Ein Pin, den ein anderer Schreiber hält, die Bench-Konsole oder eine Sitzung vor einem Panel-Neustart, bietet PIN FREIGEBEN mit einem Hold von 2s. ARM wird abgelehnt, solange ein Pin gehalten ist, und der Alarm nennt die ESC-Programmierung.

![Gestoppt](img/de/motor-wire-stopped.png)

![Anderer Schreiber](img/de/motor-wire-other.png)
