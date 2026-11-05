---
screen: BUSFAULT
title: Busfehler
order: 110
---

# Busfehler

Die Busfehler-Seite erklärt, warum Panel und IOmcu nicht über den CAN-Bus (Controller Area Network) kommunizieren können. Sie öffnet sich automatisch, wenn der CAN-Selbsttest beim Start fehlschlägt oder eine bestehende Verbindung seit 4s unterbrochen ist. Sie belegt die gesamte Anzeige, ohne Statusband und ohne STOP. Solange die Seite geöffnet ist, kann der Prüfstand nicht auf ARMED schalten.

## Der Selbsttest beim Start

![Testframes kommen verändert zurück](view:busfault)

Das Panel führt bei jedem Start während des Splash einen CAN-Echo-Selbsttest über 1200ms aus. Kommen nicht alle Testframes unversehrt zurück, öffnet sich diese Seite statt des Menüs.

- Die Überschrift nennt das Urteil.
- WAS DAS BEDEUTET erklärt es in einer Zeile.
- PRÜFREIHENFOLGE listet die Prüfungen auf, beginnend mit dem geringsten Aufwand.
- Die rechte Spalte zeigt die Zähler an beiden Enden: gesendet, zurück, verändert und verloren.

![Nichts kam zurück](view:busfault-silent)

| Überschrift | WAS DAS BEDEUTET | Die Prüfungen zielen auf |
|---|---|---|
| `Kein Testframe kam zurück.` | `Keine Antwort am Bus. Die beiden Platinen können keine Daten austauschen.` | Versorgung, vertauschte CANH und CANL, das Release, die Abschlüsse |
| `Testframes kamen mit veränderten Daten zurück.` | `Frames kommen mit veränderten Daten an. Prüfe Verkabelung und Bit-Timing.` | die Abschlüsse, die Länge der Stichleitung, verdrillte CANH und CANL |
| `Einige Testframes gingen verloren.` | `Einige Frames gehen verloren. Der Bus meldet Fehler.` | die Abschlüsse, die Buslänge, verdrillte CANH und CANL, einen Stecker |
| `Testframes gingen ohne Busfehler verloren.` | `Frames kamen korrekt an, wurden aber ungelesen verworfen. Kein Verkabelungsfehler.` | nicht die Leitung; mit den Zahlen rechts melden |

Miss bei ausgeschaltetem Prüfstand etwa 60Ω zwischen CANH und CANL. Ein Messwert von 120Ω bedeutet, dass einer der beiden Abschlüsse fehlt. Halte die gesamte Buslänge unter 5m und jede Stichleitung unter 30cm.

## Ein Link, der abbricht

![Das Panel ist vom Bus getrennt](view:busfault-lost)

Ist eine bestehende Verbindung seit 4s unterbrochen, öffnet sich dieselbe Seite. Die Überschrift beschreibt dann den Zustand des CAN-Controllers im Panel:

| Überschrift | Bedeutung |
|---|---|
| `Panel vom Bus getrennt.` | Zu viele Frames blieben unbestätigt. Der Controller hat das Senden eingestellt. |
| `Panel verbindet sich neu.` | Der Controller wartet die für die Rückkehr zum Bus nötige Ruhezeit ab. Warte 3s. |
| `CAN-Controller des Panels ist gestoppt.` | Der Controller ist gestoppt und wurde nicht neu gestartet. Dies ist ein Fehler in der Panel-Firmware. |
| `Verbindung antwortet nicht mehr.` | Der Controller ist am Bus, erhält aber keine Antwort. |
| `Controller-Zustand nicht auslesbar.` | Der Treiber läuft nicht; Senden ist nicht möglich. |

Die Seite öffnet sich nicht, solange der Prüfstand ARMED ist, da sie kein STOP-Bedienelement hat. Stattdessen meldet das Alarmband die verlorene Verbindung. Die Busfehler-Seite wartet, bis der Prüfstand SAFE ist.

Dieselben Zählerstände werden auf der SD-Karte (Secure Digital) in `BENCH/RCBENCH.LOG` geschrieben: eine Zeile je Bericht, solange die Verbindung unterbrochen ist. Die Datei ist auf 1MiB (1.048.576 Bytes) begrenzt. Bevor eine Zeile diese Grenze überschreiten würde, benennt die Aufzeichnung die Datei in `BENCH/RCBENCH.OLD` um und ersetzt eine vorhandene Datei dieses Namens. Anschließend beginnt eine neue `BENCH/RCBENCH.LOG`. Diese Zeilen werden unabhängig von der Einstellung Auto-Aufzeichnungen unter [SETUP](setup.md#sd-card) > SD CARD geschrieben.

## Eine Verbindung, die wieder antwortet

![Die Verbindung antwortet wieder](view:busfault-back)

Die Zähler rechts folgen der Verbindung, solange die Seite sichtbar ist. Hat der IOmcu 1s lang jede Anfrage beantwortet, wird der Balken grün und zeigt LINK WIEDER DA, und `verbunden` zählt die Sekunden seit der ersten Antwort. Die Seite des Starttests verhält sich ebenso und behält rechts die Zähler des Tests.

- 2s HALTEN ZUM NEUVERBINDEN ersetzt 2s HALTEN ZUM BESTÄTIGEN. Es öffnet das Menü am echten Prüfstand, SAFE.
- Eine einzige unbeantwortete Anfrage nimmt NEUVERBINDEN sofort zurück. Die nächste 1s Antworten bietet es wieder an.
- NEUVERBINDEN löscht nichts. Ein bestehender STOP bleibt, ebenso der FEHLER-Chip. FEHLER 01, der Failsafe des IOmcu, erlischt beim nächsten ARM.

## Den Bildschirm verlassen

2s HALTEN ZUM BESTÄTIGEN schließt die Seite mit derselben Haltegeste und demselben Übergang wie ARM. Das Bestätigen behebt den Fehler nicht. Der Prüfstand läuft anschließend in der Simulation und steuert keine Ausgänge an. Beim nächsten Start läuft der Selbsttest erneut. Antwortet die Verbindung wieder, tritt 2s HALTEN ZUM NEUVERBINDEN an ihre Stelle.

Lässt du vor 2s los, zeigt die Seite anstelle des roten Balkens 4s lang bernsteinfarben, was die Taste tut: `Halte 2s, um zu bestätigen.` oder `Halte 2s, um neu zu verbinden.` Ein Tipp auf den Balken schließt den Hinweis.

## Verwandte Seiten

- [Menü](overview.md): der Splash und der Selbsttest.
- [Statusband und STOP](band.md): VERBINDUNG und KEINE VERBINDUNG.
- [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/blob/main/docs/Link-de.md): die Urteile im Detail.
