---
id: BUG-0313
title: GOG 2024 guest: game time paused for ~7 s after its world load in Tribe (black screen)
status: open
kind: bug
stage: Tribe
reported: 2026-10-30
source: BUG-0175 split (fix/w13-net)
role: guest
versions: 0.7.2.4 and later (GOG 2024 build)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0175
---

## Report
Split from BUG-0175 (2026-10-30, fix/w13-net), first seen 2026-10-24 (fix/gog2024-sync): after its
world load in the Tribe stage the GOG 2024 guest keeps its game time paused (cGameTimeManager
+0x48 = 3, black screen, ~23 fps) for ~7 s; the GOG 2016 guest is already running when the
test's start() returns. Citizens get no `Update` meanwhile (`coop.tribe` on the guest:
`ai_skipped=0/0`). Lab 20261030-133141 (lane 10, GOG 2024): "game time running 20.1 s after the
start" in Tribe (3.1 s in Civ). Players see a black screen then, so no mirror error is visible;
what keeps the 2024 game paused (planet still loading?) is not known.
Reproduce: `SPORE_LANE=10 python3 tools/tests/coop_sync.py tribe` prints the seconds until both
games run.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-30: opened.
