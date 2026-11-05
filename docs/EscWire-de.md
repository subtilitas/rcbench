# ESC-Signalleitung

<sub>[English](EscWire.md) · **Deutsch**</sub>

Unter MOTOR & ESC > PROGRAMMIEREN steht rechts in der Zeile `AM32, Bluejay, BLHeli_S, ESCape32` die Bezeichnung `SIGNALLEITUNG`. Diese Zeile öffnet eine Sitzung, die Einstellungen eines elektronischen Drehzahlreglers (ESC, Electronic Speed Controller) über seine Signalleitung ausliest und schreibt. Dafür nutzt sie einen Gas-Slot oder einen DShot-Port. Die Zeile bleibt abgeblendet, bis der IOmcu `LINK_CAP_PROGRAM` meldet. Die Datenformate beschreibt [Die ESC-Einstellungsseiten](LinkEsc-de.md).

Dieser Code entstand aus einer Verhaltensspezifikation, ohne Zugriff auf die Firmware-Quellen.

## Aktuelle Einschränkungen

- Nach `TORN` oder `LOST` gehen alle Ausgänge aus. Bei BLHeli_S und Bluejay verliert die Einstellungsseite dann ihre 48 Tag-Bytes und lässt sich nur noch auslesen. Ein AM32 bleibt gelöscht, bis du BACKUP ZURÜCKSPIELEN ausführst.
- 10 fehlgeschlagene Leseversuche der Seite stoppen die Sitzung. Das entspricht beim Poll-Takt 50ms. Dies gilt auch während eines Schreibvorgangs: Er endet mit `NO_ANSWER`, einem Fehler und STOP.
- Der Vermerk `ESCW.TXT` wird vor dem Schreiben gespeichert. Er bleibt auf der Karte, auch wenn der Schreibvorgang danach abgebrochen wird. Sein Ergebnis geht aus dem Vermerk nicht hervor.
- SimonK wird nicht unterstützt: Der Prüfstand hat dafür keine Implementierung über die Signalleitung, und keine Seite bietet es an. Ein ESC mit SimonK meldet `Keine Antwort`.

## Was es nimmt

| Firmware | Geschrieben | Nur lesen, mit Feldnamen | Editierbare Felder |
|---|---|---|---:|
| BLHeli_S | Layout 33 | Layout 32 | 12 |
| Bluejay | Layout 208 | Layouts 33, 200 bis 207 | 15 |
| AM32 | Revisionen 3 und 4 | | 39 auf Revision 3, 40 auf Revision 4 |
| ESCape32 | Revision 17 | | 46 |

Eine Seite ohne Feldnamen zeigt ihre Bytes, 16 je Zeile. Bei einer Firmware oder einem Layout ohne Schreibunterstützung fehlt die Taste zum Schreiben.

## Drücken und Halten

Eine Taste bleibt vom Drücken bis zum Loslassen in der gedrückten Farbe. Dabei wird nur diese Taste neu gezeichnet. Beim Halten füllt sie sich innerhalb von 2s von ihrer dunklen Farbe zu Rot, in beiden Framebuffern. ABBRECHEN wirkt sofort beim Drücken.

![Hold auf VERBINDEN bei 1s von 2s](img/de/motor-wire-fill-connect.png)

![Hold auf NEU STARTEN bei 1s von 2s](img/de/motor-wire-fill-again.png)

![Hold auf SCHREIBEN bei 1s von 2s](img/de/motor-wire-fill-write.png)

![Hold auf ohne Backup schreiben bei 1s von 2s](img/de/motor-wire-fill-no-backup.png)

![Hold auf VERLASSEN bei 1s von 2s](img/de/motor-wire-fill-leave.png)

![Hold auf FREIGEBEN bei 1s von 2s](img/de/motor-wire-fill-release.png)

![Hold auf ZURÜCKSPIELEN bei 1s von 2s](img/de/motor-wire-fill-restore.png)

![NEU AUSLESEN gedrückt](img/de/motor-wire-press-read.png)

![VERSORGUNG EIN gedrückt](img/de/motor-wire-press-hand.png)

![ZURÜCK gedrückt](img/de/motor-wire-press-back.png)

![eine Zeile gedrückt](img/de/motor-wire-press-row.png)

![ABBRECHEN gedrückt](img/de/motor-wire-press-cancel.png)

![WEITER gedrückt](img/de/motor-wire-press-next.png)

![ZURÜCK der langen Liste gedrückt](img/de/motor-wire-press-prev.png)

![ein Listeneintrag gedrückt](img/de/motor-wire-press-choice.png)

![ABBRECHEN einer Liste gedrückt](img/de/motor-wire-press-list-cancel.png)

## Verbinden

Der Streifen zeigt das Ziel der Sitzung, zum Beispiel `ZIEL SLOT 1 GPIO 0`, gefolgt vom Netzteil und SAFE oder ARMED. Fehlt ein Ziel-Pin, erscheint die Zielangabe in der Warnfarbe.

| Zeile | Bereich | Vorgabe |
|---|---|---|
| ZIEL | Gas-Slots und DShot-Ports | der erste |
| VERSORGUNG | SWITCHED, BY HAND | SWITCHED |
| SPANNUNG | 3,00V, oder die untere Grenze von SUPPLY, falls höher, bis zur oberen Grenze von SUPPLY für diese Platine und diese Einstellung | Sollwert von SUPPLY |
| STROMBEGRENZUNG | 0,05A, oder die untere Grenze von SUPPLY, falls höher, bis zur oberen Grenze von SUPPLY für diese Platine und diese Einstellung | Sollwert von SUPPLY |
| EINSTIEGSZEIT | 0,5s bis 120s | 3,0s |
| AUS-ZEIT | 0s bis 10s | 1,0s |

Die Sitzung hat keine Vorgabespannung. SPANNUNG und STROMBEGRENZUNG folgen SUPPLY, bis du auf eine Zahl tippst und einen anderen Wert eingibst. VERSORGUNG behält die letzte Wahl auch nach einem Neustart des Panels im nichtflüchtigen Speicher (NVS-Schlüssel `escw_by_hand`). Ohne gespeicherten Wert beginnt sie mit SWITCHED. SWITCHED nutzt den PD mini. BY HAND legt die Leitung auf high und wartet höchstens 60s auf `VERSORGUNG EIN`.

Nimm den Propeller ab. Schließe nur Signal und Masse an: Ein BEC (Battery Eliminator Circuit) auf der Mittelader des ESC würde sonst die Versorgungsschiene des Prüfstands speisen. Stecke Servos ab. Halte die rote Taste VERBINDEN 2s.

![Verbinden](img/de/motor-wire-connect.png)

![Verbinden, BY HAND](img/de/motor-wire-hand.png)

![Tastenfeld Spannung](img/de/motor-wire-keypad.png)

![Zielliste](img/de/motor-wire-target.png)

![Kein Ziel](img/de/motor-wire-notarget.png)

![Eintritt](img/de/motor-wire-entering.png)

![Wartet auf Strom](img/de/motor-wire-handwait.png)

## Die Einstellungen

IDENTITÄT zeigt Firmware, Version, Layout oder Revision, Signatur oder Name sowie `SCHREIBBAR` oder `NUR LESEN` mit Begründung. Die anderen Reiter enthalten die Einstellungsfelder. Tippe auf eine Zahl, um das Tastenfeld zu öffnen, oder auf eine Auswahl, um ihre Liste zu öffnen. Jeder Editor zeigt die Hilfezeile des Felds. Änderungen betreffen zunächst nur den Entwurf auf dem Panel; geänderte Felder sind am Namen markiert. Erst SCHREIBEN überträgt sie zum ESC.

![AM32 Identität](img/de/motor-wire-am.png)

![AM32 Motor](img/de/motor-wire-am-motor.png)

![AM32 Bremse und Sinus](img/de/motor-wire-am-brake.png)

![AM32 Eingang](img/de/motor-wire-am-input.png)

![AM32 Schutz](img/de/motor-wire-am-protect.png)

![Eine Auswahl](img/de/motor-wire-am-choice.png)

![Eine Zahl](img/de/motor-wire-am-number.png)

![BLHeli_S](img/de/motor-wire-s.png)

![Bluejay](img/de/motor-wire-bj.png)

![BLHeli_S Identität](img/de/motor-wire-s-id.png)

![BLHeli_S Eingang und Ton](img/de/motor-wire-s-2.png)

![Bluejay Identität](img/de/motor-wire-bj-id.png)

![Bluejay Eingang und Ton](img/de/motor-wire-bj-2.png)

![ESCape32 Identität](img/de/motor-wire-e32.png)

![ESCape32 Motor](img/de/motor-wire-e32-1.png)

![ESCape32 Duty](img/de/motor-wire-e32-2.png)

![ESCape32 Gas](img/de/motor-wire-e32-3.png)

![ESCape32 Eingang und Ton](img/de/motor-wire-e32-4.png)

![ESCape32 Schutz](img/de/motor-wire-e32-5.png)

![Nur lesen, Identität](img/de/motor-wire-readonly.png)

![Nur lesen, Zeilen](img/de/motor-wire-readonly-rows.png)

![Bytes eines Layouts ohne Feldnamen](img/de/motor-wire-hex.png)

## Schreiben

n WERTE SCHREIBEN öffnet eine Liste der Änderungen. Sicherheitskritische Felder bleiben editierbar. Sie stehen zuerst, mit `PRÜFE` in der Warnfarbe und ihrem neuen Wert. Jede Seite listet 12 Änderungen. Mit ZURÜCK und WEITER blätterst du durch die Liste. Die rote Taste zum Bestätigen durch 2s Halten erscheint, nachdem du die letzte Seite gesehen hast. ESCape32 erlaubt bis zu 46 Änderungen auf 4 Seiten.

Die Bestätigungsseite hält eine eigene Kopie des Entwurfs. Durch Halten bestätigst du daher genau die aufgelisteten Änderungen. Bis zum Ergebnis des Schreibens nimmt die Sitzung dann nur ABBRECHEN und STOP an. ABBRECHEN während eines Kartenzugriffs bricht den Schreibvorgang ab; der ESC bleibt unverändert. SCHREIBEN wartet, solange eine Datei von der Karte gelesen wird. Trifft die Datei bei offener Bestätigungsseite ein, wird sie nicht geladen. Die Seite meldet dies.

1. Der erste Schreibvorgang einer Sitzung speichert ihr Backup unter `BENCH/ESCW/CONF`, als `ESCW001-AM32.TXT` bis `ESCW999-AM32.TXT` (siehe [Namen der Backups](#namen-der-backups)). Fehlt die Karte oder schlägt das Speichern fehl, fragt die Seite nach einer Bestätigung. Durch erneutes Halten für 2s setzt du den Vorgang ohne Backup fort.
2. Der Vermerk `BENCH/ESCW/CONF/ESCW.TXT` enthält die geänderten Felder und die aus dem ESC ausgelesenen Einstellungen. Schlägt dieses Speichern fehl, fragt die Seite nach derselben Bestätigung.
3. Die Sitzung liest erneut aus, schreibt, liest zurück und meldet das Ergebnis in Worten. Ein Schreibvorgang auf BLHeli_S oder Bluejay dauert etwa 3848ms.

Eine gelöschte Einstellungsseite kann nicht als Backup dienen. Die Sitzung verwendet die ersten ausgelesenen, nicht gelöschten Einstellungen. Nach dem Zurückspielen eines Backups sind dies die anschließend ausgelesenen Werte. Enthält eine `music`-Zeile ein Byte außerhalb 0x20 bis 0x7E, wird keine Datei erstellt. Die Sitzung schreibt nichts und nennt den Grund.

![Schreiben](img/de/motor-wire-am-write.png)

![Erste Seite einer langen Liste](img/de/motor-wire-am-long.png)

![Letzte Seite einer langen Liste](img/de/motor-wire-am-last.png)

![Die Karte nimmt das Backup](img/de/motor-wire-am-card.png)

![Kein Backup auf der Karte](img/de/motor-wire-no-backup.png)

![Dateien auf der Karte](img/de/motor-wire-files.png)

![Schreibt](img/de/motor-wire-am-running.png)

DATEIEN zeigt die Dateien auf der Karte, die neueste zuerst. Unter BACKUP DER SITZUNG SPEICHERN und DATEI LÖSCHEN stehen 11 Dateien je Seite. Hier kannst du das Backup der Sitzung auch von Hand speichern. Das Laden einer Datei ändert nur die editierbaren Felder des Entwurfs und setzt dieselbe Firmware und dasselbe Layout voraus. Eine im Editor des Panels geänderte Datei hat `edited` als letzte Zeile statt der Prüfsumme; die ESC-Programmierung liest ihre Feldzeilen.

### Namen der Backups

Der Name eines Backups ist `ESCW`, eine Nummer von 001 bis 999 und die Firmware: `ESCW001-AM32.TXT`, `ESCW002-BLHELI_S.TXT`, `ESCW003-BLUEJAY.TXT`, `ESCW004-ESCAPE32.TXT`. Ein von Hand gespeichertes Backup verwendet dasselbe Namensschema. Seine Nummer liegt um eins über der höchsten im Ordner, unabhängig vom Firmware-Zusatz im Dateinamen. So wird keine Nummer zweimal vergeben, und die neueste Datei steht zuerst. Ist die Nummer 999 belegt, nimmt die Sitzung die niedrigste freie Nummer; sind alle 999 belegt, sagt das Speichern, dass die Karte voll ist. Dateien mit Namen wie `ESCW001.TXT` und unter anderen Namen hineinkopierte Dateien werden genauso gelistet und geladen. `ESCW.TXT` ist der Vermerk zum Schreibvorgang, kein Backup.

### Eine Datei löschen

DATEI LÖSCHEN schaltet die Liste in den Löschmodus. Ein Tipp auf eine Datei öffnet die Bestätigung mit ihrem Namen. Halte die rote Taste 2s, um sie zu löschen. Anschließend wird die Liste erneut von der Karte gelesen. Backups, die diese Sitzung seit VERBINDEN gespeichert hat, lassen sich hier nicht löschen; die Seite erklärt dies. Verwende dafür LOGS > FILES. Schlägt das Löschen fehl, bleibt die Datei unverändert, und die Seite meldet den Fehler. Gelöscht wird nur auf der Karte, von der die Liste stammt. Hast du sie inzwischen gewechselt, wird nichts gelöscht, und die Seite nennt den Grund.

## Gelöschter AM32

Liefert ein AM32 beim Auslesen seiner Einstellungen 0xFF, bietet er nur BACKUP ZURÜCKSPIELEN an und speichert kein Backup. Eine Datei wird nur angenommen, wenn ihre CRC-32-Prüfsumme gültig ist und Pin-Code, Flash-Code, Bootloader-Protokoll und Name mit denen des ESC übereinstimmen. Halte die rote Taste 2s, um das Backup zurückzuspielen.

![Gelöschter AM32](img/de/motor-wire-erased.png)

![Backup zurückspielen](img/de/motor-wire-restore.png)

## Verlassen

ZURÜCK öffnet die Bestätigung VERLASSEN. Halte die Taste 2s: Der ESC verlässt seinen Bootloader, die Leitung geht auf low, das Netzteil wird ausgeschaltet, und der Pin wird wieder seinem Ausgang zugeordnet. Nicht geschriebene Änderungen gehen verloren.

Bei einem gelöschten AM32 weist die Bestätigungsseite darauf hin, dass der ESC bis BACKUP ZURÜCKSPIELEN gelöscht bleibt, der Pin zurückgegeben und das Netzteil ausgeschaltet wird. Das Halten sendet CLOSE. Bei BY HAND fordert die Seite dich auf, die Stromversorgung des ESC zu trennen.

ESCape32 hat keinen LEAVE-Befehl über die Signalleitung. Die Sitzung endet durch Aus- und Einschalten der Stromversorgung. SWITCHED parkt die Leitung und schaltet das Netzteil aus. BY HAND parkt die Leitung und fordert dich auf, die Stromversorgung des ESC zu trennen.

![Verlassen](img/de/motor-wire-am-leave.png)

![Verlassen auf ESCape32](img/de/motor-wire-e32-leave.png)

![Verlassen auf einem gelöschten AM32](img/de/motor-wire-erased-leave.png)

## Stops und andere Schreiber

STOP im Band stoppt die Sitzung; ein laufender Schreibvorgang wird jedoch erst nach dem Zurücklesen beendet. ABBRECHEN auf der Seite wirkt sofort beim Drücken, auch während eines Schreibvorgangs. Bei einem Stopp oder beim Verlust der Stromversorgung oder der Verbindung werden offene Editoren und Bestätigungsseiten geschlossen. Die Anzeige meldet, was geschehen ist.

Nach einem Stopp oder Fehler bietet die Seite STOP LÖSCHEN und PIN ZURÜCK an. STOP LÖSCHEN hebt den Stopp auf, behält aber den Pin. Ist der STOP des Panels eingerastet, erscheint nur PIN ZURÜCK mit `STOP ist eingerastet: Halte STOP 2s, um ihn zurückzusetzen.` STOP LÖSCHEN benötigt den Heartbeat, den der eingerastete STOP unterbindet. Nach dem Zurücksetzen von STOP erscheint STOP LÖSCHEN wieder.

Nach einem Stopp oder Fehler bleibt SUPPLY OFF auf der Seite SUPPLY eingerastet, bis der Pin freigegeben ist. Solange die Sitzung den Pin hält, kann das Netzteil daher nicht wieder einschalten. Die Seite PIN GEHALTEN bietet dann nur PIN ZURÜCK an, mit `Ein Stopp oder Fehler beendete die Sitzung: PIN ZURÜCK, dann VERBINDEN.` ENTER wird in diesem Zustand mit `SUPPLY_OFF` abgelehnt, bevor sich etwas bewegt. Eine Ablehnung mit SUPPLY OFF unter PIN GEHALTEN ohne Stopp oder Fehler zeigt nur die Ablehnungsmeldung.

Ein gehaltener Pin im Zustand `OPEN`, ohne Stopp und ohne laufenden Vorgang, hat eine eigene Seite. Sie erscheint nach ABBRECHEN, nach STOP LÖSCHEN oder nach einem abgelehnten ENTER. Der Ablehnungsgrund steht dort, etwa ein fehlender PD mini oder eine Spannung zu nah an der Quellspannung. VERSORGUNG, SPANNUNG, STROMBEGRENZUNG, EINSTIEGSZEIT und AUS-ZEIT sind nur lesbar. Die Seite bietet NEU STARTEN mit rotem 2s-Halten und PIN ZURÜCK an. Nach einem Stopp oder Fehler, auch nach STOP LÖSCHEN, steht jedoch nur PIN ZURÜCK zur Verfügung.

NEU STARTEN sendet die aktuellen Werte der Sitzung. Diese folgen SUPPLY bis zu einer eigenen Eingabe; es sind nicht die Werte des letzten VERBINDEN. BY HAND zeigt weder Spannung noch Strombegrenzung. Ändert sich ein Wert während des Haltens, wird das Halten abgebrochen. Nach ABBRECHEN nennt die Meldung dies als Ursache. ZURÜCK nennt die Taste, die den jeweiligen Schritt beendet.

Eine Meldung bleibt während des Schritts, in dem sie entstand, und auf der anschließenden Seite sichtbar. Der nächste Druck auf eine sichtbare Taste löscht sie; ein Druck auf eine leere Stelle tut dies nicht. Erscheint VERBINDEN mit freiem Pin und ohne laufenden Vorgang, werden alle Meldungen gelöscht, außer `LEFT`, dem Verbindungszustand und dem Fehler, der den Vorgang beendet hat. Dieser Fehler verschwindet beim nächsten Tastendruck. Ein Verbindungsverlust schließt in jedem Schritt offene Tastenfelder, Auswahllisten und Bestätigungsseiten. Seine Meldung ersetzt die Meldung zu ABBRECHEN.

Hält ein anderer Programmierzugriff den Pin, etwa die Prüfstandskonsole oder eine Sitzung vor einem Panel-Neustart, bietet die Seite PIN FREIGEBEN mit 2s-Halten an. Sie nennt den GPIO und den Slot oder Port des Pins. ARM wird abgelehnt, solange ein Pin gehalten wird; die Warnung nennt die ESC-Programmierung.

![Gestoppt](img/de/motor-wire-stopped.png)

![Gestoppt durch den STOP des Panels](img/de/motor-wire-stopped-band.png)

![Pin gehalten](img/de/motor-wire-held.png)

![Pin gehalten nach einem Stop](img/de/motor-wire-held-stop.png)

![Pin gehalten, BY HAND](img/de/motor-wire-held-hand.png)

![Pin kam nicht zurück](img/de/motor-wire-not-back.png)

![Einen Pin eines anderen Schreibers freigeben](img/de/motor-wire-release.png)

![Anderer Schreiber](img/de/motor-wire-other.png)
