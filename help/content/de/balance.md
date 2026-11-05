---
screen: BALANCE
title: Wuchten
order: 80
---

# Wuchten

BALANCE zeigt Menge und Position der Ausgleichsmasse für einen Propeller oder Impeller sowie die Anordnung der Sensoren. In diesem Release zeigt die Seite feste Beispielwerte; es ist kein Sensor angeschlossen. Öffne sie über die Kachel BALANCE im [Menü](overview.md). Sie hat drei Reiter: WUCHTEN, PRÜFGESTELL und FLUGMODELL.

## Der Reiter WUCHTEN

![Ein Zweiblattpropeller](view:balance)

Ein Wuchtergebnis besteht aus einem Betrag und einem Winkel. Auf der rechten Karte wählst du den Rotor und siehst das Ergebnis.

| Bedienelement oder Zeile | Funktion |
|---|---|
| PROPELLER, EDF | wählt den Rotortyp; EDF bezeichnet einen Impeller (Electric Ducted Fan) |
| BLÄTTER, `-`, `+` | legt die Blattzahl von 2 bis 6 fest; tippe auf die Zahl, um das Tastenfeld zu öffnen |
| VIBRATION | die Vibration, in g |
| WINKEL | der Winkel der schweren Stelle, in ° |
| HINZUFÜGEN | die hinzuzufügende Masse, in g |
| WO | die Position der Masse: zwischen zwei Blättern oder an einem Winkel auf der Nabe |

VIBRATION, WINKEL und HINZUFÜGEN zeigen feste Werte von 0,42g, 265° und 0,35g. Das sind keine Messungen. Darunter stehen die Markierung SIMULIERT und `Nicht gemessen.`. WO berechnet aus dem Winkel und der eingestellten Blattzahl die Position relativ zu den Blättern.

![Ein Impeller mit fünf Blättern](view:balance-edf)

Bei einem Impeller ist die Korrektur ein Winkel auf der Nabe, `Nabe, 265°`, weil eine Blattspitze im Mantel nicht erreichbar ist.

![Das Tastenfeld der Blattzahl, 5 getippt](view:balance-keypad)

Das Tastenfeld bedeckt beide Karten. OK übernimmt eine Blattzahl von 2 bis 6. Bei anderen Werten bleibt das Tastenfeld offen und zeigt den erlaubten Bereich in der Warnfarbe. ABBRECHEN, das Home-Feld oder ein Reiter schließt es, ohne die Blattzahl zu ändern.

Ohne Indexpuls benötigt das Wuchten vier Läufe: eine Referenzmessung und je einen Lauf mit einer Testmasse bei 0, 120 und 240°. Ein Indexpuls reduziert die Zahl der Läufe auf zwei.

## Der Reiter PRÜFGESTELL

![Wo die Sensoren an einem Testgestell hinkommen](view:balance-rig)

- Verschraube den Beschleunigungssensor quer zur Welle, so nah am Lager wie möglich. Klebe ihn nicht fest.
- Zeichne mit einem Stift eine Indexmarkierung auf die Motorglocke. So entsteht ein Puls pro Umdrehung. Klebeband fügt dem zu wuchtenden Teil Masse hinzu. Markierungen auf den Blättern ergeben einen Puls je Blatt.
- Halte das Sensorkabel aus dem Luftstrom des Propellers.

## Der Reiter FLUGMODELL

![Wo die Sensoren an einem Flugmodell hinkommen](view:balance-aircraft)

- Befestige den Beschleunigungssensor flach am Motorspant, nahe den Schrauben, nicht an der Haube.
- Setze die Indexmarkierung auf die Motorglocke. Ein Spinner kann nach dem Abnehmen in einem anderen Winkel sitzen.
- Binde das Heck fest. Ein Modell, das schaukeln kann, wirkt als Feder und beeinflusst die Messung.

## Verwandte Seiten

- [Sensoren](setup-sensors.md): Die Schlüssel Vibration und Optischer Tacho werden in diesem Release gespeichert, aber nicht ausgewertet. Sie haben keine Einstellungszeile.
- [Wuchten](https://github.com/subtilitas/rcbench/blob/main/docs/Balance-de.md): Sensorwahl und Platzierung im Detail.
