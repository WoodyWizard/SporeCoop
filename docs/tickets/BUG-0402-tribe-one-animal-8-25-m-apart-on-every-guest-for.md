---
id: BUG-0402
title: Tribe: one animal 8-25 m apart on every guest for minutes in campaigns (sync-check n apart), after the co-op epic comes
status: open
kind: desync
stage: Tribe
reported: 2026-11-01
source: lab log gates over recorded campaign runs (BUG-0381 analysis, fix/w32-tribe-sync)
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0381, BUG-0305, BUG-0281
---

## Report
Split off BUG-0381 (its `n apart`, 8 results). In the recorded Tribe runs `n apart` is of three kinds:
* **one NPC on every guest, 8-25 m, for minutes**: out/lab/20261031-024148 campaign#1, `n327` from 04:10:50 to
  04:21:38 on all three guests (24.0, 24.8, 14.5, 15.5, 10.1, 8.1, 13.8, 10.3, 17.5 m), the first MISMATCH 6 s
  after the host's "an epic creature comes! together: 80 food" (04:10:44; the next epic 04:32). Also
  20261031-185843 campaign#2 `n135` 11-16 m 2 and 7 min after the epic, 20261031-214905 campaign#1 `n312`
  15 m 2 min after the epic. Not every case follows an epic (214905 campaign#3 `n398` 18-26 m came a minute
  before it).
* 2-3 animals just over the 8 m limit (8.2-10.5 m) for one check pair: a running herd (the BUG-0305 /
  BUG-0281 class of proxies trailing on their path), e.g. 20261031-000328 campaign_tribe n156 / n157.
* one 510 m (20261030-210914 campaign#1 n658): an NPC at the planet's centre (the host logs "npc N is
  updated at the planet's centre ... dead=1" warnings in Tribe).
Not reproduced on lane 7 (fix/w32-tribe-sync): play_tribe 2 x 10 min, chaos_tribe 2, tribe_coop 2, campaign_tribe 1
(out/lab/20261101-131136), all without an `n apart`; a co-op epic spawned next to the village and left alone
(`coop.tribe epic`, 100 s, sampled every second on both games) stayed 0.5 m from its proxy (it stood still); hit once
(`coop.tribe epic hit <member> 1`) it walked at 4-5.5 m/s for ~10 s, its proxy at most 2.3 m off (90 s).

## Root cause
Open. Inferred for the first kind: the co-op epic (huge, fast while it fights the village) and its proxy
on the guests: the proxy does not keep up (the proxies' walking, npc.cpp; reworked on another branch now).
Not checked: which NPC n327 was (the logs had no per-NPC line). Since fix/w32-tribe-sync a MISMATCH line is
followed by `sync-check:   n327 here: <the NPC's coop.npc list line>` on each game (species, hp, speed, pack /
family on the host; err / age / want / at on a guest), so the next campaign run says what the NPC was.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
