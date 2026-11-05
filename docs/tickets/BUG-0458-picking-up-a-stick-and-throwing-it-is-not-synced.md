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
- 2026-11-03: not started on fix/w41-creature-left (K1 batch): the time box went to BUG-0179, BUG-0482, BUG-0517 and the probes of BUG-0496 / BUG-0504. The plan of docs/re/creature.md#creature-hold-slots stands; it needs a command that makes the avatar pick up a save object (no test path carries yet).
- 2026-11-04: not started on fix/w42-creature-left (R3): the time went to BUG-0525 and BUG-0532; a rock / stick needs a moving host-owned worldobj kind, and the Space branch of this batch adds worldobj kinds (merge risk). Plan unchanged: docs/re/creature.md#creature-hold-slots.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): not started (the time went to BUG-0501, BUG-0558, BUG-0496, BUG-0557).
- 2026-11-05 (fix/w44-guest-player3, P2b of 0.7.8): not reached (the time went to BUG-0600 and BUG-0465).
