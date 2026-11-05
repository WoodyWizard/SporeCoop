---
id: BUG-0561
title: Creature: an allied nest has no ally (green) icon on the guest's minimap, only the grey discovered mark
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-guest-player
commits: 7a97b869, ca09f017
tests: coop_creature_nest_ally.py
signature:
docs: docs/re/creature.md#creature-player-rules
related:
---

## Report
The guest: an allied nest shows only the grey 'discovered' mark on its minimap, not the ally face.
Reports: SporeCoop-report-2026-11-04 (Discord text) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
* Observed (Ghidra): the minimap 0xD425A0 gives a nest the ally icon (flag 0x200000, picture 0xB3B6763E in 0xE10560)
  when its herd's mbBestFriends +0x191 is set (or the herd is the world's player species); it never reads the
  relationship table. +0x191 is set only by ApplyEvent 0xBA4A00 -> 0xBA4840 -> cHerd_SetBestFriends 0xC6A210 on a
  relation change. The guest writes the host's table into its own (social.cpp enforce_table): no ApplyEvent, +0x191
  stayed 0, the grey "discovered" icon (0x05E51BCE).
* Observed (lane 9, fix off): the guest's table had relation 6 and its herds +0x191 = 0 (out/lab/20261104-212719).

## Fix
`social.cpp` (`social.nest_ally`): after the table is enforced (every 0.5 s on a guest) every herd of a species in the
host's table gets +0x191 = (relation == 6) through the game's setter cHerd_SetBestFriends; the minimap redraws its
icon by itself (2.5 s). `coop.fix social.nest_ally off`: as before.

## Verification
coop_creature_nest_ally.py (suite creature_nest_ally): the host's record of a wild species becomes allied; the
guest's own table and every herd of the species in the guest's game must follow (+0x191), and drop it again.
* FAIL with `SPORE_FIX_OFF=social.nest_ally`: out/lab/20261104-212719.
* PASS: out/lab/20261104-211232, out/lab/20261104-220621.
* Not verified: the drawn icon (the minimap's icon cache is not read); the alliance made the game's way (the test
  writes the host's record).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (P1), no evidence beyond the text.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): fixed: the guest's herds follow the alliance (social.nest_ally).
- 2026-11-05: released in 0.7.7.
