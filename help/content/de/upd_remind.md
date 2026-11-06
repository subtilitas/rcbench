---
screen: UPD_REMIND
title: Release-Erinnerung
order: 65
---

# Release-Erinnerung

Die Erinnerung meldet ein neueres Release der Panel-Firmware auf GitHub. Sie öffnet sich im [Menü](overview.md), wenn die Suche beim Start eines findet. Mit ERINNERUNG BEIM START unter [Updates](updates.md) schaltest du sie ein oder aus. Ab Werk ist sie eingeschaltet.

![Die Erinnerung](view:upd-remind)

| Bedienelement | Funktion |
|---|---|
| JETZT NICHT | schließt die Erinnerung bis zum nächsten Start des Panels; das Home-Feld hat dieselbe Wirkung |
| NICHT MEHR ERINNERN | schaltet ERINNERUNG BEIM START aus und fordert das Speichern an; unter SETUP > UPDATES kannst du die Erinnerung wieder einschalten |
| 2s HALTEN ZUM LADEN & INSTALLIEREN | startet dieselbe Aktion wie unter [Updates](updates.md#der-download) |

## Wann die Suche läuft

Bei eingeschalteter Erinnerung und gespeichertem Heimnetz sucht das Panel einmal nach jedem Start. Es wartet, bis der Splash geschlossen, der Prüfstand SAFE und der Netzteilausgang aus ist. Außerdem darf kein Aufzeichnungslauf offen sein. WI-FI muss geschlossen sein, und es darf kein IOmcu-Update angeboten werden oder laufen. Sind diese Bedingungen innerhalb von 10min nach dem Start nicht erfüllt, entfällt die Suche für diesen Start.

Die Erinnerung wartet, solange der Prüfstand ARMED ist oder eine ARM-Anfrage aussteht, der Netzteilausgang eingeschaltet ist oder eine Rückfrage beziehungsweise Fortschrittsanzeige zum IOmcu-Update geöffnet ist. Sobald diese Bedingungen entfallen und das Menü sichtbar ist, öffnet sich die Erinnerung.

Scheitert die Suche oder findet sie kein neueres Release, erscheint keine Erinnerung. Das Ergebnis steht unter [Updates](updates.md).

## Verwandte Seiten

- [Updates](updates.md)
- [Panel-Update](update.md)
