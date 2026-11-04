---
id: BUG-0488
title: Tribe: tools, members and nests sometimes stand elsewhere on the guest (more than 15 m / 3 m)
status: open
kind: desync
stage: Tribe
reported: 2026-11-03
source: divergence detector, nightly run 2026-11-03 (out/lab/20261103-012624, out/detector/20261103-final.md)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0523, BUG-0481, BUG-0514
---

## Report
Detector: `Tribe:unmatched:tribe_tool` 4% of Tribe comparisons, `unmatched:citizen` 1%, `unmatched:nest` 1%, `host_only:interactive_ornament` once (4 near the guest on the host only). Related to BUG-0481 (play_tribe: members not where the host has them). Class G8 (remote placement).
Found by the lab (no player report). Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Analysis 2026-11-03 (fix/w41-tribe-world):
* Tools (`unmatched:tribe_tool`, the biggest share, 4%): observed in the code, nothing in the mod syncs tribe tools
  (no CreateTool on a guest, no tool table): each game makes the tools its tribes buy after the world was shared
  at its own spot. Split into BUG-0523 (to be done with BUG-0514's tool mapping on fix/w41-tribe-actions).
* Members (`unmatched:citizen`, "8 on the host, 6 on the guest, 2 host / 0 guest without a partner"): mirrors the
  guest had not spawned yet (BUG-0481's late mirrors), not members elsewhere.
* Nests (`unmatched:nest`, campaign_tribe#2 only): not analysed.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: analysed on fix/w41-tribe-world; tools split into BUG-0523, not fixed.
