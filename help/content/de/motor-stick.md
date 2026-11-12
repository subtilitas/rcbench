---
screen: none
title: ESC STICK
order: 12
---

# ESC STICK

ESC STICK stellt einen ESC (Electronic Speed Controller) über sein Gasknüppel-Menü ein. Der Prüfstand versorgt ihn aus dem PD mini, fährt die Gaspositionen des Profils an und zählt die Menütöne am Phasenabgriff oder an einem INA3221-Kanal in der Versorgungsleitung des ESC. Öffne `ESCs per Gasknüppel` unter MOTOR & ESC > [PROGRAMMIEREN](motor.md#programmieren). Die Sitzung füllt die Breite unter dem Ausgangsstreifen.

Kein ESC wurde aufgezeichnet, und kein Profil ist geprüft. Alle Pieptonzeiten sind einstellbare Vorgaben. FERTIG bedeutet, dass die Auswahlbewegungen bei den gezählten Gruppen ausgeführt wurden. Es bestätigt nicht, dass der ESC die Änderungen gespeichert hat.

## Vor einem Lauf

1. Nimm den Propeller ab. Während der ESC versorgt ist, fährt der Lauf Gas auf MAX und zurück. In seltenen Fällen kann der Motor anlaufen und drehen.
2. Lass den fest montierten Motor am ESC angeschlossen oder ersetze ihn durch einen Lastwiderstand an den Motorleitungen.
3. Gib dem Lauf eine Pieptonquelle: schließe den Phasenabgriff an eine Motorleitung an und schalte ihn unter SETUP, SENSORS ein, oder lege den Shunt eines INA3221-Kanals in die Leitung vom Netzteilausgang zum ESC und stelle die Zeile des Kanals, etwa `0x40 CH2 misst`, auf NETZTEIL.
4. Verbinde die ESC-Signalleitung mit einem unter [Ausgänge](outputs.md) als Gas zugeordneten Pin: MOTOR PWM oder ein DShot-Eintrag.
5. Verbinde die ESC-Versorgungsleitungen mit dem Netzteilausgang. Aktiviere den PD mini [im Reiter PD MINI auf SUPPLY](supply.md#zeilen-des-pd-mini).
6. Schalte den Netzteilausgang aus.

START bleibt gesperrt, bis diese Voraussetzungen erfüllt sind. Die Zeile daneben nennt den Grund, etwa `Kein Gas-Pin zugeordnet. Ordne einen auf der Seite OUTPUTS zu.` `Netzteil ist simuliert. START benötigt einen PD mini.` oder `Keine Pieptonquelle. Schalte unter SENSORS den Phasenabgriff ein oder stelle einen INA3221-Kanal auf NETZTEIL.` START benötigt einen höchstens 1000ms alten Netzteilmesswert: Ausgang aus und Strom für 200ms höchstens 20mA.

Ein Lauf zählt am Phasenabgriff, wenn dessen Erfassung bei START läuft, sonst am INA3221-Kanal. Die Quelle bleibt die des Laufs bis zu seinem Ende. Der Strom des PD mini selbst ist keine Quelle: er misst alle 100 bis 152ms, langsamer als ein kurzer Piepton von 128ms. Seine Messwerte entscheiden weiterhin die Netzteilregeln.

## Hersteller und Modelle

![Die Hersteller](view:motor-stick)

Die Liste hat zwei Ebenen.

- **Hersteller**: alphabetisch, mit der Anzahl ausführbarer Modelle, etwa `10 VON 82 AUSFÜHRBAR`. Ohne ausführbares Modell erscheint ein Hersteller dunkel. Tippe auf ihn, um seine Modelle zu öffnen.
- **Modelle**: nach Strom, Spannung und Name sortiert. Jede Zeile zeigt Modell, Strom, Spannung, Familie und Profilstatus oder den Grund, warum kein Lauf möglich ist. ZURÜCK führt zur Herstellerliste.

![Die Modelle von Kontronik, markiert mit MANUELL](view:motor-stick-hand-list)

Die rote Markierung MANUELL kennzeichnet ESCs, die während des Laufs einen Handgriff benötigen.

## Suche

![Eine Suche wird auf den Herstellern getippt](view:motor-stick-find)
![Eine angewendete Suche, die Modelle von Hobbywing](view:motor-stick-found)

SUCHE filtert die angezeigte Listenebene. Die Tastatur erscheint rechts; jede Eingabe aktualisiert den Filter.

- Gesucht wird im zusammengesetzten Text aus Hersteller und Familie oder aus Hersteller und Modellname.
- Groß- und Kleinschreibung spielen keine Rolle. `*` steht für eine beliebige Zeichenfolge: `sky*v2` findet Skywalker-V2-Modelle.
- Der Suchtext darf bis zu 16 Zeichen enthalten und bleibt auf beiden Ebenen bis zum Neustart erhalten.
- OK übernimmt die Suche. ABBRECHEN stellt den Suchtext vom Öffnen der Tastatur wieder her. X im Suchfeld löscht ihn.

## Ein Profil

![Zwei Werte gewählt: START 2](view:motor-stick-items)

Die Profilseite listet die Menüpunkte. Jeder beginnt mit BEHALTEN und bleibt damit unverändert. Wähle mit `-` und `+` einen Wert; die Tasten stoppen an beiden Enden der Liste. AKTION, KEINE EINSTELLUNG markiert Aktionen wie das Zurücksetzen. KEINE AUSWAHL steht bei einem Punkt mit nur einem Wert. Jede Seite zeigt 5 Punkte.

Die rechte Spalte enthält ÄNDERN, den angezeigten Bereich und die Blättertasten `^` und `v`. Dazu kommen gegebenenfalls MANUELLER EINGRIFF NÖTIG, Menüart, Netzteilspannung und Strom für den Lauf, Einstiegszeit sowie Werte und Vorgabe des gewählten Punkts. Nennt das Profil gemessene Pieptonlängen, zeigt sie auch PIEPTON MIN, LANG und PAUSE MIN mit "(Profil)": Der Lauf nimmt sie statt der Einstellungen. Die obere Karte eines Laufs zeigt PIEPTON MIN und LANG bei jedem Lauf, mit "(Profil)" oder "(Einstellung)". START zeigt die Anzahl gewählter Werte.

Die Fußleiste lautet `[ESC STICK] [START n] [OPTIONEN]`; während eines Laufs steht in der Mitte ABBRECHEN, danach OK. Ein Hinweis über der Fußleiste sagt, warum START nicht starten kann, oder dass das Profil nicht geprüft ist.

OPTIONEN öffnet die Liste mit den Netzteileinstellungen und den Pieptonzeiten. Es nimmt einen Druck bei SAFE und ohne laufenden Lauf an; scharf schalten unter der offenen Liste schließt sie.

![Die Optionsliste](view:motor-stick-timing)

Die Vorgaben sind nicht gemessen. Nennt ein Profil seine Pieptonlängen, nimmt der Lauf PIEPTON MIN, LANG und LANG MAX daraus statt aus diesen Einstellungen, und PAUSE MIN, wo der halbe kurze Piepton kleiner ist. Spannung 0,0V nimmt die Spannung aus der Zellenzahl des Profils. Eine Änderung gilt sofort; SPEICHERN in der Leiste der Liste behält sie, und ESC STICK ZURÜCKSETZEN setzt die 14 Einstellungen auf ihre Vorgaben.

Ändere eine Einstellung mit `-` und `+` oder tippe auf ihren Wert, um das Tastenfeld zu öffnen. Es zeigt Name, Bereich und Einheit; die Zahl der Nachkommastellen entspricht der Schrittweite. OK rundet auf den nächsten Schritt: Aus 1,23A werden 1,25A. Bei Werten außerhalb des Bereichs bleibt das Tastenfeld offen und hebt den Bereich in der Warnfarbe hervor. ABBRECHEN oder das Home-Feld verwirft die Eingabe; die Liste bleibt offen. Das Home-Feld schließt die Liste, bei ungespeicherter Änderung mit Rückfrage.

![Das Tastenfeld von Piepton min, 250 getippt](view:motor-stick-timing-keypad)

## Die Warnung und der Lauf

![PROPELLER AB? MOTOR GESICHERT?](view:motor-stick-warning)

START öffnet die bildschirmfüllende Warnung PROPELLER AB? MOTOR GESICHERT? mit Profil, Netzteilsollwerten und Anzahl der Änderungen. Halte 2s HALTEN ZUM STARTEN wie ARM für 2s, um den Lauf zu beginnen. Verlässt der Finger die Taste oder wird während des Haltens STOP gedrückt, bricht das Halten ab. ABBRECHEN oder das Home-Feld schließt die Warnung.

![Ein Lauf zählt Punktgruppen](view:motor-stick-run)

Der Lauf schaltet den Prüfstand auf ARMED MOTOR. Die Seite zeigt Phase, Pieptonzahl der laufenden Gruppe, letzte Gruppe mit Prüfung der Reihenfolge und woher die Pieptöne kommen: `PIEPTÖNE VOM PHASENABGRIFF`, oder den INA3221-Kanal mit seinem Strom relativ zum Grundwert. ABBRECHEN, das Home-Feld, STOP oder das Verlassen der Seite beendet den Lauf: Gas auf MIN, Netzteil aus, Prüfstand SAFE. Nach ABBRECHEN oder dem Home-Feld bleibt das Ergebnis sichtbar.

Die grüne Leuchte der Signalsäule leuchtet bei einem erkannten Piepton mindestens 150ms lang.

![Ein Lauf mit laufendem Phasenabgriff](view:motor-stick-tone)

Ist der Phasenabgriff unter SETUP, SENSORS aktiviert, ergänzt die Seite eine reine Anzeige. Sie zeigt Zustand, Tonhöhe des letzten Fensters von 8ms, Anzahl verlorener und nicht ausgelesener Pieptöne sowie die letzten 4 Töne mit Nummer, Länge in ms und Tonhöhe in Hz. Ein Lauf, der startet, während die Erfassung läuft, zählt diese Pieptöne. Er endet mit PIEPTON VERLOREN, wenn eine Pieptonnummer fehlt, und mit ABGRIFF ANGEHALTEN, wenn die Erfassung anhält; einer, der am INA3221 zählt, endet mit INA3221 VERLOREN, wenn 1000ms lang kein Fenster kommt.

| Zustand des Abgriffs | Bedeutung |
|---|---|
| `LÄUFT` | Erfassung läuft. |
| `ÜBERLAUF` | Erfassung läuft; seit dem Aktivieren des Abgriffs trat ein Überlauf auf. |
| `WARTET` | Keine Verbindung oder seit 500ms keine aktuelle Antwort der Tone-Page des IOmcu. |
| `KEINE TONE-PAGE` | IOmcu-Version älter als 0.14.0. |
| `PIN BELEGT` | Pin des Abgriffs anderweitig zugeordnet. |
| `LÄUFT NICHT` | Aktiviert, aber die Einstellung wurde abgelehnt oder noch nicht geschrieben. |

## Das Ergebnis

![Fertig](view:motor-stick-done)

Das Ergebnis nennt die ausgeführten Auswahlen und bei einem abgebrochenen Lauf den Grund. Es bleibt bis OK sichtbar.

![Durch STOP beendet: Rot dunkel](view:motor-stick-aborted)
![Vom Netzteil beendet: Rot leuchtet](view:motor-stick-failed)

Die rote Leuchte der Signalsäule zeigt ein unerwartetes Laufende an, auch einen vom Prüfstand selbst ausgelösten Stopp. Bei FERTIG, gedrücktem STOP, ABBRECHEN, dem Home-Feld oder dem Verlassen der Seite bleibt sie aus.

## Handgriffe

![Ein Profil mit Handgriffen](view:motor-stick-hand)
![Seine Handgriffe](view:motor-stick-hand-info)

Manche ESCs benötigen einen Jumper oder einen Tastendruck am ESC, um den Programmiermodus zu öffnen. Dazu gehören alle Kontronik-ESCs im Satz. Solche Profile zeigen MANUELLER EINGRIFF NÖTIG in Rot. Tippe darauf, um die Handgriffe und ihren Zeitpunkt zu sehen. Beim ersten Öffnen des Profils nach einem Neustart erscheinen sie automatisch.

![Die Warnung mit einem Handgriff vor dem Einschalten](view:motor-stick-hand-warning)
![Vier Handgriffe vor dem Einschalten, unter ALLE HANDGRIFFE zu lesen](view:motor-stick-hand-steps)

Die Warnung listet die Handgriffe vor dem Einschalten auf. Passen sie nicht vollständig, öffnet ALLE HANDGRIFFE die Liste. Erst nachdem du diese geöffnet und mit OK geschlossen hast, zählt das Halten von 2s HALTEN ZUM STARTEN.

![Der Lauf wartet auf den Jumper](view:motor-stick-hand-prompt)

Ist während des Laufs ein Handgriff nötig, verdeckt eine Aufforderung mit ERLEDIGT und ABBRECHEN die Seite. ERLEDIGT ist die ersten 1000ms gesperrt. Vor einem Einschalten schaltet ERLEDIGT danach das Netzteil ein und ist dort darum eine Halte-Taste von 2s. Ohne Bestätigung innerhalb von 60s endet der Lauf mit NICHT BESTÄTIGT.

![Die Handgriffe nach dem Programmieren, am Ende eines Laufs](view:motor-stick-hand-after)

Handgriffe nach dem Programmieren erscheinen am Laufende.

![Der Lauf wartet auf die Bestätigung des ESC](view:motor-stick-hand-end)

Ein Kontronik-ESC bestätigt einen gespeicherten Modus mit Tönen. Fällt die Versorgung vor Ende der Bestätigung aus, sperrt er sich. Nach dem Speichern hält der Lauf die Versorgung und die Gasposition des Speichervorgangs bis ERLEDIGT aufrecht.

![NICHT BESTÄTIGT: der ESC kann gesperrt sein](view:motor-stick-hand-locked)

Geht das Netzteil aus, bevor der ESC bestätigt hat, markiert das Ergebnis die Änderung als UNKLAR und sagt, dass der ESC gesperrt sein kann.

## Verwandte Seiten

- [Die Bildschirmhilfe](band.md#die-bildschirmhilfe): "?" im Band öffnet die Hilfe dieser Seite.
- [Motor und ESC](motor.md#programmieren): PROGRAMMIEREN und der Ausgangsstreifen.
- [Stick-Programmierung](https://github.com/subtilitas/rcbench/blob/main/docs/StickProgramming-de.md): Profile, Zeiten und jede Regel eines Laufs.
