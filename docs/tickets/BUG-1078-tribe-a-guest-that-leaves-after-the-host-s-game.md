---
id: BUG-1078
title: Tribe: a guest that leaves after the host's game over and reload faults in its own creature_attack event (DramaManager_Get+0xBAA, data 0x120)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: 
fixed_in: 0.7.14
branch: fix/w49-drama
commits: 4c33c54d
tests: tribe_attack_stale; tribe_bounce_world (then any Tribe test that starts warm after it)
signature: 0xC0000005 at 0x00D521BA SporeCoop::DramaManager_Get+0xBAA (data 0x00000120), calls 00D4D536 00D4E84B 00B5B915 00D522E1
docs: docs/re/tribe.md#tribe-attack-event-target, docs/06-testing.md#testing-w49-drama
related: BUG-0582, BUG-0975
---

## Report
Found by the lab's warm resets (fix/w49-warm, lab 20261113-103507, lane 7; no player report). tribe_bounce_world
(the host's game over and its reload, the guest follows) passed. In the warm reset of the next test
(tribe_member_air) the guest's game left the session (`coop.leave`); 5 s later, alone, its Drama Manager started
the creature_attack event it had wanted to start four times during tribe_bounce_world (`drama: this game would start
its own creature_attack event: not on a guest`) and the game faulted once (caught by the game's handler, the game
went on and rejoined):

    drama: creature_attack event starts (host or alone)
    exception 0xC0000005 at 0x00D521BA SporeCoop::DramaManager_Get+0xBAA (data 0x00000120)
    called from: DramaManager_StartEvent+0x56 < DramaManager_StartKind+0x110B < ... < DramaManager_Update+0x71

(the names are the nearest symbols: DramaManager_Get+0xBAA is code past the getter, inside the event start). The lab
failed tribe_member_air for it (`guest faults: total faults: 1`). Logs: out/lab/20261113-103507/
tribe_member_air.guest.game.log (line ~580), tribe_bounce_world.guest.game.log.

A player meets it when a guest leaves a Tribe session (or the host leaves) after the host's game over and reload,
while its own game holds a creature_attack event back.

Reproduce: `tools/lab.py run tribe_bounce_world,tribe_member_air --in-order --lane N` (the fault needs the held
event: the guest's log must have the "would start its own creature_attack" line during tribe_bounce_world).

## Root cause
Observed (Ghidra GOG 2024, the fault's registers, and the test's own reads; docs/re/tribe.md#tribe-attack-event-target).
The fault is in the creature attack event's start (`SporeCoop::CreatureAttackEvent_Start` 0xD51BE0, vtable
0x147B320 +0x30; the old symbol name DramaManager_Get+0xBAA was the nearest one): it reads `[place + 0x120]`, the
combatant of the attacked tribe's hut (or of its second tool when it has more than one), without a check; ecx 0x120
and ebp 0 in the report = no hut, at most one tool. The attacked tribe is the event's object property 0x5F11333, a
counted reference set by SpawnNpcTribe (`DramaManager_NoteNpcTribe` 0xD516B0: the Nth NPC tribe the game makes,
N picked at random by the event's setup). A guest's game makes NPC tribes for the host's new ones, so one of them can
become the target. After the host's game over and reload the re-alignment destroys the guest's tribes the host's
world lost (tribe::remove_village, DestroyInstance): the hut goes, the tribe object stays alive through the event's
reference. As a guest the event is held back (tribe.guest_attacks, the "would start its own creature_attack" lines);
once the game is alone (coop.leave, or the host leaving) it starts and faults. The test reproduces it exactly
(target set to the guest's tribe for the host's new one, the bounce, `coop.drama target` reads tribe alive=0
place=0, then leave + start: 0x00D521BA data 0x120). Inferred: in lab 20261113-103507 the game's own random pick had
landed on such a tribe (the test sets it with the game's own setter instead of waiting for the pick).

## Fix
drama.cpp (the DramaManager_StartEvent hook): before a kind 7 start in Tribe, an attacked tribe (0x5F11333) whose
place the start reads is null is set to null with the game's own setter (`SporeCoop::DramaEvent_SetObject`
0xD4ED80, which releases the reference); the start then takes its other branch (the tribe list at its picked index,
or nothing). One check at the only place that reads it, for every way a tribe loses its hut under the event (not only
the re-alignment). Log: `drama: the creature attack's village ... is gone ... forgotten before its start`. Switch
`coop.fix tribe.attack_target off` (0.7.13 behaviour). `coop.drama target [<tribe obj>]` reads / sets the target
(tests). New symbols: DramaEvent_GetObject, DramaEvent_SetObject, CreatureAttackEvent_Start, DramaManager_NoteNpcTribe.

## Verification
tribe_attack_stale (new, lane 6): FAIL with `SPORE_LAB_FIXOFF="tribe_attack_stale=tribe.attack_target"`
20261113-124553 (the guest, alone: 0xC0000005 at 0x00D521BA CreatureAttackEvent_Start+0x5DA, data 0x120, ecx 0x120,
ebp 0); PASS with the fix 20261113-125057-2 and 20261113-125310 (`dropped=1`, no fault).
tribe_bounce_world is warm again (suite.py): `tribe_bounce_world,tribe_member_air --in-order`, lane 7, warm reset into
tribe_member_air: 20261113-125618 both OK, no fault; 20261113-125057 no fault, tribe_member_air failed its own
"the guest sees the throws" check (0 of 3 blows; the test fails now and then also cold: 6 of 13 recorded runs since
2026-11-11, twin runs included). In neither run had the game's random pick landed on a removed tribe (no BUG-1078 line): the warm runs show
no new fault, the proof of the fix is tribe_attack_stale. Regression (lane 6): tribe_gameover_reload and
tribe_bounce_mirrors OK, tribe_gameover warm after tribe_gameover_reload FAIL once ("the player tribe has members
again" after the bounce, no drama line) in 20261113-125618-2, OK alone 20261113-130729 and warm after
tribe_gameover_reload on lane 7 20261113-130900; play_tribe --quick OK 20261113-130108.

## History
- 2026-11-13: opened.
- 2026-11-13: root cause found and fixed on fix/w49-drama (tribe.attack_target), test tribe_attack_stale.
- 2026-11-14: released in 0.7.14.
