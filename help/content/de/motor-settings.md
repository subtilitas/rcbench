---
screen: none
title: Motor-Optionen
order: 11
---

# Motor-Optionen

OPTIONEN ist der dritte Reiter von [MOTOR & ESC](motor.md). Der linke Bereich enthält die 8 ESC-Einstellungen (Electronic Speed Controller) des Prüfstands in zwei Spalten: AKKU und MOTOR links, GASAUSGANG rechts. Die rechte Hauptspalte bleibt sichtbar und bedienbar, mit Anzeigen, ZIEL, ARM, Summen und SPITZENWERTE ZURÜCKSETZEN.

![OPTIONEN](view:motor-settings)

Jede 48px hohe Zeile zeigt Einstellung, Wert, Einheit und eine Hilfezeile. Tippe auf den Wert, um das Tastenfeld über dem linken Bereich und der Reiterleiste zu öffnen. Es nennt die Einstellung und ihren Bereich. OK übernimmt gültige Werte. Bei einem Wert außerhalb des Bereichs bleibt das Tastenfeld offen und hebt den Bereich in der Warnfarbe hervor. ABBRECHEN oder das Home-Feld schließt es ohne Änderung. Werte werden auf die Schrittweite der Einstellung gerundet: Aus 4321mAh werden 4300mAh.

![Das Tastenfeld für Kapazität](view:motor-settings-keypad)

## Die Einstellungen

| Überschrift | Einstellung | Bereich | Vorgabe | Wirkung |
|---|---|---|---|---|
| AKKU | Zellen | 1 bis 14S | 6 | gespeichert; in diesem Release auf keiner Seite verwendet |
| AKKU | Kapazität | 0 bis 30000mAh, Schritte von 100mAh | 5000 | gespeichert; in diesem Release auf keiner Seite verwendet |
| AKKU | Innenwiderstand | 0 bis 200mΩ | 18 | gespeichert; in diesem Release auf keiner Seite verwendet |
| MOTOR | Motorpole | 2 bis 42, Schritte von 2 | 14 | Drehzahl = eRPM (elektrische Drehzahl) geteilt durch Pole/2 |
| MOTOR | Nenn-kV | 0 bis 10000rpm/V, Schritte von 10 | 0 | kV-Wert auf [LIVE](motor.md#kv-rpm-v-und-wirk); bei 0 bleibt er leer |
| GASAUSGANG | Leerlaufpuls | 800 bis 1600us, Schritte von 10 | 1000 | PWM-Breite (Pulsweitenmodulation) bei 0% Gas |
| GASAUSGANG | Vollgaspuls | 1400 bis 2400us, Schritte von 10 | 2000 | PWM-Breite bei 100% Gas |
| GASAUSGANG | Rampengrenze | 5 bis 300%/s, Schritte von 5 | 55 | Gasrampe des Panelmodells, solange kein IOmcu antwortet |

Der Leerlaufpuls muss mindestens 100us unter dem Vollgaspuls liegen. Andernfalls bleibt das Tastenfeld offen und der Wert unverändert. In der Warnfarbe erscheint `Leerlaufpuls höchstens 1900us (100us unter Vollgaspuls)` oder `Vollgaspuls mindestens 1100us (100us über Leerlaufpuls)`. Diese Prüfung ist nötig, weil sich die zulässigen Bereiche von 800 bis 1600us und 1400 bis 2400us überlappen.

Leerlaufpuls und Vollgaspuls werden beim Verbindungsaufbau und nach jeder Änderung an den IOmcu gesendet. Der IOmcu setzt Gasbefehle an zugeordneten Pins sofort um, ohne Rampe.

Leerlaufpuls und Vollgaspuls lassen sich nur bei SAFE ändern. Sie sind gesperrt, solange der Prüfstand ARMED ist, ARM gehalten wird oder eine ARM- oder DISARM-Anfrage auf Antwort wartet. Dann zeigt die Hilfezeile `Nur bei SAFE einstellbar` in der Warnfarbe. Ein offenes Tastenfeld für einen der beiden Werte schließt, wenn ARM gedrückt wird oder der Prüfstand SAFE verlässt. Die übrigen 6 Werte lassen sich jederzeit ändern.

Eine Änderung von Leerlaufpuls oder Vollgaspuls erreicht den IOmcu bei der ersten Abfrage mindestens 300ms nach der letzten Eingabe. Bis dahin wird ARM bei angeschlossenem IOmcu mit `Pulsbereich noch nicht an den IOmcu gesendet. Halte ARM erneut.` abgelehnt; der Prüfstand bleibt SAFE. Halte ARM erneut: Die 2s geben der Übertragung genügend Zeit.

Übernimmt der IOmcu das Pulspaar nicht, bleibt ARM gesperrt, bis es angewendet ist. Das Band nennt die nötige Änderung:

| Band | Vorgehen |
|---|---|
| `Kein Pulsbereich vom IOmcu. Aktualisiere die IOmcu-Firmware, dann halte ARM erneut.` | Aktualisiere den IOmcu. Beim Verbinden bietet das Panel sein mitgeliefertes Image an. |
| `Leerlaufpuls weniger als 100us unter Vollgaspuls. Ändere einen, dann halte ARM erneut.` | Senke den Leerlaufpuls oder erhöhe den Vollgaspuls. |
| `IOmcu hat den Pulsbereich abgelehnt. Ändere Leerlaufpuls, Vollgaspuls oder ZIEL.` | Kürze den Puls oder ordne den Motor-Pin auf OUTPUTS einer langsameren Rate zu. Ein Puls, der länger als sein PWM-Frame ist, wird abgelehnt. |
| `Der IOmcu hat einen anderen Pulsbereich. Ändere Leerlaufpuls oder Vollgaspuls.` | Ändere einen der beiden Werte; das Panel sendet das Paar erneut. |

Auch das Schreiben einer Zuordnung auf OUTPUTS oder ein erneuter Verbindungsaufbau zum IOmcu sendet das Pulspaar erneut.

## Die Werte behalten

Ein geänderter Wert gilt sofort und fordert das Speichern an. Das Panel schreibt die Einstellungen im ersten Frame, in dem der Prüfstand SAFE, der Netzteilausgang ausgeschaltet und keine Platinenzeichnung im Lade- oder Speichervorgang ist. Dabei werden alle ungespeicherten Einstellungen gespeichert, auch Änderungen aus SETUP. Die Fußzeile zeigt den Speicherstatus:

| Zeile | Bedeutung |
|---|---|
| `GESPEICHERT` | Alle Einstellungen sind gespeichert. |
| `SPEICHERN AUSSTEHEND` | Speichern ist angefordert und wartet auf einen ruhenden Prüfstand. |
| `NICHT GESPEICHERT` | Das Schreiben ist fehlgeschlagen; die Werte gelten weiter, sind aber ungespeichert. |
| `ÄNDERUNGEN IN SETUP NICHT GESPEICHERT` | Änderungen aus SETUP oder einer anderen Seite sind ungespeichert, ohne Speicheranforderung. Die nächste Änderung hier speichert sie mit. |

Nach einem fehlgeschlagenen Schreibvorgang bleiben die Werte ungespeichert. Die nächste Änderung fordert einen neuen Schreibvorgang an. Beim Verlassen von SETUP nennt die [Warnung zu ungespeicherten Werten](setup.md#mit-ungespeicherten-werten-verlassen) diese Änderungen.

## Verwandte Seiten

- [Motor und ESC](motor.md)
- [Ausgänge](outputs.md): Pins für die Gassteuerung.
- [Setup](setup.md): Speichertaste und Warnung beim Verlassen.
