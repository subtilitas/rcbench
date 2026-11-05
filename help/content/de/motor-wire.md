---
screen: none
title: ESC-Signalleitung
order: 13
---

# ESC-Signalleitung

Diese Sitzung liest und schreibt ESC-Einstellungen (Electronic Speed Controller) über die Signalleitung. Unterstützt werden AM32, Bluejay, BLHeli_S und ESCape32. Öffne sie unter MOTOR & ESC > PROGRAMMIEREN. Die Zeile bleibt abgeblendet, bis der IOmcu die erforderliche Fähigkeit meldet.

SimonK wird nicht unterstützt.

## Vor dem Verbinden

1. Nimm den Propeller ab.
2. Schließe nur Signal und Masse an. Lass die Mittelader des ESC getrennt: Ein BEC (Battery Eliminator Circuit) würde die Versorgungsschiene des Prüfstands speisen.
3. Stecke die Servos ab.
4. Prüfe `ZIEL`, `VERSORGUNG`, `SPANNUNG` und `STROMBEGRENZUNG`. `SPANNUNG` und `STROMBEGRENZUNG` folgen SUPPLY, bis du andere Werte eingibst.
5. Halte `2s HALTEN: VERBINDEN` für 2s.

Schließe bei `BY HAND` die ESC-Versorgung an, sobald du dazu aufgefordert wirst. Tippe innerhalb von 60s auf `VERSORGUNG EIN`.

## Einstellungen

Tippe auf eine Zahl, um das Tastenfeld zu öffnen, oder auf eine Auswahl, um ihre Liste zu öffnen. Jeder Editor zeigt eine Hilfzeile. Änderungen bleiben auf dem Panel, bis du `n WERTE SCHREIBEN` verwendest.

`NUR LESEN` erklärt, warum Schreiben nicht verfügbar ist. Die Zeilen sind abgeblendet.

## Schreiben

Die Schreibseite listet die Änderungen auf. Sicherheitskritische Felder stehen zuerst, mit `PRÜFE` neben dem neuen Wert. Eine lange Liste blätterst du mit `WEITER` und `ZURÜCK` durch. Erst nach Anzeige der letzten Seite wird die Haltefunktion verfügbar. Halte 2s. Bis zum Ende des Schreibvorgangs sind anschließend nur `ABBRECHEN` und `STOP` aktiv. Vor dem ersten Schreiben einer Sitzung wird ein Backup gespeichert; eine gelöschte Seite kann nicht als Backup dienen. Ohne Karte erscheint eine Rückfrage.

Ein Schreibvorgang auf BLHeli_S oder Bluejay dauert etwa 3848ms. Lass die Versorgung eingeschaltet.

## Dateien

`DATEIEN` listet `ESCW001` bis `ESCW999` auf der Karte. Beim Laden werden die Werte in den Entwurf übernommen; Firmware und Layout müssen übereinstimmen. Wird ein AM32 als gelöscht ausgelesen, ist nur `BACKUP ZURÜCKSPIELEN` verfügbar.

## Verlassen

Nach einem Stopp oder Fehler bietet die gehaltene Sitzung nur `PIN ZURÜCK` an. Erst danach ist `VERBINDEN` verfügbar. `ZURÜCK` öffnet die Seite zum Verlassen; halte dort 2s. Ein gelöschter AM32 bleibt bis zu `BACKUP ZURÜCKSPIELEN` gelöscht. Beim Verlassen wird der Pin freigegeben und das Netzteil ausgeschaltet. ESCape32 hat keinen LEAVE-Befehl: Aus- und Einschalten beendet die Sitzung. Trenne bei `BY HAND` die ESC-Versorgung selbst.
