---
id: BUG-0645
title: Tribe: the guest cannot have more than 6 members and its village shows 12/6
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-14
role: guest
versions: 0.7.8
fixed_in: 0.7.10
branch: fix/w46-tribe-civ
commits: 0f273391, 142b991e
tests: tribe_level
signature:
docs: docs/re/tribe.md#tribe-level
related: BUG-0422, BUG-0418
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-14 (the guest, s 5519): "I can't spawn more than 6 members,
and I see my village as guest 12/6 (over the 6 limit)".

## Root cause
Observed (logs): the host's `tribe(host): members=` between 4 and 36 during Tribe (both players' members); the guest
`local_tribes=6`, `ours=69/5`, no rejection lines for the guest's commands near the mark (host `rejected=5` flat since
18:39); one "M0ng0r gave 1 tribe member to Sterben" at 19:04:50. docs/re/tribe.md: cTribe::SpawnMember returns 0 at the
population cap (0xC8EC60).
Inferred, not determinable from the logs: the guest's village shows the shared or mirrored member count (12) against
its own village's cap (6), and its own spawn is refused at that cap. Class BUG-0422 (per-player state) or BUG-0418.

Observed 2026-11-07 (Ghidra + lab): the population cap is the player tribe's level's (cTribe +0x38, cTribe_MaxPopulation 0xC8EC70: 6, 9, 12, 15, 30, 45, 60). Only the hut upgrade 0xCDA4F0 raises the level (from the goal progress) and writes the cap into the Tribe HUD (+0x10 of [0x169B41C]); it ran on the host only. The guest's tribe stayed at level 0, cap 6, with the host's members: "12/6", and the guest's game refused new members at its cap.

## Fix
`wsync.tribe_level` (BUG-0645): a wsync row with the host's player tribe level; the guest stores it and the HUD cap of that level. Off: as 0.7.8.

## Verification
Test tribe_level (coop_tribe_level.py): the host's tribe at level 2 (the upgrade's two stores) with 10 members; the guest's game's own cap function and HUD cap.
- FAIL (SPORE_FIX_OFF=wsync.tribe_level): out/lab/20261107-075004 (guest level 0, cap 6, HUD 6).
- PASS: out/lab/20261107-074905, out/lab/20261107-080150.
- Not verified: the hut's upgraded look on the guest. A member the guest buys never reaches the host: BUG-0671.

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
- 2026-11-07: fixed in fix/w46-tribe-civ (wsync.tribe_level); BUG-0671 opened for the guest's own purchase.
- 2026-11-07: released in 0.7.10.
