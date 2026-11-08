---
id: BUG-0738
title: All stages: the ~60 callers of PlayerSpecies and IsPlayerSpecies never classified for a guest
status: fixed
kind: bug
stage: All
reported: 2026-11-08
source: BUG-0418 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content-b
commits: e896b4a8
tests: make test (tools/docs_lint.py checks the anchor)
signature:
docs: docs/re/creature.md#creature-player-species-callers
related: BUG-0418, BUG-0732, BUG-0733, BUG-0734, BUG-0735, BUG-0736, BUG-0737
---

## Report
K2b audit of the class BUG-0418: the ~60 callers of PlayerSpecies 0x4DF490 and the readers of IsPlayerSpecies vt 0xD0 (Tribe, Civ, Space ones too) were never classified for a guest playing its own creature.

## Root cause
Static (Ghidra, GOG 2024, 2026-11-08, the fork audit of fix/w46-k2b-content-b): 60 calling functions of 0x4DF490 and every vt 0xD0 reader classified as party (a partner's species should count), own (the guest's own avatar should get the rule) or keep (the world's species is right everywhere): the table in docs/re/creature.md#creature-player-species-callers. The Tribe, Civ, Space, UI and editor callers are the empire's / tribe's species of the host's world: keep.

## Fix
The table; the rules with a visible effect got their own tickets and fixes: BUG-0732 (size, own), BUG-0733 (the partner's size), BUG-0734 (hostile species: not reproduced, the npc.partners AI swap covers it), BUG-0735 (pack defence, party), BUG-0736 (Summon Flock), BUG-0737 (AI flags, strength, default species: wontfix); the ones already covered are named in the table (BUG-0016, 0358, 0375, 0462, 0561, 0642).

## Verification
no-fail-run: docs only (the classification); the fixes it led to have their own FAIL / PASS runs.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): the classification written; children fixed or closed.
- 2026-11-08: released in 0.7.11.
