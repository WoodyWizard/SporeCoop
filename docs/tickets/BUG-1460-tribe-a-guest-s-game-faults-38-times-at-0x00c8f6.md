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
commits:
tests: campaign_tribe_noclick (tools/tests/coop_campaign.py)
signature: dc04aee5:0x00C8F6D3 read 0x0 (ecx=0), function 0xC8F6D0 called at 0xC286B1 with the result of 0xC230B0
docs:
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

## Fix
None yet. Next: name 0xC230B0 and 0xC8F6D0 (Ghidra), see which of the mod's tribe paths (raid / pincer, epic, member
tools of 0.7.16's own species) gives the lookup a key it does not know.

## Verification
Open.

## History
- 2026-11-23: opened (fix/w65-tests triage).
