# SporeCoop 0.7.15 audit: tests, lab, coverage

Auditor area: tools/lab.py and helpers, tools/tests/*.py, invariant monitor / log gates / detector, the campaign bot,
unit tests in tests/, coverage. Repo state: main at a762f240 (v0.7.14). Read-only: no lab runs were started, `make test`
was not run either (the machine was at load 15 on 12 cores from lanes in use; the owner's rule is no extra load
during lab work). All numbers come from the recorded runs and the source.

Data set: every `results.json` under `SporeCoop/out/lab` and `SporeCoop-wt/**/out/lab` (4842 runs, 17 430 test
results, 2026-10-23 .. 2026-11-14, 846 test-hours). "fix-off" results (twin runs with a fix switched off on purpose)
were found by the `SPORE_FIX_OFF: <x> off` line in each test log (1901 results) and left out of every failure rate.
Scripts: `/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/{agg,stats,flaky2,fixoff}.py`.

## Summary verdict

The test system is large, careful and well documented (991 suite entries, 6 never run, a lint that keeps every test
in a suite, self-tests for the lab, gates, monitor and walk analysis, seeds and replay lines in the bots). Its
problems are cost and noise, not missing tests. 28 % of all recorded test time is in failed results; on rel/main
7.9 % of test results fail, and in the 0.7.14 final check only about 2 of 13 failures were confirmed mod bugs. The
cost concentrates: the campaign family is 23 % of all lab time and fails 60 % of its full runs on rel/main (half of
them the bot stalling in a stage change); start and reset of the games are 17.5 % of all lab time; repeated runs of
one test on one branch are 29 %. The test code is copy-paste heavy (90 private `wait()` copies, 851 hand-rolled poll
loops, 4119 sleeps, 337 tests importing other tests) and its thresholds are not tied to measured fps or machine
load. The best returns are in the lab's own tooling (flake accounting, resource-aware scheduling, campaign
behaviour after a stall, warm-state contract), not in deleting tests.

## Metrics

### Volume and where the time goes

| what | value | source |
|---|---|---|
| suite entries (tools/tests/suite.py) | 991 (full 800, nightly 905, extended 83, players 73, multi 29, detector 18, smoke 14, campaign 6, mixed 3) | `suite.T` |
| test files in tools/tests | 706 (638 coop_*, 11 kit_*, 5 play_*), 103 133 lines; median coop_* file 111 lines, largest coop_campaign.py 2022 | wc |
| warm / cold entries | 478 / 513 | suite.py |
| never run (labcov) | 6: mem_census_tour_both, mem_census_tour_solo_10, mem_census_tribe, mem_census_tribe_solo, perf_readable_tribe, update_api403 | `lab.py coverage --days 14` |
| recorded lab time | 846 test-hours in 22 days (peaks 80.8 h on Nov 1, 83.0 h on Nov 12) | results.json |
| time in failed results | 241 h (28 %) | |
| fix / feat branches | 457 h normal + 65 h fix-off twins (8 %) + 6 h quick | |
| rel / main | 314 h, of it 80 h in failed non-twin results (25 %) | |
| (branch, test) pairs run 5+ times on one branch | 246 h (29 % of all lab time); max: tribe_walk_mobile 177 runs on fix/w45-remote-bodies-2 (8.2 h), cell_particles 91 on fix/w26-cell-spare | |
| game start / warm reset time | 148.5 h (17.5 % of all lab time); cold start median 33 s (p90 45, n 13 306), warm reset median 19 s (p90 23, n 2572) | `start` field |
| warm reset phases | host in game 5-6 s, guest in game 15-17 s (rejoin + world transfer + load), then a fixed 3 s sleep | twoinst.py:567-617, logs |
| cold start phases | wine up 4, host menu 10-18, host in game 16-25, guest in game 29-38 s, then a fixed 4 s sleep | twoinst.py:849 |
| campaign family | 195 h (23 % of all lab time), 577 results, 84 h of them failed | |
| play_* bots | 121 h, 689 results, 23 h failed | |

### Suite cost (sum of each test's median passed duration)

| suite | tests | test-hours | on 6 lanes |
|---|---|---|---|
| smoke | 14 | 0.4 | ~0.1 h |
| full | 800 | 23.0 | ~3.8 h |
| nightly | 905 | 33.2 | ~5.5 h |
| extended | 83 | 8.0 | ~1.3 h |
| players | 73 | 3.1 | ~0.5 h |
| campaign | 6 | 2.6 | (critical path: `campaign` alone 82 min) |
| release rule union (smoke, full, nightly, extended, players, multi, campaign) | 979 | 38.7 | ~6.5 h |

In `full`, 634 of 800 tests take under 2 min (12.3 h together); the top 10 % of tests take 32 % of the time, the top
25 % take 51 %. Fixed start overhead inside `full`: about 3.0 h cold (327 x 33 s) + 2.5 h warm (473 x 19 s) = 24 %.

### Release-cycle cost (runs on rel/X branches)

| release | runs | wall h | test h |
|---|---|---|---|
| 0.7.9 | 22 | 14.0 | 20.5 |
| 0.7.10 | 18 | 8.1 | 15.6 |
| 0.7.11 | 17 | 7.5 | 16.9 |
| 0.7.12 | 10 | 9.1 | 31.2 |
| 0.7.13 | 21 | 10.9 | 25.8 |
| 0.7.14 | 17 | 7.9 | 20.7 |

Final checks: 0.7.13 `20261112-175447` 154 tests, 174 min wall, 9.1 test-h, 10 failures, 111 cold / 32 warm starts
(67 min of cold starts). 0.7.14 `20261114-005006` 111 tests, 95 min wall, 7.9 test-h, 13 failures, 66 cold / 37 warm
(47 min of cold starts), the eight longest tests (campaign 4947 s, campaign_civ_jump 2617, campaign_tribe_noclick 2607,
campaign_tc2 2086, ...) are 55 % of its test time; plus the rerun `20261114-050940` (55 tests, 92 min). Each release
ran 2-4 final-check-sized runs.

0.7.14 final check failures by verdict (docs/06-testing.md#testing-fcgates-0714, #testing-w49-warmfault, tickets):
confirmed mod bugs 2 (civ_wild_pooled BUG-1100, campaign_tribe_noclick BUG-1101); test or bot 5 (tribe_attack_stale
precondition, tribe_away_bundle BUG-1102, puppet_walk traceback BUG-1103, tribe_civ_editor_slow driver BUG-1096,
campaign_tribe_end); warm-state leak 2 (same_view and npc_ai_lookup faults BUG-1097; npc_ai_lookup 0 reuses BUG-1099);
machine load 2 (campaign_civ_jump hitch, npc_walk_tribe p50 0.11 > 0.10 at 22 fps); unexplained 2 (campaign ground
BUG-1104, tribe_foreign_click).

### Flakiness

* rel/main, fix-off excluded: 4494 results, 353 failures (7.9 %); 122 of 796 tests failed at least once there; the 20
  worst tests hold 53 % of the failures.
* The same test on the same commit both passed and failed (rel/main only, fix-off excluded): 98 of 232 commit/test
  pairs with 2+ results, in 54 tests. All branches: 619 of 1559 pairs, 306 tests (fix branches may run uncommitted
  trees, so this is an upper bound there).

Worst on rel/main (6+ runs, fail / runs): partner_pass 6/8, tribe_camera_null 5/7, editor_now 5/7,
body_trail_tribe_player 5/7, effects_party_civ 5/8, **campaign 38/63**, tribe_member_air 4/8, editor_strays 3/6,
puppet_jump 6/13, defer_identity 6/13, pack_target 5/11, puppet_stops 8/19, update 4/10, long_tribe_short 4/10,
long_creature 5/13, play_creature 19/60, play_civ 17/65, play_tribe 12/55, campaign_space 11/65.

Mixed results on one commit, all branches (pairs mixed / pairs): tribe_walk_mobile 29/49, play_creature 16/25,
campaign 11/19, play_tribe 11/22, puppet_stops 10/18, puppet_walk 10/20, npc_walk 10/20, cell_coop 9/22,
campaign_tribe 9/29, tribe_walk 9/20, tribe_npc_dash 9/17, body_trail_tribe_player 9/19. The walk / dash / body-trail
family (load-sensitive motion measurements) dominates.

Failure kinds on rel/main (first matching reason): assertion 170, monitor gate 35, sync-check gate 26, fault count 20,
harness 6, died 4, report 4, hitch 1. Tracebacks over all branches: 201 failures (ConnectionError 63, TimeoutError 54,
then plain Python bugs: AttributeError 12, NameError 12, TypeError 11, KeyError 7, ModuleNotFoundError 8);
space_badges_rejoin alone 28. pyflakes on tools/tests and tools today: 96 warnings, 0 undefined names (the NameErrors
were fixed), 49 unused imports, 36 unused locals.

### Tickets of kind test

155 of 1103 tickets are `kind: test` (147 fixed, 5 not-reproduced, 2 duplicate, 1 wontfix); 57 more are `tooling`.
Ticket sources: campaign 85, detector 67, final check 57, gate 23, key frame 29, play_* 27 (the long tests do find
bugs). Recurring classes, by title (a ticket can be in two):

| class | ~count | examples |
|---|---|---|
| luck / precondition / bot input (the scenario did not happen, a click missed, the bot picked a wrong NPC) | 35 | BUG-0618 joint hunts +0, BUG-0934 click picks another object, BUG-0690 bites never kill, BUG-0761 no conquest cinematic, BUG-0294 NPC without proxy, BUG-1091, BUG-1096 |
| measured at the wrong moment / from the wrong point | 29 | BUG-1105 cut measured from the hand set, BUG-0586 calibration during the join pause, BUG-0944 click into the stage-entry pause, BUG-0628 meal before the first sample, BUG-0984 / 0924 key frames at bad moments |
| gate / monitor / detector misjudging | 26 | BUG-0439 two transfers read as a bad link, BUG-0393 view jump, BUG-0681 / 0662 two world scales, BUG-1102 forced host-away rounds, BUG-1104 |
| machine load / fps / VRAM | 23 | BUG-0407 walk tests, BUG-0321 / 0322 / 0363 dash p90, BUG-0527 relay cost check, BUG-0663 Xwayland, BUG-1098 VRAM over 18 games |
| bookkeeping instead of game state / cannot fail | 10 | BUG-0135, 0148, 0222, 0888 (dead twin switches), 0891, 0892, 0974 (planted desync no longer fails) |
| warm reuse / test order | 10 (with tooling) | BUG-0257, 0312, 0356, 0396, 0437, 0601, 0898, 0975, 1097, 1099 |
| missing coverage | 9 | BUG-0162, 0152, 0168, 0748, 0750 |

### Unit coverage

* tests/*.cpp: 10 202 lines in 11 programs (net_tests 4348 with 72 tests, win_tests 1934, validate_tests 1103,
  fuzz_decoders 1066, npc 645, cell 326, fixes 255, space 185, text 156, jpeg 97, release_tsan 87), plus Python
  self-tests (lab_selftest 68 checks, gates, walk, dash, tools, visual, state fields, test_lint). `make test` takes
  about 49-63 s since feat/lab-speed (the lab's `unit` result: 48-63 s on 0.7.14, 321-386 s before).
* Code with unit coverage: mods/coop/net (6.6k lines) and 35 pure headers (cell_logic.h, settings_core.h,
  find_core.h, host_world.h, call_guard.h, realign_rule.h, space_logic.h, dna_ledger.h, drive_plan.h, lifecycle.h,
  busy.h, ...): about 11k of the 101.7k lines of mods/coop (~11 %). 46 of the 80 headers in mods/coop have no test.
* Only through the lab: coop.cpp 8626, tribe.cpp 7811, cellsync.cpp 7331, civ.cpp 6647, npc.cpp 4258, creations.cpp
  3756, spacecoop.cpp 2803, game.cpp 2692, coop_ui.cpp 2189, space_trade.cpp 2163, world.cpp 1968, worldobj.cpp 1892,
  wsync.cpp 1818, modes.cpp 1748. Bug / desync / crash / freeze tickets per stage: Creature 209, All 153, Tribe 151,
  Space 111, Cell 97, Civ 74.

## Findings

### TST-1 (high): the campaign is the most expensive and least reliable test, and its failures are partly invisible

Evidence:
* The campaign family used 195 of 846 test-hours (23 %); `campaign` (82 min) fails 38 of 63 runs on rel/main (60 %);
  campaign_space 11/65, campaign_tribe and campaign_short mixed on one commit in 9/29 and 7/31 pairs.
* Of 144 campaign failures (all branches) 56 contain "the stage change X -> Y did not finish in N min" (the bot did not
  get the stage done: mostly Tribe raids), 38 `[stuck]`, 25 `[desync]`, 24 `[fault]`, 17 tracebacks, 7 crashes.
* 46 of the 144 have `RESULT: FAIL` with no line starting with `FAIL`: the campaign prints findings as
  `[stuck] ...`, `[census] ...` (coop_campaign.py:29, :1966). tools/lab.py:640 collects only `FAIL` lines, so
  results.json, summary.md and flaky.json show an empty reason (example: lab 20261029-122937 campaign#1, and
  campaign_tribe_end in the 0.7.14 final check).
* A stalled advance ends the run unless `--jump` is given (coop_campaign.py:1958-1975); the `campaign` suite entry
  (suite.py:966) has no `--jump`, so a Tribe stall at minute 30 throws away the Civ and Space halves of an 82 min run.
* It is also the most productive bug source (85 tickets name a campaign run).

Bug link: BUG-0290, 0311, 0739, 0991, 1079, 1091, 1096 (bot), BUG-1101, 1104 (final check 0.7.14).

Proposal:
1. Campaign findings that fail the test also print `FAIL <kind> <phase>: <text>` (keep the bracket lines). S.
2. `campaign` in the suite runs with `--jump` (CAMPAIGN_STRICT_TRIBE stays strict: the stall is still a FAIL), so one
   run always reaches Civ and Space and reports all findings. S.
3. Final-check reruns: rerun only the failing segment test (campaign_ct, campaign_tc2, campaign_space, ...) unless the
   full campaign itself found a mod bug; the full campaign runs once per release on the release mod code
   (release rule 2 already allows "one whole campaign"). Process change, S.
4. Track the bot's stall rate as its own number (`stuck advance` per stage) in the summary, so bot work is
   prioritised by data (Tribe raids are the top).

Effect: up to 50 min of campaign play recovered per stalled run, empty failure reasons gone, fewer 82 min reruns.
Size S-M. Risk: low; with `--jump` a later stage starts from the jump fixture, so it is not the continuous save
(note that in the result). Verification: replay recorded logs of the 46 empty-reason failures through the new
FAIL printing (a unit case in lab_selftest); run `campaign --seed <a seed that stalled>` with `--jump` and check
that the Civ / Space findings appear.

### TST-2 (high): flake accounting is fragmented and counts twin runs as failures

Evidence:
* tools/lab.py:901 writes `out/lab/flaky.json` under `ROOT`, the checkout the lab runs from. There are 355 such files
  (main + worktrees); main's covers 1785 of 17 430 results (10 %). The summary's "Unstable tests (all runs)" is
  therefore per worktree.
* `failed` in flaky.json includes the intended FAILs of fix-off twin runs (1901 results); results.json has no field
  for the switch (it can only be found by grepping the log for `SPORE_FIX_OFF:`, as labcov's fixcheck does).
* measured_seconds() (lab.py:652) and labcov already read every worktree via `loggates.lab_dirs`; flake history
  does not.

Bug link: none (new). Related: BUG-0362 (flaky.json lock).

Proposal: `lab.py flaky [--days N] [--branch rel/*]` computed from all results.json (like labcov): per test
runs / fails / fail rate on rel and main, "mixed on one commit" count, last reasons, fix-off and quick results
excluded; record `fix_off` and `fixture`/`warm` in each result of results.json; drop flaky.json or make it a cache of
that. A failing test in a final check then prints "known flake: 5 of 11 on rel/main since <date>" next to it. S.

Effect: agents stop investigating known flakes from zero; the flake list drives TST-3 work. Risk: none to the mod.
Verification: compare its output with this report's numbers (e.g. campaign 38/63, partner_pass 6/8 on rel/main).

### TST-3 (high): load-sensitive thresholds and a scheduler that does not count the machine

Evidence:
* The walk / dash / body-trail family is the top of the mixed-result list (tribe_walk_mobile 29/49 commits,
  puppet_walk 10/20, npc_walk 10/20, tribe_npc_dash 9/17, puppet_stops 10/18, body_trail_tribe_player 9/19).
* npc_walk_tribe failed the 0.7.14 final check at p50 0.11 m against ERR_P50_TRIBE 0.10 (coop_npc_walk.py:64-65, :544)
  while the fix gives 0.01-0.03 and no fix 0.23-0.33; over 13 runs the p50 follows the guest's fps (0.02 at 46-66
  fps, 0.11 at 22, 0.14 at 14: docs/06-testing.md#testing-fcgates-0714). The limit sits at a quarter of the gap,
  not in its middle.
* 789 `ck(...)` lines compare against a literal number; only 9 coop_* files scale a limit with measured fps.
* The lab caps lanes (6), not games: the 0.7.14 final check ran 22 games (five 4-player tests); VRAM fits about 18
  (docs/06-testing.md#testing-gpu-overcommit, BUG-1098: garbage key frames). MemGate (lab.py:483) knows only this
  run's tests and RAM; the shared headless Xwayland saturates at 10-14 games (BUG-0663); the XwSampler only labels
  a failure `(load)` afterwards (lab.py:640-648, 13 results carry it).

Bug link: BUG-0321, 0322, 0363, 0387, 0407, 0527, 0663, 1098.

Proposal:
1. Threshold rule: every limit of a twin-checked measurement documents the measured PASS and FAIL ranges next to it
   (several already do) and sits between them with margin (for npc_walk_tribe ~0.12-0.15, still far under 0.23);
   where the value follows fps, the limit is a function of the observer's measured fps (the tests already print it).
   Add to test_lint: a module-level limit constant needs a `# pass a-b, fail c-d` comment. S per test, M overall.
2. Scheduler: a machine-wide game budget (count `SporeApp.exe` processes with `pgrep -x`, read
   `/sys/class/drm/card*/device/mem_info_vram_used`, the Xwayland ticks it already samples) in MemGate's admit():
   a 4-game test waits while games >= ~16 or Xwayland > 80 %. Mark the motion tests `load_sensitive=True` in suite.py
   and never start two of them beside a 4-game test. S-M.
3. A `load` failure is rerun once on the same lane when the machine is calmer, reported as `(load, rerun ok)`; still
   counted in TST-2's history. S. (Only for results the lab marked load, so no real failure is hidden silently.)

Effect: the largest single flaky family and the VRAM garbage class mostly go away; fewer final-check reruns.
Risk: a limit moved too far stops catching the bug: every moved limit must be re-twinned (SPORE_FIX_OFF FAILS).
Verification: fixcheck after each move; rerun the motion family 5x on a loaded lab and compare the mixed rate.

### TST-4 (high): warm reuse leaks state between tests, and the class grows with the warm move

Evidence: 232 tests were made warm in 0.7.14 (docs/06-testing.md#testing-w49-warm). Warm-order bugs so far: BUG-0257
(disabled guest), 0312 (fix switch left off), 0356 / 0396 (absolute counters), 0437 / 0601 (NPC numbering, family),
0898 (co-op window left open), 0975 (crash on the next reset), 1097 (fault count kept), 1099 (heap reuse after many
warm runs). Two of the 13 failures of the 0.7.14 final check were this class. twoinst keeps its own list of what to
reset (warm_reset, forget_holds, forget_settings, KNOBS, clear_faults: twoinst.py:467-617), and each new leak adds one
more line there.

Bug link: the tickets above; BUG-1055 (lifecycle registry question in plan-0.7.14).

Proposal: a warm contract instead of a growing list:
1. A console command in the mod (`coop.testreset` or part of the BUG-1055 lifecycle registry) that runs every
   module's session reset and prints a fingerprint: fix switches, test knobs, fault count, NPC id counter, holds,
   open windows, per-module counters that tests read.
2. twoinst records the fingerprint after a cold start per fixture (lane_state) and compares it after each warm reset;
   a difference refuses the warm start (cold instead) and prints which keys differed.
3. Nightly: run each warm batch once in a shuffled order (seed printed) to flush out order effects.
Size M (mod side small if the lifecycle registry lands; harness S). Risk: low; a refused warm start costs 14 s.
Verification: the recorded leaks as cases: after kit_cmd_fault, after a fix-off run, after puppet_fight: the
fingerprint differs before the fix of each and the refusal triggers (lab_selftest case + one lab A/B).

### TST-5 (medium): copy-paste harness code and tests used as libraries

Evidence (tools/tests):
* 90 private `def wait(cond, secs, step)` copies (identical bodies, e.g. coop_cell_end_sync.py, coop_cell_away.py,
  coop_cell_camera.py), 45 `sp()`, 46 `mode()`, 39 `num()`, 23 `faults()`, 24 `field()`, 15 `own_pos()`, 15
  `log_size()`; 851 hand-rolled `while time.time() - t0 < N` loops in 483 files.
* 4119 `time.sleep` calls in 675 files; 3891 with a literal (sum 12 195 s written into scripts; the most common: 1 s
  x656, 3 s x619, 2 s x601).
* 1122 `re.search(r'...')` parses of console text in coop_* tests, against 125 distinct console commands
  (coop.pool 660 calls, coop.tribe 495, coop.status 445, coop.npc 419); no command has a machine-readable output.
  Output format changes broke tests before (play_creature counted no kills after a format change:
  docs/stability-report.md).
* 337 of 638 coop tests import from other test files (63 from coop_ui, 20 coop_creature_kills, 19 coop_multi,
  18 coop_civ); play_tribe imports LIMITS from coop_sync.
* "Pick a wild NPC" is solved in each test: 56 files parse `coop.npc list`; the first-listed-NPC bug class
  (BUG-0437, 0601, 0417) was fixed per test, with "not checked yet" for others (docs/06-testing.md#testing-harness-g2).
* tools/tests/run_all.sh: a 2026-10-23 copy of the old suite list (unused since the lab; suite.py:1 still names it).
* 10 copies of the `g_fail` / `CHECK` macro in tests/*.cpp (cosmetic).

Proposal: a `tools/tests/harness.py` (or twoinst additions): `wait_until(cond, timeout, step, what)` that prints how
long it waited and the last value on a timeout; console helpers (`mode`, `faults`, `kv(out)`, `num`, `field`);
`pick_npc(con, wild=True, not_family=True, near=..)`; move shared pieces that other tests import (coop_ui,
coop_creature_kills, coop_multi, coop_civ helpers) into library modules. Migrate the identical copies by a codemod;
leave odd variants alone. Delete run_all.sh. S for the helpers and codemod, M for moving the libraries.
Effect: no direct lab time; the "waited for the wrong thing" and "picked a wrong NPC" classes (64 test tickets) get
one place to fix; timeouts become measurable (wait telemetry tells which sleeps are too long). Risk: low if the
codemod only replaces byte-identical bodies. Verification: test_lint learns to flag a new private `def wait(`;
run the touched tests' smoke + one stage batch.

### TST-6 (medium): fixed settle sleeps and the warm reset's world transfer sit in every test's critical path

Evidence: cold start ends with a fixed `time.sleep(4)` (twoinst.py:849), warm reset with `time.sleep(3)`
(twoinst.py:614); a warm reset is 19 s of which the guest's rejoin, world transfer and load are about 10-11 s (host in
game at 5-6 s); start and reset are 148.5 h, 17.5 % of all lab time; in the full suite 5.5 of 23 test-hours.
BUG-0869 (open) describes a world resume for an unchanged world (taken from a fork).

Proposal:
1. Replace the two fixed settles with a condition: both games' frames advance and game time runs (`game_runs`,
   `_game_busy` exist), with the old value as the upper bound. S. About 3 s x ~990 tests = ~0.8 test-h per nightly.
2. Warm reset without the transfer: when the guest already holds the same fixture world (the lab state knows the
   fixture), let it load its own copy of the fixture slot and join with a resume receipt (BUG-0869 design), keeping
   cold for every join / share test. M. About 8 s x 473 warm tests = ~1 h per full.
Risk: (1) tests that relied on the extra seconds to settle start too early: run each batch before switching;
(2) touches the player-facing join path in world.cpp: only behind a switch and with its own tests; may be judged not
worth it for 1 h if TST-1/3 are done first. Verification: start-phase timings in the log (already printed), the
warm batches of #testing-w49-warm run twice with and without.

### TST-7 (medium): no test impact selection for intermediate runs

Evidence: lab.py has no "changed files" selection (grep: none). Of 113 merges in v0.7.10..v0.7.14, 22 touched no
mod / sdk / sporekit / updater code (tests, tools, docs only), 22 only stage modules, 69 core files (coop.cpp,
net/, game.cpp, sporekit/). 165 full tests have `fixture=None`, so the stage of a test is only in its name.
A tribe-only change needs the 172 Tribe tests (5.5 h) + 83 cross/core tests (2.9 h) of `full`, not 23 h.

Proposal: `lab.py run impact --base <rev>`: the tests whose script or imported test library changed, the stage
batches of the changed stage modules (a map in suite.py: tribe.cpp -> tribe tests, cellsync.cpp -> cell, ...),
smoke always, everything for core files. For the "integration run after every two merges" and fix branches only;
the final check and the release rule stay full. Give every suite entry a `stage` (derived once from fixture / name).
Size M. Effect: the 19 % tests-only merges need minutes, not an integration run; stage-only merges about 60 % less.
coop.cpp (8.6k lines) is in most core merges; moving its stage-specific commands and hooks into the stage files
(a refactor topic of another auditor) is what makes the selection pay for the rest. Risk: a cross-stage regression
slips to the final check (where it costs a rerun). Verification: replay the 113 merges: which final-check failures
of 0.7.12-0.7.14 would the impact set have caught at merge time.

### TST-8 (medium): the development loop repeats whole tests to measure rates

Evidence: 246 h (29 % of all lab time) in (branch, test) pairs run 5+ times on one branch; 65 h of fix-off twins.
`--repeat N` (lab.py:706) runs N named copies; a cold test restarts both games each time; nothing stops early once the
answer is clear, and the agent computes the rate by hand from the summary.

Proposal: `lab.py run X --repeat N --until fail|pass|K-fails` with a printed rate and a 90 % interval at the end;
warm tests repeat on warm games; a dev-only `SPORE_FAIL_FAST=1` ends a test at its first FAIL when only a
reproduction is wanted. S. Effect: fewer runs per reproduction ("fails 1 in 3" needs ~6-9 runs to show, not 20).
Risk: none to the release (dev mode only). Verification: lab_selftest cases for the stopping rule.

### TST-9 (medium): the stage modules have little unit-testable pure code

Evidence: about 11 % of mods/coop has unit coverage (net/ and 35 pure headers); the stage modules (45k lines) are
reached only through the lab, and Creature (209), Tribe (151) and Space (111) carry most mod tickets. The extraction
pattern works where used: cell_logic.h, realign_rule.h, dna_ledger.h, drive_plan.h, busy.h, call_guard.h,
lifecycle.h each replaced lab-only checks with ms-fast cases, and unit twins (`--nofix --expect-fail` for net_tests,
98 FAILs) prove they can fail.

Proposal: extract decision logic that is not about game memory into `*_rule.h` headers with unit tests, starting
with the rules behind recurring lab-only bugs: Tribe member ownership / selection rules and order acceptance
(tribe.cpp; BUG-1087 class), NPC proxy placement and ground / stuck judgement (npc.cpp; BUG-1106, 0952), the
world-share / save queue state machine (world.cpp; BUG-0954, 0331), Civ money / city ownership bookkeeping
(civ.cpp; double-counted derricks). Each extraction keeps its lab test; the unit case also gets its twin.
Size M-L (per module M). Risk: refactoring live code paths: one module per branch, lab stage batch before merge.
Verification: unit cases fail with the old rule (twin), stage batch passes.

### TST-10 (low): small lab hygiene items

* pyflakes in `make test` (unit-lint): 96 warnings today (49 unused imports, 36 unused locals), 0 undefined names;
  21-40 past failures were NameError / TypeError / AttributeError / KeyError tracebacks a lint would have caught
  earlier. S, low value now that the code is clean; add it so it stays clean.
* `minutes` in suite.py is off by more than 2x for 512 tests (the scheduler now uses measured medians, so only the
  memory and quick logic still read it): derive it from the records in `lab.py coverage`. S.
* The six never-run tests (mem_census_tour_both, mem_census_tour_solo_10, mem_census_tribe, mem_census_tribe_solo,
  perf_readable_tribe, update_api403): run once or drop them. S.
* `campaign` precondition-type failures in other tests (tribe_attack_stale "the scenario did not happen": no
  member died in 60 s) fail like a bug. An `INCONCLUSIVE` verdict (the test's own precondition about the game or bot
  did not happen) retried once by the lab and counted in TST-2's history would remove some noise; it must not be
  used where the mod can be what prevents the scenario. Needs an owner decision (see Open questions). S.

## Not worth doing

* Deleting tests to save time: 260 full tests ran 5+ times and never failed, but together they take 5.6 of 23
  test-hours; most are short regression guards for fixed tickets. The cost is in the long tests, start overhead and
  reruns.
* Making `--quick` or fewer rounds the final check: already ruled out (quick missed BUG-0910 at 3 min); keep.
* More lanes or more games at once: VRAM (~18 games) and the single Xwayland are the limit; more parallelism raises
  the load-flake rate (TST-3).
* Broad retries of failed tests (pass-on-retry = ok): it would hide real one-in-N bugs, which the project has many of
  (the lab's rule since BUG-0073). Only the `load` and harness cases deserve an automatic second run.
* Replacing the lab with pytest or another framework, or a fake game for integration: the game is the system under
  test; the lab's custom pieces (lanes, gates, monitor, warm reuse, fix twins) would all have to be rebuilt.
* Converting all 4119 sleeps: most are inside test scenarios (input pacing, animation time) where a condition is not
  defined; only the per-test critical path (TST-6) and the waits behind known flakes are worth changing.
* Loosening the monitor / sync-check gates in general: they found real bugs (the detector is the source of 67
  tickets, the gates of 23); fix the misjudging rules one by one with replay (monitor.py replay) as done so far.
* Unifying the C++ CHECK macros: 10 short copies, no bug history.

## Open questions

1. Must every final-check rerun repeat the 82 min `campaign`, or is one whole campaign per release cycle on the
   release mod code enough, with segment tests in reruns (TST-1)?
2. Would the owner accept an `INCONCLUSIVE` verdict for a test whose precondition did not happen (retried once,
   counted, never on the release rule's last run)?
3. Is a second headless Xwayland (one per 3 lanes) possible with the agent-workspaces / headless shell setup? It
   would lift the load ceiling more than any scheduler (BUG-0663).
4. Is BUG-1055 (lifecycle registry) planned for 0.7.15? TST-4's fingerprint is cheapest on top of it.
5. Splitting coop.cpp's stage-specific parts (for TST-7) overlaps the code-structure audit: who owns it?
6. `make test` was not run in this audit (machine under lab load); its last recorded lab `unit` results (48-63 s,
   rel/0.7.14) passed except one on 20261113-025531.
