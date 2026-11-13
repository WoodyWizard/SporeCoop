---
id: BUG-1076
title: Tribe: a guest faults 9 times at 0xCE69F9/0xCE69FB (dc04aee5, data 0x3F400000 / 0x7C) while the host's players' hut falls in the campaign's Tribe end
status: open
kind: crash
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests:
signature: SporeApp.exe+0x8E69F9 / +0x8E69FB (0xCE69F9 / 0xCE69FB) read 0x3F400000 / 0x7C, caught
docs:
related: BUG-0672
---

## Report
fix/w49-tests full campaign, lab 20261113-082316 (lane 4), guest Dave (instance 34, 4 players, the campaign's own
planet Cadian): during the Tribe end's raids (09:18-09:24) Dave's game caught 9 faults, the first at 09:18:55
`0xC0000005 at 0x00CE69F9 SporeApp.exe+0x8E69F9 (data 0x3F400000)`, then 0x00CE69FB (data 0x7C), tid 772 (main),
called from (stack scan) App::Property::SetValueFloat+0x185D < FNV_hash+0x126 < GlobalUI_FindWindowByID+0x28E4.
Next lines in Dave's log: the guest ended the cinematic 94D17D9F (TRG_Upgrade, BUG-0983), a dead member 12 m over the
ground, "a hut falls here ... the hut at (155 -468 -104) fell in the host's game" (the players' own hut) and the
conquest of the village at (-4 -506 4). The campaign's `fault` findings caught it (the run fails).
Log: SPORE-lab/lane4 sporekit.34.log of that run (copied by the lab into out/lab/20261113-082316 when it ends).

## Root cause
Not analysed (outside the package). 0xCE69F9 lies just before 0xCE6AD0 (cPlanetRecord_GetPlanetID) and the species
footprint code of BUG-0672 (docs/re/tribe.md 2345); a species / planet record read on a freed or wrong object is the
inferred class.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
