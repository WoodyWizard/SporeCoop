---
id: BUG-1073
title: Civ: after the campaign's in-game load of the civ_npc fixture from Tribe, cCity::Capture with its cinematic never gives the city (Civ end stuck)
status: open
kind: bug
stage: Civ
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests: campaign (the jump path)
signature:
docs:
related: BUG-0311, BUG-0982, BUG-1055
---

## Report
Found by fix/w49-tests (Q1) in two 4-player campaigns on rel/0.7.14 + fix/w49-tests (lab 20261113-060756 lane 4, the
full campaign; 20261113-060758 lane 5): the Tribe end did not come in 20 min, the host jumped (`coop.load` of the
civ_npc fixture in game, slot 10, the guests followed), Civ was played 3-8 min, then the Civ end's shortcut
`cCity::Capture(city, player pol, 0, 0 /* not instant */, 0, 0)` ran every 30 s for 20 min: the mod logged
"Alice captured a city" (lane 4) / "Carol captured a city" (lane 5) each time (h_Capture after o_Capture), but no city
changed hands (`coop.civ`: still pol=000021CB ...), no cinematic, `busy: -`: "the stage change civ -> space did not
finish in 20 min" (stuck). The same call with instant=1 on the stuck host took the city at once (lane 4, by hand).
In 0.7.13's final check the same jump path captured all 9 cities (out/lab/20261112-175447).
Not reproduced on a fresh world: capprobe (civ_npc, 2 games, the same call) took 2 of 2 cities after 8 s on
rel/0.7.13 and on this branch; campaign_space (4 players, civ fixture, 2 min play, no jump) captured all
(lab 20261113-072523).
Reproduce: `coop_campaign.py --from tribe --to space` with a Tribe end that does not finish (CAMPAIGN_OLD_TRIBE=1),
so the host jumps to civ_npc; watch "cCity::Capture of 1 city ... (9 of other nations listed)" repeat.

## Root cause
Not analysed (outside Q1). Suspects: what an in-game load from Tribe leaves set for Civ (the lifecycle
registry of BUG-1055, civ's capture caption state), the BUG-0982 specialty hooks (h_NeedsSpecialty, cCity listener).
The non-instant capture finishes at the end of its cinematic; here no cinematic started.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
