---
screen: none
title: Motor-Optionen
order: 11
---

# Motor-Optionen

OPTIONEN, der zweite Reiter der Leiste von [MOTOR & ESC](motor.md), hält die 8 ESC-Einstellungen (Electronic Speed Controller) des Prüfstands in der linken Spalte unter drei Überschriften: AKKU, MOTOR und GASAUSGANG. Die rechte Spalte bleibt an ihrem Platz und wirkt: die Anzeigen, ZIEL, ARM, die Summen und SPITZENWERTE ZURÜCKSETZEN.

![OPTIONEN](view:motor-settings)

Eine Zeile zeigt die Einstellung, eine Hilfezeile und ihren Wert mit Einheit. Ein Tipp auf den Wert öffnet das Tastenfeld über der linken Spalte und der Leiste. Das Tastenfeld nennt die Einstellung und ihren Bereich. OK übernimmt einen Wert im Bereich; ein Wert außerhalb hält das Tastenfeld offen und nennt den Bereich in der Warnfarbe. ABBRECHEN und das Home-Tag schließen das Tastenfeld und ändern nichts. Ein getippter Wert landet auf dem Schritt der Einstellung: 4321 mAh werden 4300 mAh.

![Das Tastenfeld für Kapazität](view:motor-settings-keypad)

## Die Einstellungen

| Überschrift | Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|---|
| AKKU | Zellen | 1 bis 14 S | 6 | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| AKKU | Kapazität | 0 bis 30000 mAh, Schritte von 100 mAh | 5000 | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| AKKU | Innenwiderstand | 0 bis 200 mOhm | 18,0 | gespeichert; kein Bildschirm nutzt ihn in diesem Release |
| MOTOR | Motorpole | 2 bis 42, Schritte von 2 | 14 | die Drehzahl ist eRPM (elektrische Drehzahl) geteilt durch Pole/2 |
| MOTOR | Nenn-kV | 0 bis 10000 rpm/V, Schritte von 10 | 0 | der kV-Wert auf [LIVE](motor.md#kv-rpm-v-und-eff); 0 lässt ihn leer |
| GASAUSGANG | Leerlaufpuls | 800 bis 1600 us, Schritte von 10 | 1000 | die PWM-Breite (Pulsweitenmodulation) bei 0 % Throttle |
| GASAUSGANG | Vollpuls | 1400 bis 2400 us, Schritte von 10 | 2000 | die PWM-Breite bei 100 % Throttle |
| GASAUSGANG | Rampengrenze | 5 bis 300 %/s, Schritte von 5 | 55 | die Throttle-Rampe des Panelmodells, genutzt, solange kein IOmcu antwortet |

Leerlaufpuls und Vollpuls gehen beim Link-Aufbau und bei jeder Änderung an den IOmcu. Der IOmcu fährt keine Throttle-Rampe: Ein gebundener Pin springt auf den Befehl.

Leerlaufpuls und Vollpuls nehmen nur bei einem Prüfstand in SAFE einen Wert an. Solange der Prüfstand ARMED ist oder ein ARM oder ein DISARM auf seine Antwort wartet, lautet ihre Hilfezeile `Nur bei SAFE einstellbar` in der Warnfarbe, und der Wert nimmt keinen Tipp an. Ein offenes Tastenfeld für einen von beiden schließt, wenn der Prüfstand SAFE verlässt. Die anderen 6 Zeilen nehmen jederzeit einen Wert an.

## Die Werte behalten

Ein geänderter Wert gilt sofort und fordert das Schreiben an. Das Panel schreibt im ersten Frame mit dem Prüfstand SAFE, abgeschaltetem Netzteilausgang und ohne Platinenzeichnung, die geladen oder gespeichert wird. Die Einstellungen sind ein Speicher: Das Schreiben behält jeden ungespeicherten Wert, auch die unter SETUP geänderten. Die Zeile unter den Einstellungen sagt, wo der Speicher steht:

| Zeile | Bedeutung |
|---|---|
| `GESPEICHERT` | nichts ist ungeschrieben |
| `SPEICHERN AUSSTEHEND` | das Schreiben ist angefordert und wartet auf einen ruhenden Prüfstand |
| `NICHT GESPEICHERT` | der Speicher hat das Schreiben abgelehnt; die Werte gelten und bleiben ungespeichert |
| `ÄNDERUNGEN IN SETUP NICHT GESPEICHERT` | unter SETUP oder auf einem anderen Bildschirm geänderte Werte sind ungespeichert, und kein Schreiben ist angefordert; die nächste Änderung hier schreibt sie mit |

Nach einem abgelehnten Schreiben bleiben die Werte ungespeichert. Die nächste Änderung fordert das Schreiben erneut an, und die Warnung von SETUP beim Verlassen nennt sie ([Mit ungespeicherten Werten verlassen](setup.md#mit-ungespeicherten-werten-verlassen)).

## Verwandte Seiten

- [Motor und ESC](motor.md)
- [Ausgänge](outputs.md): welche Pins der Throttle treibt.
- [Setup](setup.md): die Speichertaste und die Warnung beim Verlassen.
