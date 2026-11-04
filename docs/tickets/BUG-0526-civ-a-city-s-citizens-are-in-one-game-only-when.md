---
id: BUG-0526
title: Civ: a city's citizens are in one game only when only that game's camera is near it (guest_only / host_only:citizen with up to 19)
status: open
kind: desync
stage: Civ
reported: 2026-11-04
source: play_civ --repeat 2 with SPORE_DETECTOR=20 (out/lab/20261104-005314), fix/w41-civ-citizens
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0476
---

## Report
After BUG-0476 (citizens near a guest follow the host's), play_civ with the detector (2 runs, 62 Civ comparisons, out/lab/20261104-005314) still has `Civilization:guest_only:citizen` in 19 % (e.g. 15 citizens on the guest, none on the host at a city) and `host_only:citizen` in 5 % (19 on the host, none on the guest), and `unmatched:citizen` in 39 % (247 objects, about 4 per comparison; before the fix 59 % and about 17). The quiet detect_civ (both cameras at the capital): unmatched 24 %, 20 objects. Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_civ`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Inferred (not traced): a city's citizens are spawned (or kept) only by a game whose camera is near the city (a level of detail, as the Space planets' life), so a city the guest looks at has its own citizens and the host's game none to send, and the other way round. The citizen kind can only bind and walk citizens both games have.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-04: opened.
