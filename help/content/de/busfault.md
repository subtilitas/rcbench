---
screen: BUSFAULT
title: Busfehler
order: 110
---

# Busfehler

Der Busfehler-Bildschirm sagt, warum Panel und Koprozessor nicht über den CAN-Bus (Controller Area Network) miteinander reden. Er öffnet von selbst, in zwei Fällen: Der CAN-Selbsttest beim Start schlägt fehl, oder ein Link, der stand, ist seit 4 s weg. Er belegt den ganzen Bildschirm: kein Band und kein STOP. Hinter ihm kann der Prüfstand nicht auf ARMED schalten.

## Der Selbsttest beim Start

![Testframes kommen verändert zurück](view:busfault)

Das Panel führt bei jedem Start einen CAN-Echo-Selbsttest aus, 1200 ms lang innerhalb des Splash. Jedes Urteil außer dem, dass alle Testframes unversehrt zurückkamen, öffnet diesen Bildschirm statt des Menüs.

- Die Überschrift ist das Urteil.
- WAS DAS BEDEUTET sagt es in einer Zeile.
- PRÜFEN, IN DIESER REIHENFOLGE listet, was zu prüfen ist, die billigste Prüfung zuerst.
- Die rechte Spalte zeigt, was beide Enden gezählt haben: gesendet, zurück, verändert, verloren.

![Nichts kam zurück](view:busfault-silent)

| Überschrift | WAS DAS BEDEUTET | Die Prüfungen zielen auf |
|---|---|---|
| `kein Testframe kam zurück` | `Am Bus hat niemand geantwortet. Die Platinen reden nicht.` | Versorgung, vertauschte CANH und CANL, das Release, die Abschlüsse |
| `Testframes verändert zurück` | `Frames kommen verändert an. Leitung oder Bit-Timing.` | die Abschlüsse, die Länge der Stichleitung, verdrillte CANH und CANL |
| `Testframes gehen verloren` | `Frames gehen teils verloren, der Bus meldet Fehler.` | die Abschlüsse, die Buslänge, verdrillte CANH und CANL, einen Stecker |
| `Testframes weg ohne Busfehler` | `Frames kamen intakt, blieben ungelesen. Nicht die Leitung.` | nicht die Leitung; mit den Zahlen rechts melden |

Die Abschlüsse messen bei ausgeschaltetem Prüfstand etwa 60 Ohm zwischen CANH und CANL. 120 Ohm heißt, einer der beiden fehlt. Der ganze Bus bleibt unter 5 m, jede Stichleitung unter 30 cm.

## Ein Link, der abbricht

![Das Panel ist vom Bus getrennt](view:busfault-lost)

Derselbe Bildschirm öffnet, wenn ein Link, der stand, seit 4 s weg ist. Die Überschrift sagt dann, was der eigene CAN-Controller des Panels tut:

| Überschrift | Bedeutung |
|---|---|
| `Panel ist vom Bus getrennt` | zu viele Frames blieben unbestätigt; der Controller sendet nicht mehr |
| `Panel verbindet sich neu` | er zählt die Ruhezeit, die ein Neuverbinden braucht; 3 s warten |
| `Controller des Panels steht` | er steht und wurde nicht neu gestartet: ein Fehler in der Firmware des Panels |
| `Link antwortet nicht mehr` | der Controller ist am Bus, und nichts antwortet |
| `Controller nicht lesbar` | der Treiber läuft nicht; nichts kann gesendet werden |

Bei ARMED öffnet der Bildschirm nie: Er hat kein STOP. Bei ARMED meldet das Alert-Band, dass der Link weg ist, und der Bildschirm wartet auf das Schalten auf SAFE.

Dieselben Zahlen gehen nach `RCBENCH.LOG` auf der SD-Karte (Secure Digital), eine Zeile je Bericht, solange der Link weg ist.

## Den Bildschirm verlassen

2 s HALTEN ZUM QUITTIEREN schließt den Bildschirm, mit der Geste und dem Übergang von ARM. Das Quittieren repariert nichts. Der Prüfstand läuft danach in der Simulation, nichts steuert einen Ausgang an, und der Test läuft beim nächsten Start erneut.

## Verwandte Seiten

- [Menü](overview.md): der Splash und der Selbsttest.
- [Statusband und STOP](band.md): LINK und KEIN LINK.
- [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/blob/main/docs/Link-de.md): die Urteile im Detail.
