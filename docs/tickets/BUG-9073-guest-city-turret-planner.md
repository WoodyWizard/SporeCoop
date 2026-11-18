---
id: BUG-9073
title: Guest city turret purchases and sales diverge from the host wall and treasury
status: open
kind: desync
stage: Civilization
reported: 2026-11-06
source: Linked founder native city planner
role: host guest
versions: duo16-dev
fixed_in:
branch: feat/lineages-civ-combat
commits:
tests: tools/tests/duo_civ_wall_planner.py
signature: Guest planner charged 2000 on pickup and left the host wall empty
docs: docs/DUO.md
related: BUG-9054 BUG-9072
---

Two local games loaded the disposable Clyth campaign with four reserved seats.
On duo16, dragging the guest founder's turret onto a wall slot created the
turret only in the guest city. Selling it removed the local turret without
returning the 2,000 Sporebucks charged when the guest picked it up.

The native planner debits on pickup, assigns wall slots during hover, and
settles the edit after release. Its positive budget adjustment shares the
native AddMoney entry point with passive income. Ordinary buildings use a
different placement callback.

The active build sends the guest's completed wall edit to the host. The host
checks world, city, ownership, wall slot and native turret identity before
settling the price or refund. Moving a turret retains its native identity.
Slot snapshots repair the guest wall after conflicting edits. The frozen
executable lacks the verified planner hooks.

Two games on Clyth agreed on the slot and the 2,000-Sporebuck purchase and
resale. An invalid drop left both walls empty. Moving the turret retained
its host serial and HP. Closing the guest while hovering over a second slot
left only the committed turret on the host. The packaged DLL restored that
same serial, HP and slot on a cold host and showed it to a fresh guest.
