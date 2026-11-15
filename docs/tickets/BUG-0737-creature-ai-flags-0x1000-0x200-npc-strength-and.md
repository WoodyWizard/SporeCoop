---
id: BUG-0737
title: Creature: AI flags 0x1000 / 0x200, NPC strength and the default species read the world's species (classify)
status: wontfix
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0418 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in:
branch: fix/w46-k2b-content-b
commits: e896b4a8
tests:
signature:
docs: docs/re/creature.md#creature-player-species-callers
related: BUG-0418, BUG-0738
---

## Report
K2b audit of the class BUG-0418: the AI flags 0xC04440 (0x1000 player species, 0x200 brain level > 4), the NPC strength relative to the player species 0xC02870 / 0xC0A100 and the default species 0xC22450 read the world's species; for a guest's own creature they never answer "player species".

## Root cause
Static (Ghidra, GOG 2024, 2026-11-08): 0xC04440 writes [creature +0xB4C] +0x5FC when a creature is made, grows or is read; the only reader of 0x1000 found is 0xD7AF80, the condition of BT node 0x02D876AB ("the sender of stimulus 0x800 has 0x1000"), no reader of 0x200 (byte search). 0xC02870 (called only by Create 0xC09D2A) and the same formula in the herd spawn 0xC0A100 set an NPC's strength from its species' health against the world species': the world's NPC tuning, the same on every game (the guest's NPCs are the host's). 0xC22450 SetSpeciesProfile takes the player species only for a null species (a fallback).

## Fix
None: the strength and the default species are right with the world's species on every game; for the flags no visible rule was found (one BT condition of an unknown stimulus 0x800). Recorded in docs/re/creature.md#creature-player-species-callers. Should a rule of stimulus 0x800 show up, the puppet's +0xB4C +0x5FC can get 0x1000 after its spawn and growth.

## Verification
no-fail-run: static audit; no visible rule (the strength and the default species are the world's on every game by design, the flags have no reader with an effect found).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): closed wontfix with the static audit.
