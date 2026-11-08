---
id: BUG-0489
title: gameplay markers: one of about twenty is on one game only or elsewhere (Creature 14%, Tribe 2%): find what a gameplay marker is before deciding
status: fixed
kind: test
stage: Creature
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-g4-tests
commits:
tests: tools_selftest (make test)
signature:
docs: docs/re/creature.md#creature-gameplay-markers
related: BUG-0741
---

## Report
Detector: `Creature:unmatched:gameplay_marker` in 14% of Creature comparisons (21 host / 20 guest, 1 without a partner within 3 m), `Tribe:unmatched:gameplay_marker` 2%. kGameplayMarker (0x036BE27E) is not described in docs/re: find what places it (nests, missions, the player's own goals) in Ghidra; if per player, add it to worlddiff.IGNORE with that reason, else a sync ticket.
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (Ghidra, GOG 2024; docs/re/creature.md#creature-gameplay-markers): kGameplayMarker 0x036BE27E is the class
cGameplayMarker, an invisible spawn point (herd, tribe, city site, ornament, path markers) made by the planet populate step
of a stage start and saved, so a guest gets the host's markers with the world: shared content, not made per player. Each
game consumes some of them by its own logic: the host's NPC tribe controller destroys the tribe spawn marker it formed a
tribe at (0xCC6440; the guest's copy keeps it: the Tribe pattern, 1 guest / 0 host unpaired, BUG-0741), and 0xD408E0
destroys a part level's ornaments and their markers when the local avatar's own collection completes it (per player).
Observed in the lab logs: the Creature rows are "22 on the host, 21 on the guest, 1 host / 0 guest" for every guest of a
run alike (fix/w46-advance-reg out/lab/20261107-184159 campaign_tribe#1: guest, extra1, extra2). Inferred: the extra host
marker is one the guest's own logic consumed (the part-level cleanup) or the host's populate step around its own avatar;
not placed (the rows carry no position). What the players see (herds, nests, tribes, ornaments) is compared by the
detector itself.

## Fix
Detector only: `gameplay_marker` in worlddiff.IGNORE (invisible per-game bookkeeping; the spawned objects are compared),
with the reason in the code. tools/detector_report.py keeps its label for old logs. The latent Tribe case is BUG-0741.

## Verification
tools_selftest (make test) gets the case "a gameplay marker on the host only: no row". FAIL with the old worlddiff
(`SELFTEST_WORLDDIFF=<copy of 5e15c390's worlddiff.py> python3 tools/tests/tools_selftest.py`: ['Creature:unmatched:gameplay_marker']),
PASS with this branch. no-fail-run: a detector classification change, checked by the selftest, not by a lab run (the rows
came from organic play, e.g. fix/w46-advance-reg out/lab/20261107-184159 campaign_tribe#1).

## History
- 2026-11-03: opened.
- 2026-11-08 (fix/w46-g4-tests): what a gameplay marker is (Ghidra); per-game consumption; detector ignores it; selftest FAIL/PASS; fixed.
