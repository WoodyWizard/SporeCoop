---
id: BUG-1434
title: After a role swap the new host plays from its co-op copy of the old host's galaxy (an old planet starts instead of the session's world), and the copy keeps the old host's marker, so the next share replaces or moves the new host's progress
status: open
kind: bug
stage: Creature
reported: 2026-11-22
source: discord
role: all
versions: 0.7.15.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Two players, Creature. After the first host left, the players swapped roles. The new host's game started an older planet of the first host's galaxy instead of the session's world (its Cell progress at 100 %). Later the new host's save of that game was missing. Full analysis: out/research/dc-report-20261122-world-switch.md in the main checkout (not in git; six SporeCoop-report zips, coop 0.7.15.5, Steam).

## Root cause
Confirmed (code + logs): a guest gets a copy of the host's whole galaxy, old planets too (world.cpp:241-250, world.cpp:1765-1773, slotfs.cpp:231-290); after the swap the new host's galaxy menu showed that copy, and nothing warned it (only slotfs reads the slot marker sporecoop.txt). Likely: the planet started was one of the first host's old planets in that copy. Inferred from code, not seen in logs: the new host's saves go into a slot whose marker still names the old host's planet, so the next share of that planet replaces the slot (slotfs.cpp:529-541, rmtree) or tidies it into Games-sporecoop-backup (slotfs.cpp:292-311). slotfs.cpp is unchanged from v0.7.15.5 to rel/0.7.16.

## Fix
Proposed: 0.7.16.1: a copy slot with a local save newer than its receipt is moved to the backup, not deleted (or its marker is dropped at the first own save); the host is told when it hosts from a copy of another player's galaxy and for which planet the copy was made. 0.7.17: share only the played planet's .spo and the galaxy's shared files (RE of GGEUserData.dat); a 'continue the last co-op world' entry.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-22: opened.
