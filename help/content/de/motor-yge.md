---
screen: none
title: YGE-ESCs
order: 14
---

# YGE-ESCs

Diese Sitzung liest und schreibt die Parametertabelle eines YGE-ESC (Electronic Speed Controller) über sein Telemetriekabel, auf dem Pin eines Programmier-Ports. Öffne sie unter MOTOR & ESC > PROGRAMMIEREN, Zeile `YGE-ESCs`. Die Zeile bleibt abgeblendet, bis der IOmcu die erforderliche Fähigkeit meldet.

Führe vor der ersten Sitzung die Mode-Einstellung aus (`ESCs per Gasknüppel`): Sie setzt alle Werte.

## Vor VERBINDEN

1. Nimm den Propeller ab.
2. Schließe nur Telemetriesignal und Masse an. Zieh das Gaskabel ab: Seine mittlere Ader (BEC, Battery Eliminator Circuit) bleibt vom Prüfstand getrennt.
3. Prüfe `PORT`, `VERSORGUNG`, `SPANNUNG`, `STROMBEGRENZUNG` und `BOOT-WARTEZEIT` (1.0s bis 20.0s, Vorgabe 5.0s). `SPANNUNG` und `STROMBEGRENZUNG` folgen SUPPLY, bis du andere Werte eingibst.
4. Halte `2s HALTEN ZUM VERBINDEN` 2s lang.

YGE erlaubt nur Akkus als Versorgung des ESC. Mit `BY HAND` schließt du den Akku des ESC an, wenn danach gefragt wird, und tippst innerhalb von 60s `VERSORGUNG EIN`.

## Einstellungen

Die Tabs sind IDENTITÄT, MODE & BEC, SCHUTZ, START, GOVERNOR & RPM, STICK und RAW. Ein Feld mit dem Namen in der Warnfarbe ist ein CHECK-Feld: Sein neuer Wert steht vor dem Schreiben über der Fußleiste. Abgeblendete Zeilen sind nur lesbar. Tippe auf eine Auswahl, um ihre Liste zu öffnen, oder auf eine Zahl für das Tastenfeld. Ein Wert außerhalb der schreibbaren Menge wird abgelehnt. Änderungen bleiben auf dem Panel, bis du sie schreibst.

`NEU AUSLESEN` liest die Tabelle erneut. Mit ungeschriebenen Änderungen fragt es zuerst: 2s halten liest, `ZURÜCK` behält die Änderungen.

## Schreiben

`n WERTE SCHREIBEN` ist ein rotes 2s-Halten. Es ist grau, solange nichts geändert ist. Das erste Schreiben einer Sitzung speichert zuerst ein Backup `YGE001-<Typ>` in `BENCH/YGE/CONF`. Ohne Karte geht das Schreiben, und die alten Werte bleiben nur im RAM. Nach dem Schreiben wird die Tabelle zurückgelesen und verglichen.

## Dateien

`DATEIEN` listet die Backups und Dateien in `BENCH/YGE/CONF`. Ein Tipp lädt die Werte einer Datei in den Entwurf. Eine Datei eines anderen ESC-Typs wird abgelehnt.

## Verlassen

`VERLASSEN` ohne Änderungen beendet die Sitzung sofort. Mit ungeschriebenen Änderungen listet eine Seite sie auf: 2s halten verlässt ohne Schreiben, `ZURÜCK` bleibt. Nach einem Stopp werden nur `PIN ZURÜCK` und, beim eigenen Stopp der Seite, `STOP LÖSCHEN` angeboten. Mit `BY HAND` trennst du den Akku des ESC selbst.
