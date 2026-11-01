---
screen: none
title: ESC-Signalleitung
order: 13
---

# ESC-Signalleitung

Die Sitzung liest und schreibt die Einstellungen eines ESC (Electronic Speed Controller) mit AM32, Bluejay, BLHeli_S oder ESCape32 über seine Signalleitung. Öffne sie auf MOTOR & ESC > PROGRAMMIEREN. Die Zeile ist abgeblendet, bis der IOmcu die Fähigkeit meldet.

Kein Frame hat einen echten ESC erreicht. SimonK wird nicht unterstützt.

## Vor dem Verbinden

1. Nimm den Propeller ab.
2. Schließe nur Signal und Masse an, die Mittelader des ESC getrennt. Ein BEC (Battery Eliminator Circuit) speist sonst die Schiene des Bench.
3. Stecke Servos ab.
4. Prüfe `ZIEL`, `STROM`, `SPANNUNG` und `LIMIT`. `SPANNUNG` und `LIMIT` folgen SUPPLY, bis du andere eingibst.
5. Halte `2s HALTEN: VERBINDEN` für 2s.

Mit `BY HAND` schließe den Strom des ESC an, wenn die Seite es sagt, und tippe binnen 60s auf `STROM IST AN`.

## Einstellungen

Tippe auf eine Zahl für das Tastenfeld, oder auf eine Auswahl für ihre Liste. Jeder Editor zeigt eine Zeile Hilfe. Eine Änderung bleibt auf dem Panel bis `n WERTE SCHREIBEN`.

`NUR LESEN` nennt seinen Grund. Die Zeilen sind abgeblendet, und es gibt kein Schreiben.

## Schreiben

Die Seite listet die Änderungen, jedes heikle Feld zuerst, mit `PRÜFE` und seinem neuen Wert. Halte 2s. Das erste Schreiben einer Sitzung speichert erst das Backup. Ohne Karte fragt die Seite.

Ein Schreiben auf BLHeli_S oder Bluejay dauert etwa 3848ms. Lass den Strom an.

## Dateien

`DATEIEN` listet `ESCW001` bis `ESCW999` auf der Karte. Eine Datei geht in den Entwurf, für dieselbe Firmware und dasselbe Layout. Ein AM32, der sich gelöscht liest, bietet `AUS BACKUP HERSTELLEN` und sonst nichts.

## Verlassen

`ZURÜCK` öffnet die Seite zum Verlassen: halte 2s. ESCape32 hat kein LEAVE: ein Power-Cycle beendet die Sitzung, und mit `BY HAND` trennst du seinen Strom.
