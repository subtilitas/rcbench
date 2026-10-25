---
screen: BUSFAULT
title: Busfehler
order: 110
---

# Busfehler

Der Busfehler-Bildschirm sagt, warum Panel und IOmcu nicht über den CAN-Bus (Controller Area Network) miteinander reden. Er öffnet von selbst, in zwei Fällen: Der CAN-Selbsttest beim Start schlägt fehl, oder ein Link, der stand, ist seit 4s weg. Er belegt den ganzen Bildschirm: kein Band und kein STOP. Hinter ihm kann der Prüfstand nicht auf ARMED schalten.

## Der Selbsttest beim Start

![Testframes kommen verändert zurück](view:busfault)

Das Panel führt bei jedem Start einen CAN-Echo-Selbsttest aus, 1200ms lang innerhalb des Splash. Jedes Urteil außer dem, dass alle Testframes unversehrt zurückkamen, öffnet diesen Bildschirm statt des Menüs.

- Die Überschrift ist das Urteil.
- WAS DAS BEDEUTET sagt es in einer Zeile.
- IN DIESER REIHENFOLGE PRÜFEN listet, was zu prüfen ist, die billigste Prüfung zuerst.
- Die rechte Spalte zeigt, was beide Enden gezählt haben: gesendet, zurück, verändert, verloren.

![Nichts kam zurück](view:busfault-silent)

| Überschrift | WAS DAS BEDEUTET | Die Prüfungen zielen auf |
|---|---|---|
| `Kein Testframe kam zurück.` | `Keine Antwort am Bus. Die beiden Platinen können keine Daten austauschen.` | Versorgung, vertauschte CANH und CANL, das Release, die Abschlüsse |
| `Testframes kamen mit veränderten Daten zurück.` | `Frames kommen mit veränderten Daten an. Verkabelung und Bit-Timing prüfen.` | die Abschlüsse, die Länge der Stichleitung, verdrillte CANH und CANL |
| `Einige Testframes gingen verloren.` | `Einige Frames gehen verloren. Der Bus meldet Fehler.` | die Abschlüsse, die Buslänge, verdrillte CANH und CANL, einen Stecker |
| `Testframes gingen ohne Busfehler verloren.` | `Frames kamen korrekt an, wurden aber ungelesen verworfen. Kein Verkabelungsfehler.` | nicht die Leitung; mit den Zahlen rechts melden |

Die Abschlüsse messen bei ausgeschaltetem Prüfstand etwa 60Ω zwischen CANH und CANL. 120Ω heißt, einer der beiden fehlt. Der ganze Bus bleibt unter 5m, jede Stichleitung unter 30cm.

## Ein Link, der abbricht

![Panel vom Bus getrennt.](view:busfault-lost)

Derselbe Bildschirm öffnet, wenn ein Link, der stand, seit 4s weg ist. Die Überschrift sagt dann, was der eigene CAN-Controller des Panels tut:

| Überschrift | Bedeutung |
|---|---|
| `Panel vom Bus getrennt.` | zu viele Frames blieben unbestätigt; der Controller sendet nicht mehr |
| `Panel verbindet sich neu.` | er zählt die Ruhezeit, die ein Neuverbinden braucht; 3s warten |
| `CAN-Controller des Panels ist gestoppt.` | er steht und wurde nicht neu gestartet: ein Fehler in der Firmware des Panels |
| `Verbindung antwortet nicht mehr.` | der Controller ist am Bus, und nichts antwortet |
| `Controller-Zustand nicht auslesbar.` | der Treiber läuft nicht; nichts kann gesendet werden |

Bei ARMED öffnet der Bildschirm nie: Er hat kein STOP. Bei ARMED meldet das Alert-Band, dass der Link weg ist, und der Bildschirm wartet auf das Schalten auf SAFE.

Dieselben Zahlen gehen nach `BENCH/RCBENCH.LOG` auf der SD-Karte (Secure Digital), eine Zeile je Bericht, solange der Link weg ist. Die Datei hält höchstens 1MiB (1.048.576 Bytes): Eine Zeile, die sie darüber brächte, benennt sie in `BENCH/RCBENCH.OLD` um, ersetzt die dort liegende und beginnt eine neue `BENCH/RCBENCH.LOG`. Die Zeilen werden unabhängig von Auto-Logs unter [SETUP](setup.md#sd-card) > SD CARD geschrieben.

## Den Bildschirm verlassen

2s HALTEN ZUM BESTÄTIGEN schließt den Bildschirm, mit der Geste und dem Übergang von ARM. Das Quittieren repariert nichts. Der Prüfstand läuft danach in der Simulation, nichts steuert einen Ausgang an, und der Test läuft beim nächsten Start erneut.

## Verwandte Seiten

- [Menü](overview.md): der Splash und der Selbsttest.
- [Statusband und STOP](band.md): VERBINDUNG und KEINE VERBINDUNG.
- [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/blob/main/docs/Link-de.md): die Urteile im Detail.
