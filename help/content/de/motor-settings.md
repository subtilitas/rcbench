---
screen: none
title: Motor-Optionen
order: 11
---

# Motor-Optionen

OPTIONEN, der dritte Reiter der Leiste von [MOTOR & ESC](motor.md), hält die 8 ESC-Einstellungen (Electronic Speed Controller) des Prüfstands in der linken Spalte unter drei Überschriften, in zwei Spalten: AKKU und MOTOR links, GASAUSGANG rechts. Die rechte Spalte bleibt an ihrem Platz und wirkt: die Anzeigen, ZIEL, ARM, die Summen und SPITZENWERTE ZURÜCKSETZEN.

![OPTIONEN](view:motor-settings)

Eine Zeile zeigt die Einstellung und ihren Wert mit Einheit, 48px hoch, und darunter eine Hilfezeile. Ein Tipp auf den Wert öffnet das Tastenfeld über der linken Spalte und der Leiste. Das Tastenfeld nennt die Einstellung und ihren Bereich. OK übernimmt einen Wert im Bereich; ein Wert außerhalb hält das Tastenfeld offen und nennt den Bereich in der Warnfarbe. ABBRECHEN und das Home-Tag schließen das Tastenfeld und ändern nichts. Ein getippter Wert landet auf dem Schritt der Einstellung: 4321mAh werden 4300mAh.

![Das Tastenfeld für Kapazität](view:motor-settings-keypad)

## Die Einstellungen

| Überschrift | Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|---|
| AKKU | Zellen | 1 bis 14 S | 6 | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| AKKU | Kapazität | 0 bis 30000mAh, Schritte von 100mAh | 5000 | gespeichert; kein Bildschirm nutzt sie in diesem Release |
| AKKU | Innenwiderstand | 0 bis 200mΩ | 18 | gespeichert; kein Bildschirm nutzt ihn in diesem Release |
| MOTOR | Motorpole | 2 bis 42, Schritte von 2 | 14 | die Drehzahl ist eRPM (elektrische Drehzahl) geteilt durch Pole/2 |
| MOTOR | Nenn-kV | 0 bis 10000rpm/V, Schritte von 10 | 0 | der kV-Wert auf [LIVE](motor.md#kv-rpm-v-und-eff); 0 lässt ihn leer |
| GASAUSGANG | Leerlaufpuls | 800 bis 1600us, Schritte von 10 | 1000 | die PWM-Breite (Pulsweitenmodulation) bei 0% Throttle |
| GASAUSGANG | Vollpuls | 1400 bis 2400us, Schritte von 10 | 2000 | die PWM-Breite bei 100% Throttle |
| GASAUSGANG | Rampengrenze | 5 bis 300%/s, Schritte von 5 | 55 | die Throttle-Rampe des Panelmodells, genutzt, solange kein IOmcu antwortet |

Das Tastenfeld des Leerlaufpulses lehnt einen Wert ab, der nicht mindestens 100us unter dem Vollpuls liegt, das des Vollpulses einen, der nicht mindestens 100us über dem Leerlaufpuls liegt. Das Tastenfeld bleibt offen, zeigt `Leerlaufpuls höchstens 1900us (100us unter Vollpuls)` oder `Vollpuls mindestens 1100us (100us über Leerlaufpuls)` in der Warnfarbe, und die Zeile behält ihren Wert. Die Bereiche überlappen sich, 800 bis 1600us und 1400 bis 2400us, daher braucht das Paar diese Prüfung.

Leerlaufpuls und Vollpuls gehen beim Link-Aufbau und bei jeder Änderung an den IOmcu. Der IOmcu fährt keine Throttle-Rampe: Ein gebundener Pin springt auf den Befehl.

Leerlaufpuls und Vollpuls nehmen nur bei einem Prüfstand in SAFE einen Wert an. Solange der Prüfstand ARMED ist, ARM gehalten wird oder ein ARM oder ein DISARM auf seine Antwort wartet, lautet ihre Hilfezeile `Nur bei SAFE einstellbar` in der Warnfarbe, und der Wert nimmt keinen Tipp an. Ein offenes Tastenfeld für einen von beiden schließt, wenn ARM gedrückt wird oder der Prüfstand SAFE verlässt. Die anderen 6 Zeilen nehmen jederzeit einen Wert an.

Eine Änderung von Leerlaufpuls oder Vollpuls erreicht den IOmcu beim ersten Poll 300ms oder mehr nach der letzten Eingabe. Bis dahin wird ARM bei angeschlossenem IOmcu mit `Pulsbereich noch nicht an den IOmcu gesendet. ARM erneut halten.` abgelehnt, und der Prüfstand bleibt SAFE. ARM erneut halten: Das Halten über 2s gibt der Änderung die Zeit anzukommen.

Übernimmt der IOmcu das Paar nicht, bleibt ARM abgelehnt, bis das Paar in Kraft ist, und das Band sagt, was zu ändern ist:

| Band | Was tun |
|---|---|
| `Kein Pulsbereich vom IOmcu. IOmcu-Firmware aktualisieren, dann ARM erneut halten.` | den IOmcu aktualisieren: Das Panel bietet das mitgebrachte Image an, wenn der IOmcu sich verbindet |
| `Leerlaufpuls liegt nicht 100us oder mehr unter Vollpuls. Einen ändern, dann ARM erneut halten.` | Leerlaufpuls senken oder Vollpuls erhöhen |
| `Der IOmcu hat den Pulsbereich abgelehnt. Leerlaufpuls, Vollpuls oder OUTPUTS ändern.` | den Puls kürzen oder den Motor-Pin auf OUTPUTS mit einer langsameren Rate zuordnen: Ein Puls, der länger als sein PWM-Frame ist, wird abgelehnt |
| `Der IOmcu hält einen anderen Pulsbereich. Leerlaufpuls oder Vollpuls ändern.` | einen der beiden Werte ändern; das Panel sendet das Paar erneut |

Eine auf OUTPUTS geschriebene Zuordnung und ein erneutes Verbinden des IOmcu senden das Paar ebenfalls erneut.

## Die Werte behalten

Ein geänderter Wert gilt sofort und fordert das Schreiben an. Das Panel schreibt im ersten Frame mit dem Prüfstand SAFE, abgeschaltetem Netzteilausgang und ohne Platinenzeichnung, die geladen oder gespeichert wird. Die Einstellungen sind ein Speicher: Das Schreiben behält jeden ungespeicherten Wert, auch die unter SETUP geänderten. Die Zeile am Fuß der Seite sagt, wo der Speicher steht:

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
