---
id: BUG-9301
title: Tribe (guest): the host's villagers keep the wrong body (no species sync for mirrors)
status: open
kind: desync
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9043, BUG-9153
---

## Report
Live 67-dev, Tribe: "All of [the guest's] creatures were the same (Host was not their species on their side.)"

## Root cause
Observed in the code: `spawn_member` (tribe.cpp ~5690-5706) makes a mirror with the local tribe's own SpawnMember /
CreateCitizen, so it wears that local tribe's species; the member snapshot carries no species, the per-tribe TRBI
species is used only to make missing NPC tribes, and nothing on the guest calls SetSpecies / SetSpeciesProfile on a
tribe. So the host's new outfit (292ACDD7, host 21:37:49) never reached the guest's copy of the host's village.
Likely second cause (fits "all the same"): the guest's own outfitter (20:31:56) went down the native request path while
the swap was undone for the editor, so the game applied the guest's outfit to its player tribe, the host's village.

## Fix
Planned: on a guest, compare each local tribe's species with the host's TRBI entry (mapped by creations::local_species)
and apply a mismatch with the setters apply_guest_model uses; send the guest's native outfitter request through
open_own_editor(kTribalOutfitter) (BUG-9043 path). Switch `tribe.mirror_species`. Test: host outfit change and a guest
outfitter: the host's tribe species on the guest equals the host's (FAIL / PASS).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
