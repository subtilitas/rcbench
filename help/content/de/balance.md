---
screen: BALANCE
title: Wuchten
order: 80
---

# Wuchten

BALANCE findet, wo ein Propeller oder Impeller Masse braucht, und zeigt, wo die beiden Sensoren hinkommen. Der Bildschirm öffnet über die Kachel BALANCE im [Menü](overview.md). Drei Reiter: WUCHTEN, PRÜFGESTELL und FLUGMODELL.

## Der Reiter WUCHTEN

![Ein Zweiblattpropeller](view:balance)

Ein Wuchtergebnis ist ein Betrag und ein Winkel. Die rechte Karte stellt den Rotor ein und zeigt das Ergebnis.

| Bedienelement oder Zeile | Was es tut |
|---|---|
| PROPELLER, EDF | der Rotortyp; EDF ist ein Impeller (Electric Ducted Fan) |
| BLÄTTER, `-`, `+` | die Blattzahl, 2 bis 6; ein Tippen auf die Zahl öffnet das Tastenfeld |
| VIBRATION | die Vibration, in g |
| WINKEL | der Winkel der schweren Stelle, in deg |
| HINZUFÜGEN | die zuzufügende Masse, in g |
| WO | wo die Masse hinkommt: zwischen zwei Blätter oder auf die Nabe unter einem Winkel |

In diesem Release ist kein Sensor angeschlossen. VIBRATION, WINKEL und HINZUFÜGEN sind feste Werte, 0,42 g, 265 deg und 0,35 g, keine Messung. Eine Marke SIMULIERT und `Nicht gemessen.` stehen darunter. Die Blattrechnung funktioniert: WO setzt den Winkel für die eingestellte Blattzahl in einen Bezug auf die Blätter um.

![Ein Impeller mit fünf Blättern](view:balance-edf)

Bei einem Impeller ist die Korrektur ein Winkel auf der Nabe, `Nabe, 265 Grad`, weil eine Blattspitze im Mantel nicht erreichbar ist.

![Das Tastenfeld der Blattzahl, 5 getippt](view:balance-keypad)

Das Tastenfeld bedeckt beide Karten. OK nimmt 2 bis 6; jede andere Zahl lässt es offen und nennt den Bereich in der Warnfarbe. ABBRECHEN, das Home-Tag und ein Reiter schließen es und ändern nichts.

Ohne Indexpuls braucht das Wuchten vier Läufe: eine Referenz, dann je eine Testmasse bei 0, 120 und 240 deg. Ein Indexpuls halbiert das.

## Der Reiter PRÜFGESTELL

![Wo die Sensoren an einem Testgestell hinkommen](view:balance-rig)

- Der Beschleunigungssensor kommt quer zur Welle, so nah am Lager wie möglich, verschraubt statt geklebt.
- Die Indexmarke kommt auf die Motorglocke, eine Marke pro Umdrehung, mit einem Stift gezogen. Klebeband ist Masse auf dem Teil, das gewuchtet wird. Eine Marke auf einem Blatt gibt einen Puls je Blatt.
- Das Sensorkabel bleibt aus dem Luftstrom des Propellers.

## Der Reiter FLUGMODELL

![Wo die Sensoren an einem Flugmodell hinkommen](view:balance-aircraft)

- Der Beschleunigungssensor kommt flach auf den Motorspant, nahe den Schrauben, nicht auf die Haube.
- Die Indexmarke kommt auf die Glocke, nicht auf den Spinner: Ein Spinner kommt in einem neuen Winkel wieder drauf.
- Das Heck festbinden. Ein Modell, das schaukeln kann, ist eine Feder in der Messung.

## Verwandte Seiten

- [Anschlüsse](setup-interfaces.md): die Einstellungen Vibration und Optischer Tacho, in diesem Release gespeichert und nicht gelesen.
- [Wuchten](https://github.com/subtilitas/rcbench/blob/main/docs/Balance-de.md): Sensorwahl und Platzierung im Detail.
