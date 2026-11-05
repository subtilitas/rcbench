---
screen: UPD_REMIND
title: Release reminder
order: 65
---

# Release reminder

The reminder announces a newer panel firmware release on GitHub. It opens on the [menu](overview.md) when the startup check finds one. REMINDER AT START on [Updates](updates.md) enables or disables it; it is enabled by default.

![The reminder](view:upd-remind)

| Control | What it does |
|---|---|
| NOT NOW | closes the reminder until the next start of the panel; leaving by the HOME tag does the same |
| NEVER REMIND ME | switches REMINDER AT START off and asks for the save; SETUP > UPDATES switches it on again |
| HOLD 2s TO DOWNLOAD & INSTALL | the same hold as on [Updates](updates.md#the-download) |

## When the check runs

With the reminder enabled and a saved home network, the panel checks once after each startup. It waits until the splash has closed, the bench is SAFE, the supply output is off, no log run is open, WI-FI is closed, and no IOmcu update is offered or running. If these conditions are not met within 10min of startup, the check is skipped for that startup.

The reminder waits while the bench is ARMED or an arm request is pending, the supply output is on, or an IOmcu update question or progress screen is open. It opens when these conditions have cleared and the menu is visible.

If the check fails or finds no newer release, no reminder appears. Its result is available on [Updates](updates.md).

## Related pages

- [Updates](updates.md)
- [Panel update](update.md)
