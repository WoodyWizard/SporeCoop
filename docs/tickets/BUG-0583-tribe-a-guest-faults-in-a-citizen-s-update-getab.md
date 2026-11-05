---
id: BUG-0583
title: Tribe: a guest faults in a citizen's Update (GetAbilityIndexByType) after leaving and joining again
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-realign), realign_tribe_rejoin
role: guest
versions: 
fixed_in: 0.7.7
branch: fix/w43-crashes
commits: def4c4f0
tests: realign_tribe_rejoin (coop_realign_planet.py --case rejoin)
signature: 0xC0000005 at 0x00C0C20D cCreatureBase::GetAbilityIndexByType+0x2D (data 0x8), calls 00C1CF47 00C2158D 00C24D82 00CDF949
docs: docs/re/tribe.md#tribe-leader-tool
related: BUG-0328, BUG-0172
---

## Report
Found by the lab (no player report). realign_tribe_rejoin with `SPORE_FIX_OFF=wobj.tools_planet`, lane 7,
out/lab/20261104-212803: the guest left (`coop.leave`), joined again and loaded the host's world (s 51); 18 s
later its game faulted 1151 times (every frame) at 0x00C0C20D in GetAbilityIndexByType (data 0x8) from
cCreatureCitizen::Update -> cCreatureBase::Update. Before the rejoin the guest had made the host's 3 NPC tribes
(tribe.npc_tribes) and its first save of the session stripped them (tribe.save_strip, "31 objects of ours").
Not seen in 3 runs of the same test with every fix on (lab 20261104-212408, 20261104-212748).

## Root cause
* Observed (fault record, lab 20261104-212803): GetAbilityIndexByType's count 21 (EBX), index 20 (ESI) null, ECX = 1:
  the getter 0xC24E40 returned through its leader branch (0xC23430 leaves ECX = its result).
* Observed (Ghidra, docs/re/tribe.md#tribe-leader-tool): a leader with a tool of type 1..6 counts n + 8 abilities, the
  getter serves n + 7; the last is null.
* Inferred: the guest's mirror that led its tribe there got a weapon or instrument through the mirrors' tool field
  (tribe.tool_hold copies the host's member's; the host's member did not lead in the host's game). The game itself
  never gives a leader such a tool.

## Fix
`tribe.leader_tool` (tribe.cpp apply_held_tool): no tool of type 1..6 on this game's tribe leader (it keeps its
staff); `coop.tribe` lists tool= and leader= per member. Off: the host's tool as before.

## Verification
no-fail-run: not reproduced. Tried: realign_tribe_rejoin with `SPORE_FIX_OFF=wobj.tools_planet` twice (lab
20261105-001310: no fault); the probe tools/tests/coop_tribe_leader_tool.py (a formed tribe, with and without a rejoin:
no member led in the guest's game only, lab 20261105-002001, -002140; --direct with the fix off: the leader mirror
held a spear for seconds without a fault, lab 20261105-002402). The guard rests on the getter's code and the fault's
registers.

## History
- 2026-11-04: opened.
- 2026-11-05: cause from the fault's registers and the getter's code, guard tribe.leader_tool, not reproduced (fix/w43-crashes).
- 2026-11-05: released in 0.7.7.
