---
id: BUG-0214
title: Tests: the coop.fix registry misses switches, some switches turn off several fixes, and the registry check cannot see it (BUG-0116 incomplete)
status: fixed
kind: test
stage: All
reported: 2026-10-28
source: GitHub #148 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.1
branch: fix/w5-tools
commits: 8124796, 964b098
tests: make test (fixes_tests, test-asan), fix_registry, leave_cleanup, vote_stale, tribe_chief_removed, win_unit
signature:
docs: docs/06-testing.md#testing-fix-rules
related:
switch: none
---

## Report
From GitHub issue #148: https://github.com/WoodyWizard/SporeCoop/issues/148 (the full analysis, links and the scenario are there).

**Summary**

BUG-0116 (commits 4099ebc, dd3c506, ed70db7, release 0.7.3.0) promised one registry, `coop.fix`, for every fix switch, with a source scan that fails `make test` when a fix bool is left out. That promise is not kept:
- several switches are not registered, and the scan cannot find them;
- some switches turn off more than their own fix (`session.end`, `tribe.chiefs`, the updater's `g_old_fix`);
- `net_tests_asan --nofix` aborts partway, so most later fix tests are never seen failing under ASan;
- BUG-0047 has no switch at all;
- 29 tickets name switch names that `coop.fix` answers with "unknown fix".

Severity: low. It affects only tests and the lab: a twin run (`coop.fix X off`, `SPORE_FIX_OFF`, `coop.fix all off`) can miss a fix or show the wrong bug. Players are not affected.


## Root cause
Confirmed on b423f56: fixes_tests read only mods/coop/*.cpp, *.h and net/*.h and matched only `g_fix_x` / `x_fix` names, so struct members (`net::Fixes::async_resolve`), other names (`g_keep_posse`, `g_world_drop`), the inverted `HitTable::legacy` and the updater's bools were outside the registry. `session.end` off fell back to the per-path lists of before BUG-0033, which also skipped every later core reset (votes, shares, away state, DNA); `tribe.chiefs` returned early from clear_coop_tables and so also kept the co-op epic; `upd::g_old_fix` switched BUG-0114 and BUG-0150 together; `net_tests --nofix` turned off the DBBF bounds fix and died under ASan in test_dbbf_wrap; BUG-0047 had no switch.

## Fix
Registry rules R1-R9 of #148 (fixes.h, docs/06-testing.md#testing-fix-rules): `COOP_FIX_UNSAFE` (13 switches; `all off` skips them, `all off --unsafe` does not), one name per bool, a hint for unknown names (`session_end` -> `session.end`). `FIX_SWITCH` for portable files, registered in the new mods/coop/fix_table.cpp (the 8 net.* registrations moved there). New: `net.async_resolve`, `creations.world_drop`, `fleet.posse`, `team.hunt_hits` (`HitTable::legacy` inverted to `fix`, `set_legacy_hits` removed), `update.dup`, `update.old_suffix` + `update.keep_failed` (the split `g_old_fix`; win_tests `--old-suffix` / `--old-keep`); unit-test only `net.dbbf_bounds`, `net.refpack_bound` (new switch for BUG-0047) in kUnitOnly[]. `tribe.chiefs` gates only the chief and pincer tables. `end_session(EndPath, cell)` replaces end_session_modules: with `session.end` off only the module calls (block 1) and, on Restart, the remotes follow the old per-path lists; the core resets (block 3) always run. fixes_tests scans net/*.cpp and updater/ too, every FIX_SWITCH / net::Fixes member, marker comments, with an inline self-test and `--names`. net_tests --nofix runs to the end under ASan; `--old-dbbf` / `--old-refpack` must die there (make test-asan). Strict SPORE_FIX_OFF in twoinst. Tickets: `switch:` field on 108 fixed tickets, checked by tickets.py check.

## Verification
FAIL on b423f56: the new fixes_tests over b423f56's sources fails 9 times (g_world_drop, g_keep_posse, swap_ledger guard, g_bounds_fix, net::Fixes::async_resolve, g_dup_fix, g_pick_fix, g_release_lock_fix, g_old_fix); b423f56's `net_tests_asan --nofix` aborts with a heap-buffer-overflow (dbpf.cpp le32) before test_discovery_prune (logs not kept).
PASS on fix/w5-tools: fixes_tests ok (113 registrations); `net_tests_asan --nofix` FAILED (40) at the end, `--old-dbbf` / `--old-refpack` stopped by ASan; make test exit 0. Lab run out/lab/20261028-230704 (lane 7): vote_stale, creation_leave, leave_cleanup, join_name, tribe_epic, tribe_chief_removed, space_fleet ok; fix_registry failed on its own new alias check (coop.fleet answers "not in space" at the menu: test fixed), then out/lab/20261028-231456 fix_registry ok (113 switches, 13 unsafe, all off / --unsafe / on, hint, aliases).
Twins in out/lab/20261028-231529-w5-tools-twins (standalone runs, lane 7): leave_cleanup --without-fix FAIL (BUG-0033 still shown by `coop.sessionfix 0`); tribe_chief_removed --nofix FAIL (BUG-0081 still shown); vote_stale --without-fix FAIL; vote_stale with SPORE_FIX_OFF=session.end PASS (the vote is dropped although session.end is off: the decoupling); SPORE_FIX_OFF=no.such fails at once ("unknown fix no.such"). win_unit `--old-suffix` fails only the BUG-0114 checks, `--old-keep` only the BUG-0150 ones (standalone, lane 7).
Not run: vote_stale with session.end off on b423f56 (expected FAIL by reading: the old Leave path skipped drop_vote); tribe_epic with tribe.chiefs off (the test also checks the chief sizes, which that switch rightly breaks).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-tools (8124796 registry, 964b098 switch: fields). Other wave-5 branches that add a reset to the session end: put a coop.cpp reset into block 3 of end_session (after the `g_vote_scope_fix` line), and a switch in a portable file into fix_table.cpp.
- 2026-10-29: released in 0.7.3.1.
