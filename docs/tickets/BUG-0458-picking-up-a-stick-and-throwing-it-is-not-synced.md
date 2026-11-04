---
id: BUG-0458
title: picking up a stick and throwing it is not synced (carried / thrown objects)
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-npc-anims
related:
---

## Report
Host's mark (s 1478): 'Picking a stick and throwing it in creature stage is not synced.' Carried and thrown world objects are local to each game. Class G4 (BUG-0421).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not fixed (fix/w40-creature-acts, 2026-11-03): observed in Ghidra only, see docs/re/creature.md#creature-npc-anims
(Picking up and throwing): the carry node 0xD84120 takes fruit, cRock (noun 0x2A8FB3F) and sort-3 interactive
ornaments (~ the stick); the carried / thrown object moves in each game separately. Inferred cause: carried objects are
not host-owned world objects (worldobj.cpp has no kind for them). Not reproduced at runtime (no test command carries).
fix/w40-misc (2026-11-03, Ghidra only, not fixed: the time box went to BUG-0506 and BUG-0511): the hold slots creature+0xE40 and HoldObject 0xC15B20 / ReleaseObject 0xC15EB0, the hook points for a relay, and a plan: docs/re/creature.md#creature-hold-slots.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
- 2026-11-03: analysed on fix/w40-creature-acts, not fixed (findings in the docs).
- 2026-11-03: hold slots and the hook points found on fix/w40-misc (Ghidra), not fixed.
