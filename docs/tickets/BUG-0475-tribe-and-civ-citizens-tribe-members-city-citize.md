---
id: BUG-0475
title: Tribe and Civ: citizens (tribe members, city citizens) are shown at another size on the guest
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, report out/detector/20261103-0236.md)
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-scale
commits: 59c64045 3fa7d38c d4f3ff94 cfa77d8b f07fcbb0
tests: mirror_scale_tribe (tools/tests/coop_mirror_scale.py --kind tribe_member)
signature:
docs: docs/re/tribe.md#tribe-member-scale, docs/known-issues.md#issue-civ-citizen-scale
related: BUG-0419, BUG-0461, BUG-0476
switch: tribe.scale
---

## Report
Detector: `Tribe:scale:citizen` in 70% of Tribe comparisons (3.79 host / 4.47 guest), `Civilization:scale:citizen` 50% (1.50 / 1.21). Same class as BUG-0461 (NPC scale sent and not applied): the mirrors keep a default or own scale. Class G2 (BUG-0419).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed: a member's size is its own game's pick (cCreatureCitizen GetDesiredScale vt 0x80, random per member); the guest's mirror (a bound local member or SpawnMember) kept its own. Civ half: city citizens are not mirrored at all (each game simulates its own, BUG-0476), the detector pairs different individuals; nothing to apply a size to.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Tribe members of every tribe are a field kind (`tribe_member`, field `scale`) of the replication engine (fields.h): the guest sets the host member's size on its mirror. Switch `coop.fix tribe.scale off`. The Civ half stays open with BUG-0476 (known-issues#issue-civ-citizen-scale).
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
FAIL with SPORE_FIX_OFF=tribe.scale: out/lab/20261103-125805 (4 of 28 pairs, 14 %). PASS: out/lab/20261103-125153 (0 of 28, detector Tribe:scale:citizen 0 of 4).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-scale (replicated fields, docs/sync-architecture-plan.md#sync-fields).
- 2026-11-03: released in 0.7.4.
- 2026-11-04 (fix/w41-civ-citizens): Civ half: fields kind `civ_citizen` on the citizens bound by BUG-0476 (`coop.fix civ.citizen_scale off`). FAIL with SPORE_FIX_OFF=civ.citizen_scale: out/lab/20261104-001130 (17 of 40 unambiguous pairs of another size); PASS out/lab/20261104-000735. The detector's `scale:citizen` stays in part: its greedy pairs cross in a crowd (docs/known-issues.md#issue-civ-citizen-scale).
