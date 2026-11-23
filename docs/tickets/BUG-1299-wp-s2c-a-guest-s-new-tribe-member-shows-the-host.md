---
id: BUG-1299
title: WP-S2c: a guest's new tribe member shows the host's species for a moment before its own
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-19
source: plan
role: guest
versions: 
fixed_in: 0.7.16
branch: feat/w56-editors
commits: f3373bd1
tests: tribe_birth_species, tribe_birth_species_off (coop_own_editors_robust.py birth)
signature: SporeCoop::cTribe_SpeciesSlot 0x00C8E9B0
docs: docs/re/tribe.md#tribe-birth-species, docs/re/creations.md#creations-own-editors-robust
related: BUG-1271, BUG-0671, BUG-0824
---

## Report
Lineages comparison (item 6): a guest's new member (a baby it bought, a mirror) showed the tribe's species for
0.5-3.5 s before the species pass changed it (one member per 0.15 s after 0.5 s of seeing it); the fork's members are
born in their village's species.

## Root cause
✔ (Ghidra entries of docs/re/tribe.md#tribe-two-species) Citizen_Init 0xC27330 takes the slot through
cTribe_SpeciesSlot; the pass waits on purpose (a body is built over several frames, BUG-0318).

## Fix
`tribe.birth_species`: the host's baby of a known buyer and a guest's mirror spawn of a known owner run with the
(hooked) slot accessor answering the owner's line profile for the player tribe's slot 0; nothing is written into the
tribe; the member is noted as changed by the mod (it goes back to the slot like the others).

## Verification
PASS: tribe_birth_species 20261119-104136 (night), 20261119-113824, merged tree 20261119-115724. FAIL (twin,
SPORE_FIX_OFF=tribe.birth_species): tribe_birth_species_off 20261119-105127 (first seen in the tribe's species, but
both games faulted: BUG-1313) and 20261119-113824 (first seen 067B3E4A on both games, no fault: the right reason
only).

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: fix and its test written (switch, FAIL twin entry); the lab run waits for the orchestrator.
- 2026-11-19: fix/w57-tribe-keys: lab PASS and twin FAIL; the twin's fault was BUG-1313 (fixed there); fixed.
- 2026-11-23: released in 0.7.16.
