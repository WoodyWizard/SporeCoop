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
branch: fix/l47-view
commits:
tests: manual two-player plan (ticket Fix)
signature:
docs: docs/re/tribe.md#tribe-swap-cards
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

Precision (2026-11-20, fix/l47-view): the first cause is observed in the code (spawn_member, no species in the member
snapshot, no SetSpecies on a guest outside the test command `setspecies`); also the guest's food mirroring compares the
local tribe's species with the host's (guest_state), so a wrong species also stops that village's food from mirroring
(inferred from the code, not seen). The second cause (the native outfitter on a guest) is inferred from the log times,
not checked. Not changed here: the guest's native outfitter request is not rerouted (outfit vote code, BUG-9298's
area); the species sync below puts the host's species back on the host's village after it either way.

## Fix
Switch `tribe.mirror_species` (tribe.cpp, after make_npc_tribes; on). A guest, on each state chunk with the host's
tribe table (TRBI), compares every mapped local tribe (g_local_tribe_of) with the host tribe's species
(creations::local_species of the TRBI key): a member (not a detached body) or species slot 0 wearing another species
counts. A difference seen for the same target 2 s in a row is applied outside the frame (species_apply): members
cCreatureAnimal::SetSpecies(profile, own age, true) (the game's own call after an editor, as `coop.tribe setspecies`),
species slots Tribe_SetSpeciesProfile (slot 0 always, others that held a wrong species). At most one try per tribe
every 3 s; a species not on the guest is asked for (creations::want_species) and applied on a later try. Held during
the guest's own outfit vote (g_outfit.token), local play, a load, a save and a cinematic. Readout: `coop.tribe species`
(both games: per tribe slot0 and the members' species; guest: fixed= members= wanted= last=) and the log line "the
host's species for a village here". Test (manual): the host changes its outfit (planner, outfitter): within ~5 s the
guest's `coop.tribe species` shows the host's village with the host's key; off: the old key stays.
## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): seen: both games list tribe 0 291EE5B5 (host) and tribe 1 7CE63A2C (guest) after the entry; no outfit change tested. First run: NPC tribe 2 differed (host 291981F2, guest 7DF97DCB), fixed=0; see the history line on how to read it.

- Live QA 2026-11-21, second run (QA agent, fix/l47-qa 9d64f85d = fix/l47-powers aeaedfc3 + BUG-9308, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a live Creature -> Tribe entry from Vandh): not reached. The host changed its outfit in the outfitter of the BUG-9298 vote
  (a part added, budget 60 -> 30, Done), but its own `coop.tribe species` kept tribe 0 slot0=291EE5B5 and members
  291EE5B5 before and after (so the guest had nothing new to mirror: guest also 291EE5B5; tribe 1 "(host 7CE63A2C =
  7CE63A2C here)"). The vote ended unanswered for the guest (BUG-9298); whether the host's own outfit is applied only at a
  full vote was not checked. On the guest's screen the host's villagers were too small to judge the look.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on fix/l47-view (built, not run in a game yet; the lead runs the two-player test).
- 2026-11-21: live MSI: NPC tribe 2 showed 291981F2 on the host and 7DF97DCB on the guest with `fixed=0`. The pass does
  cover NPC tribes (every mapped tribe), but it compares with creations::local_species of the host's key: a guest that
  stored the host's creature under a key of its own (`coop.creations`: "host species 291981F2 = 7DF97DCB here", the same
  records) counts as matching, by design. Not checked which case this was. `coop.tribe species` on a guest now shows
  each tribe's `(host XXXXXXXX = YYYYYYYY here)`: equal to the members' key = the remap (same look, not a bug); members
  differing from the "here" key with `fixed=0` and `wanted=` rising = the species is missing on the guest (asked from
  the host every 3 s); neither = a bug to report.
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: second live QA run (see Verification).
