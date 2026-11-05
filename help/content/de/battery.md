---
screen: BATTERY
title: Akku
order: 70
---

# Akku

BATTERY zeigt die Spannungsabweichung jeder Zelle vom Mittelwert des Akkus und bewertet den Akku. Öffne die Seite über die Kachel BATTERY im [Menü](overview.md). Die Kachel trägt in diesem Release die Markierung SIMULIERT.

## Am Prüfstand: kein Akku

![BATTERY ohne Quelle](view:battery-none)

Keine der beiden Platinen liest in diesem Release einen Balancer-Anschluss aus. Die Firmware liefert daher keine Messwerte an diese Seite. Sie zeigt KEIN AKKU, `Kein Akku am Balancer-Anschluss.` und anstelle der Grafik `Kein Messgerät am Balancer-Anschluss.`. In den Zeilen steht `---`.

## Was ein gespeister Bildschirm zeigt

![Zellenabweichung im Host-Renderer](view:battery)

Das Bild stammt aus dem Host-Renderer, der Zellspannungen für die Anzeige vorgibt.

- ZELLENABWEICHUNG zeigt die Spannungsabweichung jeder Zelle vom Mittelwert des Akkus. Ab einem Unterschied von 30mV erscheint die schwächste Zelle rot.
- Die Skala passt sich dem Akku an, mit einer Untergrenze von 12mV, und steht neben der Grafik.
- Unter jedem Balken stehen die Zellennummer und die Spannung.

| Urteil | Zeile | Unterschied |
|---|---|---|
| GUT | `Zellwerte sind unter Last gleichmäßig.` | unter 30mV |
| BEOBACHTEN | `Der Unterschied zwischen den Zellen wächst.` | ab 30mV |
| ERSETZEN | `Eine Zelle weicht von den anderen ab.` | ab 60mV |
| KEIN AKKU | `Kein Akku am Balancer-Anschluss.` | keine Zellen |

Der Unterschied ist der größte Spannungsabstand zwischen zwei Zellen.

| Zeile | Anzeige |
|---|---|
| ZELLEN | die Zellenzahl, `6S` |
| AKKU | die Akkuspannung |
| MITTELWERT | die mittlere Zellspannung |
| UNTERSCHIED | der größte Spannungsunterschied, in der Farbe des Urteils |
| AKKU iR | der Innenwiderstand des Akkus |
| ENTNOMMEN | die entnommene Ladung im Verhältnis zur Kapazität |

Miss unter Last. In Ruhe kann eine schwache Zelle unauffällig wirken.

## Verwandte Seiten

- [Menü](overview.md): die Marke SIMULIERT.
