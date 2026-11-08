---
id: BUG-0734
title: Creature: hostile species never target a guest's creature on the host (BT node 0x04A1A1E6 compares with the world's species)
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0418 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in:
branch: fix/w46-k2b-content-b
commits: 8911e865, 02cec174
tests: hostile_party, hostile_party_slow (coop_hostile_party.py)
signature:
docs: docs/re/creature.md#creature-player-species-callers
related: BUG-0418, BUG-0016, BUG-0462
---

## Report
K2b audit of the class BUG-0418: the hostile-species targeting 0xD7D9F0 (relation 2 / 5 animals target player-species adults) skips a guest playing its own creature; npc.partners' AI swap only partly mitigates.

## Root cause
Static (Ghidra, GOG 2024): 0xD7D9F0 is the condition of BT node 0x04A1A1E6 and compares the candidate's +0xB20 with PlayerSpecies directly (call 0xD7DA43). Runtime (lab 20261108-042844): a species made hostile to the players (its record relation 2, `coop.social record`; the guest next to four of its adults): the condition was called 0 times in 40 s (a counting hook) and all four animals took the guest's puppet as their target (+0xE7C) anyway: the npc.partners AI swap (BUG-0016) lets an animal near a partner tick with the puppet as the avatar, and its own avatar-oriented nodes go for it. A first party-answer hook of the condition was written, never called, and taken out. Found on the way: GetRelationship(animal, puppet) on a puppet without archetype tuning outside the swap faults in the diet rule (IsHerbivore 0xC0B8E8 data 0x608, lab 20261108-034334, from the mod's own test command; that command lends the avatar's tuning now).

## Fix
None needed (the hostile animals go for the guest through the AI swap). The test hostile_party guards it.

## Verification
no-fail-run: not reproduced (the node never ran). The guard test: PASS out/lab/20261108-042844, out/lab/20261108-043118 (hostile_party and hostile_party_slow, the code without the hook); with the swap off (SPORE_FIX_OFF=npc.partners) it fails: out/lab/20261108-043319 (no animal targets the guest).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): not reproduced; the guard test above.
