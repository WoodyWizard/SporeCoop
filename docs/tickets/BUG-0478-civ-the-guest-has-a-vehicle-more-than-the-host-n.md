---
id: BUG-0478
title: Civ: the guest has a vehicle more than the host now and then; roads between cities built on the host are missing on the guest
status: fixed
kind: desync
stage: Civ
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-civ-crashes
commits: 0823fb59, 9a9262ab, c269e094
tests: civ_mirror_remove
signature:
docs: docs/re/civ.md#civ-planner-lifetime
related: BUG-0186
---

## Report
Detector: `Civilization:unmatched:vehicle` in 28% of Civ comparisons (4 on the host, 5 on the guest, 1 without a partner: a stale or duplicate mirror), `guest_only:vehicle` 2%; `noun_only_host:inter_city_road` 10% (a road the host's game has, none in the guest's). Classes G5 (stale mirrors) and G4 (roads not mirrored).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
* Observed (lab, `coop.civ nouns 18C6DE8` with dead marks): the guest removed a mirror the host no longer
  had with `cVehicle::Die(v, 0, -1)` alone, which only sets the dead flag: the wreck stays in the noun list
  and the civilization's lists for good (and its AI drives it on); an NPC vehicle that dies on the host is
  gone from the host's list at once. The extra guest vehicles of the detector (more the longer a run
  played) are these wrecks.
* Roads: not traced (where the host's game makes an inter_city_road).

## Fix
`civ.mirror_remove`: the guest removes such a vehicle with the game's own removal `SporeCoop::Vehicle_Remove`
0xCAA7B0 (Die, the behavior manager, the civilization's removal that destroys it). Roads not fixed, moved to BUG-0494
(docs/known-issues.md#issue-civ-roads-not-mirrored): the road creation of the host is not traced.

## Verification
* FAIL (`SPORE_FIX_OFF=civ.mirror_remove`): `out/lab/20261103-131214`: the guest keeps the two dead vehicles the host's
  game removed.
* PASS: `out/lab/20261103-125816` (civ_mirror_remove#1, #2): the guest has no vehicle the host lacks and
  no dead vehicle. After c269e094 (the removal waits while a cinematic plays): `out/lab/20261103-135157`
  (civ_mirror_remove, civ_planner, civ_caption, civ_coop, civ_ruin_repair, civ_discovery: all OK).

## History
- 2026-11-03: opened.
- 2026-11-03: vehicle half fixed on fix/w40-civ-crashes (civ.mirror_remove); roads moved to BUG-0494.
- 2026-11-03: released in 0.7.4.
