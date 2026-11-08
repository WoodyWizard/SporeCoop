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
tests: tribe_gameover, tribe_gameover_reload, tribe_gameover_reload2, tribe_gameover_slow
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
Not reproduced on GOG 2024 (0.7.11 N1, see Verification). Reasoning:
* Observed: with the guard off (`tribe.gameover`, the code before ba9dc46) the members go into the tribe that is
  out ("spawned 2", "spawned 6", also during the bounce back's reload 0.5 s and 2.5 s after OK) and the OK bounces
  back without a fault, 8 of 8 tries on GOG 2024 (4 on 2026-10-27, 4 on 2026-11-07 at 60 and 20 fps).
* The 2 faults of 2026-10-23 were at 0xC0DAE3, a GOG 2016 address (several lanes of that week ran GOG 2016: lanes 2,
  4, 7 and 8 in docs/06-testing.md's runs of 2026-10-23). Checked 2026-11-07 on the GOG 2024 exe: 0xC0DAD0 there is
  another function (it reads only +0xB54 and its argument); the reads described (+0xB54, then the species +0xB20) are
  GOG 2024 CreatureBase_SetMoveScale 0xC0DB10, and 0xC0DAE3 = 0xC0DAD0 + 0x13 matches 0xC0DB23 = 0xC0DB10 + 0x13
  (`movss xmm2,[ecx+0x3c]` through a null +0xB54): the BUG-0285 / BUG-0310 site. So the same fault exists on GOG 2024,
  reached there through Citizen_OffStageCheck (fixed by tribe.leave_walk). No GOG 2024 lab game log of
  2026-11-04..07 (all worktrees) has a fault at 0x00C0DB23.
* Inferred: on GOG 2024 the new member's +0xB54 is set by the time the caption's OK runs (8 of 8 without the guard);
  the guard (tribe.gameover) keeps the path closed for the guest's mirror spawns and the test command anyway.

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

0.7.11 N1 (fix/w46-n1-crashes, GOG 2024, lane 10), the guard off (SPORE_FIX_OFF=tribe.gameover), all OK, 0 faults:
out/lab/20261107-231709: tribe_gameover (the caption, 2 members), tribe_gameover_slow (6 members, 20 fps over the
player network), tribe_gameover_reload (2 members 0.5 s after OK, 3 times), tribe_gameover_reload2 (4 members 2.5 s
after OK, 3 times). The guard on: tribe_gameover (out/lab/20261107-223550), tribe_gameover_slow and
tribe_gameover_reload (out/lab/20261107-232504) OK.

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-27: not reproduced on GOG 2024 (4 tries with the old code); guard and test added on fix/w1-tribe-civ (ba9dc46).
- 2026-10-28: switch: tribe.gameover (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
- 2026-10-30 (fix/w13-tribe-civ): still not reproduced. All lab game logs of the main checkout (out/lab, 2026-10-22..30) searched for tribe-stage faults: none at this ticket's address in any tribe test; the only Tribe faults in campaigns are BUG-0285 / BUG-0310 (0xC0DB23, fixed by tribe.leave_walk). On this branch tribe_coop, tribe_play, tribe_far, chaos_tribe, menu_stale_player, campaign_tribe, tribe_leave_walk (3x) passed on lane 6 without a fault, freeze or crash (lab runs 20261030-141833, 20261030-140307). Status kept.
- 2026-10-30 (fix/w13-tribe-civ): cross-reference: the GOG 2016 fault 0xC0DAE3 in 0xC0DAD0 (reads +0xB54 and +0xB20 unchecked) matches GOG 2024 CreatureBase_SetMoveScale 0xC0DB10 (+0x13 = 0xC0DB23, BUG-0285 / BUG-0310) by its reads and its callers (cCreatureAnimal::Create, WalkTo; inferred, the 2016 bytes were not compared). Here the caller is the new member's creation, not Citizen_OffStageCheck, so tribe.leave_walk does not cover this path; the tribe.gameover guard still does.
- 2026-11-07 (fix/w46-n1-crashes, 0.7.11 N1): with the guard off at the caption, during the reload, 6 members, 20 fps and the player network: no fault on GOG 2024 (lab 20261107-231709); 0xC0DAE3 (GOG 2016) is 0xC0DB23 on GOG 2024 (the BUG-0285 site), not in any GOG 2024 lab log of 2026-11-04..07. Stays not-reproduced, closed with the runs.
