---
id: BUG-0458
title: picking up a stick and throwing it is not synced (carried / thrown objects)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in:
branch: fix/w45-creature-reports
commits: COMMIT_PLACEHOLDER
tests: creature_carry (coop_creature_carry.py)
signature:
docs: docs/re/creature.md#creature-npc-anims, docs/re/creature.md#creature-carry-relay
switch: creature.carry_relay
related:
---

## Report
Host's mark (s 1478): 'Picking a stick and throwing it in creature stage is not synced.' Carried and thrown world objects are local to each game. Class G4 (BUG-0421).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (lab, fix/w45-creature-reports): the host's creature held the fixture's rock (the game's HoldObject): the
  guest's game showed the host's puppet with an empty hold slot and the rock where it lay; released, each game's physics
  threw and rolled its own rock (65-68 m apart: out/lab/20261106-002005, -004127 with the switch off). The fixture's
  creatures cannot pick the rock up by a click (cursor kind 0x1C), so the test calls HoldObject as the carry node does.
* Observed: HoldObject / ReleaseObject take the object's cSpatialObject and return a bool
  (docs/re/creature.md#creature-carry-relay).
Not fixed (fix/w40-creature-acts, 2026-11-03): observed in Ghidra only, see docs/re/creature.md#creature-npc-anims
(Picking up and throwing): the carry node 0xD84120 takes fruit, cRock (noun 0x2A8FB3F) and sort-3 interactive
ornaments (~ the stick); the carried / thrown object moves in each game separately. Inferred cause: carried objects are
not host-owned world objects (worldobj.cpp has no kind for them). Not reproduced at runtime (no test command carries).
fix/w40-misc (2026-11-03, Ghidra only, not fixed: the time box went to BUG-0506 and BUG-0511): the hold slots creature+0xE40 and HoldObject 0xC15B20 / ReleaseObject 0xC15EB0, the hook points for a relay, and a plan: docs/re/creature.md#creature-hold-slots.

## Fix
fix/w45-creature-reports (`creature.carry_relay`, player_rules.cpp, MSG_PLAYER_RULE kinds 4-6): the own creature's
Creature_HoldObject / ReleaseObject of a rock or ornament are sent; the partner's game holds its copy (the nearest
object of that noun within 6 m of the position both games last agreed on) in the hold slot of the carrier's puppet,
keeps it there after the release and puts it, with a held object's physics, at each point where the thrower's object
comes to rest or creeps (for up to 60 s; never within 4 m of its own creature). `coop.fix creature.carry_relay off`:
carried objects stay in the carrier's game (0.7.8). Not covered: the flight itself (seen in the puppet's hand until it
lands), the throw's damage, sticks together with the worldobj ornament state, fruit.

## Verification
creature_carry (coop_creature_carry.py): the host, then the guest, holds the rock, carries it to the nest and lets go;
the other game must show the carrier's puppet holding a rock within 2.5 m of it, and its rock within 1 m of the
thrower's (two samples in a row within 15 s of the rest).
- FAIL with `SPORE_FIX_OFF=creature.carry_relay`: out/lab/20261106-004127 (no rock in the puppet's hand; 68.44 m and
  22.65 m apart), 20261106-002005.
- PASS: out/lab/20261106-003953 (0.08 m / 0.05 m), 20261106-003813 (0.13 / 0.24 m); earlier builds of the fix failed on
  the copy's own physics (rolled 5-115 m: 20261106-002616, -002757) and crushed the guest's creature by repeated
  teleports (20261106-001427, -002414): see the docs.

## History
- 2026-11-02: opened.
- 2026-11-03: analysed on fix/w40-creature-acts, not fixed (findings in the docs).
- 2026-11-03: hold slots and the hook points found on fix/w40-misc (Ghidra), not fixed.
- 2026-11-03: not started on fix/w41-creature-left (K1 batch): the time box went to BUG-0179, BUG-0482, BUG-0517 and the probes of BUG-0496 / BUG-0504. The plan of docs/re/creature.md#creature-hold-slots stands; it needs a command that makes the avatar pick up a save object (no test path carries yet).
- 2026-11-04: not started on fix/w42-creature-left (R3): the time went to BUG-0525 and BUG-0532; a rock / stick needs a moving host-owned worldobj kind, and the Space branch of this batch adds worldobj kinds (merge risk). Plan unchanged: docs/re/creature.md#creature-hold-slots.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): not started (the time went to BUG-0501, BUG-0558, BUG-0496, BUG-0557).
- 2026-11-05 (fix/w44-guest-player3, P2b of 0.7.8): not reached (the time went to BUG-0600 and BUG-0465).
- 2026-11-06 (fix/w45-creature-reports, P3 of 0.7.9): reproduced in the lab; fixed behind creature.carry_relay.
