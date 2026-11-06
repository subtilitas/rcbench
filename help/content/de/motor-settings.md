---
screen: none
title: Motor-Optionen
order: 11
---

# Motor-Optionen

OPTIONEN in der Fußleiste von [MOTOR & ESC](motor.md) öffnet die 8 ESC-Einstellungen (Electronic Speed Controller) des Prüfstands als Liste über der ganzen Seite, unter AKKU, MOTOR und GASAUSGANG. Sie öffnet nur bei SAFE; sonst zeigt das Band `OPTIONEN nur bei SAFE. Schalte zuerst auf SAFE.` Verlässt der Prüfstand SAFE, schließt die Liste.

![OPTIONEN](view:motor-settings)

Die Liste funktioniert wie die von SETUP: `-` und `+` verstellen einen Wert, gehalten wiederholen sie, und Ziehen rollt. Tippe auf einen Wert, um das Tastenfeld über der Seite zu öffnen. Es nennt die Einstellung und ihren Bereich. OK übernimmt gültige Werte. Bei einem Wert außerhalb des Bereichs bleibt das Tastenfeld offen und hebt den Bereich in der Warnfarbe hervor. ABBRECHEN oder das Home-Feld schließt es ohne Änderung. Werte werden auf die Schrittweite der Einstellung gerundet: Aus 4321mAh werden 4300mAh.

Die Leiste unten zeigt `[MOTOR & ESC] [MOTOR & ESC ZURÜCKSETZEN] [SPEICHERN]`. Unter MOTOR & ESC bleiben Protokoll und Pins des Gases. MOTOR & ESC ZURÜCKSETZEN setzt diese 8 Einstellungen auf ihre Vorgaben, keine anderen.

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

Leerlaufpuls und Vollgaspuls lassen sich nur bei SAFE ändern. Die Liste öffnet nur bei SAFE und schließt, sobald der Prüfstand SAFE verlässt; ein offenes Tastenfeld schließt mit ihr, ohne die Eingabe zu übernehmen.

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

Ein geänderter Wert gilt sofort. SPEICHERN fordert das Schreiben an; das Panel schreibt die Einstellungen im ersten Frame, in dem der Prüfstand SAFE, der Netzteilausgang ausgeschaltet und keine Platinenzeichnung im Lade- oder Speichervorgang ist. Dabei werden alle ungespeicherten Einstellungen gespeichert, auch Änderungen aus SETUP.

| SPEICHERN zeigt | Bedeutung |
|---|---|
| `GESPEICHERT` | Alle Einstellungen sind gespeichert. |
| `SPEICHERN` | Eine Änderung ist ungespeichert; tippe, um sie zu speichern. |
| `BEI STILLSTAND` | Speichern ist angefordert und wartet auf einen ruhenden Prüfstand. |
| `NICHT GESPEICHERT` | Das Schreiben ist fehlgeschlagen; die Werte gelten weiter, sind aber ungespeichert. |

Ist eine Änderung ungespeichert, zeigt das Home-Feld zuerst die [Warnung zu ungespeicherten Werten](setup.md#mit-ungespeicherten-werten-verlassen): BLEIBEN, VERWERFEN oder SPEICHERN.

## Verwandte Seiten

- [Motor und ESC](motor.md)
- [Ausgänge](outputs.md): Pins für die Gassteuerung.
- [Setup](setup.md): Speichertaste und Warnung beim Verlassen.
