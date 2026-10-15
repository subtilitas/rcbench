---
screen: BATTERY
title: Akku
order: 70
---

# Akku

BATTERY zeigt, wie weit jede Zelle eines Akkus vom Mittelwert des Akkus abweicht, und gibt ein Urteil über den Akku. Der Bildschirm öffnet über die Kachel BATTERY im [Menü](overview.md). Die Kachel trägt in diesem Release die Marke SIMULIERT.

## Am Prüfstand: kein Akku

![BATTERY ohne Quelle](view:battery-none)

In diesem Release speist keine Firmware diesen Bildschirm: Keine der beiden Platinen liest einen Balancer-Anschluss. Der Bildschirm zeigt daher KEIN AKKU, `nichts am Balancer-Anschluss` und `kein Monitor am Balancer-Anschluss`, wo die Grafik stünde. Die Zeilen zeigen `---`.

## Was ein gespeister Bildschirm zeigt

![Zellenabweichung im Host-Renderer](view:battery)

Das Bild stammt aus dem Host-Renderer, der den Bildschirm mit Zellspannungen speist.

- ZELLENABWEICHUNG zeichnet jede Zelle als ihre Abweichung vom Mittelwert des Akkus. Ab einer Streuung von 30 mV ist die schwächste Zelle rot gezeichnet.
- Die Skala folgt dem Akku bis hinunter zu einer Untergrenze von 12 mV und steht neben der Grafik.
- Unter jedem Balken: die Nummer der Zelle und ihre Spannung.

| Urteil | Zeile | Streuung |
|---|---|---|
| GUT | `Zellen gleich unter Last` | unter 30 mV |
| BEOBACHTEN | `die Streuung wächst` | ab 30 mV |
| ERSETZEN | `eine Zelle weicht ab` | ab 60 mV |
| KEIN AKKU | `nichts am Balancer-Anschluss` | keine Zellen |

Die Streuung ist der größte Abstand zwischen zwei beliebigen Zellen.

| Zeile | Zeigt |
|---|---|
| ZELLEN | die Zellenzahl, `6S` |
| AKKU | die Akkuspannung |
| MITTEL | die mittlere Zellspannung |
| STREUUNG | die Streuung, in der Farbe des Urteils |
| AKKU iR | den Innenwiderstand des Akkus |
| ENTNOMMEN | die entnommene Ladung, von der Kapazität |

Unter Last messen. In Ruhe sieht eine schwache Zelle aus wie die anderen.

## Verwandte Seiten

- [Menü](overview.md): die Marke SIMULIERT.
