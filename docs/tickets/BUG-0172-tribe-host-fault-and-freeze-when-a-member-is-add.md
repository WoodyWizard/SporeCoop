---
id: BUG-0172
title: Tribe: host fault and freeze when a member is added during the game-over caption
status: not-reproduced
kind: crash
stage: Tribe
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: host
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch: fix/w1-tribe-civ
commits: ba9dc46
tests: tribe_gameover
signature:
docs: docs/re/tribe.md#tribe-lifetime
related:
switch: tribe.gameover
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* When the player tribe dies out the game shows "Alas! Your population has fallen below the
  minimum needed to survive ... Bounce back in time" and waits for OK (the bounce back reloads the
  last save; with co-op the guest follows, checked 3x in play_tribe work, 2026-10-23). If a member
  is added to the player tribe while that caption is up, pressing OK faults at **0x00C0DAE3**
  (in 0xC0DAD0, a creature method reading `this+0xB54` and the species `this+0xB20`, both null for
  the new member; callers 0xC09CAE in cCreatureAnimal::Create, 0xC0AB99, 0xC0E74D, 0xC1CEAB,
  0xD410F8) and the host stops drawing for good (2 of 2 tries).
* Reproduce (lane N): host kills its own members through the damage path
  (`coop.tribe pincer <own id> <enemy id> 300`, a few times each, until only the knocked-out
  chieftain is left and the caption shows), then `coop.tribe spawn 2`, then `key enter` on the
  host. Without the spawn the bounce back works (frames go on, both machines have the same 3
  members afterwards).
* Only the test command adds members at that moment today; a guest adds mirrors (SpawnMember)
  whenever the host has members it lacks, so a guest that shows its own game-over caption while
  the host already bounced back could hit the same path (not seen: the guest showed no caption in
  4 tries, once it did while the host had none left alive).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not reproduced on GOG 2024; a guard added anyway (fix/w1-tribe-civ, ba9dc46): `spawn_member` adds no
member to the player tribe while none of its members has health left (the game-over state, the
caption comes a few seconds later); this covers the guest's mirror spawns and the test command.
`coop.tribe fix gameover 0` restores the old way; `coop.status` counts `spawns_blocked`.

## Verification
Two-game test `tools/tests/coop_tribe_gameover.py` (suite `tribe_gameover`): the host knocks out
every member of the player tribe, waits for the caption (`coop.ui find "Alas"` finds it, window
0x0445C9AB), `coop.tribe spawn 2`, OK, then the host must keep drawing, bounce back to members with
health, and no game may fault.

- With `--nofix` on GOG 2024 it PASSED 4 of 4 times (out/lab/nofix-20261027/nofix_tribe_gameover.log,
  nofix2_, nofix3_, nofix4_): the spawn went through ("spawned 2"), OK bounced back, no fault. The
  2026-10-23 tries (2 of 2 faults) were probably on the GOG 2016 lanes (inferred).
- With the fix: PASS, lab runs 20261027-184454, 20261027-185915 (2x), "spawned 0", `spawns_blocked=1`.

So the test is a regression test for the scenario, not a proof that the guard fixes the fault.

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: not reproduced on GOG 2024 (4 tries with the old code); guard and test added on fix/w1-tribe-civ (ba9dc46).
- 2026-10-28: switch: tribe.gameover (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
