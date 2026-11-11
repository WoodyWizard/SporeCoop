---
id: BUG-0918
title: the avatar's combat target names a destroyed creature (a partner's puppet, a proxy): a combat key uses freed memory
status: fixed
kind: crash
stage: Creature
reported: 2026-11-11
source: found by analysis of BUG-0917 (lab 20261111-030815)
role: host,guest
versions: 0.7.x
fixed_in: unreleased
branch: fix/w47-target-gap
commits: 5dc9698c
tests: avatar_stale_target
signature: Ability_Use 0xD1E930 / Ability_TargetGap 0xD99A20 on avatar +0xE7C (no player crash report known)
docs: docs/re/creature.md#creature-avatar-stale-target
related: BUG-0917, BUG-0453, BUG-0456, BUG-0512
---

## Report
No player report. Found while analysing BUG-0917 (a test command faulted on the avatar's stale combat target): the
game's own attack reads the same stale field, so a player can hit it. The way: in the Creature stage a player's
target is a creature that then leaves that player's game: the partner's creature (a click on it makes it the target)
when the partner dies, hides or leaves, or (a guest) an animal the host's game removes. The next attack key uses the
target that is gone.

## Root cause
Observed:
* The avatar's combat target is the field +0xE7C (the target's combatant). The game's ability use (`Ability_Use`
  0xD1E930, a combat key) reads it, casts it (0xAD2610: the combatant's vt 0x5C) and measures the gap to it
  (`Ability_TargetGap`); nothing checks that the target lives (disassembly, docs/re/creature.md#creature-avatar-stale-target).
* The game does not clear the avatar's target when the creature is destroyed (it does for NPCs,
  docs/re/creature.md#creature-lost-target): lab 20261111-030815 and 20261111-031056, the host's avatar with the
  partner's puppet as its target, the puppet removed as on the partner's death: the field named it for the whole
  1.5 s watched in 8 of 8 rounds each. The puppet's memory is freed: its address was the address of a later puppet.
* 12 presses of the combat key on the stale field in each of those runs: no fault (the freed block still held the
  creature). The same read through the test command faulted twice in long sessions (BUG-0917: the block was
  reused; a jump to 0x3F800000, a read at 0x2C).
Inferred: that a player's key press crashes the same way once the block is reused (the same function on the same
pointer; not made to crash on purpose). No crash report with this signature is known (`tickets.py find`).

## Fix
mods/coop/gamefixes.cpp, the destroy hook (cGameNounManager::DestroyInstance, `destroy_guard`): before a creature is
destroyed (by the game or the mod), an avatar whose target field points into it loses its target through the
creature's own SetTarget (vt 0x84 with null: the reference is released while the target still lives). Creature
stage only. Counter in `coop.status` ("avatar targets cleared"), a log line per clear (the first 16).
Switch: `coop.fix game.avatar_stale_target off`.

## Verification
Test `avatar_stale_target` (tools/tests/coop_avatar_stale_target.py, lane 9): a real click on the partner's puppet,
`coop.puppet respawn 3`, then the game's target field is read from memory.
* FAIL: out/lab/20261111-031056 (`SPORE_FIX_OFF=game.avatar_stale_target`): "the game's target field of the avatar
  did not name the removed puppet 0.3 s after the removal (8 of 8 rounds did)". Also out/lab/20261111-030815 (before
  the fix existed).
* PASS: out/lab/20261111-031225 (the field clear 5-20 ms after the removal, the first read; 8 clears logged),
  out/lab/20261111-031354.
Play runs with both fixes (fix/w47-target-gap, `SPORE_DETECTOR=20`, no fault in any game log): play_creature x3 on
lane 9: out/lab/20261111-031649, out/lab/20261111-032734, out/lab/20261111-033823 (all OK; the avatar's target was
cleared at a destroy 1, 3 and 2 times, the reach command met no stale target any more); long_creature_short x2 on
lane 10: out/lab/20261111-031658 (both OK, 4 clears). Before the game fix, play_creature out/lab/20261111-025527
(the guarded command only): OK, one stale target met and reported as no target.
The full long_creature (lane 10, 45.9 min, the test that failed): out/lab/20261111-034946 OK, no fault, 3 clears.
Related tests on lane 9 (all OK): creature_kills out/lab/20261111-035019, npc_lost_target out/lab/20261111-035136,
team out/lab/20261111-035316, avatar_stale_target out/lab/20261111-035408. The campaign was not rerun.

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-target-gap (game.avatar_stale_target).
