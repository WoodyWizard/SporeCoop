---
id: BUG-0741
title: Tribe: a guest keeps the tribe spawn marker the host's NPC tribe controller used (latent, harmless while tribe.guest_tribes blocks the guest's controller)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-08
source: fix/w46-g4-tests BUG-0489 analysis (Ghidra)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-b1-tribe
commits: 5c6fd384
tests: tribe_copy_marker
signature:
docs: docs/re/creature.md#creature-gameplay-markers, docs/re/tribe.md#tribe-copy-marker
related: BUG-0489, BUG-0587
switch: tribe.copy_marker
---

## Report
Found by static analysis for BUG-0489 (docs/re/creature.md#creature-gameplay-markers), no player report. The host's NPC
tribe controller (NpcTribeController_FormTribe 0xCC6440) destroys the tribe spawn marker (cGameplayMarker, +0x108 ==
0x57F09E5E) it formed a tribe at; the guest's copy of that tribe (made from the host's state) leaves the guest's marker in
place: the Tribe detector's 1 guest / 0 host unpaired gameplay marker (2% of Tribe comparisons). Nothing breaks today:
the guest's controller is blocked (`tribe.guest_tribes`, BUG-0587). It would matter if a guest's controller ever formed
tribes (that fix off, or a later host handover): it could form a tribe on a site already used.

## Root cause
Observed (Ghidra, GOG 2024): NpcTribeController_FormTribe 0xCC6440 picks the tribe spawn marker nearest the player
tribe, SpawnNpcTribe at it, and destroys the marker (cGameNounManager::DestroyInstance) in the game whose controller
formed the tribe. Observed (runtime, lab 20261108-143535, lane 7, `coop.fix tribe.copy_marker off`): the host's
controller formed two tribes (4 -> 2 markers on the host); the guest's game made its copies (tribe.npc_tribes,
BUG-0381) at the same places and kept all 4 markers, two of them exactly where the host's were used up. Inferred: the
copy never goes through FormTribe. Harmless while tribe.guest_tribes (BUG-0483 / BUG-0587) blocks the guest's
controller; it would let a guest controller (that fix off, a later host handover) form a tribe on a used site.

## Fix
mods/coop/tribe.cpp `use_marker_at`: when the guest's game makes its copy of a host NPC tribe (make_npc_tribes), it
destroys its own tribe spawn marker (noun 0x36BE27E, +0x108 == 0x57F09E5E) nearest the host tribe's position, within
30 m (marker position +0x1A8), as the host's controller did; logged as "the copy of the host's NPC tribe used up the
tribe spawn marker at ...". `coop.tribe markers` also prints `markers_used=`. Switch `coop.fix tribe.copy_marker off`
(the guest keeps its markers, 0.7.11).

## Verification
Test tribe_copy_marker (coop_tribe_copy_marker.py, new, cold, ~1 min): both games list the same 4 tribe spawn markers;
the host's controller forms its next tribes (`coop.tribe form`, tribe.form_pace off for the forming); the host loses a
marker for each new tribe; the guest must have its copy of each and no marker within 5 m of a used one, and the same
marker count as the host.
* FAIL (SPORE_FIX_OFF=tribe.copy_marker): out/lab/20261108-143535-fix-w46-b1-tribe (lane 7): copies 2 of 2, the guest
  keeps the markers at (446 -150 185) and (438 181 188), host 2 / guest 4.
* PASS: out/lab/20261108-143653-fix-w46-b1-tribe #1, #2 (lane 7): host 2 / guest 2 markers, the guest's log names each
  marker used up (0.0 m off the host tribe's place).
* The first two FAIL runs of the new test (20261108-143241, -143432) were test problems: tribe.form_pace held the
  forming; the controller formed two tribes in one second (the test now takes any number).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-b1-tribe): reproduced with a new test (FAIL with the switch off), fixed with tribe.copy_marker, 2 PASS.
