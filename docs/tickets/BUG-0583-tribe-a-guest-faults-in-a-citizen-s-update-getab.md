---
id: BUG-0583
title: Tribe: a guest faults in a citizen's Update (GetAbilityIndexByType) after leaving and joining again
status: open
kind: crash
stage: Tribe
reported: 2026-11-04
source: lab (fix/w43-realign), realign_tribe_rejoin
role: guest
versions: 
fixed_in:
branch:
commits:
tests: realign_tribe_rejoin (coop_realign_planet.py --case rejoin)
signature: 0xC0000005 at 0x00C0C20D cCreatureBase::GetAbilityIndexByType+0x2D (data 0x8), calls 00C1CF47 00C2158D 00C24D82 00CDF949
docs:
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
Not analysed. Inferred: a citizen without its ability data (as BUG-0172's member added during a caption), a mirror
spawned into a tribe of the reloaded world before its species' records were there.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
