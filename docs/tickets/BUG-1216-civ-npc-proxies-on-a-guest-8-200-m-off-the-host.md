---
id: BUG-1216
title: Civ: NPC proxies on a guest 8-200 m off the host's NPCs in the 4-player campaign's Civ (sync-check n apart; campaign_tc2), also on v0.7.14.1
status: open
kind: desync
stage: Civ
reported: 2026-11-16
source: final check 0.7.15 (lab 20261116-025154 campaign_tc2)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in:
branch: fix/w51-t4
commits:
tests: campaign_tc2, campaign_segment (from the run's Civ save)
signature: sync-check: MISMATCH with the host (Civilization A3930F03, ...): n: N apart
docs: docs/06-testing.md#testing-final-triage-tribe-civ-0715
related: BUG-0402, BUG-1106, BUG-0329, BUG-0339
---

## Report
Final check 0.7.15 (lab 20261116-025154, rel/0.7.15 11114769, lane 4): campaign_tc2 printed `RESULT: OK`, the lab's
sync-check gate failed it: `12 on extra1, extra2, guest, host (n apart): ... (Civilization A3930F03, 45 objects compared):
n: 3 apart (n406 9.7 n404 8.8 n403 8.7)`, about 2.5 min into Civ (s 1363 and s 1397, two check pairs). Rerun of the
failed segment (`lab.py run campaign_segment` with a segment.json for the run's Civ save, seed 482315165; the gate
failure leaves no segment.json of its own): 20261116-084409 PASS (no MISMATCH), 20261116-085558 FAIL (`n: 2 apart
(n809 8.3 n810 8.2)` on Carol). The same segment on v0.7.14.1 (a detached worktree of the tag, its build, the 0.7.15
tools): 20261116-091255-2 #1 `n: 7 apart (n467 205.5 n462 186.6 ...)` on Carol. So not a 0.7.15 regression.

## Root cause
Three kinds, each from the `sync-check:   nNNN here:` lines of the host and the guest:
* A (the final check, all three guests, standing herd, species 067A147A): the host's NPC stands at agl 0.00 on the
  host's ground; at the same point every guest's ground is 10.47 m higher (`want_agl=-10.47`), the proxies stand on
  their own ground (`agl=0.00`, own height, docs/re/tribe.md#tribe-animal-height). The proxy is where the height rule
  puts it; the check compares absolute positions, so the terrain difference between the host's live Civ and the
  guests' copy of its save shows as `n apart` (BUG-0402's fourth kind, here in Civ). No terrain stamp of either game is
  within 50 m of the point (the stamp lines of 03:11:40-03:16 differ only in the host's own Civ start stamps); why the
  ground differs is not found. The segment runs (every game loads the same save) did not show this kind.
* B (segment run 2, Carol only, two walking animals of species 0654E5BC, 4 m/s): Carol's terrain at the target is the
  host's (`want_agl=0.02`), the proxies are 8.2 m over it (`agl=8.23`, `grounded=0`, their own speed 0) next to the
  target (`err` = the height): walked proxies in the air in Civ; the out-of-ground rule only lifts, nothing lowers a
  walked proxy there (inferred: in Tribe the game's own update grounds animals, docs/re/tribe.md#tribe-animal-height;
  not checked for Civ).
* C (v0.7.14.1 segment, Carol, the same species 0654E5BC, after Dave's rejoin): Carol's targets (`want`) lie 120-205 m
  from where the host's NPCs of those ids are, fresh (`age=0.10`, 9 states a second), the proxies 50-63 m from them:
  inferred an id or target mix-up of that herd on one guest; not analysed further.
Not a 0.7.15 regression (kind C on v0.7.14.1, BUG-0402 / BUG-1106 of 0.7.7-0.7.14 the same class).

## Fix
None in this triage (fix/w51-t4 had the Tribe / Civ final-check failures to classify). Candidates: compare Tribe and
Civ NPCs at the guest's own height (the monitor's proxies rule) for kind A; ground a walked Civ proxy (kind B); trace
the herd's ids on a guest for kind C. Needs its own package with the segment rerun above as the test.

## Verification
* FAIL runs: 20261116-025154:campaign_tc2 (rel/0.7.15), 20261116-085558:campaign_segment (fix/w51-t4 = rel/0.7.15's mod
  code), 20261116-091255-2:campaign_segment#1 (v0.7.14.1, fix/w51-t4-v07141 worktree).
* PASS: 20261116-084409:campaign_segment (fix/w51-t4).

## History
- 2026-11-16: opened (fix/w51-t4, final-check triage 0.7.15): old, three kinds; not fixed in the triage.
