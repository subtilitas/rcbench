---
screen: BATTERY
title: Akku
order: 70
---

# Akku

BATTERY zeigt, wie weit jede Zelle eines Akkus vom Mittelwert des Akkus abweicht, und gibt ein Urteil über den Akku. Der Bildschirm öffnet über die Kachel BATTERY im [Menü](overview.md). Die Kachel trägt in diesem Release die Marke SIMULIERT.

## Am Prüfstand: kein Akku

![BATTERY ohne Quelle](view:battery-none)

In diesem Release speist keine Firmware diesen Bildschirm: Keine der beiden Platinen liest einen Balancer-Anschluss. Der Bildschirm zeigt daher KEIN AKKU, `Kein Akku am Balancer-Anschluss.` und `Kein Messgerät am Balancer-Anschluss.`, wo die Grafik stünde. Die Zeilen zeigen `---`.

## Was ein gespeister Bildschirm zeigt

![Zellenabweichung im Host-Renderer](view:battery)

Das Bild stammt aus dem Host-Renderer, der den Bildschirm mit Zellspannungen speist.

- ZELLENABWEICHUNG zeichnet jede Zelle als ihre Abweichung vom Mittelwert des Akkus. Ab einer Streuung von 30mV ist die schwächste Zelle rot gezeichnet.
- Die Skala folgt dem Akku bis hinunter zu einer Untergrenze von 12mV und steht neben der Grafik.
- Unter jedem Balken: die Nummer der Zelle und ihre Spannung.

| Urteil | Zeile | Streuung |
|---|---|---|
| GUT | `Zellwerte sind unter Last gleichmäßig.` | unter 30mV |
| BEOBACHTEN | `Der Unterschied zwischen den Zellen wächst.` | ab 30mV |
| ERSETZEN | `Eine Zelle weicht von den anderen ab.` | ab 60mV |
| KEIN AKKU | `Kein Akku am Balancer-Anschluss.` | keine Zellen |

Die Streuung ist der größte Abstand zwischen zwei beliebigen Zellen.

| Zeile | Zeigt |
|---|---|
| ZELLEN | die Zellenzahl, `6S` |
| AKKU | die Akkuspannung |
| MITTELWERT | die mittlere Zellspannung |
| UNTERSCHIED | die Streuung, in der Farbe des Urteils |
| AKKU iR | den Innenwiderstand des Akkus |
| ENTNOMMEN | die entnommene Ladung, von der Kapazität |

Unter Last messen. In Ruhe sieht eine schwache Zelle aus wie die anderen.

## Verwandte Seiten

- [Menü](overview.md): die Marke SIMULIERT.
