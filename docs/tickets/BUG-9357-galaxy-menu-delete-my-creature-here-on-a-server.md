---
id: BUG-9357
title: Galaxy menu: Delete my creature here on a server planet's bin
status: fixed
kind: feature
stage: galaxy
reported: 2026-11-23
source: owner
role: 
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 7f3d2bd0
tests: tools/qa_win (manual, the MSI, real clicks)
signature:
docs: docs/12-server-and-players.md#server-menu-gaps
related: BUG-9287, BUG-9290, BUG-9295, BUG-9337
---

## Report
Owner 2026-11-17: "when a player is on the Online Galaxy Menu, we should allow them to delete their creature on the
world still. Deleting their creature will let them rejoin that planet with a new one." The planet panel's bin on a
server planet opened Spore's own "Delete Game" box (a local save file of that name).

## Root cause
The bin (window 0x3C4925E) switches the menu to state 0xB, whose enter shows Spore's delete box for the entry
(docs/re/space.md#space-menu-pages). The player's creature on a planet is the record in its worlds.ini for that world
(5U A); a host whose upload made the world's species its own adopted that creature again on the next entry.

## Fix
The switch to state 0xB for a server planet is taken: our box ("Delete my creature", Cancel). The worlds this player
played the planet's eras in are remembered (coop.ini [galaxy_worlds], after 10 s in a stage); Delete forgets the
records there and the campaign creature for them (creations::forget_world) and marks the world (worlds.ini
[deleted]): its host makes a first creature there instead of adopting the world's species; the next creature
recorded ends the mark. The old nest stays for its family and goes to a new species with the nest rotation (7F).
Switches: `galaxy.delete_creature` (off: Spore's box), `creations.delete_mark` (off: the records only).

## Verification
The MSI (MSISpaceStart profile, its own creature on Testplanet01, real clicks): switch off, the bin opened Spore's
"Delete Game: Delete Testplanet01?" (cancelled); on, our box, Delete: "Your creature on Testplanet01 is deleted"
(record cleared). FAIL (before the mark, 19:39): Play on Testplanet01 adopted the old creature again (record back).
PASS (with the mark, 19:5x): Play on Testplanet01 opened the creature editor for a new creature (mode editor, record
empty). A planet with no creature: "You have no creature of your own on Testplanet02."

## History
- 2026-11-23: opened.
- 2026-11-23: built on feat/lineages-alpha (7f3d2bd0), seen in game on the MSI.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
