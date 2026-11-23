---
id: BUG-1460
title: Tribe: a guest's game faults 38 times at 0x00C8F6D3 (game function 0xC8F6D0 called on null: the lookup 0xC230B0 returned none, call at 0xC286B1) during a raid (campaign_tribe_noclick, caught)
status: open
kind: bug
stage: Tribe
reported: 2026-11-23
source: lab
role: 
versions: 0.7.16
fixed_in:
branch:
commits: 1a8e910f
tests: campaign_tribe_noclick (tools/tests/coop_campaign.py)
signature: dc04aee5:0x00C8F6D3 read 0x0 (ecx=0), function 0xC8F6D0 called at 0xC286B1 with the result of 0xC230B0
docs: docs/re/tribe.md#tribe-makeway-null-tribe
related: BUG-0505, BUG-0942, BUG-1091, BUG-1169
---

## Report
Validation run of 0.7.16 (lab 20261123-023643, rel/0.7.16 fd28f88f, mod code 5376d388, lane 2):
campaign_tribe_noclick "FAIL fault tribe/advance: Bob: 38 new caught fault(s) (total 38)" (and, separately, "the
stage change tribe -> civ did not finish in 38 min (goal bar (3.0, 5.0))"). Passed in the nightly 20261122-194210 on
the same mod code.

## Root cause
Not known yet. Bob's (guest, 4 players) log, the only exception line (SporeKit logs a fault address once):
`03:09:42.182 [s 1967.019] exception 0xC0000005 at 0x00C8F6D3 SporeCoop::cTribe_PopulationCount+0x1A3 (data
0x00000000) ... eax=0 ecx=0`, `called from: SporeCoop::cTribe_ToolLayout+0x836 < ? < ? < ? <
SporeCoop::AnimatedCreature_SetEpicFlag+0x4A0 < Anim__AnimatedCreature_ctor+0x2455` (stack scan, the later frames may be
stale). The symbol names are only the nearest ones: 0x00C8F6D3 is the 2nd instruction of a separate function at
0xC8F6D0 (`sub esp,0x10; mov eax,[ecx]`: a thiscall on a null object), called at 0xC286B1 with this = the result of
0xC230B0(esi, ...) (a lookup that returned none). Context: 2 s after `pincer attack: +50% damage` (the tribe co-op raid),
1.7 s before the mod's epic event (`an epic creature comes!` 03:09:43.9 on all games), during the host's raid on
village 2. First time in every kept lab log of every worktree (grep 0x00C8F6D3). Caught: Bob's game went on and
reached Civilization in the same session (summary: stages Creature > Tribe > Civilization).
The stuck Tribe end is the known flake of this test (BUG-1091 / BUG-1169 class: "the host's click: no member selected
(7 own)", villages 2 and 5 untouched at 750/750 and 1000/1000 for 4 raids; 9 of 14 failed on rel/main), not this fault.

CORRECTION (2026-11-23, static RE with objdump on the GOG 2024 exe, docs/re/tribe.md#tribe-makeway-null-tribe): the
symbol names in the log line and the stack scan were the nearest ones only. 0xC230B0 is `Citizen_GetTribe` (verified:
`mov eax,[ecx+0x1014]; ret`, the citizen's owner tribe, null for a citizen out of its tribe); 0xC8F6D0 is
`cTribe_PostStimulusNear` (verified, thiscall(tribe, pos, radius, stimLo, stimHi, value, sender), ret 0x18: posts a
behaviour stimulus to the tribe's members near pos; faults on its 2nd instruction with a null tribe); the call at
0xC286B1 is in `Creature_AskGroupToMakeWay` 0xC28530 (likely), called only by `GroupLocomotion_Step` 0xC2D380 (likely,
LocomotionStrategy vt +0xC), from the physics locomotion step 0xC43300 for every moving creature: a citizen that walks,
is blocked on its path and has +0x1014 = 0 makes it dereference the null tribe. Not a raid order, a tool or the epic
(the SetEpicFlag / ctor frames were stale stack entries).
Mod path (code verified; that it made this fault is inferred, the faulting citizen 0x29444530 is not in the log):
tribe_mirrors.cpp guest_tick, the Death branch: a dead member's mirror is taken out of its tribe (`detach_body`,
tribe.cpp, +0x1014 = null) and then still driven by `mirror_step` every tick (drive::step issues game::move_to when its
plan says walk; the last MoveTo is never ended, the mirror's behaviour tree is skipped on the guest): a walking,
tribe-less body that gets blocked runs into this. Log: `bodies_detached` 72 -> 77 between 03:09:15 and 03:09:55 while
`removed` stayed 75 (five detached bodies alive at the fault), 2 s after the raid's "pincer attack" (members dying in a
crowd); no SetTribe(null) of the game itself on that guest. The branch is from 0.7.15 or older (b7e3706c, before
v0.7.15): not a 0.7.16 regression.

## Fix
Made for 0.7.16.1 (2026-11-23, fix/w65-mod), both parts (the proposal as below):
(a) `coop.fix tribe.body_still` (tribe_mirrors.cpp, the Death branch): after detach_body a body is stopped once
(game::stop, then put back where its host body lies: StopMovement can put a creature back up to 3 m) and no longer
driven by mirror_step (no MoveTo for a body).
(b) `coop.fix tribe.makeway_null`: a hook on `SporeCoop::Creature_AskGroupToMakeWay` (cdecl(ctx, creature), plain
ret) that returns false for a citizen without a tribe (+0x1014 null) and calls the original otherwise: stops the whole
class on host and guest. (b) is the guaranteed stop of the fault, (a) the behaviour fix.
Test idea: a guest's raid with deaths in a crowd, the detached bodies' moves logged (spatial 0xC42020 +0x5C, +0x330
bits); with both switches off the fault must come back (not deterministic: the body must be blocked).

Code: tribe_mirrors.cpp guest_tick's Death branch (tribe_logic::body_still, GMember::body_stopped; game::stop once,
game::teleport when over 1 m from the host's body, no mirror_step); tribe.cpp h_MakeWay on
`SporeCoop::Creature_AskGroupToMakeWay` (now verified: prologue `sub esp,0x28`, plain ret, the caller pops 8 bytes and
tests al), tribe_logic::makeway_skip, read-only, an atomic counter (any thread). Status: `coop.tribe` prints
`tribe bodies: still fix=1 stilled=N | makeway_null fix=1 skipped=N`. Unit test tests/tribe_tests.cpp body_rules (FAILS
with --nofix).

## Verification
Open, waiting for the lab run. No deterministic lab case: the fault needs a detached body that walks AND is blocked on
its path (Creature_AskGroupToMakeWay's +0x330 bits); a check that the guest's body of a member killed while walking stays
within 1 m of the host's body (tribe_dead_player with a walk order before the kill) would show part (a) only, not
written yet. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_dead_member tribe_dead_player tribe_join_body
tribe_body_teardown tribe_body_menu --lane N --retry 0 --no-build` PASS (the body paths), `campaign_tribe_noclick
--repeat 3` without 0x00C8F6D3 and with `tribe bodies: ... skipped=` in the guests' coop.tribe.

## History
- 2026-11-23: opened (fix/w65-tests triage).
- 2026-11-23: fix/w69-triage2 (0.7.16.1 final check triage): campaign_tribe_noclick#2 of 20261123-111750 stuck at goal bar 1/5 is not this fault: no exception line in any of its 12 game logs (no 0x00C8F6D3, faults 0); the host's raid click failed (BUG-1535, the BUG-1091 driver class, also before 0.7.16.1). The makeway_null skipped= count is only in coop.status, not logged: unknown for that run; the campaign now keeps coop.status with each finding.
