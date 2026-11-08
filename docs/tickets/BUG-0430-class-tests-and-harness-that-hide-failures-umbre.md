---
id: BUG-0430
title: class: tests and harness that hide failures (umbrella)
status: fixed
kind: bug
stage: Tests
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-g4-0430
commits: 5be1f0c3, bebe5304, 32285a2d, 7bf2df3e, 8972110d, f1bd2a8d, 7e34eb44, 17ac4d37, 35f71bc2, 9e8ac0dd, f41c071e, 2aa45910, 353eb128, 5cfbe0fb
tests: make test (test_lint, re_lint, tickets twin), creature_desync, civ_role_damage, cinematic_puppet, nest_objects, cell_sync_check
signature:
docs: docs/06-testing.md#testing-g4-tests
related:
---

## Report
Not covered: warm tests switch fixes off without the lane record (coop_civ_ruin_repair.py:33, coop_alt_enter.py:95, coop_guest_missing_pack.py:97); knobs reset only on success (coop.worldfix retry 0); BUG-0356 probably this; bookkeeping asserts (coop_party_creature:84/123, coop_creature_kills:125, coop_cell_away:122; tests of 0010, 0011, 0040, 0059, 0064, 0066, 0082); tests that cannot fail (0095, 0096, 0098, 0172, sync_lowfps, 0340 scenario); coop_nest_objects.py in no suite; 0109/0110/0122/0169 unit only; fresh-only / tolerance filters (0321, 0379) hide lateness; BUG-0021 status stale; 0353 == 0362; re_lint does not scan tools/*.py; no .sym return-type check (ApplyRelationship lacks the ST0 note).
General fix: warm_reset restores all switches and knobs; lint asserts on coop.* counters; make test checks suite membership and that every fixed ticket names a switch or reason; `returns:` in .sym checked by re_lint.
Source: docs/generalisation-plan.md G14.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
G4 (fix/w46-g4-0430, merged into fix/w46-g4-tests), one mechanism per class:
* Warm reuse refused for games whose switches or knobs a test set (sporectl.Console.on_call notes every such call in
  the lane state, set back or not; a cold start records the games' own switch report, `coop.diag` settings and kit.fix,
  a warm start compares it): the warm tests that switched fixes off and `coop.worldfix retry 0` (5be1f0c3).
* re_lint scans tools/*.py and tools/tests/*.py and needs `returns: float (ST0)` for float results; five GOG 2024
  entries completed, Graphics::IRenderer::Get verified (bebe5304).
* tickets check `twin`: a fixed bug names its twin switch or the reason it has none (32285a2d).
* tools/tests/test_lint.py in make test: every game test in a suite (nest_objects added), checks on mod counters need a
  reason (COUNTER_ALLOW with "pending" marks) (7bf2df3e).
* Bookkeeping asserts moved to the game's state: BUG-0010 creature_desync (the companion's own object in the guest's
  game under the guest's key of the creation; twin `creations.ask_species`) (8972110d, f1bd2a8d); BUG-0064 new
  civ_role_damage (the host's building hit points per hit of the guest's vehicles: no role / military / after the
  rejoin) (7e34eb44, 17ac4d37); BUG-0066 cinematic_puppet (the guest's game checked for a copy of the old puppet;
  9e8ac0dd).
* Fresh-only / tolerance filters: BUG-0321 tribe_npc_dash SKIP only on a slow host, the share of fresh states checked
  (f41c071e), then judged at the same moment on both games' own positions (BUG-0363, fix/w46-g4-dash); BUG-0379 the
  sync-check counts what only its late windows let through and three such checks in a row are an issue (`late`,
  `sync_late=`, switch `diag.late_streak`) (2aa45910, 353eb128).
* Tickets: BUG-0021 closed, BUG-0362 duplicate of BUG-0353, BUG-0169 / BUG-0098 lines (5cfbe0fb).

## Verification
* creature_desync (BUG-0010): PASS 2 of 2 out/lab/20261108-084439; FAIL with SPORE_FIX_OFF=creations.ask_species,creations.own_species
  20261108-085007 (both companion checks).
* civ_role_damage (BUG-0064): PASS 2 of 2 20261108-085404; FAIL with SPORE_FIX_OFF=role.fix 20261108-085906 (B 26.0 =
  1.25 x A 21.0, the old player's military role kept).
* cinematic_puppet (BUG-0066): PASS 3 of 3 20261108-090139; FAIL with SPORE_FIX_OFF=npc.deferpuppet 20261108-090454 on
  the host check "the old puppet is no NPC of the host"; the new guest-side check did NOT fail there (no copy reached
  the guest's game within the cinematic): the host check stays the one that catches BUG-0066.
* nest_objects (now in a suite): PASS 2 of 2 20261108-081333.
* sync-check late (BUG-0379): test_diag_compare (make test) covers a rising total (not late), a stood-still host value
  the guest misses (late), the streak; the first version raised a false `late` MISMATCH (cell_sync_check
  20261108-080337 #1, a rising food total), fixed by 353eb128: cell_sync_check + cell_pause 6 of 6
  20261108-090600, SPORE_NET=mobile cell_sync_check 1 of 1 20261108-091700, no `late` issue.
  no-fail-run (lab): a guest late for good is not made on demand; the unit test is its FAIL case.
* lab_selftest under a background lab (BUG-0743): unit 3 of 3 20261108-072703.
* The warm-reuse rule: the lab printed "(no warm reuse: an earlier test set `frame.limit 66`; cold start)" in this
  branch's runs (e.g. 20261108-075604 tribe_npc_dash#3) and warm runs still happened elsewhere (creature_social_ally
  #2/#3 "warm reset", 20261108-074443).

## History
- 2026-11-01: opened.
- 2026-11-04 (fix/w43-tests): G1 (fix/w43-tests): tribe_guest_tribes' twin run passed step 1 on the bug since W2 (BUG-0587); play_tribe calibrated during the cold join pause (BUG-0586); cell_coop's meal (BUG-0356); cell_new_game's empty screen (BUG-0293); the proxies gate was right (BUG-0503, a real desync fixed). Section docs/06-testing.md#testing-harness-g1.
- 2026-11-05 (fix/w44-tests, G2): long_creature_short (rel/0.7.8 out/lab/20261105-123757, lane 4) failed "restart:
  console error TimeoutError('timed out')" and so "the games survived the session": the restarted guest joined at
  12:45:17, loaded the shared world until 12:46:09 (52 s) and played on (its game log); the console calls of
  Session.restart / twoinst.wait_guest_in timed out while it loaded and the first exception ended the transition. Kind:
  harness (it measured the console's answer time, not the game). coop_long_session.py: restart, rejoin, menu and
  wait_together take a console timeout as "not yet" (busy_ok) and wait on until their own deadline (240 s join, 120 s
  together, as wait_together already had); a guest that never comes back still fails. Not rerun in the failing
  timing (a 52 s load is not reproducible on demand). G2's other cases: docs/06-testing.md#testing-harness-g2
  (BUG-0397, BUG-0437, BUG-0601, BUG-0589, BUG-0587, BUG-0440).
- 2026-11-08 (fix/w46-g4-tests): G4: every item of the umbrella closed (fixed, already fixed, or a ticket of its own); the BUG-0363 dash measure now shows a real bug, BUG-0753 (open). Closed.
