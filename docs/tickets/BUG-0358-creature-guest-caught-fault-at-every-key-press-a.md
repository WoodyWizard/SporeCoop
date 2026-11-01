---
id: BUG-0358
title: Creature (guest): caught fault at every key press after the own creature swap, stale ability bar (dc04aee5:0xD4A397)
status: fixed
kind: crash
stage: creature
reported: 2026-10-31
source: lab 20261031-185843 campaign#2 (lane 4, 4 players, final 0.7.3.2 verification, main 55da78b)
role: guest
versions: coop 0.7.3.1 (main 55da78b), SporeKit 0.3.5, GOG 2024 dc04aee5
fixed_in: 0.7.3.2
branch: fix/w27-creature-fault
commits: 656a7c9, e10c2b0
tests: ability_bar (tools/tests/coop_ability_bar.py)
signature: dc04aee5:0xD4A397 read [esi+0xC], esi = 0x41900000 (AbilityBar_FindByKey 0xD4A320 <- AbilityBar_OnKey 0xD4A450 <- creature OnKeyDown 0xD3AFF0); crash_triage names it Simulator::CreatureStageEventHandler+0x23D7 (nearest symbol, not the function)
docs: docs/re/creature.md#creature-ability-bar, docs/06-testing.md#testing-ability-bar
related: BUG-0318 (the same SetSpecies swap), BUG-0346 (crash_triage names unknown functions by the nearest symbol)
---

## Report
Final verification lab run 20261031-185843, campaign#2 (cell -> creature -> ... -> space, 4 players).
During the 8 minutes of Creature play one guest (Bob) had 10-19 new caught faults every 30 s, 203
in all, every one `exception 0xC0000005 at 0x00D4A397 Simulator::CreatureStageEventHandler+0x23D7
(data 0x4190000C) eax=41900000 esi=41900000 ebp=00000009 ebx=000000A0 edi=00000031`, the same stack
(`calls: 00D4A512 00D3B045 00814EE1 ...`). Only one address in the whole log (the game log prints
the first one; `exc` counts them). The game went on, the campaign reached space. The other guests
and the host had none; no other lab run had this address. The guest was the only player with own
creatures in its profile: its avatar was swapped from the session's evolved species 29105272 to
its own 'Kirgle' (290225CD) 3 s after each load (`body: own avatar species 29105272 -> 290225CD`).

## Root cause
Observed (disassembly, docs/re/creature.md#creature-ability-bar):
* 0xD4A397 is not in CreatureStageEventHandler (the name is the nearest symbol) but in 0xD4A320
  (`AbilityBar_FindByKey`), reached for every key without a modifier in the creature stage:
  OnKeyDown 0xD3AFF0 -> 0xD4A450 -> 0xD4A320. It walks the HUD ability bar (global 0x169E3EC, 25
  slots of ability indexes) and gets each slot's ability from 0xD1E830, then reads `+0xC`. The
  faulting slot was row 4, column 0 (ebx 0xA0, edi 0x31 = key '1'), index 9 (ebp).
* 0xD1E830 returns the avatar's ability for an index below its ability count; above it reads the
  world species' ability array (`cSpeciesManager +0xA4` profile, `+0x6D4`) without a bound check.
  The returned 0x41900000 is a float (18.0) from past that array.
* The bar is filled by 0xD49400 at the HUD start from the avatar the game has then (the world's
  species). `cCreatureAnimal::SetSpecies` (the mod's own creature swap, creations.cpp
  apply_own_avatar, since 0.5.0) does not refill it.
Observed (lane 6, creature fixture, the test's prints): the guest's own creatures have 9 to 17
abilities, the fixture species 11 (bar highest index 7). A bar built for the 17-ability creature
followed by the swap to the 9-ability one leaves 5 of 11 slots past the abilities (fix off).
Inferred: in the campaign the evolved world species 29105272 had at least 10 abilities and the
guest's 'Kirgle' 9, so slot index 9 was past it; the fault needs non-zero garbage past the
species' array, which is why only this run faulted (the swap runs on every guest with own
creatures in every campaign). Not caused by the recent creature changes (BUG-0329 drive.own_height,
BUG-0330 team rescue, BUG-0334 npc.kinematic_sync): none of them touches the bar or the avatar's
species; the gap exists since the own creature swap (6fea8a9, v0.5.0). Machine load is not needed.

## Fix
creations.cpp `bar_tick` (each frame in the creature stage): when the avatar's abilities (count and
objects) change under the same avatar (our SetSpecies), or a slot of the bar points past the
avatar's abilities, the game's own builder `SporeCoop::AbilityBar_Rebuild` (0xD49400) runs again
outside the frame. A new avatar (a load) is left to the game, which builds its bar itself.
`coop.fix creations.ability_bar off`: the old behaviour. `coop.creations bar [rebuild]`: the bar
against the avatar's abilities / the game builds it now (tests).

## Verification
* FAIL: 20261031-205235 (`SPORE_FIX_OFF=creations.ability_bar`, lane 6): bar of 11 slots, 5 past
  the guest's 9 abilities; not the bar the game builds for the avatar. (No caught fault: the reads
  past the species' array returned 0 on the fixture, see Root cause.)
* PASS: 20261031-205349 (lane 8), 20261031-205913 (lane 6, with creature, team, creations, select,
  editor_after_swap, editor_after_load: 7/7). Run dirs under the worktree's out/lab.
* Not done: a 4-player campaign rerun (the fault itself needs garbage past the array and showed
  in 1 of the campaign runs only).

## History
- 2026-10-31: opened (lab 20261031-185843 campaign#2); root cause, fix and test on fix/w27-creature-fault.
- 2026-11-01: released in 0.7.3.2.
