---
screen: none
title: ESC STICK
order: 12
---

# ESC STICK

ESC STICK stellt einen ESC (Electronic Speed Controller) über sein Throttle-Stick-Menü ein. Der Prüfstand versorgt den ESC aus dem Netzteil PD mini, fährt den Throttle auf die Positionen, die das ESC-Profil nennt, und zählt die Pieptöne des Menüs als Pulse im Strom des Netzteils. Es öffnet aus der Zeile `ESCs per Gasknüppel` auf [PROGRAMMIEREN](motor.md#programmieren) von MOTOR & ESC, über die volle Breite unter dem Ausgangsstreifen.

Kein ESC wurde aufgezeichnet, und kein Profil ist geprüft. Jede Pieptonzeit ist ein Vorgabewert, als Einstellung gehalten. FERTIG heißt, jede Auswahlbewegung wurde auf einer gezählten Gruppe ausgeführt, nicht, dass der ESC sie gespeichert hat.

## Vor einem Lauf

1. Den Propeller abnehmen. Ein Lauf fährt den Throttle auf MAX und zurück, während der ESC versorgt ist. In seltenen Fällen kann ein Motor anlaufen und drehen.
2. Den Motor am ESC lassen, fest montiert, oder an seiner Stelle einen Lastwiderstand über die Motorleitungen setzen. Die Pieptöne werden in beiden Fällen aus dem Strom gezählt.
3. Die Signalleitung des ESC an einen Pin legen, der unter [Ausgänge](outputs.md) als Throttle gebunden ist: MOTOR PWM oder ein DShot-Eintrag.
4. Die Versorgungsleitungen des ESC an den Netzteilausgang legen. Den PD mini unter [Anschlüsse](setup-interfaces.md#der-pd-mini) einschalten.
5. Den Netzteilausgang abschalten.

START öffnet nichts, solange eines davon fehlt, und die Zeile neben START nennt den Grund, zum Beispiel `Kein Gas-Pin zugeordnet. Auf der Seite OUTPUTS zuordnen.` oder `Netzteil ist simuliert. START benötigt einen PD mini.`. START braucht einen Messwert des Netzteils, nicht älter als 1000 ms, der den Ausgang aus mit höchstens 20 mA für 200 ms meldet.

## Hersteller und Modelle

![Die Hersteller](view:motor-stick)

Die Liste hat zwei Ebenen.

- **Hersteller**: alphabetisch, jeder mit der Zahl seiner ausführbaren Modelle, `10 VON 82 AUSFÜHRBAR`. Ein Hersteller ohne ausführbares Modell ist dunkel gezeichnet. Ein Tipp öffnet seine Modelle.
- **Modelle**: nach Strom, dann Spannung, dann Name. Eine Zeile zeigt das Modell, seinen Strom und seine Spannung, seine Familie und was sein Profil ist oder warum es nicht ausführbar ist. ZURÜCK führt zu den Herstellern.

![Die Modelle von Kontronik, markiert mit MANUELL](view:motor-stick-hand-list)

Eine rote Marke MANUELL kennzeichnet ein Modell, dessen ESC während des Laufs eine Person braucht.

## Suche

![Eine Suche wird auf den Herstellern getippt](view:motor-stick-find)
![Eine angewendete Suche, die Modelle von Hobbywing](view:motor-stick-found)

SUCHE filtert die angezeigte Ebene. Ihre Tastatur dockt rechts an, und jede Taste filtert sofort.

- Ein Modell wird gefunden, wenn der Text in Hersteller und Familie, als ein Text gelesen, vorkommt, oder in Hersteller und eigenem Namen.
- Groß- und Kleinschreibung zählen nicht. `*` steht für eine beliebige Zeichenfolge: `sky*v2` findet die Modelle Skywalker V2.
- Die Suche fasst bis zu 16 Zeichen und bleibt auf beiden Ebenen bis zu einem Neustart.
- OK behält die Suche, ABBRECHEN kehrt zur Suche zurück, mit der die Tastatur geöffnet wurde, und X im Feld löscht sie.

## Ein Profil

![Zwei Werte gewählt: START 2](view:motor-stick-items)

Die Seite eines Profils listet seine Menüpunkte. Jeder beginnt bei BEHALTEN, das den Punkt lässt, wie er ist. `-` und `+` gehen durch die Werte des Punkts und halten an beiden Enden. AKTION, KEINE EINSTELLUNG kennzeichnet einen Punkt, der eine Aktion ist, etwa ein Zurücksetzen; KEINE AUSWAHL einen Punkt mit einem Wert. Die Zeile unter der Liste nennt die Werte des gewählten Punkts und die Vorgabe. START zählt die gewählten Werte.

Der Kopf zeigt die Art des Menüs, Spannung und Strom des Netzteils für den Lauf und die Einstiegszeit. TIMING öffnet die Pieptonzeiten und die Netzteileinstellungen:

![Die Timing-Einstellungen](view:motor-stick-timing)

Keine der Zeiten ist gemessen. SCHLIESSEN fordert das Speichern an; das Speichern wartet auf den Prüfstand SAFE und den Netzteilausgang aus.

`-` und `+` stellen eine Zeile. Ein Tippen auf ihren Wert öffnet das Tastenfeld über der Seite. Es nennt die Einstellung, ihren Bereich und ihre Einheit und nimmt die Stellen des Schritts nach dem Punkt. OK setzt den Wert wie ein Schritt auf den näheren Schritt: 1,23 A sind 1,25 A. Ein Wert außerhalb des Bereichs lässt das Tastenfeld offen und nennt den Bereich in der Warnfarbe. ABBRECHEN und das Home-Tag ändern nichts; TIMING bleibt.

![Das Tastenfeld von Piepton min, 250 getippt](view:motor-stick-timing-keypad)

## Die Warnung und der Lauf

![PROPELLER AB? MOTOR GESICHERT?](view:motor-stick-warning)

START öffnet eine Warnung über den ganzen Bildschirm: PROPELLER AB? MOTOR GESICHERT?. Sie nennt das Profil, die Sollwerte des Netzteils und die Zahl der Änderungen. HALTEN ZUM STARTEN startet den Lauf nach einem Halten von 2 s, wie ARM. Ein Finger, der die Taste verlässt, oder ein STOP während des Haltens bricht es ab; ABBRECHEN oder das Home-Tag schließt die Warnung.

![Ein Lauf zählt Punktgruppen](view:motor-stick-run)

Der Lauf schaltet den Prüfstand auf ARMED MOTOR. Die Seite zeigt die Phase, die Pieptöne der laufenden Gruppe, die letzte Gruppe und ob sie in Folge war, und den Strom gegen seinen Grundwert. ABBRECHEN, das Home-Tag, STOP und das Verlassen des Bildschirms beenden den Lauf mit dem Throttle auf MIN, dem Netzteil aus und dem Prüfstand SAFE. Nach ABBRECHEN oder dem Home-Tag bleibt das Ergebnis auf der Seite.

Das Grün der Signalsäule leuchtet, solange ein Piepton erkannt wird, mindestens 150 ms.

![Ein Lauf mit laufendem Phasenabgriff](view:motor-stick-tone)

Ist der Phasenabgriff unter SETUP, ANSCHLÜSSE eingeschaltet, fügt die Seite eine Anzeige hinzu, die nur liest: den Zustand des Abgriffs, die Tonhöhe seines letzten 8-ms-Fensters, die Zahl verlorener und nicht gelesener Pieptöne und die letzten 4 Pieptöne mit Nummer, Länge in ms und Tonhöhe in Hz. Der Lauf zählt seine Pieptöne weiter aus dem Strom des Netzteils.

| Zustand des Abgriffs | Bedeutung |
|---|---|
| `LÄUFT` | die Erfassung läuft |
| `ÜBERLAUF` | sie läuft, und die Erfassung ist seit dem Einschalten des Abgriffs übergelaufen |
| `WARTET` | kein Link, oder in den letzten 500 ms keine frische Antwort der Tone-Page des IOmcu |
| `KEINE TONE-PAGE` | der IOmcu spricht ein Protokoll älter als 4.8 |
| `PIN BELEGT` | der Pin des Abgriffs ist anderweitig gebunden |
| `LÄUFT NICHT` | eingeschaltet, und die Einstellung wurde abgelehnt oder ist noch nicht geschrieben |

## Das Ergebnis

![Fertig](view:motor-stick-done)

Das Ergebnis bleibt bis OK: welche Auswahlen ausgeführt wurden, und bei einem beendeten Lauf der Grund.

![Durch STOP beendet: Rot dunkel](view:motor-stick-aborted)
![Vom Netzteil beendet: Rot leuchtet](view:motor-stick-failed)

Das Rot der Signalsäule leuchtet, wenn der Lauf endete, weil etwas nicht wie erwartet war, einschließlich eines Stopps, den der Prüfstand selbst ausgelöst hat. Es bleibt dunkel bei FERTIG, einem gedrückten STOP, ABBRECHEN, dem Home-Tag und dem Verlassen des Bildschirms.

## Handgriffe

![Ein Profil mit Handgriffen](view:motor-stick-hand)
![Seine Handgriffe](view:motor-stick-hand-info)

Manche ESCs gehen erst in ihren Programmiermodus, wenn eine Person einen Jumper steckt oder eine Taste am ESC drückt. Jeder Kontronik-ESC im Satz gehört dazu. Ein solches Profil zeigt MANUELLER EINGRIFF NÖTIG in Rot. Ein Tipp zeigt die Handgriffe und wann jeder fällig ist; sie öffnen von selbst, wenn das Profil nach einem Start zum ersten Mal geöffnet wird.

![Die Warnung mit einem Handgriff vor dem Einschalten](view:motor-stick-hand-warning)
![Vier Handgriffe vor dem Einschalten, unter ALLE HANDGRIFFE zu lesen](view:motor-stick-hand-steps)

Die Warnung listet die Handgriffe, die vor dem Einschalten fällig sind. Passen nicht alle, öffnet ALLE HANDGRIFFE sie, und HALTEN ZUM STARTEN zählt erst, nachdem ALLE HANDGRIFFE geöffnet und mit OK geschlossen wurde.

![Der Lauf wartet auf den Jumper](view:motor-stick-hand-prompt)

Ein Handgriff, der während des Laufs fällig ist, deckt die Seite mit ERLEDIGT und ABBRECHEN ab. ERLEDIGT ist die ersten 1000 ms dunkel. Ohne ERLEDIGT binnen 60 s endet der Lauf mit NICHT BESTÄTIGT.

![Die Handgriffe nach dem Programmieren, am Ende eines Laufs](view:motor-stick-hand-after)

Handgriffe, die nach dem Programmieren fällig sind, öffnen, wenn der Lauf vorbei ist.

![Der Lauf wartet auf die Bestätigung des ESC](view:motor-stick-hand-end)

Ein Kontronik-ESC bestätigt einen gespeicherten Modus mit Tönen und sperrt sich, wenn seine Versorgung vor dem Ende der Bestätigung ausgeht. Der Lauf hält den ESC nach dem Speichern versorgt, den Stick, wo er gespeichert hat, bis ERLEDIGT.

![NICHT BESTÄTIGT: der ESC kann gesperrt sein](view:motor-stick-hand-locked)

Geht das Netzteil aus, bevor der ESC bestätigt hat, markiert das Ergebnis die Änderung als UNKLAR und sagt, dass der ESC gesperrt sein kann.

## Verwandte Seiten

- [Motor und ESC](motor.md#programmieren): PROGRAMMIEREN und der Ausgangsstreifen.
- [Stick-Programmierung](https://github.com/subtilitas/rcbench/blob/main/docs/StickProgramming-de.md): Profile, Zeiten und jede Regel eines Laufs.
