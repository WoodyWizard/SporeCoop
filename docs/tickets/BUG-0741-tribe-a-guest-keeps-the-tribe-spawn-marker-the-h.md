---
id: BUG-0741
title: Tribe: a guest keeps the tribe spawn marker the host's NPC tribe controller used (latent, harmless while tribe.guest_tribes blocks the guest's controller)
status: open
kind: bug
stage: Tribe
reported: 2026-11-08
source: fix/w46-g4-tests BUG-0489 analysis (Ghidra)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0489, BUG-0587
---

## Report
Found by static analysis for BUG-0489 (docs/re/creature.md#creature-gameplay-markers), no player report. The host's NPC
tribe controller (NpcTribeController_FormTribe 0xCC6440) destroys the tribe spawn marker (cGameplayMarker, +0x108 ==
0x57F09E5E) it formed a tribe at; the guest's copy of that tribe (made from the host's state) leaves the guest's marker in
place: the Tribe detector's 1 guest / 0 host unpaired gameplay marker (2% of Tribe comparisons). Nothing breaks today:
the guest's controller is blocked (`tribe.guest_tribes`, BUG-0587). It would matter if a guest's controller ever formed
tribes (that fix off, or a later host handover): it could form a tribe on a site already used.

## Root cause
Observed (Ghidra, GOG 2024): the marker is destroyed in FormTribe after SpawnNpcTribe, only in the game whose controller
formed the tribe. Inferred: the guest keeps it because its tribe copy never goes through FormTribe.
Possible fix (not done, low priority): when the guest makes its copy of a host-formed NPC tribe, destroy the guest's
0x57F09E5E marker nearest the new tribe.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
