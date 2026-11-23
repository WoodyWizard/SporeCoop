---
id: BUG-1335
title: Tribe: a guest's game faults in CreateEffect+0x146 when a member just made chief gets its staff on a hand bone its skeleton does not have
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-20
source: lab
role: guest
versions: 0.7.16
fixed_in: unreleased
branch: fix/w59-tribe
commits: 617d7380
tests: tribe_hand_bone, tribe_hand_bone_off (coop_tribe_hand_bone.py); unit tribe_tests hand_bone (--nofix fails)
signature: dc04aee5:SporeApp.exe Simulator::cCreatureBase::CreateEffect+0x146 < Creature_StartBoneEffect 0xC17EC0 < SporeCoop::Citizen_SetHandheldItem+0x379 (guest, Tribe)
docs: docs/re/tribe.md#tribe-hand-bone, docs/06-testing.md#testing-tribe-hand-bone
related: BUG-0652, BUG-0906, BUG-1299, BUG-1319
---

## Report
Lab 20261119-214823 (the first 0.7.16 final check), late_species on lane 3: `guest faults: total faults: 1`,
`0xC0000005 at 0x00C15216 Simulator::cCreatureBase::CreateEffect+0x146 data=0x3E800140` in the guest's game (the live
one, after its restart), s 37.4, 0.65 s after it joined the host's Tribe world again. The frame was cut off and the
game went on. Not seen in any other run (all worktrees' lab logs searched).

## Root cause
* ✔ (log + Ghidra) EBP 0xAF00 = bone 64 * 700; CreateEffect indexes the body's bone array
  (`[[creature +0xB54] +0x17C] +0x2E4`, 700 bytes a bone) with no check but `bone != -1` and read a float (0.25) as the
  bone's pointer. The chain: our CreateEffect hook (the original call) < 0xC17EC0 < Citizen_SetHandheldItem 0xC250A0
  < 0xC26470 < cTribe_SetChieftain 0xC8FEE0 < `tribe::apply_leader` (tribe.leader_sync, BUG-0652): the mirror of the
  host's chief was made chief and got its staff (item 0x11) on its hand bone +0xFC4.
* ✔ (Ghidra) +0xFC4 comes from the species' limb table +0xE40 (0xC24270) when the citizen is made; nothing ties it to
  the body's bone array, which the model builds over the next frames.
* ~ (inferred) At the world's opening the guest spawned 2 mirrors (one born in the guest's line species, BUG-1299) and
  removed its own chief; 0.65 s later the mirror bound to the host's chief was made chief before its body had 65
  bones. The first join of the same test and tribe_gameover_reload2 ran the same flow (`leads its tribe here too (the
  leader was none)` 0.6 s after the removal) without a fault. Whether the born line species widens the window is not
  measured (tribe_hand_bone step 2 prints the hand bone and the bone count at a spawn).
* Not BUG-0906's hand sync: its lines came 0.27 s after the fault and worked (the triage's "then member #4's hand"
  was after the fault, a different member).

## Fix
`creature.fx_bone` (BUG-1335): (1) game.cpp's CreateEffect hook returns null without the game's call for a bone
outside the creature's bone array (`bone_rules::check`, the array there and the bone not in it; a body without its
model), as the game's own "no bone" does; its callers handle null (SetHandheldItem finds no attachment and returns);
counted, 10 log lines. (2) A tribe member's own calls of the mod wait until its body has its hand bone
(`tribe::hand_ready`: tribe.leader_sync's SetChieftain, tribe.held_item's attach, the hold's attach); the fields
re-apply next round. (3) An item whose effect was skipped is attached again once the bone is there
(`hand_repair_tick`, every game in Tribe, 30 s at most). Off: the game's call (the fault). Tests' commands:
`coop.tribe handbone [id]`, `hand_race [owner]`, `hand_plant <id> [item]`.

## Verification
Unit: tests/tribe_tests.cpp `hand_bone` passes, `--nofix` (the game's only check) fails 6 checks.
Lab: not run then (the 0.7.16 final check holds the lab): tribe_hand_bone must PASS, tribe_hand_bone_off must FAIL
(the planted call faults the guest), late_species must PASS again.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): tribe_hand_bone PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643; the twin tribe_hand_bone_off FAIL in 20261120-130608 (guest faults: 2); late_species PASS in the same four runs. Closed.

## History
- 2026-11-20: opened (fix/w59-tribe, triage package E of the 0.7.16 final check).
- 2026-11-20: code, unit test and lab test on fix/w59-tribe; lab runs pending.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
