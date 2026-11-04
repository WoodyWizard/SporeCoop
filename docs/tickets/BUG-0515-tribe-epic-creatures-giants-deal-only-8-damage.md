---
id: BUG-0515
title: Tribe: epic creatures ('giants') deal only 8 damage
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: host
versions: 0.7.3.6
fixed_in: 0.7.5
branch: fix/w41-tribe-actions
commits: 521d849b
tests: tribe_epic_damage (coop_tribe_epic_damage.py)
signature:
docs: docs/re/tribe.md#tribe-epic-damage
related: BUG-0121
switch: tribe.epic_tuning
---

## Report
Host's mark (0.7.3.6, report 20261103-203858): 'in tribe stage giants only dmg 8'. The host log has 'an epic creature comes! together: 80 food' 30 min earlier and several 'pincer attack: +50% damage'. Which side's damage is meant (the epic's hits, or the tribe's hits on the epic) is not clear.
Target: 0.7.5 (Tribe).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (GOG 2024, lab lane 2, docs/re/tribe.md#tribe-epic-damage): the "8" is the co-op epic (the one the mod
sends, "an epic creature comes! together: 80 food") hitting the tribe's members, in the host's game (it simulates
every tribe; every player sees the members' health of the host). Its herd, made by CreateHerd with the epic
archetype only, has mDamageMultiplier (+0x158) 1.0; the planet's own epic's herd has 15.0. Measured blows of the
co-op epic on a member: 1.5 and 8.0 (an ordinary animal's).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`tribe.epic_tuning`: spawn_epic gives the co-op epic's herd the damage multiplier of the planet's own epic herd
(15 when the planet has none) before the epic is made. Blows now 120. The size (7.7-9.6 vs the game's 11.4) is not
changed.

## Verification
`coop_tribe_epic_damage.py` (suite `tribe_epic_damage`): spawns the co-op epic, puts it next to a member, reads
its herd and its blows (host TakeDamage).
* FAIL `SPORE_FIX_OFF=tribe.epic_tuning`: out/lab/20261104-000238 (x1.00 vs the game's x15.00, max blow 8.0);
  also out/lab/20261103-235907 (before the switch existed, the same 8.0).
* PASS: out/lab/20261104-000059 (x15.00, blows 120).

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage), needs the player's details.
- 2026-11-03 (fix/w41-tribe-actions): the co-op epic's damage multiplier was 1 (game's epic 15); fixed.
- 2026-11-04: released in 0.7.5.
