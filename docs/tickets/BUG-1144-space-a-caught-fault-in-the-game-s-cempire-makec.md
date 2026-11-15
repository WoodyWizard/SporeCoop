---
id: BUG-1144
title: Space: a caught fault at 0xC32817 (reported as cEmpire_MakeContact+0x3C7) when an NPC ship would visit the guest right after it saved with an allies mirror nearby (space_allies, once in 4 runs)
status: fixed
kind: crash
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-space2
commits: e3c7441b
tests: space_allies_near, space_allies
signature: 0xC0000005 at 0x00C32817 SporeCoop::cEmpire_SpawnVisitorUFO+0x7 (before: "cEmpire_MakeContact+0x3C7"), data 0x000000B8, ecx=0, called from 0x10091D6 (cSimulatorSpaceGame_SpawnVisitor)
docs: docs/re/space.md#space-near-stars, docs/archive/known-issues-2026.md#issue-space-makecontact-fault
related: BUG-1133, BUG-0838, BUG-0225
switch: allies.near_rebuild
---

## Report
space_allies on feat/w51-space, run 20261115-030136 (lane 6, cold): after the host left, the guest went to the galaxy
menu and loaded its own save (Game1/Madas); during the planet populate of that load the game faulted (caught, the test
went on and every check passed; the lab fails it on the fault). Guest log: exception 0xC0000005 at 0x00C32817
SporeCoop::cEmpire_MakeContact+0x3C7, ecx=esi=edi=ebp=0, called from PlanetPopulate_ForStage+0x8BE, tid main.

## Root cause
Observed (objdump of GOG 2024 + runtime on lane 6, confidence high). CORRECTION of the report above: the fault was not
in the load of the guest's own save and not in MakeContact. It came at 03:12:07.09, in the guest's home system, 2.7 s
after the guest's save during the session (h_Save at 03:12:04.37 unmirrors and holds the mirror for 3 s), before the
host left. SporeKit named it by the nearest symbol: MakeContact 0xC32450 ends at 0xC32745; 0xC32817 is the first
memory read (`cmp [edi+0xB8],0`) of another function, 0xC32810, called with ecx = 0.
* The caller (return 0x10091D6) is the NPC visitor spawn 0x1009070 of the system update 0x1009C90: with a chance of
  about 6 % a second it picks a random star of the near list `[cSimulatorSpaceGame+0x30]+0x7C` (stars around the
  active one with flag 0x10 or 0x20, i.e. owned), skips the player's, and calls 0xC32810 on
  `cStarManager::GetEmpire(star+0x54)` with no null check. The list is rebuilt only on the game's star messages
  (0xFE6A50); the game itself never lists a star nobody owns.
* space_allies.cpp `unapply()` (a save: for 3 s; `allies remove`; the session's end) gives the mirror's star back to
  nobody (+0x54 = -1, the flags without 0x10 / 0x20) and erases the mirror's cEmpire, while the list (filled when the
  guest visited that star with the mirror applied) still holds it: GetEmpire(-1) = null -> the fault.
* Probe (lane 6, scratch script, not in git): at home only the home star is listed; after a visit to the mirror's star
  and back both (the mirror star with owner 800021CE); `coop.space allies remove`: the entry has owner FFFFFFFF, and the
  same fault (0x00C32817, data 0xB8, the star record on the stack) came 10 s later.
* Why one in four: space_allies visits the colony (lands there, fights the raid), flies home and saves; the fault needs
  the visitor chance to fire inside the 3 s hold and pick the mirror's star (1 of 2): about 1 - e^(-0.18 / 2), a few
  % per run by that estimate, more with a slower frame time. After the session's end the same list holds the star
  until the next rebuild; space_allies leaves to the menu within a second, a player would stay.

## Fix
`allies.near_rebuild` (space_allies.cpp): after `unapply()` restored stars, the near list is rebuilt by the game's own
0xFE6A50 (`SporeCoop::SpaceNearStars_Rebuild`, on the object `[cSimulatorSpaceGame+0x30]`, its vtable 0x1494398
checked; only in Space), as the game's message handler 0xFE6E40 does. The list then holds no star of nobody (the
mirror's star comes back to it with the game's next rebuild while the mirror is applied). `coop.space allies near`
prints the list (`stale=<n>`). Symbols: SpaceNearStars_Rebuild, SpaceNearStars_vftable, cSimulatorSpaceGame_SpawnVisitor,
cEmpire_SpawnVisitorUFO (the last makes the next report name the real function).

## Verification
- FAIL (SPORE_LAB_FIXOFF=space_allies_near=allies.near_rebuild): out/lab/20261115-041924 (lane 7): right after the
  save the list holds star 4587532 with owner FFFFFFFF, NO EMPIRE (stale 1).
- PASS: out/lab/20261115-041921 (lane 6), out/lab/20261115-042358 (lane 7): after the save and after the session's end
  the list holds only the home star (rebuilds=1, 2), no fault.
- REPEATS_PLACEHOLDER

## History
- 2026-11-15: opened.
- 2026-11-15: cause found (the near-star list, not MakeContact), reproduced on purpose, fixed on fix/w51-space2 (allies.near_rebuild).
