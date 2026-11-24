---
id: BUG-1526
title: Creature: neither player sees the partner's health, and a healing effect stays on for good
status: fixed
kind: bug
stage: Creature
reported: 2026-11-23
source: discord
role: guest
versions: 0.7.15.5
fixed_in: 0.7.17
branch: fix/w82-creature
commits: e24afb9a, 9a4c9bc3
tests: tools/tests/coop_creature_tag_hp.py (creature_tag_hp, twin creature_tag_hp_off)
signature:
docs: docs/re/creature.md#creature-meter-hold
related: BUG-0373, BUG-1472
switch: creature.tag_hp
---

## Report
Player report on Discord 2026-11-22 (coop 0.7.15.5, Steam; out/research/dc-report-20261122-world-switch.md section 5, the guest's own marks after the world switch): mark 4: the guest does not see the host's HP, the healing effect stays forever (perhaps only visibly), the host does not see the guest's HP. No matching ticket found with `tickets.py find`. Next: fix-player-report (reproduce with a test that fails first).

## Root cause
* Health (observed in the code): the game shows a creature's health in its rollover, and puppets.cpp h_HoverFilter lets
  the hover pass through a partner's creature (the pass-through in the combat stance, the social hover): no player
  could ever see the partner's health, though the puppet has it (creature.partner_hp, BUG-0373).
* Healing effect that stays: not found. The report's logs have no effect lines (effects log off); the player's own
  effects' stops go past the limit since 0.7.15.2 (BUG-1252), the host's NPCs' since this branch (BUG-1516); the
  pack's regen (teamplay.cpp) sets the health without an effect. Not reproduced.

## Fix
coop.cpp: a partner's name tag in Creature carries its health as its own game has it (its state's health / max:
`Name  8/10`), so both players see it (the setting all.name_tags still hides it). Switch `creature.tag_hp` (off: the
name alone, 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: creature_tag_hp, creature_tag_hp_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_tag_hp --lane N --retry 0 --no-build` (each tag ends with the partner's own game's health;
  key frame tag_hp).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_tag_hp_off --lane N --retry 0 --no-build` (the name alone).
* The healing effect part: no-fail-run: not reproduced (no effect named in the report).
0.7.17 release lab: PASS: creature_tag_hp (lab 20261124-020506).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-C1), branch fix/w82-creature.
- 2026-11-23: the health part fixed on fix/w82-creature (e24afb9a); the healing effect not reproduced; waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
