---
screen: UPD_REMIND
title: Release reminder
order: 65
---

# Release reminder

The reminder says that a newer panel firmware release is on GitHub. It opens on the [menu](overview.md) after the check at start found one. REMINDER AT START on [Updates](updates.md) switches it; it is on by default.

![The reminder](view:upd-remind)

| Control | What it does |
|---|---|
| IGNORE NOW | closes the reminder until the next start of the panel; leaving by the HOME tag does the same |
| NEVER REMIND ME | switches REMINDER AT START off and asks for the save; SETUP > UPDATES switches it on again |
| HOLD 2 s: DOWNLOAD & INSTALL | the same hold as on [Updates](updates.md#the-download) |

## When the check runs

With the reminder on and a home network kept, the panel checks once after each start. It waits for the splash to be gone, the bench SAFE, the supply output off, no log run open, FILES & UPDATE closed and no coprocessor update offered or running. A check that finds no such moment within 10 min of the start does not run at that start.

The reminder does not open while the bench is ARMED or an arm waits, while the supply output is on, or while the coprocessor's update question or progress is up. It opens once those have gone and the menu shows.

A check that finds no newer release or fails shows nothing; its result is on [Updates](updates.md).

## Related pages

- [Updates](updates.md)
- [Panel update](update.md)
